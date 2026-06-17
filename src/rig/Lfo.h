#pragma once
// Lfo + FracDelayLine — shared utilities for the stereo section (ModBlock,
// DelayBlock). Header-only, no JUCE dependency: the measurement suites
// compile these with juce_audio_basics only, same as the other blocks.

#include <cmath>
#include <vector>
#include <cstdint>

namespace nam_rig
{

// LFO with a phase accumulator in [0,1). Stereo offsets are read with
// value(offset01) so L/R stay phase-locked to one accumulator. Waveform
// defaults to Sine; DelayBlock never changes it, so its behaviour is byte
// identical to the original sine-only LFO.
class Lfo
{
public:
    enum Wave { Sine = 0, Triangle, Square, SampleHold };

    void prepare(double fs) { mFs = fs; mPhase = 0.0; setRateHz(mRateHz); }
    void reset()
    {
        mPhase = 0.0;
        mHeld = 0.0f;
        mRng = 0x2545F491u;
    }

    void setRateHz(float hz)
    {
        mRateHz = hz;
        mInc = (double)hz / mFs;
    }
    void setWaveform(int w) { mWave = (Wave)w; }

    // -1..1 at (phase + offset01 cycles). Does not advance.
    float value(double offset01 = 0.0) const
    {
        if (mWave == Sine) // exact original path (DelayBlock relies on this)
            return (float)std::sin(2.0 * kPi * (mPhase + offset01));
        if (mWave == SampleHold)
            return mHeld; // stepped: stereo offset is moot
        double p = mPhase + offset01;
        p -= std::floor(p); // wrap to [0,1)
        if (mWave == Triangle)
            return (float)(1.0 - 4.0 * std::abs(p - 0.5)); // p0:-1 p.5:+1
        return p < 0.5 ? 1.0f : -1.0f;                     // Square
    }

    void advance()
    {
        mPhase += mInc;
        if (mPhase >= 1.0)
        {
            mPhase -= 1.0;
            // refresh the sample-and-hold value once per cycle (LCG; only used
            // by the SampleHold waveform, harmless otherwise).
            mRng = mRng * 1664525u + 1013904223u;
            mHeld = (float)((double)(mRng >> 8) / (double)(1u << 24)) * 2.0f - 1.0f;
        }
    }

private:
    static constexpr double kPi = 3.14159265358979323846;
    double mFs = 48000.0, mPhase = 0.0, mInc = 0.0;
    float mRateHz = 1.0f;
    Wave mWave = Sine;
    float mHeld = 0.0f;
    uint32_t mRng = 0x2545F491u;
};

// Fractional delay line, 4-point Hermite interpolation. Power-of-two ring.
// write() then read: delaySamples is relative to the just-written sample,
// so feedback loops must use delaySamples >= 2 (Hermite reads d-1..d+2).
class FracDelayLine
{
public:
    void prepare(int maxDelaySamples)
    {
        int size = 16;
        while (size < maxDelaySamples + 8)
            size <<= 1;
        mBuf.assign((size_t)size, 0.0f);
        mMask = (uint32_t)size - 1;
        mW = 0;
        mMaxDelay = maxDelaySamples;
    }

    void reset() { std::fill(mBuf.begin(), mBuf.end(), 0.0f); }

    void write(float x)
    {
        mW = (mW + 1) & mMask;
        mBuf[mW] = x;
    }

    float readInt(int delaySamples) const
    {
        return mBuf[(mW - (uint32_t)delaySamples) & mMask];
    }

    float readFrac(double delaySamples) const
    {
        const int di = (int)delaySamples;
        const float t = (float)(delaySamples - di);
        // Hermite needs the sample after the integer tap: clamp inside buffer.
        const float xm1 = readInt(di - 1);
        const float x0  = readInt(di);
        const float x1  = readInt(di + 1);
        const float x2  = readInt(di + 2);
        const float c = (x1 - xm1) * 0.5f;
        const float v = x0 - x1;
        const float w = c + v;
        const float a = w + v + (x2 - x0) * 0.5f;
        const float b = w + a;
        return ((((a * t) - b) * t + c) * t + x0);
    }

    // 6-point, 5th-order Lagrange fractional read. Flatter magnitude than the
    // 4-point Hermite readFrac (reproduces cubics exactly, not just quadratics),
    // and being FIR it is a continuous function of the fractional delay -- no
    // recursive state -- so a swept delay (chorus / vibrato) never clicks at the
    // integer-sample crossings that make a recursive all-pass interpolator glitch.
    // Reads taps di-2 .. di+3, so keep delaySamples >= 2; prepare's +8 ring guard
    // already covers di+3.
    float readFrac6(double delaySamples) const
    {
        const int di = (int)delaySamples;
        const float t = (float)(delaySamples - di);
        const float xm2 = readInt(di - 2);
        const float xm1 = readInt(di - 1);
        const float x0  = readInt(di);
        const float x1  = readInt(di + 1);
        const float x2  = readInt(di + 2);
        const float x3  = readInt(di + 3);
        // Lagrange basis factors for offsets {-2,-1,0,1,2,3} at position t; each
        // tap's coefficient is the product of all factors EXCEPT its own, over a
        // fixed denominator (the product of node spacings).
        const float tm2 = t + 2.0f, tm1 = t + 1.0f, t0 = t,
                    t1 = t - 1.0f, t2 = t - 2.0f, t3 = t - 3.0f;
        const float c_m2 = (tm1 * t0 * t1 * t2 * t3) * (-1.0f / 120.0f);
        const float c_m1 = (tm2 * t0 * t1 * t2 * t3) * ( 1.0f / 24.0f);
        const float c_0  = (tm2 * tm1 * t1 * t2 * t3) * (-1.0f / 12.0f);
        const float c_1  = (tm2 * tm1 * t0 * t2 * t3) * ( 1.0f / 12.0f);
        const float c_2  = (tm2 * tm1 * t0 * t1 * t3) * (-1.0f / 24.0f);
        const float c_3  = (tm2 * tm1 * t0 * t1 * t2) * ( 1.0f / 120.0f);
        return c_m2 * xm2 + c_m1 * xm1 + c_0 * x0 + c_1 * x1 + c_2 * x2 + c_3 * x3;
    }

    int maxDelay() const { return mMaxDelay; }

private:
    std::vector<float> mBuf;
    uint32_t mMask = 0, mW = 0;
    int mMaxDelay = 0;
};

} // namespace nam_rig
