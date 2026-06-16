// mod_test — offline verification harness for the modulation section
// (measurement-first). Exits nonzero on any FAIL.
//
// ModVoice = one voiced effect; ModBlock = 3-slot series section.
//   T1  tremolo depth (capped to its voiced musical maximum)
//   T2  tremolo rate via envelope period
//   T3  chorus stays inside its designed sweep window
//   T4  phaser notches move (hardwired sweep)
//   T5  mix 0 is dry
//   T6  stereo spread ~90 deg (tremolo, Width 1)
//   T7  mono-aliased buffer == stereo left (bit-exact)
//   T8  tempo sync resolves rate from BPM + division
//   T9  section = voices in series (1 slot == single voice; 2nd slot stacks)
//   T10 new types finite + non-silent
//   T11 harmonic tremolo bands anti-correlated
//   T12 Width controls the stereo spread
//   T13 per-effect voicing constants (depthMax / bakedBbd / authenticWave)
//   T14 tremolo at full depth keeps headroom (never dead silence / square)
//   T15 only Tremolo honours the Shape waveform; others hardwire their LFO
//   T16 flanger stays bounded at max feedback (incl. Invert on)
//   T17 ZDF/TPT phaser: bounded at high feedback + resonance grows with feedback
//   T18 flanger Manual shifts the static comb position
//   T19 flanger Invert flips the comb polarity (notch <-> peak)
//   T20 free Rate capped per effect (flanger 20 Hz, others 10 Hz; sync uncapped)
//   T21 Bi-Phase finite + non-silent (parallel & series)
//   T22 Bi-Phase parallel stereo A/B split scales with Width
//   T23 Bi-Phase resonance grows with feedback (impulse ring)
//   T24 Bi-Phase second sweep generator (P2Ratio) changes the motion
#include "rig/ModBlock.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <complex>

using nam_rig::BlockContext;
using nam_rig::Lfo;
using nam_rig::ModBlock;
using nam_rig::ModVoice;

static int gFails = 0;
#define CHECK(cond, ...) do { \
    const bool chkOk_ = (cond); \
    std::printf("%s: ", chkOk_ ? "PASS" : "FAIL"); std::printf(__VA_ARGS__); std::printf("\n"); \
    if (!chkOk_) ++gFails; } while (0)

static constexpr double SR = 48000.0;
static constexpr int BLK = 512;

template <class M>
static void run(M &m, std::vector<float> &l, std::vector<float> &r)
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

static double mag(const std::vector<float> &x, size_t from, size_t len, double freq)
{
    std::complex<double> acc{0, 0};
    for (size_t i = 0; i < len; ++i)
        acc += (double)x[from + i]
               * std::exp(std::complex<double>(0, -2.0 * M_PI * freq * (double)i / SR));
    return std::abs(acc) * 2.0 / (double)len;
}

// run a single voice over a 1 s 1 kHz tone, return the left channel
static std::vector<float> runWave(int type, int wave, float depth)
{
    ModVoice m;
    m.setType(type);
    m.setRateHz(2.0f);
    m.setDepth(depth);
    m.setMix(1.0f);
    m.setWaveform(wave);
    m.prepare({SR, BLK});
    auto l = tone(1000.0, 0.5, (int)SR), r = l;
    run(m, l, r);
    return l;
}

int main()
{
    // ---- T1: tremolo depth, capped to its voiced maximum ----
    {
        ModVoice m;
        m.setType(ModVoice::kTremolo);
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
        const double meas = 1.0 - mn / mx;
        const double want = 0.6 * ModVoice::depthMax(ModVoice::kTremolo);
        CHECK(std::abs(meas - want) < 0.04, "T1 tremolo depth %.3f (voiced want %.3f)", meas, want);
    }

    // ---- T2: tremolo rate ----
    {
        ModVoice m;
        m.setType(ModVoice::kTremolo);
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
        int minima = 0;
        for (size_t i = 1; i + 1 < env.size(); ++i)
            if (env[i] < env[i - 1] && env[i] <= env[i + 1] && env[i] < mn + 0.1 * (mx - mn))
                ++minima;
        const double rate = minima / 2.0;
        CHECK(std::abs(rate - 4.0) <= 0.5, "T2 tremolo rate %.1f Hz (set 4.0)", rate);
    }

    // ---- T3: chorus stays inside its sweep window ----
    {
        ModVoice m;
        m.setType(ModVoice::kChorus);
        m.setRateHz(0.5f);
        m.setDepth(1.0f);
        m.setMix(1.0f);
        m.prepare({SR, BLK});
        std::vector<float> l((size_t)SR * 2, 0.0f);
        for (size_t i = 0; i < l.size(); i += 4800)
            l[i] = 1.0f;
        auto r = l;
        run(m, l, r);
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
        }
        CHECK(dMin > 6.0 && dMax < 18.0, "T3 chorus delay window [%.1f, %.1f] ms (design [7,17])", dMin, dMax);
        CHECK(dMax - dMin > 3.0, "T3 chorus sweeps (range %.1f ms > 3)", dMax - dMin);
    }

    // ---- T4: phaser notches move ----
    {
        ModVoice m;
        m.setType(ModVoice::kPhaser);
        m.setRateHz(0.2f);
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
        ModVoice m;
        m.setType(ModVoice::kChorus);
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

    // ---- T6: stereo spread ~90 deg (tremolo, Width 1) ----
    {
        ModVoice m;
        m.setType(ModVoice::kTremolo);
        m.setRateHz(2.0f);
        m.setDepth(0.8f);
        m.setMix(1.0f);
        m.prepare({SR, BLK});
        auto l = tone(1000.0, 0.5, (int)SR * 3), r = l;
        run(m, l, r);
        std::vector<float> lt(l.begin() + (int)SR, l.end()), rt(r.begin() + (int)SR, r.end());
        auto el = envelope(lt, 48), er = envelope(rt, 48);
        double mL = 0, mR = 0;
        for (auto e : el) mL += e;
        for (auto e : er) mR += e;
        mL /= (double)el.size();
        mR /= (double)er.size();
        const double period = 1000.0 / 2.0;
        int bestLag = 0;
        double bestC = -1e18;
        for (int lag = 0; lag < (int)period; ++lag)
        {
            double c = 0;
            int n = 0;
            for (size_t i = 0; i + (size_t)lag < er.size() && i < el.size(); ++i, ++n)
                c += (el[i] - mL) * (er[i + (size_t)lag] - mR);
            if (n > 0) c /= (double)n;
            if (c > bestC) { bestC = c; bestLag = lag; }
        }
        const double frac = bestLag / period;
        const double d90 = std::min(std::abs(frac - 0.25), std::abs(frac - 0.75));
        CHECK(d90 < 0.06, "T6 L/R envelope offset %.2f cycles (want 0.25/0.75)", frac);
    }

    // ---- T7: mono aliasing (left == right) equals stereo left ----
    {
        ModVoice m1, m2;
        for (auto *m : {&m1, &m2})
        {
            m->setType(ModVoice::kChorus);
            m->setRateHz(1.0f);
            m->setDepth(0.7f);
            m->setMix(0.5f);
            m->prepare({SR, BLK});
        }
        auto x = tone(330.0, 0.3, (int)SR);
        auto aliased = x;
        for (size_t p = 0; p < aliased.size(); p += BLK)
            m1.process(aliased.data() + p, aliased.data() + p,
                       (int)std::min<size_t>(BLK, aliased.size() - p));
        auto lref = x, rref = x;
        run(m2, lref, rref);
        bool same = true;
        for (size_t i = 0; i < x.size(); ++i)
            if (aliased[i] != lref[i]) { same = false; break; }
        CHECK(same, "T7 mono-aliased buffer == stereo left (bit-exact)");
    }

    // ---- T8: tempo sync ----
    {
        ModVoice m;
        m.setBpm(120.0);
        m.setRateHz(0.1f);
        m.setSyncIndex(3); // 1/4 = 1 beat -> 2 Hz
        CHECK(std::abs(m.effectiveRateHz() - 2.0f) < 0.01f, "T8 sync 1/4 @120 -> %.3f Hz", m.effectiveRateHz());
        m.setSyncIndex(6); // 1/8 -> 4 Hz
        CHECK(std::abs(m.effectiveRateHz() - 4.0f) < 0.01f, "T8 sync 1/8 @120 -> %.3f Hz", m.effectiveRateHz());
        m.setSyncIndex(0);
        CHECK(std::abs(m.effectiveRateHz() - 0.1f) < 1e-4f, "T8 sync Off -> free %.3f", m.effectiveRateHz());
    }

    // ---- T9: section = voices in series ----
    {
        ModBlock sec;
        sec.setType(0, ModVoice::kTremolo);
        sec.setRateHz(0, 4.0f);
        sec.setDepth(0, 0.6f);
        sec.setMix(0, 1.0f);
        sec.setSlotBypassed(0, false);
        for (int s = 1; s < ModBlock::kSlots; ++s)
            sec.setSlotBypassed(s, true);
        sec.prepare({SR, BLK});

        ModVoice one;
        one.setType(ModVoice::kTremolo);
        one.setRateHz(4.0f);
        one.setDepth(0.6f);
        one.setMix(1.0f);
        one.prepare({SR, BLK});

        auto x = tone(1000.0, 0.5, (int)SR);
        auto la = x, ra = x, lb = x, rb = x;
        run(sec, la, ra);
        run(one, lb, rb);
        bool same = true;
        for (size_t i = 0; i < x.size(); ++i)
            if (la[i] != lb[i]) { same = false; break; }
        CHECK(same, "T9 section 1-slot == single ModVoice (bit-exact)");

        auto makeChorus = [](ModBlock &m) {
            m.setType(0, ModVoice::kChorus);
            m.setRateHz(0, 1.0f);
            m.setDepth(0, 0.7f);
            m.setMix(0, 1.0f);
            m.setSlotBypassed(0, false);
            m.setSlotBypassed(1, true);
            m.setSlotBypassed(2, true);
        };
        ModBlock chorusOnly, stacked;
        makeChorus(chorusOnly);
        makeChorus(stacked);
        stacked.setType(1, ModVoice::kTremolo);
        stacked.setRateHz(1, 5.0f);
        stacked.setDepth(1, 0.8f);
        stacked.setMix(1, 1.0f);
        stacked.setSlotBypassed(1, false);
        chorusOnly.prepare({SR, BLK});
        stacked.prepare({SR, BLK});
        auto l1 = x, r1 = x, l2 = x, r2 = x;
        run(chorusOnly, l1, r1);
        run(stacked, l2, r2);
        double d = 0;
        for (size_t i = 0; i < x.size(); ++i)
            d = std::max(d, (double)std::abs(l2[i] - l1[i]));
        CHECK(d > 0.01, "T9 second slot stacks in series (max diff %.3f)", d);
    }

    // ---- T10: new types finite + non-silent ----
    {
        bool ok = true;
        for (int t : {ModVoice::kVibrato, ModVoice::kRotary, ModVoice::kUniVibe, ModVoice::kHarmTrem})
        {
            ModVoice m;
            m.setType(t);
            m.setRateHz(2.0f);
            m.setDepth(0.7f);
            m.setMix(1.0f);
            m.prepare({SR, BLK});
            auto l = tone(440.0, 0.3, (int)SR), r = l;
            run(m, l, r);
            double e = 0;
            bool fin = true;
            for (float v : l) { if (!std::isfinite(v)) fin = false; e += (double)v * v; }
            if (!fin || e < 1e-3) ok = false;
        }
        CHECK(ok, "T10 new types (vibrato/rotary/uni-vibe/harm-trem) finite + non-silent");
    }

    // ---- T11: harmonic tremolo bands anti-correlated ----
    {
        ModVoice m;
        m.setType(ModVoice::kHarmTrem);
        m.setRateHz(3.0f);
        m.setDepth(0.9f);
        m.setMix(1.0f);
        m.prepare({SR, BLK});
        std::vector<float> l((size_t)SR * 2);
        for (size_t i = 0; i < l.size(); ++i)
            l[i] = (float)(0.3 * std::sin(2.0 * M_PI * 200.0 * (double)i / SR) +
                           0.3 * std::sin(2.0 * M_PI * 3000.0 * (double)i / SR));
        auto r = l;
        run(m, l, r);
        std::vector<double> eLo, eHi;
        for (size_t p = (size_t)SR; p + 480 <= l.size(); p += 480)
        {
            eLo.push_back(mag(l, p, 480, 200.0));
            eHi.push_back(mag(l, p, 480, 3000.0));
        }
        double mL = 0, mH = 0;
        for (size_t i = 0; i < eLo.size(); ++i) { mL += eLo[i]; mH += eHi[i]; }
        mL /= (double)eLo.size();
        mH /= (double)eHi.size();
        double cov = 0, vL = 0, vH = 0;
        for (size_t i = 0; i < eLo.size(); ++i)
        {
            const double a = eLo[i] - mL, b = eHi[i] - mH;
            cov += a * b; vL += a * a; vH += b * b;
        }
        const double corr = cov / (std::sqrt(vL * vH) + 1e-12);
        CHECK(corr < -0.3, "T11 harmonic trem bands anti-correlated (corr %.2f)", corr);
    }

    // ---- T12: Width controls the stereo spread ----
    {
        auto spread = [](float width) {
            ModVoice m;
            m.setType(ModVoice::kTremolo);
            m.setRateHz(2.0f);
            m.setDepth(0.8f);
            m.setMix(1.0f);
            m.setWidth(width);
            m.prepare({SR, BLK});
            auto l = tone(1000.0, 0.5, (int)SR), r = l;
            run(m, l, r);
            double d = 0;
            for (size_t i = 4800; i < l.size(); ++i)
                d = std::max(d, (double)std::abs(l[i] - r[i]));
            return d;
        };
        CHECK(spread(0.0f) < 1e-6, "T12 width 0 -> L==R (max diff %.2e)", spread(0.0f));
        CHECK(spread(1.0f) > 0.05, "T12 width 1 -> spread (max diff %.3f)", spread(1.0f));
    }

    // ---- T13: per-effect voicing constants ----
    {
        const bool ok =
            ModVoice::depthMax(ModVoice::kTremolo) < 1.0f &&
            ModVoice::depthMax(ModVoice::kChorus) == 1.0f &&
            ModVoice::bakedBbd(ModVoice::kChorus) > 0.0f &&
            ModVoice::bakedBbd(ModVoice::kPhaser) == 0.0f &&
            ModVoice::authenticWave(ModVoice::kFlanger) == Lfo::Triangle &&
            ModVoice::authenticWave(ModVoice::kChorus) == Lfo::Sine &&
            ModVoice::mixFor(ModVoice::kFlanger, 1.0f) == 0.5f &&  // flanger Mix caps at 50/50
            ModVoice::mixFor(ModVoice::kFlanger, 0.0f) == 0.0f;    // ...and reaches fully dry
        CHECK(ok, "T13 voicing constants (tremolo capped, BBD baked on delay types, flanger=triangle, flanger mix caps 50/50)");
    }

    // ---- T14: tremolo at full depth keeps headroom (no dead silence/square) ----
    {
        ModVoice m;
        m.setType(ModVoice::kTremolo);
        m.setRateHz(4.0f);
        m.setDepth(1.0f);
        m.setMix(1.0f);
        m.prepare({SR, BLK});
        auto l = tone(1000.0, 0.5, (int)SR * 2), r = l;
        run(m, l, r);
        std::vector<float> tail(l.begin() + (int)SR, l.end());
        auto env = envelope(tail, 48);
        double mn = 1e9, mx = 0;
        for (auto e : env) { mn = std::min(mn, e); mx = std::max(mx, e); }
        CHECK(mn / mx > 0.10, "T14 full depth keeps headroom (min/max %.2f > 0.10 = no silence)", mn / mx);
    }

    // ---- T15: only Tremolo honours the Shape waveform ----
    {
        auto cs = runWave(ModVoice::kChorus, Lfo::Sine, 0.7f);
        auto cq = runWave(ModVoice::kChorus, Lfo::Square, 0.7f);
        bool chorusSame = true;
        for (size_t i = 0; i < cs.size(); ++i)
            if (cs[i] != cq[i]) { chorusSame = false; break; }
        auto ts = runWave(ModVoice::kTremolo, Lfo::Sine, 0.7f);
        auto tq = runWave(ModVoice::kTremolo, Lfo::Square, 0.7f);
        double td = 0;
        for (size_t i = 0; i < ts.size(); ++i)
            td = std::max(td, (double)std::abs(ts[i] - tq[i]));
        CHECK(chorusSame && td > 0.01,
              "T15 chorus hardwires LFO (sq==sine), tremolo honours Shape (diff %.3f)", td);
    }

    // ---- T16: flanger stays bounded at max feedback (tone-shaped regen) ----
    {
        ModVoice m;
        m.setType(ModVoice::kFlanger);
        m.setRateHz(0.3f);
        m.setDepth(0.8f);
        m.setFeedback(0.95f); // new Regen ceiling
        m.setMix(0.5f);
        m.prepare({SR, BLK});
        auto l = tone(220.0, 0.4, (int)SR * 2), r = l;
        run(m, l, r);
        bool fin = true;
        double pk = 0;
        for (float v : l) { if (!std::isfinite(v)) fin = false; pk = std::max(pk, (double)std::abs(v)); }
        CHECK(fin && pk < 4.0, "T16 flanger max feedback stays bounded (peak %.2f)", pk);

        // same, with Invert on (negative regen) -> must also stay bounded
        ModVoice mi;
        mi.setType(ModVoice::kFlanger);
        mi.setRateHz(0.3f);
        mi.setDepth(0.8f);
        mi.setFeedback(0.95f);
        mi.setInvert(true);
        mi.setMix(0.5f);
        mi.prepare({SR, BLK});
        auto li = tone(220.0, 0.4, (int)SR * 2), ri = li;
        run(mi, li, ri);
        bool fin2 = true;
        double pk2 = 0;
        for (float v : li) { if (!std::isfinite(v)) fin2 = false; pk2 = std::max(pk2, (double)std::abs(v)); }
        CHECK(fin2 && pk2 < 4.0, "T16 flanger invert + max feedback stays bounded (peak %.2f)", pk2);
    }

    // ---- T17: ZDF/TPT phaser — high feedback bounded; resonance grows with feedback ----
    {
        // Run the phaser over a 40-tone log-spaced bed (same construction as T4),
        // with the sweep almost frozen (slow rate) so the spectrum is ~stationary.
        auto runPhaser = [](float fb) {
            ModVoice m;
            m.setType(ModVoice::kPhaser);
            m.setRateHz(0.05f); // near-frozen sweep -> stable notch positions to measure
            m.setFeedback(fb);
            m.setMix(0.5f);
            m.prepare({SR, BLK});
            std::vector<float> l((size_t)SR * 2, 0.0f);
            for (int kk = 0; kk < 40; ++kk)
            {
                const double f = 100.0 * std::pow(80.0, kk / 39.0);
                for (size_t i = 0; i < l.size(); ++i)
                    l[i] += (float)(0.02 * std::sin(2.0 * M_PI * f * (double)i / SR + kk));
            }
            auto r = l;
            run(m, l, r);
            return l;
        };

        // (a) unconditional stability: full-up feedback stays finite + bounded
        auto hi = runPhaser(0.99f);
        bool fin = true;
        double pk = 0;
        for (float v : hi) { if (!std::isfinite(v)) fin = false; pk = std::max(pk, (double)std::abs(v)); }
        CHECK(fin && pk < 4.0, "T17 phaser high feedback stays bounded (peak %.2f)", pk);

        // (b) resonance: feedback pushes the loop poles toward the unit circle, so
        // the network rings. Freeze the sweep (rate 0 -> stationary fc) and feed an
        // impulse; the decay-tail energy must grow monotonically with feedback.
        // (A spectral-probe metric is unreliable here: the resonant peaks are
        // narrow and fall between coarse probe tones.)
        auto tailEnergy = [](float fb) {
            ModVoice m;
            m.setType(ModVoice::kPhaser);
            m.setRateHz(0.0f); // frozen sweep -> stationary resonant frequency
            m.setFeedback(fb);
            m.setMix(0.5f);
            m.prepare({SR, BLK});
            std::vector<float> l((size_t)SR / 4, 0.0f); // 0.25 s
            l[0] = 1.0f;                                // impulse
            auto r = l;
            run(m, l, r);
            double e = 0;
            for (size_t i = 400; i < l.size(); ++i) // skip the direct transient
                e += (double)l[i] * (double)l[i];
            return e;
        };
        const double e0 = tailEnergy(0.0f);
        const double e4 = tailEnergy(0.4f);
        const double e8 = tailEnergy(0.8f);
        CHECK(e0 < e4 && e4 < e8 && e8 > 1e-6,
              "T17 phaser rings more with feedback (tail energy %.2e < %.2e < %.2e)", e0, e4, e8);
    }

    // ---- T18: flanger Manual shifts the static comb position ----
    {
        // depth 0 -> no sweep, so the delay = Manual base only. The delayed copy
        // shows up as a secondary peak in the impulse response; its time = delay.
        auto combDelayMs = [](float manual) {
            ModVoice m;
            m.setType(ModVoice::kFlanger);
            m.setDepth(0.0f);     // static comb (no sweep)
            m.setFeedback(0.0f);
            m.setManual(manual);
            m.setMix(0.5f);
            m.prepare({SR, BLK});
            std::vector<float> l((size_t)SR / 10, 0.0f);
            l[0] = 1.0f;          // impulse
            auto r = l;
            run(m, l, r);
            size_t pkAt = 0;
            double pk = 0;
            for (size_t i = 8; i < l.size(); ++i) // skip the dry impulse
                if (std::abs(l[i]) > pk) { pk = std::abs(l[i]); pkAt = i; }
            return (double)pkAt * 1000.0 / SR;
        };
        const double dLo = combDelayMs(0.0f); // ~kFlMinMs (0.5 ms)
        const double dHi = combDelayMs(1.0f); // ~kFlManualMaxMs (8 ms)
        CHECK(dHi > dLo + 2.0, "T18 Manual moves the comb (delay %.2f -> %.2f ms)", dLo, dHi);
    }

    // ---- T19: flanger Invert flips the comb polarity (notch <-> peak) ----
    {
        // Static comb; invert negates the delayed path, so dry+wet becomes dry-wet
        // -> peaks and notches swap. Per-frequency magnitudes should anti-correlate.
        auto combMags = [](bool inv) {
            ModVoice m;
            m.setType(ModVoice::kFlanger);
            m.setDepth(0.0f);
            m.setFeedback(0.0f);
            m.setManual(0.5f); // mid delay -> several notches across the probe band
            m.setInvert(inv);
            m.setMix(0.5f);
            m.prepare({SR, BLK});
            std::vector<float> l((size_t)SR, 0.0f);
            for (int kk = 0; kk < 40; ++kk)
            {
                const double f = 200.0 + kk * 55.0; // 200..2345 Hz
                for (size_t i = 0; i < l.size(); ++i)
                    l[i] += (float)(0.02 * std::sin(2.0 * M_PI * f * (double)i / SR + kk));
            }
            auto r = l;
            run(m, l, r);
            std::vector<double> mags;
            for (int kk = 0; kk < 40; ++kk)
                mags.push_back(mag(l, (size_t)SR / 2, (size_t)SR / 2, 200.0 + kk * 55.0));
            return mags;
        };
        auto off = combMags(false), on = combMags(true);
        double mo = 0, mn = 0;
        for (size_t i = 0; i < off.size(); ++i) { mo += off[i]; mn += on[i]; }
        mo /= (double)off.size();
        mn /= (double)on.size();
        double cov = 0, vo = 0, vn = 0;
        for (size_t i = 0; i < off.size(); ++i)
        {
            const double a = off[i] - mo, b = on[i] - mn;
            cov += a * b; vo += a * a; vn += b * b;
        }
        const double corr = cov / (std::sqrt(vo * vn) + 1e-12);
        CHECK(corr < -0.3, "T19 flanger Invert flips comb polarity (corr %.2f < -0.3)", corr);
    }

    // ---- T20: free Rate is capped per effect (flanger 20 Hz, others 10 Hz) ----
    {
        ModVoice f;
        f.setType(ModVoice::kFlanger);
        f.setRateHz(20.0f);
        CHECK(std::abs(f.effectiveRateHz() - 20.0f) < 0.01f,
              "T20 flanger free rate reaches 20 Hz (%.2f)", f.effectiveRateHz());

        ModVoice c;
        c.setType(ModVoice::kChorus);
        c.setRateHz(20.0f);
        CHECK(std::abs(c.effectiveRateHz() - 10.0f) < 0.01f,
              "T20 non-flanger free rate capped at 10 Hz (%.2f)", c.effectiveRateHz());

        // sync ignores the cap (honours the host division): 1/16 @240 BPM = 16 Hz
        ModVoice s;
        s.setType(ModVoice::kChorus);
        s.setBpm(240.0);
        s.setSyncIndex(9);
        CHECK(s.effectiveRateHz() > 10.0f, "T20 synced rate not capped (%.2f Hz)", s.effectiveRateHz());
    }

    // ---- T21: Bi-Phase finite + non-silent (parallel and series) ----
    {
        bool ok = true;
        for (bool series : {false, true})
        {
            ModVoice m;
            m.setType(ModVoice::kBiPhase);
            m.setRateHz(2.0f);
            m.setDepth(0.7f);
            m.setFeedback(0.5f);
            m.setP2Ratio(1.5f);
            m.setSeries(series);
            m.prepare({SR, BLK});
            auto l = tone(440.0, 0.3, (int)SR), r = l;
            run(m, l, r);
            double e = 0;
            bool fin = true;
            for (float v : l) { if (!std::isfinite(v)) fin = false; e += (double)v * v; }
            if (!fin || e < 1e-3) ok = false;
        }
        CHECK(ok, "T21 Bi-Phase finite + non-silent (parallel & series)");
    }

    // ---- T22: Bi-Phase parallel stereo A/B split scales with Width ----
    {
        auto spread = [](float width) {
            ModVoice m;
            m.setType(ModVoice::kBiPhase);
            m.setRateHz(2.0f);
            m.setDepth(0.7f);
            m.setP2Ratio(1.5f);
            m.setSeries(false); // parallel
            m.setWidth(width);
            m.prepare({SR, BLK});
            auto l = tone(1000.0, 0.5, (int)SR), r = l;
            run(m, l, r);
            double d = 0;
            for (size_t i = 4800; i < l.size(); ++i)
                d = std::max(d, (double)std::abs(l[i] - r[i]));
            return d;
        };
        CHECK(spread(0.0f) < 1e-6, "T22 Bi-Phase width 0 -> L==R (max diff %.2e)", spread(0.0f));
        CHECK(spread(1.0f) > 0.02, "T22 Bi-Phase width 1 -> A/B stereo split (max diff %.3f)", spread(1.0f));
    }

    // ---- T23: Bi-Phase resonance grows with feedback (impulse ring, frozen sweep) ----
    {
        auto tailEnergy = [](float fb) {
            ModVoice m;
            m.setType(ModVoice::kBiPhase);
            m.setRateHz(0.0f); // frozen sweep -> stationary resonant frequency
            m.setDepth(0.7f);
            m.setFeedback(fb);
            m.setSeries(false);
            m.prepare({SR, BLK});
            std::vector<float> l((size_t)SR / 4, 0.0f);
            l[0] = 1.0f; // impulse
            auto r = l;
            run(m, l, r);
            double e = 0;
            for (size_t i = 400; i < l.size(); ++i)
                e += (double)l[i] * (double)l[i];
            return e;
        };
        const double e0 = tailEnergy(0.0f), e4 = tailEnergy(0.4f), e8 = tailEnergy(0.8f);
        CHECK(e0 < e4 && e4 < e8 && e8 > 1e-6,
              "T23 Bi-Phase rings more with feedback (tail %.2e < %.2e < %.2e)", e0, e4, e8);
    }

    // ---- T24: Bi-Phase second sweep generator (P2Ratio) changes the motion ----
    {
        auto runRatio = [](float ratio) {
            ModVoice m;
            m.setType(ModVoice::kBiPhase);
            m.setRateHz(2.0f);
            m.setDepth(0.7f);
            m.setP2Ratio(ratio);
            m.setSeries(false);
            m.setWidth(0.0f); // mono-sum both cores so Gen 2 (Core B) is in the measured channel
            m.prepare({SR, BLK});
            auto l = tone(1000.0, 0.5, (int)SR), r = l;
            run(m, l, r);
            return l;
        };
        auto a = runRatio(1.0f), b = runRatio(2.5f);
        double d = 0;
        for (size_t i = 4800; i < a.size(); ++i)
            d = std::max(d, (double)std::abs(a[i] - b[i]));
        CHECK(d > 0.02, "T24 Bi-Phase Gen 2 ratio changes the sweep (max diff %.3f)", d);
    }

    std::printf("\n%s (%d FAIL)\n", gFails == 0 ? "ALL PASS" : "FAILURES", gFails);
    return gFails == 0 ? 0 : 1;
}
