// premod_test — offline verification harness for the MONO front-of-amp
// modulation pedal (PreModBlock). Measurement-first; exits nonzero on any FAIL.
//
//   T1  chorus output is finite, bounded, and non-silent (audibly modulates)
//   T2  Mix = 0 is bit-exact dry (block is transparent at zero mix)
//   T3  chorus wet path actually MOVES (a swept comb: output varies vs a
//       fixed-delay reference -> pitch/comb modulation is present)
//   T4  un-voiced types (Phaser/Flanger/Tremolo/Uni-Vibe) are exact passthrough
//       (scaffold stubs -> transparent, never silent)
//   T5  tempo sync resolves the LFO rate from BPM x division (effectiveRateHz)
//   T6  free Rate is capped per pedal (chorus <= kChorusMaxRateHz; sync uncapped)
//   T7  determinism: same input + same params -> identical output

#include "rig/PreModBlock.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace nam_rig;

static int g_fail = 0;
static void check(bool ok, const char *msg)
{
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", msg);
    if (!ok) ++g_fail;
}

// A short guitar-ish test tone (a few partials) at fs.
static std::vector<float> tone(int n, double fs, double f0 = 220.0)
{
    std::vector<float> x((size_t)n);
    for (int i = 0; i < n; ++i)
    {
        const double t = (double)i / fs;
        x[(size_t)i] = (float)(0.6 * std::sin(2.0 * M_PI * f0 * t)
                             + 0.3 * std::sin(2.0 * M_PI * 2.0 * f0 * t)
                             + 0.1 * std::sin(2.0 * M_PI * 3.0 * f0 * t));
    }
    return x;
}

static bool allFinite(const std::vector<float> &x)
{
    for (float v : x) if (!std::isfinite(v)) return false;
    return true;
}
static float maxAbs(const std::vector<float> &x)
{
    float m = 0.0f;
    for (float v : x) m = std::max(m, std::fabs(v));
    return m;
}
static double rmsDiff(const std::vector<float> &a, const std::vector<float> &b)
{
    double s = 0.0;
    const size_t n = a.size();
    for (size_t i = 0; i < n; ++i) { const double d = (double)a[i] - b[i]; s += d * d; }
    return std::sqrt(s / (double)n);
}

// Run the block over a copy of `in` with the given setup lambda.
template <class F>
static std::vector<float> run(const std::vector<float> &in, double fs, F setup)
{
    PreModBlock pm;
    BlockContext ctx{fs, (int)in.size()};
    pm.prepare(ctx);
    setup(pm);
    std::vector<float> y = in;
    pm.process(y.data(), (int)y.size());
    return y;
}

int main()
{
    const double fs = 48000.0;
    const int n = 24000; // 0.5 s
    const auto x = tone(n, fs);

    std::printf("premod_test — mono front-of-amp modulation pedal\n");

    // T1: chorus finite/bounded/non-silent + audibly modulating.
    {
        auto y = run(x, fs, [](PreModBlock &pm) {
            pm.setType(PreModBlock::kChorus);
            pm.setRateHz(1.2f);
            pm.setDepth(0.7f);
            pm.setMix(0.5f);
        });
        const bool fin = allFinite(y);
        const float pk = maxAbs(y);
        const double diff = rmsDiff(x, y); // must differ from dry (effect audible)
        check(fin, "T1 chorus output finite");
        check(pk > 0.05f && pk < 4.0f, "T1 chorus bounded + non-silent");
        check(diff > 1.0e-3, "T1 chorus audibly differs from dry");
    }

    // T2: Mix = 0 -> transparent (dry). Mix is de-zippered with a 10 ms one-pole
    // (so it never clicks), so it settles to dry rather than being bit-exact from
    // sample zero; check the settled tail matches the dry input tightly.
    {
        auto y = run(x, fs, [](PreModBlock &pm) {
            pm.setType(PreModBlock::kChorus);
            pm.setDepth(1.0f);
            pm.setMix(0.0f);
        });
        const size_t half = x.size() / 2; // past the 10 ms smoothing ramp
        float maxDev = 0.0f;
        for (size_t i = half; i < x.size(); ++i) maxDev = std::max(maxDev, std::fabs(y[i] - x[i]));
        check(maxDev < 1.0e-4f, "T2 Mix=0 settles to dry (transparent)");
    }

    // T3: the chorus wet path moves (swept vs a fixed LFO). Compare a normal
    // moving-LFO chorus against a rate-0 (static delay) chorus at the same depth:
    // the moving one must differ, proving the sweep (comb/pitch motion) is real.
    {
        auto moving = run(x, fs, [](PreModBlock &pm) {
            pm.setType(PreModBlock::kChorus);
            pm.setRateHz(2.0f);
            pm.setDepth(0.8f);
            pm.setMix(1.0f); // full wet -> isolate the wet path
        });
        auto still = run(x, fs, [](PreModBlock &pm) {
            pm.setType(PreModBlock::kChorus);
            pm.setRateHz(0.0f); // no sweep -> a fixed delay
            pm.setDepth(0.8f);
            pm.setMix(1.0f);
        });
        check(allFinite(moving) && allFinite(still), "T3 wet paths finite");
        check(rmsDiff(moving, still) > 1.0e-3, "T3 sweep moves the comb (moving != static)");
    }

    // T4: un-voiced pedal types are exact passthrough (scaffold stubs).
    {
        const PreModBlock::Type stubs[] = {PreModBlock::kPhaser, PreModBlock::kFlanger,
                                           PreModBlock::kTremolo, PreModBlock::kUniVibe};
        bool allPass = true;
        for (auto ty : stubs)
        {
            auto y = run(x, fs, [ty](PreModBlock &pm) {
                pm.setType(ty);
                pm.setDepth(1.0f);
                pm.setMix(1.0f);
            });
            for (size_t i = 0; i < x.size(); ++i)
                if (y[i] != x[i]) { allPass = false; break; }
        }
        check(allPass, "T4 un-voiced types are exact passthrough (not silent)");
    }

    // T5: tempo sync resolves rate from BPM x division. At 120 BPM, "1/4" (index 3,
    // 1 beat) -> 2 Hz; "1/8" (index 6, 0.5 beat) -> 4 Hz.
    {
        PreModBlock pm;
        pm.prepare({fs, 512});
        pm.setBpm(120.0);
        pm.setSyncIndex(3); // 1/4
        const float r14 = pm.effectiveRateHz();
        pm.setSyncIndex(6); // 1/8
        const float r18 = pm.effectiveRateHz();
        check(std::fabs(r14 - 2.0f) < 1.0e-3f, "T5 sync 1/4 @120 = 2 Hz");
        check(std::fabs(r18 - 4.0f) < 1.0e-3f, "T5 sync 1/8 @120 = 4 Hz");
    }

    // T6: free-rate cap. Ask for 20 Hz on chorus (free) -> clamped to the chorus
    // ceiling; sync is NOT clamped (honours the host division even if high).
    {
        PreModBlock pm;
        pm.prepare({fs, 512});
        pm.setType(PreModBlock::kChorus);
        pm.setSyncIndex(0); // free
        pm.setRateHz(20.0f);
        const bool capped = pm.effectiveRateHz() <= PreModBlock::kChorusMaxRateHz + 1.0e-4f;
        pm.setBpm(240.0);
        pm.setSyncIndex(9); // 1/16 @240 BPM = 16 Hz -> uncapped
        const bool syncFree = pm.effectiveRateHz() > PreModBlock::kChorusMaxRateHz;
        check(capped, "T6 free chorus rate capped to the chorus ceiling");
        check(syncFree, "T6 sync ignores the free-rate cap");
    }

    // T7: determinism.
    {
        auto a = run(x, fs, [](PreModBlock &pm) {
            pm.setType(PreModBlock::kChorus); pm.setRateHz(1.5f); pm.setDepth(0.6f); pm.setMix(0.5f);
        });
        auto b = run(x, fs, [](PreModBlock &pm) {
            pm.setType(PreModBlock::kChorus); pm.setRateHz(1.5f); pm.setDepth(0.6f); pm.setMix(0.5f);
        });
        bool same = (a.size() == b.size());
        for (size_t i = 0; same && i < a.size(); ++i) same = (a[i] == b[i]);
        check(same, "T7 deterministic (same setup -> identical output)");
    }

    std::printf("%s (%d failures)\n", g_fail == 0 ? "ALL PASS" : "FAILURES", g_fail);
    return g_fail == 0 ? 0 : 1;
}
