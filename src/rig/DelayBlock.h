#pragma once
// DelayBlock — stereo delay (post-cab): free time or host-tempo sync
// (straight / dotted / triplet), feedback tone LPF inside the loop, normal or
// ping-pong routing, wow+flutter modulated repeats, mix, width. Time changes
// glide (one-pole smooth + Hermite fractional read) for tape-style repitch
// instead of clicks. Zero latency. Verified by tests/delay_test.cpp.
//
// Mono-buffer rule (left == right): mono in-place delay on the left line.

#include "Blocks.h"
#include "Lfo.h"
#include "Biquad.h"
#include <array>
#include <algorithm>

namespace nam_rig
{

class DelayBlock : public StereoBlock
{
public:
    static constexpr float kMaxTimeMs = 2000.0f, kMinTimeMs = 20.0f;
    static constexpr float kMaxFeedback = 0.95f;
    // Wow/flutter at full knob: slow deep + fast shallow, in ms of sweep.
    static constexpr double kWowHz = 0.9, kWowDepthMs = 2.5;
    static constexpr double kFlutterHz = 6.5, kFlutterDepthMs = 0.25;

    // Sync divisions in quarter-note beats; index 0 = Free (use time knob).
    // Order is the parameter StringArray order — never reorder, only append.
    static constexpr std::array<double, 14> kSyncBeats = {
        0.0,        // Free
        4.0,        // 1/1
        3.0,        // 1/2.
        2.0,        // 1/2
        4.0 / 3.0,  // 1/2T
        1.5,        // 1/4.
        1.0,        // 1/4
        2.0 / 3.0,  // 1/4T
        0.75,       // 1/8.
        0.5,        // 1/8
        1.0 / 3.0,  // 1/8T
        0.375,      // 1/16.
        0.25,       // 1/16
        1.0 / 6.0}; // 1/16T

    const char *name() const override { return "Delay"; }

    void prepare(const BlockContext &ctx) override
    {
        mFs = ctx.sampleRate;
        const int maxDelay =
            (int)std::ceil((kMaxTimeMs + kWowDepthMs + kFlutterDepthMs + 4.0) * 0.001 * mFs);
        for (auto &d : mLine)
            d.prepare(maxDelay);
        mWow.prepare(mFs);
        mWow.setRateHz((float)kWowHz);
        mFlutter.prepare(mFs);
        mFlutter.setRateHz((float)kFlutterHz);
        // Time glide ~80 ms; mix/width zipper guard 10 ms.
        mTimeK = 1.0f - std::exp((float)(-1.0 / (0.080 * mFs)));
        mSmoothK = 1.0f - std::exp((float)(-1.0 / (0.010 * mFs)));
        updateTone(true);
        reset();
        mPrepared = true;
    }

    void reset() override
    {
        for (auto &d : mLine)
            d.reset();
        for (auto &t : mTone)
            t.reset();
        mWow.reset();
        mFlutter.reset();
        mTimeZ = (double)currentTimeMs();
        mMixZ = mMix;
        mWidthZ = mWidth;
    }

    // ---- parameters (audio thread) ----
    void setTimeMs(float ms) { mTimeMs = std::clamp(ms, kMinTimeMs, kMaxTimeMs); }
    void setSyncIndex(int idx) { mSyncIdx = std::clamp(idx, 0, (int)kSyncBeats.size() - 1); }
    void setBpm(double bpm) { mBpm = (bpm > 1.0) ? bpm : 120.0; }
    void setFeedback(float f) { mFeedback = std::clamp(f, 0.0f, kMaxFeedback); }
    void setToneHz(float hz)
    {
        if (hz != mToneHz)
        {
            mToneHz = hz;
            if (mPrepared)
                updateTone(false);
        }
    }
    void setPingPong(bool pp) { mPingPong = pp; }
    void setWidth(float w) { mWidth = std::clamp(w, 0.0f, 1.0f); }
    void setModAmount(float m) { mModAmt = std::clamp(m, 0.0f, 1.0f); }
    void setMix(float m) { mMix = std::clamp(m, 0.0f, 1.0f); }

    // Effective time after sync resolution (exposed for verification).
    float currentTimeMs() const
    {
        if (mSyncIdx > 0)
            return std::clamp((float)(kSyncBeats[(size_t)mSyncIdx] * 60000.0 / mBpm),
                              kMinTimeMs, kMaxTimeMs);
        return mTimeMs;
    }

    const Biquad &toneFilter() const { return mTone[0]; } // for verification

    void process(float *left, float *right, int numSamples) override
    {
        const bool stereo = (left != right);
        const float targetMs = currentTimeMs();

        for (int i = 0; i < numSamples; ++i)
        {
            // Double + snap: a float one-pole stalls an ulp-starved ~0.1 ms
            // short of the target (measured: echo landed 3 smp early).
            mTimeZ += (double)mTimeK * ((double)targetMs - mTimeZ);
            if (std::abs((double)targetMs - mTimeZ) < 1.0e-4)
                mTimeZ = (double)targetMs;
            mMixZ += mSmoothK * (mMix - mMixZ);
            mWidthZ += mSmoothK * (mWidth - mWidthZ);

            const double modMs = (double)mModAmt
                                 * (kWowDepthMs * (double)mWow.value()
                                    + kFlutterDepthMs * (double)mFlutter.value());
            mWow.advance();
            mFlutter.advance();
            const double dSmp =
                std::max(2.0, (mTimeZ + modMs) * 0.001 * mFs);

            const float dryL = left[i];
            const float dryR = stereo ? right[i] : dryL;

            // Taps are read before this sample's write, so read at dSmp-1 to
            // make the effective delay exactly dSmp (echo lands at nominal
            // time; dSmp >= 2 keeps the Hermite window in valid history).
            float wetL = mLine[0].readFrac(dSmp - 1.0);
            float wetR = stereo ? mLine[1].readFrac(dSmp - 1.0) : wetL;

            // Tone LPF sits at the line output, inside the feedback loop:
            // every trip through the loop darkens the repeat again.
            if (mToneOn)
            {
                wetL = mTone[0].processSample(wetL);
                if (stereo)
                    wetR = mTone[1].processSample(wetR);
                else
                    wetR = wetL;
            }

            if (mPingPong && stereo)
            {
                // Mono-summed input enters L; L bounces to R; R feeds back to L.
                mLine[0].write(0.5f * (dryL + dryR) + mFeedback * wetR);
                mLine[1].write(wetL);
            }
            else
            {
                mLine[0].write(dryL + mFeedback * wetL);
                if (stereo)
                    mLine[1].write(dryR + mFeedback * wetR);
            }

            // Width: M/S scale on the wet signal only.
            if (stereo)
            {
                const float m = 0.5f * (wetL + wetR);
                const float s = 0.5f * (wetL - wetR) * mWidthZ;
                wetL = m + s;
                wetR = m - s;
            }

            left[i] = (1.0f - mMixZ) * dryL + mMixZ * wetL;
            if (stereo)
                right[i] = (1.0f - mMixZ) * dryR + mMixZ * wetR;
        }

        for (auto &t : mTone) // flush denormal filter state between blocks
        {
            if (std::abs(t.z1) < 1.0e-30f) t.z1 = 0.0f;
            if (std::abs(t.z2) < 1.0e-30f) t.z2 = 0.0f;
        }
    }

private:
    void updateTone(bool force)
    {
        mToneOn = mToneHz < 20000.0f;
        if (mToneOn || force)
        {
            const auto coeffs = Biquad::lowpass(mFs, std::min((double)mToneHz, 0.45 * mFs));
            for (auto &t : mTone)
            {
                const float z1 = t.z1, z2 = t.z2; // keep state, swap coefficients
                t = coeffs;
                t.z1 = z1;
                t.z2 = z2;
            }
        }
    }

    double mFs = 48000.0, mBpm = 120.0;
    std::array<FracDelayLine, 2> mLine;
    std::array<Biquad, 2> mTone;
    Lfo mWow, mFlutter;

    float mTimeMs = 350.0f, mFeedback = 0.35f, mToneHz = 8000.0f;
    float mWidth = 1.0f, mModAmt = 0.0f, mMix = 0.25f;
    int mSyncIdx = 0;
    bool mPingPong = false, mToneOn = true, mPrepared = false;

    double mTimeZ = 350.0;
    float mMixZ = 0.25f, mWidthZ = 1.0f;
    float mTimeK = 0.001f, mSmoothK = 0.01f;
};

} // namespace nam_rig
