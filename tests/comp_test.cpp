// comp_test — offline verification harness for CompBlock (measurement-first).
// Exits nonzero on any FAIL.
//
// Model as of July 2026:
//  - AUTO-MAKEUP: the compressor sets its own output gain so the output peaks at
//    the input level with Level at 0 dB (no static makeup, no Boost knob). The
//    makeup follows peaks slowly, so it sets the overall level without fighting
//    per-note dynamics — dynamic tests therefore probe gain reduction via grDb()
//    rather than the (makeup-flattened) output level.
//  - RATIO knob on Clean/FET (ratioExposed); Opto/OTA use their fixed character.
//  - RELEASE knob on Clean/FET/Opto (releaseExposed); scales the voicing release
//    (150 ms == the tuned default), OTA keeps its built-in timing.
#include "rig/CompBlock.h"
#include <cstdio>
#include <cmath>
#include <vector>

using nam_rig::CompBlock;
using nam_rig::BlockContext;

static int gFails = 0;
#define CHECK(cond, ...) do { \
    const bool chkOk_ = (cond); /* unique name: must not shadow caller's vars */ \
    std::printf("%s: ", chkOk_ ? "PASS" : "FAIL"); std::printf(__VA_ARGS__); std::printf("\n"); \
    if (!chkOk_) ++gFails; } while (0)

static constexpr double SR = 48000.0;
static constexpr int BLK = 512;

static void run(CompBlock &c, std::vector<float> &x)
{
    for (size_t p = 0; p < x.size(); p += BLK)
        c.process(x.data() + p, (int)std::min<size_t>(BLK, x.size() - p));
}

static std::vector<float> tone(double ampDb, int n)
{
    std::vector<float> v((size_t)n);
    const float a = std::pow(10.0f, (float)ampDb / 20.0f);
    for (int i = 0; i < n; ++i)
        v[(size_t)i] = a * (float)std::sin(2.0 * M_PI * 1000.0 * i / SR);
    return v;
}

// steady-state peak dB over the last 100 ms of a buffer (sine RMS -> peak)
static double tailDb(const std::vector<float> &x)
{
    const size_t n = 4800;
    double e = 0;
    for (size_t i = x.size() - n; i < x.size(); ++i) e += (double)x[i] * x[i];
    return 10.0 * std::log10(e / (double)n) + 3.0103; // RMS->peak of sine
}

// run a buffer through a chosen voicing (ratio/release at their defaults)
static std::vector<float> runMode(int mode, float sustain, float attackMs,
                                  float character, const std::vector<float> &in)
{
    CompBlock c;
    c.setSustain(sustain);
    c.setAttackMs(attackMs);
    c.setMode(mode);
    c.setCharacter(character);
    c.prepare({SR, BLK});
    auto x = in;
    run(c, x);
    return x;
}

// |X(freq)| over n samples from start (Goertzel)
static double goertzel(const std::vector<float> &x, size_t start, size_t n, double freq)
{
    const double w = 2.0 * M_PI * freq / SR;
    const double cw = std::cos(w), sw = std::sin(w), coeff = 2.0 * cw;
    double s1 = 0, s2 = 0;
    for (size_t i = 0; i < n; ++i)
    {
        const double s0 = (double)x[start + i] + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    const double re = s1 - s2 * cw, im = s2 * sw;
    return std::sqrt(re * re + im * im) / (double)(n / 2);
}

// windowed RMS in dB, +/- half samples around centre
static double envWin(const std::vector<float> &y, size_t c, size_t half)
{
    double e = 0;
    for (size_t i = c - half; i < c + half; ++i)
        e += (double)y[i] * y[i];
    return 10.0 * std::log10(e / (double)(2 * half));
}

int main()
{
    const float sustain = 0.5f;
    const float T = CompBlock::thresholdForSustain(sustain);   // -27.5 dB
    (void)T;

    // ---- T1: auto-makeup matches output LOUDNESS (RMS) to the input ----
    // With Level at 0 dB, a steady tone comes out at its input loudness whether
    // it is below threshold (unity) or being compressed (makeup restores it), so
    // engaging the comp is level-neutral.
    {
        bool ok = true;
        double worst = 0;
        for (double inDb : {-50.0, -40.0, -30.0, -20.0, -10.0, -5.0})
        {
            CompBlock c; c.setSustain(sustain); c.setAttackMs(1.0f);
            c.setRatio(CompBlock::kRatio); c.prepare({SR, BLK});
            auto ref = tone(inDb, 96000); auto x = ref; run(c, x); // 2 s: makeup converges
            double ie = 0, oe = 0;
            for (size_t i = x.size() - 9600; i < x.size(); ++i)
            { ie += (double)ref[i] * ref[i]; oe += (double)x[i] * x[i]; }
            const double errDb = std::abs(10.0 * std::log10(oe / ie));
            worst = std::max(worst, errDb);
            if (errDb > 1.0) ok = false;
        }
        CHECK(ok, "T1 auto-makeup: output loudness (RMS) matches input (worst %.2f dB, want < 1.0)", worst);
    }

    // ---- T2: soft-knee transfer slope above knee == 1/ratio (pure computer) ----
    {
        bool ok = true; double worst = 0;
        const float Tt = -20.0f;
        for (float ratio : {3.0f, CompBlock::kRatio, 12.0f})
        {
            const double o1 = -2.0 + CompBlock::computeGainDb(-2.0f, Tt, ratio, CompBlock::kKneeDb);
            const double o2 = -10.0 + CompBlock::computeGainDb(-10.0f, Tt, ratio, CompBlock::kKneeDb);
            const double slope = (o1 - o2) / 8.0;
            worst = std::max(worst, std::abs(slope - 1.0 / ratio));
            if (std::abs(slope - 1.0 / ratio) > 0.02) ok = false;
        }
        CHECK(ok, "T2 transfer slope above knee == 1/ratio (worst %.3f, want < 0.02)", worst);
    }

    // ---- T3: attack blooms before clamping; release recovers the GR ----
    {
        std::vector<float> x(144000);
        for (size_t i = 0; i < x.size(); ++i)
        {
            const double amp = (i < 48000 || i >= 96000) ? 0.01 : 0.5623; // -40 / -5 / -40 dB
            x[i] = (float)(amp * std::sin(2.0 * M_PI * 1000.0 * (double)i / SR));
        }
        // attack: makeup is slow, so the pick transient still overshoots the clamp.
        CompBlock c; c.setSustain(sustain); c.setAttackMs(10.0f);
        c.setRatio(CompBlock::kRatio); c.prepare({SR, BLK});
        auto y = x; run(c, y);
        auto envDbAt = [&](size_t center)
        {
            double e = 0; for (size_t i = center - 480; i < center + 480; ++i) e += (double)y[i] * y[i];
            return 10.0 * std::log10(e / 960.0);
        };
        const double early = envDbAt(48000 + 240);   // ~5 ms in: barely compressed
        const double settled = envDbAt(48000 + 4800); // 100 ms in: clamped
        CHECK(early - settled > 2.0,
              "T3a transient blooms before compression clamps (%.1f dB)", early - settled);

        // release: GR (unconfounded by makeup) recovers after the loud passage.
        auto grAt = [&](size_t target)
        {
            CompBlock cc; cc.setSustain(sustain); cc.setAttackMs(10.0f);
            cc.setRatio(CompBlock::kRatio); cc.prepare({SR, BLK});
            auto xx = x; size_t p = 0; float g = 0;
            while (p < target) { int n = (int)std::min<size_t>(64, target - p); cc.process(xx.data() + p, n); p += (size_t)n; g = cc.grDb(); }
            return (double)g;
        };
        const double gr100 = grAt(96000 + 4800);  // 100 ms after the drop
        const double gr900 = grAt(96000 + 43200); // 900 ms after the drop
        CHECK(gr100 - gr900 > 1.0 && gr900 < 1.0,
              "T3b release recovers GR: %.1f dB (100ms) -> %.1f dB (900ms)", gr100, gr900);
    }

    // ---- T4: sustain does what it says on a decaying note ----
    {
        auto t30 = [&](float s)
        {
            CompBlock c; c.setSustain(s); c.setRatio(CompBlock::kRatio); c.prepare({SR, BLK});
            // exp-decaying 220 Hz tone: -10 dB start, -40 dB/s decay
            std::vector<float> x(144000);
            for (size_t i = 0; i < x.size(); ++i)
            {
                const double t = (double)i / SR;
                x[i] = (float)(std::pow(10.0, (-10.0 - 40.0 * t) / 20.0) * std::sin(2.0 * M_PI * 220.0 * t));
            }
            run(c, x);
            // time for output env to fall 30 dB below its level at t = 100 ms
            // (reference AFTER the attack settles, to skip the uncompressed bloom).
            const size_t refStart = 4800;
            double e0 = 0; for (size_t i = refStart; i < refStart + 960; ++i) e0 += (double)x[i] * x[i];
            const double refDb = 10.0 * std::log10(e0 / 960.0);
            for (size_t i = refStart + 960; i + 960 <= x.size(); i += 480)
            {
                double e = 0; for (size_t k = 0; k < 960; ++k) e += (double)x[i + k] * x[i + k];
                if (10.0 * std::log10(e / 960.0) < refDb - 30.0)
                    return (double)(i - refStart) / SR;
            }
            return (double)x.size() / SR;
        };
        const double tLow = t30(0.1f), tHigh = t30(0.9f);
        CHECK(tHigh > tLow * 1.3,
              "T4 sustain lengthens decay: T30 %.2fs (s=0.1) -> %.2fs (s=0.9), want >1.3x", tLow, tHigh);
    }

    // ---- T5: sub-threshold unity at Level 0; Level knob trims exactly ----
    {
        auto ref = tone(-40.0, 96000);
        // sustain 0 -> threshold -10 dB, so -40 dB is well below: no GR, makeup ~unity
        CompBlock c0; c0.setSustain(0.0f); c0.setLevelDb(0.0f);
        c0.setRatio(CompBlock::kRatio); c0.prepare({SR, BLK});
        auto x0 = ref; run(c0, x0);
        const double u = tailDb(x0) - tailDb(ref);
        CHECK(std::abs(u) < 0.3, "T5a sub-threshold unity at Level 0 (%.2f dB, want ~0)", u);

        CompBlock c6; c6.setSustain(0.0f); c6.setLevelDb(6.0f);
        c6.setRatio(CompBlock::kRatio); c6.prepare({SR, BLK});
        auto x6 = ref; run(c6, x6);
        const double d = tailDb(x6) - tailDb(ref);
        CHECK(std::abs(d - 6.0) < 0.3, "T5b Level +6 dB trims output +6 dB (got %.2f)", d);
    }

    // ---- T6: no zipper — per-sample output delta bounded on steady tone ----
    {
        CompBlock c; c.setSustain(0.9f); c.setAttackMs(1.0f); c.prepare({SR, BLK});
        auto x = tone(-10.0, 48000);
        run(c, x);
        // a 1 kHz sine at 48k moves at most ~13% of peak per sample; allow 2x
        double maxStep = 0, peak = 0;
        for (size_t i = 9600; i + 1 < x.size(); ++i)
        {
            maxStep = std::max(maxStep, (double)std::abs(x[i + 1] - x[i]));
            peak = std::max(peak, (double)std::abs(x[i]));
        }
        CHECK(maxStep < 0.27 * peak, "T6 max per-sample step %.3f of peak (want < 0.27)", maxStep / peak);
    }

    // ======================= VOICINGS (compMode) =======================

    // ---- T7: per-mode transfer slope == 1/ratio; ratios FET > Clean > Opto ----
    {
        bool ok = true;
        double worst = 0;
        for (int m : {1, 2, 3}) // OTA, Opto, FET
        {
            const auto v = CompBlock::voicingFor((CompBlock::Mode)m);
            const float Tt = -20.0f;
            const double o1 = -1.0 + CompBlock::computeGainDb(-1.0f, Tt, v.ratio, v.kneeDb);
            const double o2 = -9.0 + CompBlock::computeGainDb(-9.0f, Tt, v.ratio, v.kneeDb);
            const double slope = (o1 - o2) / 8.0;
            worst = std::max(worst, std::abs(slope - 1.0 / v.ratio));
            if (std::abs(slope - 1.0 / v.ratio) > 0.03)
                ok = false;
        }
        ok = ok && CompBlock::voicingFor(CompBlock::Mode::FET).ratio > CompBlock::kRatio &&
             CompBlock::voicingFor(CompBlock::Mode::Opto).ratio < CompBlock::kRatio;
        CHECK(ok, "T7 per-mode slope==1/ratio (worst %.3f), ratios FET>Clean>Opto", worst);
    }

    // ---- T8: FET's fast attack clamps the pick transient harder than Clean ----
    {
        std::vector<float> x(96000);
        for (size_t i = 0; i < x.size(); ++i)
        {
            const double amp = (i < 24000) ? 0.01 : 0.5623; // -40 -> -5 dB step
            x[i] = (float)(amp * std::sin(2.0 * M_PI * 1000.0 * (double)i / SR));
        }
        auto clean = runMode(0, 0.5f, 10.0f, 0.0f, x);
        auto fet = runMode(3, 0.5f, 10.0f, 0.0f, x);
        // bloom = early overshoot (~6 ms in) minus settled (200 ms in)
        const double cleanBloom = envWin(clean, 24000 + 300, 120) - envWin(clean, 24000 + 9600, 480);
        const double fetBloom = envWin(fet, 24000 + 300, 120) - envWin(fet, 24000 + 9600, 480);
        CHECK(fetBloom < cleanBloom - 0.5,
              "T8 FET clamps transient faster (bloom FET %.1f < Clean %.1f dB)", fetBloom, cleanBloom);
    }

    // ---- T9: Opto's program-dependent release recovers slower than Clean (via GR) ----
    {
        std::vector<float> x(144000);
        for (size_t i = 0; i < x.size(); ++i)
        {
            const double amp = (i < 48000) ? 0.5623 : 0.01; // -5 dB for 1 s, then -40 dB
            x[i] = (float)(amp * std::sin(2.0 * M_PI * 1000.0 * (double)i / SR));
        }
        auto grAt = [&](int mode, size_t target)
        {
            CompBlock c; c.setSustain(0.5f); c.setAttackMs(10.0f); c.setMode(mode); c.prepare({SR, BLK});
            auto xx = x; size_t p = 0; float g = 0;
            while (p < target) { int n = (int)std::min<size_t>(64, target - p); c.process(xx.data() + p, n); p += (size_t)n; g = c.grDb(); }
            return (double)g;
        };
        // fraction of GR still held 150 ms after the drop (normalised by GR pre-drop)
        const double cRem = grAt(0, 48000 + 7200) / (grAt(0, 47000) + 1.0e-9);
        const double oRem = grAt(2, 48000 + 7200) / (grAt(2, 47000) + 1.0e-9);
        CHECK(oRem > cRem + 0.1,
              "T9 Opto releases slower: GR held @150ms Opto %.2f > Clean %.2f", oRem, cRem);
    }

    // ---- T10: FET adds harmonic grit a clean VCA does not ----
    {
        auto x = tone(-5.0, 48000); // above threshold -> compressing + driven
        auto clean = runMode(0, 0.7f, 5.0f, 0.8f, x);
        auto fet = runMode(3, 0.7f, 5.0f, 0.8f, x);
        auto thd = [&](const std::vector<float> &y) {
            const double f1 = goertzel(y, 24000, 24000, 1000.0);
            const double h2 = goertzel(y, 24000, 24000, 2000.0);
            const double h3 = goertzel(y, 24000, 24000, 3000.0);
            return (h2 + h3) / (f1 + 1.0e-12);
        };
        const double cThd = thd(clean), fThd = thd(fet);
        CHECK(fThd > cThd * 2.0 && fThd > 0.01,
              "T10 FET adds harmonics: THD FET %.4f vs Clean %.4f", fThd, cThd);
    }

    // ---- T11: Character knob scales the analog colour (0 = clean) ----
    {
        auto x = tone(-5.0, 48000);
        auto fet0 = runMode(3, 0.7f, 5.0f, 0.0f, x); // colour off
        auto fet1 = runMode(3, 0.7f, 5.0f, 0.8f, x); // colour up
        auto thd = [&](const std::vector<float> &y) {
            const double f1 = goertzel(y, 24000, 24000, 1000.0);
            const double h2 = goertzel(y, 24000, 24000, 2000.0);
            const double h3 = goertzel(y, 24000, 24000, 3000.0);
            return (h2 + h3) / (f1 + 1.0e-12);
        };
        const double t0 = thd(fet0), t1 = thd(fet1);
        CHECK(t0 < 0.01 && t1 > t0 * 3.0,
              "T11 Character scales colour: THD char0 %.4f -> char0.8 %.4f", t0, t1);
    }

    // ---- T12: voicing harmonic signature (Opto even-forward vs OTA odd-forward) ----
    {
        auto x = tone(-8.0, 48000);
        auto ota = runMode(1, 0.7f, 8.0f, 0.9f, x);
        auto opto = runMode(2, 0.7f, 8.0f, 0.9f, x);
        auto h2over3 = [&](const std::vector<float> &y) {
            const double h2 = goertzel(y, 24000, 24000, 2000.0);
            const double h3 = goertzel(y, 24000, 24000, 3000.0);
            return h2 / (h3 + 1.0e-9);
        };
        const double rOpto = h2over3(opto), rOta = h2over3(ota);
        CHECK(rOpto > rOta,
              "T12 Opto more 2nd-harmonic (even) than OTA: h2/h3 Opto %.2f > OTA %.2f", rOpto, rOta);
    }

    // ---- T13: Release knob scales GR recovery on an exposed voicing (Clean) ----
    {
        std::vector<float> x(144000);
        for (size_t i = 0; i < x.size(); ++i)
        {
            const double amp = (i < 48000) ? 0.5623 : 0.01; // -5 dB for 1 s, then -40 dB
            x[i] = (float)(amp * std::sin(2.0 * M_PI * 1000.0 * (double)i / SR));
        }
        auto grAt = [&](float relMs, size_t target)
        {
            CompBlock c; c.setSustain(0.5f); c.setAttackMs(10.0f);
            c.setMode(0); c.setRatio(CompBlock::kRatio); c.setReleaseMs(relMs); c.prepare({SR, BLK});
            auto xx = x; size_t p = 0; float g = 0;
            while (p < target) { int n = (int)std::min<size_t>(64, target - p); c.process(xx.data() + p, n); p += (size_t)n; g = c.grDb(); }
            return (double)g;
        };
        // 200 ms after the drop: a long release still holds more GR than a short one
        const double gFast = grAt(40.0f, 48000 + 9600);
        const double gSlow = grAt(600.0f, 48000 + 9600);
        CHECK(gSlow > gFast + 1.0,
              "T13 Release knob lengthens recovery: GR@200ms 40ms %.1f -> 600ms %.1f", gFast, gSlow);
    }

    std::printf("\n%s (%d failure%s)\n", gFails ? "RESULT: FAIL" : "RESULT: ALL PASS", gFails, gFails == 1 ? "" : "s");
    return gFails ? 1 : 0;
}
