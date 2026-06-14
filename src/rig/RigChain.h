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
// Each rig has its OWN AA/oversampling factor; when they differ, Dual delay-
// compensates the faster voice so both stay sample-aligned at the mix.
//
// Phase alignment: each rig has a fractional align delay + a polarity flip,
// driven from PhaseAlign::measure() on the two rendered voice outputs (see
// PhaseAlign.h) so it works whether the cab is an IR or baked into the .nam.
// Defaults (delay 0, polarity +1) skip both paths, keeping SoloA bit-exact.
// measureAlignment() renders an internal probe through both voices to drive it.
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
#include "Biquad.h"     // band-limit for the level-match measurement

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
        mMaxBlock = juce::jmax(1, maxBlockSize);
        mSampleRate = sampleRate;
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

    void setInTrimA(float g) { mInTrimA = g; }
    void setInTrimB(float g) { mInTrimB = g; }
    void setOutTrimA(float g) { mOutTrimA = g; }
    void setOutTrimB(float g) { mOutTrimB = g; }

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

    // Measure the broadband time/polarity offset between the two voices by
    // rendering an internal probe through each full voice (amp->eq->cab) and
    // cross-correlating the OUTPUTS — cab-agnostic (works for IR or in-model
    // cabs). Settles the cab convolvers (async load + crossfade) first, then
    // clears all state afterwards so live audio resumes clean.
    //
    // NOT realtime-safe: it renders offline and sleeps while the convolver
    // settles. The CALLER must guarantee the audio thread is not processing
    // (NamRigProcessor::autoAlign suspends processing around it).
    AlignResult measureAlignment()
    {
        const int n = 4096;
        settleCab(cab);
        settleCab(cabB);
        reset();

        // Deterministic broadband probe (white-ish noise -> sharp xcorr peak).
        std::vector<float> a((size_t)n), b((size_t)n);
        std::uint32_t s = 0x9e3779b9u;
        for (int i = 0; i < n; ++i)
        {
            s = s * 1103515245u + 12345u;
            const float v = 0.25f * (float)((int)((s >> 16) & 0x7fff) - 16384) / 16384.0f;
            a[(size_t)i] = v;
            b[(size_t)i] = v;
        }

        renderVoice(0, a.data(), n);
        reset();
        renderVoice(1, b.data(), n);
        reset();

        return PhaseAlign::measure(a.data(), b.data(), n, kMaxAlignSamples);
    }

    struct VoiceLevels { double rmsA = 0.0, rmsB = 0.0; };

    // Measure each voice's actual output RMS by rendering a probe through the
    // FULL voice (cal in-trim -> amp -> eq -> cab -> normalize out-trim), so the
    // result reflects what's really heard (cab + cal/normalize included). Used
    // by NamRigProcessor::matchLevels to set the per-rig Level knobs equal.
    // Same threading contract as measureAlignment (caller suspends processing).
    VoiceLevels measureLevels()
    {
        const int n = 8192; // long window -> stable RMS
        settleCab(cab);
        settleCab(cabB);
        reset();

        std::vector<float> a((size_t)n), b((size_t)n);
        std::uint32_t s = 0x9e3779b9u;
        for (int i = 0; i < n; ++i)
        {
            s = s * 1103515245u + 12345u;
            const float v = 0.25f * (float)((int)((s >> 16) & 0x7fff) - 16384) / 16384.0f;
            a[(size_t)i] = v;
            b[(size_t)i] = v;
        }

        renderVoice(0, a.data(), n, true); // withTrims = include cal/normalize
        reset();
        renderVoice(1, b.data(), n, true);
        reset();

        // Perceptual band-limit before RMS. Distortion dumps energy into fizzy
        // HF harmonics we don't hear as proportionally loud, so plain (or worse,
        // K-weighted) RMS over-counts a crunchy amp and the match leaves it too
        // quiet. Bracketing the guitar-loudness band (see bandLimit, ~80 Hz ..
        // 2.5 kHz) tracks perceived loudness much better.
        bandLimit(a.data(), n);
        bandLimit(b.data(), n);

        VoiceLevels r;
        r.rmsA = voiceRms(a.data(), n);
        r.rmsB = voiceRms(b.data(), n);
        return r;
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

        double compA = 0.0, compB = 0.0;
        if (mMode == Dual)
        {
            const double LA = amp.latencySamples() + eq.latencySamples() + cab.latencySamples();
            const double LB = ampB.latencySamples() + eqB.latencySamples() + cabB.latencySamples();
            compA = std::max(0.0, LB - LA);
            compB = std::max(0.0, LA - LB);
        }

        // ---- per-rig voices (in-trim -> amp -> eq -> cab -> out-trim), mono ----
        if (runA)
        {
            if (mInTrimA != 1.0f) scale(vA, numSamples, mInTrimA);
            if (!amp.isBypassed()) amp.process(vA, numSamples);
            if (!eq.isBypassed()) eq.process(vA, numSamples);
            if (!cab.isBypassed()) cab.process(vA, numSamples);
            if (mOutTrimA != 1.0f) scale(vA, numSamples, mOutTrimA);
            alignVoice(mFdlA, vA, numSamples, compA + mAlignA, mPolA);
        }
        if (runB)
        {
            if (mInTrimB != 1.0f) scale(vB, numSamples, mInTrimB);
            if (!ampB.isBypassed()) ampB.process(vB, numSamples);
            if (!eqB.isBypassed()) eqB.process(vB, numSamples);
            if (!cabB.isBypassed()) cabB.process(vB, numSamples);
            if (mOutTrimB != 1.0f) scale(vB, numSamples, mOutTrimB);
            alignVoice(mFdlB, vB, numSamples, compB + mAlignB, mPolB);
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
        const double LA = amp.latencySamples() + eq.latencySamples() + cab.latencySamples();
        const double LB = ampB.latencySamples() + eqB.latencySamples() + cabB.latencySamples();
        double voice = LA + mAlignA;
        if (mMode == SoloB)
            voice = LB + mAlignB;
        else if (mMode == Dual)
            voice = std::max(LA, LB) + std::max(mAlignA, mAlignB);
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
    static void scale(float *v, int n, float g)
    {
        for (int i = 0; i < n; ++i)
            v[i] *= g;
    }

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

    // Offline-render one voice (amp->eq->cab) in place, chunked by the prepared
    // block size so the convolver never sees an oversized block. Bypass flags
    // are ignored. withTrims folds in the cal/normalize trims (for level
    // measurement); alignment leaves them out (gain doesn't shift the lag).
    void renderVoice(int rig, float *buf, int n, bool withTrims = false)
    {
        AmpBlock &a = rig ? ampB : amp;
        EqBlock &e = rig ? eqB : eq;
        CabBlock &c = rig ? cabB : cab;
        const float inTrim = rig ? mInTrimB : mInTrimA;
        const float outTrim = rig ? mOutTrimB : mOutTrimA;
        const int chunk = juce::jmax(1, juce::jmin(mMaxBlock, n));
        for (int pos = 0; pos < n; pos += chunk)
        {
            const int m = juce::jmin(chunk, n - pos);
            if (withTrims && inTrim != 1.0f) scale(buf + pos, m, inTrim);
            a.process(buf + pos, m);
            e.process(buf + pos, m);
            c.process(buf + pos, m);
            if (withTrims && outTrim != 1.0f) scale(buf + pos, m, outTrim);
        }
    }

    // RMS of a rendered voice, skipping the amp's startup ramp.
    static double voiceRms(const float *x, int n)
    {
        const int skip = juce::jmin(1024, n / 4);
        double sum = 0.0;
        int cnt = 0;
        for (int i = skip; i < n; ++i)
        {
            sum += (double)x[i] * (double)x[i];
            ++cnt;
        }
        return cnt > 0 ? std::sqrt(sum / (double)cnt) : 0.0;
    }

    // Bracket the guitar-loudness band (~80 Hz .. 2.5 kHz) before the RMS so
    // subsonics and distortion fizz don't skew the level measure. The LPF is
    // deliberately well below any standard loudness curve: distortion fizz lives
    // ~4-10 kHz and we don't hear it as proportionally loud, so cutting it hard
    // keeps a crunchy amp from being over-counted (and matched too quiet). Uses
    // the same verified RBJ Biquads the EQ/cab run.
    void bandLimit(float *x, int n) const
    {
        Biquad hp = Biquad::highpass(mSampleRate, 80.0);
        Biquad lp = Biquad::lowpass(mSampleRate, 2500.0);
        hp.process(x, n);
        lp.process(x, n);
    }

    // Wait out juce::dsp::Convolution's async IR load + crossfade by probing
    // with unit impulses until two consecutive responses are bit-identical
    // (same approach as tests/rig_chain_process.cpp). No-op without an IR.
    void settleCab(CabBlock &c)
    {
        if (!c.isIrLoaded())
            return;
        // JUCE swaps the IR in on a background thread, so the first probes would
        // otherwise read identical PRE-load responses and exit early. Let the
        // load land first (mirrors tests/rig_chain_process.cpp), then probe out
        // the crossfade until two consecutive responses match.
        juce::Thread::sleep(300);
        const int n = juce::jmax(1, juce::jmin(mMaxBlock, 256));
        std::vector<float> prev((size_t)n, 0.0f), probe((size_t)n, 0.0f);
        for (int tries = 0; tries < 200; ++tries)
        {
            std::fill(probe.begin(), probe.end(), 0.0f);
            probe[0] = 1.0f;
            c.process(probe.data(), n);
            if (tries > 0 &&
                std::memcmp(prev.data(), probe.data(), (size_t)n * sizeof(float)) == 0)
                return;
            prev = probe;
            juce::Thread::sleep(5);
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
    float mInTrimA = 1.0f, mInTrimB = 1.0f;
    float mOutTrimA = 1.0f, mOutTrimB = 1.0f;
    double mAlignA = 0.0, mAlignB = 0.0; // fractional align delay (samples)
    FracDelayLine mFdlA, mFdlB;
    std::vector<float> mVoiceA, mVoiceB;
    int mMaxBlock = 512; // prepared block size (probe render chunk size)
    double mSampleRate = 48000.0; // for the level-measurement band-limit filters
    bool mPrepared = false;
};

} // namespace nam_rig
