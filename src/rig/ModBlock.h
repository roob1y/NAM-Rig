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
        kVibrato, kRotary, kUniVibe, kHarmTrem
    };

    // Voicing constants (fixed; knobs scale within these)
    static constexpr double kChorusBaseMs = 7.0, kChorusSpreadMs = 10.0;
    static constexpr double kFlangerBaseMs = 1.0, kFlangerSpreadMs = 5.0;
    static constexpr double kVibBaseMs = 2.0, kVibSpreadMs = 8.0;
    static constexpr double kRotBaseMs = 3.0, kRotSpreadMs = 4.0;
    static constexpr float kRotSlowHz = 0.72f, kRotFastHz = 6.6f; // chorale / tremolo
    static constexpr double kPhaserCenterHz = 700.0, kPhaserOctaves = 2.0;
    static constexpr double kUniCenterHz = 400.0;
    static constexpr double kHarmXoverHz = 800.0;
    static constexpr int kPhaserStages = 4;
    static constexpr double kRightLfoOffset = 0.25; // 90 degrees at Width = 1
    static constexpr float kPhaserFixedDepth = 1.0f; // phaser sweep is hardwired
    static constexpr float kUniFeedback = 0.3f;      // uni-vibe resonance is hardwired

    // ---- per-effect voicing (public so the tests + UI can read it) ----
    static int authenticWave(Type t) { return t == kFlanger ? Lfo::Triangle : Lfo::Sine; }
    static float depthMax(Type t)
    {
        return (t == kTremolo || t == kHarmTrem) ? 0.85f : 1.0f; // keep AM short of silence
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
        case kFlanger:
        case kPhaser:
        case kUniVibe: return 0.5f;
        default: return 1.0f; // vibrato / rotary / tremolo / harm-trem = full wet
        }
    }
    static bool mixExposed(Type t) { return t == kChorus; }

    const char *name() const override { return "Modulation"; }

    void prepare(const BlockContext &ctx) override
    {
        mFs = ctx.sampleRate;
        // wooden-cabinet resonance: peaking biquad ~250 Hz, Q 1.2, +4 dB (RBJ)
        {
            const double fc = 250.0, Q = 1.2, gainDb = 4.0;
            const double A = std::pow(10.0, gainDb / 40.0);
            const double w0 = 2.0 * 3.14159265358979323846 * fc / mFs;
            const double cw = std::cos(w0), sw = std::sin(w0), al = sw / (2.0 * Q);
            const double a0 = 1.0 + al / A;
            mCabB0 = (float)((1.0 + al * A) / a0);
            mCabB1 = (float)((-2.0 * cw) / a0);
            mCabB2 = (float)((1.0 - al * A) / a0);
            mCabA1 = (float)((-2.0 * cw) / a0);
            mCabA2 = (float)((1.0 - al / A) / a0);
        }
        const int maxDelay =
            (int)std::ceil((kChorusBaseMs + kChorusSpreadMs + 2.0) * 0.001 * mFs);
        for (auto &d : mLine)
            d.prepare(maxDelay);
        mLfo.prepare(mFs);
        mSmoothK = 1.0f - std::exp((float)(-1.0 / (0.010 * mFs))); // 10 ms
        reset();
        mPrepared = true;
    }

    void reset() override
    {
        for (auto &d : mLine)
            d.reset();
        for (auto &ap : mAllpass)
            ap = {};
        mFbState[0] = mFbState[1] = 0.0f;
        mFbLp[0] = mFbLp[1] = 0.0f;
        mXoverLp[0] = mXoverLp[1] = 0.0f;
        mBbdLp[0] = mBbdLp[1] = 0.0f;
        mTremG[0] = mTremG[1] = 1.0f; // unity gain (no mute on start)
        mHornLp[0] = mHornLp[1] = 0.0f;
        mDrumLp[0] = mDrumLp[1] = 0.0f;
        mCabZ1[0] = mCabZ1[1] = 0.0f;
        mCabZ2[0] = mCabZ2[1] = 0.0f;
        mWindLp[0] = mWindLp[1] = 0.0f;
        mRumbleLp[0] = mRumbleLp[1] = 0.0f;
        mNoiseRng = 0x9E3779B9u;
        mRotHornPhase = 0.0;
        mRotDrumPhase = 0.0;
        mRotHornSpeed = kRotSlowHz;
        mRotDrumSpeed = kRotSlowHz * 0.78f;
        mLfo.reset();
        mDepthZ = mDepth;
        mMixZ = mMix;
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
    void setWidth(float w) { mWidth = w; }         // 0 = mono spread, 1 = 90 deg
    void setDrive(float d) { mRotDrive = d; }      // Rotary: Leslie tube-amp drive
    void setRotFast(bool f) { mRotFast = f; }      // Rotary: slow (chorale) / fast (tremolo)
    void setRotNoise(bool n) { mRotNoise = n; }    // Rotary: motor/wind mechanical noise on/off

    // LFO period in beats for each modSync choice (index 0 = Off = free rate).
    static constexpr int kNumSync = 10;
    static double syncBeats(int i)
    {
        static const double beats[kNumSync] = {0.0, 4.0, 2.0, 1.0, 1.5, 2.0 / 3.0,
                                               0.5, 0.75, 1.0 / 3.0, 0.25};
        return (i > 0 && i < kNumSync) ? beats[i] : 0.0;
    }
    float effectiveRateHz() const
    {
        const double beats = syncBeats(mSyncIndex);
        return beats > 0.0 ? (float)((mBpm / 60.0) / beats) : mFreeRateHz;
    }

    void process(float *left, float *right, int numSamples) override
    {
        const bool stereo = (left != right);
        const Type ty = mType;
        const double rate = (double)effectiveRateHz();
        mLfo.setRateHz((float)rate);
        mLfo.setWaveform(ty == kTremolo ? mUserWave : authenticWave(ty)); // shape hardwired per type
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
        mWindLpCoef = coefForHz(1500.0, mFs); // wind = noise high-passed above this
        mRumbleLpCoef = coefForHz(120.0, mFs); // motor rumble = noise low-passed
        const double rOff = (double)mWidth * kRightLfoOffset;
        const float mixTarget = mixFor(ty, mMix); // only Chorus uses the knob

        for (int i = 0; i < numSamples; ++i)
        {
            mDepthZ += mSmoothK * (mDepth - mDepthZ);
            mMixZ += mSmoothK * (mixTarget - mMixZ);
            const float depth = mDepthZ * dMax; // voiced musical maximum

            // pass the LFO phase OFFSET (not value) so multi-tap effects can
            // read several phases of the same accumulator this sample.
            left[i] = processSample(0, left[i], 0.0, depth);
            if (stereo)
                right[i] = processSample(1, right[i], rOff, depth);

            mLfo.advance();
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
            const double sweepMs =
                kFlangerBaseMs + (double)depth * kFlangerSpreadMs * (0.5 + 0.5 * (double)lfo);
            mLine[(size_t)ch].write(x + mFeedback * mFbState[(size_t)ch]);
            float wet = mLine[(size_t)ch].readFrac(std::max(2.0, sweepMs * 0.001 * mFs));
            wet = bbdColor(ch, wet);
            float &flp = mFbLp[(size_t)ch];
            flp += mFbLpCoef * (wet - flp); // tone-shape the regen (tames fizz)
            mFbState[(size_t)ch] = flp;
            return (1.0f - mMixZ) * x + mMixZ * wet;
        }
        case kPhaser:
        {
            // 4 first-order allpasses, classic sweep hardwired (depth fixed);
            // Feedback (resonance) is the one exposed control.
            const double fc = std::min(
                0.45 * mFs,
                kPhaserCenterHz * std::pow(2.0, (double)lfo * kPhaserOctaves * (double)kPhaserFixedDepth));
            const double tanw = std::tan(3.14159265358979323846 * fc / mFs);
            const float a = (float)((tanw - 1.0) / (tanw + 1.0));

            float v = x + mFeedback * mFbState[(size_t)ch];
            for (int s = 0; s < kPhaserStages; ++s)
            {
                auto &st = mAllpass[(size_t)(ch * kPhaserStages + s)];
                const float y = a * v + st.x1 - a * st.y1;
                st.x1 = v;
                st.y1 = y;
                v = y;
            }
            mFbState[(size_t)ch] = v;
            return (1.0f - mMixZ) * x + mMixZ * v;
        }
        case kTremolo:
        {
            const float target = 1.0f - depth * (0.5f + 0.5f * lfo);
            float &gn = mTremG[(size_t)ch];
            gn += mTremCoef * (target - gn); // smoothing de-clicks square/S&H edges
            return (1.0f - mMixZ) * x + mMixZ * (x * gn);
        }
        case kVibrato:
        {
            const double sweepMs =
                kVibBaseMs + (double)depth * kVibSpreadMs * (0.5 + 0.5 * (double)lfo);
            mLine[(size_t)ch].write(x);
            float wet = mLine[(size_t)ch].readFrac(std::max(2.0, sweepMs * 0.001 * mFs));
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

            // --- tube power amp: clean at Drive 0, asymmetric growl when pushed.
            // Pre-gain into a soft clip, then MAKEUP ~2.6/pre (not 1/pre, which
            // collapsed the level) so driving compresses + thickens without
            // getting quiet. Small bias adds the even-harmonic tube warmth. ---
            float xd = x;
            if (mRotDrive > 0.0f)
            {
                const float pre = 1.0f + mRotDrive * 5.0f;
                const float bias = 0.15f * mRotDrive;
                const float sat = (std::tanh(x * pre + bias) - std::tanh(bias)) * (2.6f / pre);
                xd = x + mRotDrive * (sat - x);
            }

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

            // --- bass drum: high-pass the deep lows, slow amplitude throb ---
            float &dlp = mDrumLp[(size_t)ch];
            dlp += mDrumLpCoef * (lowB - dlp);
            const float drumIn = lowB - dlp; // remove subsonics the woofer can't throw
            const float cosD = std::cos(6.2831853f * (float)(mRotDrumPhase + rot));
            const float drum = drumIn * (1.0f - 0.45f * depth * (0.5f - 0.5f * cosD));

            // (Synthesized motor/wind noise removed — it read as a shaker, not
            //  air. Real cabinet ambience needs a recorded sample; the mRotNoise
            //  toggle is left as the on/off hook for that if we add it.)

            // --- wooden cabinet: low-mid resonant body (peaking biquad, TDF2) ---
            float wet = horn + drum;
            {
                float &z1 = mCabZ1[(size_t)ch], &z2 = mCabZ2[(size_t)ch];
                const float y = mCabB0 * wet + z1;
                z1 = mCabB1 * wet - mCabA1 * y + z2;
                z2 = mCabB2 * wet - mCabA2 * y;
                wet = y;
            }
            return (1.0f - mMixZ) * x + mMixZ * wet;
        }
        case kUniVibe:
        {
            // staggered allpass centres + hardwired resonance. The vibe's throb
            // comes from an ASYMMETRIC (skewed) lamp sweep + a touch of amplitude.
            static const double mult[kPhaserStages] = {0.5, 1.0, 1.8, 3.2};
            const float g01 = 0.5f + 0.5f * lfo;
            const float skew = std::pow(g01, 1.8f);
            const float ulfo = skew * 2.0f - 1.0f;
            float v = x + kUniFeedback * mFbState[(size_t)ch];
            for (int s = 0; s < kPhaserStages; ++s)
            {
                const double fc = std::min(
                    0.45 * mFs,
                    kUniCenterHz * mult[s] * std::pow(2.0, (double)ulfo * (double)depth));
                const double tanw = std::tan(3.14159265358979323846 * fc / mFs);
                const float a = (float)((tanw - 1.0) / (tanw + 1.0));
                auto &st = mAllpass[(size_t)(ch * kPhaserStages + s)];
                const float y = a * v + st.x1 - a * st.y1;
                st.x1 = v;
                st.y1 = y;
                v = y;
            }
            mFbState[(size_t)ch] = v;
            v *= 1.0f - 0.12f * depth * skew; // subtle lamp amplitude movement
            return (1.0f - mMixZ) * x + mMixZ * v;
        }
        case kHarmTrem:
        {
            float &lp = mXoverLp[(size_t)ch];
            lp += mXoverCoef * (x - lp);
            const float low = lp, high = x - lp;
            const float g = 0.5f + 0.5f * lfo;
            const float wet = low * (1.0f - depth * g) + high * (1.0f - depth * (1.0f - g));
            return (1.0f - mMixZ) * x + mMixZ * wet;
        }
        }
        return x;
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

    static float coefForHz(double hz, double fs)
    {
        const double fc = std::min(std::max(hz, 1.0), 0.45 * fs);
        return (float)(1.0 - std::exp(-2.0 * 3.14159265358979323846 * fc / fs));
    }
    static float coefForMs(float ms, double fs)
    {
        return 1.0f - (float)std::exp(-1.0 / ((double)std::max(0.05f, ms) * 0.001 * fs));
    }

    void flushDenormals()
    {
        for (auto &st : mAllpass)
        {
            if (std::abs(st.x1) < 1.0e-30f) st.x1 = 0.0f;
            if (std::abs(st.y1) < 1.0e-30f) st.y1 = 0.0f;
        }
        for (auto &f : mFbState)
            if (std::abs(f) < 1.0e-30f) f = 0.0f;
        for (auto &f : mXoverLp)
            if (std::abs(f) < 1.0e-30f) f = 0.0f;
        for (auto &f : mBbdLp)
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
        for (auto &f : mWindLp)
            if (std::abs(f) < 1.0e-30f) f = 0.0f;
        for (auto &f : mRumbleLp)
            if (std::abs(f) < 1.0e-30f) f = 0.0f;
    }

    struct ApState { float x1 = 0.0f, y1 = 0.0f; };

    double mFs = 48000.0;
    Type mType = kChorus;
    Lfo mLfo;
    std::array<FracDelayLine, 2> mLine;
    std::array<ApState, 2 * kPhaserStages> mAllpass{};
    std::array<float, 2> mFbState{};
    std::array<float, 2> mFbLp{};            // flanger feedback tone state
    std::array<float, 2> mXoverLp{};         // harmonic-trem / rotary crossover state
    std::array<float, 2> mBbdLp{};           // BBD HF-rolloff state
    std::array<float, 2> mTremG{1.0f, 1.0f}; // smoothed tremolo gain (de-click)
    float mDepth = 0.5f, mFeedback = 0.0f, mMix = 0.5f;
    float mDepthZ = 0.5f, mMixZ = 0.5f, mSmoothK = 0.01f;
    float mWidth = 1.0f;
    int mUserWave = 0;                          // Tremolo's Shape choice
    float mBakedBbd = 0.0f;                     // per-block, from bakedBbd(type)
    float mBbdCoef = 1.0f, mXoverCoef = 0.1f;   // per-block filter coeffs
    float mTremCoef = 1.0f, mFbLpCoef = 1.0f;   // tremolo de-click, flanger fb tone
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
    bool mRotNoise = true;             // Leslie mechanical noise on/off
    uint32_t mNoiseRng = 0x9E3779B9u;  // wind/rumble noise generator (LCG)
    std::array<float, 2> mWindLp{}, mRumbleLp{};            // wind HP / rumble LP state
    float mWindLpCoef = 1.0f, mRumbleLpCoef = 0.1f;
    float mFreeRateHz = 1.0f;
    double mBpm = 120.0;
    int mSyncIndex = 0;
    bool mPrepared = false;
};

// The modulation SECTION: kSlots voices chained in series. Each slot has its
// own type/rate/sync/wet-dry mix/width and its own bypass. Presented to
// RigChain as a single StereoBlock (RigChain is unchanged).
class ModBlock : public StereoBlock
{
public:
    static constexpr int kSlots = 3;

    const char *name() const override { return "Modulation"; }

    void prepare(const BlockContext &ctx) override
    {
        for (auto &v : mVoice)
            v.prepare(ctx);
    }
    void reset() override
    {
        for (auto &v : mVoice)
            v.reset();
    }

    void process(float *left, float *right, int numSamples) override
    {
        for (auto &v : mVoice)
            if (!v.isBypassed())
                v.process(left, right, numSamples);
    }

    double latencySamples() const override
    {
        double s = 0.0;
        for (auto &v : mVoice)
            s += v.latencySamples();
        return s;
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
    void setRotNoise(int s, bool n) { mVoice[idx(s)].setRotNoise(n); }
    void setSlotBypassed(int s, bool b) { mVoice[idx(s)].setBypassed(b); }
    void setBpm(double bpm)
    {
        for (auto &v : mVoice)
            v.setBpm(bpm);
    }

private:
    static size_t idx(int s) { return (size_t)std::min(std::max(s, 0), kSlots - 1); }
    std::array<ModVoice, kSlots> mVoice;
};

} // namespace nam_rig
