// comp_test — offline verification harness for CompBlock (measurement-first).
// Exits nonzero on any FAIL. First run expected on Windows (sandbox was down
// when this block was written) — alongside gate_test.
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

// steady-state RMS dB over the last 100 ms of a buffer
static double tailDb(const std::vector<float> &x)
{
    const size_t n = 4800;
    double e = 0;
    for (size_t i = x.size() - n; i < x.size(); ++i) e += (double)x[i] * x[i];
    return 10.0 * std::log10(e / (double)n) + 3.0103; // RMS->peak of sine
}

int main()
{
    const float sustain = 0.5f;
    const float T = CompBlock::thresholdForSustain(sustain);   // -27.5 dB
    const float M = CompBlock::makeupForThreshold(T);

    // ---- T1: static transfer curve matches the analytic single source of truth ----
    {
        bool ok = true;
        double worst = 0;
        for (double inDb : {-50.0, -40.0, -30.0, -25.0, -20.0, -10.0, -5.0})
        {
            CompBlock c; c.setSustain(sustain); c.setAttackMs(1.0f); c.prepare({SR, BLK});
            auto x = tone(inDb, 48000);
            run(c, x);
            const double outDb = tailDb(x);
            const double want = inDb + CompBlock::computeGainDb((float)inDb, T) + M;
            const double err = std::abs(outDb - want);
            if (err > worst) worst = err;
            // ~0.27 dB systematic is expected: the detector sees instantaneous
            // |sine| (log-domain ripple), the analytic curve assumes peak level.
            if (err > 0.4) ok = false;
        }
        CHECK(ok, "T1 static curve matches analytic (worst err %.2f dB, want < 0.4)", worst);
    }

    // ---- T2: slope above knee == 1/ratio ----
    {
        auto level = [&](double inDb)
        {
            CompBlock c; c.setSustain(sustain); c.setAttackMs(1.0f); c.prepare({SR, BLK});
            auto x = tone(inDb, 48000);
            run(c, x);
            return tailDb(x);
        };
        const double slope = (level(-5.0) - level(-15.0)) / 10.0;
        CHECK(std::abs(slope - 1.0 / CompBlock::kRatio) < 0.05,
              "T2 slope above knee %.3f (want %.3f +/- 0.05)", slope, 1.0 / CompBlock::kRatio);
    }

    // ---- T3: attack and release timing ----
    {
        CompBlock c; c.setSustain(sustain); c.setAttackMs(10.0f); c.prepare({SR, BLK});
        // -40 dB (below T) for 1 s, then -5 dB for 1 s, then -40 dB again
        std::vector<float> x(144000);
        for (size_t i = 0; i < x.size(); ++i)
        {
            const double amp = (i < 48000 || i >= 96000) ? 0.01 : 0.5623; // -40 / -5 dB
            x[i] = (float)(amp * std::sin(2.0 * M_PI * 1000.0 * (double)i / SR));
        }
        run(c, x);
        auto envDbAt = [&](size_t center)
        {
            double e = 0; for (size_t i = center - 480; i < center + 480; ++i) e += (double)x[i] * x[i];
            return 10.0 * std::log10(e / 960.0);
        };
        // attack: GR settled by 5x attack (50 ms) into the loud section
        const double early = envDbAt(48000 + 240);            // ~5 ms in: barely compressed yet
        const double settled = envDbAt(48000 + 4800);         // 100 ms in: fully compressed
        CHECK(early - settled > 2.0,
              "T3 pick transient passes before compression clamps (%.1f dB bloom)", early - settled);
        // release: ~5x release (750 ms) after the loud section, GR ~ recovered
        const double tail1 = envDbAt(96000 + 4800);           // 100 ms after drop: still compressed-ish
        const double tail2 = envDbAt(96000 + 43200);          // 900 ms after: recovered
        CHECK(tail2 - tail1 > 1.0, "T3 release recovers gain after loud passage (%.1f dB)", tail2 - tail1);
    }

    // ---- T4: sustain does what it says on a decaying note ----
    {
        auto t30 = [&](float s)
        {
            CompBlock c; c.setSustain(s); c.prepare({SR, BLK});
            // exp-decaying 220 Hz tone: -10 dB start, -40 dB/s decay
            std::vector<float> x(144000);
            for (size_t i = 0; i < x.size(); ++i)
            {
                const double t = (double)i / SR;
                x[i] = (float)(std::pow(10.0, (-10.0 - 40.0 * t) / 20.0) * std::sin(2.0 * M_PI * 220.0 * t));
            }
            run(c, x);
            // time for output env to fall 30 dB below its level at t = 100 ms.
            // (Reference must be AFTER the attack settles: measuring from t=0
            // catches the uncompressed pick bloom, which inflates the start
            // level for high sustain and falsely shortens T30.)
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

    // ---- T5: boost is exact clean gain below threshold ----
    {
        CompBlock c; c.setSustain(0.0f); c.setLevelDb(0.0f); c.setBoostDb(12.0f); c.prepare({SR, BLK});
        auto x = tone(-40.0, 48000); // well below T(s=0) = -10 dB
        auto ref = x;
        run(c, x);
        // expected constant gain: makeup(T=-10) + 12 dB
        const float gDb = CompBlock::makeupForThreshold(CompBlock::thresholdForSustain(0.0f)) + 12.0f;
        const float g = std::pow(10.0f, gDb / 20.0f);
        double worst = 0;
        for (size_t i = 4800; i < x.size(); ++i)
        {
            const double want = (double)ref[i] * g;
            const double err = std::abs((double)x[i] - want);
            if (err > worst) worst = err;
        }
        CHECK(worst < 1.0e-6, "T5 sub-threshold boost is constant clean gain (worst err %.2e)", worst);
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

    std::printf("\n%s (%d failure%s)\n", gFails ? "RESULT: FAIL" : "RESULT: ALL PASS", gFails, gFails == 1 ? "" : "s");
    return gFails ? 1 : 0;
}
