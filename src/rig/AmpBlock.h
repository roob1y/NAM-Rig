#pragma once
// AmpBlock — the chain's anchor: wraps nam_aa::AaEngine (the SAME engine code
// the NAM-AA plugin runs — model set 1x..32x, SRC layer, oversamplers,
// routing). The engine's internal SRC/oversampling is invisible to neighbor
// blocks: DAW-rate mono in, DAW-rate mono out, latency reported here and
// summed by the chain.
//
// Includes the DC blocker (mono, post-engine) that NAM-AA applies after the
// model chain, so the amp block sounds identical to the NAM-AA plugin.

#include "Blocks.h"
#include "AaEngine.h"

#include <atomic>
#include <vector>
#include <cmath>
#include <algorithm>

namespace nam_rig
{

class AmpBlock : public MonoBlock
{
public:
    const char *name() const override { return "NAM Amp"; }

    void prepare(const BlockContext &ctx) override
    {
        mEngine.prepare(ctx.sampleRate, ctx.maxBlockSize);
        mDcState = 0.0f;
        mDelay.assign(4096, 0.0f); // covers any SRC group delay + block
        mDelayCap = (int)mDelay.size();
        mDelayWrite = 0;
    }

    void reset() override
    {
        mDcState = 0.0f;
        std::fill(mDelay.begin(), mDelay.end(), 0.0f);
        mDelayWrite = 0;
    }

    // Requested AA factor (1/2/4/8/16/32), set from the parameter/offline logic
    // before chain.process(). Latency depends on it — re-report PDC on change.
    void setRequestedFactor(int factor) { mRequestedFactor.store(factor); }
    int requestedFactor() const { return mRequestedFactor.load(); }

    // Amp bypass: skip the model but keep the SAME reported latency by passing the
    // dry signal through a matching delay. This keeps PDC + dual-rig alignment
    // constant (no host re-sync), unlike a plain chain skip.
    void setBypass(bool b) { mBypass.store(b); }
    bool bypassed() const { return mBypass.load(); }

    void process(float *mono, int numSamples) override
    {
        if (mBypass.load())
        {
            // Dry, delayed by the engine's group delay so latency is unchanged.
            int L = (int)std::lround(latencySamples());
            if (L < 0) L = 0;
            if (L > mDelayCap - 1) L = mDelayCap - 1;
            if (L > 0 && mDelayCap > 0)
            {
                for (int i = 0; i < numSamples; ++i)
                {
                    mDelay[(size_t)mDelayWrite] = mono[i];
                    int rd = mDelayWrite - L;
                    if (rd < 0) rd += mDelayCap;
                    mono[i] = mDelay[(size_t)rd];
                    if (++mDelayWrite >= mDelayCap) mDelayWrite = 0;
                }
            }
            mEngaged.store(0);
            return;
        }

        // Non-positive = passthrough (no model for path / model lock contended):
        // leave the buffer untouched, matching NAM-AA's behavior.
        const int engaged = mEngine.process(mono, numSamples, mRequestedFactor.load());
        mEngaged.store(engaged);

        // DC block whenever a model exists (NAM-AA parity).
        if (mEngine.anyModelLoaded())
        {
            const float R = 0.9999f;
            float s = mDcState;
            for (int i = 0; i < numSamples; ++i)
            {
                const float x = mono[i];
                const float y = x - s;
                s = x - y * R;
                mono[i] = y;
            }
            mDcState = s;
        }
    }

    double latencySamples() const override
    {
        // Only report engine latency when a model is actually loaded. With no
        // model, process() is passthrough (buffer untouched, no SRC/oversampling
        // runs), so the engine imposes ZERO delay — reporting its prepared SRC
        // latency here would be a phantom (PDC reported but not imposed). Off-rate
        // sessions correctly get the real SRC group delay back once a model loads.
        return (mEngine.isPrepared() && mEngine.anyModelLoaded())
                   ? mEngine.latencySamples(mRequestedFactor.load()) : 0.0;
    }

    // Model management / status — pass-throughs to the shared engine.
    nam_aa::AaEngine &engine() { return mEngine; }
    const nam_aa::AaEngine &engine() const { return mEngine; }
    // Engaged model factor on the last processed block (0 = passthrough,
    // kSkipped = lock contended). For UI status.
    int engagedFactor() const { return mEngaged.load(); }

private:
    nam_aa::AaEngine mEngine;
    std::atomic<int> mRequestedFactor{1};
    std::atomic<int> mEngaged{0};
    std::atomic<bool> mBypass{false};
    float mDcState = 0.0f;
    std::vector<float> mDelay;
    int mDelayWrite = 0, mDelayCap = 0;
};

} // namespace nam_rig
