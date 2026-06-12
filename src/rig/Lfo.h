#pragma once
// Lfo + FracDelayLine — shared utilities for the stereo section (ModBlock,
// DelayBlock). Header-only, no JUCE dependency: the measurement suites
// compile these with juce_audio_basics only, same as the other blocks.

#include <cmath>
#include <vector>
#include <cstdint>

namespace nam_rig
{

// Sine LFO with a phase accumulator in [0,1). Stereo offsets are read with
// value(offset01) so L/R stay phase-locked to one accumulator.
class Lfo
{
public:
    void prepare(double fs) { mFs = fs; mPhase = 0.0; setRateHz(mRateHz); }
    void reset() { mPhase = 0.0; }

    void setRateHz(float hz)
    {
        mRateHz = hz;
        mInc = (double)hz / mFs;
    }

    // -1..1 sine at (phase + offset01 cycles). Does not advance.
    float value(double offset01 = 0.0) const
    {
        return (float)std::sin(2.0 * kPi * (mPhase + offset01));
    }

    void advance()
    {
        mPhase += mInc;
        if (mPhase >= 1.0)
            mPhase -= 1.0;
    }

private:
    static constexpr double kPi = 3.14159265358979323846;
    double mFs = 48000.0, mPhase = 0.0, mInc = 0.0;
    float mRateHz = 1.0f;
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

    int maxDelay() const { return mMaxDelay; }

private:
    std::vector<float> mBuf;
    uint32_t mMask = 0, mW = 0;
    int mMaxDelay = 0;
};

} // namespace nam_rig
