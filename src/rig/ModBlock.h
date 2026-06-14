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

    const char *name() const override { return "Modulation"; }

    void prepare(const BlockContext &ctx) override
    {
        mFs = ctx.sampleRate;
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
        mXoverLp[0] = mXoverLp[1] = 0.0f;
        mBbdLp[0] = mBbdLp[1] = 0.0f;
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
        mLfo.setRateHz(effectiveRateHz());
        mLfo.setWaveform(ty == kTremolo ? mUserWave : authenticWave(ty)); // shape hardwired per type
        const float dMax = depthMax(ty);
        mBakedBbd = bakedBbd(ty);
        mBbdCoef = coefForHz(12000.0 - 10000.0 * (double)mBakedBbd, mFs);
        mXoverCoef = coefForHz(kHarmXoverHz, mFs);
        const double rOff = (double)mWidth * kRightLfoOffset;

        for (int i = 0; i < numSamples; ++i)
        {
            mDepthZ += mSmoothK * (mDepth - mDepthZ);
            mMixZ += mSmoothK * (mMix - mMixZ);
            const float depth = mDepthZ * dMax; // voiced musical maximum

            const float lfoL = mLfo.value(0.0);
            const float lfoR = mLfo.value(rOff);
            mLfo.advance();

            left[i] = processSample(0, left[i], lfoL, depth);
            if (stereo)
                right[i] = processSample(1, right[i], lfoR, depth);
        }
        flushDenormals();
    }

private:
    float processSample(int ch, float x, float lfo, float depth)
    {
        switch (mType)
        {
        case kChorus:
        {
            const double sweepMs =
                kChorusBaseMs + (double)depth * kChorusSpreadMs * (0.5 + 0.5 * (double)lfo);
            mLine[(size_t)ch].write(x);
            float wet = mLine[(size_t)ch].readFrac(sweepMs * 0.001 * mFs);
            wet = bbdColor(ch, wet);
            return (1.0f - mMixZ) * x + mMixZ * wet;
        }
        case kFlanger:
        {
            const double sweepMs =
                kFlangerBaseMs + (double)depth * kFlangerSpreadMs * (0.5 + 0.5 * (double)lfo);
            mLine[(size_t)ch].write(x + mFeedback * mFbState[(size_t)ch]);
            float wet = mLine[(size_t)ch].readFrac(std::max(2.0, sweepMs * 0.001 * mFs));
            wet = bbdColor(ch, wet);
            mFbState[(size_t)ch] = wet;
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
            const float gain = 1.0f - depth * (0.5f + 0.5f * lfo);
            return (1.0f - mMixZ) * x + mMixZ * (x * gain);
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
            const double sweepMs =
                kRotBaseMs + (double)depth * kRotSpreadMs * (0.5 + 0.5 * (double)lfo);
            mLine[(size_t)ch].write(x);
            float wet = mLine[(size_t)ch].readFrac(std::max(2.0, sweepMs * 0.001 * mFs));
            wet = bbdColor(ch, wet);
            const float am = 1.0f - 0.5f * depth * (0.5f + 0.5f * lfo);
            wet *= am;
            return (1.0f - mMixZ) * x + mMixZ * wet;
        }
        case kUniVibe:
        {
            // staggered allpass centres + hardwired resonance (throaty vibe)
            static const double mult[kPhaserStages] = {0.5, 1.0, 1.8, 3.2};
            float v = x + kUniFeedback * mFbState[(size_t)ch];
            for (int s = 0; s < kPhaserStages; ++s)
            {
                const double fc = std::min(
                    0.45 * mFs,
                    kUniCenterHz * mult[s] * std::pow(2.0, (double)lfo * (double)depth));
                const double tanw = std::tan(3.14159265358979323846 * fc / mFs);
                const float a = (float)((tanw - 1.0) / (tanw + 1.0));
                auto &st = mAllpass[(size_t)(ch * kPhaserStages + s)];
                const float y = a * v + st.x1 - a * st.y1;
                st.x1 = v;
                st.y1 = y;
                v = y;
            }
            mFbState[(size_t)ch] = v;
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
    }

    struct ApState { float x1 = 0.0f, y1 = 0.0f; };

    double mFs = 48000.0;
    Type mType = kChorus;
    Lfo mLfo;
    std::array<FracDelayLine, 2> mLine;
    std::array<ApState, 2 * kPhaserStages> mAllpass{};
    std::array<float, 2> mFbState{};
    std::array<float, 2> mXoverLp{}; // harmonic-trem crossover state
    std::array<float, 2> mBbdLp{};   // BBD HF-rolloff state
    float mDepth = 0.5f, mFeedback = 0.0f, mMix = 0.5f;
    float mDepthZ = 0.5f, mMixZ = 0.5f, mSmoothK = 0.01f;
    float mWidth = 1.0f;
    int mUserWave = 0;                          // Tremolo's Shape choice
    float mBakedBbd = 0.0f;                     // per-block, from bakedBbd(type)
    float mBbdCoef = 1.0f, mXoverCoef = 0.1f;   // per-block filter coeffs
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
