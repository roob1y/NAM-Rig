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
    static constexpr float kMinLowCutHz = 20.0f; // Low Cut at/below this = off
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
        updateLowCut(true);
        reset();
        mPrepared = true;
    }

    void reset() override
    {
        for (auto &d : mLine)
            d.reset();
        for (auto &t : mTone)
            t.reset();
        for (auto &t : mLowCut)
            t.reset();
        mWow.reset();
        mFlutter.reset();
        mTimeZ = (double)currentTimeMs();
        mTimeZR = (double)currentTimeMsR();
        mMixZ = mMix;
        mWidthZ = mWidth;
    }

    // ---- parameters (audio thread) ----
    void setTimeMs(float ms) { mTimeMs = std::clamp(ms, kMinTimeMs, kMaxTimeMs); }
    void setSyncIndex(int idx) { mSyncIdx = std::clamp(idx, 0, (int)kSyncBeats.size() - 1); }
    // Right-side division: index 0 = Link (R mirrors L); 1..13 select the same
    // musical divisions as kSyncBeats[1..13]. Unlinking puts the delay in DUAL
    // mode (independent per-side time + feedback) -- the dotted-1/8 + 1/4 trick.
    void setSyncIndexR(int idx) { mSyncIdxR = std::clamp(idx, 0, (int)kSyncBeats.size() - 1); }
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
    // Feedback high-pass (Low Cut): clears bass build-up so stacked repeats stay
    // defined. <= kMinLowCutHz is treated as off.
    void setLowCutHz(float hz)
    {
        if (hz != mLowCutHz)
        {
            mLowCutHz = hz;
            if (mPrepared)
                updateLowCut(false);
        }
    }
    void setPingPong(bool pp) { mPingPong = pp; }
    void setWidth(float w) { mWidth = std::clamp(w, 0.0f, 1.0f); }
    void setModAmount(float m) { mModAmt = std::clamp(m, 0.0f, 1.0f); }
    void setMix(float m) { mMix = std::clamp(m, 0.0f, 1.0f); }

    // Effective LEFT time after sync resolution (exposed for verification + UI).
    float currentTimeMs() const
    {
        if (mSyncIdx > 0)
            return std::clamp((float)(kSyncBeats[(size_t)mSyncIdx] * 60000.0 / mBpm),
                              kMinTimeMs, kMaxTimeMs);
        return mTimeMs;
    }

    // Effective RIGHT time: Link (idx 0) mirrors the left; otherwise its own
    // synced division. Used by the dual routing and the tap visualiser.
    float currentTimeMsR() const
    {
        if (mSyncIdxR <= 0)
            return currentTimeMs();
        return std::clamp((float)(kSyncBeats[(size_t)mSyncIdxR] * 60000.0 / mBpm),
                          kMinTimeMs, kMaxTimeMs);
    }
    bool dualTime() const { return mSyncIdxR > 0; } // R unlinked -> independent sides

    const Biquad &toneFilter() const { return mTone[0]; } // for verification

    void process(float *left, float *right, int numSamples) override
    {
        const bool stereo = (left != right);
        const float targetMsL = currentTimeMs();
        const float targetMsR = stereo ? currentTimeMsR() : targetMsL;
        const bool dual = stereo && dualTime(); // independent L/R times + feedback

        for (int i = 0; i < numSamples; ++i)
        {
            // Per-channel time glide (double + snap; a float one-pole stalls an
            // ulp-starved ~0.1 ms short -> echo lands a few samples early).
            mTimeZ += (double)mTimeK * ((double)targetMsL - mTimeZ);
            if (std::abs((double)targetMsL - mTimeZ) < 1.0e-4)
                mTimeZ = (double)targetMsL;
            mTimeZR += (double)mTimeK * ((double)targetMsR - mTimeZR);
            if (std::abs((double)targetMsR - mTimeZR) < 1.0e-4)
                mTimeZR = (double)targetMsR;
            mMixZ += mSmoothK * (mMix - mMixZ);
            mWidthZ += mSmoothK * (mWidth - mWidthZ);

            const double modMs = (double)mModAmt
                                 * (kWowDepthMs * (double)mWow.value()
                                    + kFlutterDepthMs * (double)mFlutter.value());
            mWow.advance();
            mFlutter.advance();
            // Read at dSmp-1 (taps read before this sample's write) so the
            // effective delay is exactly dSmp; dSmp >= 2 keeps Hermite valid.
            const double dSmpL = std::max(2.0, (mTimeZ + modMs) * 0.001 * mFs);
            const double dSmpR = std::max(2.0, (mTimeZR + modMs) * 0.001 * mFs);

            const float dryL = left[i];
            const float dryR = stereo ? right[i] : dryL;

            float wetL = mLine[0].readFrac(dSmpL - 1.0);
            float wetR = stereo ? mLine[1].readFrac(dSmpR - 1.0) : wetL;

            // In-loop tone shaping, re-applied every repeat: high-cut (Tone) then
            // low-cut, so the two together act as a feedback band-pass.
            if (mToneOn)
            {
                wetL = mTone[0].processSample(wetL);
                wetR = stereo ? mTone[1].processSample(wetR) : wetL;
            }
            if (mLowCutOn)
            {
                wetL = mLowCut[0].processSample(wetL);
                wetR = stereo ? mLowCut[1].processSample(wetR) : wetL;
            }

            if (dual)
            {
                // Independent L/R delays (e.g. dotted-1/8 left, 1/4 right). Each
                // side feeds back into itself; ping-pong doesn't apply here.
                mLine[0].write(dryL + mFeedback * wetL);
                mLine[1].write(dryR + mFeedback * wetR);
            }
            else if (mPingPong && stereo)
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
        for (auto &t : mLowCut)
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

    void updateLowCut(bool force)
    {
        mLowCutOn = mLowCutHz > kMinLowCutHz;
        if (mLowCutOn || force)
        {
            const auto coeffs = Biquad::highpass(mFs, std::clamp((double)mLowCutHz, 20.0, 0.45 * mFs));
            for (auto &t : mLowCut)
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
    std::array<Biquad, 2> mTone, mLowCut;
    Lfo mWow, mFlutter;

    float mTimeMs = 350.0f, mFeedback = 0.35f, mToneHz = 8000.0f;
    float mLowCutHz = 20.0f; // feedback high-pass (Low Cut); <= kMinLowCutHz = off
    float mWidth = 1.0f, mModAmt = 0.0f, mMix = 0.25f;
    int mSyncIdx = 0, mSyncIdxR = 0; // mSyncIdxR: 0 = Link (mirror L)
    bool mPingPong = false, mToneOn = true, mLowCutOn = false, mPrepared = false;

    double mTimeZ = 350.0, mTimeZR = 350.0;
    float mMixZ = 0.25f, mWidthZ = 1.0f;
    float mTimeK = 0.001f, mSmoothK = 0.01f;
};

} // namespace nam_rig
