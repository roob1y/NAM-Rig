// cabdyn_test — offline verification harness for CabDynamicsBlock (the Dynamic
// Cab delta wrapper). JUCE-free: includes only rig/CabDynamicsBlock.h, which
// pulls rig/Biquad.h. Build (from repo root, alongside the other block tests):
//   g++ -std=c++17 -O2 -Wall -Wextra -Isrc tests/cabdyn_test.cpp -o cabdyn_test
// Exits nonzero on any FAIL.
//
// T1  bit-exact bypass — all macros 0 => memcmp(out,in)==0 for BOTH pre and post
// T2  aliasing bound — (a) halfband reconstruction + decimator stopband,
//     (b) hard-driven breakup has inharmonic (alias) energy < -55 dB vs fundamental
// T3  stability at max settings — full-scale noise stays finite/bounded; impulse decays
// T4  Cab-Size sweep artifact — steady tone, sweeping Size: no zipper/click, finite
// T5  determinism — identical input+params => identical output
// T6  envelope-driven, not static EQ — loud passage deviates A1, quiet barely does
// T7  modulation ceilings — measured A1/A2/enc/drive stay within the stated caps
// T8  engage->disengage is click-free and returns to bit-exact bypass
#include "rig/CabDynamicsBlock.h"
#include <cstdio>
#include <cmath>
#include <cstring>
#include <vector>
#include <random>

using nam_rig::CabDynamicsBlock;

static int gFails = 0;
#define CHECK(cond, ...) do { \
    const bool ok_ = (cond); \
    std::printf("%s: ", ok_ ? "PASS" : "FAIL"); std::printf(__VA_ARGS__); std::printf("\n"); \
    if (!ok_) ++gFails; } while (0)

static constexpr double SR = 48000.0;
static constexpr int BLK = 512;
static constexpr double kPi = 3.14159265358979323846;

static void runPre(CabDynamicsBlock &d, std::vector<float> &m)
{
    for (size_t p = 0; p < m.size(); p += BLK)
        d.processPre(m.data() + p, (int)std::min<size_t>(BLK, m.size() - p));
}
static void runPost(CabDynamicsBlock &d, std::vector<float> &m)
{
    for (size_t p = 0; p < m.size(); p += BLK)
        d.processPost(m.data() + p, (int)std::min<size_t>(BLK, m.size() - p));
}

// Hann-windowed Goertzel magnitude at f over x[start, start+N). The window
// suppresses the fundamental's spectral sidelobes so an alias bin isn't polluted
// by leakage from a strong nearby tone. Ratio use cancels the window gain.
static double goertzelW(const std::vector<float> &x, size_t start, size_t N, double f, double sr)
{
    const double w = 2.0 * kPi * f / sr;
    const double c = 2.0 * std::cos(w);
    double s1 = 0.0, s2 = 0.0;
    for (size_t n = 0; n < N; ++n)
    {
        const double hann = 0.5 - 0.5 * std::cos(2.0 * kPi * (double)n / (double)(N - 1));
        const double s0 = (double)x[start + n] * hann + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return std::sqrt(std::max(0.0, s1 * s1 + s2 * s2 - c * s1 * s2)) / (double)N;
}
static double goertzel(const std::vector<float> &x, size_t start, size_t N, double f)
{
    return goertzelW(x, start, N, f, SR);
}

int main()
{
    // ---------- T1: bit-exact bypass (all macros default 0) ----------
    {
        CabDynamicsBlock d;
        d.prepare(SR, BLK);
        std::mt19937 rng(1);
        std::uniform_real_distribution<float> u(-0.8f, 0.8f);
        std::vector<float> in((size_t)SR, 0.0f);
        for (auto &v : in) v = u(rng);
        std::vector<float> pre = in, post = in;
        runPre(d, pre);
        runPost(d, post);
        const bool preExact  = std::memcmp(pre.data(),  in.data(), in.size() * sizeof(float)) == 0;
        const bool postExact = std::memcmp(post.data(), in.data(), in.size() * sizeof(float)) == 0;
        CHECK(preExact,  "T1a processPre bit-exact bypass (byte-identical)");
        CHECK(postExact, "T1b processPost bit-exact bypass (byte-identical)");
    }

    // ---------- T2a: halfband reconstruction + decimator stopband ----------
    {
        CabDynamicsBlock::Halfband hb;
        hb.design();
        // 1 kHz up->(identity)->down: passband gain ~1 (reconstruction sane)
        const size_t N = 8192;
        std::vector<float> out(N, 0.0f), ref(N);
        for (size_t n = 0; n < N; ++n)
        {
            const float x = (float)std::sin(2.0 * kPi * 1000.0 * (double)n / SR);
            ref[n] = x;
            float a, b;
            hb.up(x, a, b);
            out[n] = hb.down(a, b);
        }
        const double gPass = goertzel(out, 2048, 4096, 1000.0) / goertzel(ref, 2048, 4096, 1000.0);
        CHECK(std::fabs(20.0 * std::log10(gPass)) < 0.5,
              "T2a halfband 1 kHz passband gain %.3f dB (|.|<0.5)", 20.0 * std::log10(gPass));

        // Decimator stopband: a 30 kHz tone at the 2x rate (96 k) is above the
        // 24 kHz base-Nyquist, so down() MUST reject it — this is exactly the
        // content the clipper produces that would otherwise fold/alias.
        hb.reset();
        const size_t M = 8192;
        std::vector<float> u2x(M), dout(M / 2, 0.0f);
        for (size_t m = 0; m < M; ++m) u2x[m] = (float)std::sin(2.0 * kPi * 30000.0 * (double)m / (2.0 * SR));
        for (size_t n = 0; n < M / 2; ++n) dout[n] = hb.down(u2x[2 * n], u2x[2 * n + 1]);
        double outmax = 0.0;
        for (size_t n = 512; n < M / 2; ++n) outmax = std::max(outmax, (double)std::fabs(dout[n]));
        CHECK(20.0 * std::log10(outmax + 1e-12) < -55.0,
              "T2a decimator 30 kHz stopband %.1f dB (< -55, input 0 dB)", 20.0 * std::log10(outmax + 1e-12));
    }

    // ---------- T2b: hard-driven breakup aliasing bound ----------
    {
        CabDynamicsBlock d;
        d.prepare(SR, BLK);
        d.setAgeDrive(1.0f); // max breakup drive
        const double f0 = 3300.0;
        const size_t N = (size_t)SR; // 1 s
        std::vector<float> m(N);
        for (size_t n = 0; n < N; ++n) m[n] = 0.9f * (float)std::sin(2.0 * kPi * f0 * (double)n / SR);
        runPre(d, m);
        const size_t start = N / 2, win = N / 4;
        const double fund = goertzel(m, start, win, f0);
        double maxInharm = 0.0, atF = 0.0;
        for (double f = 200.0; f < 20000.0; f += 50.0)
        {
            bool harmonic = false;
            for (int k = 1; k <= 8; ++k)
                if (std::fabs(f - k * f0) < 150.0) { harmonic = true; break; }
            if (harmonic) continue;
            const double mag = goertzel(m, start, win, f);
            if (mag > maxInharm) { maxInharm = mag; atF = f; }
        }
        const double dB = 20.0 * std::log10((maxInharm + 1e-15) / (fund + 1e-15));
        CHECK(dB < -55.0, "T2b worst inharmonic/alias %.1f dB @ %.0f Hz (< -55)", dB, atF);
    }

    // ---------- T3: stability at max settings ----------
    {
        CabDynamicsBlock d;
        d.prepare(SR, BLK);
        d.setAgeDrive(1.0f);
        d.setThump(1.0f);
        d.setCabSize(1.0f);
        std::mt19937 rng(7);
        std::uniform_real_distribution<float> u(-1.0f, 1.0f);
        const size_t N = (size_t)(SR * 2.0);
        std::vector<float> m(N);
        for (auto &v : m) v = u(rng);
        runPre(d, m);
        runPost(d, m);
        bool finite = true; float peak = 0.0f;
        for (float v : m) { if (!std::isfinite(v)) finite = false; peak = std::max(peak, std::fabs(v)); }
        CHECK(finite, "T3a full-scale noise stays finite at max settings");
        CHECK(peak < 4.0f, "T3b output bounded (peak %.3f < 4.0)", peak);

        CabDynamicsBlock d2;
        d2.prepare(SR, BLK);
        d2.setThump(1.0f);
        d2.setCabSize(1.0f);
        std::vector<float> imp((size_t)SR, 0.0f);
        imp[0] = 1.0f;
        runPost(d2, imp);
        double tail = 0.0;
        for (size_t n = (size_t)(SR * 0.6); n < imp.size(); ++n) tail = std::max(tail, (double)std::fabs(imp[n]));
        CHECK(tail < 1.0e-3, "T3c enclosure impulse tail decays (max after 0.6s = %.2e)", tail);
    }

    // ---------- T4: Cab-Size sweep artifact check ----------
    {
        CabDynamicsBlock d;
        d.prepare(SR, BLK);
        d.setThump(1.0f);
        const size_t N = (size_t)SR;
        std::vector<float> m(N);
        for (size_t n = 0; n < N; ++n) m[n] = 0.5f * (float)std::sin(2.0 * kPi * 220.0 * (double)n / SR);
        for (size_t p = 0; p < N; p += BLK)
        {
            const int n = (int)std::min<size_t>(BLK, N - p);
            d.setCabSize((float)((double)p / (double)N));
            d.processPost(m.data() + p, n);
        }
        bool finite = true; float maxStep = 0.0f;
        for (size_t n = 1; n < N; ++n)
        {
            if (!std::isfinite(m[n])) finite = false;
            maxStep = std::max(maxStep, std::fabs(m[n] - m[n - 1]));
        }
        CHECK(finite, "T4a Cab-Size sweep stays finite");
        CHECK(maxStep < 0.15f, "T4b no zipper/click on Size sweep (max sample step %.4f < 0.15)", maxStep);
    }

    // ---------- T5: determinism ----------
    {
        auto make = []() {
            CabDynamicsBlock d; d.prepare(SR, BLK);
            d.setAgeDrive(0.7f); d.setThump(0.6f); d.setCabSize(0.4f);
            return d;
        };
        std::mt19937 rng(3);
        std::uniform_real_distribution<float> u(-0.7f, 0.7f);
        std::vector<float> in((size_t)SR);
        for (auto &v : in) v = u(rng);
        CabDynamicsBlock a = make(), b = make();
        std::vector<float> x = in, y = in;
        runPre(a, x); runPost(a, x);
        runPre(b, y); runPost(b, y);
        CHECK(std::memcmp(x.data(), y.data(), x.size() * sizeof(float)) == 0,
              "T5 deterministic (identical output for identical input+params)");
    }

    // ---------- T6: envelope-driven, not a static EQ ----------
    {
        CabDynamicsBlock loud; loud.prepare(SR, BLK); loud.setThump(1.0f);
        CabDynamicsBlock quiet; quiet.prepare(SR, BLK); quiet.setThump(1.0f);
        const size_t N = (size_t)(SR * 0.6);
        std::vector<float> ml(N), mq(N);
        for (size_t n = 0; n < N; ++n)
        {
            const double s = std::sin(2.0 * kPi * 90.0 * (double)n / SR);
            ml[n] = 0.9f * (float)s;
            mq[n] = 0.01f * (float)s;
        }
        runPre(loud, ml);
        runPre(quiet, mq);
        CHECK(loud.dbgA1Db() > 1.0f, "T6a loud passage blooms A1 (%.2f dB > 1.0)", loud.dbgA1Db());
        CHECK(quiet.dbgA1Db() < 0.2f, "T6b quiet passage barely deviates A1 (%.3f dB < 0.2)", quiet.dbgA1Db());
    }

    // ---------- T7: modulation ceilings ----------
    {
        CabDynamicsBlock d; d.prepare(SR, BLK);
        d.setAgeDrive(1.0f); d.setThump(1.0f); d.setCabSize(1.0f);
        const size_t N = (size_t)(SR * 0.6);
        std::vector<float> m(N);
        for (size_t n = 0; n < N; ++n) m[n] = 0.95f * (float)std::sin(2.0 * kPi * 120.0 * (double)n / SR);
        std::vector<float> post = m;
        runPre(d, m);
        runPost(d, post);
        CHECK(d.dbgA1Db() <= 3.0f + 1e-3f, "T7a A1 resonance ceiling (%.3f <= +3 dB)", d.dbgA1Db());
        CHECK(d.dbgA2Db() >= -3.0f - 1e-3f, "T7b A2 shelf floor (%.3f >= -3 dB)", d.dbgA2Db());
        CHECK(d.dbgDrive() <= 3.3f, "T7c breakup drive ceiling (%.3f <= 3.3x)", d.dbgDrive());
        CHECK(d.dbgEncMix() <= 0.12f + 1e-4f, "T7d enclosure wet-mix ceiling (%.4f <= 0.12)", d.dbgEncMix());
    }

    // ---------- T8: engage -> disengage click-free + returns to bit-exact ----------
    {
        CabDynamicsBlock d; d.prepare(SR, BLK);
        std::mt19937 rng(11);
        std::uniform_real_distribution<float> u(-0.6f, 0.6f);
        d.setAgeDrive(1.0f);
        std::vector<float> warm((size_t)(SR * 0.3));
        for (auto &v : warm) v = u(rng);
        runPre(d, warm);
        d.setAgeDrive(0.0f);
        std::vector<float> settle((size_t)(SR * 0.5), 0.0f);
        for (auto &v : settle) v = u(rng) * 0.1f;
        runPre(d, settle);
        float maxStep = 0.0f;
        for (size_t n = 1; n < settle.size(); ++n)
            maxStep = std::max(maxStep, std::fabs(settle[n] - settle[n - 1]));
        CHECK(maxStep < 0.5f, "T8a disengage has no click (max step %.3f)", maxStep);
        std::vector<float> fresh((size_t)SR);
        for (auto &v : fresh) v = u(rng);
        std::vector<float> in = fresh;
        runPre(d, fresh);
        CHECK(std::memcmp(fresh.data(), in.data(), in.size() * sizeof(float)) == 0,
              "T8b returns to bit-exact bypass after disengage");
    }

    std::printf("\n%s (%d failure%s)\n", gFails == 0 ? "ALL PASS" : "FAILURES", gFails, gFails == 1 ? "" : "s");
    return gFails == 0 ? 0 : 1;
}
