#pragma once
// CabBlock — mono cabinet IR loader/convolver (priority #2 after the amp).
// Uniform-partitioned juce::dsp::Convolution with zero added latency, so PDC
// is unaffected; trims/normalises on load. Stereo fan-out happens AFTER this
// block (scoping decision: mono through cab; dual-IR stereo is a later option).
//
// loadImpulseResponse is internally async + lock-free swapped by JUCE, so it's
// safe to call from the message thread while audio runs.

#include "Blocks.h"
#include <juce_dsp/juce_dsp.h>

namespace nam_rig
{

class CabBlock : public MonoBlock
{
public:
    const char *name() const override { return "Cab IR"; }

    void prepare(const BlockContext &ctx) override
    {
        mSpec.sampleRate = ctx.sampleRate;
        mSpec.maximumBlockSize = (juce::uint32)ctx.maxBlockSize;
        mSpec.numChannels = 1;
        mConv.prepare(mSpec);
        mPrepared = true;
    }

    void reset() override { mConv.reset(); }

    // Load a cab IR (wav/aiff). Trimmed and normalised; resampled to the
    // current rate by JUCE. Returns false if the file doesn't exist.
    bool loadIr(const juce::File &irFile)
    {
        if (!irFile.existsAsFile())
            return false;
        mConv.loadImpulseResponse(irFile,
                                  juce::dsp::Convolution::Stereo::no,
                                  juce::dsp::Convolution::Trim::yes,
                                  0, // full length
                                  juce::dsp::Convolution::Normalise::yes);
        mIrName = irFile.getFileNameWithoutExtension();
        mIrLoaded.store(true);
        return true;
    }

    bool isIrLoaded() const { return mIrLoaded.load(); }
    juce::String irName() const { return mIrName; }

    void process(float *mono, int numSamples) override
    {
        if (!mIrLoaded.load() || !mPrepared)
            return; // passthrough until an IR is loaded

        juce::dsp::AudioBlock<float> block(&mono, 1, (size_t)numSamples);
        juce::dsp::ProcessContextReplacing<float> ctx(block);
        mConv.process(ctx);
    }

    double latencySamples() const override
    {
        // Uniform-partitioned convolution: zero added latency by construction.
        return mPrepared ? (double)mConv.getLatency() : 0.0;
    }

private:
    juce::dsp::Convolution mConv; // default: uniform-partitioned, zero latency
    juce::dsp::ProcessSpec mSpec{48000.0, 512, 1};
    juce::String mIrName;
    std::atomic<bool> mIrLoaded{false};
    bool mPrepared = false;
};

} // namespace nam_rig
