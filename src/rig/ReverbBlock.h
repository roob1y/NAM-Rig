#pragma once
// ReverbBlock — 8-line Householder FDN (post-cab): Size scales mutually-odd
// line lengths, Decay sets exact per-line gains for the target T60
// (g_i = 10^(-3 L_i / (T60 fs)) — the decay time is verifiable by
// construction), Damp is a one-pole LPF per line (HF decays faster), input
// runs predelay -> 4 series Schroeder allpass diffusers -> equal injection.
// L/R taps are orthogonal +/- combinations of the lines. Zero latency
// (predelay is effect delay). Verified by tests/reverb_test.cpp.
//
// Mono-buffer rule (left == right): mono input, left tap only.

#include "Blocks.h"
#include "Lfo.h" // FracDelayLine (integer reads here)
#include <array>
#include <algorithm>

namespace nam_rig
{

class ReverbBlock : public StereoBlock
{
public:
    static constexpr int kNumLines = 8;
    // Line lengths in ms at Size = 1.0 (mutually non-harmonic, 23..61 ms).
    static constexpr std::array<double, kNumLines> kLineMs = {
        23.0, 27.7, 31.7, 37.5, 42.8, 48.1, 54.2, 61.3};
    static constexpr std::array<double, 4> kDiffMs = {3.0, 5.7, 8.9, 12.6};
    static constexpr float kDiffG = 0.65f;
    static constexpr float kMinSize = 0.5f, kMaxSize = 1.5f;

    const char *name() const override { return "Reverb"; }

    void prepare(const BlockContext &ctx) override
    {
        mFs = ctx.sampleRate;
        for (int i = 0; i < kNumLines; ++i)
            mLine[(size_t)i].prepare((int)std::ceil(kLineMs[(size_t)i] * kMaxSize * 0.001 * mFs) + 4);
        for (size_t i = 0; i < mDiff.size(); ++i)
            mDiff[i].prepare((int)std::ceil(kDiffMs[i] * 0.001 * mFs) + 4);
        mPredelay.prepare((int)std::ceil(0.2 * mFs) + 4);
        mSmoothK = 1.0f - std::exp((float)(-1.0 / (0.010 * mFs)));
        updateGeometry(true);
        reset();
        mPrepared = true;
    }

    void reset() override
    {
        for (auto &l : mLine)
            l.reset();
        for (auto &d : mDiff)
            d.reset();
        mPredelay.reset();
        std::fill(mDampState.begin(), mDampState.end(), 0.0f);
        mMixZ = mMix;
    }

    // ---- parameters (audio thread) ----
    void setSize(float s)
    {
        s = std::clamp(s, kMinSize, kMaxSize);
        if (s != mSize)
        {
            mSize = s;
            if (mPrepared)
                updateGeometry(false);
        }
    }
    void setDecaySeconds(float t60)
    {
        t60 = std::max(0.05f, t60);
        if (t60 != mT60)
        {
            mT60 = t60;
            if (mPrepared)
                updateGeometry(false);
        }
    }
    void setDampHz(float hz)
    {
        if (hz != mDampHz)
        {
            mDampHz = hz;
            if (mPrepared)
                updateGeometry(false);
        }
    }
    void setPredelayMs(float ms) { mPredelayMs = std::clamp(ms, 0.0f, 200.0f); }
    void setMix(float m) { mMix = std::clamp(m, 0.0f, 1.0f); }

    int lineLengthSamples(int i) const { return mLen[(size_t)i]; } // verification
    float lineGain(int i) const { return mGain[(size_t)i]; }       // verification

    void process(float *left, float *right, int numSamples) override
    {
        const bool stereo = (left != right);
        const int pre = (int)std::round((double)mPredelayMs * 0.001 * mFs);

        for (int n = 0; n < numSamples; ++n)
        {
            mMixZ += mSmoothK * (mMix - mMixZ);

            const float dryL = left[n];
            const float dryR = stereo ? right[n] : dryL;

            // Mono in -> predelay -> diffusion
            mPredelay.write(0.5f * (dryL + dryR));
            float d = mPredelay.readInt(std::max(1, pre));
            for (size_t a = 0; a < mDiff.size(); ++a)
            {
                // Schroeder allpass: y = -g x + z, z delayed (x + g y)
                const float z = mDiff[a].readInt(mDiffLen[a]);
                const float y = -kDiffG * d + z;
                mDiff[a].write(d + kDiffG * y);
                d = y;
            }

            // Read all line outputs, apply damping + decay gain
            std::array<float, kNumLines> v;
            float sum = 0.0f;
            for (int i = 0; i < kNumLines; ++i)
            {
                float o = mLine[(size_t)i].readInt(mLen[(size_t)i]);
                // one-pole damping LPF inside the loop (knob at 16k = off,
                // same extremes-mean-bypass convention as the cab cuts)
                if (mDampOn)
                {
                    auto &z = mDampState[(size_t)i];
                    z += mDampK * (o - z);
                    o = z;
                }
                o *= mGain[(size_t)i];
                v[(size_t)i] = o;
                sum += o;
            }

            // Householder feedback: f = v - (2/N) sum(v)  (orthogonal, lossless)
            const float h = (2.0f / (float)kNumLines) * sum;
            const float inj = 0.25f * d;
            for (int i = 0; i < kNumLines; ++i)
                mLine[(size_t)i].write(inj + v[(size_t)i] - h);

            // Orthogonal +/- output taps
            float wetL = 0.0f, wetR = 0.0f;
            for (int i = 0; i < kNumLines; ++i)
            {
                wetL += kSignL[(size_t)i] * v[(size_t)i];
                wetR += kSignR[(size_t)i] * v[(size_t)i];
            }
            wetL *= 0.5f;
            wetR *= 0.5f;

            left[n] = (1.0f - mMixZ) * dryL + mMixZ * wetL;
            if (stereo)
                right[n] = (1.0f - mMixZ) * dryR + mMixZ * wetR;
        }

        for (auto &z : mDampState) // denormal flush between blocks
            if (std::abs(z) < 1.0e-30f)
                z = 0.0f;
    }

private:
    static constexpr std::array<float, kNumLines> kSignL = {+1, -1, +1, -1, +1, -1, +1, -1};
    static constexpr std::array<float, kNumLines> kSignR = {+1, +1, -1, -1, +1, +1, -1, -1};

    void updateGeometry(bool force)
    {
        (void)force;
        for (int i = 0; i < kNumLines; ++i)
        {
            // Odd lengths keep the lines mutually non-resonant after scaling.
            int len = (int)std::round(kLineMs[(size_t)i] * (double)mSize * 0.001 * mFs);
            len |= 1;
            mLen[(size_t)i] = std::max(3, len);
            // Exact T60: each pass of line i loses 60/T60 * (len/fs) dB.
            mGain[(size_t)i] =
                (float)std::pow(10.0, -3.0 * (double)mLen[(size_t)i] / ((double)mT60 * mFs));
        }
        for (size_t a = 0; a < mDiff.size(); ++a)
            mDiffLen[a] = std::max(2, (int)std::round(kDiffMs[a] * 0.001 * mFs));
        // One-pole LPF coefficient from cutoff; knob at/above 16 kHz = OFF
        mDampOn = mDampHz < 16000.0f;
        const double fc = std::min((double)mDampHz, 0.45 * mFs);
        mDampK = (float)(1.0 - std::exp(-2.0 * 3.14159265358979323846 * fc / mFs));
    }

    double mFs = 48000.0;
    std::array<FracDelayLine, kNumLines> mLine;
    std::array<FracDelayLine, 4> mDiff;
    FracDelayLine mPredelay;
    std::array<int, kNumLines> mLen{};
    std::array<int, 4> mDiffLen{};
    std::array<float, kNumLines> mGain{};
    std::array<float, kNumLines> mDampState{};

    float mSize = 1.0f, mT60 = 2.0f, mDampHz = 6000.0f, mPredelayMs = 20.0f, mMix = 0.25f;
    float mDampK = 1.0f, mMixZ = 0.25f, mSmoothK = 0.01f;
    bool mDampOn = true, mPrepared = false;
};

} // namespace nam_rig
