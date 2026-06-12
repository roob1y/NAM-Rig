#pragma once
// ModBlock — multi-type stereo modulation (post-cab): Chorus / Flanger /
// Phaser / Tremolo behind one Type switch with shared Rate / Depth /
// Feedback / Mix. Right channel runs the same LFO 90 degrees behind the
// left for the classic stereo spread. Zero latency (effect delays are not
// compensation). Verified by tests/mod_test.cpp.
//
// Mono-buffer rule (RigChain may pass left == right): when the pointers
// alias we process the left channel only.

#include "Blocks.h"
#include "Lfo.h"
#include <array>
#include <algorithm>

namespace nam_rig
{

class ModBlock : public StereoBlock
{
public:
    enum Type { kChorus = 0, kFlanger, kPhaser, kTremolo };

    // Voicing constants (fixed; knobs scale within these)
    static constexpr double kChorusBaseMs = 7.0, kChorusSpreadMs = 10.0;
    static constexpr double kFlangerBaseMs = 1.0, kFlangerSpreadMs = 5.0;
    static constexpr double kPhaserCenterHz = 700.0, kPhaserOctaves = 2.0;
    static constexpr int kPhaserStages = 4;
    static constexpr double kRightLfoOffset = 0.25; // 90 degrees

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
    void setRateHz(float hz) { mLfo.setRateHz(hz); }
    void setDepth(float d) { mDepth = d; }
    void setFeedback(float f) { mFeedback = f; }
    void setMix(float m) { mMix = m; }

    void process(float *left, float *right, int numSamples) override
    {
        const bool stereo = (left != right);
        for (int i = 0; i < numSamples; ++i)
        {
            mDepthZ += mSmoothK * (mDepth - mDepthZ);
            mMixZ += mSmoothK * (mMix - mMixZ);

            const float lfoL = mLfo.value(0.0);
            const float lfoR = mLfo.value(kRightLfoOffset);
            mLfo.advance();

            left[i] = processSample(0, left[i], lfoL);
            if (stereo)
                right[i] = processSample(1, right[i], lfoR);
        }
        flushDenormals();
    }

private:
    float processSample(int ch, float x, float lfo)
    {
        switch (mType)
        {
        case kChorus:
        {
            const double sweepMs =
                kChorusBaseMs + (double)mDepthZ * kChorusSpreadMs * (0.5 + 0.5 * (double)lfo);
            mLine[(size_t)ch].write(x);
            const float wet = mLine[(size_t)ch].readFrac(sweepMs * 0.001 * mFs);
            return (1.0f - mMixZ) * x + mMixZ * wet;
        }
        case kFlanger:
        {
            const double sweepMs =
                kFlangerBaseMs + (double)mDepthZ * kFlangerSpreadMs * (0.5 + 0.5 * (double)lfo);
            mLine[(size_t)ch].write(x + mFeedback * mFbState[(size_t)ch]);
            const float wet =
                mLine[(size_t)ch].readFrac(std::max(2.0, sweepMs * 0.001 * mFs));
            mFbState[(size_t)ch] = wet;
            return (1.0f - mMixZ) * x + mMixZ * wet;
        }
        case kPhaser:
        {
            // 4 first-order allpasses, fc swept exponentially around 700 Hz.
            const double fc = std::min(
                0.45 * mFs,
                kPhaserCenterHz * std::pow(2.0, (double)lfo * kPhaserOctaves * (double)mDepthZ));
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
            const float gain = 1.0f - mDepthZ * (0.5f + 0.5f * lfo);
            return (1.0f - mMixZ) * x + mMixZ * (x * gain);
        }
        }
        return x;
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
    }

    struct ApState { float x1 = 0.0f, y1 = 0.0f; };

    double mFs = 48000.0;
    Type mType = kChorus;
    Lfo mLfo;
    std::array<FracDelayLine, 2> mLine;
    std::array<ApState, 2 * kPhaserStages> mAllpass{};
    std::array<float, 2> mFbState{};
    float mDepth = 0.5f, mFeedback = 0.0f, mMix = 0.5f;
    float mDepthZ = 0.5f, mMixZ = 0.5f, mSmoothK = 0.01f;
    bool mPrepared = false;
};

} // namespace nam_rig
