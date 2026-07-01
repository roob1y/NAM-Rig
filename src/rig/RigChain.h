#pragma once
// RigChain — the fixed serial chain host, now with a DUAL-RIG core.
//
//   [mono shared pre]  gate -> comp -> drive (3-slot rack)
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
#include "DriveBlock.h"
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
#include <atomic>
#include <cmath>
#include <cstdint>
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

    void setInputCal(float g) { mInputCal = g; } // global, pre-everything
    void setInTrimA(float g) { mInTrimA = g; }
    void setInTrimB(float g) { mInTrimB = g; }
    void setOutTrimA(float g) { mOutTrimA = g; }
    void setOutTrimB(float g) { mOutTrimB = g; }

    // ---- drive auto-gain (per amp) ----
    // When on, a measured per-amp makeup (see measureDriveMakeup) is applied at
    // each amp's OUTPUT so engaging the drive rack -- at any pedal output level --
    // doesn't change that amp's loudness. Independent for A and B in Dual mode.
    void setDriveAutoGain(bool on) { mDriveAgc = on; }
    bool driveAutoGain() const { return mDriveAgc; }
    void setDriveMakeup(float a, float b) { mDriveMakeupA = a; mDriveMakeupB = b; }
    float driveMakeupA() const { return mDriveMakeupA; }
    float driveMakeupB() const { return mDriveMakeupB; }

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

        // Pink (equal-energy-per-octave) probe — roughly guitar's spectral
        // balance, so the nonlinear amp distorts it like it would real playing
        // (white noise over-drives the highs). Paul Kellett economy pink filter,
        // then normalized to ~0.15 RMS so the amp sees a sensible drive level.
        std::vector<float> a((size_t)n), b((size_t)n);
        std::uint32_t s = 0x9e3779b9u;
        float k0 = 0.0f, k1 = 0.0f, k2 = 0.0f;
        double sq = 0.0;
        for (int i = 0; i < n; ++i)
        {
            s = s * 1103515245u + 12345u;
            const float w = (float)((int)((s >> 16) & 0x7fff) - 16384) / 16384.0f;
            k0 = 0.99765f * k0 + w * 0.0990460f;
            k1 = 0.96300f * k1 + w * 0.2965164f;
            k2 = 0.57000f * k2 + w * 1.0526913f;
            const float p = k0 + k1 + k2 + w * 0.1848f;
            a[(size_t)i] = p;
            sq += (double)p * p;
        }
        const float norm = (sq > 0.0) ? (float)(0.15 / std::sqrt(sq / (double)n)) : 1.0f;
        for (int i = 0; i < n; ++i) { a[(size_t)i] *= norm; b[(size_t)i] = a[(size_t)i]; }

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

    // Per-amp DRIVE auto-gain makeup. For each amp, render a pink probe through
    //   clean  = amp(probe)            (drive rack bypassed)
    //   driven = amp(drive(probe))     (through the shared drive rack)
    // and return the gain that makes the DRIVEN amp output as loud as the CLEAN
    // one -- so switching a pedal on (at ANY output-volume setting) leaves that
    // amp's loudness unchanged, only its tone/gain character. EQ + cab are linear
    // and identical in both paths, so they cancel in the ratio: we measure at the
    // amp output and skip the cab entirely (no async convolver settle -> fast
    // enough to re-run whenever the drive changes). Per amp, so Dual holds A and B
    // independently. Loudness = the same guitar-weighted band RMS as measureLevels.
    // Same threading contract as measureLevels/measureAlignment (caller suspends).
    struct DriveMakeup { float a = 1.0f, b = 1.0f; };
    DriveMakeup measureDriveMakeup()
    {
        if (drive.isBypassed())
            return {}; // no drive in the path -> unity makeup (nothing to match)
        const int n = 8192;
        reset();
        std::vector<float> probe((size_t)n);
        makePinkProbe(probe.data(), n, 0.15f);
        DriveMakeup mk;
        mk.a = (mMode != SoloB) ? ampDriveMakeup(0, probe.data(), n) : 1.0f;
        mk.b = (mMode != SoloA) ? ampDriveMakeup(1, probe.data(), n) : 1.0f;
        reset();
        return mk;
    }

    // Full chain on a DAW-rate buffer (1 = mono fold, 2 = stereo).
    void process(juce::AudioBuffer<float> &buffer)
    {
        const int numSamples = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();
        if (numSamples == 0)
            return;

        float *ch0 = buffer.getWritePointer(0);

        // ---- global input calibration (feeds the whole pre-amp section:
        //      gate -> comp -> drive -> split). Unity by default -> bit-exact. ----
        if (mInputCal != 1.0f)
            scale(ch0, numSamples, mInputCal);

        // ---- shared mono pre ----
        if (!gate.isBypassed())
            { gate.process(ch0, numSamples);  heal(gate, ch0, numSamples); }
        if (!comp.isBypassed())
            { comp.process(ch0, numSamples);  heal(comp, ch0, numSamples); }
        if (!drive.isBypassed())
            { drive.process(ch0, numSamples); heal(drive, ch0, numSamples); }

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
            if (!amp.isBypassed()) { amp.process(vA, numSamples); heal(amp, vA, numSamples); }
            if (!eq.isBypassed())  { eq.process(vA, numSamples);  heal(eq, vA, numSamples); }
            if (!cab.isBypassed()) { cab.process(vA, numSamples); heal(cab, vA, numSamples); }
            if (mOutTrimA != 1.0f) scale(vA, numSamples, mOutTrimA);
            // Per-amp drive auto-gain: hold this amp's loudness constant vs no drive.
            if (mDriveAgc && !drive.isBypassed() && mDriveMakeupA != 1.0f)
                scale(vA, numSamples, mDriveMakeupA);
            alignVoice(mFdlA, vA, numSamples, compA + mAlignA, mPolA);
        }
        if (runB)
        {
            if (mInTrimB != 1.0f) scale(vB, numSamples, mInTrimB);
            if (!ampB.isBypassed()) { ampB.process(vB, numSamples); heal(ampB, vB, numSamples); }
            if (!eqB.isBypassed())  { eqB.process(vB, numSamples);  heal(eqB, vB, numSamples); }
            if (!cabB.isBypassed()) { cabB.process(vB, numSamples); heal(cabB, vB, numSamples); }
            if (mOutTrimB != 1.0f) scale(vB, numSamples, mOutTrimB);
            if (mDriveAgc && !drive.isBypassed() && mDriveMakeupB != 1.0f)
                scale(vB, numSamples, mDriveMakeupB);
            alignVoice(mFdlB, vB, numSamples, compB + mAlignB, mPolB);
        }

        // ---- mix to the output bus ----
        float *left = ch0;
        float *right = (numChannels > 1) ? buffer.getWritePointer(1) : ch0;
        mix(left, right, vA, vB, numSamples, numChannels);

        // ---- shared stereo post ----
        for (auto *b : stereoBlocks())
            if (!b->isBypassed())
            {
                b->process(left, right, numSamples);
                heal(*b, left, right, numSamples);
            }
    }

    // Total chain PDC in DAW samples: shared pre + the parallel voice section
    // (max of the two voices, INCLUDING their align delays) + shared post.
    double latencySamples() const
    {
        const double pre = gate.latencySamples() + comp.latencySamples() + drive.latencySamples();
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

    // ---- NaN/Inf self-heal telemetry (message thread reads; audio thread writes) ----
    // count = number of times a block emitted a non-finite sample and was auto-
    // reset; lastBlock = name() of the most recent offender (nullptr = healthy).
    struct GuardReport { std::uint32_t count; const char *lastBlock; };
    GuardReport guardReport() const
    {
        return { mNanCount.load(std::memory_order_relaxed),
                 mNanLast.load(std::memory_order_relaxed) };
    }
    void clearGuardReport()
    {
        mNanCount.store(0, std::memory_order_relaxed);
        mNanLast.store(nullptr, std::memory_order_relaxed);
    }

    // Per-rig output peak for the editor's OUT L·R meters, in dBFS (floor -100).
    float rigOutLDb(int rig) const { return mRigPeakL[rig & 1].load(std::memory_order_relaxed); }
    float rigOutRDb(int rig) const { return mRigPeakR[rig & 1].load(std::memory_order_relaxed); }

    // ---- the blocks ----
    GateBlock gate; // shared pre
    CompBlock comp;
    DriveBlock drive; // 3-slot drive rack (shared, before split)
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

    // Absolute peak of a buffer (linear). Used for the Mix panel meters only.
    static float bufPeak(const float *x, int n)
    {
        float p = 0.0f;
        for (int i = 0; i < n; ++i)
            p = juce::jmax(p, std::abs(x[i]));
        return p;
    }
    // Convert a linear L/R peak pair to dBFS and publish for the editor meters.
    void storeRigPeak(int rig, float linL, float linR) const
    {
        auto toDb = [](float lin) {
            return lin > 1.0e-5f ? 20.0f * std::log10(lin) : -100.0f;
        };
        mRigPeakL[rig & 1].store(toDb(linL), std::memory_order_relaxed);
        mRigPeakR[rig & 1].store(toDb(linR), std::memory_order_relaxed);
    }

    // ---- NaN/Inf self-heal --------------------------------------------------
    // If a block emits a non-finite sample its internal state is already corrupt
    // (a NaN latched in a filter pole, the convolver's FFT partition buffers, or
    // the model's recurrent memory) and stays corrupt — silencing the rig — until
    // something resets it. So after each block we scan its output: on a non-finite
    // hit we reset THAT block (clears the poisoned state), zero its output so no
    // downstream block inherits the NaN, and bump the telemetry. When clean the
    // scan only reads the buffer, so the clean path stays bit-exact.
    static bool hasNonFinite(const float *x, int n)
    {
        for (int i = 0; i < n; ++i)
            if (!std::isfinite(x[i]))
                return true;
        return false;
    }
    void recordTrip(const char *blockName)
    {
        mNanCount.fetch_add(1, std::memory_order_relaxed);
        mNanLast.store(blockName, std::memory_order_relaxed);
    }
    void heal(MonoBlock &b, float *buf, int n)
    {
        if (!hasNonFinite(buf, n))
            return;
        b.reset();
        std::fill(buf, buf + n, 0.0f);
        recordTrip(b.name());
    }
    void heal(StereoBlock &b, float *l, float *r, int n)
    {
        if (!hasNonFinite(l, n) && !hasNonFinite(r, n))
            return;
        b.reset();
        std::fill(l, l + n, 0.0f);
        std::fill(r, r + n, 0.0f);
        recordTrip(b.name());
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
        // Per-rig output telemetry for the Mix panel's OUT L·R meters. Each rig's
        // mono voice peak scales linearly through level + pan, so we take the raw
        // voice peak once and apply the same gains the mix below uses.
        const float pkA = bufPeak(vA, n), pkB = bufPeak(vB, n);

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
            storeRigPeak(0, a ? pkA * mLevelA : 0.0f, a ? pkA * mLevelA : 0.0f);
            storeRigPeak(1, b ? pkB * mLevelB : 0.0f, b ? pkB * mLevelB : 0.0f);
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
            const float pk = (mMode == SoloA) ? pkA * mLevelA : pkB * mLevelB;
            storeRigPeak(0, mMode == SoloA ? pk : 0.0f, mMode == SoloA ? pk : 0.0f);
            storeRigPeak(1, mMode == SoloB ? pk : 0.0f, mMode == SoloB ? pk : 0.0f);
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
            storeRigPeak(0, pkA * mLevelA * gLA, pkA * mLevelA * gRA);
            storeRigPeak(1, pkB * mLevelB * gLB, pkB * mLevelB * gRB);
        }
    }

    // Offline-render one voice (amp->eq->cab) in place, chunked by the prepared
    // block size so the convolver never sees an oversized block. Block on/off
    // (isBypassed) flags are ignored, but the cab's IR convolution self-gates on
    // its own conv-bypass (cabOn) so the measurement reflects the real path.
    // withTrims folds in the cal/normalize trims (for level measurement);
    // alignment leaves them out (gain doesn't shift the lag).
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

    // One amp's drive makeup = clean-output loudness / driven-output loudness.
    // Renders the probe through the amp twice (clean, then drive->amp), matching
    // the live signal order (shared drive rack -> per-rig in-trim -> amp). Blocks
    // are reset around each render; the caller has already suspended the audio
    // thread. Clamped to +-12 dB so a pathological measurement can't run away.
    float ampDriveMakeup(int rig, const float *probe, int n)
    {
        AmpBlock &a = rig ? ampB : amp;
        const float inTrim = rig ? mInTrimB : mInTrimA;
        std::vector<float> clean((size_t)n), driven((size_t)n);
        std::memcpy(clean.data(), probe, (size_t)n * sizeof(float));
        std::memcpy(driven.data(), probe, (size_t)n * sizeof(float));

        drive.reset(); a.reset();
        renderAmp(a, driven.data(), n, inTrim, true);  // drive rack -> in-trim -> amp
        drive.reset(); a.reset();
        renderAmp(a, clean.data(), n, inTrim, false);  // in-trim -> amp (drive bypassed)
        drive.reset(); a.reset();

        bandLimit(clean.data(), n);   // same guitar-weighted loudness as measureLevels
        bandLimit(driven.data(), n);
        const double rc = voiceRms(clean.data(), n);
        const double rd = voiceRms(driven.data(), n);
        if (rd < 1.0e-9) return 1.0f; // driven output silent -> nothing to match
        return juce::jlimit(0.25f, 4.0f, (float)(rc / rd));
    }

    // Render a probe through (optionally the shared drive rack, then) the per-rig
    // in-trim and amp, in place, chunked to the prepared block size. Block on/off
    // flags are ignored so the measurement reflects the configured drive+amp.
    void renderAmp(AmpBlock &a, float *buf, int n, float inTrim, bool withDrive)
    {
        const int chunk = juce::jmax(1, juce::jmin(mMaxBlock, n));
        for (int pos = 0; pos < n; pos += chunk)
        {
            const int m = juce::jmin(chunk, n - pos);
            if (withDrive) drive.process(buf + pos, m);
            if (inTrim != 1.0f) scale(buf + pos, m, inTrim);
            a.process(buf + pos, m);
        }
    }

    // Deterministic pink (equal-energy-per-octave) probe normalized to ~targetRms
    // -- guitar-like spectral balance so the amp distorts it like real playing.
    // Same Paul Kellett economy filter measureLevels uses.
    static void makePinkProbe(float *out, int n, float targetRms)
    {
        std::uint32_t s = 0x9e3779b9u;
        float k0 = 0.0f, k1 = 0.0f, k2 = 0.0f;
        double sq = 0.0;
        for (int i = 0; i < n; ++i)
        {
            s = s * 1103515245u + 12345u;
            const float w = (float)((int)((s >> 16) & 0x7fff) - 16384) / 16384.0f;
            k0 = 0.99765f * k0 + w * 0.0990460f;
            k1 = 0.96300f * k1 + w * 0.2965164f;
            k2 = 0.57000f * k2 + w * 1.0526913f;
            const float p = k0 + k1 + k2 + w * 0.1848f;
            out[i] = p;
            sq += (double)p * p;
        }
        const float norm = (sq > 0.0) ? (float)((double)targetRms / std::sqrt(sq / (double)n)) : 1.0f;
        for (int i = 0; i < n; ++i) out[i] *= norm;
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

    // Guitar-weighted loudness filter before the RMS — K-weighting's idea (boost
    // the presence region the ear is most sensitive to) but with the fizz rolled
    // off so a crunchy amp isn't over-counted. HP 80 (drop subsonics) -> +3.5 dB
    // presence shelf @2.5k (so a brighter amp reads as the louder it sounds) ->
    // LP 5.5k (cut the 6-10k distortion fizz we don't hear as proportionally
    // loud). Same verified RBJ Biquads the EQ/cab run; tune the shelf/LP by ear.
    void bandLimit(float *x, int n) const
    {
        Biquad hp = Biquad::highpass(mSampleRate, 80.0);
        Biquad shelf = Biquad::highshelf(mSampleRate, 2500.0, 3.5);
        Biquad lp = Biquad::lowpass(mSampleRate, 5500.0);
        hp.process(x, n);
        shelf.process(x, n);
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

    std::array<MonoBlock *, 9> allMonoBlocks()
    {
        return {&gate, &comp, &drive, &amp, &eq, &cab, &ampB, &eqB, &cabB};
    }

    std::array<StereoBlock *, 3> stereoBlocks() { return {&mod, &delay, &reverb}; }
    std::array<const StereoBlock *, 3> stereoBlocks() const { return {&mod, &delay, &reverb}; }

    int mMode = SoloA;
    float mLevelA = 1.0f, mLevelB = 1.0f;
    float mPanA = -1.0f, mPanB = 1.0f; // default hard L / hard R for Dual
    float mPolA = 1.0f, mPolB = 1.0f;  // polarity (+1 / -1)
    float mInputCal = 1.0f; // global input calibration (pre-split)
    float mInTrimA = 1.0f, mInTrimB = 1.0f;
    float mOutTrimA = 1.0f, mOutTrimB = 1.0f;
    double mAlignA = 0.0, mAlignB = 0.0; // fractional align delay (samples)
    bool mDriveAgc = false;                       // drive auto-gain (amp-output match) on/off
    float mDriveMakeupA = 1.0f, mDriveMakeupB = 1.0f; // per-amp measured makeup gains
    FracDelayLine mFdlA, mFdlB;
    std::vector<float> mVoiceA, mVoiceB;
    int mMaxBlock = 512; // prepared block size (probe render chunk size)
    double mSampleRate = 48000.0; // for the level-measurement band-limit filters
    bool mPrepared = false;

    std::atomic<std::uint32_t> mNanCount{0};      // self-heal trip counter
    std::atomic<const char *> mNanLast{nullptr};  // name() of last offender

    // Per-rig OUT L·R peak telemetry (dBFS), written by mix() on the audio thread,
    // read by the editor timer. mutable: mix() is const but still publishes meters.
    mutable std::atomic<float> mRigPeakL[2]{{-100.0f}, {-100.0f}};
    mutable std::atomic<float> mRigPeakR[2]{{-100.0f}, {-100.0f}};
};

} // namespace nam_rig
