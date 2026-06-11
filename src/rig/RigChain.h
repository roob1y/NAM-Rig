#pragma once
// RigChain — the fixed serial chain host. Owns all blocks in their fixed
// order, runs the mono section on channel 0, fans out to stereo after the
// cab, then runs the stereo section. Total PDC = sum of block latencies.
//
// Shared (no copy drift) between PluginProcessor::processBlock and the
// offline harness tests/rig_chain_process.cpp — the same rule that gave
// NAM-AA its verified-chain guarantee via AaEngine/SrcLayer.

#include "Blocks.h"
#include "GateBlock.h"
#include "CompBlock.h"
#include "AmpBlock.h"
#include "EqBlock.h"
#include "CabBlock.h"

namespace nam_rig
{

class RigChain
{
public:
    void prepare(double sampleRate, int maxBlockSize)
    {
        const BlockContext ctx{sampleRate, maxBlockSize};
        for (auto *b : monoBlocks())
            b->prepare(ctx);
        for (auto *b : stereoBlocks())
            b->prepare(ctx);
        mPrepared = true;
    }

    void reset()
    {
        for (auto *b : monoBlocks())
            b->reset();
        for (auto *b : stereoBlocks())
            b->reset();
    }

    bool isPrepared() const { return mPrepared; }

    // Full chain on a DAW-rate buffer (1 or 2 channels). Mono section runs on
    // channel 0; the stereo fan-out copies ch0 -> ch1 (post-cab); the stereo
    // section then processes L/R (on a mono buffer, L == R pointer-wise is not
    // allowed — we pass ch0 twice only if there is a single channel and the
    // block must behave mono-safely; v1 stubs are passthrough so this is
    // exercised once real stereo blocks land).
    void process(juce::AudioBuffer<float> &buffer)
    {
        const int numSamples = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();
        if (numSamples == 0)
            return;

        float *ch0 = buffer.getWritePointer(0);

        // ---- mono section ----
        for (auto *b : monoBlocks())
            if (!b->isBypassed())
                b->process(ch0, numSamples);

        // ---- stereo fan-out (post-cab) ----
        for (int ch = 1; ch < numChannels; ++ch)
            buffer.copyFrom(ch, 0, buffer, 0, 0, numSamples);

        // ---- stereo section ----
        float *left = ch0;
        float *right = (numChannels > 1) ? buffer.getWritePointer(1) : ch0;
        for (auto *b : stereoBlocks())
            if (!b->isBypassed())
                b->process(left, right, numSamples);
    }

    // Total chain PDC in DAW samples (sum of block latencies; bypassed
    // latency-bearing blocks still report — they stay in the path).
    double latencySamples() const
    {
        double total = 0.0;
        for (auto *b : monoBlocks())
            total += b->latencySamples();
        for (auto *b : stereoBlocks())
            total += b->latencySamples();
        return total;
    }

    // ---- the blocks, fixed order ----
    GateBlock gate;
    CompBlock comp;
    AmpBlock amp;
    EqBlock eq;
    CabBlock cab;
    ModBlock mod;
    DelayBlock delay;
    ReverbBlock reverb;

private:
    std::array<MonoBlock *, 5> monoBlocks() { return {&gate, &comp, &amp, &eq, &cab}; }
    std::array<const MonoBlock *, 5> monoBlocks() const { return {&gate, &comp, &amp, &eq, &cab}; }
    std::array<StereoBlock *, 3> stereoBlocks() { return {&mod, &delay, &reverb}; }
    std::array<const StereoBlock *, 3> stereoBlocks() const { return {&mod, &delay, &reverb}; }

    bool mPrepared = false;
};

} // namespace nam_rig
