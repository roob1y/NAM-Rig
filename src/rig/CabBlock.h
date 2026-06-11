#pragma once
// CabBlock — mono cabinet IR loader/convolver (priority #2 after the amp).
// Uniform-partitioned juce::dsp::Convolution with zero added latency, so PDC
// is unaffected; normalised to unity energy on load. Stereo fan-out happens
// AFTER this block (scoping decision: mono through cab; dual-IR stereo is a
// later option).
//
// loadImpulseResponse is internally async + lock-free swapped by JUCE, so it's
// safe to call from the message thread while audio runs.

#include "Blocks.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include <cmath>

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

    // Load a cab IR (wav/aiff), resampled to the current rate by JUCE.
    // Normalised to UNITY ENERGY (1/sqrt(sum(ir^2))) — a unit impulse stays a
    // unit impulse and overall loudness is consistent across IRs. (JUCE's
    // Normalise::yes scales everything to 0.125 ≈ -18 dB instead — verified
    // empirically — so we normalise ourselves and pass Normalise::no.)
    // Returns false if the file doesn't exist or can't be read.
    bool loadIr(const juce::File &irFile)
    {
        if (!irFile.existsAsFile())
            return false;

        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(irFile));
        if (!reader || reader->lengthInSamples <= 0)
            return false;

        juce::AudioBuffer<float> ir(1, (int)reader->lengthInSamples);
        reader->read(&ir, 0, (int)reader->lengthInSamples, 0, true, false);

        double energy = 0.0;
        const float *d = ir.getReadPointer(0);
        for (int i = 0; i < ir.getNumSamples(); ++i)
            energy += (double)d[i] * d[i];
        if (energy > 0.0)
            ir.applyGain((float)(1.0 / std::sqrt(energy)));

        mConv.loadImpulseResponse(std::move(ir), reader->sampleRate,
                                  juce::dsp::Convolution::Stereo::no,
                                  juce::dsp::Convolution::Trim::yes,
                                  juce::dsp::Convolution::Normalise::no);
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
