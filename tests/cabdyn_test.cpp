// cabdyn_test — offline verification harness for CabDynamicsBlock (the Dynamic
// Cab delta wrapper) and the per-cab LF resonance estimator (LfResonance.h).
// JUCE-free: includes only rig/CabDynamicsBlock.h + rig/LfResonance.h, which pull
// rig/Biquad.h. Build (from repo root, alongside the other block tests):
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
// T9  Stage B is comb-free — the level-dependent fundamental response is smooth (no
//     crossover phase-cancellation: no >0 dB bumps, adjacent-bin ripple < 1 dB)
// ---- PHYSICS UPGRADE (docs/cabdyn/PHYSICS_UPGRADE.md §9) ----
// T10 excursion selectivity — full-scale 60 Hz => dispPush >= 0.8; 2 kHz => <= 0.05
// T11 IMD engages — LF+HF two-tone: HF sidebands in [-48,-14] dBc; LF removed => no self-mod
// T12 IM adds no static color — Thump 1/Age 0, quiet HF: pre-path within 0.1 dB of bypass
// T13 IM alias grid — Thump 1/Age 0: energy off the |m*fHF +/- n*fLF| grid < -55 dBc
// T14 thermal — 6 s full-scale sine @ Age 1: droop in [1.0,1.5] dB, monotonic; recovers
//     (d) motor-floor engage: Age 0 / Thump 1 still sags ~60% of the Age-1 dose
// T15 Fs estimator — 78 Hz gtr / 52 Hz bass curves valid; flat/hash invalid (falls back)
// T16 resonance plumb-through — setSpeakerResonance(52,8) stable; T9 comb sweep still smooth
// T17 A1 keys on displacement — fs 60: 60 Hz => dbgA1Db >= 1.5; 400 Hz => < 0.3; center tracks
// T18 speaker-drive trim — sidechain-only calibration: -18 dB kills dispPush on the T10a
//     tone; +12 dB restores it on a 12 dB quieter tone; 0 dB is bit-exact vs never-set
#include "rig/CabDynamicsBlock.h"
#include "rig/LfResonance.h"
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

// Rig-realistic test level (PHYSICS_UPGRADE §12): the measured INTERNAL pre-cab
// RMS on Robbie's rig is ~ -14 dBFS (renders were -10.1 dB trimmed post-chain),
// and the excursion/push knees are calibrated to that structure. Program-
// BEHAVIOUR tests therefore probe with ~0.19-amplitude tones; full-scale tones
// (~15 dB hotter than the real internal level) remain in use only where they
// should pin everything (stability, aliasing, ceilings).
static constexpr double kRigAmp = 0.192;

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

// Level-dependent fundamental gain of Stage B at frequency f: process a sine of
// amplitude `amp` with the given macros, return output-fundamental / amp. Comparing
// a hard vs a near-linear drive reveals any crossover phase-cancellation comb.
// `fsRes` (>0) sets a per-cab resonance so T16 can probe the retuned modal path.
static double fundGain(double f, double amp, float age, float thump, float fsRes = 0.0f, float promRes = 0.0f)
{
    CabDynamicsBlock d;
    d.prepare(SR, BLK);
    if (fsRes > 0.0f) d.setSpeakerResonance(fsRes, promRes);
    d.setAgeDrive(age);
    d.setThump(thump);
    const size_t N = (size_t)(SR * 0.5);
    std::vector<float> m(N);
    for (size_t n = 0; n < N; ++n) m[n] = (float)(amp * std::sin(2.0 * kPi * f * (double)n / SR));
    runPre(d, m);
    return goertzel(m, N / 2, N / 4, f) / amp;
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
    // NOTE: CabDynamicsBlock now holds std::atomic<float> resonance targets (spec §4),
    // so it is non-copyable/non-movable — construct the two instances IN PLACE and
    // configure identically rather than returning one by value from a helper.
    {
        auto config = [](CabDynamicsBlock &d) {
            d.prepare(SR, BLK);
            d.setAgeDrive(0.7f); d.setThump(0.6f); d.setCabSize(0.4f);
        };
        std::mt19937 rng(3);
        std::uniform_real_distribution<float> u(-0.7f, 0.7f);
        std::vector<float> in((size_t)SR);
        for (auto &v : in) v = u(rng);
        CabDynamicsBlock a, b;
        config(a); config(b);
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
            // "quiet" = rig-realistic quiet: measured quiet-playing 90 Hz slice is
            // |LP2| ~ 0.010-0.016 INTERNAL (§12); a hotter pure 90 Hz tone would be
            // louder in-band than real quiet playing and sit on the knee edge.
            mq[n] = 0.016f * (float)s;
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
        CHECK(d.dbgDrive() <= 3.8f + 1e-3f, "T7c breakup drive ceiling (%.3f <= 3.8x)", d.dbgDrive());
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

    // ---------- T9: Stage B is comb-free (phase-alignment / describing-function) ----------
    {
        // The level-dependent fundamental response must be SMOOTH. A harmonics-only
        // exciter may compress the mids a little, but must NOT produce phase-
        // cancellation comb (alternating >0 dB bumps and deep dips) near the 800 /
        // 3800 Hz crossover corners. This is the regression guard for the Stage B1
        // describing-function fundamental cancellation.
        double prev = 0.0, worstRipple = 0.0, worstBump = -100.0;
        bool first = true;
        for (double f = 500.0; f <= 4500.0; f += 100.0)
        {
            const double gh = fundGain(f, 0.9, 1.0f, 0.0f);   // hard drive
            const double gs = fundGain(f, 0.02, 1.0f, 0.0f);  // near-linear
            const double ld = 20.0 * std::log10(gh / gs);     // level-dependent gain
            if (ld > worstBump) worstBump = ld;
            if (!first) worstRipple = std::max(worstRipple, std::fabs(ld - prev));
            prev = ld;
            first = false;
        }
        CHECK(worstRipple < 1.0, "T9a no crossover comb (max adjacent ripple %.3f dB < 1.0)", worstRipple);
        CHECK(worstBump < 0.5, "T9b compressive exciter never boosts fundamental (max %.3f dB < 0.5)", worstBump);
    }

    // ================= PHYSICS UPGRADE tests (docs/cabdyn/PHYSICS_UPGRADE.md §9) =================

    // ---------- T10: excursion selectivity (displacement, not broadband envelope) ----------
    // The whole point of the excursion model: dispPush follows cone DISPLACEMENT (a
    // resonant LP at ~90 Hz), so a RIG-LEVEL LF note pushes hard while an equally
    // loud HF note (mids/HF produce ~0 displacement) barely does. This kills the
    // class of bug where a loud 2 kHz bend fattened the 90 Hz resonance.
    {
        auto dispAt = [](double f) {
            CabDynamicsBlock d; d.prepare(SR, BLK);
            d.setThump(1.0f); // engage so processPre runs (dispPush is computed there)
            const size_t N = (size_t)(SR * 0.6);
            std::vector<float> m(N);
            for (size_t n = 0; n < N; ++n) m[n] = (float)(kRigAmp * std::sin(2.0 * kPi * f * (double)n / SR));
            runPre(d, m);
            return d.dbgDispPush();
        };
        const float dLo = dispAt(60.0);
        const float dHi = dispAt(2000.0);
        CHECK(dLo >= 0.8f, "T10a rig-level 60 Hz pushes displacement (dispPush %.3f >= 0.8)", dLo);
        CHECK(dHi <= 0.05f, "T10b rig-level 2 kHz barely displaces (dispPush %.3f <= 0.05)", dHi);
    }

    // ---------- T11: IMD engages (Bl(x) AM + Doppler FM) ----------
    // Loud LF + quiet HF two-tone: the LF displacement amplitude/frequency-modulates
    // the HF, so sidebands appear at fHF +/- fLF. With no LF, the HF cannot self-
    // modulate, so the sidebands must collapse (proves the coupling is LF-driven, not
    // a fixed nonlinearity on the HF).
    {
        const double fLF = 100.0, fHF = 4500.0;
        auto sidebandDbc = [&](bool withLF) {
            CabDynamicsBlock d; d.prepare(SR, BLK);
            d.setThump(1.0f); d.setAgeDrive(0.6f);
            const size_t N = (size_t)(SR * 1.0);
            std::vector<float> m(N);
            for (size_t n = 0; n < N; ++n)
            {
                const double t = (double)n / SR;
                const double lf = withLF ? kRigAmp * std::sin(2.0 * kPi * fLF * t) : 0.0;
                const double hf = 0.2 * kRigAmp * std::sin(2.0 * kPi * fHF * t);
                m[n] = (float)(lf + hf);
            }
            runPre(d, m);
            const size_t start = N / 2, win = N / 4;
            const double carrier = goertzel(m, start, win, fHF);
            const double sbLo = goertzel(m, start, win, fHF - fLF);
            const double sbHi = goertzel(m, start, win, fHF + fLF);
            const double sb = std::max(sbLo, sbHi);
            return 20.0 * std::log10((sb + 1e-15) / (carrier + 1e-15));
        };
        const double sbOn = sidebandDbc(true);
        const double sbOff = sidebandDbc(false);
        CHECK(sbOn >= -48.0 && sbOn <= -14.0,
              "T11a HF sidebands present with LF (%.1f dBc in [-48,-14])", sbOn);
        CHECK(sbOff < -70.0,
              "T11b no self-modulation without LF (%.1f dBc < -70)", sbOff);
    }

    // ---------- T12: IM adds no static color (the two-tap zero-at-rest property) ----------
    // Thump 1 / Age 0 => Wim = 0.5*1 = 0.5 (IM engaged) but with quiet HF-only tones
    // there is NO LF excursion (xs ~ 0), so mod == clean bit-exactly and gAM = 0 =>
    // the IM delta is EXACTLY 0. The pre-path magnitude at the probe tones must match
    // a reference within 0.1 dB (no static comb from the fixed center delay).
    {
        const double f1 = 3000.0, f2 = 8000.0;
        // reference: block with Thump 0 (all stages transparent) — but processPre is
        // bit-exact bypass there, so the reference IS the input. Compare the Thump-1
        // output magnitude at f1/f2 against the input magnitude (quiet tones keep the
        // other stages ~identity at these HF freqs; A2 shelf droop is Age-only=0, A1
        // is LF-only, B1 needs Age=0 so it's off). Any IM comb would show here.
        CabDynamicsBlock d; d.prepare(SR, BLK);
        d.setThump(1.0f); // engages IM (Wim=0.5) but NOT A1 gain (needs dispPush; tones are quiet HF)
        const size_t N = (size_t)(SR * 1.0);
        std::vector<float> m(N), in(N);
        for (size_t n = 0; n < N; ++n)
        {
            const double t = (double)n / SR;
            const double s = 0.02 * std::sin(2.0 * kPi * f1 * t) + 0.02 * std::sin(2.0 * kPi * f2 * t);
            in[n] = (float)s; m[n] = (float)s;
        }
        runPre(d, m);
        const size_t start = N / 2, win = N / 4;
        const double d1 = 20.0 * std::log10(goertzel(m, start, win, f1) / goertzel(in, start, win, f1));
        const double d2 = 20.0 * std::log10(goertzel(m, start, win, f2) / goertzel(in, start, win, f2));
        CHECK(std::fabs(d1) < 0.1, "T12a IM no color @ 3 kHz (%.4f dB vs bypass, |.|<0.1)", d1);
        CHECK(std::fabs(d2) < 0.1, "T12b IM no color @ 8 kHz (%.4f dB vs bypass, |.|<0.1)", d2);
    }

    // ---------- T13: IM alias grid (base-rate IM does not fold) ----------
    // Thump 1 / Age 0, LF 95 Hz + HF 4.7 kHz: every bin of energy must sit ON the
    // legitimate intermodulation grid |m*fHF +/- n*fLF| (m<=1, n<=8). Any energy OFF
    // that grid (i.e. a fold) must be < -55 dBc — the guard that base-rate IM is safe
    // (xs is band-limited <~500 Hz, products stay away from Nyquist).
    {
        const double fLF = 95.0, fHF = 4700.0;
        CabDynamicsBlock d; d.prepare(SR, BLK);
        d.setThump(1.0f);
        const size_t N = (size_t)SR;
        std::vector<float> m(N);
        for (size_t n = 0; n < N; ++n)
        {
            const double t = (double)n / SR;
            m[n] = (float)(kRigAmp * std::sin(2.0 * kPi * fLF * t) + 0.2 * kRigAmp * std::sin(2.0 * kPi * fHF * t));
        }
        runPre(d, m);
        const size_t start = N / 2, win = N / 4;
        const double carrier = goertzel(m, start, win, fHF);
        double worstOff = 0.0, atF = 0.0;
        for (double f = 500.0; f < 20000.0; f += 25.0)
        {
            // is f on the grid |m*fHF +/- n*fLF|, m in {0,1}, n in [0,8]?
            bool onGrid = false;
            for (int mm = 0; mm <= 1 && !onGrid; ++mm)
                for (int nn = 0; nn <= 8; ++nn)
                {
                    if (std::fabs(f - (mm * fHF + nn * fLF)) < 40.0) { onGrid = true; break; }
                    if (std::fabs(f - std::fabs(mm * fHF - nn * fLF)) < 40.0) { onGrid = true; break; }
                }
            if (onGrid) continue;
            const double mag = goertzel(m, start, win, f);
            if (mag > worstOff) { worstOff = mag; atF = f; }
        }
        const double dbc = 20.0 * std::log10((worstOff + 1e-15) / (carrier + 1e-15));
        CHECK(dbc < -55.0, "T13 worst off-grid (alias) energy %.1f dBc @ %.0f Hz (< -55)", dbc, atF);
    }

    // ---------- T14: thermal voice-coil compression ----------
    // 6 s of RIG-LEVEL sustained playing at Age 1: amplitude 0.286 gives mean
    // x^2 = 0.041 = kThermFull (the measured internal riffing power, §12), so
    // pwrN -> 1 and droopDb -> -1.5 * Age; at the ~3.5 s tau the 6 s point sits at
    // ~1-e^{-6/3.5} ~ 0.82 of full => expected ~1.23 dB. Then 6 s of silence must
    // recover. Compression only, monotone (ripple tol).
    {
        CabDynamicsBlock d; d.prepare(SR, BLK);
        d.setAgeDrive(1.0f);
        const size_t N = (size_t)(SR * 6.0);
        std::vector<float> m(N);
        for (size_t n = 0; n < N; ++n) m[n] = 0.286f * (float)std::sin(2.0 * kPi * 80.0 * (double)n / SR);
        // sample the droop at 1 s intervals to check monotone increase in magnitude
        double prevMag = 0.0; bool mono = true;
        for (int sec = 1; sec <= 6; ++sec)
        {
            const size_t p0 = (size_t)(SR * (double)(sec - 1));
            const size_t p1 = (size_t)(SR * (double)sec);
            d.processPre(m.data() + p0, (int)(p1 - p0));
            const double mag = -(double)d.dbgThermDb(); // droop magnitude (>=0)
            if (mag < prevMag - 0.02) mono = false;     // ripple tolerance
            prevMag = mag;
        }
        const double droop = -(double)d.dbgThermDb();
        CHECK(droop >= 1.0 && droop <= 1.5, "T14a thermal droop after 6 s (%.3f dB in [1.0,1.5])", droop);
        CHECK(mono, "T14b thermal droop grows monotonically under sustained power");
        // 6 s of silence -> recover
        std::vector<float> sil((size_t)(SR * 6.0), 0.0f);
        runPre(d, sil);
        const double resid = -(double)d.dbgThermDb();
        CHECK(resid < 0.3, "T14c thermal recovers on silence (residual %.3f dB < 0.3)", resid);

        // (d) motor-floor engage (rev 2026-07-07): heating is motor physics, not
        // wear. Age 0 / Thump 1 must still sag at kThermMotor (0.6) of the Age-1
        // dose: expected ~0.6 * 1.23 = 0.74 dB at the same 6 s point.
        CabDynamicsBlock d2; d2.prepare(SR, BLK);
        d2.setThump(1.0f); // Age stays 0 — fresh cone, motor still heats
        std::vector<float> m2(N);
        for (size_t n = 0; n < N; ++n) m2[n] = 0.286f * (float)std::sin(2.0 * kPi * 80.0 * (double)n / SR);
        runPre(d2, m2);
        const double droopFresh = -(double)d2.dbgThermDb();
        CHECK(droopFresh >= 0.55 && droopFresh <= 0.95,
              "T14d fresh-cab (Age 0, Thump 1) motor sag (%.3f dB in [0.55,0.95])", droopFresh);
    }

    // ---------- T15: Fs estimator (LfResonance.h, synthetic dB curves) ----------
    // Synthesize mean-centred dB response curves directly on the 200-pt log grid
    // 40..8000 Hz: a resonant LF bump (Gaussian in log-f) + a gentle HF rolloff +
    // a seeded +/-1.5 dB deterministic hash (comb residue). estimateLfResonance must
    // recover the bump frequency within tolerance, and reject a flat/hash-only curve.
    {
        using nam_rig::ir::estimateLfResonance;
        using nam_rig::ir::LfEstimate;
        const int NP = 200;
        const float FLO = 40.0f, FHI = 8000.0f;
        const double lrSpan = std::log((double)FHI / (double)FLO);
        auto freqAt = [&](int i) { return (double)FLO * std::exp(lrSpan * (double)i / (double)(NP - 1)); };
        // deterministic hash in [-1.5,1.5] dB (comb-like residual, seeded per-index)
        auto hash = [](int i) {
            unsigned h = (unsigned)i * 2654435761u + 1013904223u;
            h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
            return ((double)(h & 0xFFFF) / 65535.0 * 2.0 - 1.0) * 1.5;
        };
        auto synth = [&](double fBump, double bumpDb, bool addRoll, bool bumpOn) {
            std::vector<float> r(NP);
            double sum = 0.0;
            for (int i = 0; i < NP; ++i)
            {
                const double f = freqAt(i);
                double v = 0.0;
                if (bumpOn)
                {
                    const double lx = std::log(f / fBump) / std::log(2.0); // octaves from bump
                    v += bumpDb * std::exp(-(lx * lx) / (2.0 * 0.30 * 0.30)); // ~0.3-oct wide
                }
                if (addRoll) v += -6.0 * std::log(f / 200.0) / std::log(2.0) * (f > 200.0 ? 1.0 : 0.0) * 0.15;
                v += hash(i);
                r[(size_t)i] = (float)v; sum += v;
            }
            const float mean = (float)(sum / (double)NP);
            for (int i = 0; i < NP; ++i) r[(size_t)i] -= mean; // mean-centre like computeResponse
            return r;
        };
        // 78 Hz guitar bump (prominent)
        {
            auto r = synth(78.0, 7.0, true, true);
            LfEstimate e = estimateLfResonance(r.data(), NP, FLO, FHI);
            const bool near = std::fabs(e.fsHz - 78.0f) / 78.0f <= 0.12f;
            CHECK(e.valid && near, "T15a guitar 78 Hz curve -> valid %d, fs %.1f (±12%%)", (int)e.valid, e.fsHz);
        }
        // 52 Hz bass bump
        {
            auto r = synth(52.0, 8.0, true, true);
            LfEstimate e = estimateLfResonance(r.data(), NP, FLO, FHI);
            const bool near = std::fabs(e.fsHz - 52.0f) / 52.0f <= 0.15f;
            CHECK(e.valid && near, "T15b bass 52 Hz curve -> valid %d, fs %.1f (±15%%)", (int)e.valid, e.fsHz);
        }
        // flat + hash only (no bump) -> invalid, caller falls back to 90
        {
            auto r = synth(78.0, 0.0, false, false);
            LfEstimate e = estimateLfResonance(r.data(), NP, FLO, FHI);
            CHECK(!e.valid, "T15c flat/hash curve -> invalid (falls back 90); reported valid=%d", (int)e.valid);
        }
    }

    // ---------- T16: resonance plumb-through (retune stays stable + comb-free) ----------
    // setSpeakerResonance(52, 8) shifts the excursion LP2, A1 base, B3 corner, and the
    // MODAL centers (mScale 0.75 for a bass cab). At max settings the block must stay
    // finite/bounded, and the Stage B comb sweep (T9-style) must still be smooth — the
    // modal rescale cannot re-introduce a crossover comb (delta has no fundamental).
    {
        CabDynamicsBlock d; d.prepare(SR, BLK);
        d.setSpeakerResonance(52.0f, 8.0f);
        d.setAgeDrive(1.0f); d.setThump(1.0f); d.setCabSize(1.0f);
        std::mt19937 rng(23);
        std::uniform_real_distribution<float> u(-1.0f, 1.0f);
        const size_t N = (size_t)(SR * 2.0);
        std::vector<float> m(N);
        for (auto &v : m) v = u(rng);
        runPre(d, m);
        bool finite = true; float peak = 0.0f;
        for (float v : m) { if (!std::isfinite(v)) finite = false; peak = std::max(peak, std::fabs(v)); }
        CHECK(finite && peak < 4.0f, "T16a stable at max with fs=52 (finite %d, peak %.3f < 4.0)", (int)finite, peak);
        CHECK(std::fabs(d.dbgFsEst() - 52.0f) < 0.5f, "T16b resonance consumed (dbgFsEst %.1f ~ 52)", d.dbgFsEst());
        // T9-style comb sweep with the retuned modal path
        double prev = 0.0, worstRipple = 0.0;
        bool first = true;
        for (double f = 500.0; f <= 4500.0; f += 100.0)
        {
            const double gh = fundGain(f, 0.9, 1.0f, 0.0f, 52.0f, 8.0f);
            const double gs = fundGain(f, 0.02, 1.0f, 0.0f, 52.0f, 8.0f);
            const double ld = 20.0 * std::log10(gh / gs);
            if (!first) worstRipple = std::max(worstRipple, std::fabs(ld - prev));
            prev = ld; first = false;
        }
        CHECK(worstRipple < 1.2, "T16c comb sweep smooth after modal rescale (max ripple %.3f dB < 1.2)", worstRipple);
    }

    // ---------- T17: A1 keys on displacement, and its center tracks the shifted f0 ----------
    // With per-cab fs = 60 Hz: a full-scale 60 Hz note (large displacement) blooms A1
    // >= 1.5 dB, while a full-scale 400 Hz note (~zero displacement) leaves A1 < 0.3 dB
    // — A1 is displacement-driven, not broadband-envelope-driven. The built center
    // must sit near 60 Hz (allowing the Thump/excursion f0 shift), not the 90 default.
    {
        {
            CabDynamicsBlock d; d.prepare(SR, BLK);
            d.setSpeakerResonance(60.0f, 0.0f);
            d.setThump(1.0f);
            const size_t N = (size_t)(SR * 0.6);
            std::vector<float> m(N);
            for (size_t n = 0; n < N; ++n) m[n] = (float)(kRigAmp * std::sin(2.0 * kPi * 60.0 * (double)n / SR));
            runPre(d, m);
            CHECK(d.dbgA1Db() >= 1.5f, "T17a rig-level 60 Hz blooms A1 with fs=60 (%.2f dB >= 1.5)", d.dbgA1Db());
            // center: fsEst*(1-0.12*Thump)*(1+0.15*dispPush). Thump=1 -> 60*0.88 ~ 52.8,
            // with the Klippel-anchored +15% excursion shift -> ~60.7; well below 90.
            CHECK(d.dbgA1CenterHz() > 45.0 && d.dbgA1CenterHz() < 64.0,
                  "T17c A1 center tracks shifted f0 (%.1f Hz in ~[45,64], not 90)", d.dbgA1CenterHz());
        }
        {
            CabDynamicsBlock d; d.prepare(SR, BLK);
            d.setSpeakerResonance(60.0f, 0.0f);
            d.setThump(1.0f);
            const size_t N = (size_t)(SR * 0.6);
            std::vector<float> m(N);
            for (size_t n = 0; n < N; ++n) m[n] = (float)(kRigAmp * std::sin(2.0 * kPi * 400.0 * (double)n / SR));
            runPre(d, m);
            CHECK(d.dbgA1Db() < 0.3f, "T17b rig-level 400 Hz barely blooms A1 (%.3f dB < 0.3)", d.dbgA1Db());
        }
    }

    // ---------- T18: speaker-drive trim (sidechain-only level calibration) ----------
    // The §12 knees are calibrated to one rig's internal level; setSpeakerDriveDb
    // tells the model how far a different rig sits from that convention. It scales
    // ONLY the detectors (excursion, push envelopes, thermal integrator) — the
    // audio path and the IM/band math are untouched. Guards: (a) the T10a tone
    // with trim -18 dB reads as a quiet rig => dispPush collapses; (b) a tone
    // 12 dB quieter than T10a with trim +12 dB reads calibrated => dispPush
    // restores; (c) trim 0 dB is BIT-EXACT vs a block that never called the setter.
    {
        auto dispWithTrim = [](double amp, float trimDb) {
            CabDynamicsBlock d; d.prepare(SR, BLK);
            d.setSpeakerDriveDb(trimDb);
            d.setThump(1.0f);
            const size_t N = (size_t)(SR * 0.6);
            std::vector<float> m(N);
            for (size_t n = 0; n < N; ++n) m[n] = (float)(amp * std::sin(2.0 * kPi * 60.0 * (double)n / SR));
            runPre(d, m);
            return d.dbgDispPush();
        };
        const float dCold = dispWithTrim(kRigAmp, -18.0f);            // hot cal, quiet rig story
        const float dHot  = dispWithTrim(kRigAmp * 0.251, +12.0f);    // -12 dB tone, +12 dB trim
        CHECK(dCold <= 0.05f, "T18a -18 dB trim kills dispPush on the T10a tone (%.3f <= 0.05)", dCold);
        CHECK(dHot  >= 0.8f,  "T18b +12 dB trim restores dispPush on a -12 dB tone (%.3f >= 0.8)", dHot);

        // (c) 0 dB == never-set, byte-identical (same input, same macros, full stack)
        std::mt19937 rng(31);
        std::uniform_real_distribution<float> u(-0.5f, 0.5f);
        std::vector<float> in((size_t)SR);
        for (auto &v : in) v = u(rng);
        CabDynamicsBlock a, b;
        a.prepare(SR, BLK); b.prepare(SR, BLK);
        a.setSpeakerDriveDb(0.0f); // b never calls the setter
        a.setAgeDrive(0.8f); a.setThump(0.7f); a.setCabSize(0.5f);
        b.setAgeDrive(0.8f); b.setThump(0.7f); b.setCabSize(0.5f);
        std::vector<float> xa = in, xb = in;
        runPre(a, xa); runPost(a, xa);
        runPre(b, xb); runPost(b, xb);
        CHECK(std::memcmp(xa.data(), xb.data(), xa.size() * sizeof(float)) == 0,
              "T18c trim 0 dB is bit-exact vs never-set (byte-identical)");
    }

    std::printf("\n%s (%d failure%s)\n", gFails == 0 ? "ALL PASS" : "FAILURES", gFails, gFails == 1 ? "" : "s");
    return gFails == 0 ? 0 : 1;
}
