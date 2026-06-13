#pragma once
// RigChain — the fixed serial chain host, now with a DUAL-RIG core.
//
//   [mono shared pre]  gate -> comp/drive
//                          |
//          split ----------+----------
//          |                          |
//   Rig A: amp  -> eq  -> cab    Rig B: ampB -> eqB -> cabB     (mono each)
//          | (align + pol)            | (align + pol)
//          +------- mix (mode + per-rig level/pan -> stereo) ---+
//                          |
//   [stereo shared post]  mod -> delay -> reverb
//
// Mode: SoloA / SoloB / Dual. Solo plays one rig centered at unity (pan
// bypassed) so SoloA is BIT-EXACT to the old single chain (the regression
// gate); Dual places both rigs with an equal-power pan law and per-rig level.
// v1 shares ONE AA/oversampling setting across both amps, so the two voices
// have equal latency and need no inter-voice alignment delay.
//
// Phase alignment: each rig has a fractional align delay + a polarity flip,
// driven from PhaseAlign::measure() on the two rendered voice outputs (see
// PhaseAlign.h) so it works whether the cab is an IR or baked into the .nam.
// Defaults (delay 0, polarity +1) skip both paths, keeping SoloA bit-exact.
//
// Shared (no copy drift) between PluginProcessor::processBlock and the offline
// harnesses (tests/rig_chain_process.cpp, tests/dualrig_test.cpp).

#include "Blocks.h"
#include "GateBlock.h"
#include "CompBlock.h"
#include "AmpBlock.h"
#include "EqBlock.h"
#include "CabBlock.h"
#include "ModBlock.h"
#include "DelayBlock.h"
#include "ReverbBlock.h"
#include "Lfo.h"        // FracDelayLine (align delay)
#include "PhaseAlign.h" // cross-correlation measurement

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace nam_rig
{

class RigChain
{
public:
    enum Mode { SoloA = 0, SoloB = 1, Dual = 2 };
    static constexpr int kMaxAlignSamples = 4096; // ~85 ms at 48k

    void prepare(double sampleRate, int maxBlockSize)
    {
        const BlockContext ctx{sampleRate, maxBlockSize};
        for (auto *b : allMonoBlocks())
            b->prepare(ctx);
        for (auto *b : stereoBlocks())
            b->prepare(ctx);
        mVoiceA.assign((size_t)juce::jmax(1, maxBlockSize), 0.0f);
        mVoiceB.assign((size_t)juce::jmax(1, maxBlockSize), 0.0f);
        mFdlA.prepare(kMaxAlignSamples);
        mFdlB.prepare(kMaxAlignSamples);
        mPrepared = true;
    }

    void reset()
    {
        for (auto *b : allMonoBlocks())
            b->reset();
        for (auto *b : stereoBlocks())
            b->reset();
        mFdlA.reset();
        mFdlB.reset();
    }

    bool isPrepared() const { return mPrepared; }

    // ---- mixer controls (message thread; cheap scalars) ----
    void setMode(int mode) { mMode = juce::jlimit(0, 2, mode); }
    int mode() const { return mMode; }
    void setLevelA(float linear) { mLevelA = linear; }
    void setLevelB(float linear) { mLevelB = linear; }
    void setPanA(float pan) { mPanA = juce::jlimit(-1.0f, 1.0f, pan); }
    void setPanB(float pan) { mPanB = juce::jlimit(-1.0f, 1.0f, pan); }

    // ---- phase alignment controls ----
    void setPolarityA(bool invert) { mPolA = invert ? -1.0f : 1.0f; }
    void setPolarityB(bool invert) { mPolB = invert ? -1.0f : 1.0f; }
    void setAlignA(double d) { mAlignA = juce::jlimit(0.0, (double)kMaxAlignSamples, d); }
    void setAlignB(double d) { mAlignB = juce::jlimit(0.0, (double)kMaxAlignSamples, d); }
    double alignA() const { return mAlignA; }
    double alignB() const { return mAlignB; }

    // Apply a measured A/B lag (samples; >0 => B later than A, so delay A):
    // delays the earlier voice so the two line up. Manual nudge uses setAlignA/B.
    void setAlignmentLag(double lagSamples)
    {
        if (lagSamples >= 0.0) { setAlignA(lagSamples); mAlignB = 0.0; }
        else { setAlignB(-lagSamples); mAlignA = 0.0; }
    }
    // Full auto-align from a PhaseAlign::measure() result (lag + polarity).
    void applyAlignment(const AlignResult &r)
    {
        setAlignmentLag(r.lagSamples);
        setPolarityB(r.invert);
    }

    // Full chain on a DAW-rate buffer (1 = mono fold, 2 = stereo).
    void process(juce::AudioBuffer<float> &buffer)
    {
        const int numSamples = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();
        if (numSamples == 0)
            return;

        float *ch0 = buffer.getWritePointer(0);

        // ---- shared mono pre ----
        if (!gate.isBypassed())
            gate.process(ch0, numSamples);
        if (!comp.isBypassed())
            comp.process(ch0, numSamples);

        // ---- split into the two voice buffers ----
        float *vA = mVoiceA.data();
        float *vB = mVoiceB.data();
        std::memcpy(vA, ch0, (size_t)numSamples * sizeof(float));
        const bool runB = (mMode != SoloA);
        const bool runA = (mMode != SoloB);
        if (runB)
            std::memcpy(vB, ch0, (size_t)numSamples * sizeof(float));

        // ---- per-rig voices (amp -> eq -> cab), mono ----
        if (runA)
        {
            if (!amp.isBypassed()) amp.process(vA, numSamples);
            if (!eq.isBypassed()) eq.process(vA, numSamples);
            if (!cab.isBypassed()) cab.process(vA, numSamples);
            alignVoice(mFdlA, vA, numSamples, mAlignA, mPolA);
        }
        if (runB)
        {
            if (!ampB.isBypassed()) ampB.process(vB, numSamples);
            if (!eqB.isBypassed()) eqB.process(vB, numSamples);
            if (!cabB.isBypassed()) cabB.process(vB, numSamples);
            alignVoice(mFdlB, vB, numSamples, mAlignB, mPolB);
        }

        // ---- mix to the output bus ----
        float *left = ch0;
        float *right = (numChannels > 1) ? buffer.getWritePointer(1) : ch0;
        mix(left, right, vA, vB, numSamples, numChannels);

        // ---- shared stereo post ----
        for (auto *b : stereoBlocks())
            if (!b->isBypassed())
                b->process(left, right, numSamples);
    }

    // Total chain PDC in DAW samples: shared pre + the parallel voice section
    // (max of the two voices, INCLUDING their align delays) + shared post.
    double latencySamples() const
    {
        const double pre = gate.latencySamples() + comp.latencySamples();
        const double voiceA = amp.latencySamples() + eq.latencySamples()
                              + cab.latencySamples() + mAlignA;
        const double voiceB = ampB.latencySamples() + eqB.latencySamples()
                              + cabB.latencySamples() + mAlignB;
        double voice = voiceA;
        if (mMode == SoloB)
            voice = voiceB;
        else if (mMode == Dual)
            voice = std::max(voiceA, voiceB);
        double post = 0.0;
        for (auto *b : stereoBlocks())
            post += b->latencySamples();
        return pre + voice + post;
    }

    // Equal-power pan law: pan in [-1,+1] -> (gainL, gainR), center = -3 dB.
    static void panGains(float pan, float &gL, float &gR)
    {
        const float t = juce::jlimit(0.0f, 1.0f, (pan + 1.0f) * 0.5f);
        const float a = t * (juce::MathConstants<float>::pi * 0.5f);
        gL = std::cos(a);
        gR = std::sin(a);
    }

    // ---- the blocks ----
    GateBlock gate; // shared pre
    CompBlock comp;
    AmpBlock amp;   // Rig A
    EqBlock eq;
    CabBlock cab;
    AmpBlock ampB;  // Rig B
    EqBlock eqB;
    CabBlock cabB;
    ModBlock mod;   // shared post
    DelayBlock delay;
    ReverbBlock reverb;

private:
    // Fractional align delay + polarity on one voice. delay 0 & polarity +1 ->
    // untouched (keeps SoloA bit-exact). Integer delays are exact (Hermite at
    // t=0 returns the tap), fractional ones interpolate.
    void alignVoice(FracDelayLine &fdl, float *v, int n, double delay, float pol)
    {
        if (delay > 0.0)
        {
            for (int i = 0; i < n; ++i)
            {
                fdl.write(v[i]);
                v[i] = fdl.readFrac(delay) * pol;
            }
        }
        else if (pol < 0.0f)
        {
            for (int i = 0; i < n; ++i)
                v[i] = -v[i];
        }
    }

    // Solo bypasses pan and plays the rig at its level centered (unity at
    // level 1 -> SoloA is bit-exact to the old mono->stereo fan-out). Dual
    // applies per-rig level + equal-power pan.
    void mix(float *outL, float *outR, const float *vA, const float *vB,
             int n, int numChannels) const
    {
        if (numChannels == 1)
        {
            // Mono fold: sum the active rigs at their levels (pan is moot).
            const bool a = (mMode != SoloB), b = (mMode != SoloA);
            for (int i = 0; i < n; ++i)
            {
                float s = 0.0f;
                if (a) s += vA[i] * mLevelA;
                if (b) s += vB[i] * mLevelB;
                outL[i] = s;
            }
            return;
        }

        if (mMode == SoloA || mMode == SoloB)
        {
            const float *v = (mMode == SoloA) ? vA : vB;
            const float lvl = (mMode == SoloA) ? mLevelA : mLevelB;
            for (int i = 0; i < n; ++i)
            {
                const float s = v[i] * lvl;
                outL[i] = s;
                outR[i] = s;
            }
        }
        else // Dual
        {
            float gLA, gRA, gLB, gRB;
            panGains(mPanA, gLA, gRA);
            panGains(mPanB, gLB, gRB);
            for (int i = 0; i < n; ++i)
            {
                const float a = vA[i] * mLevelA;
                const float b = vB[i] * mLevelB;
                outL[i] = a * gLA + b * gLB;
                outR[i] = a * gRA + b * gRB;
            }
        }
    }

    std::array<MonoBlock *, 8> allMonoBlocks()
    {
        return {&gate, &comp, &amp, &eq, &cab, &ampB, &eqB, &cabB};
    }

    std::array<StereoBlock *, 3> stereoBlocks() { return {&mod, &delay, &reverb}; }
    std::array<const StereoBlock *, 3> stereoBlocks() const { return {&mod, &delay, &reverb}; }

    int mMode = SoloA;
    float mLevelA = 1.0f, mLevelB = 1.0f;
    float mPanA = -1.0f, mPanB = 1.0f; // default hard L / hard R for Dual
    float mPolA = 1.0f, mPolB = 1.0f;  // polarity (+1 / -1)
    double mAlignA = 0.0, mAlignB = 0.0; // fractional align delay (samples)
    FracDelayLine mFdlA, mFdlB;
    std::vector<float> mVoiceA, mVoiceB;
    bool mPrepared = false;
};

} // namespace nam_rig
