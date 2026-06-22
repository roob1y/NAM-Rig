#pragma once
// Plate onset fix: convolve the dry input with the derived early-reflection kernel
// (resources/plate_early_kernel.wav -> BinaryData) and ADD it to the plate wet.
// The algorithmic FDN supplies the tail; this supplies the dense, blooming onset
// that the FDN alone can't make (see plate_onset_release/PLATE_ONSET_IMPLEMENTATION.md).
#include <juce_dsp/juce_dsp.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "BinaryData.h"

namespace nam_rig
{
class EarlyConvolver
{
public:
    void prepare(double fs, int maxBlock, int /*numCh*/)
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate       = fs;
        spec.maximumBlockSize = (juce::uint32) juce::jmax(1, maxBlock);
        spec.numChannels      = 2;
        mConv.prepare(spec);
        mConv.loadImpulseResponse(BinaryData::plate_early_kernel_wav,
                                  (size_t) BinaryData::plate_early_kernel_wavSize,
                                  juce::dsp::Convolution::Stereo::yes,
                                  juce::dsp::Convolution::Trim::no,
                                  0,
                                  juce::dsp::Convolution::Normalise::no);
        mScratch.setSize(2, juce::jmax(1, maxBlock), false, false, true);
        mReady = true;
    }
    void reset() { mConv.reset(); mScratch.clear(); }

    // Convolve dryL/dryR with the early kernel and add (scaled by gain) into outL/outR.
    void addEarly(const float *dryL, const float *dryR,
                  float *outL, float *outR, int n, float gain)
    {
        if (!mReady || gain <= 0.0f) return;
        if (mScratch.getNumSamples() < n) mScratch.setSize(2, n, false, false, true);
        float *sL = mScratch.getWritePointer(0);
        float *sR = mScratch.getWritePointer(1);
        const bool stereo = (outR != outL);
        for (int i = 0; i < n; ++i) { sL[i] = dryL[i]; sR[i] = (dryR ? dryR[i] : dryL[i]); }
        juce::dsp::AudioBlock<float> blk(mScratch.getArrayOfWritePointers(), 2, (size_t) n);
        juce::dsp::ProcessContextReplacing<float> ctx(blk);
        mConv.process(ctx);
        for (int i = 0; i < n; ++i) { outL[i] += gain * sL[i]; if (stereo) outR[i] += gain * sR[i]; }
    }

private:
    juce::dsp::Convolution mConv;
    juce::AudioBuffer<float> mScratch;
    bool mReady = false;
};
} // namespace nam_rig
