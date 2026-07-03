#pragma once
// Svf — a topology-preserving (TPT / zero-delay-feedback) 2-pole state-variable
// filter, after Zavalishin / Andy Simper (Cytomic "Solving the continuous SVF").
//
// Why this and not Biquad.h (RBJ): the envelope filter sweeps cutoff EVERY
// sample. RBJ direct-form coefficients are trig functions of the pole angle, so
// recomputing them per sample is costly AND injects zipper discontinuities when
// the difference-equation history no longer matches the new coefficients; at
// high Q it can blow up. The TPT SVF's state variables ARE the LP/BP/HP outputs
// and evolve continuously, cutoff and Q are (near) orthogonal, and the form is
// unconditionally stable even at max Q under fast modulation — exactly what an
// auto-wah needs. It also hands out LP/BP/HP simultaneously (+ notch = LP+HP),
// which is the mode switch on the Mu-Tron / Q-Tron for free.
//
// JUCE-free; verified offline by tests/env_filter_test.cpp.

#include <cmath>

namespace nam_rig
{

class Svf
{
public:
    struct Out { float lp, bp, hp, notch; };

    void prepare(double sampleRate)
    {
        mSr = (sampleRate > 0.0) ? sampleRate : 48000.0;
        reset();
    }

    void reset() { mIc1 = 0.0f; mIc2 = 0.0f; }

    // Update coefficients from cutoff (Hz) and quality factor Q. Safe to call
    // per sample. Cutoff is clamped to (0, ~0.45*fs); Q is clamped so the
    // damping k = 1/Q stays > 0 (no true infinite self-oscillation / div-by-0).
    inline void setCoeffs(float cutoffHz, float Q)
    {
        const float nyqLimit = (float)(0.45 * mSr);
        if (cutoffHz < 5.0f) cutoffHz = 5.0f;
        else if (cutoffHz > nyqLimit) cutoffHz = nyqLimit;
        if (Q < 0.30f) Q = 0.30f;         // below ~0.3 it's overdamped mush
        else if (Q > 60.0f) Q = 60.0f;    // cap near-self-oscillation

        constexpr float kPi = 3.14159265358979323846f; // M_PI isn't defined on MSVC without _USE_MATH_DEFINES
        const float g = std::tan(kPi * cutoffHz / (float)mSr);
        const float k = 1.0f / Q;         // damping
        mK = k;
        mA1 = 1.0f / (1.0f + g * (g + k));
        mA2 = g * mA1;
        mA3 = g * mA2;
    }

    // One sample. Returns all four taps; the block picks the mode it wants.
    inline Out tick(float v0)
    {
        const float v3 = v0 - mIc2;
        const float v1 = mA1 * mIc1 + mA2 * v3;
        const float v2 = mIc2 + mA2 * mIc1 + mA3 * v3;
        mIc1 = 2.0f * v1 - mIc1;
        mIc2 = 2.0f * v2 - mIc2;

        const float lp = v2;
        const float bp = v1;               // resonant bandpass state
        const float hp = v0 - mK * v1 - v2;
        const float notch = v0 - mK * v1;  // = lp + hp
        return { lp, bp, hp, notch };
    }

    // Flush denormals in the integrator states (call once per block).
    void flushDenorms()
    {
        if (std::abs(mIc1) < 1.0e-30f) mIc1 = 0.0f;
        if (std::abs(mIc2) < 1.0e-30f) mIc2 = 0.0f;
    }

private:
    double mSr = 48000.0;
    float mA1 = 0.0f, mA2 = 0.0f, mA3 = 0.0f, mK = 1.0f;
    float mIc1 = 0.0f, mIc2 = 0.0f; // integrator states
};

} // namespace nam_rig
