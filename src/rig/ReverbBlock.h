#pragma once
// ReverbBlock — a typed CHARACTER reverb (post-cab, stereo). Like ModBlock, each
// character is voiced like the real thing rather than being one generic engine,
// and the UI shows only the controls a character actually has; everything else is
// hardwired to that character's sweet spot, so you can't dial a bad sound.
//
// FOUR engines render the WET signal for seven characters:
//   - Modulated Householder FDN  -> Room, Hall, Ambience, Bloom
//   - Dattorro figure-8 plate     -> Plate
//   - Dispersive allpass spring   -> Spring
//   - Octave-up feedback shimmer  -> Shimmer (selectable pitch interval)
//
// Engines are pure WET generators: process() overwrites the buffer with the wet
// signal (reading the dry as excitation first). ReverbBlock owns the dry copy and
// runs every character through ONE shared guardrail stage (GuardMixer) so the
// foolproofing is identical everywhere:
//   - wet low-cut    : hardwired HPF on the wet path (kills low-end mud)
//   - auto-makeup    : holds perceived wet level constant as Decay/Size change
//   - ducking        : wet steps back under playing, blooms in the gaps
//   - width          : global M/S stereo width
//   - bloom swell    : slow attack on the wet (Bloom only)
//   - freeze         : infinite sustain (engines set feedback->1, input->0)
//
// Voicing introspection (sizeExposed/...) is public + static so the UI, the audio
// thread and the tests share one source of truth. Zero latency (predelay is
// effect delay). Mono-buffer rule (left == right): mono in, mono wet on left.
//
// Verified by tests/reverb_test.cpp (engine DSP + the guardrail behaviours).

#include "Blocks.h"
#include "Lfo.h" // FracDelayLine + Lfo
#include <array>
#include <vector>
#include <algorithm>
#include <cmath>

namespace nam_rig
{

// ---------------------------------------------------------------------------
// small shared helpers
// ---------------------------------------------------------------------------
namespace reverb_detail
{
constexpr double kPi = 3.14159265358979323846;

inline float onePole(double hz, double fs)
{
    const double fc = std::min(std::max(hz, 1.0), 0.45 * fs);
    return (float)(1.0 - std::exp(-2.0 * kPi * fc / fs));
}

// delay-line Schroeder allpass step (fixed integer length). Reads BEFORE write.
inline float allpassInt(FracDelayLine &line, int len, float g, float x)
{
    const float z = line.readInt(len);
    const float y = -g * x + z;
    line.write(x + g * y);
    return y;
}

// first-order allpass section (no delay line). Used by the spring cascade.
struct Ap1
{
    float x1 = 0.0f, y1 = 0.0f;
    inline float process(float a, float x)
    {
        const float y = a * x + x1 - a * y1;
        x1 = x;
        y1 = y;
        return y;
    }
    void reset() { x1 = y1 = 0.0f; }
};

inline void flush(float &z) { if (std::abs(z) < 1.0e-30f) z = 0.0f; }
} // namespace reverb_detail

// ===========================================================================
// FdnReverb — modulated 8-line Householder FDN (Room/Hall/Ambience/Bloom).
// Renders WET only. Freeze sets line gains to 1 and stops input injection.
// ===========================================================================
class FdnReverb
{
public:
    static constexpr int kNumLines = 8;
    static constexpr std::array<double, kNumLines> kLineMs = {
        23.0, 27.7, 31.7, 37.5, 42.8, 48.1, 54.2, 61.3};
    static constexpr std::array<double, 4> kDiffMs = {3.0, 5.7, 8.9, 12.6};
    static constexpr float kMinSize = 0.5f, kMaxSize = 1.5f;
    static constexpr double kModMaxMs = 2.0;
    static constexpr float kModRateHz = 0.7f;

    void prepare(double fs)
    {
        mFs = fs;
        const double headroom = kModMaxMs * 0.001 * mFs + 8.0;
        for (int i = 0; i < kNumLines; ++i)
            mLine[(size_t)i].prepare((int)std::ceil(kLineMs[(size_t)i] * kMaxSize * 0.001 * mFs + headroom));
        for (size_t i = 0; i < mDiff.size(); ++i)
            mDiff[i].prepare((int)std::ceil(kDiffMs[i] * 0.001 * mFs) + 8);
        mPredelay.prepare((int)std::ceil(0.2 * mFs) + 8);
        mLfo.prepare(mFs);
        mLfo.setRateHz(kModRateHz);
        updateGeometry();
        reset();
        mPrepared = true;
    }

    void reset()
    {
        for (auto &l : mLine) l.reset();
        for (auto &d : mDiff) d.reset();
        mPredelay.reset();
        std::fill(mDampState.begin(), mDampState.end(), 0.0f);
        mLfo.reset();
    }

    void setSize(float s) { s = std::clamp(s, kMinSize, kMaxSize); if (s != mSize) { mSize = s; if (mPrepared) updateGeometry(); } }
    void setDecaySeconds(float t60) { t60 = std::max(0.05f, t60); if (t60 != mT60) { mT60 = t60; if (mPrepared) updateGeometry(); } }
    void setDampHz(float hz) { if (hz != mDampHz) { mDampHz = hz; if (mPrepared) updateGeometry(); } }
    void setPredelayMs(float ms) { mPredelayMs = std::clamp(ms, 0.0f, 200.0f); }
    void setModDepth(float d) { mMod = std::clamp(d, 0.0f, 1.0f); }
    void setDiffusion(float g) { mDiffG = std::clamp(g, 0.0f, 0.8f); }
    void setFreeze(bool f) { mFreeze = f; }

    int lineLengthSamples(int i) const { return mLen[(size_t)i]; }
    float lineGain(int i) const { return mGain[(size_t)i]; }

    // Overwrites left/right with the WET signal.
    void process(float *left, float *right, int numSamples)
    {
        using namespace reverb_detail;
        const bool stereo = (left != right);
        const int pre = (int)std::round((double)mPredelayMs * 0.001 * mFs);
        const bool modOn = mMod > 0.0f && !mFreeze;
        const float modSamp = (float)(mMod * kModMaxMs * 0.001 * mFs);
        const float inGain = mFreeze ? 0.0f : 1.0f; // freeze: stop new input

        for (int n = 0; n < numSamples; ++n)
        {
            const float dryL = left[n];
            const float dryR = stereo ? right[n] : dryL;

            mPredelay.write(0.5f * (dryL + dryR));
            float d = mPredelay.readInt(std::max(1, pre));
            for (size_t a = 0; a < mDiff.size(); ++a)
                d = allpassInt(mDiff[a], mDiffLen[a], mDiffG, d);

            std::array<float, kNumLines> v;
            float sum = 0.0f;
            for (int i = 0; i < kNumLines; ++i)
            {
                float o;
                if (modOn)
                {
                    const float m = modSamp * mLfo.value((double)i * 0.13);
                    o = mLine[(size_t)i].readFrac((double)mLen[(size_t)i] + (double)m);
                }
                else
                {
                    o = mLine[(size_t)i].readInt(mLen[(size_t)i]);
                }
                if (mDampOn && !mFreeze)
                {
                    auto &z = mDampState[(size_t)i];
                    z += mDampK * (o - z);
                    o = z;
                }
                o *= mFreeze ? 1.0f : mGain[(size_t)i]; // freeze: lossless recirculation
                v[(size_t)i] = o;
                sum += o;
            }

            const float h = (2.0f / (float)kNumLines) * sum;
            const float inj = inGain * 0.25f * d;
            for (int i = 0; i < kNumLines; ++i)
                mLine[(size_t)i].write(inj + v[(size_t)i] - h);

            float wetL = 0.0f, wetR = 0.0f;
            for (int i = 0; i < kNumLines; ++i)
            {
                wetL += kSignL[(size_t)i] * v[(size_t)i];
                wetR += kSignR[(size_t)i] * v[(size_t)i];
            }
            left[n] = 0.5f * wetL;
            if (stereo)
                right[n] = 0.5f * wetR;
            if (modOn)
                mLfo.advance();
        }
        for (auto &z : mDampState) reverb_detail::flush(z);
    }

private:
    static constexpr std::array<float, kNumLines> kSignL = {+1, -1, +1, -1, +1, -1, +1, -1};
    static constexpr std::array<float, kNumLines> kSignR = {+1, +1, -1, -1, +1, +1, -1, -1};

    void updateGeometry()
    {
        for (int i = 0; i < kNumLines; ++i)
        {
            int len = (int)std::round(kLineMs[(size_t)i] * (double)mSize * 0.001 * mFs);
            len |= 1;
            mLen[(size_t)i] = std::max(3, len);
            mGain[(size_t)i] = (float)std::pow(10.0, -3.0 * (double)mLen[(size_t)i] / ((double)mT60 * mFs));
        }
        for (size_t a = 0; a < mDiff.size(); ++a)
            mDiffLen[a] = std::max(2, (int)std::round(kDiffMs[a] * 0.001 * mFs));
        mDampOn = mDampHz < 16000.0f;
        mDampK = reverb_detail::onePole(mDampHz, mFs);
    }

    double mFs = 48000.0;
    std::array<FracDelayLine, kNumLines> mLine;
    std::array<FracDelayLine, 4> mDiff;
    FracDelayLine mPredelay;
    std::array<int, kNumLines> mLen{};
    std::array<int, 4> mDiffLen{};
    std::array<float, kNumLines> mGain{};
    std::array<float, kNumLines> mDampState{};
    Lfo mLfo;
    float mSize = 1.0f, mT60 = 2.0f, mDampHz = 6000.0f, mPredelayMs = 20.0f;
    float mMod = 0.0f, mDiffG = 0.65f, mDampK = 1.0f;
    bool mDampOn = true, mPrepared = false, mFreeze = false;
};

// ===========================================================================
// PlateReverb — Dattorro figure-8 plate (Plate). Renders WET only.
// ===========================================================================
class PlateReverb
{
public:
    void prepare(double fs)
    {
        mFs = fs;
        auto ms = [&](double s) { return s / 29761.0 * 1000.0; };
        for (int i = 0; i < 4; ++i) mIn[(size_t)i].prepare(samp(ms(kInS[(size_t)i])) + 8);
        mAp1[0].prepare(samp(ms(672)) + 64); mAp1[1].prepare(samp(ms(908)) + 64);
        mDelA[0].prepare(samp(ms(4453)) + 8); mDelA[1].prepare(samp(ms(4217)) + 8);
        mAp2[0].prepare(samp(ms(1800)) + 8); mAp2[1].prepare(samp(ms(2656)) + 8);
        mDelB[0].prepare(samp(ms(3720)) + 8); mDelB[1].prepare(samp(ms(3163)) + 8);
        mPredelay.prepare(samp(200.0) + 8);
        mLfo.prepare(mFs); mLfo.setRateHz(1.1f);
        for (int i = 0; i < 4; ++i) mInLen[(size_t)i] = samp(ms(kInS[(size_t)i]));
        mAp1Len[0] = ms(672); mAp1Len[1] = ms(908);
        mDelALen[0] = samp(ms(4453)); mDelALen[1] = samp(ms(4217));
        mAp2Len[0] = samp(ms(1800)); mAp2Len[1] = samp(ms(2656));
        mDelBLen[0] = samp(ms(3720)); mDelBLen[1] = samp(ms(3163));
        const double tapsL[7] = {266, 2974, 1913, 1996, 1990, 187, 1066};
        const double tapsR[7] = {353, 3627, 1228, 2673, 2111, 335, 121};
        for (int i = 0; i < 7; ++i) { mTapL[(size_t)i] = samp(ms(tapsL[i])); mTapR[(size_t)i] = samp(ms(tapsR[i])); }
        reset(); mPrepared = true;
    }

    void reset()
    {
        for (auto &l : mIn) l.reset();
        for (auto &l : mAp1) l.reset();
        for (auto &l : mDelA) l.reset();
        for (auto &l : mAp2) l.reset();
        for (auto &l : mDelB) l.reset();
        mPredelay.reset();
        mBw = 0.0f; mDamp[0] = mDamp[1] = 0.0f; mTank[0] = mTank[1] = 0.0f;
        mLfo.reset();
    }

    void setDecaySeconds(float t60) { mT60 = std::max(0.1f, t60); mDirty = true; }
    void setDampHz(float hz) { mDampHz = hz; mDirty = true; }
    void setPredelayMs(float ms) { mPredelayMs = std::clamp(ms, 0.0f, 200.0f); }
    void setModDepth(float d) { mMod = std::clamp(d, 0.0f, 1.0f); }
    void setFreeze(bool f) { mFreeze = f; }

    void process(float *left, float *right, int numSamples)
    {
        using namespace reverb_detail;
        if (mDirty) recompute();
        const bool stereo = (left != right);
        const int pre = (int)std::round((double)mPredelayMs * 0.001 * mFs);
        const float excSamp = mFreeze ? 0.0f : (float)(mMod * 0.0008 * mFs);
        const float decay = mFreeze ? 1.0f : mDecay;
        const float inGain = mFreeze ? 0.0f : 1.0f;

        for (int n = 0; n < numSamples; ++n)
        {
            const float dryL = left[n];
            const float dryR = stereo ? right[n] : dryL;

            mPredelay.write(0.5f * (dryL + dryR));
            float x = inGain * mPredelay.readInt(std::max(1, pre));
            mBw += mBwCoef * (x - mBw); x = mBw;
            for (int i = 0; i < 4; ++i)
                x = allpassInt(mIn[(size_t)i], mInLen[(size_t)i], kInG[(size_t)i], x);

            const float modL = excSamp * mLfo.value(0.0);
            const float modR = excSamp * mLfo.value(0.25);
            float a = x + decay * mTank[1];
            {
                const float z = mAp1[0].readFrac((double)mAp1Len[0] + (double)modL);
                const float y = -kDecayDiff1 * a + z;
                mAp1[0].write(a + kDecayDiff1 * y); a = y;
                mDelA[0].write(a); a = mDelA[0].readInt(mDelALen[0]);
                if (!mFreeze) { mDamp[0] += mDampK * (a - mDamp[0]); a = mDamp[0]; }
                a *= decay;
                a = allpassInt(mAp2[0], mAp2Len[0], -kDecayDiff2, a);
                mDelB[0].write(a); mTank[0] = mDelB[0].readInt(mDelBLen[0]);
            }
            float b = x + decay * mTank[0];
            {
                const float z = mAp1[1].readFrac((double)mAp1Len[1] + (double)modR);
                const float y = -kDecayDiff1 * b + z;
                mAp1[1].write(b + kDecayDiff1 * y); b = y;
                mDelA[1].write(b); b = mDelA[1].readInt(mDelALen[1]);
                if (!mFreeze) { mDamp[1] += mDampK * (b - mDamp[1]); b = mDamp[1]; }
                b *= decay;
                b = allpassInt(mAp2[1], mAp2Len[1], -kDecayDiff2, b);
                mDelB[1].write(b); mTank[1] = mDelB[1].readInt(mDelBLen[1]);
            }

            const float wetL = 0.6f * (mDelA[1].readInt(mTapL[0]) + mDelA[1].readInt(mTapL[1])
                                       - mAp2[1].readInt(mTapL[2]) + mDelB[1].readInt(mTapL[3])
                                       - mDelA[0].readInt(mTapL[4]) - mAp2[0].readInt(mTapL[5])
                                       - mDelB[0].readInt(mTapL[6]));
            const float wetR = 0.6f * (mDelA[0].readInt(mTapR[0]) + mDelA[0].readInt(mTapR[1])
                                       - mAp2[0].readInt(mTapR[2]) + mDelB[0].readInt(mTapR[3])
                                       - mDelA[1].readInt(mTapR[4]) - mAp2[1].readInt(mTapR[5])
                                       - mDelB[1].readInt(mTapR[6]));
            left[n] = wetL;
            if (stereo) right[n] = wetR;
            mLfo.advance();
        }
        flush(mBw); flush(mDamp[0]); flush(mDamp[1]); flush(mTank[0]); flush(mTank[1]);
    }

private:
    int samp(double ms) const { return std::max(1, (int)std::round(ms * 0.001 * mFs)); }
    void recompute()
    {
        using namespace reverb_detail;
        const double loopMs = (mDelALen[0] + mAp2Len[0] + mDelBLen[0]) / mFs * 1000.0;
        mDecay = (float)std::clamp(std::pow(10.0, -3.0 * loopMs / ((double)mT60 * 1000.0)), 0.0, 0.95);
        mDampK = onePole(mDampHz, mFs);
        mBwCoef = onePole(11000.0, mFs);
        mDirty = false;
    }

    double mFs = 48000.0;
    static constexpr double kInS[4] = {142, 107, 379, 277};
    static constexpr float kInG[4] = {0.75f, 0.75f, 0.625f, 0.625f};
    static constexpr float kDecayDiff1 = 0.70f, kDecayDiff2 = 0.50f;
    std::array<FracDelayLine, 4> mIn;
    std::array<FracDelayLine, 2> mAp1, mDelA, mAp2, mDelB;
    FracDelayLine mPredelay;
    std::array<int, 4> mInLen{};
    std::array<double, 2> mAp1Len{};
    std::array<int, 2> mDelALen{}, mAp2Len{}, mDelBLen{};
    std::array<int, 7> mTapL{}, mTapR{};
    Lfo mLfo;
    float mBw = 0.0f, mBwCoef = 0.0f;
    std::array<float, 2> mDamp{}, mTank{};
    float mDecay = 0.5f, mDampK = 1.0f;
    float mT60 = 2.0f, mDampHz = 7000.0f, mPredelayMs = 10.0f, mMod = 0.3f;
    bool mPrepared = false, mDirty = true, mFreeze = false;
};

// ===========================================================================
// SpringReverb — dispersive allpass model (Spring). Renders WET only.
// ===========================================================================
class SpringReverb
{
public:
    static constexpr int kSprings = 2;
    static constexpr int kMaxStages = 120;

    void prepare(double fs)
    {
        mFs = fs;
        const double loopMs[kSprings] = {34.0, 41.0};
        for (int s = 0; s < kSprings; ++s)
        {
            mLoop[(size_t)s].prepare((int)std::ceil(loopMs[s] * 0.001 * mFs) + 8);
            mLoopLen[(size_t)s] = std::max(2, (int)std::round(loopMs[s] * 0.001 * mFs));
        }
        mPredelay.prepare((int)std::ceil(0.05 * mFs) + 8);
        reset(); mPrepared = true;
    }

    void reset()
    {
        for (auto &l : mLoop) l.reset();
        for (auto &chain : mAp) for (auto &ap : chain) ap.reset();
        mPredelay.reset();
        mSpringZ[0] = mSpringZ[1] = 0.0f; mLp[0] = mLp[1] = 0.0f; mHp[0] = mHp[1] = 0.0f;
    }

    void setDecaySeconds(float t60) { mT60 = std::max(0.1f, t60); mDirty = true; }
    void setDampHz(float hz) { mDampHz = hz; mDirty = true; }
    void setTension(float t) { mTension = std::clamp(t, 0.0f, 1.0f); mDirty = true; }
    void setFreeze(bool f) { mFreeze = f; }

    void process(float *left, float *right, int numSamples)
    {
        using namespace reverb_detail;
        if (mDirty) recompute();
        const bool stereo = (left != right);
        const float fb = mFreeze ? 0.999f : mFb;
        const float inGain = mFreeze ? 0.0f : 1.0f;
        for (int n = 0; n < numSamples; ++n)
        {
            const float dryL = left[n];
            const float dryR = stereo ? right[n] : dryL;
            mPredelay.write(0.5f * (dryL + dryR));
            const float in = inGain * mPredelay.readInt(std::max(1, mPreLen));

            float out[kSprings];
            for (int s = 0; s < kSprings; ++s)
            {
                float v = in + fb * mSpringZ[(size_t)s];
                for (int k = 0; k < mStages; ++k)
                    v = mAp[(size_t)s][(size_t)k].process(mApA, v);
                mLoop[(size_t)s].write(v);
                float d = mLoop[(size_t)s].readInt(mLoopLen[(size_t)s]);
                if (!mFreeze) { mLp[(size_t)s] += mDampK * (d - mLp[(size_t)s]); d = mLp[(size_t)s]; }
                const float hpIn = d;
                mHp[(size_t)s] += mHpK * (hpIn - mHp[(size_t)s]);
                d = hpIn - mHp[(size_t)s];
                mSpringZ[(size_t)s] = d;
                out[s] = d;
            }
            left[n] = 1.1f * out[0] + 0.35f * out[1];
            if (stereo) right[n] = 0.35f * out[0] + 1.1f * out[1];
        }
        for (int s = 0; s < kSprings; ++s) { flush(mSpringZ[(size_t)s]); flush(mLp[(size_t)s]); flush(mHp[(size_t)s]); }
    }

private:
    void recompute()
    {
        using namespace reverb_detail;
        mPreLen = std::max(1, (int)std::round(0.004 * mFs));
        mStages = std::clamp((int)std::round(60 + mTension * 60), 8, kMaxStages);
        mApA = 0.5f + 0.28f * mTension;
        const double avgMs = (double)(mLoopLen[0] + mLoopLen[1]) * 0.5 / mFs * 1000.0;
        mFb = (float)std::clamp(std::pow(10.0, -3.0 * avgMs / ((double)mT60 * 1000.0)), 0.0, 0.97);
        mDampK = onePole((double)mDampHz, mFs);
        mHpK = onePole(120.0, mFs);
        mDirty = false;
    }

    double mFs = 48000.0;
    std::array<FracDelayLine, kSprings> mLoop;
    std::array<std::array<reverb_detail::Ap1, kMaxStages>, kSprings> mAp;
    FracDelayLine mPredelay;
    std::array<int, kSprings> mLoopLen{};
    std::array<float, kSprings> mSpringZ{}, mLp{}, mHp{};
    int mPreLen = 1, mStages = 80;
    float mApA = 0.62f, mFb = 0.8f, mDampK = 1.0f, mHpK = 0.01f;
    float mT60 = 2.0f, mDampHz = 4500.0f, mTension = 0.5f;
    bool mPrepared = false, mDirty = true, mFreeze = false;
};

// ===========================================================================
// ShimmerReverb — compact modulated FDN + octave-up pitch shifter in feedback
// (Shimmer). Pitch interval is selectable. Renders WET only.
// ===========================================================================
class ShimmerReverb
{
public:
    static constexpr int kLines = 4;
    static constexpr std::array<double, kLines> kLineMs = {37.0, 43.0, 47.0, 53.0};
    enum Pitch { kOctaveUp = 0, kTwoOctaves, kFifthPlusOctave };

    void prepare(double fs)
    {
        mFs = fs;
        for (int i = 0; i < kLines; ++i)
            mLine[(size_t)i].prepare((int)std::ceil(kLineMs[(size_t)i] * 0.001 * mFs) + 16);
        mPredelay.prepare((int)std::ceil(0.2 * mFs) + 8);
        mWindow = (int)std::round(0.045 * mFs);
        mShift.prepare(mWindow * 2 + 16);
        mLfo.prepare(mFs); mLfo.setRateHz(0.6f);
        updateGeometry();
        reset(); mPrepared = true;
    }

    void reset()
    {
        for (auto &l : mLine) l.reset();
        mPredelay.reset(); mShift.reset();
        std::fill(mDampState.begin(), mDampState.end(), 0.0f);
        mGrain[0] = 0.0; mGrain[1] = 0.0; mShimZ = 0.0f;
        mLfo.reset();
    }

    void setDecaySeconds(float t60) { mT60 = std::max(0.2f, t60); if (mPrepared) updateGeometry(); }
    void setDampHz(float hz) { mDampHz = hz; if (mPrepared) updateGeometry(); }
    void setPredelayMs(float ms) { mPredelayMs = std::clamp(ms, 0.0f, 200.0f); }
    void setShimmer(float s) { mShimmer = std::clamp(s, 0.0f, 1.0f); }
    void setModDepth(float d) { mMod = std::clamp(d, 0.0f, 1.0f); }
    void setPitch(int p) { mPitch = (Pitch)std::clamp(p, 0, 2); }
    void setFreeze(bool f) { mFreeze = f; }

    void process(float *left, float *right, int numSamples)
    {
        using namespace reverb_detail;
        const bool stereo = (left != right);
        const int pre = (int)std::round((double)mPredelayMs * 0.001 * mFs);
        const float modSamp = mFreeze ? 0.0f : (float)(mMod * 1.5 * 0.001 * mFs);
        const double W = (double)mWindow;
        const float inGain = mFreeze ? 0.0f : 1.0f;

        // pitch voices: {grain increment (=ratio-1), gain}
        double inc0 = 1.0, inc1 = 0.0; float g0 = 1.0f, g1 = 0.0f;
        if (mPitch == kTwoOctaves) { inc0 = 3.0; g0 = 1.0f; }
        else if (mPitch == kFifthPlusOctave) { inc0 = 0.5; g0 = 0.6f; inc1 = 1.0; g1 = 0.6f; }

        for (int n = 0; n < numSamples; ++n)
        {
            const float dryL = left[n];
            const float dryR = stereo ? right[n] : dryL;
            mPredelay.write(0.5f * (dryL + dryR));
            const float in = inGain * mPredelay.readInt(std::max(1, pre));

            std::array<float, kLines> v;
            float sum = 0.0f;
            for (int i = 0; i < kLines; ++i)
            {
                const float m = modSamp * mLfo.value((double)i * 0.21);
                float o = mLine[(size_t)i].readFrac((double)mLen[(size_t)i] + (double)m);
                auto &z = mDampState[(size_t)i];
                if (!mFreeze) { z += mDampK * (o - z); o = z; }
                o *= mFreeze ? 1.0f : mGain[(size_t)i];
                v[(size_t)i] = o;
                sum += o;
            }
            const float h = 0.5f * sum;

            const float wet = 0.5f * sum;
            mShift.write(wet);
            auto grainTap = [&](double &grain, double inc) -> float {
                grain += inc;
                if (grain >= W) grain -= W;
                const double p1 = grain, p2 = (grain + W * 0.5 >= W) ? grain - W * 0.5 : grain + W * 0.5;
                const float s1 = mShift.readFrac(2.0 + (W - p1));
                const float s2 = mShift.readFrac(2.0 + (W - p2));
                const float w1 = 0.5f - 0.5f * (float)std::cos(2.0 * kPi * p1 / W);
                const float w2 = 0.5f - 0.5f * (float)std::cos(2.0 * kPi * p2 / W);
                return w1 * s1 + w2 * s2; // w1+w2 == 1
            };
            float pitched = g0 * grainTap(mGrain[0], inc0);
            if (g1 > 0.0f) pitched += g1 * grainTap(mGrain[1], inc1);
            mShimZ += mShimCoef * (pitched - mShimZ);
            pitched = mShimZ;

            const float fb = std::tanh(mShimmer * 0.9f * pitched); // self-limiting + sheen
            const float inject = in + fb;
            for (int i = 0; i < kLines; ++i)
                mLine[(size_t)i].write(0.5f * inject + v[(size_t)i] - h);

            const float wetL = v[0] - v[1] + v[2] - v[3];
            const float wetR = v[0] + v[1] - v[2] - v[3];
            left[n] = 0.5f * wetL;
            if (stereo) right[n] = 0.5f * wetR;
            mLfo.advance();
        }
        for (auto &z : mDampState) reverb_detail::flush(z);
        reverb_detail::flush(mShimZ);
    }

private:
    static constexpr double kPi = reverb_detail::kPi;
    void updateGeometry()
    {
        for (int i = 0; i < kLines; ++i)
        {
            int len = (int)std::round(kLineMs[(size_t)i] * 0.001 * mFs);
            len |= 1;
            mLen[(size_t)i] = std::max(3, len);
            mGain[(size_t)i] = (float)std::pow(10.0, -3.0 * (double)mLen[(size_t)i] / ((double)mT60 * mFs));
        }
        mDampK = reverb_detail::onePole(mDampHz, mFs);
        mShimCoef = reverb_detail::onePole(6000.0, mFs);
    }

    double mFs = 48000.0;
    std::array<FracDelayLine, kLines> mLine;
    FracDelayLine mPredelay, mShift;
    std::array<int, kLines> mLen{};
    std::array<float, kLines> mGain{}, mDampState{};
    Lfo mLfo;
    int mWindow = 2048;
    std::array<double, 2> mGrain{0.0, 0.0};
    float mShimZ = 0.0f, mShimCoef = 0.1f;
    Pitch mPitch = kOctaveUp;
    float mT60 = 4.0f, mDampHz = 7000.0f, mPredelayMs = 30.0f, mMix = 0.3f, mShimmer = 0.5f, mMod = 0.4f;
    float mDampK = 1.0f;
    bool mPrepared = false, mFreeze = false;
};

// ===========================================================================
// GuardMixer — the shared foolproofing stage. Takes the dry (saved) and the wet
// (engine output) and produces the final mix, applying per-character guardrails
// so no setting sounds bad. Stateful (per channel) but config is pushed per block.
// ===========================================================================
class GuardMixer
{
public:
    struct Config
    {
        float mix = 0.25f;     // wet/dry
        float hpfHz = 120.0f;  // hardwired wet low-cut
        float makeup = 1.0f;   // auto level
        float duckAmt = 0.3f;  // 0..1 ducking depth
        float width = 1.0f;    // 0 = mono, 1 = full
        float swellRate = 0.0f;// >0 = Bloom slow attack (per-sample rise)
        bool freeze = false;
    };

    void prepare(double fs)
    {
        mFs = fs;
        mSmoothK = 1.0f - std::exp((float)(-1.0 / (0.010 * mFs)));
        mDuckAtt = reverb_detail::onePole(60.0, mFs);   // ~fast attack on the envelope
        mDuckRel = reverb_detail::onePole(2.5, mFs);    // slow release
        mSwellRel = reverb_detail::onePole(0.12, mFs);  // bloom tail rings out slowly
        reset();
    }

    void reset()
    {
        mHpL = mHpR = 0.0f; mDuckEnv = 0.0f; mSwell = 0.0f;
        mMixZ = 0.0f; mMakeupZ = 1.0f;
    }

    void snapMix(float mix) { mMixZ = mix; }

    // Process in place: dry in scratchL/R, wet in left/right -> final in left/right.
    void process(float *left, float *right, const float *dryL, const float *dryR,
                 int numSamples, bool stereo, const Config &c)
    {
        const float hpK = reverb_detail::onePole(c.hpfHz, mFs);
        for (int n = 0; n < numSamples; ++n)
        {
            mMixZ += mSmoothK * (c.mix - mMixZ);
            mMakeupZ += mSmoothK * (c.makeup - mMakeupZ);

            float wL = left[n];
            float wR = stereo ? right[n] : wL;

            // wet low-cut (one-pole high-pass), per channel
            mHpL += hpK * (wL - mHpL); wL -= mHpL;
            mHpR += hpK * (wR - mHpR); wR -= mHpR;

            // auto-makeup
            wL *= mMakeupZ; wR *= mMakeupZ;

            // width (M/S)
            const float mid = 0.5f * (wL + wR);
            const float side = 0.5f * (wL - wR) * c.width;
            wL = mid + side; wR = mid - side;

            // ducking from the dry envelope (frozen tails are never ducked)
            const float rect = std::max(std::abs(dryL[n]), std::abs(dryR[n]));
            const float k = rect > mDuckEnv ? mDuckAtt : mDuckRel;
            mDuckEnv += k * (rect - mDuckEnv);
            float duck = 1.0f;
            if (!c.freeze && c.duckAmt > 0.0f)
                duck = 1.0f - c.duckAmt * (mDuckEnv / (mDuckEnv + 0.08f));
            wL *= duck; wR *= duck;

            // bloom swell: slow attack keyed off the WET so the tail blooms in and
            // then rings out slowly (rather than being gated off when you stop).
            if (c.swellRate > 0.0f)
            {
                const float wetMag = std::max(std::abs(wL), std::abs(wR));
                const float target = wetMag > 0.0005f ? 1.0f : 0.0f;
                if (target > mSwell) mSwell += c.swellRate * (target - mSwell);
                else mSwell += mSwellRel * (target - mSwell);
                wL *= mSwell; wR *= mSwell;
            }

            const float g = mMixZ;
            left[n] = (1.0f - g) * dryL[n] + g * wL;
            if (stereo)
                right[n] = (1.0f - g) * dryR[n] + g * wR;
        }
        reverb_detail::flush(mHpL); reverb_detail::flush(mHpR); reverb_detail::flush(mDuckEnv);
    }

private:
    double mFs = 48000.0;
    float mSmoothK = 0.01f, mDuckAtt = 0.1f, mDuckRel = 0.001f, mSwellRel = 0.001f;
    float mHpL = 0.0f, mHpR = 0.0f, mDuckEnv = 0.0f, mSwell = 0.0f;
    float mMixZ = 0.0f, mMakeupZ = 1.0f;
};

// ===========================================================================
// ReverbBlock — character selector + shared guardrail mixer.
// ===========================================================================
class ReverbBlock : public StereoBlock
{
public:
    enum Type { kRoom = 0, kHall, kPlate, kSpring, kShimmer, kAmbience, kBloom };
    static constexpr int kNumTypes = 7;

    static constexpr int kNumLines = FdnReverb::kNumLines;
    static constexpr float kMinSize = FdnReverb::kMinSize;
    static constexpr float kMaxSize = FdnReverb::kMaxSize;

    static const char *typeName(int t)
    {
        static const char *n[kNumTypes] = {"Room", "Hall", "Plate", "Spring", "Shimmer", "Ambience", "Bloom"};
        return (t >= 0 && t < kNumTypes) ? n[t] : "Room";
    }
    static bool usesFdn(Type t) { return t == kRoom || t == kHall || t == kAmbience || t == kBloom; }

    // ---- voicing introspection (UI + tests) ----
    static bool sizeExposed(Type t) { return t == kHall; }
    static bool toneExposed(Type) { return true; }
    static bool predelayExposed(Type t) { return t == kPlate || t == kBloom; } // Hall folds it into Size; Shimmer/Room/Spring/Ambience hardwire
    static bool modExposed(Type t) { return t == kHall || t == kPlate || t == kBloom || t == kShimmer; } // Ambience hardwires
    static bool shimmerExposed(Type t) { return t == kShimmer; }
    static bool pitchExposed(Type t) { return t == kShimmer; }
    static bool tensionExposed(Type t) { return t == kSpring; }
    static bool swellExposed(Type t) { return t == kBloom; }
    static const char *toneCaption(Type t) { return t == kSpring ? "Tone" : "Damping"; }

    const char *name() const override { return "Reverb"; }

    void prepare(const BlockContext &ctx) override
    {
        mFdn.prepare(ctx.sampleRate);
        mPlate.prepare(ctx.sampleRate);
        mSpring.prepare(ctx.sampleRate);
        mShimmer.prepare(ctx.sampleRate);
        mMixer.prepare(ctx.sampleRate);
        const int cap = std::max(16, ctx.maxBlockSize);
        mDryL.assign((size_t)cap, 0.0f);
        mDryR.assign((size_t)cap, 0.0f);
        mPrepared = true;
        pushParams();
        mMixer.snapMix(effMix());
        reset();
    }

    void reset() override
    {
        mFdn.reset(); mPlate.reset(); mSpring.reset(); mShimmer.reset();
        mMixer.reset();
        mMixer.snapMix(mPrepared ? effMix() : 0.0f);
    }

    void setType(int t)
    {
        const Type nt = (Type)std::clamp(t, 0, kNumTypes - 1);
        if (nt != mType) { mType = nt; if (mPrepared) { resetActive(); mMixer.reset(); mMixer.snapMix(effMix()); pushParams(); } }
    }
    void setSize(float s) { mSize = s; if (mPrepared) pushParams(); }
    void setDecaySeconds(float t) { mT60 = t; if (mPrepared) pushParams(); }
    void setDampHz(float hz) { mDampHz = hz; if (mPrepared) pushParams(); }
    void setPredelayMs(float ms) { mPredelayMs = ms; if (mPrepared) pushParams(); }
    void setMix(float m) { mMix = std::clamp(m, 0.0f, 1.0f); if (mPrepared) pushParams(); }
    void setMod(float d) { mMod = std::clamp(d, 0.0f, 1.0f); if (mPrepared) pushParams(); }
    void setShimmer(float s) { mShimmerAmt = std::clamp(s, 0.0f, 1.0f); if (mPrepared) pushParams(); }
    void setTension(float t) { mTension = std::clamp(t, 0.0f, 1.0f); if (mPrepared) pushParams(); }
    void setWidth(float w) { mWidth = std::clamp(w, 0.0f, 1.0f); }
    void setSwell(float s) { mSwell = std::clamp(s, 0.0f, 1.0f); }
    void setPitch(int p) { mPitch = std::clamp(p, 0, 2); if (mPrepared) pushParams(); }
    void setFreeze(bool f) { mFreeze = f; if (mPrepared) pushParams(); }

    int lineLengthSamples(int i) const { return mFdn.lineLengthSamples(i); }
    float lineGain(int i) const { return mFdn.lineGain(i); }

    void process(float *left, float *right, int numSamples) override
    {
        pushParams();
        const bool stereo = (left != right);

        // Pure-dry shortcut keeps mix=0 bit-exact and saves the engine when idle.
        if (mMix <= 0.0f && mMixerIdle() && !mFreeze)
            return;

        if ((int)mDryL.size() < numSamples) { mDryL.assign((size_t)numSamples, 0.0f); mDryR.assign((size_t)numSamples, 0.0f); }
        for (int n = 0; n < numSamples; ++n) { mDryL[(size_t)n] = left[n]; mDryR[(size_t)n] = stereo ? right[n] : left[n]; }

        switch (mType)
        {
        case kPlate: mPlate.process(left, right, numSamples); break;
        case kSpring: mSpring.process(left, right, numSamples); break;
        case kShimmer: mShimmer.process(left, right, numSamples); break;
        default: mFdn.process(left, right, numSamples); break;
        }

        GuardMixer::Config c;
        c.mix = effMix();
        c.hpfHz = hpfForType(mType);
        c.makeup = makeupForType(mType, effT60());
        c.duckAmt = duckForType(mType);
        c.width = mWidth;
        c.swellRate = (mType == kBloom) ? swellRate() : 0.0f;
        c.freeze = mFreeze;
        mMixer.process(left, right, mDryL.data(), mDryR.data(), numSamples, stereo, c);
    }

private:
    bool mMixerIdle() const { return true; } // mix==0 path: nothing to flush (mixer snaps on next active block)

    void resetActive()
    {
        switch (mType)
        {
        case kPlate: mPlate.reset(); break;
        case kSpring: mSpring.reset(); break;
        case kShimmer: mShimmer.reset(); break;
        default: mFdn.reset(); break;
        }
    }

    float effMix() const { return mMix; }
    float effT60() const
    {
        switch (mType)
        {
        case kRoom: return std::min(mT60, 3.0f);
        case kAmbience: return std::min(mT60, 1.2f);
        case kBloom: return std::max(mT60, 2.5f);
        case kSpring: return std::min(mT60, 4.0f);
        case kShimmer: return std::max(mT60, 1.5f);
        default: return mT60;
        }
    }
    // foolproofing tables -----------------------------------------------------
    static float hpfForType(Type t)
    {
        switch (t)
        {
        case kSpring: return 220.0f;   // springs are midrangey; keep the lows out
        case kRoom: return 180.0f;
        case kAmbience: return 200.0f;
        case kPlate: return 150.0f;
        case kShimmer: return 160.0f;
        case kBloom: return 90.0f;     // let the big swell keep some weight
        default: return 120.0f;        // Hall
        }
    }
    static float duckForType(Type t)
    {
        switch (t)
        {
        case kRoom: return 0.22f;
        case kSpring: return 0.20f;
        case kPlate: return 0.30f;
        case kHall: return 0.32f;
        case kBloom: return 0.42f;
        case kShimmer: return 0.48f;
        case kAmbience: return 0.45f;
        default: return 0.3f;
        }
    }
    // hold perceived wet level roughly constant as Decay changes (longer decay
    // builds up more for sustained input, so scale it back).
    static float makeupForType(Type, float t60)
    {
        return std::clamp(std::sqrt(2.0f / std::max(0.2f, t60)), 0.5f, 1.8f);
    }
    float swellRate() const
    {
        // Swell 0 -> ~40 ms attack, Swell 1 -> ~1.5 s attack (per-sample rise coef).
        const float ms = 40.0f + mSwell * 1460.0f;
        return 1.0f - std::exp(-1.0f / (ms * 0.001f * 48000.0f));
    }

    void pushParams()
    {
        switch (mType)
        {
        case kPlate:
            mPlate.setDecaySeconds(effT60()); mPlate.setDampHz(mDampHz);
            mPlate.setPredelayMs(mPredelayMs); mPlate.setModDepth(mMod); mPlate.setFreeze(mFreeze);
            break;
        case kSpring:
            mSpring.setDecaySeconds(effT60()); mSpring.setDampHz(mDampHz);
            mSpring.setTension(mTension); mSpring.setFreeze(mFreeze);
            break;
        case kShimmer:
            mShimmer.setDecaySeconds(effT60()); mShimmer.setDampHz(mDampHz);
            mShimmer.setPredelayMs(30.0f); // hardwired (folded out of the UI)
            mShimmer.setShimmer(mShimmerAmt); mShimmer.setModDepth(mMod);
            mShimmer.setPitch(mPitch); mShimmer.setFreeze(mFreeze);
            break;
        default: // FDN family
        {
            float size, mod, pre, diff;
            switch (mType)
            {
            case kRoom:     size = 0.6f;  mod = 0.15f; pre = 8.0f;  diff = 0.70f; break;
            case kAmbience: size = 0.45f; mod = 0.25f; pre = 0.0f;  diff = 0.78f; break;
            // Bloom hardwires a long predelay; Hall folds predelay into Size.
            case kBloom:    size = 1.45f; mod = std::max(0.45f, mMod); pre = mPredelayMs; diff = 0.60f; break;
            case kHall:
            default:        size = mSize; mod = mMod;  pre = 10.0f + (mSize - kMinSize) * 60.0f; diff = 0.65f; break;
            }
            mFdn.setSize(size); mFdn.setModDepth(mod); mFdn.setPredelayMs(pre);
            mFdn.setDiffusion(diff); mFdn.setDecaySeconds(effT60()); mFdn.setDampHz(mDampHz);
            mFdn.setFreeze(mFreeze);
            break;
        }
        }
    }

    FdnReverb mFdn;
    PlateReverb mPlate;
    SpringReverb mSpring;
    ShimmerReverb mShimmer;
    GuardMixer mMixer;
    std::vector<float> mDryL, mDryR;

    Type mType = kHall;
    float mSize = 1.0f, mT60 = 2.0f, mDampHz = 6000.0f, mPredelayMs = 20.0f, mMix = 0.25f;
    float mMod = 0.3f, mShimmerAmt = 0.5f, mTension = 0.5f, mWidth = 1.0f, mSwell = 0.4f;
    int mPitch = 0;
    bool mFreeze = false, mPrepared = false;
};

} // namespace nam_rig
