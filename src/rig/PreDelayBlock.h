#pragma once
// PreDelayBlock — a delay pedal that sits IN FRONT OF THE AMP, in the shared pre
// section (after the drive rack, before the A/B split — the classic pedalboard
// "delay into the amp" spot). Deliberately DISTINCT from the post-cab STEREO
// DelayBlock: this one is coloured by the amp downstream and is voiced after three
// SPECIFIC real delay pedals rather than the tape/clean characters of the stereo
// unit. Same per-model-fit ethos as DriveBlock's pedal models.
//
// MONO by default (process() drives mLaneL only -> BIT-EXACT to the single-lane block).
// In the dual-amp rig it can run MONO-IN / STEREO-OUT (processStereo()) as a TRUE alternating
// PING-PONG: the taps keep the MONO spacing (D, 2D, 3D, ...) but alternate hard L / hard R —
// tap 1 -> Amp A (L), tap 2 -> Amp B (R), tap 3 -> L, ... — so the repeat RATE is the same as
// the mono delay (NOT doubled: there are no extra in-between taps). The dry note stays CENTRED
// (feeds both amps). Cross-coupled at UNITY on the L->R hop with the feedback decay on the
// R->L return, so each L/R pair is equal level (taps 1&2 same, 3&4 same a step quieter, ...),
// the decay happening once per PAIR. Both lanes share the ONE delay time, so there is no comb
// mismatch and no precedence/Haas lean (the flaw in the removed per-amp "Spread" stereo, where
// two DIFFERENT-time lanes pulled the image to one side). NO spread control — the ping-pong is
// fixed. (An impulse shows the R tap a hair under its L partner from one extra bandwidth-LP
// pass on the bounce; real guitar-band audio barely sees it.)
//
// Each amp still gets its OWN dry (a driveSend clean/driven split keeps its per-amp character).
//
// The three models (Robbie's pick), each grounded in how the real unit actually
// makes sound (BBD stage/clock physics, companding, converter bandwidth, preamp):
//
//   0  BOSS DD-7        — clean, transparent DIGITAL delay. VERIFIED from the 2008
//                         Roland service notes (docs/predelay/dd7.md): 2SK880 JFET
//                         input buffer (1 MΩ), 24-bit AK4552 codec + custom DSP,
//                         NO compander (dropped the DD-2/DD-3 NE570), NJM4558 output
//                         buffer (1 kΩ), buffered bypass. Full audio bandwidth,
//                         repeats do NOT degrade per pass, dry stays analog-unity and
//                         the wet is added on top. The neutral reference. (Boss's own
//                         Analog mode models a DM-2 and Modulate adds a static gentle
//                         chorus; here the base voice is the pristine digital delay and
//                         the Mod knob covers the Modulate behaviour.)
//
//   1  MXR CARBON COPY  — dark ANALOG BBD. VERIFIED from real-board repair traces +
//                         the official Dunlop M169 manual (docs/predelay/carbon_copy.md):
//                         4× BL3208 BBDs (2048 stages each = 8192 total, two series
//                         pairs), an SA571 compander (2:1 noise reduction, compress→BBD→
//                         expand), TL062 op-amps, 1 MΩ buffered input, 1 kΩ output, a
//                         clean analog dry through-path blended with the wet. A BBD's
//                         bandwidth is its clock's Nyquist (t = N/(2·fclk) → Nyquist =
//                         N/(4·t)), so the repeats band-limit and DARKEN as the delay
//                         lengthens (~3.4 kHz at 600 ms). The Carbon Copy is famously
//                         dark because a steep fixed Sallen-Key reconstruction filter
//                         dominates at all settings — the exact −3 dB corner is NOT
//                         circuit-verified (schematic images are bot-blocked); ~2.6 kHz
//                         here is an ear-tunable perceptual match for our single 2-pole
//                         in-loop LP (the real filter is ~3 kHz but 30–36 dB/oct). Mod =
//                         a subtle 0.2–2.2 Hz LFO on the BBD clock (pitch warble). No
//                         tone control on the M169. Max 600 ms; self-oscillates. FLAGGED
//                         items (corner, coupling caps, clock chip) offer a controlled-
//                         probe measurement against Robbie's real pedal.
//
//   2  MEMORY MAN       — EHX Deluxe Memory Man: lush ANALOG BBD. Researched from the real
//                         circuit (docs/predelay/memory_man.md): **2× MN3005 in SERIES =
//                         8192 stages**, 550 ms, CD4047 clock, NE570/571 compander, and NO
//                         tone control. Voiced from the FACTORY calibration (Howard Davis/EHX
//                         1978): the delay path is flat below ~900 Hz, has a RESONANT PRESENCE
//                         PEAK of ~+3 dB at ~2.5 kHz, and a −3 dB corner at ~3.2–3.5 kHz that
//                         rolls off sharply — modeled as one RESONANT reconstruction LP
//                         (antiAlias 3200, bwQ 1.30), no separate mid. That 2.5 kHz peak is why
//                         it reads present/hi-fi where the Carbon Copy stays dark. Its signature
//                         is a TRIANGLE-LFO modulation (Depth knob = a fixed musical depth, so the
//                         chorus/vibrato is consistent at any delay) through a TRUE CROSSFADE Blend
//                         — full-wet = vibrato, mid = chorus — plus a low-Z (~100 kΩ inverting)
//                         loading input. Sings and washes; self-oscillates readily.
//
// Signal per sample (mono, one lane):
//   dry = x
//   wet = line.read(delay + mod)           // fractional read, wow/chorus modulated
//   wet = in-loop low-cut (HP)             // controls bass build-up in the feedback
//   wet = in-loop bandwidth LP             // BBD: Nyquist(time) capped by anti-alias;
//                                          //   digital: fixed converter bandwidth
//   wet = in-loop mid bump                 // Memory Man only
//   wet = user Tone high-cut               // extra darkening on the repeats (optional)
//   wet = companding/preamp soft-clip      // cubic 2nd-order ADAA (BBD compander knee);
//                                          //   bounds self-oscillation
//   line.write(dry + fb·wet)               // feedback (fbCeiling per model)
//   out = presence sheen (output-once)     // digital top-end lift, not recirculated
//   y   = (1-mix)·dry + mix·out
//
// Analog models REPITCH on a time change (the clock sweeps → tape-style pitch swoop),
// so their base delay GLIDES; the digital models snap quickly (no repitch). All
// nonlinearities run antiderivative anti-aliasing (Saturation.h), like the rest of
// the rig, because the saturator sits inside the feedback loop. Zero latency; Off is
// a bit-exact passthrough (the chain skips a bypassed block). JUCE-free core DSP,
// verified by tests/predelay_test.cpp.

#include "Blocks.h"
#include "Lfo.h"
#include "Biquad.h"
#include "Saturation.h"
#include "IoStage.h"
#include <algorithm>
#include <cmath>

namespace nam_rig
{

class PreDelayBlock : public MonoBlock
{
public:
    enum Model { kDD7 = 0, kCarbonCopy = 1, kMemoryMan = 2, kNumModels = 3 };

    // The Boss DD-7 MODE rotary (8 positions, service-notes / owner's-manual order).
    // 0-3 are the normal digital delay at four time RANGES (the D.TIME knob spans
    // within); 4-7 are the special modes. Only the DD-7 uses this; the other models
    // ignore it. HOLD = freeze/loop; MODULATE = a small static chorus; ANALOG = the
    // DM-2 model (progressive HF-darkened repeats); REVERSE = reversed playback.
    enum Dd7Mode { kMode50 = 0, kMode200, kMode800, kMode3200,
                   kHold, kModulate, kAnalog, kReverse, kNumDd7Modes };
    static float modeRangeMaxMs(int mode)
    {
        switch (mode)
        {
        case kMode50:   return 50.0f;
        case kMode200:  return 200.0f;
        case kMode800:  return 800.0f;
        case kMode3200: return kMaxTimeMs; // 3200 ms clamped to the plugin's 2000 ms ceiling
        case kModulate:
        case kAnalog:   return 800.0f;     // DM-2/Modulate span 20-800 ms on the real unit
        case kReverse:  return kMaxTimeMs; // 300-3200 ms
        default:        return kMaxTimeMs; // Hold
        }
    }
    static constexpr float kModulateRateHz = 1.0f;  // DD-7 Modulate: fixed, "just enough warble"
    static constexpr float kModulateDepth  = 0.4f;
    // Carbon Copy: the real M169 has NO mod knob — its modulation is set by two INTERNAL
    // trimmers (WIDTH/RATE), always on and subtle. So the Carbon Copy uses a FIXED internal
    // mod depth and ignores the user Mod param (its panel shows only Delay/Regen/Mix).
    static constexpr float kCarbonCopyMod  = 0.35f;
    // Memory Man Chorus/Vibrato switch = the EH7850's LFO speed-range select (it swaps a
    // cap: Chorus = slow, Vibrato = fast; it does NOT change the waveform). Named DMM rates.
    static constexpr float kDmmChorusRateHz  = 0.85f; // factory: chorus "slightly less than 1 Hz"
    static constexpr float kDmmVibratoRateHz = 4.0f;  // factory: vibrato "approx 4 Hz"
    static constexpr double kAnalogLpHz    = 2800.0; // DM-2 model: in-loop darkening of the repeats
    static constexpr float kAnalogSatDrive = 0.4f, kAnalogSatAsym = 0.05f;

    static constexpr float kMinTimeMs = 20.0f;
    static constexpr float kMaxTimeMs = 2000.0f;     // param ceiling; each model clamps to its real max
    static constexpr float kMaxFeedbackHard = 1.10f; // absolute safety clamp

    // Tempo-sync divisions in quarter-note beats; index 0 = Off (free). Same order
    // and convention as PreModBlock, so the parameter StringArray is shared. Append
    // only — never reorder.
    static constexpr int kNumSync = 10;
    static double syncBeats(int i)
    {
        static const double beats[kNumSync] = {0.0, 4.0, 2.0, 1.0, 1.5, 2.0 / 3.0,
                                               0.5, 0.75, 1.0 / 3.0, 0.25};
        return (i > 0 && i < kNumSync) ? beats[i] : 0.0;
    }

    // Per-model voicing. All-linear/zero stages = a clean digital delay; each model
    // grows its character from the fields below.
    struct Voicing
    {
        bool  bbd;          // analog bucket-brigade: time-dependent bandwidth + repitch glide
        float maxTimeMs;    // the real unit's maximum delay (clamps the Time knob's top)
        float bbdStages;    // total BBD stages -> Nyquist(time) = stages/(4·tSec); ignored if !bbd
        float antiAliasHz;  // fixed reconstruction/anti-alias LP ceiling (in-loop, recirculates)
        float bwQ;          // Q of the in-loop bandwidth LP: 0.5 = gentle/dark (no peak);
                            //   >0.707 = a RESONANT reconstruction filter -> a presence peak
                            //   just below the corner (Memory Man's measured ~2.5 kHz peak)
        float loopHpHz;     // in-loop low-cut (bass build-up control); 0 = off
        float midHz, midDb, midQ; // in-loop mid bump (0 dB = off; unused now the DMM uses bwQ)
        float satDrive, satAsym;  // companding/preamp soft-clip (cubic ADAA, in-loop); 0 = clean
        float presHz, presDb;     // output-once presence sheen (digital top); 0 dB = off
        float modRateHz, modDepthMs; // built-in modulation: a FIXED absolute ms depth (user Mod/
                            //   Depth knob scales it). Fixed ms -> the pitch-mod swing is CONSTANT
                            //   across delay settings (pitch dev = depthMs·4·rate, independent of
                            //   the delay time) = a musical, predictable chorus/vibrato at any delay.
        float glideMs;      // time-change glide (BBD repitch swoop vs a quick digital move)
        float fbCeiling;    // feedback ceiling (analog can self-oscillate >1 — sat bounds it)
    };

    static Voicing voicingFor(Model m)
    {
        switch (m)
        {
        case kCarbonCopy:
            // Dark analog BBD — VERIFIED parts (docs/predelay/carbon_copy.md): 4× BL3208
            // = 8192 stages for 600 ms; SA571 compander (gentle program-dependent knee ->
            // satDrive 0.50/asym 0.06, subtle, not distortion); self-oscillates past ~2
            // o'clock (fbCeiling 1.18 -- higher than the DD-7's 1.05 because the dark
            // in-loop LP + compander eat more loop gain per pass, so the ceiling has to
            // overcome those lumped losses to reproduce the hardware self-oscillation;
            // bounded by the in-loop compander sat + loopLimit); mod
            // = a subtle 0.2-2.2 Hz clock warble (modRateHz 1.2 within range, depth 1.3 ms);
            // BBD repitch swoop on a time change (glideMs 70). antiAliasHz ~2.6 kHz is an
            // ear-tunable perceptual match for our single 2-pole in-loop LP standing in for
            // the real steep (~30-36 dB/oct, ~3 kHz) Sallen-Key reconstruction filter -- the
            // exact corner is NOT circuit-verified (schematic bot-blocked); offer the
            // controlled-probe measurement. loopHpHz 100 = a gentle bass-runaway floor (the
            // M169 is warm/keeps lows). No mid bump, no presence, no tone control on the
            // M169. Input 1 MOhm / output 1 kOhm buffered stage in applyIo().
            // modDepthMs 1.3 with the fixed internal amount (kCarbonCopyMod 0.35) = a SUBTLE
            // warble (the M169's mod is deliberately gentle). bwQ 0.5 = a dark, NON-resonant LP
            // (no presence peak -- the M169 has no Bright switch), why it reads darker than the DMM.
            //       bbd    maxMs    stages   aa       bwQ    hp      midHz midDb midQ  sat    asym   presHz presDb modHz  modMs glide fbC
            return { true,  600.0f,  8192.0f, 2600.0f, 0.50f, 100.0f, 0.0f, 0.0f, 0.7f, 0.50f, 0.06f, 0.0f,  0.0f,  1.20f, 1.3f, 70.0f, 1.18f };
        case kMemoryMan:
            // Lush analog BBD — 2× MN3005 in SERIES = 8192 stages, 550 ms, CD4047 clock,
            // NE570/571 compander (docs/predelay/memory_man.md). REVOICED 2026-07-04 from the
            // FACTORY calibration procedure (Howard Davis/EHX 1978, archived by Morrin), which
            // specifies the delay-path response: flat below ~900 Hz, a RESONANT PRESENCE PEAK of
            // ~+3 dB at ~2.5 kHz, and a −3 dB corner at ~3.2–3.5 kHz that rolls off sharply. So
            // the voice is a single RESONANT reconstruction LP: antiAlias 3200 (the −3 dB corner,
            // just above the 550 ms clock-Nyquist so a subtle time-darkening remains) with bwQ
            // 1.30 -> +3.0 dB peak at ~2.5 kHz. NO separate mid bump (midDb 0): the old "+4 dB @
            // 650 Hz" was a mislocation of this 2.5 kHz filter resonance ~2 octaves too low. The
            // 2.5 kHz peak is what makes the DMM read "present/hi-fi" vs the Carbon Copy's dark,
            // non-resonant LP. Gentle 80 Hz low-cut ("little bass cut"). Modulation = a TRIANGLE
            // LFO on the delay (Depth knob = modDepthMs 2.5, a FIXED ms) -> a musical, delay-
            // independent chorus/vibrato (~±70 cent vibrato @ Depth max, ~±15 cent chorus). NB the
            // BBD clock warble is physically a % of the period, but realised that way the pitch
            // swing scales with delay -> an unusable multi-octave warble at long delays (Robbie
            // ear-fix 2026-07-04: fixed ms instead). A true CROSSFADE Blend (full wet = vibrato,
            // mid = chorus) + a low-Z loading input — all wired below. fbCeiling 1.06 self-
            // oscillates readily (a DMM feature; bounded by the in-loop compander sat + loopLimit).
            // Chorus rate ~0.85 Hz / Vibrato ~4 Hz (factory). No tone control on the DMM.
            //       bbd    maxMs    stages   aa       bwQ    hp     midHz midDb midQ  sat    asym   presHz presDb modHz modMs glide  fbC
            return { true,  550.0f,  8192.0f, 3200.0f, 1.30f, 80.0f, 0.0f, 0.0f, 0.80f, 0.45f, 0.06f, 0.0f,  0.0f,  1.60f, 2.5f, 80.0f, 1.06f };
        case kDD7:
        default:
            // Boss DD-7 — VERIFIED from the 2008 Roland service notes (docs/predelay/dd7.md):
            // 24-bit AK4552 codec + custom DSP, NO compander (dropped the DD-2/DD-3 NE570),
            // NO in-loop tone filter -> the standard digital repeats are FULL-BANDWIDTH and
            // do NOT degrade per pass (constant bandwidth, unlike a BBD). So: antiAlias ~19 kHz
            // (effectively full band; the recirculating 2-pole there is transparent in the
            // guitar band yet loses a hair per pass so max feedback can't run away), satDrive 0
            // (no companding/compression), fbCeiling 1.05 (F.BACK self-oscillates -> "Trick
            // Sound"). The Mod knob defaults 0 so the standard mode is clean; dialing it in
            // emulates the DD-7's separate Modulate mode (a small static chorus). Repitch off
            // (digital, quick time move). Input 1 MΩ / output 1 kΩ buffered stage in ioFor().
            // glide 45 ms: turning D.TIME re-clocks the buffer -> the trails PITCH-BEND
            // as they slew (the authentic digital-delay time-sweep), not an instant jump.
            // fbCeiling 1.05: F.BACK builds into a self-oscillating swell at the top
            // ("Trick Sound") — the loopLimit backstop keeps it bounded.
            //       bbd    maxMs     stages aa        bwQ    hp    midHz midDb midQ  sat   asym  presHz presDb modHz modMs glide fbC
            return { false, 2000.0f, 0.0f,  19000.0f, 0.50f, 0.0f, 0.0f, 0.0f, 0.7f, 0.0f, 0.0f, 0.0f,  0.0f,  0.35f, 0.9f, 45.0f, 1.05f };
        }
    }

    const char *name() const override { return "Pre Delay"; }

    void prepare(const BlockContext &ctx) override
    {
        mFs = ctx.sampleRate;
        // Ring sized for TWICE the full Time range: REVERSE mode reads backward through
        // the buffer at 2x the write rate, so a T-length reverse grain spans 2T of line.
        const int maxDelay = (int)std::ceil((2.0f * kMaxTimeMs + 20.0f) * 0.001f * (float)mFs);
        mSmoothK = 1.0f - std::exp((float)(-1.0 / (0.010 * mFs))); // 10 ms de-zip on mix
        updateGlide();
        for (Lane *ln : {&mLaneL, &mLaneR})
        {
            ln->line.prepare(maxDelay);
            ln->lfo.prepare(mFs);
            ln->lfo.setWaveform(lfoWaveFor()); // per-model LFO waveform (Memory Man triangle, else sine)
            ln->io.prepare(mFs);
            applyIo(*ln);
            ln->analogLp = Biquad::lowpass(mFs, std::min(kAnalogLpHz, 0.45 * mFs), 0.5); // DD-7 Analog (DM-2) darkening
            rebuildFixed(*ln, true);
        }
        reset();
        mPrepared = true;
    }

    void reset() override
    {
        for (Lane *ln : {&mLaneL, &mLaneR})
        {
            ln->line.reset();
            ln->lfo.reset();
            ln->io.reset();
            ln->loopHp.reset();
            ln->loopLp.reset();
            ln->mid.reset();
            ln->tone.reset();
            ln->pres.reset();
            ln->analogLp.reset();
            ln->satX1 = ln->satX2 = 0.0;
            ln->dcX1 = ln->dcY1 = 0.0;
            ln->aSatX1 = ln->aSatX2 = ln->aDcX1 = ln->aDcY1 = 0.0;
            ln->revPhase = 0.0;
            ln->baseZ = (double)currentTimeMs();
            ln->mixZ = mMix;
            ln->levelZ = mLevel;
        }
    }

    // ---- parameters (audio thread) ----
    void setModel(int m)
    {
        const Model mm = (Model)std::min(std::max(m, 0), (int)kNumModels - 1);
        if (mm != mModel)
        {
            mModel = mm;
            mVoicing = voicingFor(mm);
            if (mPrepared)
            {
                updateGlide();
                for (Lane *ln : {&mLaneL, &mLaneR})
                {
                    applyIo(*ln);
                    rebuildFixed(*ln, true);
                    ln->lfo.setRateHz(mVoicing.modRateHz);
                    ln->lfo.setWaveform(lfoWaveFor());
                }
            }
        }
    }
    void setDd7Mode(int m) { mMode = std::clamp(m, 0, (int)kNumDd7Modes - 1); }
    void setTimeMs(float ms) { mTimeMs = std::clamp(ms, kMinTimeMs, kMaxTimeMs); }
    void setSyncIndex(int i) { mSyncIndex = std::clamp(i, 0, kNumSync - 1); }
    void setBpm(double bpm) { if (bpm > 1.0) mBpm = bpm; }
    void setFeedback(float f) { mFeedback = std::clamp(f, 0.0f, 1.0f); } // 0..1 knob
    void setMix(float m) { mMix = std::clamp(m, 0.0f, 1.0f); }
    void setMod(float m) { mModAmt = std::clamp(m, 0.0f, 1.0f); }        // scales the built-in mod depth
    void setLevel(float l) { mLevel = std::clamp(l, 0.0f, 1.0f); }       // Memory Man master Volume/Level (unity at 1)
    void setChorusVib(int m) { mChorusVib = std::clamp(m, 0, 1); }       // Memory Man 0=Chorus (slow) 1=Vibrato (fast)
    void setToneHz(float hz)
    {
        if (hz != mToneHz) { mToneHz = hz; if (mPrepared) { rebuildTone(mLaneL); rebuildTone(mLaneR); } }
    }

    // Effective (sync-resolved) base delay, clamped to the model's real maximum.
    float currentTimeMs() const
    {
        float t = mTimeMs;
        const double beats = syncBeats(mSyncIndex);
        if (beats > 0.0)
            t = (float)(beats * 60000.0 / mBpm);
        float maxMs = std::min(kMaxTimeMs, mVoicing.maxTimeMs);
        if (mModel == kDD7) maxMs = std::min(maxMs, modeRangeMaxMs(mMode)); // DD-7 MODE time range
        return std::clamp(t, kMinTimeMs, maxMs);
    }

    // MONO: drive the L lane ONLY at the base time -> BIT-EXACT to the pre-stereo block.
    void process(float *mono, int numSamples) override
    {
        processLane(mono, numSamples, mLaneL, currentTimeMs());
    }

    // STEREO PING-PONG (mono-in / stereo-out): taps keep the MONO spacing (D, 2D, 3D...) but
    // alternate hard L / hard R (same repeat rate as mono, no extra taps). Cross-coupled unity
    // L->R hop + feedback on the R->L return -> equal-level L/R pairs (1&2, 3&4, ...). Dry
    // centred; each amp keeps its OWN dry. No spread — the ping-pong is fixed.
    void processStereo(float *left, float *right, int numSamples)
    {
        const float t = currentTimeMs(); // one delay time, both lanes (same repeat rate as mono)

        mLaneL.io.processIn(left, numSamples);
        mLaneR.io.processIn(right, numSamples);

        const int mode = (mModel == kDD7) ? mMode : (int)kMode800;
        const bool hold = (mode == kHold), reverse = (mode == kReverse);
        const bool analog = (mode == kAnalog), modulate = (mode == kModulate);
        float lfoRate = mVoicing.modRateHz;
        if (mModel == kDD7 && modulate)  lfoRate = kModulateRateHz;
        else if (mModel == kMemoryMan)   lfoRate = mChorusVib ? kDmmVibratoRateHz : kDmmChorusRateHz;
        mLaneL.lfo.setRateHz(lfoRate);
        mLaneR.lfo.setRateHz(lfoRate);
        const float fb = hold ? 1.0f : (mFeedback * mVoicing.fbCeiling);
        const float modAmt = modulate ? kModulateDepth
                           : (mModel == kCarbonCopy ? kCarbonCopyMod : mModAmt);
        updateBandwidth(mLaneL, t);
        updateBandwidth(mLaneR, t);
        const double fsK = 0.001 * mFs;

        for (int i = 0; i < numSamples; ++i)
        {
            const float dryL = left[i], dryR = right[i];
            float outwL = 0.0f, outwR = 0.0f;
            const float wetL = laneRecirc(mLaneL, dryL, reverse, analog, modAmt, fsK, t, outwL);
            const float wetR = laneRecirc(mLaneR, dryR, reverse, analog, modAmt, fsK, t, outwR);
            // TRUE alternating ping-pong: dry enters L; L bounces to R at UNITY (so the R tap
            // matches the L tap it came from), R feeds back to L scaled by the feedback knob.
            // The taps stay at the MONO spacing (D, 2D, 3D, ...) but alternate L, R, L, R — same
            // repeat rate as mono, no extra in-between taps. Each L/R pair is equal level (1&2,
            // 3&4, ...), decaying once per pair. Dry stays centred; each amp keeps its own dry.
            mLaneL.line.write(loopLimit(hold ? (fb * wetR) : (dryL + fb * wetR)));
            mLaneR.line.write(loopLimit(wetL));
            left[i]  = mixLaw(mLaneL, dryL, outwL);
            right[i] = mixLaw(mLaneR, dryR, outwR);
            mLaneL.lfo.advance();
            mLaneR.lfo.advance();
        }

        mLaneL.io.processOut(left, numSamples);
        mLaneR.io.processOut(right, numSamples);
        flushDenormals(mLaneL);
        flushDenormals(mLaneR);
    }

    double latencySamples() const override { return 0.0; }

    // ---- verification hooks ----
    Model model() const { return mModel; }
    Voicing currentVoicing() const { return mVoicing; }
    float currentLoopLpHz() const { return mLaneL.loopLpHzBuilt; } // effective in-loop bandwidth corner (L lane)

private:
    // One delay lane = a full mono delay (line + feedback loop + filters + modulation +
    // I/O). process() uses mLaneL only (bit-exact mono); processStereo runs BOTH lanes as a
    // ping-pong. Each lane owns its LFO/filters/sat state so the two can recirculate
    // independently — mono stays byte-identical to the single-lane block.
    struct Lane
    {
        FracDelayLine line;
        Lfo lfo;
        IoStage io; // per-lane input+output stage (impedance/coupling/buffer)
        Biquad loopHp, loopLp, mid, tone, pres, analogLp;
        bool loopHpOn = false, loopLpOn = true, midOn = false, toneOn = false, presOn = false;
        float loopLpHzBuilt = -1.0f; // last-built in-loop bandwidth corner (BBD tracks time)
        double revPhase = 0.0;       // Reverse-mode grain phase
        double aSatX1 = 0.0, aSatX2 = 0.0, aDcX1 = 0.0, aDcY1 = 0.0; // Analog-mode sat state
        double satX1 = 0.0, satX2 = 0.0;   // cubic ADAA history
        double dcX1 = 0.0, dcY1 = 0.0;     // even-harmonic DC blocker
        double baseZ = 350.0;              // glided base delay (ms)
        float mixZ = 0.28f, levelZ = 1.0f; // smoothed Mix / Level
    };

    // Per-sample core for ONE lane: advance the Mix/Level smoothers + delay glide, read
    // the wet at the (modulated) delay time, run the in-loop filters/sat, and compute the
    // output-once presence wet. Returns the RECIRCULATING (filtered) wet to feed back and
    // sets `outwOut` (the presence-shaped wet for the output mix). Does NOT write the line
    // or advance the LFO — the caller owns the feedback write. Byte-identical maths to the
    // former inline loop, so mono stays bit-exact.
    float laneRecirc(Lane &ln, float dry, bool reverse, bool analog, float modAmt,
                     double fsK, float baseTarget, float &outwOut)
    {
        (void)dry; // dry is the caller's; kept in the signature for symmetry/readability
        ln.mixZ += mSmoothK * (mMix - ln.mixZ);
        ln.levelZ += mSmoothK * (mLevel - ln.levelZ);
        // Glide the base delay toward the target (analog = slow pitch swoop; digital =
        // quick, click-free). Snap when within a hair to kill one-pole crawl.
        ln.baseZ += (double)mGlideK * ((double)baseTarget - ln.baseZ);
        if (std::abs((double)baseTarget - ln.baseZ) < 1.0e-4) ln.baseZ = baseTarget;

        const double lfo = (double)ln.lfo.value();
        // Modulation depth is a FIXED absolute ms (not a % of the delay) -> constant musical
        // pitch mod at every delay (pitch dev = depthMs·4·rate). (Robbie ear-fix 2026-07-04.)
        const double modMs = (double)modAmt * (double)mVoicing.modDepthMs * lfo;
        const double tSamp = std::max(3.0, (ln.baseZ + modMs) * fsK);
        float wet = reverse ? reverseRead(ln, tSamp) : ln.line.readFrac6(tSamp - 1.0); // REVERSE = grain playback

        // In-loop tone (recirculates -> compounds per repeat = analog "repeats darken"):
        if (ln.loopHpOn) wet = ln.loopHp.processSample(wet); // low-cut
        if (ln.loopLpOn) wet = ln.loopLp.processSample(wet); // bandwidth (BBD Nyquist / converter)
        if (ln.midOn)    wet = ln.mid.processSample(wet);    // Memory Man mid bump
        if (ln.toneOn)   wet = ln.tone.processSample(wet);   // user high-cut
        if (analog) { wet = ln.analogLp.processSample(wet); wet = analogSat(ln, wet); } // DD-7 ANALOG
        // Companding / preamp soft-clip (cubic ADAA, in-loop): BBD compander knee.
        if (mVoicing.satDrive > 0.0f) wet = loopSat(ln, wet);

        // Output-once presence sheen (not recirculated -> shapes timbre without compounding).
        float outw = wet;
        if (ln.presOn) outw = ln.pres.processSample(outw);
        outwOut = outw;
        return wet;
    }

    // Output blend for one lane. DD-7 / Carbon Copy ADD the wet on top of a unity dry;
    // Memory Man BLEND is a TRUE CROSSFADE (full wet = vibrato, mid = chorus) + master Level.
    float mixLaw(Lane &ln, float dry, float outw) const
    {
        const bool crossfade = (mModel == kMemoryMan);
        const float blended = crossfade ? ((1.0f - ln.mixZ) * dry + ln.mixZ * outw)
                                        : (dry + ln.mixZ * outw);
        return (mModel == kMemoryMan) ? (blended * ln.levelZ) : blended;
    }

    // Process one lane in place at the given (pre-glide) base delay time. This is the
    // former mono process() loop, now parameterised on the lane + its target time.
    void processLane(float *buf, int numSamples, Lane &ln, float baseTarget)
    {
        ln.io.processIn(buf, numSamples); // input buffer / coupling stage (colours dry + delay input)
        // DD-7 MODE (other models behave as a plain digital delay = kMode800):
        const int mode = (mModel == kDD7) ? mMode : (int)kMode800;
        const bool hold = (mode == kHold), reverse = (mode == kReverse);
        const bool analog = (mode == kAnalog), modulate = (mode == kModulate);
        // LFO rate: the DD-7 Modulate MODE forces a fixed chorus; the Memory Man's rate is
        // set by its Chorus/Vibrato switch (slow/fast); others use the voicing rate.
        float lfoRate = mVoicing.modRateHz;
        if (mModel == kDD7 && modulate)  lfoRate = kModulateRateHz;
        else if (mModel == kMemoryMan)   lfoRate = mChorusVib ? kDmmVibratoRateHz : kDmmChorusRateHz;
        ln.lfo.setRateHz(lfoRate);
        // Feedback: knob (0..1) scaled by the model ceiling; HOLD locks it to 1 (freeze).
        // Analog models push past unity (self-oscillation) — the in-loop companding
        // soft-clip + loopLimit bound the level so it never runs away.
        const float fb = hold ? 1.0f : (mFeedback * mVoicing.fbCeiling);
        // MODULATE mode = fixed chorus depth; the Carbon Copy's mod is an INTERNAL always-on
        // trimmer warble (no mod knob) = a fixed subtle amount; other models use the Mod knob.
        const float modAmt = modulate ? kModulateDepth
                           : (mModel == kCarbonCopy ? kCarbonCopyMod : mModAmt);
        // BBD bandwidth tracks the clock: recompute the in-loop LP corner for this
        // block from the target time (cheap — once per block, not per sample). Digital
        // models keep their fixed converter bandwidth.
        updateBandwidth(ln, baseTarget);
        const double fsK = 0.001 * mFs;

        for (int i = 0; i < numSamples; ++i)
        {
            const float dry = buf[i];
            float outw = 0.0f;
            // Read + in-loop-filter this lane's recirculating wet (no line write yet).
            const float wet = laneRecirc(ln, dry, reverse, analog, modAmt, fsK, baseTarget, outw);
            // Mono / own-lane feedback: the delay input is dry + fb·its OWN wet.
            ln.line.write(loopLimit(hold ? (fb * wet) : (dry + fb * wet)));
            buf[i] = mixLaw(ln, dry, outw);
            ln.lfo.advance();
        }
        ln.io.processOut(buf, numSamples); // output buffer / coupling stage (per model)
        flushDenormals(ln);
    }

    // Loop safety/headroom limiter: TRANSPARENT below ±kLoopLin so normal repeats are
    // bit-clean (critical for the clean digital models), soft-limiting above so that at
    // max feedback the loop SWELLS and SUSTAINS to a bounded drone — the DD-7's digital-
    // headroom clip on the "Trick Sound" self-oscillation — instead of running away.
    // Applied to the feedback write for every model (the analog models' in-loop compander
    // already bounds them well below this, so it never bites there).
    static float loopLimit(float x)
    {
        constexpr float kLin = 1.5f, kCeil = 2.2f; // linear to ±1.5; asymptote ±2.2
        const float a = std::abs(x);
        if (a <= kLin) return x;
        const float s = x < 0.0f ? -1.0f : 1.0f;
        return s * (kLin + (kCeil - kLin) * std::tanh((a - kLin) / (kCeil - kLin)));
    }

    // In-loop companding/preamp soft-clip: the cubic soft-clip on 2nd-order ADAA
    // (shared kernel, Saturation.h), plus an optional even-harmonic (cosh) term for
    // the asymmetric BBD/preamp warmth, DC-blocked so nothing accumulates in the
    // loop. Same construction as DelayBlock's tape saturation, single channel.
    float loopSat(Lane &ln, float x)
    {
        const double drive = (double)mVoicing.satDrive;
        const double asym  = (double)mVoicing.satAsym;
        const double xb = (double)x * drive;
        const double y = sat::cubicADAA2(xb, ln.satX1, ln.satX2);
        ln.satX2 = ln.satX1;
        ln.satX1 = xb;
        double yb = (drive > 1.0e-9) ? y / drive : (double)x;
        if (asym != 0.0)
        {
            const double xc = (double)x > 1.0 ? 1.0 : ((double)x < -1.0 ? -1.0 : (double)x);
            const double in = yb + asym * (std::cosh(kEvenShape * xc) - 1.0);
            yb = in - ln.dcX1 + kDcBlockR * ln.dcY1; // one-pole DC blocker
            ln.dcX1 = in;
            ln.dcY1 = yb;
        }
        const float out = (float)yb;
        return std::isfinite(out) ? out : 0.0f;
    }

    // DD-7 REVERSE mode: two crossfaded reverse grains of length tSamp. To play the last
    // T samples BACKWARDS while the line keeps recording forwards, the read offset
    // ("samples ago") must sweep 0 -> 2T over a grain (the read pointer moves back through
    // the buffer at 2x the write rate: absolute read = A - phase*T, i.e. newest-to-oldest).
    // The two half-offset grains use a sin^2 window that sums to unity, and the window is
    // 0 at the 2T->0 wrap, so the grain boundary doesn't click.
    float reverseRead(Lane &ln, double tSamp)
    {
        const double T = std::max(64.0, tSamp);
        float out = 0.0f;
        for (int g = 0; g < 2; ++g)
        {
            double ph = ln.revPhase + 0.5 * (double)g;
            ph -= std::floor(ph);
            const float s = ln.line.readFrac6(std::max(2.0, 2.0 * ph * T)); // 2x sweep = true reverse
            const float win = 0.5f * (1.0f - std::cos(2.0 * 3.14159265358979323846 * ph));
            out += s * win;
        }
        ln.revPhase += 1.0 / T;
        if (ln.revPhase >= 1.0) ln.revPhase -= 1.0;
        return out;
    }

    // DD-7 ANALOG mode warmth: a light asymmetric cubic soft-clip (its own ADAA state,
    // fixed drive) so the DM-2 model rounds/compresses the darkened repeats.
    float analogSat(Lane &ln, float x)
    {
        const double drive = (double)kAnalogSatDrive, asym = (double)kAnalogSatAsym;
        const double xb = (double)x * drive;
        const double y = sat::cubicADAA2(xb, ln.aSatX1, ln.aSatX2);
        ln.aSatX2 = ln.aSatX1;
        ln.aSatX1 = xb;
        double yb = y / drive;
        const double xc = (double)x > 1.0 ? 1.0 : ((double)x < -1.0 ? -1.0 : (double)x);
        const double in = yb + asym * (std::cosh(kEvenShape * xc) - 1.0);
        yb = in - ln.aDcX1 + kDcBlockR * ln.aDcY1;
        ln.aDcX1 = in;
        ln.aDcY1 = yb;
        const float out = (float)yb;
        return std::isfinite(out) ? out : 0.0f;
    }

    // Recompute the glide coefficient from the model's time-change feel.
    void updateGlide()
    {
        const double ms = std::max(1.0f, mVoicing.glideMs);
        mGlideK = 1.0f - (float)std::exp(-1.0 / (ms * 0.001 * mFs));
    }

    // LFO waveform per model: Memory Man = triangle; everything else = sine.
    int lfoWaveFor() const { return (mModel == kMemoryMan) ? (int)Lfo::Triangle : (int)Lfo::Sine; }

    // Per-model INPUT+OUTPUT stage (impedance loading / coupling / buffer), reusing
    // IoStage.h like DriveBlock. Each model's I/O is now set from its researched circuit
    // doc: DD-7 (2008 Roland service notes, dd7.md) = 2SK880 JFET buffer, 1 MΩ in = the DI
    // reference -> NO loading shelf, 10 µF couplings -> subsonic HPs, 24-bit codec -> no HF
    // smoothing, so transparent buffered. Carbon Copy (M169 manual, carbon_copy.md) = 1 MΩ
    // buffered in / 1 kΩ out, also transparent (darkness is the in-loop LP, not the I/O).
    // Memory Man (memory_man.md) = a LOW ~100 kΩ inverting input that LOADS the source (the
    // "dark dry" gotcha) -> a gentle high-shelf cut. Only impedances/behaviour are anchored;
    // exact coupling-cap values are schematic-gated (bot-blocked images) so the couplings are
    // modeled as subsonic HPs. Never guessed.
    void applyIo(Lane &ln)
    {
        switch (mModel)
        {
        case kDD7:
            ln.io.setBuffered(2.0f, 1.6f, 0.0f, 0.0f); // subsonic in/out coupling HPs; else transparent
            break;
        case kCarbonCopy:
            // VERIFIED from the Dunlop M169 manual (docs/predelay/carbon_copy.md): 1 MΩ
            // buffered input (= the DI reference -> NO loading shelf) and 1 kΩ output.
            // Coupling-cap values are schematic-gated/unverified -> modeled as subsonic
            // high-passes like the DD-7. No output HF smoothing: the darkness is the
            // in-loop reconstruction LP, not the output buffer.
            ln.io.setBuffered(2.0f, 1.6f, 0.0f, 0.0f);
            break;
        case kMemoryMan:
            // Vintage DMM (docs/predelay/memory_man.md): a LOW ~100 kΩ INVERTING input that
            // loads the source (the famous "dark dry tone" gotcha) — unlike the DD-7/Carbon
            // Copy 1 MΩ buffers. Modeled as a gentle high-shelf CUT (mostly relevant with a
            // high-Z guitar; subtle here since the predelay sits after the buffered drive).
            // ~300 Ω buffered output (Nano spec). Exact coupling caps schematic-gated -> subsonic HPs.
            ln.io.setLoaded(7.0f, 3000.0f, -1.5f, -0.5f, 2.0f, 0.0f);
            break;
        default:
            ln.io.setTransparent(); // default (all three models set their I/O above)
            break;
        }
    }

    // Build the FIXED (time-independent) filters for the current model on one lane: low-
    // cut, mid bump, presence, and the digital bandwidth LP. The BBD bandwidth LP is
    // (re)built per block by updateBandwidth(); force=true also seeds it here.
    void rebuildFixed(Lane &ln, bool force)
    {
        ln.loopHpOn = mVoicing.loopHpHz > 0.0f;
        if (ln.loopHpOn)
            ln.loopHp = Biquad::highpass(mFs, std::min((double)mVoicing.loopHpHz, 0.45 * mFs), 0.5);

        ln.midOn = mVoicing.midDb != 0.0f && mVoicing.midHz > 0.0f;
        if (ln.midOn)
            ln.mid = Biquad::peaking(mFs, std::min((double)mVoicing.midHz, 0.45 * mFs),
                                     (double)mVoicing.midQ, (double)mVoicing.midDb);

        ln.presOn = mVoicing.presDb != 0.0f && mVoicing.presHz > 0.0f;
        if (ln.presOn)
            ln.pres = Biquad::peaking(mFs, std::min((double)mVoicing.presHz, 0.45 * mFs),
                                      0.7, (double)mVoicing.presDb);

        rebuildTone(ln);

        if (force && !mVoicing.bbd)
        {
            // Digital: fixed converter bandwidth, build once.
            ln.loopLpHzBuilt = std::min(mVoicing.antiAliasHz, (float)(0.45 * mFs));
            ln.loopLp = Biquad::lowpass(mFs, ln.loopLpHzBuilt, mVoicing.bwQ);
            ln.loopLpOn = true;
        }
        else if (force && mVoicing.bbd)
        {
            ln.loopLpHzBuilt = -1.0f; // force a rebuild on the next updateBandwidth()
            updateBandwidth(ln, currentTimeMs());
        }
    }

    void rebuildTone(Lane &ln)
    {
        ln.toneOn = mToneHz < 20000.0f;
        if (ln.toneOn)
            ln.tone = Biquad::lowpass(mFs, std::min((double)mToneHz, 0.45 * mFs));
    }

    // BBD bandwidth = the clock's Nyquist, which falls as the delay lengthens:
    //   fclk = stages/(2·tSec)  ->  Nyquist = stages/(4·tSec)
    // The effective in-loop corner is that Nyquist (with a little margin) capped by
    // the fixed reconstruction/anti-alias filter, and floored so it never collapses.
    // Digital models are fixed and skip this. Only rebuilds when the corner really
    // moves (>~3%), so a steady setting costs nothing.
    void updateBandwidth(Lane &ln, float timeMs)
    {
        if (!mVoicing.bbd) return;
        const float tSec = std::max(0.005f, timeMs * 0.001f);
        const float nyq = 0.85f * mVoicing.bbdStages / (4.0f * tSec);
        const float corner = std::min(mVoicing.antiAliasHz, std::max(700.0f, nyq));
        if (ln.loopLpHzBuilt < 0.0f || std::abs(corner - ln.loopLpHzBuilt) > 0.03f * ln.loopLpHzBuilt)
        {
            const float z1 = ln.loopLp.z1, z2 = ln.loopLp.z2; // keep state across a coefficient swap
            ln.loopLp = Biquad::lowpass(mFs, std::min((double)corner, 0.45 * mFs), mVoicing.bwQ);
            ln.loopLp.z1 = z1;
            ln.loopLp.z2 = z2;
            ln.loopLpHzBuilt = corner;
            ln.loopLpOn = true;
        }
    }

    void flushDenormals(Lane &ln)
    {
        for (Biquad *b : {&ln.loopHp, &ln.loopLp, &ln.mid, &ln.tone, &ln.pres})
        {
            if (std::abs(b->z1) < 1.0e-30f) b->z1 = 0.0f;
            if (std::abs(b->z2) < 1.0e-30f) b->z2 = 0.0f;
        }
        if (std::abs(ln.dcY1) < 1.0e-30) ln.dcY1 = 0.0;
    }

    double mFs = 48000.0, mBpm = 120.0;
    Model mModel = kDD7;
    Voicing mVoicing = voicingFor(kDD7);

    Lane mLaneL, mLaneR; // L = mono / Amp A; R = Amp B (ping-pong, processStereo only)

    int mMode = kMode3200;               // DD-7 MODE rotary (default = widest range, full Time range)
    static constexpr double kDcBlockR = 0.9995; // ~3.8 Hz one-pole DC blocker
    static constexpr double kEvenShape = 2.0;   // cosh even-harmonic richness

    float mTimeMs = 350.0f, mFeedback = 0.35f, mMix = 0.28f, mModAmt = 0.25f, mToneHz = 20000.0f;
    float mLevel = 1.0f;                  // Memory Man master Volume/Level (unity default)
    int mChorusVib = 0;                  // Memory Man Chorus(0)/Vibrato(1) switch
    int mSyncIndex = 0;

    float mGlideK = 0.01f;   // glide smoother coefficient (per model)
    float mSmoothK = 0.01f;  // 10 ms Mix/Level de-zip coefficient
    bool mPrepared = false;
}; // class PreDelayBlock

} // namespace nam_rig
