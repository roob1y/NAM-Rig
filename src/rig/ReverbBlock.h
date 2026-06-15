#pragma once
// ReverbBlock — a typed CHARACTER reverb (post-cab, stereo). Like ModBlock, each
// character is voiced like the real thing rather than being one generic engine,
// and the UI shows only the controls a character actually has; everything else is
// hardwired to that character's sweet spot, so you can't dial a bad sound.
//
// FOUR engines power seven characters:
//   - Modulated Householder FDN  -> Room, Hall, Ambience, Bloom
//       8-line FDN with exact-T60 line gains (g_i = 10^(-3 L_i / (T60 fs)), so the
//       decay time is verifiable by construction), Schroeder input diffusers, a
//       one-pole damping LPF per line, AND slow per-line delay modulation that
//       breaks up the metallic ringing of a static FDN (the classic "smooth tail"
//       trick). Mod depth 0 reproduces the original static FDN byte-for-byte.
//   - Dattorro figure-8 plate     -> Plate   (bright, dense, fast diffusion)
//   - Dispersive allpass spring   -> Spring  (boingy chirp via long AP cascades)
//   - Octave-up feedback shimmer  -> Shimmer (compact FDN + pitch shifter in the
//                                             feedback path -> ascending wash)
//
// Voicing introspection (sizeExposed/toneExposed/...) is public + static so the
// UI and the tests read the same source of truth as the audio thread.
//
// Default type is Hall with mod 0, which is exactly the original 8-line reverb,
// so tests/reverb_test.cpp (T1..T7) still characterise that engine. Zero latency
// (predelay is effect delay). Mono-buffer rule (left == right): mono in, left tap.

#include "Blocks.h"
#include "Lfo.h" // FracDelayLine + Lfo
#include <array>
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

// one-pole LPF coefficient for a cutoff in Hz (clamped below Nyquist).
inline float onePole(double hz, double fs)
{
    const double fc = std::min(std::max(hz, 1.0), 0.45 * fs);
    return (float)(1.0 - std::exp(-2.0 * kPi * fc / fs));
}

// delay-line Schroeder allpass step (fixed integer length). Reads BEFORE write,
// so length is relative to the just-written sample (>= 1).
inline float allpassInt(FracDelayLine &line, int len, float g, float x)
{
    const float z = line.readInt(len);
    const float y = -g * x + z;
    line.write(x + g * y);
    return y;
}

// first-order allpass section (no delay line) — y = a*x + x1 - a*y1. Used by the
// spring dispersion cascade. State is {x1, y1}.
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

inline void flush(float &z)
{
    if (std::abs(z) < 1.0e-30f)
        z = 0.0f;
}
} // namespace reverb_detail

// ===========================================================================
// FdnReverb — the modulated 8-line Householder FDN. Powers Room/Hall/Ambience/
// Bloom; the host (ReverbBlock) feeds it per-character effective parameters.
// ===========================================================================
class FdnReverb
{
public:
    static constexpr int kNumLines = 8;
    // Line lengths in ms at Size = 1.0 (mutually non-harmonic, 23..61 ms).
    static constexpr std::array<double, kNumLines> kLineMs = {
        23.0, 27.7, 31.7, 37.5, 42.8, 48.1, 54.2, 61.3};
    static constexpr std::array<double, 4> kDiffMs = {3.0, 5.7, 8.9, 12.6};
    static constexpr float kMinSize = 0.5f, kMaxSize = 1.5f;
    static constexpr double kModMaxMs = 2.0;  // ±ms of per-line delay modulation
    static constexpr float kModRateHz = 0.7f; // slow, chorused tail

    void prepare(double fs)
    {
        mFs = fs;
        const double headroom = kModMaxMs * 0.001 * mFs + 8.0;
        for (int i = 0; i < kNumLines; ++i)
            mLine[(size_t)i].prepare((int)std::ceil(kLineMs[(size_t)i] * kMaxSize * 0.001 * mFs + headroom));
        for (size_t i = 0; i < mDiff.size(); ++i)
            mDiff[i].prepare((int)std::ceil(kDiffMs[i] * 0.001 * mFs) + 8);
        mPredelay.prepare((int)std::ceil(0.2 * mFs) + 8);
        mSmoothK = 1.0f - std::exp((float)(-1.0 / (0.010 * mFs)));
        mLfo.prepare(mFs);
        mLfo.setRateHz(kModRateHz);
        updateGeometry();
        reset();
        mPrepared = true;
    }

    void reset()
    {
        for (auto &l : mLine)
            l.reset();
        for (auto &d : mDiff)
            d.reset();
        mPredelay.reset();
        std::fill(mDampState.begin(), mDampState.end(), 0.0f);
        mLfo.reset();
        mMixZ = mMix;
    }

    // ---- parameters (audio thread) ----
    void setSize(float s)
    {
        s = std::clamp(s, kMinSize, kMaxSize);
        if (s != mSize) { mSize = s; if (mPrepared) updateGeometry(); }
    }
    void setDecaySeconds(float t60)
    {
        t60 = std::max(0.05f, t60);
        if (t60 != mT60) { mT60 = t60; if (mPrepared) updateGeometry(); }
    }
    void setDampHz(float hz)
    {
        if (hz != mDampHz) { mDampHz = hz; if (mPrepared) updateGeometry(); }
    }
    void setPredelayMs(float ms) { mPredelayMs = std::clamp(ms, 0.0f, 200.0f); }
    void setMix(float m) { mMix = std::clamp(m, 0.0f, 1.0f); }
    void setModDepth(float d) { mMod = std::clamp(d, 0.0f, 1.0f); }   // 0 = static FDN
    void setDiffusion(float g) { mDiffG = std::clamp(g, 0.0f, 0.8f); }

    int lineLengthSamples(int i) const { return mLen[(size_t)i]; } // verification
    float lineGain(int i) const { return mGain[(size_t)i]; }       // verification

    void process(float *left, float *right, int numSamples)
    {
        using namespace reverb_detail;
        const bool stereo = (left != right);
        const int pre = (int)std::round((double)mPredelayMs * 0.001 * mFs);
        const bool modOn = mMod > 0.0f;
        const float modSamp = (float)(mMod * kModMaxMs * 0.001 * mFs);

        for (int n = 0; n < numSamples; ++n)
        {
            mMixZ += mSmoothK * (mMix - mMixZ);

            const float dryL = left[n];
            const float dryR = stereo ? right[n] : dryL;

            // mono in -> predelay -> 4 series Schroeder diffusers
            mPredelay.write(0.5f * (dryL + dryR));
            float d = mPredelay.readInt(std::max(1, pre));
            for (size_t a = 0; a < mDiff.size(); ++a)
                d = allpassInt(mDiff[a], mDiffLen[a], mDiffG, d);

            // read all lines (optionally modulated), apply damping + decay gain
            std::array<float, kNumLines> v;
            float sum = 0.0f;
            for (int i = 0; i < kNumLines; ++i)
            {
                float o;
                if (modOn)
                {
                    // per-line phase offsets keep the L/R image decorrelated
                    const float m = modSamp * mLfo.value((double)i * 0.13);
                    o = mLine[(size_t)i].readFrac((double)mLen[(size_t)i] + (double)m);
                }
                else
                {
                    o = mLine[(size_t)i].readInt(mLen[(size_t)i]); // byte-exact static path
                }
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

            // orthogonal +/- output taps
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

            if (modOn)
                mLfo.advance();
        }

        for (auto &z : mDampState)
            reverb_detail::flush(z);
    }

private:
    static constexpr std::array<float, kNumLines> kSignL = {+1, -1, +1, -1, +1, -1, +1, -1};
    static constexpr std::array<float, kNumLines> kSignR = {+1, +1, -1, -1, +1, +1, -1, -1};

    void updateGeometry()
    {
        for (int i = 0; i < kNumLines; ++i)
        {
            int len = (int)std::round(kLineMs[(size_t)i] * (double)mSize * 0.001 * mFs);
            len |= 1; // odd lengths stay mutually non-resonant after scaling
            mLen[(size_t)i] = std::max(3, len);
            mGain[(size_t)i] =
                (float)std::pow(10.0, -3.0 * (double)mLen[(size_t)i] / ((double)mT60 * mFs));
        }
        for (size_t a = 0; a < mDiff.size(); ++a)
            mDiffLen[a] = std::max(2, (int)std::round(kDiffMs[a] * 0.001 * mFs));
        mDampOn = mDampHz < 16000.0f; // knob at/above 16 kHz = OFF
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

    float mSize = 1.0f, mT60 = 2.0f, mDampHz = 6000.0f, mPredelayMs = 20.0f, mMix = 0.25f;
    float mMod = 0.0f, mDiffG = 0.65f;
    float mDampK = 1.0f, mMixZ = 0.25f, mSmoothK = 0.01f;
    bool mDampOn = true, mPrepared = false;
};

// ===========================================================================
// PlateReverb — Dattorro (1997) figure-8 plate. Predelay -> input bandwidth LPF
// -> 4 series allpass diffusers -> a recirculating tank (two crossed halves,
// each: modulated decay-diffuser, long delay, damping LPF, fixed decay-diffuser,
// long delay) -> multi-tap stereo output. Lengths are the canonical Dattorro
// figures (samples @ 29761 Hz) converted to ms and rescaled to the host rate.
// ===========================================================================
class PlateReverb
{
public:
    void prepare(double fs)
    {
        mFs = fs;
        auto ms = [&](double samples29761) { return samples29761 / 29761.0 * 1000.0; };
        for (int i = 0; i < 4; ++i)
            mIn[(size_t)i].prepare(samp(ms(kInS[(size_t)i])) + 8);
        // tank lines (per side); ap1 is modulated so give it excursion headroom
        mAp1[0].prepare(samp(ms(672)) + 64);
        mAp1[1].prepare(samp(ms(908)) + 64);
        mDelA[0].prepare(samp(ms(4453)) + 8);
        mDelA[1].prepare(samp(ms(4217)) + 8);
        mAp2[0].prepare(samp(ms(1800)) + 8);
        mAp2[1].prepare(samp(ms(2656)) + 8);
        mDelB[0].prepare(samp(ms(3720)) + 8);
        mDelB[1].prepare(samp(ms(3163)) + 8);
        mPredelay.prepare(samp(200.0) + 8);
        mLfo.prepare(mFs);
        mLfo.setRateHz(1.1f);
        mSmoothK = 1.0f - std::exp((float)(-1.0 / (0.010 * mFs)));

        for (int i = 0; i < 4; ++i) mInLen[(size_t)i] = samp(ms(kInS[(size_t)i]));
        mAp1Len[0] = ms(672); mAp1Len[1] = ms(908);
        mDelALen[0] = samp(ms(4453)); mDelALen[1] = samp(ms(4217));
        mAp2Len[0] = samp(ms(1800)); mAp2Len[1] = samp(ms(2656));
        mDelBLen[0] = samp(ms(3720)); mDelBLen[1] = samp(ms(3163));
        // output taps (Dattorro): left reads mostly the right half + vice versa
        const double tapsL[7] = {266, 2974, 1913, 1996, 1990, 187, 1066};
        const double tapsR[7] = {353, 3627, 1228, 2673, 2111, 335, 121};
        for (int i = 0; i < 7; ++i) { mTapL[(size_t)i] = samp(ms(tapsL[i])); mTapR[(size_t)i] = samp(ms(tapsR[i])); }
        reset();
        mPrepared = true;
    }

    void reset()
    {
        for (auto &l : mIn) l.reset();
        for (auto &l : mAp1) l.reset();
        for (auto &l : mDelA) l.reset();
        for (auto &l : mAp2) l.reset();
        for (auto &l : mDelB) l.reset();
        mPredelay.reset();
        mBw = 0.0f;
        mDamp[0] = mDamp[1] = 0.0f;
        mTank[0] = mTank[1] = 0.0f;
        mLfo.reset();
        mMixZ = mMix;
    }

    void setDecaySeconds(float t60) { mT60 = std::max(0.1f, t60); mDirty = true; }
    void setDampHz(float hz) { mDampHz = hz; mDirty = true; }
    void setPredelayMs(float ms) { mPredelayMs = std::clamp(ms, 0.0f, 200.0f); }
    void setMix(float m) { mMix = std::clamp(m, 0.0f, 1.0f); }
    void setModDepth(float d) { mMod = std::clamp(d, 0.0f, 1.0f); }

    void process(float *left, float *right, int numSamples)
    {
        using namespace reverb_detail;
        if (mDirty) recompute();
        const bool stereo = (left != right);
        const int pre = (int)std::round((double)mPredelayMs * 0.001 * mFs);
        const float excSamp = (float)(mMod * 0.0008 * mFs); // up to ~0.8 ms of wobble

        for (int n = 0; n < numSamples; ++n)
        {
            mMixZ += mSmoothK * (mMix - mMixZ);
            const float dryL = left[n];
            const float dryR = stereo ? right[n] : dryL;

            mPredelay.write(0.5f * (dryL + dryR));
            float x = mPredelay.readInt(std::max(1, pre));
            mBw += mBwCoef * (x - mBw); // input bandwidth (anti-zing)
            x = mBw;
            for (int i = 0; i < 4; ++i)
                x = allpassInt(mIn[(size_t)i], mInLen[(size_t)i], kInG[(size_t)i], x);

            // two crossed tank halves; each reads the OTHER half's last node
            const float modL = excSamp * mLfo.value(0.0);
            const float modR = excSamp * mLfo.value(0.25);
            float a = x + mDecay * mTank[1];
            {
                const float z = mAp1[0].readFrac((double)mAp1Len[0] + (double)modL);
                const float y = -kDecayDiff1 * a + z;
                mAp1[0].write(a + kDecayDiff1 * y);
                a = y;
                mDelA[0].write(a);
                a = mDelA[0].readInt(mDelALen[0]);
                mDamp[0] += mDampK * (a - mDamp[0]);
                a = mDamp[0] * mDecay;
                a = allpassInt(mAp2[0], mAp2Len[0], -kDecayDiff2, a);
                mDelB[0].write(a);
                mTank[0] = mDelB[0].readInt(mDelBLen[0]);
            }
            float b = x + mDecay * mTank[0];
            {
                const float z = mAp1[1].readFrac((double)mAp1Len[1] + (double)modR);
                const float y = -kDecayDiff1 * b + z;
                mAp1[1].write(b + kDecayDiff1 * y);
                b = y;
                mDelA[1].write(b);
                b = mDelA[1].readInt(mDelALen[1]);
                mDamp[1] += mDampK * (b - mDamp[1]);
                b = mDamp[1] * mDecay;
                b = allpassInt(mAp2[1], mAp2Len[1], -kDecayDiff2, b);
                mDelB[1].write(b);
                mTank[1] = mDelB[1].readInt(mDelBLen[1]);
            }

            // multi-tap output: left from the right half, right from the left half
            const float wetL = 0.6f * (mDelA[1].readInt(mTapL[0]) + mDelA[1].readInt(mTapL[1])
                                       - mAp2[1].readInt(mTapL[2]) + mDelB[1].readInt(mTapL[3])
                                       - mDelA[0].readInt(mTapL[4]) - mAp2[0].readInt(mTapL[5])
                                       - mDelB[0].readInt(mTapL[6]));
            const float wetR = 0.6f * (mDelA[0].readInt(mTapR[0]) + mDelA[0].readInt(mTapR[1])
                                       - mAp2[0].readInt(mTapR[2]) + mDelB[0].readInt(mTapR[3])
                                       - mDelA[1].readInt(mTapR[4]) - mAp2[1].readInt(mTapR[5])
                                       - mDelB[1].readInt(mTapR[6]));

            left[n] = (1.0f - mMixZ) * dryL + mMixZ * wetL;
            if (stereo)
                right[n] = (1.0f - mMixZ) * dryR + mMixZ * wetR;
            mLfo.advance();
        }
        flush(mBw); flush(mDamp[0]); flush(mDamp[1]); flush(mTank[0]); flush(mTank[1]);
    }

private:
    int samp(double ms) const { return std::max(1, (int)std::round(ms * 0.001 * mFs)); }

    void recompute()
    {
        using namespace reverb_detail;
        // tie the Dattorro 'decay' gain to the Decay (T60) knob via the tank loop
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
    float mT60 = 2.0f, mDampHz = 7000.0f, mPredelayMs = 10.0f, mMix = 0.3f, mMod = 0.3f;
    float mMixZ = 0.3f, mSmoothK = 0.01f;
    bool mPrepared = false, mDirty = true;
};

// ===========================================================================
// SpringReverb — dispersive allpass model. Two parallel "springs"; each is a
// feedback loop containing a long cascade of first-order allpasses (the source of
// the chirpy, dispersive "boing") plus a tuned delay and a damping LPF. Tension
// scales the allpass coefficient + cascade length (the boing pitch/character).
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
        mSmoothK = 1.0f - std::exp((float)(-1.0 / (0.010 * mFs)));
        reset();
        mPrepared = true;
    }

    void reset()
    {
        for (auto &l : mLoop) l.reset();
        for (auto &chain : mAp)
            for (auto &ap : chain) ap.reset();
        mPredelay.reset();
        mSpringZ[0] = mSpringZ[1] = 0.0f;
        mLp[0] = mLp[1] = 0.0f;
        mHp[0] = mHp[1] = 0.0f;
        mMixZ = mMix;
    }

    void setDecaySeconds(float t60) { mT60 = std::max(0.1f, t60); mDirty = true; }
    void setDampHz(float hz) { mDampHz = hz; mDirty = true; }
    void setTension(float t) { mTension = std::clamp(t, 0.0f, 1.0f); mDirty = true; }
    void setMix(float m) { mMix = std::clamp(m, 0.0f, 1.0f); }

    void process(float *left, float *right, int numSamples)
    {
        using namespace reverb_detail;
        if (mDirty) recompute();
        const bool stereo = (left != right);
        for (int n = 0; n < numSamples; ++n)
        {
            mMixZ += mSmoothK * (mMix - mMixZ);
            const float dryL = left[n];
            const float dryR = stereo ? right[n] : dryL;
            mPredelay.write(0.5f * (dryL + dryR));
            const float in = mPredelay.readInt(std::max(1, mPreLen));

            float out[kSprings];
            for (int s = 0; s < kSprings; ++s)
            {
                float v = in + mFb * mSpringZ[(size_t)s];
                for (int k = 0; k < mStages; ++k)
                    v = mAp[(size_t)s][(size_t)k].process(mApA, v);
                mLoop[(size_t)s].write(v);
                float d = mLoop[(size_t)s].readInt(mLoopLen[(size_t)s]);
                // damping LPF + DC-blocking HPF inside the loop
                mLp[(size_t)s] += mDampK * (d - mLp[(size_t)s]);
                d = mLp[(size_t)s];
                const float hpIn = d;
                mHp[(size_t)s] += mHpK * (hpIn - mHp[(size_t)s]);
                d = hpIn - mHp[(size_t)s];
                mSpringZ[(size_t)s] = d;
                out[s] = d;
            }
            // stereo: spring 0 leans left, spring 1 leans right, with cross-bleed
            const float wetL = 1.1f * out[0] + 0.35f * out[1];
            const float wetR = 0.35f * out[0] + 1.1f * out[1];
            left[n] = (1.0f - mMixZ) * dryL + mMixZ * wetL;
            if (stereo)
                right[n] = (1.0f - mMixZ) * dryR + mMixZ * wetR;
        }
        for (int s = 0; s < kSprings; ++s) { flush(mSpringZ[(size_t)s]); flush(mLp[(size_t)s]); flush(mHp[(size_t)s]); }
    }

private:
    void recompute()
    {
        using namespace reverb_detail;
        mPreLen = std::max(1, (int)std::round(0.004 * mFs));
        // tension -> dispersion: more stages + higher allpass coefficient = boingier
        mStages = std::clamp((int)std::round(60 + mTension * 60), 8, kMaxStages);
        mApA = 0.5f + 0.28f * mTension; // 0.50..0.78
        // feedback gain from T60 over the average loop length
        const double avgMs = (double)(mLoopLen[0] + mLoopLen[1]) * 0.5 / mFs * 1000.0;
        mFb = (float)std::clamp(std::pow(10.0, -3.0 * avgMs / ((double)mT60 * 1000.0)), 0.0, 0.97);
        mDampK = onePole((double)mDampHz, mFs);
        mHpK = onePole(120.0, mFs); // remove rumble from the long cascade
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
    float mT60 = 2.0f, mDampHz = 4500.0f, mTension = 0.5f, mMix = 0.3f;
    float mMixZ = 0.3f, mSmoothK = 0.01f;
    bool mPrepared = false, mDirty = true;
};

// ===========================================================================
// ShimmerReverb — a compact 4-line modulated FDN whose feedback path is sent
// through an octave-up pitch shifter (delay-line, dual-grain, raised-cosine
// crossfade). Each pass re-injects the pitched signal, so the tail blooms a
// shimmering octave above the input. Shimmer sets the re-injection amount.
// ===========================================================================
class ShimmerReverb
{
public:
    static constexpr int kLines = 4;
    static constexpr std::array<double, kLines> kLineMs = {37.0, 43.0, 47.0, 53.0};

    void prepare(double fs)
    {
        mFs = fs;
        for (int i = 0; i < kLines; ++i)
            mLine[(size_t)i].prepare((int)std::ceil(kLineMs[(size_t)i] * 0.001 * mFs) + 16);
        mPredelay.prepare((int)std::ceil(0.2 * mFs) + 8);
        mWindow = (int)std::round(0.045 * mFs); // ~45 ms grain
        mShift.prepare(mWindow * 2 + 16);
        mLfo.prepare(mFs);
        mLfo.setRateHz(0.6f);
        mSmoothK = 1.0f - std::exp((float)(-1.0 / (0.010 * mFs)));
        updateGeometry();
        reset();
        mPrepared = true;
    }

    void reset()
    {
        for (auto &l : mLine) l.reset();
        mPredelay.reset();
        mShift.reset();
        std::fill(mDampState.begin(), mDampState.end(), 0.0f);
        mGrain = 0.0;
        mShimZ = 0.0f;
        mLfo.reset();
        mMixZ = mMix;
    }

    void setDecaySeconds(float t60) { mT60 = std::max(0.2f, t60); if (mPrepared) updateGeometry(); }
    void setDampHz(float hz) { mDampHz = hz; if (mPrepared) updateGeometry(); }
    void setPredelayMs(float ms) { mPredelayMs = std::clamp(ms, 0.0f, 200.0f); }
    void setMix(float m) { mMix = std::clamp(m, 0.0f, 1.0f); }
    void setShimmer(float s) { mShimmer = std::clamp(s, 0.0f, 1.0f); }
    void setModDepth(float d) { mMod = std::clamp(d, 0.0f, 1.0f); }

    void process(float *left, float *right, int numSamples)
    {
        using namespace reverb_detail;
        const bool stereo = (left != right);
        const int pre = (int)std::round((double)mPredelayMs * 0.001 * mFs);
        const float modSamp = (float)(mMod * 1.5 * 0.001 * mFs);
        const double W = (double)mWindow;

        for (int n = 0; n < numSamples; ++n)
        {
            mMixZ += mSmoothK * (mMix - mMixZ);
            const float dryL = left[n];
            const float dryR = stereo ? right[n] : dryL;

            mPredelay.write(0.5f * (dryL + dryR));
            const float in = mPredelay.readInt(std::max(1, pre));

            // FDN read
            std::array<float, kLines> v;
            float sum = 0.0f;
            for (int i = 0; i < kLines; ++i)
            {
                const float m = modSamp * mLfo.value((double)i * 0.21);
                float o = mLine[(size_t)i].readFrac((double)mLen[(size_t)i] + (double)m);
                auto &z = mDampState[(size_t)i];
                z += mDampK * (o - z);
                o = z * mGain[(size_t)i];
                v[(size_t)i] = o;
                sum += o;
            }
            const float h = 0.5f * sum; // 4x4 Householder (2/N = 0.5)

            // octave-up shifter fed from the tank sum; re-injected next round
            const float wet = 0.5f * sum;
            mShift.write(wet);
            mGrain += 1.0; // (ratio-1) = 1 for +1 octave
            if (mGrain >= W) mGrain -= W;
            const double p1 = mGrain, p2 = (mGrain + W * 0.5 >= W) ? mGrain - W * 0.5 : mGrain + W * 0.5;
            const float s1 = mShift.readFrac(2.0 + (W - p1));
            const float s2 = mShift.readFrac(2.0 + (W - p2));
            const float w1 = 0.5f - 0.5f * (float)std::cos(2.0 * kPi * p1 / W);
            const float w2 = 0.5f - 0.5f * (float)std::cos(2.0 * kPi * p2 / W);
            float pitched = w1 * s1 + w2 * s2; // w1+w2 == 1 by construction
            mShimZ += mShimCoef * (pitched - mShimZ); // tame the top of the octave
            pitched = mShimZ;

            // tanh-limited feedback: keeps the recirculating octave from running
            // away at high Shimmer/long decay, and the gentle nonlinearity is the
            // sheen that makes the wash sing (the "analog" in the feedback path).
            const float fb = std::tanh(mShimmer * 0.9f * pitched);
            const float inject = in + fb;
            for (int i = 0; i < kLines; ++i)
                mLine[(size_t)i].write(0.5f * inject + v[(size_t)i] - h);

            const float wetL = v[0] - v[1] + v[2] - v[3];
            const float wetR = v[0] + v[1] - v[2] - v[3];
            left[n] = (1.0f - mMixZ) * dryL + mMixZ * (0.5f * wetL);
            if (stereo)
                right[n] = (1.0f - mMixZ) * dryR + mMixZ * (0.5f * wetR);
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
    double mGrain = 0.0;
    float mShimZ = 0.0f, mShimCoef = 0.1f;
    float mT60 = 4.0f, mDampHz = 7000.0f, mPredelayMs = 30.0f, mMix = 0.3f, mShimmer = 0.5f, mMod = 0.4f;
    float mDampK = 1.0f, mMixZ = 0.3f, mSmoothK = 0.01f;
    bool mPrepared = false;
};

// ===========================================================================
// ReverbBlock — the character selector. Holds all four engines, dispatches to
// the active one, and applies the per-character voicing (which controls are live
// vs hardwired) so the audio thread, the UI and the tests agree.
// ===========================================================================
class ReverbBlock : public StereoBlock
{
public:
    enum Type { kRoom = 0, kHall, kPlate, kSpring, kShimmer, kAmbience, kBloom };
    static constexpr int kNumTypes = 7;

    // legacy aliases (referenced by the processor + reverb_test)
    static constexpr int kNumLines = FdnReverb::kNumLines;
    static constexpr float kMinSize = FdnReverb::kMinSize;
    static constexpr float kMaxSize = FdnReverb::kMaxSize;

    static const char *typeName(int t)
    {
        static const char *n[kNumTypes] = {"Room", "Hall", "Plate", "Spring", "Shimmer", "Ambience", "Bloom"};
        return (t >= 0 && t < kNumTypes) ? n[t] : "Room";
    }
    static bool usesFdn(Type t) { return t == kRoom || t == kHall || t == kAmbience || t == kBloom; }

    // ---- voicing introspection (UI + tests read these) ----
    static bool sizeExposed(Type t) { return t == kHall; }                  // others hardwire size
    static bool toneExposed(Type) { return true; }                          // every character has Tone
    static bool predelayExposed(Type t) { return t == kHall || t == kPlate || t == kBloom || t == kShimmer; }
    static bool modExposed(Type t) { return t == kHall || t == kPlate || t == kBloom || t == kShimmer || t == kAmbience; }
    static bool shimmerExposed(Type t) { return t == kShimmer; }
    static bool tensionExposed(Type t) { return t == kSpring; }
    static const char *toneCaption(Type t) { return t == kSpring ? "Tone" : "Damping"; }

    const char *name() const override { return "Reverb"; }

    void prepare(const BlockContext &ctx) override
    {
        mFdn.prepare(ctx.sampleRate);
        mPlate.prepare(ctx.sampleRate);
        mSpring.prepare(ctx.sampleRate);
        mShimmer.prepare(ctx.sampleRate);
        mPrepared = true;
        pushParams(); // push host params into the engines, then snap smoothers
        reset();      // so the wet/dry mix starts exactly at its set value
    }

    void reset() override
    {
        mFdn.reset();
        mPlate.reset();
        mSpring.reset();
        mShimmer.reset();
    }

    // ---- parameters (audio thread) ----
    void setType(int t)
    {
        const Type nt = (Type)std::clamp(t, 0, kNumTypes - 1);
        if (nt != mType)
        {
            mType = nt;
            if (mPrepared) { resetActive(); pushParams(); }
        }
    }
    void setSize(float s) { mSize = s; if (mPrepared) pushParams(); }
    void setDecaySeconds(float t) { mT60 = t; if (mPrepared) pushParams(); }
    void setDampHz(float hz) { mDampHz = hz; if (mPrepared) pushParams(); }
    void setPredelayMs(float ms) { mPredelayMs = ms; if (mPrepared) pushParams(); }
    void setMix(float m) { mMix = m; if (mPrepared) pushParams(); }
    void setMod(float d) { mMod = std::clamp(d, 0.0f, 1.0f); if (mPrepared) pushParams(); }
    void setShimmer(float s) { mShimmerAmt = std::clamp(s, 0.0f, 1.0f); if (mPrepared) pushParams(); }
    void setTension(float t) { mTension = std::clamp(t, 0.0f, 1.0f); if (mPrepared) pushParams(); }

    // verification accessors (FDN engine)
    int lineLengthSamples(int i) const { return mFdn.lineLengthSamples(i); }
    float lineGain(int i) const { return mFdn.lineGain(i); }

    void process(float *left, float *right, int numSamples) override
    {
        pushParams(); // cheap: engine setters early-out when unchanged
        switch (mType)
        {
        case kPlate: mPlate.process(left, right, numSamples); break;
        case kSpring: mSpring.process(left, right, numSamples); break;
        case kShimmer: mShimmer.process(left, right, numSamples); break;
        default: mFdn.process(left, right, numSamples); break; // Room/Hall/Ambience/Bloom
        }
    }

private:
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

    // Compute each character's effective parameters (hardwiring whatever the UI
    // hides) and push them to the active engine.
    void pushParams()
    {
        switch (mType)
        {
        case kPlate:
            mPlate.setDecaySeconds(mT60);
            mPlate.setDampHz(mDampHz);
            mPlate.setPredelayMs(mPredelayMs);
            mPlate.setMix(mMix);
            mPlate.setModDepth(mMod);
            break;
        case kSpring:
            mSpring.setDecaySeconds(std::min(mT60, 4.0f)); // springs are short by nature
            mSpring.setDampHz(mDampHz);
            mSpring.setTension(mTension);
            mSpring.setMix(mMix);
            break;
        case kShimmer:
            mShimmer.setDecaySeconds(std::max(mT60, 1.5f)); // shimmer wants a long bed
            mShimmer.setDampHz(mDampHz);
            mShimmer.setPredelayMs(mPredelayMs);
            mShimmer.setMix(mMix);
            mShimmer.setShimmer(mShimmerAmt);
            mShimmer.setModDepth(mMod);
            break;
        default: // FDN family
        {
            float size, mod, pre, diff, t60;
            switch (mType)
            {
            case kRoom:     size = 0.6f;  mod = 0.15f; pre = 8.0f;  diff = 0.70f; t60 = std::min(mT60, 3.0f); break;
            case kAmbience: size = 0.45f; mod = std::max(0.25f, mMod); pre = 0.0f; diff = 0.78f; t60 = std::min(mT60, 1.2f); break;
            case kBloom:    size = 1.45f; mod = std::max(0.45f, mMod); pre = mPredelayMs; diff = 0.60f; t60 = std::max(mT60, 2.5f); break;
            case kHall:
            default:        size = mSize; mod = mMod;  pre = mPredelayMs; diff = 0.65f; t60 = mT60; break;
            }
            mFdn.setSize(size);
            mFdn.setModDepth(mod);
            mFdn.setPredelayMs(pre);
            mFdn.setDiffusion(diff);
            mFdn.setDecaySeconds(t60);
            mFdn.setDampHz(mDampHz);
            mFdn.setMix(mMix);
            break;
        }
        }
    }

    FdnReverb mFdn;
    PlateReverb mPlate;
    SpringReverb mSpring;
    ShimmerReverb mShimmer;

    Type mType = kHall; // default == original 8-line FDN (mod 0)
    float mSize = 1.0f, mT60 = 2.0f, mDampHz = 6000.0f, mPredelayMs = 20.0f, mMix = 0.25f;
    float mMod = 0.0f, mShimmerAmt = 0.5f, mTension = 0.5f;
    bool mPrepared = false;
};

} // namespace nam_rig
