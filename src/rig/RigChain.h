#pragma once
// RigChain — the fixed serial chain host, now with a DUAL-RIG core.
//
//   [mono shared pre]  gate -> comp -> drive (3-slot rack) -> premod (mono mod pedal)
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
#include "EnvFilterBlock.h"
#include "CompBlock.h"
#include "DriveBlock.h"
#include "PreModBlock.h"
#include "PreDelayBlock.h"
#include "AmpBlock.h"
#include "EqBlock.h"
#include "CabBlock.h"
#include "CabDynamicsBlock.h"
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
    // Which amp(s) the driven signal is sent to. The amp that ISN'T targeted
    // receives the pre-drive ("clean") tap instead, so you can run one dirty
    // amp + one clean amp off the single shared drive rack. Both (default) sends
    // the driven bus to both amps -> bit-exact to the old shared-drive behavior.
    enum DriveSend { SendA = 0, SendB = 1, SendBoth = 2 };
    static constexpr int kMaxAlignSamples = 4096; // ~85 ms at 48k

    void prepare(double sampleRate, int maxBlockSize)
    {
        const BlockContext ctx{sampleRate, maxBlockSize};
        for (auto *b : allMonoBlocks())
            b->prepare(ctx);
        for (auto *b : stereoBlocks())
            b->prepare(ctx);
        // Dynamic Cab is a non-MonoBlock delta wrapper (JUCE-free core), so it is
        // prepared explicitly rather than via allMonoBlocks().
        cabDyn.prepare(sampleRate, maxBlockSize);
        cabDynB.prepare(sampleRate, maxBlockSize);
        mVoiceA.assign((size_t)juce::jmax(1, maxBlockSize), 0.0f);
        mVoiceB.assign((size_t)juce::jmax(1, maxBlockSize), 0.0f);
        mCleanTap.assign((size_t)juce::jmax(1, maxBlockSize), 0.0f);
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
        cabDyn.reset();
        cabDynB.reset();
        mFdlA.reset();
        mFdlB.reset();
    }

    bool isPrepared() const { return mPrepared; }

    // ---- mixer controls (message thread; cheap scalars) ----
    void setMode(int mode) { mMode = juce::jlimit(0, 2, mode); }
    int mode() const { return mMode; }
    // Drive-send routing (SendA / SendB / SendBoth). Default Both.
    void setDriveSend(int s) { mDriveSend = juce::jlimit(0, 2, s); }
    int driveSend() const { return mDriveSend; }
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
    // Pre-amp mod pedal position: true = BEFORE the drive rack, false = AFTER it (default).
    void setPremodPreDrive(bool b) { mPremodPreDrive = b; }
    // Stereo front-mod: in Dual, the post-drive premod becomes mono-in / stereo-out
    // (L lane -> Amp A, R lane -> Amp B). Off/Solo/pre-drive keep the mono premod.
    void setPremodStereo(bool b) { mPremodStereo = b; }
    void setPremodSpread(float s) { premod.setSpread(s); } // 0 = dual-mono, 1 = 180°
    // Pre-amp delay pedal position: true = BEFORE the drive rack, false = AFTER it (default).
    void setPredelayPreDrive(bool b) { mPredelayPreDrive = b; }
    // Stereo front-delay: in Dual, the post-drive predelay becomes mono-in / stereo-out as a
    // PING-PONG (dry centred; wet repeats bounce L=Amp A / R=Amp B at equal time -> balanced,
    // no per-repeat lean). Off/Solo/pre-drive keep the mono predelay. No spread control.
    void setPredelayStereo(bool b) { mPredelayStereo = b; }
    // Envelope filter position: false = BEFORE the drive rack (default, classic
    // auto-wah-into-drive), true = AFTER the drive rack (wah on the driven signal).
    void setEnvFilterPostDrive(bool b) { mEnvFilterPostDrive = b; }
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
        // Envelope filter / auto-wah: runs on the globally-CALIBRATED signal (the
        // input-cal scale above already trimmed the input to the pre-amp reference,
        // so the level-dependent sweep is anchored to the user's dBu calibration,
        // exactly like the gate/comp/drive). Position relative to the drive rack is
        // switchable (mEnvFilterPostDrive): PRE-drive (default) runs here, BEFORE the
        // compressor, so it tracks the un-squashed dynamics the real pedal senses and
        // then feeds the wah'd signal into the overdrive; POST-drive runs after the
        // drive rack (below), sweeping the already-driven signal. Zero latency.
        if (!mEnvFilterPostDrive && !envfilter.isBypassed())
            { envfilter.process(ch0, numSamples); heal(envfilter, ch0, numSamples); }
        if (!comp.isBypassed())
            { comp.process(ch0, numSamples);  heal(comp, ch0, numSamples); }
        // Mono front-of-amp modulation pedal. Its position relative to the drive
        // rack is switchable (mPremodPreDrive): PRE-drive feeds a clean modulated
        // signal into the overdrive; POST-drive (default) modulates the already-
        // driven signal before it hits the amp. Either way it's before the amp
        // split, so it interacts with the amp — unlike the post-cab stereo ModBlock.
        if (mPremodPreDrive && !premod.isBypassed())
            { premod.process(ch0, numSamples); heal(premod, ch0, numSamples); }
        // Mono front-of-amp DELAY pedal (voiced after real delay pedals). Like the
        // premod, its position relative to the drive rack is switchable
        // (mPredelayPreDrive): PRE-drive feeds the echoes into the overdrive; POST-
        // drive (default) delays the already-driven signal before the amp. Either way
        // it's before the split, so the repeats are coloured by the amp — distinct
        // from the post-cab stereo DelayBlock. Default chain: drive -> premod -> predelay.
        if (mPredelayPreDrive && !predelay.isBypassed())
            { predelay.process(ch0, numSamples); heal(predelay, ch0, numSamples); }
        // Clean (drive-bypassed) tap for the drive-send routing. When the send
        // selector routes drive to only ONE amp, the OTHER amp gets this pre-drive
        // signal — everything up to here (gate/comp/env-pre/pre-mod-pre/pre-delay-
        // pre) but NOT the drive rack or any post-drive-positioned pedal. Captured
        // only when it's actually needed (send != Both AND drive is running); the
        // Both path never reads it, so it stays bit-exact to the shared-drive chain.
        // All four pre-amp pedals report zero latency, so this tap is sample-aligned
        // with the driven bus below — no realignment needed.
        const bool needCleanTap = (mDriveSend != SendBoth) && !drive.isBypassed();
        float *clean = mCleanTap.data();
        if (needCleanTap)
            std::memcpy(clean, ch0, (size_t)numSamples * sizeof(float));

        // Stereo front-mod engages only in Dual with an active POST-drive premod: the
        // pedal is deferred to a per-voice, mono-in/stereo-out pass AFTER the split
        // (below) so its L feeds Amp A and R feeds Amp B. Solo / pre-drive / bypassed
        // keep the mono premod on ch0 here, so those paths stay bit-exact.
        const bool stereoActive = mPremodStereo && (mMode == Dual)
                                  && !mPremodPreDrive && !premod.isBypassed();
        // Same deal for the front delay: in Dual with an active post-drive predelay it is
        // deferred to a per-voice mono-in/stereo-out PING-PONG pass after the split (below).
        // Solo / pre-drive / bypassed keep the mono pass.
        const bool predStereoActive = mPredelayStereo && (mMode == Dual)
                                      && !mPredelayPreDrive && !predelay.isBypassed();

        if (!drive.isBypassed())
            { drive.process(ch0, numSamples); heal(drive, ch0, numSamples); }
        if (!mPremodPreDrive && !premod.isBypassed() && !stereoActive)
            { premod.process(ch0, numSamples); heal(premod, ch0, numSamples); }
        if (!mPredelayPreDrive && !predelay.isBypassed() && !predStereoActive)
            { predelay.process(ch0, numSamples); heal(predelay, ch0, numSamples); }
        // Env filter POST-drive branch: sweep the already-driven signal (fatter,
        // cocked-wah/synthy). Still mono, still before the amp split. Zero latency.
        if (mEnvFilterPostDrive && !envfilter.isBypassed())
            { envfilter.process(ch0, numSamples); heal(envfilter, ch0, numSamples); }

        // ---- split into the two voice buffers ----
        // Per the drive-send selector, each voice is fed either the driven bus
        // (ch0) or the clean pre-drive tap. Both -> both driven (default, bit-
        // exact). SendA -> only A driven, B clean; SendB -> only B driven, A clean.
        // When needCleanTap is false (send=Both or drive bypassed) both are driven.
        float *vA = mVoiceA.data();
        float *vB = mVoiceB.data();
        const bool runB = (mMode != SoloA);
        const bool runA = (mMode != SoloB);
        const bool aDriven = !needCleanTap || (mDriveSend != SendB);
        const bool bDriven = !needCleanTap || (mDriveSend != SendA);
        std::memcpy(vA, aDriven ? ch0 : clean, (size_t)numSamples * sizeof(float));
        if (runB)
            std::memcpy(vB, bDriven ? ch0 : clean, (size_t)numSamples * sizeof(float));

        // Stereo front-mod pass: one LFO read at two phases so the L/R sweeps stay
        // phase-locked. L modulates Amp A's source (clean tap or driven bus), R
        // modulates Amp B's — a decorrelated chorus/flanger/vibe (or anti-phase
        // tremolo = auto-pan) spread ACROSS the two amps, before they process. Both
        // voices are always filled here (stereoActive implies Dual -> runA && runB).
        if (stereoActive)
        {
            premod.processStereo(vA, vB, numSamples);
            heal(premod, vA, numSamples);
            heal(premod, vB, numSamples);
        }
        // Stereo front-delay PING-PONG pass, AFTER the stereo mod (keeps the mono chain's
        // premod -> predelay order). Dry stays centred; the wet repeats bounce vA (Amp A) /
        // vB (Amp B) at equal time. Both voices are filled (predStereoActive implies Dual).
        if (predStereoActive)
        {
            predelay.processStereo(vA, vB, numSamples);
            heal(predelay, vA, numSamples);
            heal(predelay, vB, numSamples);
        }
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
            // Dynamic Cab wraps the IR convolution: pre-conv deltas (reactive
            // impedance + cone breakup) -> static IR -> post-conv delta (enclosure
            // air). All macros at 0 = bit-exact bypass, so SoloA stays byte-exact.
            cabDyn.processPre(vA, numSamples);
            if (!cab.isBypassed()) { cab.process(vA, numSamples); heal(cab, vA, numSamples); }
            cabDyn.processPost(vA, numSamples);
            healDyn(cabDyn, vA, numSamples);
            if (mOutTrimA != 1.0f) scale(vA, numSamples, mOutTrimA);
            alignVoice(mFdlA, vA, numSamples, compA + mAlignA, mPolA);
        }
        if (runB)
        {
            if (mInTrimB != 1.0f) scale(vB, numSamples, mInTrimB);
            if (!ampB.isBypassed()) { ampB.process(vB, numSamples); heal(ampB, vB, numSamples); }
            if (!eqB.isBypassed())  { eqB.process(vB, numSamples);  heal(eqB, vB, numSamples); }
            cabDynB.processPre(vB, numSamples);
            if (!cabB.isBypassed()) { cabB.process(vB, numSamples); heal(cabB, vB, numSamples); }
            cabDynB.processPost(vB, numSamples);
            healDyn(cabDynB, vB, numSamples);
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
            {
                b->process(left, right, numSamples);
                heal(*b, left, right, numSamples);
            }
    }

    // Total chain PDC in DAW samples: shared pre + the parallel voice section
    // (max of the two voices, INCLUDING their align delays) + shared post.
    double latencySamples() const
    {
        const double pre = gate.latencySamples()
                         + envfilter.latencySamples() + comp.latencySamples()
                         + drive.latencySamples() + premod.latencySamples()
                         + predelay.latencySamples();
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
    EnvFilterBlock envfilter; // auto-wah, before comp (tracks raw dynamics)
    CompBlock comp;
    DriveBlock drive; // 3-slot drive rack (shared, before split)
    PreModBlock premod; // mono front-of-amp modulation pedal (after drive, before split)
    PreDelayBlock predelay; // mono front-of-amp delay pedal (after premod, before split)
    AmpBlock amp;   // Rig A
    EqBlock eq;
    CabBlock cab;
    CabDynamicsBlock cabDyn;   // Dynamic Cab (level-dependent delta wrapping cab): Rig A
    AmpBlock ampB;  // Rig B
    EqBlock eqB;
    CabBlock cabB;
    CabDynamicsBlock cabDynB;  // Dynamic Cab: Rig B
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
    // Dynamic Cab isn't a MonoBlock (JUCE-free core), so it gets its own heal.
    void healDyn(CabDynamicsBlock &b, float *buf, int n)
    {
        if (!hasNonFinite(buf, n))
            return;
        b.reset();
        std::fill(buf, buf + n, 0.0f);
        recordTrip("Cab Dynamics");
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

    std::array<MonoBlock *, 12> allMonoBlocks()
    {
        return {&gate, &envfilter, &comp, &drive, &premod, &predelay,
                &amp, &eq, &cab, &ampB, &eqB, &cabB};
    }

    std::array<StereoBlock *, 3> stereoBlocks() { return {&mod, &delay, &reverb}; }
    std::array<const StereoBlock *, 3> stereoBlocks() const { return {&mod, &delay, &reverb}; }

    int mMode = SoloA;
    int mDriveSend = SendBoth; // which amp(s) the drive rack feeds (default both)
    float mLevelA = 1.0f, mLevelB = 1.0f;
    float mPanA = -1.0f, mPanB = 1.0f; // default hard L / hard R for Dual
    float mPolA = 1.0f, mPolB = 1.0f;  // polarity (+1 / -1)
    float mInputCal = 1.0f; // global input calibration (pre-split)
    bool mPremodPreDrive = false; // pre-amp mod pedal: before (true) / after (false) the drive rack
    bool mPremodStereo = false;   // Dual + post-drive: premod is mono-in/stereo-out (L->Amp A, R->Amp B)
    bool mPredelayPreDrive = false; // pre-amp delay pedal: before (true) / after (false) the drive rack
    bool mPredelayStereo = false;   // Dual + post-drive: predelay is mono-in/stereo-out ping-pong (L=Amp A, R=Amp B)
    bool mEnvFilterPostDrive = false; // env filter: before (false, default) / after (true) the drive rack
    float mInTrimA = 1.0f, mInTrimB = 1.0f;
    float mOutTrimA = 1.0f, mOutTrimB = 1.0f;
    double mAlignA = 0.0, mAlignB = 0.0; // fractional align delay (samples)
    FracDelayLine mFdlA, mFdlB;
    std::vector<float> mVoiceA, mVoiceB;
    std::vector<float> mCleanTap; // pre-drive signal for the drive-send routing
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
