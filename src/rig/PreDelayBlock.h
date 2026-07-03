#pragma once
// PreDelayBlock — a MONO delay pedal that sits IN FRONT OF THE AMP, in the shared
// pre section (after the drive rack, before the A/B split — the classic pedalboard
// "delay into the amp" spot). Deliberately DISTINCT from the post-cab STEREO
// DelayBlock: this one is coloured by the amp downstream, is mono, and is voiced
// after four SPECIFIC real delay pedals rather than the tape/clean characters of
// the stereo unit. Same per-model-fit ethos as DriveBlock's pedal models.
//
// The four models (Robbie's pick), each grounded in how the real unit actually
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
//   1  MXR CARBON COPY  — dark ANALOG BBD (bucket-brigade + NE570-style companding).
//                         A BBD's bandwidth is its clock's Nyquist: t = N/(2·fclk),
//                         so bandwidth ≈ N/(4·t) — repeats band-limit and DARKEN as
//                         the delay lengthens. The Carbon Copy is famously dark: a
//                         LOW fixed reconstruction/anti-alias filter dominates at all
//                         times (~2.6 kHz), companding compresses the repeats, and
//                         two internal trimmers add a subtle chorus warble. Max 600 ms.
//
//   2  MEMORY MAN       — EHX Deluxe Memory Man: lush ANALOG BBD (MN3005). Documented
//                         voice: "high end heavily attenuated, LITTLE bass cut, a
//                         STRONG MID BOOST alongside the high cut." So: an in-loop MID
//                         bump + gentle low-cut + BBD bandwidth that goes very dark at
//                         its long settings (single 4096-stage line → Nyquist ~1.8 kHz
//                         at 550 ms), companding, and its signature DEEP chorus/vibrato
//                         modulation. Sings and washes where the Carbon Copy stays dry
//                         and dark. Max 550 ms.
//
//   3  KORG SDD-3000    — early DIGITAL rack delay (the U2/"present digital" sound).
//                         "12-bit-plus-one" (~13-bit) companded conversion → a gritty,
//                         organic digital texture; 17 kHz bandwidth (so BRIGHT, and
//                         fixed — a converter, not a clock-swept BBD); and a defining
//                         analog INPUT PREAMP that adds presence/sheen. Max 1023 ms.
//
// Signal per sample (mono):
//   dry = x
//   wet = line.read(delay + mod)           // fractional read, wow/chorus modulated
//   wet = in-loop low-cut (HP)             // controls bass build-up in the feedback
//   wet = in-loop bandwidth LP             // BBD: Nyquist(time) capped by anti-alias;
//                                          //   digital: fixed converter bandwidth
//   wet = in-loop mid bump                 // Memory Man only
//   wet = user Tone high-cut               // extra darkening on the repeats (optional)
//   wet = companding/preamp soft-clip      // cubic 2nd-order ADAA (BBD compander knee /
//                                          //   SDD 13-bit grit); bounds self-oscillation
//   line.write(dry + fb·wet)               // feedback (fbCeiling per model)
//   out = presence sheen (output-once)     // SDD preamp / digital top, not recirculated
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
    enum Model { kDD7 = 0, kCarbonCopy = 1, kMemoryMan = 2, kSDD3000 = 3, kNumModels = 4 };

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
        float loopHpHz;     // in-loop low-cut (bass build-up control); 0 = off
        float midHz, midDb, midQ; // in-loop mid bump (Memory Man); 0 dB = off
        float satDrive, satAsym;  // companding/preamp soft-clip (cubic ADAA, in-loop); 0 = clean
        float presHz, presDb;     // output-once presence sheen (digital top); 0 dB = off
        float modRateHz, modDepthMs; // built-in modulation; user Mod knob scales the depth
        float glideMs;      // time-change glide (BBD repitch swoop vs a quick digital move)
        float fbCeiling;    // feedback ceiling (analog can self-oscillate >1 — sat bounds it)
    };

    static Voicing voicingFor(Model m)
    {
        switch (m)
        {
        case kCarbonCopy:
            // Dark analog BBD. Two 4096-stage lines (~8192) for 600 ms; a LOW fixed
            // reconstruction filter (~2.6 kHz) keeps it dark at every setting (the
            // clock Nyquist only bites below it at the very longest times). Companding
            // + BBD headroom -> a gentle compressed knee (satDrive). Subtle chorus from
            // the internal width/rate trimmers.        stages  aa     hp    midHz midDb midQ  sat   asym  presHz presDb modHz modMs glide fbC
            return { true,  600.0f,  8192.0f, 2600.0f, 100.0f, 0.0f, 0.0f, 0.7f, 0.50f, 0.05f, 0.0f,  0.0f,  1.20f, 1.3f, 70.0f, 1.03f };
        case kMemoryMan:
            // Lush analog BBD (single 4096-stage MN3005 path -> Nyquist drops to
            // ~1.8 kHz at 550 ms, so it goes very dark long). Its documented voice is a
            // STRONG MID BOOST + high cut + little bass cut, so: an in-loop mid bump at
            // ~650 Hz, a gentle 80 Hz low-cut, a slightly higher anti-alias ceiling than
            // the Carbon Copy (brighter mids pop at short times). Deep signature chorus.
            //       bbd    maxMs    stages  aa       hp     midHz  midDb  midQ  sat    asym   presHz presDb modHz  modMs  glide  fbC
            return { true,  550.0f,  4096.0f, 3800.0f, 80.0f, 650.0f, 4.0f, 0.80f, 0.45f, 0.06f, 0.0f,  0.0f,  0.85f, 2.2f,  80.0f, 1.06f };
        case kSDD3000:
            // Bright early-digital. 17 kHz fixed bandwidth (a converter, not a swept
            // clock), 13-bit companding grit (gentle in-loop soft-clip), and the
            // defining analog preamp -> an output-once presence sheen at ~3.4 kHz.
            //       bbd    maxMs     stages aa        hp     midHz midDb midQ  sat    asym   presHz  presDb modHz modMs glide fbC
            return { false, 1023.0f, 0.0f,  16000.0f, 40.0f, 0.0f, 0.0f, 0.7f, 0.18f, 0.03f, 3400.0f, 2.5f, 0.60f, 1.3f, 30.0f, 1.02f };
        case kDD7:
        default:
            // Boss DD-7 — VERIFIED from the 2008 Roland service notes (docs/predelay/dd7.md):
            // 24-bit AK4552 codec + custom DSP, NO compander (dropped the DD-2/DD-3 NE570),
            // NO in-loop tone filter -> the standard digital repeats are FULL-BANDWIDTH and
            // do NOT degrade per pass (constant bandwidth, unlike a BBD). So: antiAlias ~19 kHz
            // (effectively full band; the recirculating 2-pole there is transparent in the
            // guitar band yet loses a hair per pass so max feedback can't run away), satDrive 0
            // (no companding/compression), fbCeiling 1.0 (F.BACK self-oscillates -> "Trick
            // Sound"). The Mod knob defaults 0 so the standard mode is clean; dialing it in
            // emulates the DD-7's separate Modulate mode (a small static chorus). Repitch off
            // (digital, quick time move). Input 1 MΩ / output 1 kΩ buffered stage in ioFor().
            // glide 45 ms: turning D.TIME re-clocks the buffer -> the trails PITCH-BEND
            // as they slew (the authentic digital-delay time-sweep), not an instant jump.
            // fbCeiling 1.05: F.BACK builds into a self-oscillating swell at the top
            // ("Trick Sound") — the loopLimit backstop keeps it bounded.
            //       bbd    maxMs     stages aa        hp    midHz midDb midQ  sat   asym  presHz presDb modHz modMs glide fbC
            return { false, 2000.0f, 0.0f,  19000.0f, 0.0f, 0.0f, 0.0f, 0.7f, 0.0f, 0.0f, 0.0f,  0.0f,  0.35f, 0.9f, 45.0f, 1.05f };
        }
    }

    const char *name() const override { return "Pre Delay"; }

    void prepare(const BlockContext &ctx) override
    {
        mFs = ctx.sampleRate;
        // Ring sized for TWICE the full Time range: REVERSE mode reads backward through
        // the buffer at 2x the write rate, so a T-length reverse grain spans 2T of line.
        const int maxDelay = (int)std::ceil((2.0f * kMaxTimeMs + 20.0f) * 0.001f * (float)mFs);
        mLine.prepare(maxDelay);
        mLfo.prepare(mFs);
        mLfo.setWaveform(Lfo::Sine);
        mSmoothK = 1.0f - std::exp((float)(-1.0 / (0.010 * mFs))); // 10 ms de-zip on mix
        updateGlide();
        mIo.prepare(mFs);
        applyIo();
        mAnalogLp = Biquad::lowpass(mFs, std::min(kAnalogLpHz, 0.45 * mFs), 0.5); // DD-7 Analog (DM-2) darkening
        rebuildFixed(true);
        reset();
        mPrepared = true;
    }

    void reset() override
    {
        mLine.reset();
        mLfo.reset();
        mIo.reset();
        mLoopHp.reset();
        mLoopLp.reset();
        mMid.reset();
        mTone.reset();
        mPres.reset();
        mAnalogLp.reset();
        mSatX1 = mSatX2 = 0.0;
        mDcX1 = mDcY1 = 0.0;
        mASatX1 = mASatX2 = mADcX1 = mADcY1 = 0.0;
        mRevPhase = 0.0;
        mBaseZ = (double)currentTimeMs();
        mMixZ = mMix;
    }

    // ---- parameters (audio thread) ----
    void setModel(int m)
    {
        const Model mm = (Model)std::min(std::max(m, 0), (int)kNumModels - 1);
        if (mm != mModel)
        {
            mModel = mm;
            mVoicing = voicingFor(mm);
            if (mPrepared) { updateGlide(); applyIo(); rebuildFixed(true); mLfo.setRateHz(mVoicing.modRateHz); }
        }
    }
    void setDd7Mode(int m) { mMode = std::clamp(m, 0, (int)kNumDd7Modes - 1); }
    void setTimeMs(float ms) { mTimeMs = std::clamp(ms, kMinTimeMs, kMaxTimeMs); }
    void setSyncIndex(int i) { mSyncIndex = std::clamp(i, 0, kNumSync - 1); }
    void setBpm(double bpm) { if (bpm > 1.0) mBpm = bpm; }
    void setFeedback(float f) { mFeedback = std::clamp(f, 0.0f, 1.0f); } // 0..1 knob
    void setMix(float m) { mMix = std::clamp(m, 0.0f, 1.0f); }
    void setMod(float m) { mModAmt = std::clamp(m, 0.0f, 1.0f); }        // scales the built-in mod depth
    void setToneHz(float hz)
    {
        if (hz != mToneHz) { mToneHz = hz; if (mPrepared) rebuildTone(); }
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

    void process(float *mono, int numSamples) override
    {
        mIo.processIn(mono, numSamples); // input buffer / coupling stage (colours dry + delay input)
        // DD-7 MODE (other models behave as a plain digital delay = kMode800):
        const int mode = (mModel == kDD7) ? mMode : (int)kMode800;
        const bool hold = (mode == kHold), reverse = (mode == kReverse);
        const bool analog = (mode == kAnalog), modulate = (mode == kModulate);
        mLfo.setRateHz(modulate ? kModulateRateHz : mVoicing.modRateHz);
        const float baseTarget = currentTimeMs();
        // Feedback: knob (0..1) scaled by the model ceiling; HOLD locks it to 1 (freeze).
        // Analog models push past unity (self-oscillation) — the in-loop companding
        // soft-clip + loopLimit bound the level so it never runs away.
        const float fb = hold ? 1.0f : (mFeedback * mVoicing.fbCeiling);
        const float modAmt = modulate ? kModulateDepth : mModAmt; // MODULATE = fixed chorus depth
        // BBD bandwidth tracks the clock: recompute the in-loop LP corner for this
        // block from the target time (cheap — once per block, not per sample). Digital
        // models keep their fixed converter bandwidth.
        updateBandwidth(baseTarget);
        const double fsK = 0.001 * mFs;

        for (int i = 0; i < numSamples; ++i)
        {
            mMixZ += mSmoothK * (mMix - mMixZ);
            // Glide the base delay toward the target (analog = slow pitch swoop; digital
            // = quick, click-free). Snap when within a hair to kill one-pole crawl.
            mBaseZ += (double)mGlideK * ((double)baseTarget - mBaseZ);
            if (std::abs((double)baseTarget - mBaseZ) < 1.0e-4) mBaseZ = baseTarget;

            const double lfo = (double)mLfo.value();
            const double modMs = (double)modAmt * (double)mVoicing.modDepthMs * lfo;
            const double tSamp = std::max(3.0, (mBaseZ + modMs) * fsK);

            const float dry = mono[i];
            float wet = reverse ? reverseRead(tSamp) : mLine.readFrac6(tSamp - 1.0); // REVERSE = grain playback

            // In-loop tone (recirculates -> compounds per repeat, the authentic
            // "repeats get darker each pass" of an analog delay):
            if (mLoopHpOn) wet = mLoopHp.processSample(wet); // low-cut
            if (mLoopLpOn) wet = mLoopLp.processSample(wet); // bandwidth (BBD Nyquist / converter)
            if (mMidOn)    wet = mMid.processSample(wet);    // Memory Man mid bump
            if (mToneOn)   wet = mTone.processSample(wet);   // user high-cut
            if (analog) { wet = mAnalogLp.processSample(wet); wet = analogSat(wet); } // DD-7 ANALOG = DM-2 dark + warm

            // Companding / preamp soft-clip (cubic 2nd-order ADAA, in-loop). Stands in
            // for the BBD compander's compression knee (models 1/2) and the SDD's
            // 13-bit companded grit (model 3). Bounds the feedback loop.
            if (mVoicing.satDrive > 0.0f) wet = loopSat(wet);

            // Record input into the line; HOLD mutes the input so the buffer freezes/loops.
            mLine.write(loopLimit(hold ? (fb * wet) : (dry + fb * wet)));

            // Output-once presence sheen (not recirculated -> shapes timbre without
            // compounding down the tail): the SDD/DD digital top-end lift.
            float outw = wet;
            if (mPresOn) outw = mPres.processSample(outw);

            // Authentic delay-pedal mix: the DRY stays at unity (on the DD-7 it is a
            // fixed ANALOG through-path that never hits the converter) and the WET is
            // ADDED on top, scaled by the mix/E.LEVEL knob — NOT a dry/wet crossfade.
            mono[i] = dry + mMixZ * outw;
            mLfo.advance();
        }
        mIo.processOut(mono, numSamples); // output buffer / coupling stage (per model)
        flushDenormals();
    }

    double latencySamples() const override { return 0.0; }

    // ---- verification hooks ----
    Model model() const { return mModel; }
    Voicing currentVoicing() const { return mVoicing; }
    float currentLoopLpHz() const { return mLoopLpHzBuilt; } // effective in-loop bandwidth corner

private:
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
    float loopSat(float x)
    {
        const double drive = (double)mVoicing.satDrive;
        const double asym  = (double)mVoicing.satAsym;
        const double xb = (double)x * drive;
        const double y = sat::cubicADAA2(xb, mSatX1, mSatX2);
        mSatX2 = mSatX1;
        mSatX1 = xb;
        double yb = (drive > 1.0e-9) ? y / drive : (double)x;
        if (asym != 0.0)
        {
            const double xc = (double)x > 1.0 ? 1.0 : ((double)x < -1.0 ? -1.0 : (double)x);
            const double in = yb + asym * (std::cosh(kEvenShape * xc) - 1.0);
            yb = in - mDcX1 + kDcBlockR * mDcY1; // one-pole DC blocker
            mDcX1 = in;
            mDcY1 = yb;
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
    float reverseRead(double tSamp)
    {
        const double T = std::max(64.0, tSamp);
        float out = 0.0f;
        for (int g = 0; g < 2; ++g)
        {
            double ph = mRevPhase + 0.5 * (double)g;
            ph -= std::floor(ph);
            const float s = mLine.readFrac6(std::max(2.0, 2.0 * ph * T)); // 2x sweep = true reverse
            const float win = 0.5f * (1.0f - std::cos(2.0 * 3.14159265358979323846 * ph));
            out += s * win;
        }
        mRevPhase += 1.0 / T;
        if (mRevPhase >= 1.0) mRevPhase -= 1.0;
        return out;
    }

    // DD-7 ANALOG mode warmth: a light asymmetric cubic soft-clip (its own ADAA state,
    // fixed drive) so the DM-2 model rounds/compresses the darkened repeats.
    float analogSat(float x)
    {
        const double drive = (double)kAnalogSatDrive, asym = (double)kAnalogSatAsym;
        const double xb = (double)x * drive;
        const double y = sat::cubicADAA2(xb, mASatX1, mASatX2);
        mASatX2 = mASatX1;
        mASatX1 = xb;
        double yb = y / drive;
        const double xc = (double)x > 1.0 ? 1.0 : ((double)x < -1.0 ? -1.0 : (double)x);
        const double in = yb + asym * (std::cosh(kEvenShape * xc) - 1.0);
        yb = in - mADcX1 + kDcBlockR * mADcY1;
        mADcX1 = in;
        mADcY1 = yb;
        const float out = (float)yb;
        return std::isfinite(out) ? out : 0.0f;
    }

    // Recompute the glide coefficient from the model's time-change feel.
    void updateGlide()
    {
        const double ms = std::max(1.0f, mVoicing.glideMs);
        mGlideK = 1.0f - (float)std::exp(-1.0 / (ms * 0.001 * mFs));
    }

    // Per-model INPUT+OUTPUT stage (impedance loading / coupling / buffer), reusing
    // IoStage.h like DriveBlock. Only the DD-7 is circuit-verified so far (2008 Roland
    // service notes, docs/predelay/dd7.md): 2SK880 JFET input buffer, 1 MΩ in = the DI
    // reference -> NO loading shelf; 10 µF couplings -> subsonic high-passes (~0.02 Hz
    // in, ~1.6 Hz out); 24-bit AK4552 codec -> no HF smoothing. So DD-7 = transparent
    // buffered (just the couplings). The BBD/other models load their source and colour
    // differently, but their I/O stages are NOT circuit-researched yet (one pedal at a
    // time) -> transparent until their schematics are verified. Never guessed.
    void applyIo()
    {
        switch (mModel)
        {
        case kDD7:
            mIo.setBuffered(2.0f, 1.6f, 0.0f, 0.0f); // subsonic in/out coupling HPs; else transparent
            break;
        default:
            mIo.setTransparent(); // pending per-pedal circuit research
            break;
        }
    }

    // Build the FIXED (time-independent) filters for the current model: low-cut, mid
    // bump, presence, and the digital bandwidth LP. The BBD bandwidth LP is (re)built
    // per block by updateBandwidth(); force=true also seeds it here.
    void rebuildFixed(bool force)
    {
        mLoopHpOn = mVoicing.loopHpHz > 0.0f;
        if (mLoopHpOn)
            mLoopHp = Biquad::highpass(mFs, std::min((double)mVoicing.loopHpHz, 0.45 * mFs), 0.5);

        mMidOn = mVoicing.midDb != 0.0f && mVoicing.midHz > 0.0f;
        if (mMidOn)
            mMid = Biquad::peaking(mFs, std::min((double)mVoicing.midHz, 0.45 * mFs),
                                   (double)mVoicing.midQ, (double)mVoicing.midDb);

        mPresOn = mVoicing.presDb != 0.0f && mVoicing.presHz > 0.0f;
        if (mPresOn)
            mPres = Biquad::peaking(mFs, std::min((double)mVoicing.presHz, 0.45 * mFs),
                                    0.7, (double)mVoicing.presDb);

        rebuildTone();

        if (force && !mVoicing.bbd)
        {
            // Digital: fixed converter bandwidth, build once.
            mLoopLpHzBuilt = std::min(mVoicing.antiAliasHz, (float)(0.45 * mFs));
            mLoopLp = Biquad::lowpass(mFs, mLoopLpHzBuilt, 0.5);
            mLoopLpOn = true;
        }
        else if (force && mVoicing.bbd)
        {
            mLoopLpHzBuilt = -1.0f; // force a rebuild on the next updateBandwidth()
            updateBandwidth(currentTimeMs());
        }
    }

    void rebuildTone()
    {
        mToneOn = mToneHz < 20000.0f;
        if (mToneOn)
            mTone = Biquad::lowpass(mFs, std::min((double)mToneHz, 0.45 * mFs));
    }

    // BBD bandwidth = the clock's Nyquist, which falls as the delay lengthens:
    //   fclk = stages/(2·tSec)  ->  Nyquist = stages/(4·tSec)
    // The effective in-loop corner is that Nyquist (with a little margin) capped by
    // the fixed reconstruction/anti-alias filter, and floored so it never collapses.
    // Digital models are fixed and skip this. Only rebuilds when the corner really
    // moves (>~3%), so a steady setting costs nothing.
    void updateBandwidth(float timeMs)
    {
        if (!mVoicing.bbd) return;
        const float tSec = std::max(0.005f, timeMs * 0.001f);
        const float nyq = 0.85f * mVoicing.bbdStages / (4.0f * tSec);
        const float corner = std::min(mVoicing.antiAliasHz, std::max(700.0f, nyq));
        if (mLoopLpHzBuilt < 0.0f || std::abs(corner - mLoopLpHzBuilt) > 0.03f * mLoopLpHzBuilt)
        {
            const float z1 = mLoopLp.z1, z2 = mLoopLp.z2; // keep state across a coefficient swap
            mLoopLp = Biquad::lowpass(mFs, std::min((double)corner, 0.45 * mFs), 0.5);
            mLoopLp.z1 = z1;
            mLoopLp.z2 = z2;
            mLoopLpHzBuilt = corner;
            mLoopLpOn = true;
        }
    }

    void flushDenormals()
    {
        for (Biquad *b : {&mLoopHp, &mLoopLp, &mMid, &mTone, &mPres})
        {
            if (std::abs(b->z1) < 1.0e-30f) b->z1 = 0.0f;
            if (std::abs(b->z2) < 1.0e-30f) b->z2 = 0.0f;
        }
        if (std::abs(mDcY1) < 1.0e-30) mDcY1 = 0.0;
    }

    double mFs = 48000.0, mBpm = 120.0;
    Model mModel = kDD7;
    Voicing mVoicing = voicingFor(kDD7);

    FracDelayLine mLine;
    Lfo mLfo;
    IoStage mIo; // per-model input+output stage (impedance/coupling/buffer)
    Biquad mLoopHp, mLoopLp, mMid, mTone, mPres, mAnalogLp;
    bool mLoopHpOn = false, mLoopLpOn = true, mMidOn = false, mToneOn = false, mPresOn = false;
    float mLoopLpHzBuilt = -1.0f; // last-built in-loop bandwidth corner (BBD tracks time)

    // companding/preamp soft-clip state (double: ADAA subtraction needs the precision)
    int mMode = kMode3200;               // DD-7 MODE rotary (default = widest range, full Time range)
    double mRevPhase = 0.0;              // Reverse-mode grain phase
    double mASatX1 = 0.0, mASatX2 = 0.0, mADcX1 = 0.0, mADcY1 = 0.0; // Analog-mode sat state
    double mSatX1 = 0.0, mSatX2 = 0.0;   // cubic ADAA history
    double mDcX1 = 0.0, mDcY1 = 0.0;     // even-harmonic DC blocker
    static constexpr double kDcBlockR = 0.9995; // ~3.8 Hz one-pole DC blocker
    static constexpr double kEvenShape = 2.0;   // cosh even-harmonic richness

    float mTimeMs = 350.0f, mFeedback = 0.35f, mMix = 0.28f, mModAmt = 0.25f, mToneHz = 20000.0f;
    int mSyncIndex = 0;

    double mBaseZ = 350.0;   // glided base delay (ms)
    float mGlideK = 0.01f;   // glide smoother coefficient (per model)
    float mMixZ = 0.28f, mSmoothK = 0.01f;
    bool mPrepared = false;
};

} // namespace nam_rig
