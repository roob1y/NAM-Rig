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
    }

    void reset() override { mDcState = 0.0f; }

    // Requested AA factor (1/2/4/8/16/32), set from the parameter/offline logic
    // before chain.process(). Latency depends on it — re-report PDC on change.
    void setRequestedFactor(int factor) { mRequestedFactor.store(factor); }
    int requestedFactor() const { return mRequestedFactor.load(); }

    void process(float *mono, int numSamples) override
    {
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
    float mDcState = 0.0f;
};

} // namespace nam_rig
