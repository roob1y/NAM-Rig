#pragma once
// Biquad — RBJ cookbook filters (TDF2) shared by EqBlock (peaking bands) and
// CabBlock (post-cab Butterworth high/low cut). Includes an analytic
// magnitude evaluator so the verification harness measures the SAME
// coefficients the audio path runs — single source of truth, no copy drift.

#include <cmath>

namespace nam_rig
{

struct Biquad
{
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;

    void reset() { z1 = z2 = 0.0f; }

    inline float processSample(float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void process(float *buf, int n)
    {
        for (int i = 0; i < n; ++i)
            buf[i] = processSample(buf[i]);
        // flush denormals in the states
        if (std::abs(z1) < 1.0e-30f) z1 = 0.0f;
        if (std::abs(z2) < 1.0e-30f) z2 = 0.0f;
    }

    bool isIdentity() const
    {
        return b0 == 1.0f && b1 == 0.0f && b2 == 0.0f && a1 == 0.0f && a2 == 0.0f;
    }

    // ---- RBJ cookbook designs ----
    static Biquad identity() { return {}; }

    static Biquad peaking(double fs, double f0, double Q, double gainDb)
    {
        if (gainDb == 0.0)
            return identity();
        const double A = std::pow(10.0, gainDb / 40.0);
        const double w = 2.0 * kPi * f0 / fs;
        const double alpha = std::sin(w) / (2.0 * Q);
        const double cosw = std::cos(w);
        const double a0 = 1.0 + alpha / A;
        Biquad q;
        q.b0 = (float)((1.0 + alpha * A) / a0);
        q.b1 = (float)((-2.0 * cosw) / a0);
        q.b2 = (float)((1.0 - alpha * A) / a0);
        q.a1 = (float)((-2.0 * cosw) / a0);
        q.a2 = (float)((1.0 - alpha / A) / a0);
        return q;
    }

    // RBJ high-shelf (shelf slope S; S=1 is steepest non-resonant). Used by the
    // plate's length-scaled multiband absorptive damping (PlateFdn).
    static Biquad highshelf(double fs, double f0, double gainDb, double S = 0.7)
    {
        if (gainDb == 0.0)
            return identity();
        const double A = std::pow(10.0, gainDb / 40.0);
        const double w = 2.0 * kPi * f0 / fs;
        const double cosw = std::cos(w);
        const double alpha = std::sin(w) / 2.0 * std::sqrt((A + 1.0 / A) * (1.0 / S - 1.0) + 2.0);
        const double sa = 2.0 * std::sqrt(A) * alpha;
        const double a0 = (A + 1.0) - (A - 1.0) * cosw + sa;
        Biquad q;
        q.b0 = (float)(A * ((A + 1.0) + (A - 1.0) * cosw + sa) / a0);
        q.b1 = (float)(-2.0 * A * ((A - 1.0) + (A + 1.0) * cosw) / a0);
        q.b2 = (float)(A * ((A + 1.0) + (A - 1.0) * cosw - sa) / a0);
        q.a1 = (float)(2.0 * ((A - 1.0) - (A + 1.0) * cosw) / a0);
        q.a2 = (float)(((A + 1.0) - (A - 1.0) * cosw - sa) / a0);
        return q;
    }

    // RBJ low-shelf (shelf slope S). gainDb < 0 cuts the lows (e.g. tape's
    // one-time bass thinning at the output).
    static Biquad lowshelf(double fs, double f0, double gainDb, double S = 0.7)
    {
        if (gainDb == 0.0)
            return identity();
        const double A = std::pow(10.0, gainDb / 40.0);
        const double w = 2.0 * kPi * f0 / fs;
        const double cosw = std::cos(w);
        const double alpha = std::sin(w) / 2.0 * std::sqrt((A + 1.0 / A) * (1.0 / S - 1.0) + 2.0);
        const double sa = 2.0 * std::sqrt(A) * alpha;
        const double a0 = (A + 1.0) + (A - 1.0) * cosw + sa;
        Biquad q;
        q.b0 = (float)(A * ((A + 1.0) - (A - 1.0) * cosw + sa) / a0);
        q.b1 = (float)(2.0 * A * ((A - 1.0) - (A + 1.0) * cosw) / a0);
        q.b2 = (float)(A * ((A + 1.0) - (A - 1.0) * cosw - sa) / a0);
        q.a1 = (float)(-2.0 * ((A - 1.0) + (A + 1.0) * cosw) / a0);
        q.a2 = (float)(((A + 1.0) + (A - 1.0) * cosw - sa) / a0);
        return q;
    }

    static Biquad highpass(double fs, double f0, double Q = 0.70710678118654752)
    {
        const double w = 2.0 * kPi * f0 / fs;
        const double alpha = std::sin(w) / (2.0 * Q);
        const double cosw = std::cos(w);
        const double a0 = 1.0 + alpha;
        Biquad q;
        q.b0 = (float)(((1.0 + cosw) / 2.0) / a0);
        q.b1 = (float)((-(1.0 + cosw)) / a0);
        q.b2 = (float)(((1.0 + cosw) / 2.0) / a0);
        q.a1 = (float)((-2.0 * cosw) / a0);
        q.a2 = (float)((1.0 - alpha) / a0);
        return q;
    }

    static Biquad lowpass(double fs, double f0, double Q = 0.70710678118654752)
    {
        const double w = 2.0 * kPi * f0 / fs;
        const double alpha = std::sin(w) / (2.0 * Q);
        const double cosw = std::cos(w);
        const double a0 = 1.0 + alpha;
        Biquad q;
        q.b0 = (float)(((1.0 - cosw) / 2.0) / a0);
        q.b1 = (float)((1.0 - cosw) / a0);
        q.b2 = (float)(((1.0 - cosw) / 2.0) / a0);
        q.a1 = (float)((-2.0 * cosw) / a0);
        q.a2 = (float)((1.0 - alpha) / a0);
        return q;
    }

    // One-pole low-pass (6 dB/oct) as a biquad: y = k*x + (1-k)*y[-1]. Gentler than
    // the 2-pole lowpass -> matches a tape echo's soft HF roll-off (multi-head unit).
    static Biquad lowpass1(double fs, double f0)
    {
        const double k = 1.0 - std::exp(-2.0 * kPi * f0 / fs);
        Biquad q;
        q.b0 = (float)k; q.b1 = 0.0f; q.b2 = 0.0f;
        q.a1 = (float)(-(1.0 - k)); q.a2 = 0.0f;
        return q;
    }

    // One-pole high-pass (6 dB/oct) as a biquad: H = g(1 - z^-1)/(1 - g z^-1), g = e^{-2pi f0/fs}.
    // Blocks DC; gentler than the 2-pole highpass -> matches a tape echo's soft low-end shed
    // (the multi-head reference loses sub-bass slowly down the tail, ~-4 dB/pass at 40 Hz).
    static Biquad highpass1(double fs, double f0)
    {
        const double g = std::exp(-2.0 * kPi * f0 / fs);
        Biquad q;
        q.b0 = (float)g; q.b1 = (float)(-g); q.b2 = 0.0f;
        q.a1 = (float)(-g); q.a2 = 0.0f;
        return q;
    }

    // Analytic |H(e^{j2πf/fs})| from the live coefficients (for verification).
    double magnitudeAt(double fs, double f) const
    {
        const double w = 2.0 * kPi * f / fs;
        const double c1 = std::cos(w), s1 = std::sin(w);
        const double c2 = std::cos(2.0 * w), s2 = std::sin(2.0 * w);
        const double nr = b0 + b1 * c1 + b2 * c2, ni = -(b1 * s1 + b2 * s2);
        const double dr = 1.0 + a1 * c1 + a2 * c2, di = -(a1 * s1 + a2 * s2);
        return std::sqrt((nr * nr + ni * ni) / (dr * dr + di * di));
    }

private:
    static constexpr double kPi = 3.14159265358979323846;
};

// Post-cab high/low cut pair (Butterworth 2nd order = 12 dB/oct each).
// Convention: HPF at/below 20 Hz is OFF, LPF at/above 20 kHz is OFF — the
// extremes of the knobs mean "out of the signal path" (bit-exact).
class CutFilters
{
public:
    void prepare(double fs)
    {
        mFs = fs;
        mHpf.reset();
        mLpf.reset();
        update(mHpfHz, mLpfHz, true);
    }

    void update(double hpfHz, double lpfHz, bool force = false)
    {
        if (force || hpfHz != mHpfHz)
        {
            mHpfHz = hpfHz;
            mHpfOn = hpfHz > 20.0;
            if (mHpfOn)
                mHpf = Biquad::highpass(mFs, hpfHz);
        }
        if (force || lpfHz != mLpfHz)
        {
            mLpfHz = lpfHz;
            mLpfOn = lpfHz < 20000.0;
            if (mLpfOn)
                mLpf = Biquad::lowpass(mFs, lpfHz);
        }
    }

    void reset()
    {
        mHpf.reset();
        mLpf.reset();
    }

    bool engaged() const { return mHpfOn || mLpfOn; }

    void process(float *buf, int n)
    {
        if (mHpfOn)
            mHpf.process(buf, n);
        if (mLpfOn)
            mLpf.process(buf, n);
    }

    const Biquad &hpf() const { return mHpf; } // for verification
    const Biquad &lpf() const { return mLpf; }

private:
    Biquad mHpf, mLpf;
    double mFs = 48000.0, mHpfHz = 20.0, mLpfHz = 20000.0;
    bool mHpfOn = false, mLpfOn = false;
};

} // namespace nam_rig
