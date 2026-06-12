#pragma once
// CompBlock — pedal-style compressor + clean boost, mono, DAW rate, pre-amp.
//
// Scoped (June 2026) as the "front of amp" tool guitarists actually use:
// Dyna-Comp/optical-style squish (one Sustain knob instead of threshold/ratio)
// plus a clean Boost stage to push the amp model. Not a studio comp — that
// belongs post-rig in the DAW.
//
// Topology (feedforward, log-domain — Giannoulis/Massberg/Reiss style):
//   |x| -> dB -> soft-knee gain computer -> branching attack/release smoother
//   on the gain-reduction signal -> gain = makeup + level - GR, then boost.
//
// Mapping:
//   Sustain s (0..1):  threshold T = -10 - 35*s dB   (more sustain = deeper
//                      compression reach), fixed ratio 6:1, knee 6 dB.
//   Attack (1..50 ms): slow attack lets the pick transient through (the
//                      classic pedal-comp "bloom"). Release fixed 150 ms.
//   Level (dB):        user trim on top of auto-makeup
//                      (makeup = -T*(1-1/R)*0.6 keeps loudness roughly level
//                      as Sustain increases).
//   Boost (dB):        clean gain AFTER the comp, 0..+20 — drives the amp.
//
// Latency: zero (chain-bypass via compOn is safe).
// Verified by tests/comp_test.cpp.

#include "Blocks.h"
#include <atomic>
#include <atomic>
#include <cmath>

namespace nam_rig
{

class CompBlock : public MonoBlock
{
public:
    const char *name() const override { return "Comp/Boost"; }

    // ---- parameters (thread-safe) ----
    void setSustain(float v01) { mSustain.store(v01); }   // 0..1
    void setAttackMs(float v) { mAttackMs.store(v); }     // 1..50 ms
    void setLevelDb(float v) { mLevelDb.store(v); }       // -12..+12 dB trim
    void setBoostDb(float v) { mBoostDb.store(v); }       // 0..+20 dB clean boost

    // Fixed character constants (the "pedal" voicing)
    static constexpr float kRatio = 6.0f;
    static constexpr float kKneeDb = 6.0f;
    static constexpr float kReleaseMs = 150.0f;

    // Static transfer curve (input dB -> output dB before level/boost), used
    // by both process() and the verification harness — single source of truth.
    static float computeGainDb(float xDb, float thresholdDb)
    {
        const float over = xDb - thresholdDb;
        float yDb;
        if (over <= -kKneeDb * 0.5f)
            yDb = xDb; // below knee: unity
        else if (over >= kKneeDb * 0.5f)
            yDb = thresholdDb + over / kRatio; // above knee: ratio
        else
        {
            const float t = over + kKneeDb * 0.5f; // 0..knee
            yDb = xDb + (1.0f / kRatio - 1.0f) * t * t / (2.0f * kKneeDb);
        }
        return yDb - xDb; // gain (<= 0)
    }

    static float thresholdForSustain(float s01)
    {
        return -10.0f - 35.0f * std::min(std::max(s01, 0.0f), 1.0f);
    }
    static float makeupForThreshold(float tDb)
    {
        return -tDb * (1.0f - 1.0f / kRatio) * 0.6f;
    }

    void prepare(const BlockContext &ctx) override
    {
        mSampleRate = ctx.sampleRate;
        reset();
        mPrepared = true;
    }

    void reset() override { mGrDb = 0.0f; }

    void process(float *mono, int numSamples) override
    {
        if (!mPrepared)
            return;

        const double sr = mSampleRate;
        const float t = thresholdForSustain(mSustain.load());
        const float makeup = makeupForThreshold(t);
        const float outDb = makeup + mLevelDb.load() + mBoostDb.load();
        const float outLin = std::pow(10.0f, outDb * 0.05f);

        const float attCoef = coefForMs(mAttackMs.load(), sr);
        const float relCoef = coefForMs(kReleaseMs, sr);

        float gr = mGrDb;

        for (int i = 0; i < numSamples; ++i)
        {
            const float x = mono[i];
            const float aDb = 20.0f * std::log10(std::max(std::abs(x), 1.0e-9f));

            const float grTarget = -computeGainDb(aDb, t); // >= 0
            // branching smoother: attack when GR rises, release when it falls
            if (grTarget > gr)
                gr += attCoef * (grTarget - gr);
            else
                gr += relCoef * (grTarget - gr);

            // total gain; GR == 0 path keeps the multiply exact w.r.t. outLin
            const float g = (gr < 1.0e-4f) ? outLin
                                           : outLin * std::pow(10.0f, -gr * 0.05f);
            mono[i] = x * g;
        }

        mGrDb = (gr < 1.0e-7f) ? 0.0f : gr; // flush
        mGrDbPub.store(mGrDb); // published for the editor's GR meter
    }

    // Last block's gain reduction in dB (>= 0). UI thread.
    float grDb() const { return mGrDbPub.load(); }

private:
    std::atomic<float> mGrDbPub{0.0f};

    static float coefForMs(float ms, double sr)
    {
        return 1.0f - (float)std::exp(-1.0 / (std::max(0.01f, ms) * 0.001 * sr));
    }

    std::atomic<float> mSustain{0.5f};
    std::atomic<float> mAttackMs{15.0f};
    std::atomic<float> mLevelDb{0.0f};
    std::atomic<float> mBoostDb{0.0f};

    float mGrDb = 0.0f;
    double mSampleRate = 48000.0;
    bool mPrepared = false;
};

} // namespace nam_rig
