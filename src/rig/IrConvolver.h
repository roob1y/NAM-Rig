#pragma once
// Derived-IR convolution for the hybrid plate/room: convolves the dry input with a
// brand-free derived IR (resources/derived_irs/*.wav -> BinaryData) and REPLACES the
// wet with it. The derived IR IS the matched character (dense-from-onset, no FDN bloom);
// a TailSustainer adds adjustable decay above the default. One IrConvolver per IR.
// Real-JUCE only — gated by NAM_DERIVED_CONV in ReverbBlock.h (offline falls back to the
// algorithmic engine, so the offline harness + reverb_test still build/run).
#include <juce_dsp/juce_dsp.h>
#include <juce_audio_basics/juce_audio_basics.h>

namespace nam_rig
{
class IrConvolver
{
public:
    // irData/irSize come from BinaryData (e.g. BinaryData::plate_derived_wav / _wavSize).
    void prepare(double fs, int maxBlock, const void *irData, size_t irSize)
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate       = fs;
        spec.maximumBlockSize = (juce::uint32) juce::jmax(1, maxBlock);
        spec.numChannels      = 2;
        mConv.prepare(spec);
        mConv.loadImpulseResponse(irData, irSize,
                                  juce::dsp::Convolution::Stereo::yes,     // true-stereo: per-channel IR
                                  juce::dsp::Convolution::Trim::no,
                                  0,
                                  juce::dsp::Convolution::Normalise::no);  // keep the matched level
        mScratch.setSize(2, juce::jmax(1, maxBlock), false, false, true);
        mReady = true;
    }
    void reset() { mConv.reset(); mScratch.clear(); }

    // out = conv(dry) (REPLACES out). dryR may be null (mono dry -> both channels).
    void renderReplace(const float *dryL, const float *dryR, float *outL, float *outR, int n)
    {
        if (!mReady) { for (int i=0;i<n;++i){ outL[i]=0.0f; if(outR!=outL) outR[i]=0.0f; } return; }
        if (mScratch.getNumSamples() < n) mScratch.setSize(2, n, false, false, true);
        float *sL = mScratch.getWritePointer(0);
        float *sR = mScratch.getWritePointer(1);
        const bool stereo = (outR != outL);
        for (int i = 0; i < n; ++i) { sL[i] = dryL[i]; sR[i] = (dryR ? dryR[i] : dryL[i]); }
        juce::dsp::AudioBlock<float> blk(mScratch.getArrayOfWritePointers(), 2, (size_t) n);
        juce::dsp::ProcessContextReplacing<float> ctx(blk);
        mConv.process(ctx);
        for (int i = 0; i < n; ++i) { outL[i] = sL[i]; if (stereo) outR[i] = sR[i]; }
    }

private:
    juce::dsp::Convolution mConv;
    juce::AudioBuffer<float> mScratch;
    bool mReady = false;
};
} // namespace nam_rig
