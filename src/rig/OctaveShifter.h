#pragma once
// OctaveShifter — a clean, low-latency time-domain pitch shifter for EXACT octave
// ratios (x2, x0.5, x0.25), driven by the shared PitchTracker. This replaces the
// analog-divider / Octavia approach (which read as "distortion, not a pitch"):
// it produces an actual, in-tune shifted pitch up AND down.
//
// Method (Puckette crossfaded variable-delay-line, made PITCH-SYNCHRONOUS):
//   y[n] = x[n - D[n]], with the read delay D ramping linearly. A delay ramping at
//   rate rho = dD/dn shifts pitch by beta = 1 - rho, so the ratio is EXACT BY
//   CONSTRUCTION (rho = 1 - beta) and can never mis-track — the octave is always
//   perfectly in tune regardless of what the tracker does. D sawtooths over a
//   window w; a second read tap half a window out of phase crossfades over the
//   wrap so there's no click.
//
//   The classic granular artifact is WARBLE: on a tonal (correlated) input the two
//   crossfading taps beat against each other. The fix here is the tracker: we lock
//   the window to w = 2*T0 (two fundamental periods). Then the two taps sit exactly
//   ONE period apart, so they read the SAME phase of the waveform and sum
//   COHERENTLY (no beating), and the wrap jump of w = 2*T0 lands in-phase (seamless).
//   Hann crossfade windows 50% apart sum to 1, so the coherent taps stay flat.
//   The tracker only refines cleanliness; it does not set the pitch.
//
// Latency: the read is a fraction of a window behind write (inherent pitch-shift
// smear, ~one period); reported as 0 by the block (the octave is a separate voice,
// intra-block smear can't be PDC-compensated anyway). JUCE-free; cubic interp.

#include <algorithm>
#include <cmath>
#include <vector>

namespace nam_rig
{

class OctaveShifter
{
public:
    void prepare(double sampleRate)
    {
        mSr = sampleRate > 0.0 ? sampleRate : 48000.0;
        mRing.assign((size_t)kSize, 0.0f);
        reset();
    }

    void reset()
    {
        std::fill(mRing.begin(), mRing.end(), 0.0f);
        mPos = 0; mPhase = 0.0f; mW = 1000.0f;
    }

    // One sample: push dry x, return the octave-shifted sample. targetW = 2*T0 (from
    // the tracker, clamped by the caller is fine — clamped here too); ratio = the
    // exact octave (2.0 up, 0.5 down, 0.25 two down). wSmooth = per-sample smoothing
    // of window changes (avoids clicks when the tracked period steps).
    inline float process(float x, float targetW, float ratio, float wSmooth)
    {
        mRing[(size_t)mPos] = x;

        const float wt = clampW(targetW);
        mW += wSmooth * (wt - mW);
        const float w = mW;

        const float rho = 1.0f - ratio;   // dD/dn : ratio is exact, independent of w
        mPhase += rho / w;
        mPhase -= std::floor(mPhase);     // wrap to [0,1)
        float pB = mPhase + 0.5f; pB -= std::floor(pB);

        const float dA = kBase + mPhase * w;
        const float dB = kBase + pB * w;
        constexpr float kTwoPi = 6.28318530717958647692f;
        const float gA = 0.5f * (1.0f - std::cos(kTwoPi * mPhase)); // Hann; gA+gB == 1
        const float gB = 0.5f * (1.0f - std::cos(kTwoPi * pB));

        const float y = gA * cubic(dA) + gB * cubic(dB);
        mPos = (mPos + 1) & kMask;
        return y;
    }

private:
    static float clampW(float w)
    {
        const float hi = (float)(kSize - 4);
        return w < 64.0f ? 64.0f : (w > hi ? hi : w);
    }

    // 4-point Catmull-Rom read `d` samples behind the just-written position.
    inline float cubic(float d) const
    {
        const float idx = (float)mPos - d;
        const int i1 = (int)std::floor(idx);
        const float f = idx - (float)i1;
        const float xm1 = mRing[(size_t)((i1 - 1) & kMask)];
        const float x0  = mRing[(size_t)(i1 & kMask)];
        const float x1  = mRing[(size_t)((i1 + 1) & kMask)];
        const float x2  = mRing[(size_t)((i1 + 2) & kMask)];
        return x0 + 0.5f * f * (x1 - xm1
             + f * (2.0f * xm1 - 5.0f * x0 + 4.0f * x1 - x2
             + f * (3.0f * (x0 - x1) + x2 - xm1)));
    }

    static constexpr int kSize = 8192;      // > 2*T0(lowest) + margin
    static constexpr int kMask = kSize - 1;
    static constexpr float kBase = 2.0f;    // min delay so cubic never reads the future

    double mSr = 48000.0;
    std::vector<float> mRing;
    int mPos = 0;
    float mPhase = 0.0f;
    float mW = 1000.0f;
};

} // namespace nam_rig
