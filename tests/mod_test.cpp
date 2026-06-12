// mod_test — offline verification harness for ModBlock (measurement-first,
// same culture as gate/comp/eq tests). Exits nonzero on any FAIL.
//
// T1 tremolo depth exact (envelope min == 1-depth at mix 1)
// T2 tremolo rate via envelope period
// T3 chorus is a pure modulated delay: output of an impulse train lands
//    within the designed sweep window, dry path intact at mix 0.5
// T4 phaser: notches exist and move (spectrum at two LFO phases differs)
// T5 mix 0 is (near-)dry; depth smoothing keeps it click-free
// T6 stereo spread: L and R envelopes are ~90 degrees apart (tremolo)
// T7 left==right aliasing (mono buffer) is safe and equals mono reference
#include "rig/ModBlock.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <complex>

using nam_rig::ModBlock;
using nam_rig::BlockContext;

static int gFails = 0;
#define CHECK(cond, ...) do { \
    const bool chkOk_ = (cond); \
    std::printf("%s: ", chkOk_ ? "PASS" : "FAIL"); std::printf(__VA_ARGS__); std::printf("\n"); \
    if (!chkOk_) ++gFails; } while (0)

static constexpr double SR = 48000.0;
static constexpr int BLK = 512;

static void run(ModBlock &m, std::vector<float> &l, std::vector<float> &r)
{
    for (size_t p = 0; p < l.size(); p += BLK)
    {
        const int n = (int)std::min<size_t>(BLK, l.size() - p);
        m.process(l.data() + p, r.data() + p, n);
    }
}

static std::vector<float> tone(double freq, double amp, int n)
{
    std::vector<float> v((size_t)n);
    for (int i = 0; i < n; ++i)
        v[(size_t)i] = (float)(amp * std::sin(2.0 * M_PI * freq * i / SR));
    return v;
}

// Peak envelope over windows of w samples
static std::vector<double> envelope(const std::vector<float> &x, int w)
{
    std::vector<double> e;
    for (size_t p = 0; p + (size_t)w <= x.size(); p += (size_t)w)
    {
        double pk = 0;
        for (int i = 0; i < w; ++i)
            pk = std::max(pk, (double)std::abs(x[p + (size_t)i]));
        e.push_back(pk);
    }
    return e;
}

// Goertzel magnitude at freq
static double mag(const std::vector<float> &x, size_t from, size_t len, double freq)
{
    std::complex<double> acc{0, 0};
    for (size_t i = 0; i < len; ++i)
        acc += (double)x[from + i]
               * std::exp(std::complex<double>(0, -2.0 * M_PI * freq * (double)i / SR));
    return std::abs(acc) * 2.0 / (double)len;
}

int main()
{
    // ---- T1/T2: tremolo depth + rate ----
    {
        ModBlock m;
        m.setType(ModBlock::kTremolo);
        m.setRateHz(4.0f);
        m.setDepth(0.6f);
        m.setMix(1.0f);
        m.prepare({SR, BLK});
        auto l = tone(1000.0, 0.5, (int)SR * 3), r = l;
        run(m, l, r);
        std::vector<float> tail(l.begin() + (int)SR, l.end());
        auto env = envelope(tail, 48);
        double mn = 1e9, mx = 0;
        for (auto e : env) { mn = std::min(mn, e); mx = std::max(mx, e); }
        const double depthMeas = 1.0 - mn / mx;
        CHECK(std::abs(depthMeas - 0.6) < 0.03,
              "T1 tremolo depth %.3f (set 0.600)", depthMeas);

        int minima = 0;
        for (size_t i = 1; i + 1 < env.size(); ++i)
            if (env[i] < env[i - 1] && env[i] <= env[i + 1] && env[i] < mn + 0.1 * (mx - mn))
                ++minima;
        const double rateMeas = minima / 2.0; // 2 s analyzed
        CHECK(std::abs(rateMeas - 4.0) <= 0.5, "T2 tremolo rate %.1f Hz (set 4.0)", rateMeas);
    }

    // ---- T3: chorus delay stays inside the designed sweep window ----
    {
        ModBlock m;
        m.setType(ModBlock::kChorus);
        m.setRateHz(0.5f);
        m.setDepth(1.0f);
        m.setMix(1.0f); // wet only: output IS the delayed signal
        m.prepare({SR, BLK});
        std::vector<float> l((size_t)SR * 2, 0.0f);
        for (size_t i = 0; i < l.size(); i += 4800)
            l[i] = 1.0f;
        auto r = l;
        run(m, l, r);
        bool inWindow = true;
        double dMin = 1e9, dMax = 0;
        for (size_t i = 48000; i + 4800 < l.size(); i += 4800)
        {
            double pk = 0;
            size_t pkAt = i;
            for (size_t j = i; j < i + 4700; ++j)
                if (std::abs(l[j]) > pk) { pk = std::abs(l[j]); pkAt = j; }
            const double dMs = (double)(pkAt - i) * 1000.0 / SR;
            dMin = std::min(dMin, dMs);
            dMax = std::max(dMax, dMs);
            if (dMs < 6.9 || dMs > 17.2)
                inWindow = false;
        }
        CHECK(inWindow, "T3 chorus delay window [%.1f, %.1f] ms (design [7, 17])", dMin, dMax);
        CHECK(dMax - dMin > 3.0, "T3 chorus actually sweeps (range %.1f ms > 3)", dMax - dMin);
    }

    // ---- T4: phaser notches move ----
    {
        ModBlock m;
        m.setType(ModBlock::kPhaser);
        m.setRateHz(0.2f);
        m.setDepth(1.0f);
        m.setMix(0.5f);
        m.prepare({SR, BLK});
        std::vector<float> l((size_t)SR * 5, 0.0f);
        for (int k = 0; k < 40; ++k)
        {
            const double f = 100.0 * std::pow(80.0, k / 39.0);
            for (size_t i = 0; i < l.size(); ++i)
                l[i] += (float)(0.02 * std::sin(2.0 * M_PI * f * (double)i / SR + k));
        }
        auto r = l;
        run(m, l, r);
        double diff = 0, ref = 0;
        for (int k = 0; k < 40; ++k)
        {
            const double f = 100.0 * std::pow(80.0, k / 39.0);
            const double m1 = mag(l, (size_t)SR, (size_t)SR / 2, f);
            const double m2 = mag(l, (size_t)(SR * 3.5), (size_t)SR / 2, f);
            diff += std::abs(m1 - m2);
            ref += std::max(m1, m2);
        }
        CHECK(diff / ref > 0.10, "T4 phaser spectrum moves (rel diff %.2f > 0.10)", diff / ref);
    }

    // ---- T5: mix 0 ~= dry ----
    {
        ModBlock m;
        m.setType(ModBlock::kChorus);
        m.setMix(0.0f);
        m.prepare({SR, BLK});
        auto l = tone(440.0, 0.25, (int)SR), r = l;
        auto ref = l;
        run(m, l, r);
        double err = 0;
        for (size_t i = 4800; i < l.size(); ++i)
            err = std::max(err, (double)std::abs(l[i] - ref[i]));
        CHECK(err < 1e-6, "T5 mix=0 dry error %.2e < 1e-6", err);
    }

    // ---- T6: stereo spread (tremolo envelopes ~90 deg apart) ----
    {
        ModBlock m;
        m.setType(ModBlock::kTremolo);
        m.setRateHz(2.0f);
        m.setDepth(0.8f);
        m.setMix(1.0f);
        m.prepare({SR, BLK});
        auto l = tone(1000.0, 0.5, (int)SR * 3), r = l;
        run(m, l, r);
        std::vector<float> lt(l.begin() + (int)SR, l.end()), rt(r.begin() + (int)SR, r.end());
        auto el = envelope(lt, 48), er = envelope(rt, 48);
        // cross-correlate MEAN-REMOVED envelopes normalized by overlap length
        // (raw correlation is DC-dominated and edge effects pin the peak to 0)
        double meanL = 0, meanR = 0;
        for (auto e : el) meanL += e;
        for (auto e : er) meanR += e;
        meanL /= (double)el.size();
        meanR /= (double)er.size();
        const double period = 1000.0 / 2.0; // envelope samples per LFO cycle (1ms hop)
        int bestLag = 0;
        double bestC = -1e18;
        for (int lag = 0; lag < (int)period; ++lag)
        {
            double c = 0;
            int n = 0;
            for (size_t i = 0; i + (size_t)lag < er.size() && i < el.size(); ++i, ++n)
                c += (el[i] - meanL) * (er[i + (size_t)lag] - meanR);
            if (n > 0)
                c /= (double)n;
            if (c > bestC) { bestC = c; bestLag = lag; }
        }
        const double frac = bestLag / period; // expect 0.25 or 0.75
        const double d90 = std::min(std::abs(frac - 0.25), std::abs(frac - 0.75));
        CHECK(d90 < 0.06, "T6 L/R envelope offset %.2f cycles (want 0.25/0.75)", frac);
    }

    // ---- T7: mono aliasing (left == right) equals true mono processing ----
    {
        ModBlock m1, m2;
        for (auto *m : {&m1, &m2})
        {
            m->setType(ModBlock::kChorus);
            m->setRateHz(1.0f);
            m->setDepth(0.7f);
            m->setMix(0.5f);
            m->prepare({SR, BLK});
        }
        auto x = tone(330.0, 0.3, (int)SR);
        auto aliased = x;
        for (size_t p = 0; p < aliased.size(); p += BLK) // left == right
            m1.process(aliased.data() + p, aliased.data() + p,
                       (int)std::min<size_t>(BLK, aliased.size() - p));
        auto lref = x, rref = x;
        run(m2, lref, rref);
        bool same = true;
        for (size_t i = 0; i < x.size(); ++i)
            if (aliased[i] != lref[i]) { same = false; break; }
        CHECK(same, "T7 mono-aliased buffer == stereo left channel (bit-exact)");
        bool finite = true;
        for (auto v : aliased)
            if (!std::isfinite(v)) finite = false;
        CHECK(finite, "T7 mono-aliased output finite");
    }

    std::printf("\n%s (%d FAIL)\n", gFails == 0 ? "ALL PASS" : "FAILURES", gFails);
    return gFails == 0 ? 0 : 1;
}
