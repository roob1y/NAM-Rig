#pragma once
// ModBlock — a 3-slot stereo modulation SECTION (post-cab). Each slot is a
// self-contained ModVoice; the slots run in SERIES so you can stack effects for
// quirky textures (e.g. vibrato -> phaser -> tremolo). The whole section is
// presented to RigChain as ONE StereoBlock, so the chain is unchanged.
//
// DESIGN: each effect is voiced like the real thing, not a generic engine.
// Only the controls an effect actually has are exposed (the UI shows them per
// type); everything else is hardwired to that effect's sweet spot, so you can't
// dial a bad sound:
//   - LFO waveform is fixed per effect (Tremolo is the only one that picks a
//     Shape); BBD warmth is baked into the bucket-brigade types; Phaser's sweep
//     and Uni-Vibe's resonance are fixed; Depth is voiced to a musical maximum
//     per effect (tremolo/harm-trem never dive to dead silence, so the
//     modulation stays a scaled sine and never squares off).
// Zero latency. Verified by tests/mod_test.cpp.
//
// Mono-buffer rule (RigChain may pass left == right): when the pointers alias
// each voice processes the left channel only.

#include "Blocks.h"
#include "Lfo.h"
#include <array>
#include <algorithm>
#include <vector>

namespace nam_rig
{

// One modulation voice. Self-contained and independently testable; the section
// below chains several in series.
class ModVoice : public StereoBlock
{
public:
    enum Type
    {
        kChorus = 0, kFlanger, kPhaser, kTremolo,
        kVibrato, kRotary, kUniVibe, kHarmTrem, kBiPhase
    };

    // Voicing constants (fixed; knobs scale within these)
    static constexpr double kChorusBaseMs = 7.0, kChorusSpreadMs = 10.0;
    static constexpr float kChorusMaxRateHz = 3.5f; // keep chorus a chorus (faster -> vibrato/warble); tunable
    // M-126-style wide flanger: Manual sets a static base delay, Width sweeps above it.
    static constexpr double kFlMinMs = 0.5, kFlManualMaxMs = 8.0, kFlSweepMs = 6.0, kFlMaxMs = 14.0;
    static constexpr double kVibBaseMs = 2.0, kVibSpreadMs = 8.0;
    static constexpr double kRotBaseMs = 3.0, kRotSpreadMs = 4.0;
    static constexpr float kRotSlowHz = 0.72f, kRotFastHz = 6.6f; // chorale / tremolo
    // Leslie angle-dependent EQ (CCRMA / Smith-Lee model, DAFx-02): as the horn
    // rotates away from the mic the level drops AND highs roll off faster than
    // lows -- i.e. a high-shelf tilt whose HF gain tracks the horn-facing factor.
    // This is the "2D" angle behaviour a static body biquad can't produce; the
    // facing signal (cosH/face) is already computed in kRotary, so no new LFO.
    static constexpr float kRotHornTiltHz = 3000.0f; // angle-shelf corner
    static constexpr float kRotHornTiltDepth = 0.6f; // HF cut when horn faces fully away (0..1)
    // Fixed horn-throat presence shaping (angle-INDEPENDENT "H0" voicing), layered
    // on the ~250 Hz wooden-body resonance for a fuller measured-average tone.
    static constexpr double kRotThroatHz = 1600.0, kRotThroatQ = 0.9, kRotThroatDb = 2.5;
    // Leslie tube power-amp voicing. The amp colours the tone even clean (warmth
    // floor + output-transformer HF rolloff), and the Drive saturation breaks up
    // mid-first (pre/de-emphasis "honk") and is ADAA-anti-aliased in processSample.
    static constexpr float kAmpWarmth = 0.06f;   // always-on tube softening (0 = off)
    static constexpr double kAmpEmphHz = 900.0, kAmpEmphQ = 0.7, kAmpEmphDb = 4.0; // pre/de-emphasis
    static constexpr float kAmpToneHz = 5500.0f; // output-transformer rolloff corner
    static constexpr float kAmpToneHf = 0.62f;   // HF gain above the corner (~ -4 dB)
    static constexpr double kPhaserCenterHz = 650.0, kPhaserOctaves = 2.6; // opened-up sweep (~107 Hz..3.9 kHz); tunable by ear
    static constexpr double kUniCenterHz = 400.0;
    static constexpr double kHarmXoverHz = 800.0;
    static constexpr int kPhaserStages = 4;
    static constexpr int kBiPhaseStages = 6;                                   // Mu-Tron Bi-Phase: 6 stages per phasor
    static constexpr double kBiPhaseCenterHz = 500.0, kBiPhaseOctaves = 2.4;   // tunable by ear
    static constexpr float kBiPhaseFbMax = 0.65f;                              // musical resonance ceiling (no whistle)
    static constexpr float kBiPhaseDepthMin = 0.15f;                           // always some sweep (never dead-static)
    // One Bi-Phase phasor's TPT integrator states (6 stages), per channel.
    // Declared here (before phasor6's signature uses it).
    struct PhasorCore { float s[kBiPhaseStages] = {0.0f}; };
    static constexpr double kRightLfoOffset = 0.25; // 90 degrees at Width = 1
    static constexpr float kPhaserFixedDepth = 1.0f; // phaser sweep is hardwired
    static constexpr float kPhaserFbMax = 0.7f;      // musical resonance ceiling (no whistle)
    static constexpr float kUniFeedback = 0.3f;      // uni-vibe resonance is hardwired (positive fb)
    // Photocell warp (replaces the old pow-skew): the incandescent lamp heats
    // faster than it cools (asymmetric one-pole), and the LDR maps light to stage
    // frequency through a power law -- the authentic lopsided Uni-Vibe throb.
    static constexpr float kUniLampHeatMs = 8.0f;    // filament heating time constant
    static constexpr float kUniLampCoolMs = 55.0f;   // filament cooling (slower -> lopsided)
    static constexpr float kUniCellGamma = 1.5f;     // photoconductive transfer: fc ~ light^gamma
    static constexpr float kUniAmDepth = 0.10f;      // subtle photocell amplitude ripple
    static constexpr float kUniDepthMin = 0.10f;     // always some throb (never dead-static)
    // Tremolo gain-shape LUT + DC-offset compensation. The LUT waveshapes the
    // bipolar LFO modulator through a (default-linear) ODD-symmetric taper, so it
    // stays zero-mean -> it can never add a DC component to the gain (no thump),
    // and kTremTaper > 1 sharpens / < 1 rounds the throb shoulders by ear.
    // kTremCenter is the DC-offset compensation: 0 = cut-only gain in [1-depth,1]
    // (the original voicing; mean drops with depth, the section ModLevelLock takes
    // up the slack) ... 1 = mean-preserving (gain mean = 1 at any depth, troughs
    // still chop to 0, peaks boost). Default 0 keeps the loved sound; tune by ear.
    static constexpr int kTremLutN = 1024;
    static constexpr float kTremTaper = 1.0f;
    static constexpr float kTremCenter = 0.0f;

    // ---- per-effect voicing (public so the tests + UI can read it) ----
    static int authenticWave(Type t) { return t == kFlanger ? Lfo::Triangle : Lfo::Sine; }
    static float depthMax(Type)
    {
        // Every effect now reaches full depth. Tremolo chops to full silence
        // (de-click smoothing stops the square/S&H clicks); harm-trem chops fully
        // too now that the LR4 crossover recombines phase-coherent and flat, so
        // the complementary AM is a clean spectral pan instead of a lumpy split.
        return 1.0f;
    }
    static float bakedBbd(Type t)
    {
        switch (t)
        {
        case kChorus: return 0.40f;
        case kFlanger: return 0.30f;
        case kVibrato: return 0.35f;
        case kRotary: return 0.45f;
        default: return 0.0f;
        }
    }
    // Wet/dry policy: only Chorus exposes a Mix knob. The rest are voiced to
    // their authentic blend (comb/notch effects need 50/50; vibrato/rotary/
    // tremolo are full-wet), so the Mix knob is hidden + hardcoded for them.
    static float mixFor(Type t, float knobMix)
    {
        switch (t)
        {
        case kChorus: return knobMix;
        case kFlanger: return knobMix * 0.5f; // knob spans dry..50/50 (deepest flange) -- can't thin past the sweet spot
        case kPhaser:
        case kBiPhase:
        case kUniVibe: return 0.5f;
        default: return 1.0f; // vibrato / rotary / tremolo / harm-trem = full wet
        }
    }
    static bool mixExposed(Type t) { return t == kChorus || t == kFlanger; }

    // Uni-Vibe photocell sweep warp (the authentic lopsided lamp motion): an
    // asymmetric thermal one-pole on the lamp drive (heats fast / cools slow)
    // feeding the LDR power-law transfer. Advances the per-channel lamp state.
    // Public so the test can drive it directly (the coeffs are set in prepare()).
    // Returns the warped sweep control in [-1, 1].
    // Leslie angle-dependent horn-tilt HF gain (single source of truth, public so
    // T27 can check the law directly). face in [0,1] (1 = horn facing the mic),
    // depth scales it so the angle colour only acts when the rotor is working.
    // Returns 1 at face=1 (flat/bright); 1 - kRotHornTiltDepth at face=0,depth=1.
    static float hornTiltGain(float face, float depth)
    {
        return 1.0f - kRotHornTiltDepth * depth * (1.0f - face);
    }

    // Rotary horn/drum balance gain (single source of truth, public so T29 can
    // check the law). k in [0,1]: 0.5 = both rotors at unity (neutral, the level
    // you like); toward 1 fades the drum out (1 = horn solo); toward 0 fades the
    // horn out (0 = drum solo). horn=true returns the horn gain, else the drum.
    static float rotorBalance(float k, bool horn)
    {
        return std::min(1.0f, 2.0f * (horn ? k : 1.0f - k));
    }

    // Leslie tube-amp saturation CURVE (memoryless; the processSample path applies
    // it via first-order ADAA to anti-alias). Public so T28 can check the curve.
    // drive in [0,1]; the warmth floor keeps a faint tube softening even clean.
    static float ampShape(float x, float drive)
    {
        const float dEff = std::max(drive, kAmpWarmth);
        const float pre = 1.0f + dEff * 5.0f;
        const float bias = 0.15f * dEff;
        return (std::tanh(x * pre + bias) - std::tanh(bias)) * (2.6f / pre);
    }

    // Tremolo gain law (single source of truth, public so the test verifies the
    // DC-offset compensation directly). mod = shaped bipolar modulator in [-1,1];
    // center 0 = cut-only (gain in [1-depth, 1], original voicing), center 1 =
    // DC-compensated (mean gain = 1 at any depth, trough still reaches 0 = full
    // chop). At depth 1 both ends meet (gain = 1 - mod). The DC term carries the
    // only mean offset; an odd-symmetric mod keeps everything else zero-mean.
    static float tremGain(float depth, float mod, float center)
    {
        const float dc = 0.5f * (1.0f - center);            // mean-offset term
        return 1.0f - depth * (dc + mod * (0.5f + 0.5f * center));
    }

    float uniVibeWarp(int ch, float lfo)
    {
        const float drive = 0.5f + 0.5f * lfo; // LFO -> lamp drive [0,1]
        float &lamp = mUniLamp[(size_t)ch];
        lamp += (drive > lamp ? mUniHeatCoef : mUniCoolCoef) * (drive - lamp);
        const float cell = std::pow(std::max(0.0f, lamp), kUniCellGamma); // LDR transfer
        return cell * 2.0f - 1.0f;                                        // warped control [-1,1]
    }

    const char *name() const override { return "Modulation"; }

    void prepare(const BlockContext &ctx) override
    {
        mFs = ctx.sampleRate;
        // Leslie body voicing (angle-INDEPENDENT "H0"): the wooden-cabinet
        // resonance (~250 Hz peaking) PLUS a horn-throat presence bump (~1.6 kHz),
        // two RBJ peaking biquads in series -> closer to the measured average tone.
        rbjPeaking(250.0, 1.2, 4.0, mFs, mCabB0, mCabB1, mCabB2, mCabA1, mCabA2);
        rbjPeaking(kRotThroatHz, kRotThroatQ, kRotThroatDb, mFs,
                   mThB0, mThB1, mThB2, mThA1, mThA2);
        mHornTiltCoef = coefForHz(kRotHornTiltHz, mFs); // angle-shelf split corner
        // Tube-amp pre/de-emphasis: boost the mids INTO the clipper then cut them
        // back after, so the midrange breaks up first (the amp "honk"); the +/-
        // pair nulls to ~flat when undriven. Plus the transformer HF-rolloff corner.
        rbjPeaking(kAmpEmphHz, kAmpEmphQ, kAmpEmphDb, mFs, mApB0, mApB1, mApB2, mApA1, mApA2);
        rbjPeaking(kAmpEmphHz, kAmpEmphQ, -kAmpEmphDb, mFs, mAqB0, mAqB1, mAqB2, mAqA1, mAqA2);
        mAmpToneCoef = coefForHz(kAmpToneHz, mFs);
        // Harmonic-tremolo Linkwitz-Riley 4th-order crossover: a Butterworth
        // (Q=1/sqrt2) LP and HP at kHarmXoverHz, each cascaded twice in process.
        rbjLowpass(kHarmXoverHz, 0.70710678, mFs, mHtLpB0, mHtLpB1, mHtLpB2, mHtLpA1, mHtLpA2);
        rbjHighpass(kHarmXoverHz, 0.70710678, mFs, mHtHpB0, mHtHpB1, mHtHpB2, mHtHpA1, mHtHpA2);
        // Tremolo gain-shape LUT: an ODD-symmetric taper of the bipolar modulator
        // (-1..1). Linear at kTremTaper=1 (transparent: a table read replaces the
        // curve eval and the table is exactly zero-mean, so it adds no DC).
        for (int i = 0; i < kTremLutN; ++i)
        {
            const double u = -1.0 + 2.0 * (double)i / (double)(kTremLutN - 1);
            mTremLut[(size_t)i] =
                (kTremTaper == 1.0f)
                    ? (float)u
                    : (float)(u < 0.0 ? -std::pow(-u, (double)kTremTaper)
                                      : std::pow(u, (double)kTremTaper));
        }
        const int maxDelay =
            (int)std::ceil((kChorusBaseMs + kChorusSpreadMs + 2.0) * 0.001 * mFs);
        for (auto &d : mLine)
            d.prepare(maxDelay);
        mLfo.prepare(mFs);
        mLfo2.prepare(mFs); // Bi-Phase Sweep Gen 2
        mSmoothK = 1.0f - std::exp((float)(-1.0 / (0.010 * mFs))); // 10 ms
        // Uni-Vibe lamp filament: fixed heat/cool one-poles (rate-independent), so
        // the sweep gets more lopsided as the LFO speeds up -- like the real lamp.
        mUniHeatCoef = coefForMs(kUniLampHeatMs, mFs);
        mUniCoolCoef = coefForMs(kUniLampCoolMs, mFs);
        reset();
        mPrepared = true;
    }

    void reset() override
    {
        for (auto &d : mLine)
            d.reset();
        for (auto &ap : mAllpass)
            ap = {};
        for (auto &c : mBiA)
            c = {};
        for (auto &c : mBiB)
            c = {};
        mFbState[0] = mFbState[1] = 0.0f;
        mFbLp[0] = mFbLp[1] = 0.0f;
        mXoverLp[0] = mXoverLp[1] = 0.0f;
        mHtLp1Z1[0] = mHtLp1Z1[1] = mHtLp1Z2[0] = mHtLp1Z2[1] = 0.0f;
        mHtLp2Z1[0] = mHtLp2Z1[1] = mHtLp2Z2[0] = mHtLp2Z2[1] = 0.0f;
        mHtHp1Z1[0] = mHtHp1Z1[1] = mHtHp1Z2[0] = mHtHp1Z2[1] = 0.0f;
        mHtHp2Z1[0] = mHtHp2Z1[1] = mHtHp2Z2[0] = mHtHp2Z2[1] = 0.0f;
        mBbdLp[0] = mBbdLp[1] = 0.0f;
        mUniLamp[0] = mUniLamp[1] = 0.5f; // lamp at mid brightness (no startup snap)
        mTremG[0] = mTremG[1] = 1.0f; // unity gain (no mute on start)
        mVibApX[0] = mVibApX[1] = 0.0f;
        mVibApY[0] = mVibApY[1] = 0.0f;
        mHornLp[0] = mHornLp[1] = 0.0f;
        mDrumLp[0] = mDrumLp[1] = 0.0f;
        mCabZ1[0] = mCabZ1[1] = 0.0f;
        mCabZ2[0] = mCabZ2[1] = 0.0f;
        mThZ1[0] = mThZ1[1] = 0.0f;
        mThZ2[0] = mThZ2[1] = 0.0f;
        mHornTiltLp[0] = mHornTiltLp[1] = 0.0f;
        mApZ1[0] = mApZ1[1] = mApZ2[0] = mApZ2[1] = 0.0f;
        mAqZ1[0] = mAqZ1[1] = mAqZ2[0] = mAqZ2[1] = 0.0f;
        mAmpX1[0] = mAmpX1[1] = 0.0f;
        mAmpF1[0] = mAmpF1[1] = 0.0f;
        mAmpToneLp[0] = mAmpToneLp[1] = 0.0f;
        mRotHornPhase = 0.0;
        mRotDrumPhase = 0.0;
        mRotHornSpeed = kRotSlowHz;
        mRotDrumSpeed = kRotSlowHz * 0.78f;
        mLfo.reset();
        mLfo2.reset();
        mDepthZ = mDepth;
        mMixZ = mMix;
        mManualZ = mManual;
        mHornDrumZ = mHornDrum;
        mSeriesZ = mSeries ? 1.0f : 0.0f;
    }

    // ---- parameters (audio thread) ----
    void setType(int t)
    {
        const Type type = (Type)t;
        if (type != mType)
        {
            mType = type;
            if (mPrepared)
                reset(); // state from another algorithm is meaningless
        }
    }
    void setRateHz(float hz) { mFreeRateHz = hz; } // free rate (Hz), used when not synced
    void setSyncIndex(int i) { mSyncIndex = i; }   // 0 = Off (free); see syncBeats()
    void setBpm(double bpm) { if (bpm > 0.0) mBpm = bpm; }
    void setWaveform(int w) { mUserWave = w; }     // only Tremolo uses it; others hardwire
    void setDepth(float d) { mDepth = d; }
    void setFeedback(float f) { mFeedback = f; }   // Flanger/Phaser only
    void setMix(float m) { mMix = m; }
    void setManual(float m) { mManual = m; }       // Flanger: static comb position (M-126 Manual)
    void setInvert(bool inv) { mInvert = inv; }    // Flanger: phase-invert the wet/regen path
    void setP2Ratio(float r) { mP2Ratio = r; }     // Bi-Phase: Sweep Gen 2 rate as a ratio of Gen 1
    void setSeries(bool s) { mSeries = s; }        // Bi-Phase: series (true) vs parallel (false) routing
    void setWidth(float w) { mWidth = w; }         // 0 = mono spread, 1 = 90 deg
    void setDrive(float d) { mRotDrive = d; }      // Rotary: Leslie tube-amp drive
    void setRotFast(bool f) { mRotFast = f; }      // Rotary: slow (chorale) / fast (tremolo)
    void setHornDrum(float b) { mHornDrum = b; }   // Rotary: horn<->drum balance (0.5 = both full)

    // LFO period in beats for each modSync choice (index 0 = Off = free rate).
    static constexpr int kNumSync = 10;
    static double syncBeats(int i)
    {
        static const double beats[kNumSync] = {0.0, 4.0, 2.0, 1.0, 1.5, 2.0 / 3.0,
                                               0.5, 0.75, 1.0 / 3.0, 0.25};
        return (i > 0 && i < kNumSync) ? beats[i] : 0.0;
    }
    // Free-rate ceiling per effect. The M-126 flanger sweeps up to 20 Hz (fast,
    // metallic); chorus stays slow so it never tips into vibrato/warble; other
    // effects stay musical at <=10 Hz. Sync ignores this (it honours the host
    // division).
    static float maxRateHz(Type t)
    {
        if (t == kFlanger) return 20.0f;
        if (t == kChorus) return kChorusMaxRateHz;
        return 10.0f;
    }
    float effectiveRateHz() const
    {
        const double beats = syncBeats(mSyncIndex);
        if (beats > 0.0)
            return (float)((mBpm / 60.0) / beats); // synced: honour the division
        return std::min(mFreeRateHz, maxRateHz(mType)); // free: cap per effect
    }

    void process(float *left, float *right, int numSamples) override
    {
        const bool stereo = (left != right);
        const Type ty = mType;
        const double rate = (double)effectiveRateHz();
        mLfo.setRateHz((float)rate);
        mLfo.setWaveform(ty == kTremolo ? mUserWave : authenticWave(ty)); // shape hardwired per type
        // Bi-Phase Sweep Gen 2 = Gen 1 x ratio, but clamped to the same musical
        // ceiling so the detune can't push one core into the buzzy zone.
        mLfo2.setRateHz((float)std::min((double)maxRateHz(ty), rate * (double)mP2Ratio));
        mLfo2.setWaveform(Lfo::Sine);
        const float dMax = depthMax(ty);
        mBakedBbd = bakedBbd(ty);
        mBbdCoef = coefForHz(12000.0 - 10000.0 * (double)mBakedBbd, mFs);
        mXoverCoef = coefForHz(kHarmXoverHz, mFs);
        mTremCoef = coefForMs(1.5f, mFs);   // de-click smoothing for tremolo gain
        mFbLpCoef = coefForHz(6500.0, mFs); // flanger feedback tone-shaping
        // Leslie: two fixed speeds (the Rate knob picks slow vs fast at its
        // midpoint), each rotor ramped with inertia -- horn light/quick, drum
        // heavy/slow -- which is the characteristic spin-up.
        const float hornTarget = mRotFast ? kRotFastHz : kRotSlowHz;
        const float drumTarget = hornTarget * 0.78f;
        mRotHornSpeed += (1.0f - std::exp(-(float)numSamples / (0.5f * (float)mFs))) * (hornTarget - mRotHornSpeed);
        mRotDrumSpeed += (1.0f - std::exp(-(float)numSamples / (1.8f * (float)mFs))) * (drumTarget - mRotDrumSpeed);
        mHornInc = (double)mRotHornSpeed / mFs;
        mDrumInc = (double)mRotDrumSpeed / mFs;
        mHornLpCoef = coefForHz(5000.0, mFs); // horn driver: rolls off the extreme top
        mDrumLpCoef = coefForHz(70.0, mFs);   // bass rotor: rolls off the deep lows
        double rOff = (double)mWidth * kRightLfoOffset;
        // Hard-edged tremolo shapes (Square / S&H) read the LFO IN PHASE across L/R
        // (no quadrature offset, which would make a chop lurch); their stereo Width
        // is an amplitude AUTO-PAN applied per-channel in processSample instead.
        // Sine/triangle keep the quadrature phase offset (the smooth throb).
        if (ty == kTremolo && (mUserWave == Lfo::Square || mUserWave == Lfo::SampleHold))
            rOff = 0.0;
        const float mixTarget = mixFor(ty, mMix); // only Chorus uses the knob

        for (int i = 0; i < numSamples; ++i)
        {
            mDepthZ += mSmoothK * (mDepth - mDepthZ);
            mMixZ += mSmoothK * (mixTarget - mMixZ);
            mManualZ += mSmoothK * (mManual - mManualZ); // de-zipper the Manual knob
            mHornDrumZ += mSmoothK * (mHornDrum - mHornDrumZ); // de-zipper Horn/Drum balance
            mSeriesZ += mSmoothK * ((mSeries ? 1.0f : 0.0f) - mSeriesZ); // de-click the routing toggle
            const float depth = mDepthZ * dMax; // voiced musical maximum

            // pass the LFO phase OFFSET (not value) so multi-tap effects can
            // read several phases of the same accumulator this sample.
            left[i] = processSample(0, left[i], 0.0, depth);
            if (stereo)
                right[i] = processSample(1, right[i], rOff, depth);

            mLfo.advance();
            mLfo2.advance(); // Bi-Phase Sweep Gen 2
            mRotHornPhase += mHornInc;
            if (mRotHornPhase >= 1.0) mRotHornPhase -= 1.0;
            mRotDrumPhase += mDrumInc;
            if (mRotDrumPhase >= 1.0) mRotDrumPhase -= 1.0;
        }
        flushDenormals();
    }

private:
    float processSample(int ch, float x, double off, float depth)
    {
        const float lfo = mLfo.value(off);
        switch (mType)
        {
        case kChorus:
        {
            // 3 voices read the same line at offset LFO phases -> lush + wide
            mLine[(size_t)ch].write(x);
            float wet = 0.0f;
            for (int v = 0; v < 3; ++v)
            {
                const float lv = mLfo.value(off + (double)v * 0.3333);
                const double sweepMs =
                    kChorusBaseMs + (double)depth * kChorusSpreadMs * (0.5 + 0.5 * (double)lv);
                wet += mLine[(size_t)ch].readFrac(sweepMs * 0.001 * mFs);
            }
            wet = bbdColor(ch, wet * 0.45f);
            return (1.0f - mMixZ) * x + mMixZ * wet;
        }
        case kFlanger:
        {
            // M-126-style: Manual sets the static comb position, Width (depth)
            // sweeps a wide range above it, and Invert phase-flips the delayed
            // path -- which moves the comb (notch<->peak) AND flips the regen
            // polarity, exactly like the hardware's single phase-invert switch.
            // BBD voicing (HF rolloff + soft clip) is baked in via bbdColor().
            const double manualBaseMs =
                kFlMinMs + (double)mManualZ * (kFlManualMaxMs - kFlMinMs);
            const double sweepMs = (double)depth * kFlSweepMs * (0.5 + 0.5 * (double)lfo);
            const double delayMs =
                std::min(kFlMaxMs, std::max(kFlMinMs, manualBaseMs + sweepMs));
            mLine[(size_t)ch].write(x + mFeedback * mFbState[(size_t)ch]);
            float wet = mLine[(size_t)ch].readFrac(std::max(2.0, delayMs * 0.001 * mFs));
            wet = bbdColor(ch, wet);
            if (mInvert)
                wet = -wet; // phase-invert: flips comb polarity + regen together
            float &flp = mFbLp[(size_t)ch];
            flp += mFbLpCoef * (wet - flp); // tone-shape the regen (tames fizz)
            mFbState[(size_t)ch] = flp;
            return (1.0f - mMixZ) * x + mMixZ * wet;
        }
        case kPhaser:
        {
            // 4-stage phaser, rebuilt as ZDF/TPT (topology-preserving) first-order
            // all-passes with the global feedback resolved with ZERO delay -- no
            // unit-sample lag in the regen path, so the resonance is placed
            // accurately and the sweep stays clean even as fc moves fast. All four
            // stages share one swept centre. Each TPT all-pass is affine in its
            // input: ap_i = alpha*in_i + beta_i, with alpha = 2G-1 (depends only on
            // fc) and beta_i = 2(1-G)*s_i (depends only on the stored state). The
            // chain therefore collapses to y_chain = A*u + B and the loop solves
            // analytically. Feedback is NEGATIVE in the loop (authentic Phase-90
            // notch structure): u = (x - k*B)/(1 + k*A); denom > 0 for all k >= 0,
            // so the loop is unconditionally stable and the full range is usable.
            const double fc = std::min(
                0.45 * mFs,
                kPhaserCenterHz * std::pow(2.0, (double)lfo * kPhaserOctaves * (double)kPhaserFixedDepth));
            const double g = std::tan(3.14159265358979323846 * fc / mFs);
            const float G = (float)(g / (1.0 + g));
            const float alpha = 2.0f * G - 1.0f; // per-stage instantaneous gain, |alpha| < 1

            // Per-channel state block (ch0 -> states 0..3, ch1 -> 4..7): fully decoupled.
            ApState *ap = &mAllpass[(size_t)(ch * kPhaserStages)];

            // Gather each stage's state contribution and build B via Horner:
            // B = alpha^3*b0 + alpha^2*b1 + alpha*b2 + b3.
            float beta[kPhaserStages];
            float B = 0.0f;
            for (int i = 0; i < kPhaserStages; ++i)
            {
                beta[i] = 2.0f * (1.0f - G) * ap[i].s;
                B = alpha * B + beta[i];
            }
            const float A = alpha * alpha * alpha * alpha; // alpha^4

            const float k = mFeedback * kPhaserFbMax;     // knob maps to the musical resonance window
            const float u = (x - k * B) / (1.0f + k * A); // zero-delay resolved chain input

            // Single evaluation pass: true output per stage + TPT integrator update.
            float in = u;
            for (int i = 0; i < kPhaserStages; ++i)
            {
                const float out = alpha * in + beta[i]; // = ap_i (reuses precomputed beta_i)
                const float v = (in - ap[i].s) * G;
                ap[i].s += 2.0f * v;                     // s_new = s + 2v (trapezoidal update)
                in = out;
            }
            return (1.0f - mMixZ) * x + mMixZ * in;
        }
        case kTremolo:
        {
            // Stereo width, per shape: sine/triangle use the LFO phase offset (the
            // quadrature throb -- already baked into `lfo` via `off`). Square/S&H
            // read the LFO IN PHASE (off forced to 0 in process()) and instead get
            // an amplitude AUTO-PAN: the R channel crossfades to anti-phase as Width
            // rises (0 = mono, 1 = full L/R ping-pong), so a chop pans cleanly
            // instead of lurching.
            float wMod = lfo;
            if ((mUserWave == Lfo::Square || mUserWave == Lfo::SampleHold) && ch == 1)
                wMod = lfo * (1.0f - 2.0f * mWidth);
            // LUT-waveshape the modulator (odd-symmetric -> adds no DC) then apply
            // the DC-offset-compensated gain law. At the default taper=1/center=0
            // this is bit-identical to the original 1 - depth*(0.5+0.5*lfo).
            const float target = tremGain(depth, tremLut(wMod), kTremCenter);
            float &gn = mTremG[(size_t)ch];
            gn += mTremCoef * (target - gn); // smoothing de-clicks square/S&H edges
            return (1.0f - mMixZ) * x + mMixZ * (x * gn);
        }
        case kVibrato:
        {
            // Single-tap time-varying delay with FIRST-ORDER ALL-PASS fractional
            // interpolation. An all-pass has UNITY magnitude at every frequency, so
            // the swept delay imparts pure pitch modulation with no amplitude ripple
            // -- the polynomial (Hermite) interpolator's small, fractional-position-
            // dependent HF gain loss is what made the old vibrato shimmer as it
            // swept. The fractional delay is biased into [0.5, 1.5) so |c| <= 1/3:
            // well inside stability and the lowest-transient region for when the
            // integer tap steps mid-sweep (the known weak spot of allpass interp).
            const double sweepMs =
                kVibBaseMs + (double)depth * kVibSpreadMs * (0.5 + 0.5 * (double)lfo);
            const double d = std::max(2.0, sweepMs * 0.001 * mFs);
            const int di = (int)(d - 0.5);                 // integer tap (frac lands in [0.5,1.5))
            const float frac = (float)(d - (double)di);    // fractional delay realised by the allpass
            const float c = (1.0f - frac) / (1.0f + frac); // allpass coefficient for that delay
            mLine[(size_t)ch].write(x);
            const float xr = mLine[(size_t)ch].readInt(di); // integer-delayed sample
            float &apx = mVibApX[(size_t)ch];               // previous integer-read input
            float &apy = mVibApY[(size_t)ch];               // previous allpass output
            float wet = c * xr + apx - c * apy;             // y = c*x[n] + x[n-1] - c*y[n-1]
            apx = xr;
            apy = wet;
            wet = bbdColor(ch, wet);
            return (1.0f - mMixZ) * x + mMixZ * wet;
        }
        case kRotary:
        {
            // 2-rotor Leslie. Per rotor, rotation angle θ drives the AMPLITUDE by
            // cos(θ) (loudest when it faces the mic) and the DOPPLER pitch by
            // sin(θ) (fastest a quarter-turn earlier) — 90° apart, which is what
            // makes it rotate instead of just throb. L/R use ~opposite phase
            // (double the Width offset) so the sound throws across the field.
            const double rot = off * 2.0; // ~opposite L/R for the stereo throw

            // --- tube power amp: an ALWAYS-ON voice (mid-focused breakup + output-
            // transformer HF rolloff) layered with the Drive saturation. The
            // makeup ~2.6/pre (not 1/pre, which collapsed the level) keeps the
            // pushed level up; the warmth floor keeps a faint tube colour even
            // clean. The saturation is ADAA anti-aliased so its harmonics don't
            // fold back as digital fizz. ---
            const float dEff = std::max(mRotDrive, kAmpWarmth);
            // pre-emphasis: boost the mids INTO the clipper (they break up first)
            float pe;
            {
                float &z1 = mApZ1[(size_t)ch], &z2 = mApZ2[(size_t)ch];
                pe = mApB0 * x + z1;
                z1 = mApB1 * x - mApA1 * pe + z2;
                z2 = mApB2 * x - mApA2 * pe;
            }
            // ADAA(tanh): F(u) = ln(cosh(pre*u+bias))/pre - tanh(bias)*u is the
            // antiderivative of the shaper; y = (F(pe)-F(pe1))/(pe-pe1) (first-order
            // antiderivative anti-aliasing), falling back to the direct curve when
            // the step is tiny.
            const float pre = 1.0f + dEff * 5.0f;
            const float bias = 0.15f * dEff;
            const float tb = std::tanh(bias);
            float &pe1 = mAmpX1[(size_t)ch];
            float &F1 = mAmpF1[(size_t)ch];
            const float Fc = std::log(std::cosh(pe * pre + bias)) / pre - tb * pe;
            const float dpe = pe - pe1;
            const float shaped = (std::abs(dpe) > 1.0e-4f)
                                     ? (Fc - F1) / dpe
                                     : (std::tanh(pe * pre + bias) - tb);
            pe1 = pe;
            F1 = Fc;
            const float sat = shaped * (2.6f / pre); // makeup: compress + thicken
            // de-emphasis: cut the mids back out (nulls the pre when undriven)
            float de;
            {
                float &z1 = mAqZ1[(size_t)ch], &z2 = mAqZ2[(size_t)ch];
                de = mAqB0 * sat + z1;
                z1 = mAqB1 * sat - mAqA1 * de + z2;
                z2 = mAqB2 * sat - mAqA2 * de;
            }
            // Drive blends the amped signal over the clean input (always >= warmth)
            const float amped = x + dEff * (de - x);
            // output-transformer bandwidth: gentle always-on HF rolloff (shelf)
            float &tl = mAmpToneLp[(size_t)ch];
            tl += mAmpToneCoef * (amped - tl);
            const float xd = tl + kAmpToneHf * (amped - tl);

            // --- 800 Hz crossover ---
            float &xo = mXoverLp[(size_t)ch];
            xo += mXoverCoef * (xd - xo);
            const float lowB = xo, highB = xd - xo;

            // --- treble horn: roll off the extreme top, doppler, directional wom ---
            float &hlp = mHornLp[(size_t)ch];
            hlp += mHornLpCoef * (highB - hlp);
            const float cosH = std::cos(6.2831853f * (float)(mRotHornPhase + rot));
            const double hornMs = 3.0 - (0.2 + 0.35 * (double)depth) * (double)cosH;
            mLine[(size_t)ch].write(hlp);
            float horn = mLine[(size_t)ch].readFrac(std::max(2.0, hornMs * 0.001 * mFs));
            const float face = 0.5f + 0.5f * cosH; // 0..1, = 1 facing the mic
            const float dir = face * face;         // peaked -> the "wom" pulse
            horn *= 1.0f - 0.80f * depth * (1.0f - dir);

            // angle-dependent tone (CCRMA): highs roll off as the horn turns away.
            // One-pole split (low band = tlp), then re-add the high band scaled by
            // hornTiltGain(face,depth) -> a high-shelf that brightens facing the
            // mic and darkens pointing away. gHf in [1-kRotHornTiltDepth, 1] so the
            // high band is only ever attenuated -> always bounded.
            float &tlp = mHornTiltLp[(size_t)ch];
            tlp += mHornTiltCoef * (horn - tlp);
            horn = tlp + hornTiltGain(face, depth) * (horn - tlp);

            // --- bass drum: high-pass the deep lows, slow amplitude throb ---
            float &dlp = mDrumLp[(size_t)ch];
            dlp += mDrumLpCoef * (lowB - dlp);
            const float drumIn = lowB - dlp; // remove subsonics the woofer can't throw
            const float cosD = std::cos(6.2831853f * (float)(mRotDrumPhase + rot));
            const float drum = drumIn * (1.0f - 0.45f * depth * (0.5f - 0.5f * cosD));

            // --- horn/drum balance: neutral at 0.5 (both unity, bit-exact), fades
            // the opposite rotor out toward each extreme (horn solo / drum solo) ---
            float wet = rotorBalance(mHornDrumZ, true) * horn
                      + rotorBalance(mHornDrumZ, false) * drum;

            // --- cabinet body voicing (angle-independent "H0"): wooden-body
            // resonance (~250 Hz) then horn-throat presence (~1.6 kHz), two RBJ
            // peaking biquads in series (TDF2) ---
            {
                float &z1 = mCabZ1[(size_t)ch], &z2 = mCabZ2[(size_t)ch];
                const float y = mCabB0 * wet + z1;
                z1 = mCabB1 * wet - mCabA1 * y + z2;
                z2 = mCabB2 * wet - mCabA2 * y;
                wet = y;
            }
            {
                float &z1 = mThZ1[(size_t)ch], &z2 = mThZ2[(size_t)ch];
                const float y = mThB0 * wet + z1;
                z1 = mThB1 * wet - mThA1 * y + z2;
                z2 = mThB2 * wet - mThA2 * y;
                wet = y;
            }
            return (1.0f - mMixZ) * x + mMixZ * wet;
        }
        case kUniVibe:
        {
            // 4-stage opto phaser, rebuilt on the same ZDF/TPT all-pass solver as
            // the Bi-Phase phasor6 but with PER-STAGE alpha_i for the staggered
            // photocell centres ({0.5,1,1.8,3.2}x400 Hz). Each TPT all-pass is
            // affine in its input (ap_i = alpha_i*in + beta_i, alpha_i = 2G_i-1,
            // beta_i = 2(1-G_i)*s_i), so the cascade collapses to y = A*u + B with
            //   A = prod(alpha_i)          (vs the phaser's alpha^4)
            //   B = nested products         (B <- alpha_i*B + beta_i, stage order)
            // and the positive feedback loop resolves zero-delay (no mFbState lag):
            //   u = (x + fb*B)/(1 - fb*A);  denom >= 0.7 for fb 0.3 -> always stable.
            //
            // The sweep is the AUTHENTIC photocell warp, not a static curve bend:
            // the lamp filament heats faster than it cools (asymmetric one-pole)
            // so the bright/dark dwell is lopsided -- and more so the faster you
            // sweep, exactly like the hardware -- and the LDR maps light to stage
            // frequency through a power law (fc ~ light^gamma).
            static const double mult[kPhaserStages] = {0.5, 1.0, 1.8, 3.2};

            // lamp + photocell: asymmetric thermal envelope -> power-law transfer
            const float w = uniVibeWarp(ch, lfo);    // warped sweep control [-1,1]
            const float cell = 0.5f * (w + 1.0f);    // back to [0,1] for the AM term
            // depth floored so the knob-down position still breathes (no dead-static)
            const float uDepth = kUniDepthMin + depth * (1.0f - kUniDepthMin);

            // per-stage ZDF/TPT all-pass: gather alpha_i/G_i/beta_i, build A and B
            ApState *ap = &mAllpass[(size_t)(ch * kPhaserStages)];
            float alpha[kPhaserStages], G[kPhaserStages], beta[kPhaserStages];
            float A = 1.0f, B = 0.0f;
            for (int s = 0; s < kPhaserStages; ++s)
            {
                const double fc = std::min(
                    0.45 * mFs,
                    std::max(20.0, mult[s] * kUniCenterHz * std::pow(2.0, (double)w * (double)uDepth)));
                const double g = std::tan(3.14159265358979323846 * fc / mFs);
                G[s] = (float)(g / (1.0 + g));
                alpha[s] = 2.0f * G[s] - 1.0f;
                beta[s] = 2.0f * (1.0f - G[s]) * ap[s].s;
                A *= alpha[s];              // A = product of alpha_i
                B = alpha[s] * B + beta[s]; // B = nested products (forward accumulation)
            }

            const float fb = kUniFeedback;                  // hardwired resonance
            const float u = (x + fb * B) / (1.0f - fb * A); // positive feedback, zero-delay

            // single pass: true stage outputs + TPT integrator updates
            float in = u;
            for (int s = 0; s < kPhaserStages; ++s)
            {
                const float out = alpha[s] * in + beta[s];
                const float v = (in - ap[s].s) * G[s];
                ap[s].s += 2.0f * v; // s_new = s + 2v (trapezoidal update)
                in = out;
            }
            in *= 1.0f - kUniAmDepth * uDepth * cell; // subtle photocell amplitude ripple
            return (1.0f - mMixZ) * x + mMixZ * in;
        }
        case kHarmTrem:
        {
            // Linkwitz-Riley 4th-order crossover at kHarmXoverHz: two cascaded
            // Butterworth (Q=1/sqrt2) sections per band = 24 dB/oct with a
            // phase-coherent LP+HP sum (flat magnitude), so the two bands are
            // cleanly separated AND recombine without colouring the dry tone.
            // Complementary LFO AM: the two band gains sum to a constant, so this
            // is a spectral PAN (bass<->treble) rather than a volume tremolo --
            // which is why it now chops to full depth without a level dip.
            const size_t c = (size_t)ch;
            float lo = biquadTDF2(x, mHtLpB0, mHtLpB1, mHtLpB2, mHtLpA1, mHtLpA2,
                                  mHtLp1Z1[c], mHtLp1Z2[c]);
            lo = biquadTDF2(lo, mHtLpB0, mHtLpB1, mHtLpB2, mHtLpA1, mHtLpA2,
                            mHtLp2Z1[c], mHtLp2Z2[c]);
            float hi = biquadTDF2(x, mHtHpB0, mHtHpB1, mHtHpB2, mHtHpA1, mHtHpA2,
                                  mHtHp1Z1[c], mHtHp1Z2[c]);
            hi = biquadTDF2(hi, mHtHpB0, mHtHpB1, mHtHpB2, mHtHpA1, mHtHpA2,
                            mHtHp2Z1[c], mHtHp2Z2[c]);
            const float g = 0.5f + 0.5f * lfo;
            const float wet = lo * (1.0f - depth * g) + hi * (1.0f - depth * (1.0f - g));
            return (1.0f - mMixZ) * x + mMixZ * wet;
        }
        case kBiPhase:
        {
            // Mu-Tron Bi-Phase: two 6-stage ZDF phasors. Core A is swept by Sweep
            // Gen 1 (mLfo), Core B by Sweep Gen 2 (mLfo2 = Gen1 x P2Ratio) -- two
            // detuned LFOs give the complex, non-repeating motion. Series/parallel
            // is a smoothed crossfade (mSeriesZ) applied to BOTH Core B's input and
            // the output combine, so toggling the routing never steps the states ->
            // no click. In parallel the two cores pan opposite (A/B stereo split)
            // by Width; mono at Width 0. Shared Feedback, 50/50 mix (notch effect).
            const float fb = mFeedback * kBiPhaseFbMax; // knob maps to the musical resonance window
            // Depth knob remapped to [min..1] so the sweep never sits dead-static.
            const float bpDepth = kBiPhaseDepthMin + depth * (1.0f - kBiPhaseDepthMin);
            const float a = phasor6(mBiA[(size_t)ch], x, mLfo.value(off), bpDepth, fb);
            const float bIn = (1.0f - mSeriesZ) * x + mSeriesZ * a; // parallel(x) -> series(A(x))
            const float b = phasor6(mBiB[(size_t)ch], bIn, mLfo2.value(off), bpDepth, fb);
            const float w = mWidth;
            const float panA = (ch == 0) ? 0.5f + 0.5f * w : 0.5f - 0.5f * w;
            const float panB = (ch == 0) ? 0.5f - 0.5f * w : 0.5f + 0.5f * w;
            const float parallelWet = panA * a + panB * b;
            const float wet = (1.0f - mSeriesZ) * parallelWet + mSeriesZ * b;
            return (1.0f - mMixZ) * x + mMixZ * wet;
        }
        }
        return x;
    }

    // One 6-stage ZDF/TPT phasor with zero-delay-resolved negative feedback (the
    // Bi-Phase building block). beta[0] is the INPUT-side stage, so the forward
    // Horner accumulation weights it by alpha^(N-1) -- matching the cascade, where
    // stage 0's output passes through every later stage. fc is clamped on BOTH
    // sides before tan() so a deep sweep can never cross Nyquist and NaN out.
    float phasor6(PhasorCore &c, float x, float lfoVal, float depth, float fb)
    {
        double fc = kBiPhaseCenterHz *
                    std::pow(2.0, (double)lfoVal * kBiPhaseOctaves * (double)depth);
        fc = std::min(0.45 * mFs, std::max(20.0, fc));
        const double g = std::tan(3.14159265358979323846 * fc / mFs);
        const float G = (float)(g / (1.0 + g));
        const float alpha = 2.0f * G - 1.0f;

        float beta[kBiPhaseStages];
        float B = 0.0f;
        for (int i = 0; i < kBiPhaseStages; ++i)
        {
            beta[i] = 2.0f * (1.0f - G) * c.s[i];
            B = alpha * B + beta[i]; // forward Horner: beta[0] ends up x alpha^(N-1)
        }
        const float a2 = alpha * alpha;
        const float A = a2 * a2 * a2; // alpha^6

        const float u = (x - fb * B) / (1.0f + fb * A); // negative feedback (Phase-90 notch), denom > 0
        float in = u;
        for (int i = 0; i < kBiPhaseStages; ++i)
        {
            const float out = alpha * in + beta[i];
            const float v = (in - c.s[i]) * G;
            c.s[i] += 2.0f * v; // TPT integrator update
            in = out;
        }
        return in;
    }

    // Baked bucket-brigade colour for the delay-based types (chorus/flanger/
    // vibrato/rotary): HF rolloff + gentle clip. No-op when bakedBbd == 0.
    float bbdColor(int ch, float wet)
    {
        if (mBakedBbd <= 0.0f)
            return wet;
        float &z = mBbdLp[(size_t)ch];
        z += mBbdCoef * (wet - z);
        const float soft = std::tanh(z * (1.0f + mBakedBbd * 0.5f));
        return wet + mBakedBbd * (soft - wet);
    }

    // Linear-interpolated read of the tremolo gain-shape LUT for a bipolar
    // modulator in [-1, 1]. Exact (== w) when the table is the identity taper.
    float tremLut(float w) const
    {
        float p = 0.5f * (w + 1.0f) * (float)(kTremLutN - 1);
        if (p < 0.0f) p = 0.0f;
        int i = (int)p;
        if (i > kTremLutN - 2) i = kTremLutN - 2;
        const float f = p - (float)i;
        return mTremLut[(size_t)i] + f * (mTremLut[(size_t)i + 1] - mTremLut[(size_t)i]);
    }

    static float coefForHz(double hz, double fs)
    {
        const double fc = std::min(std::max(hz, 1.0), 0.45 * fs);
        return (float)(1.0 - std::exp(-2.0 * 3.14159265358979323846 * fc / fs));
    }
    static float coefForMs(float ms, double fs)
    {
        return 1.0f - (float)std::exp(-1.0 / ((double)std::max(0.05f, ms) * 0.001 * fs));
    }
    // RBJ peaking-EQ biquad coefficients, normalised so a0 = 1 (TDF2-ready).
    static void rbjPeaking(double fc, double Q, double gainDb, double fs,
                           float &b0, float &b1, float &b2, float &a1, float &a2)
    {
        const double A = std::pow(10.0, gainDb / 40.0);
        const double w0 = 2.0 * 3.14159265358979323846 * fc / fs;
        const double cw = std::cos(w0), sw = std::sin(w0), al = sw / (2.0 * Q);
        const double a0 = 1.0 + al / A;
        b0 = (float)((1.0 + al * A) / a0);
        b1 = (float)((-2.0 * cw) / a0);
        b2 = (float)((1.0 - al * A) / a0);
        a1 = (float)((-2.0 * cw) / a0);
        a2 = (float)((1.0 - al / A) / a0);
    }
    // RBJ low-pass biquad, a0-normalised (TDF2-ready). Q = 1/sqrt(2) gives a
    // 2nd-order Butterworth section -> two cascaded = a Linkwitz-Riley 4th-order.
    static void rbjLowpass(double fc, double Q, double fs,
                           float &b0, float &b1, float &b2, float &a1, float &a2)
    {
        const double w0 = 2.0 * 3.14159265358979323846 * fc / fs;
        const double cw = std::cos(w0), sw = std::sin(w0), al = sw / (2.0 * Q);
        const double a0 = 1.0 + al;
        b0 = (float)(((1.0 - cw) * 0.5) / a0);
        b1 = (float)((1.0 - cw) / a0);
        b2 = b0;
        a1 = (float)((-2.0 * cw) / a0);
        a2 = (float)((1.0 - al) / a0);
    }
    // RBJ high-pass biquad, a0-normalised (TDF2-ready). Butterworth at Q=1/sqrt(2).
    static void rbjHighpass(double fc, double Q, double fs,
                            float &b0, float &b1, float &b2, float &a1, float &a2)
    {
        const double w0 = 2.0 * 3.14159265358979323846 * fc / fs;
        const double cw = std::cos(w0), sw = std::sin(w0), al = sw / (2.0 * Q);
        const double a0 = 1.0 + al;
        b0 = (float)(((1.0 + cw) * 0.5) / a0);
        b1 = (float)(-(1.0 + cw) / a0);
        b2 = b0;
        a1 = (float)((-2.0 * cw) / a0);
        a2 = (float)((1.0 - al) / a0);
    }
    // Transposed-Direct-Form-II biquad step (a0-normalised coeffs). Advances the
    // two state words in place. Shared by the harm-trem LR4 crossover sections.
    static float biquadTDF2(float x, float b0, float b1, float b2, float a1, float a2,
                            float &z1, float &z2)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void flushDenormals()
    {
        for (auto &st : mAllpass)
            if (std::abs(st.s) < 1.0e-30f) st.s = 0.0f;
        for (auto *bank : {&mBiA, &mBiB})
            for (auto &c : *bank)
                for (auto &v : c.s)
                    if (std::abs(v) < 1.0e-30f) v = 0.0f;
        for (auto &f : mFbState)
            if (std::abs(f) < 1.0e-30f) f = 0.0f;
        for (auto &f : mXoverLp)
            if (std::abs(f) < 1.0e-30f) f = 0.0f;
        for (auto &f : mBbdLp)
            if (std::abs(f) < 1.0e-30f) f = 0.0f;
        for (auto &f : mVibApY) // recursive allpass state
            if (std::abs(f) < 1.0e-30f) f = 0.0f;
        for (auto &f : mFbLp)
            if (std::abs(f) < 1.0e-30f) f = 0.0f;
        for (auto &f : mHornLp)
            if (std::abs(f) < 1.0e-30f) f = 0.0f;
        for (auto &f : mDrumLp)
            if (std::abs(f) < 1.0e-30f) f = 0.0f;
        for (auto &f : mCabZ1)
            if (std::abs(f) < 1.0e-30f) f = 0.0f;
        for (auto &f : mCabZ2)
            if (std::abs(f) < 1.0e-30f) f = 0.0f;
        for (auto &f : mThZ1)
            if (std::abs(f) < 1.0e-30f) f = 0.0f;
        for (auto &f : mThZ2)
            if (std::abs(f) < 1.0e-30f) f = 0.0f;
        for (auto &f : mHornTiltLp)
            if (std::abs(f) < 1.0e-30f) f = 0.0f;
        for (auto *bank : {&mApZ1, &mApZ2, &mAqZ1, &mAqZ2, &mAmpToneLp,
                           &mHtLp1Z1, &mHtLp1Z2, &mHtLp2Z1, &mHtLp2Z2,
                           &mHtHp1Z1, &mHtHp1Z2, &mHtHp2Z1, &mHtHp2Z2})
            for (auto &f : *bank)
                if (std::abs(f) < 1.0e-30f) f = 0.0f;
    }

    // s = TPT integrator state, shared by the ZDF Phaser and the Uni-Vibe
    // all-passes (Direct-Form x1/y1 retired when Uni-Vibe moved to ZDF/TPT).
    struct ApState { float s = 0.0f; };

    double mFs = 48000.0;
    Type mType = kChorus;
    Lfo mLfo;
    Lfo mLfo2; // Bi-Phase Sweep Generator 2
    std::array<PhasorCore, 2> mBiA{}, mBiB{}; // Bi-Phase cores A/B, per channel
    std::array<FracDelayLine, 2> mLine;
    std::array<ApState, 2 * kPhaserStages> mAllpass{};
    std::array<float, 2> mFbState{};
    std::array<float, 2> mFbLp{};            // flanger feedback tone state
    std::array<float, 2> mXoverLp{};         // rotary 800 Hz crossover state (one-pole)
    // Harmonic-tremolo LR4 crossover: two cascaded Butterworth biquads per band.
    // Coeffs are fixed (corner = kHarmXoverHz), computed in prepare().
    float mHtLpB0 = 1.0f, mHtLpB1 = 0.0f, mHtLpB2 = 0.0f, mHtLpA1 = 0.0f, mHtLpA2 = 0.0f;
    float mHtHpB0 = 1.0f, mHtHpB1 = 0.0f, mHtHpB2 = 0.0f, mHtHpA1 = 0.0f, mHtHpA2 = 0.0f;
    std::array<float, 2> mHtLp1Z1{}, mHtLp1Z2{}, mHtLp2Z1{}, mHtLp2Z2{}; // LP cascade state
    std::array<float, 2> mHtHp1Z1{}, mHtHp1Z2{}, mHtHp2Z1{}, mHtHp2Z2{}; // HP cascade state
    std::array<float, 2> mBbdLp{};           // BBD HF-rolloff state
    std::array<float, 2> mUniLamp{};         // uni-vibe lamp thermal envelope (per ch)
    std::array<float, 2> mTremG{1.0f, 1.0f}; // smoothed tremolo gain (de-click)
    std::array<float, 2> mVibApX{};          // vibrato allpass: prev integer-read input
    std::array<float, 2> mVibApY{};          // vibrato allpass: prev output (recursive)
    std::array<float, kTremLutN> mTremLut{}; // tremolo gain-shape LUT (built in prepare)
    float mDepth = 0.5f, mFeedback = 0.0f, mMix = 0.5f;
    float mDepthZ = 0.5f, mMixZ = 0.5f, mSmoothK = 0.01f;
    float mManual = 0.3f, mManualZ = 0.3f; // Flanger Manual (static comb position)
    float mHornDrum = 0.5f, mHornDrumZ = 0.5f; // Rotary horn/drum balance (0.5 = neutral)
    bool mInvert = false;                  // Flanger phase-invert switch
    float mP2Ratio = 1.5f;                 // Bi-Phase Sweep Gen 2 rate ratio
    bool mSeries = false;                  // Bi-Phase series (true) / parallel (false)
    float mSeriesZ = 0.0f;                 // smoothed routing crossfade (de-click)
    float mWidth = 1.0f;
    int mUserWave = 0;                          // Tremolo's Shape choice
    float mBakedBbd = 0.0f;                     // per-block, from bakedBbd(type)
    float mBbdCoef = 1.0f, mXoverCoef = 0.1f;   // per-block filter coeffs
    float mTremCoef = 1.0f, mFbLpCoef = 1.0f;   // tremolo de-click, flanger fb tone
    float mUniHeatCoef = 1.0f, mUniCoolCoef = 1.0f; // uni-vibe lamp heat/cool one-poles
    double mRotHornPhase = 0.0, mRotDrumPhase = 0.0;                     // Leslie rotor phases
    double mHornInc = 0.0, mDrumInc = 0.0;                              // per-sample increments
    float mRotHornSpeed = kRotSlowHz, mRotDrumSpeed = kRotSlowHz * 0.78f; // ramping Hz (inertia)
    float mRotDrive = 0.0f;            // Leslie tube-amp drive (0..1)
    bool mRotFast = false;             // slow (chorale) / fast (tremolo)
    std::array<float, 2> mHornLp{};    // horn top-end rolloff state
    std::array<float, 2> mDrumLp{};    // drum deep-low rolloff state
    std::array<float, 2> mCabZ1{}, mCabZ2{};                 // cabinet biquad state
    float mHornLpCoef = 1.0f, mDrumLpCoef = 0.1f;            // per-block driver coeffs
    float mCabB0 = 1.0f, mCabB1 = 0.0f, mCabB2 = 0.0f, mCabA1 = 0.0f, mCabA2 = 0.0f;
    float mThB0 = 1.0f, mThB1 = 0.0f, mThB2 = 0.0f, mThA1 = 0.0f, mThA2 = 0.0f; // horn-throat EQ
    std::array<float, 2> mThZ1{}, mThZ2{};                   // throat biquad state
    std::array<float, 2> mHornTiltLp{};                      // angle-shelf one-pole state
    float mHornTiltCoef = 1.0f;                              // angle-shelf split corner coef
    // Tube-amp stages: pre-emphasis biquad, de-emphasis biquad, ADAA saturation
    // state (prev pre-emphasised input + its antiderivative), transformer shelf.
    float mApB0 = 1.0f, mApB1 = 0.0f, mApB2 = 0.0f, mApA1 = 0.0f, mApA2 = 0.0f; // amp pre-emphasis
    float mAqB0 = 1.0f, mAqB1 = 0.0f, mAqB2 = 0.0f, mAqA1 = 0.0f, mAqA2 = 0.0f; // amp de-emphasis
    std::array<float, 2> mApZ1{}, mApZ2{}, mAqZ1{}, mAqZ2{}; // emphasis biquad state
    std::array<float, 2> mAmpX1{}, mAmpF1{};                 // ADAA: prev input + prev antiderivative
    std::array<float, 2> mAmpToneLp{};                       // transformer-rolloff shelf state
    float mAmpToneCoef = 1.0f;                               // transformer-rolloff corner coef
    float mFreeRateHz = 1.0f;
    double mBpm = 120.0;
    int mSyncIndex = 0;
    bool mPrepared = false;
};

// Section loudness-lock. Keeps the modulation section's output at the same
// perceived level as its input no matter which effect/parameter changed -- the
// fix for "tweaking a knob changes the volume". It tracks slow RMS-power
// envelopes of the section IN vs OUT and applies a makeup gain = sqrt(in/out).
//
// Two properties make it well-behaved:
//   * It's a RATIO (out/in), so it ignores the player's dynamics -- a louder
//     strum raises in and out together and the gain doesn't budge. It only
//     removes the steady level OFFSET the effects introduce.
//   * The envelopes are slow (kTauMs, well above any musical LFO period), so
//     tremolo/rotary/harm-trem amplitude modulation passes through untouched --
//     only the long-term level is locked, never the pulse.
// Clamped to +-6 dB and gated at a silence floor so it can't run away or
// amplify the noise floor / effect tails. JUCE-free + unit-testable.
struct ModLevelLock
{
    static constexpr float kTauMs = 700.0f;  // envelope TC (>> musical AM periods)
    static constexpr float kMinGain = 0.5f;  // -6 dB makeup floor
    static constexpr float kMaxGain = 2.0f;  // +6 dB makeup ceiling
    static constexpr float kFloor = 1.0e-6f; // input-power gate (below -> hold unity)

    static float onePoleCoef(float ms, double fs) // one-pole smoothing coef for a TC in ms
    {
        return 1.0f - (float)std::exp(-1.0 / ((double)std::max(0.05f, ms) * 0.001 * fs));
    }
    void prepare(double fs)
    {
        mEnvCoef = onePoleCoef(kTauMs, fs);
        mGainCoef = onePoleCoef(15.0f, fs); // de-zipper the applied gain
        reset();
    }
    void reset()
    {
        mInPow = mOutPow = 0.0f;
        mGainZ = 1.0f;
    }
    void setEnabled(bool e) { mEnabled = e; }
    bool enabled() const { return mEnabled; }
    float gain() const { return mGainZ; }

    // Advance the input-power envelope over the section's dry input (call before
    // the effects run). Slow TC means the one-block lead over applyOutput is
    // negligible, so no input copy is needed.
    void observeInput(const float *l, const float *r, int n)
    {
        if (!mEnabled) return;
        const bool stereo = (l != r);
        for (int i = 0; i < n; ++i)
        {
            float p = l[i] * l[i];
            if (stereo) p += r[i] * r[i];
            mInPow += mEnvCoef * (p - mInPow);
        }
        if (mInPow < 1.0e-30f) mInPow = 0.0f;
    }
    // Advance the output-power envelope over the processed buffer and apply the
    // smoothed makeup gain in place.
    void applyOutput(float *l, float *r, int n)
    {
        if (!mEnabled) return;
        const bool stereo = (l != r);
        for (int i = 0; i < n; ++i)
        {
            float p = l[i] * l[i];
            if (stereo) p += r[i] * r[i];
            mOutPow += mEnvCoef * (p - mOutPow);
            float target = 1.0f;
            if (mInPow > kFloor && mOutPow > kFloor)
            {
                target = std::sqrt(mInPow / mOutPow);
                target = std::min(kMaxGain, std::max(kMinGain, target));
            }
            mGainZ += mGainCoef * (target - mGainZ);
            l[i] *= mGainZ;
            if (stereo) r[i] *= mGainZ;
        }
        if (mOutPow < 1.0e-30f) mOutPow = 0.0f;
    }

    float mEnvCoef = 0.0f, mGainCoef = 0.0f;
    float mInPow = 0.0f, mOutPow = 0.0f, mGainZ = 1.0f;
    bool mEnabled = true;
};

// The modulation SECTION: kSlots voices chained in series. Each slot has its
// own type/rate/sync/wet-dry mix/width and its own bypass. Presented to
// RigChain as a single StereoBlock (RigChain is unchanged). A section-level
// ModLevelLock keeps the output level steady across any effect/param change.
class ModBlock : public StereoBlock
{
public:
    static constexpr int kSlots = 3;

    ModBlock() { mPost.setBypassed(true); } // post effect off until explicitly enabled

    const char *name() const override { return "Modulation"; }

    void prepare(const BlockContext &ctx) override
    {
        for (auto &v : mVoice)
            v.prepare(ctx);
        mPost.prepare(ctx); // end-of-section post effect (e.g. rotary "speaker")
        mLock.prepare(ctx.sampleRate);
        mSmoothK = 1.0f - std::exp((float)(-1.0 / (0.010 * ctx.sampleRate))); // 10 ms blend de-zip
        const size_t cap = (size_t)std::max(1, ctx.maxBlockSize);
        mDryL.assign(cap, 0.0f);
        mDryR.assign(cap, 0.0f);
        for (int s = 0; s < kSlots; ++s)
        {
            mBranchL[(size_t)s].assign(cap, 0.0f);
            mBranchR[(size_t)s].assign(cap, 0.0f);
        }
        reset();
    }
    void reset() override
    {
        for (auto &v : mVoice)
            v.reset();
        mPost.reset();
        mLock.reset();
        for (auto &w : mWz) w = 1.0f / (float)kSlots;
        mModMixZ = mModMix;
    }

    void process(float *left, float *right, int numSamples) override
    {
        bool anyFront = false;
        for (int s = 0; s < kSlots; ++s)
            if (slotAudible(s)) { anyFront = true; break; }
        // Solo is a dial-in tool: it mutes the post effect too, so you hear the
        // soloed front slot in isolation.
        const bool postOn = !mPost.isBypassed() && !anySolo();
        if (!anyFront && !postOn) return; // section idle -> fully transparent (no level-lock)

        mLock.observeInput(left, right, numSamples); // dry section input (intact at entry)
        if (anyFront)
        {
            if (mParallel)
                processParallel(left, right, numSamples);
            else
                for (int pos = 0; pos < kSlots; ++pos) // SERIES: slots chained in place, in chain order
                {
                    const int s = mOrder[pos];
                    if (slotAudible(s))
                        mVoice[(size_t)s].process(left, right, numSamples);
                }
        }
        if (postOn) // POST: end-of-section effect runs on the combined output
            mPost.process(left, right, numSamples);
        mLock.applyOutput(left, right, numSamples);   // lock to the input level
    }

    // Section loudness-lock (default on). Disable for raw effect levels or for
    // bit-exact series tests.
    void setLevelLock(bool on) { mLock.setEnabled(on); }
    bool levelLock() const { return mLock.enabled(); }

    // Section routing: false = SERIES (slots chained), true = PARALLEL (each slot
    // runs on the dry input into its own branch, blended by the pad, then ONE
    // global Mod Mix vs dry so dry is counted once). NOTE: distinct from the
    // per-slot Bi-Phase series/parallel (setSeries) -- this is the whole section.
    void setParallel(bool p) { mParallel = p; }
    bool parallel() const { return mParallel; }
    void setPad(float x, float y) { mPadX = x; mPadY = y; } // parallel blend puck (0..1 each)
    void setModMix(float m) { mModMix = m; }                // parallel: mod-bus vs dry (1 = full bus)

    // SERIES chain order: mOrder[pos] = the slot processed at that position.
    // a/b/c must be a permutation of 0..kSlots-1; a malformed order (duplicate or
    // out-of-range slot) is rejected and the identity {0,1,2} is kept, so a bad
    // value can never drop or double a slot. Default identity = bit-exact to the
    // old fixed-order series loop. Ignored in parallel (branches are summed).
    void setChainOrder(int a, int b, int c)
    {
        const int in[kSlots] = {a, b, c};
        bool seen[kSlots] = {false, false, false};
        for (int k = 0; k < kSlots; ++k)
        {
            if (in[k] < 0 || in[k] >= kSlots || seen[in[k]])
                return; // not a permutation -> keep existing order
            seen[in[k]] = true;
        }
        for (int k = 0; k < kSlots; ++k) mOrder[k] = in[k];
    }
    int chainOrder(int pos) const { return mOrder[idx(pos)]; }

    // Cartesian blend pad -> kSlots normalised slot weights. Barycentric over a
    // triangle (slot0 top-centre, slot1 bottom-left, slot2 bottom-right): inside
    // the triangle the weights sum to 1 so the blend is inherently level-
    // consistent; outside, the puck clamps to the nearest edge. The section Level
    // Lock then holds the absolute loudness as the puck moves (pad picks the
    // blend, lock holds the level).
    static void padWeights(float x, float y, float w[kSlots])
    {
        w[0] = y;                   // top-centre node weight = height
        w[1] = 1.0f - x - 0.5f * y; // bottom-left
        w[2] = x - 0.5f * y;        // bottom-right
        float sum = 0.0f;
        for (int k = 0; k < kSlots; ++k) { if (w[k] < 0.0f) w[k] = 0.0f; sum += w[k]; }
        if (sum > 1.0e-12f)
            for (int k = 0; k < kSlots; ++k) w[k] /= sum;
        else
            for (int k = 0; k < kSlots; ++k) w[k] = 1.0f / (float)kSlots;
    }

    double latencySamples() const override
    {
        double s = 0.0;
        for (auto &v : mVoice)
            s += v.latencySamples();
        return s + mPost.latencySamples();
    }

    // ---- per-slot controls (slot 0..kSlots-1) ----
    void setType(int s, int t) { mVoice[idx(s)].setType(t); }
    void setWaveform(int s, int w) { mVoice[idx(s)].setWaveform(w); }
    void setSyncIndex(int s, int i) { mVoice[idx(s)].setSyncIndex(i); }
    void setRateHz(int s, float hz) { mVoice[idx(s)].setRateHz(hz); }
    void setDepth(int s, float d) { mVoice[idx(s)].setDepth(d); }
    void setFeedback(int s, float f) { mVoice[idx(s)].setFeedback(f); }
    void setMix(int s, float m) { mVoice[idx(s)].setMix(m); }
    void setWidth(int s, float w) { mVoice[idx(s)].setWidth(w); }
    void setDrive(int s, float d) { mVoice[idx(s)].setDrive(d); }
    void setRotFast(int s, bool f) { mVoice[idx(s)].setRotFast(f); }
    void setHornDrum(int s, float b) { mVoice[idx(s)].setHornDrum(b); }
    void setManual(int s, float m) { mVoice[idx(s)].setManual(m); }
    void setInvert(int s, bool inv) { mVoice[idx(s)].setInvert(inv); }
    void setP2Ratio(int s, float r) { mVoice[idx(s)].setP2Ratio(r); }
    void setSeries(int s, bool ser) { mVoice[idx(s)].setSeries(ser); }
    void setSlotBypassed(int s, bool b) { mVoice[idx(s)].setBypassed(b); }
    // Momentary solo (dial-in): if ANY slot is soloed, only soloed slots are
    // audible (solo overrides bypass); otherwise the normal per-slot bypass
    // applies. Works in both Series and Parallel.
    void setSlotSolo(int s, bool on) { mSolo[idx(s)] = on; }

    // ---- POST block: a dedicated end-of-section effect (rotary / tremolo /
    // harm-trem "speaker/amp" stage) that runs on the combined output of the
    // three front slots, in both Series and Parallel, inside the Level Lock. ----
    void setPostType(int t) { mPost.setType(t); }
    void setPostWaveform(int w) { mPost.setWaveform(w); }
    void setPostSyncIndex(int i) { mPost.setSyncIndex(i); }
    void setPostRateHz(float hz) { mPost.setRateHz(hz); }
    void setPostDepth(float d) { mPost.setDepth(d); }
    void setPostFeedback(float f) { mPost.setFeedback(f); }
    void setPostMix(float m) { mPost.setMix(m); }
    void setPostWidth(float w) { mPost.setWidth(w); }
    void setPostDrive(float d) { mPost.setDrive(d); }
    void setPostRotFast(bool f) { mPost.setRotFast(f); }
    void setPostHornDrum(float b) { mPost.setHornDrum(b); }
    void setPostManual(float m) { mPost.setManual(m); }
    void setPostInvert(bool inv) { mPost.setInvert(inv); }
    void setPostP2Ratio(float r) { mPost.setP2Ratio(r); }
    void setPostSeries(bool s) { mPost.setSeries(s); }
    void setPostBypassed(bool b) { mPost.setBypassed(b); }

    void setBpm(double bpm)
    {
        for (auto &v : mVoice)
            v.setBpm(bpm);
        mPost.setBpm(bpm);
    }

private:
    static size_t idx(int s) { return (size_t)std::min(std::max(s, 0), kSlots - 1); }
    bool anySolo() const { return mSolo[0] || mSolo[1] || mSolo[2]; }
    // A slot is audible if it's soloed (when any solo is active, solo overrides
    // bypass) or, with no solo active, simply not bypassed.
    bool slotAudible(int s) const
    {
        return anySolo() ? mSolo[(size_t)s] : !mVoice[(size_t)s].isBypassed();
    }

    // PARALLEL routing: each active slot processes a copy of the dry input into
    // its own branch; the branches are summed by the (bypass-aware, smoothed) pad
    // weights, then one global Mod Mix blends the mod-bus against the dry input.
    void processParallel(float *left, float *right, int n)
    {
        const bool stereo = (left != right);
        const size_t N = (size_t)n;
        // 1) capture the dry section input
        for (size_t i = 0; i < N; ++i)
        {
            mDryL[i] = left[i];
            mDryR[i] = stereo ? right[i] : left[i];
        }
        // 2) each active slot -> its own fully-processed branch from the dry input
        for (int s = 0; s < kSlots; ++s)
        {
            if (!slotAudible(s)) continue;
            for (size_t i = 0; i < N; ++i)
            {
                mBranchL[(size_t)s][i] = mDryL[i];
                mBranchR[(size_t)s][i] = mDryR[i];
            }
            float *bl = mBranchL[(size_t)s].data();
            float *br = stereo ? mBranchR[(size_t)s].data() : bl;
            mVoice[(size_t)s].process(bl, br, n);
        }
        // 3) target blend weights from the pad, with bypassed nodes removed
        float wT[kSlots];
        padWeights(mPadX, mPadY, wT);
        float sum = 0.0f;
        for (int s = 0; s < kSlots; ++s)
        {
            if (!slotAudible(s)) wT[s] = 0.0f;
            sum += wT[s];
        }
        if (sum > 1.0e-12f)
        {
            for (int s = 0; s < kSlots; ++s) wT[s] /= sum;
        }
        else // puck sat on inactive node(s): spread evenly over the audible slots
        {
            int act = 0;
            for (int s = 0; s < kSlots; ++s) act += slotAudible(s) ? 1 : 0;
            for (int s = 0; s < kSlots; ++s)
                wT[s] = slotAudible(s) ? 1.0f / (float)std::max(1, act) : 0.0f;
        }
        // 4) blend the branches (smoothed weights) + one global mod-mix vs dry
        for (size_t i = 0; i < N; ++i)
        {
            for (int s = 0; s < kSlots; ++s) mWz[(size_t)s] += mSmoothK * (wT[s] - mWz[(size_t)s]);
            mModMixZ += mSmoothK * (mModMix - mModMixZ);
            float busL = 0.0f, busR = 0.0f;
            for (int s = 0; s < kSlots; ++s)
            {
                if (!slotAudible(s)) continue;
                busL += mWz[(size_t)s] * mBranchL[(size_t)s][i];
                busR += mWz[(size_t)s] * mBranchR[(size_t)s][i];
            }
            left[i] = (1.0f - mModMixZ) * mDryL[i] + mModMixZ * busL;
            if (stereo) right[i] = (1.0f - mModMixZ) * mDryR[i] + mModMixZ * busR;
        }
    }

    std::array<ModVoice, kSlots> mVoice;
    ModVoice mPost; // dedicated end-of-section post effect (rotary/tremolo/harm-trem)
    ModLevelLock mLock; // section-level output loudness match
    bool mSolo[kSlots] = {false, false, false}; // momentary dial-in solo (not a param)
    // ---- section routing state ----
    int mOrder[kSlots] = {0, 1, 2};          // series chain order (mOrder[pos] = slot)
    bool mParallel = false;                  // false = series, true = parallel
    float mPadX = 0.5f, mPadY = 1.0f / 3.0f; // blend puck (default = centroid = equal blend)
    float mModMix = 1.0f;                    // parallel: mod-bus vs dry (1 = full bus)
    float mSmoothK = 1.0f;                   // per-sample de-zip for weights + mod-mix
    float mWz[kSlots] = {1.0f / 3.0f, 1.0f / 3.0f, 1.0f / 3.0f}; // smoothed weights
    float mModMixZ = 1.0f;
    std::vector<float> mDryL, mDryR;                          // dry-input scratch
    std::array<std::vector<float>, kSlots> mBranchL, mBranchR; // per-slot branch scratch
};

} // namespace nam_rig
