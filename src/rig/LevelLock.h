#pragma once
// LevelLock — section loudness-lock used by ModBlock.
//
// Keeps a block/section's output at the same perceived level as its input no
// matter which parameter changed -- the fix for "tweaking a knob changes the
// volume". It tracks slow RMS-power envelopes of IN vs OUT and applies a makeup
// gain = sqrt(in/out).
//
// Two properties make it well-behaved:
//   * It's a RATIO (out/in), so it ignores the player's dynamics -- a louder
//     strum raises in and out together and the gain doesn't budge. It only
//     removes the steady level OFFSET the processing introduces.
//   * The envelopes are slow (kTauMs, well above any musical LFO period), so
//     tremolo/rotary/harm-trem amplitude modulation passes through untouched --
//     only the long-term level is locked, never the pulse.
// Clamped to +-6 dB and gated at a silence floor so it can't run away or
// amplify the noise floor / effect tails. JUCE-free + unit-testable.

#include <cmath>
#include <algorithm>

namespace nam_rig
{

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

    // Advance the input-power envelope over the dry input (call before the
    // processing runs). Slow TC means the one-block lead over applyOutput is
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

} // namespace nam_rig
