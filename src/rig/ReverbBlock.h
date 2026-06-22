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
#include "Biquad.h" // input voicing bells for the small room
#include <array>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#if __has_include(<juce_dsp/juce_dsp.h>) && __has_include("BinaryData.h")
#include "EarlyConvolver.h"
#define NAM_PLATE_EARLY_CONV 1
#endif

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
// 16 delay lines, a unitary Hadamard (FWHT) feedback matrix, a dense 6-stage
// allpass input diffuser and independent per-line modulation. The high line
// count + Hadamard mixing + dense diffusion build echo density fast and spread
// the modes, so the tail is a SMOOTH wash instead of the metallic ring of a
// small static FDN (measured tail spectral flatness ~0.04 -> ~0.45). Per-line
// gains still set an exact T60 (g_i = 10^(-3 L_i/(T60 fs))). Renders WET only.
class FdnReverb
{
public:
    static constexpr int kNumLines = 16;
    // prime-ish line lengths (ms @ size 1.0), spread 18..78 ms for modal density
    static constexpr std::array<double, kNumLines> kLineMs = {
        18.3, 21.7, 24.1, 27.9, 31.1, 34.7, 38.3, 41.9,
        45.7, 49.1, 53.3, 57.7, 61.3, 65.9, 70.1, 74.7};
    // dense input diffusion: 6 cascaded allpasses with growing delays
    static constexpr std::array<double, 6> kDiffMs = {7.3, 10.9, 15.7, 22.1, 29.3, 37.9};
    static constexpr float kMinSize = 0.5f, kMaxSize = 1.5f;
    static constexpr double kModMaxMs = 4.0;

    void prepare(double fs)
    {
        mFs = fs;
        const double headroom = kModMaxMs * 0.001 * mFs + 8.0;
        for (int i = 0; i < kNumLines; ++i)
            mLine[(size_t)i].prepare((int)std::ceil(kLineMs[(size_t)i] * kMaxSize * 0.001 * mFs + headroom));
        for (size_t i = 0; i < mDiff.size(); ++i)
            mDiff[i].prepare((int)std::ceil(kDiffMs[i] * kMaxSize * 0.001 * mFs) + 8);
        mPredelay.prepare((int)std::ceil(0.2 * mFs) + 8);
        mLfoA.prepare(mFs); mLfoA.setRateHz(0.5f);
        mLfoB.prepare(mFs); mLfoB.setRateHz(0.83f); // incommensurate -> non-periodic
        hadamardRow(1, mSignL); hadamardRow(2, mSignR); hadamardRow(7, mInj);
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
        mLfoA.reset(); mLfoB.reset();
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
        const float modSamp = (float)(mMod * kModMaxMs * 0.001 * mFs);
        const float apG = std::clamp(mDiffG + 0.05f, 0.5f, 0.78f);
        const float inGain = mFreeze ? 0.0f : 1.0f; // freeze: stop new input

        for (int n = 0; n < numSamples; ++n)
        {
            const float dryL = left[n];
            const float dryR = stereo ? right[n] : dryL;

            mPredelay.write(0.5f * (dryL + dryR));
            float d = mPredelay.readInt(std::max(1, pre));
            for (size_t a = 0; a < mDiff.size(); ++a)
                d = allpassInt(mDiff[a], mDiffLen[a], apG, d);

            std::array<float, kNumLines> o;
            for (int i = 0; i < kNumLines; ++i)
            {
                float r;
                if (modSamp > 0.0f)
                {
                    const float m = modSamp * (0.6f * mLfoA.value((double)i * 0.0625)
                                               + 0.4f * mLfoB.value((double)i * 0.11 + 0.03));
                    r = mLine[(size_t)i].readFrac((double)mLen[(size_t)i] + (double)m);
                }
                else
                    r = mLine[(size_t)i].readInt(mLen[(size_t)i]);
                if (mDampOn) { auto &z = mDampState[(size_t)i]; z += mDampK * (r - z); r = z; }
                o[(size_t)i] = r;
            }

            // decorrelated L/R from two orthogonal Hadamard rows (averaged -> smooth)
            float wetL = 0.0f, wetR = 0.0f;
            for (int i = 0; i < kNumLines; ++i) { wetL += mSignL[i] * o[(size_t)i]; wetR += mSignR[i] * o[(size_t)i]; }
            left[n] = 0.5f * wetL;
            if (stereo) right[n] = 0.5f * wetR;

            // feedback: per-line decay gain (or 1 in freeze) -> Hadamard mix -> inject
            std::array<float, kNumLines> fb;
            for (int i = 0; i < kNumLines; ++i) fb[(size_t)i] = (mFreeze ? 1.0f : mGain[(size_t)i]) * o[(size_t)i];
            fwht16(fb.data());
            const float injIn = inGain * d;
            for (int i = 0; i < kNumLines; ++i)
                mLine[(size_t)i].write(0.15f * (float)mInj[i] * injIn + 0.25f * fb[(size_t)i]);

            mLfoA.advance(); mLfoB.advance();
        }
        for (auto &z : mDampState) reverb_detail::flush(z);
    }

private:
    // in-place fast Walsh-Hadamard transform (unitary mixing after the 0.25 scale)
    static void fwht16(float *a)
    {
        for (int len = 1; len < 16; len <<= 1)
            for (int i = 0; i < 16; i += len << 1)
                for (int j = i; j < i + len; ++j)
                { const float x = a[j], y = a[j + len]; a[j] = x + y; a[j + len] = x - y; }
    }
    static void hadamardRow(int row, int *out)
    {
        for (int i = 0; i < 16; ++i)
        { int b = 0, m = row & i; while (m) { b ^= 1; m &= m - 1; } out[i] = b ? -1 : 1; }
    }
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
            mDiffLen[a] = std::max(2, (int)std::round(kDiffMs[a] * (double)mSize * 0.001 * mFs));
        mDampOn = mDampHz < 16000.0f;
        mDampK = reverb_detail::onePole(mDampHz, mFs);
    }

    double mFs = 48000.0;
    std::array<FracDelayLine, kNumLines> mLine;
    std::array<FracDelayLine, 6> mDiff;
    FracDelayLine mPredelay;
    std::array<int, kNumLines> mLen{};
    std::array<int, 6> mDiffLen{};
    std::array<float, kNumLines> mGain{};
    std::array<float, kNumLines> mDampState{};
    Lfo mLfoA, mLfoB;
    int mSignL[16]{}, mSignR[16]{}, mInj[16]{};
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
        for (int i = 0; i < 6; ++i) mIn[(size_t)i].prepare(samp(ms(kInS[(size_t)i])) + 8);
        mAp1[0].prepare(samp(ms(672)) + 64); mAp1[1].prepare(samp(ms(908)) + 64);
        mDelA[0].prepare(samp(ms(4453)) + 8); mDelA[1].prepare(samp(ms(4217)) + 8);
        mAp2[0].prepare(samp(ms(1800)) + 8); mAp2[1].prepare(samp(ms(2656)) + 8);
        mDelB[0].prepare(samp(ms(3720)) + 8); mDelB[1].prepare(samp(ms(3163)) + 8);
        mPredelay.prepare(samp(200.0) + 8);
        mLfo.prepare(mFs); mLfo.setRateHz(1.1f);
        for (int i = 0; i < 6; ++i) mInLen[(size_t)i] = samp(ms(kInS[(size_t)i]));
        mAp1Len[0] = samp(ms(672)); mAp1Len[1] = samp(ms(908));
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
        mBw = 0.0f; mDamp[0] = mDamp[1] = 0.0f; mTank[0] = mTank[1] = 0.0f; mBloom[0] = mBloom[1] = 0.0f; mHsLpL = mHsLpR = mLfLpL = mLfLpR = 0.0f;
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
        const float excSamp = mFreeze ? 0.0f : (float)(mMod * 0.00005 * mFs); // subtle, cents-scale wobble (was 0.0008 = a giant sweep); tunable
        const float decay = mFreeze ? 1.0f : mDecay;
        const float inGain = mFreeze ? 0.0f : 1.0f;

        for (int n = 0; n < numSamples; ++n)
        {
            const float dryL = left[n];
            const float dryR = stereo ? right[n] : dryL;

            mPredelay.write(0.5f * (dryL + dryR));
            float x = inGain * mPredelay.readInt(std::max(1, pre));
            mBw += mBwCoef * (x - mBw); x = mBw;
            for (int i = 0; i < 6; ++i)
                x = allpassInt(mIn[(size_t)i], mInLen[(size_t)i], kInG[(size_t)i], x);

            const float modL = excSamp * mLfo.value(0.0);
            const float modR = excSamp * mLfo.value(0.25);
            float a = x + decay * mTank[1];
            {
                const float z = mAp1[0].readFrac((double)mAp1Len[0] + (double)modL);
                const float y = -kDecayDiff1 * a + z;
                mAp1[0].write(a + kDecayDiff1 * y); a = y;
                mDelA[0].write(a); a = mDelA[0].readInt(mDelALen[0]);
                { mDamp[0] += mDampK * (a - mDamp[0]); a = mDamp[0]; } // damp always (freeze too)
                a *= decay;
                mBloom[0] += mBloomK * (a - mBloom[0]); a += mBloomGain * mBloom[0]; // low-mid bloom: lows ring longer (vintage-plate character)
                a = allpassInt(mAp2[0], mAp2Len[0], -kDecayDiff2, a);
                mDelB[0].write(a); mTank[0] = mDelB[0].readInt(mDelBLen[0]);
            }
            float b = x + decay * mTank[0];
            {
                const float z = mAp1[1].readFrac((double)mAp1Len[1] + (double)modR);
                const float y = -kDecayDiff1 * b + z;
                mAp1[1].write(b + kDecayDiff1 * y); b = y;
                mDelA[1].write(b); b = mDelA[1].readInt(mDelALen[1]);
                { mDamp[1] += mDampK * (b - mDamp[1]); b = mDamp[1]; } // damp always (freeze too)
                b *= decay;
                mBloom[1] += mBloomK * (b - mBloom[1]); b += mBloomGain * mBloom[1];
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
            mHsLpL += mHsK * (wetL - mHsLpL); float oL = mHsGain * wetL + (1.0f - mHsGain) * mHsLpL; mLfLpL += mLfK * (oL - mLfLpL); left[n] = oL + 0.25f * mLfLpL;
            mHsLpR += mHsK * (wetR - mHsLpR); float oR = mHsGain * wetR + (1.0f - mHsGain) * mHsLpR; mLfLpR += mLfK * (oR - mLfLpR); if (stereo) right[n] = oR + 0.25f * mLfLpR;
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
        mDecay = (float)std::clamp(std::pow(10.0, -1.5 * loopMs / ((double)mT60 * 1000.0)), 0.0, 0.997);
        const double loopSec = loopMs / 1000.0, dN = std::clamp(((double)mDampHz - 1000.0) / 15000.0, 0.0, 1.0), t60HF = std::min(0.35 + dN * 0.85, (double)mT60), rHF = std::pow(10.0, -1.5 * loopSec * (1.0 / t60HF - 1.0 / (double)mT60)); mDampK = (float)std::clamp(2.0 * rHF / (1.0 + rHF), 1.0e-4, 1.0); // Jot-style: damping designed to a TARGET HF decay (highs-die-fast ~0.5-1.8s set by the Damping knob = dark<->bright); HF clamped <= mid -> stable
        const double lfMult = std::clamp(1.15 + 0.18 * (double)mT60, 1.15, 1.9); // bloom tilt grows w/ decay, bounded
        const double gLF = std::pow(10.0, -1.5 * loopSec / ((double)mT60 * lfMult));
        const double gMax = 0.998 / std::max(1.0e-4, (double)mDecay);
        mBloomGain = (float)std::clamp(gLF / std::max(1.0e-4, (double)mDecay) - 1.0, 0.0, gMax - 1.0);
        mBloomK = (float)onePole(320.0, mFs);
        mBwCoef = onePole(5000.0 + dN * 9000.0, mFs); mHsK = (float)onePole(4000.0, mFs); mHsGain = (float)(0.12 + dN * 0.55); mLfK = (float)onePole(350.0, mFs); // output: HF high-shelf + input bandwidth both track the Damping knob (dark<->bright, centroid ~2.6-5.5 kHz) + a gentle ~350 Hz output shelf. Main low-mid BLOOM is now in-loop (frequency-dependent decay), so lows RING longer not just louder.
        mDirty = false;
    }

    double mFs = 48000.0;
    static constexpr double kInS[6] = {142, 107, 379, 277, 199, 89};
    static constexpr float kInG[6] = {0.78f, 0.78f, 0.75f, 0.75f, 0.72f, 0.72f}; // 6-stage input diffusion -> fully diffuse onset (NED~1 by 50ms)
    static constexpr float kDecayDiff1 = 0.72f, kDecayDiff2 = 0.55f; // denser tank diffusion (smoother, less metallic tail)
    std::array<FracDelayLine, 6> mIn;
    std::array<FracDelayLine, 2> mAp1, mDelA, mAp2, mDelB;
    FracDelayLine mPredelay;
    std::array<int, 6> mInLen{};
    std::array<double, 2> mAp1Len{};
    std::array<int, 2> mDelALen{}, mAp2Len{}, mDelBLen{};
    std::array<int, 7> mTapL{}, mTapR{};
    Lfo mLfo;
    float mBw = 0.0f, mBwCoef = 0.0f, mHsLpL = 0.0f, mHsLpR = 0.0f, mHsK = 0.0f, mHsGain = 1.0f, mLfLpL = 0.0f, mLfLpR = 0.0f, mLfK = 0.0f;
    std::array<float, 2> mDamp{}, mTank{}, mBloom{};
    float mDecay = 0.5f, mDampK = 1.0f, mBloomK = 0.0f, mBloomGain = 0.0f;
    float mT60 = 2.0f, mDampHz = 7000.0f, mPredelayMs = 10.0f, mMod = 0.3f;
    bool mPrepared = false, mDirty = true, mFreeze = false;
};

// ===========================================================================
// PlateFdn — v3 FDN plate, voiced to a vintage studio steel-plate reverb. 32-line
// FWHT FDN with a LENGTH-SCALED MULTIBAND absorptive damping in the feedback
// (broadband gain + two RBJ high-shelves per line, fit to the target decay-vs-
// frequency curve): the lows ring LONGEST and the HF plateaus (keeps air) — a real
// plate's decay signature, which a single damping pole cannot make. A 2-pole driver
// passes a bright transient; an input low-mid resonance fills the plate body; an
// output presence bell + plate low-cut shape the balance; a decorrelated early tap
// gives an immediate, wide, mono-safe onset. No modulation. Tone = brightness (the
// driver cutoff; the matched damping stays fixed so the decay match is robust across
// the knob). Decay = the ~1 kHz T60 in true seconds (scales the whole matched curve).
// Renders WET only. Voiced via the offline metric battery against the plate reference.
// ===========================================================================
class PlateFdn
{
public:
    static constexpr int kNumLines = 32;
        static constexpr std::array<double, kNumLines> kLineMs = {
        6.66, 8.32, 9.41, 11.16, 11.69, 13.35, 14.39, 17.53, 18.04, 20.74, 22.42, 24.69, 24.65, 27.68, 27.51, 29.25, 34.65, 33.7, 37.05, 36.6, 38.53, 42.41, 43.6, 44.85, 49.54, 52.02, 54.79, 53.07, 60.34, 62.89, 64.85, 65.48};  // alpha=0.25 RE-FIT geometry (seed976606)
    static constexpr std::array<double, 6> kDiffMs = {0.5, 0.9, 1.5, 2.3, 3.3, 4.7};
    static constexpr std::array<double, kNumLines> kDispMs = {
        0.7, 1.0, 1.3, 1.6, 0.8, 1.1, 1.4, 1.7, 0.9, 1.2, 1.5, 1.8, 2.1, 2.4, 2.0, 2.3,
        2.6, 2.9, 1.9, 2.2, 2.5, 2.8, 3.1, 3.4, 2.7, 3.0, 3.3, 1.5, 1.8, 2.1, 2.4, 2.7};
    static constexpr float kDispG = 0.62f;
    static constexpr std::array<double, kNumLines> kDispMs2 = {
        1.9, 2.3, 1.1, 2.7, 1.5, 3.1, 1.3, 2.9, 1.7, 2.5, 1.0, 3.3, 1.4, 2.1, 2.8, 1.2,
        3.0, 1.6, 2.4, 1.8, 3.2, 1.5, 2.6, 2.0, 1.1, 3.4, 1.9, 2.2, 1.3, 2.7, 1.6, 3.1};
    static constexpr float kDispG2 = 0.55f;
    static constexpr float kMinSize = 0.8f, kMaxSize = 1.6f;
    // damping shape (dB per sample of loop delay) least-squares fit to the target plate T60(f)
    static constexpr double kDampBb = -2.82e-4;                  // broadband (shorten the slightly-long lows, T30+EDT)
    static constexpr double kDampG1 = -1.15e-4, kDampF1 = 355.0;    // high-shelf 1 (re-fit: relengthen mids after the low nudge)
    static constexpr double kDampG2 = -0.38e-4, kDampF2 = 3600.0;   // high-shelf 2 (keep the top dark: centroid/modal vs 4k length balance)
    static constexpr double kDampS  = 0.7, kRef1kT60 = 2.92;        // shelf slope; Decay knob ref (1 kHz)
    // LOW-bloom damping (decay-dependent, dd=T60-2.45, ZERO at the 2.45 anchor; long side only):
    //   rB   = kBloomB1*dd^2 + kBloomC1*max(0,dd-1)^2 -> extends the broadband(low) ring as decay grows
    //   lo1  = -(kBloomA2*dd + kBloomB2*dd^2) dB high-shelf @kBloomF1 -> steepens the 125..710 slope
    // Fit (render-in-loop) to vintage-plate IR at knobs 3.0/3.5/4.0/4.5; all-negative -> loop gain stays <1.
    static constexpr double kBloomF1 = 200.0;       // low absorptive high-shelf corner
    static constexpr double kBloomB1 = 0.213, kBloomC1 = 0.819;       // rB = B1*dd^2 + C1*max(0,dd-1)^2 (hinge: body <=knob3, big bloom knob4+)
    static constexpr double kBloomA2 = -8.13e-6, kBloomB2 = 4.149e-5; // lo1 shelf coeffs (dB)

    void loadProtoLines() {
        for (int i=0;i<kNumLines;++i) mLineMsRt[(size_t)i]=kLineMs[(size_t)i];
        mDispGRt=kDispG; mDispG2Rt=kDispG2;
        
        
    }
    void prepare(double fs)
    {
        mFs = fs;
        loadProtoLines();
        for (int i = 0; i < kNumLines; ++i)
            mLine[(size_t)i].prepare((int)std::ceil(mLineMsRt[(size_t)i] * kMaxSize * 0.001 * mFs) + 8);
        for (size_t i = 0; i < mDiff.size(); ++i)
            mDiff[i].prepare((int)std::ceil(kDiffMs[i] * kMaxSize * 0.001 * mFs) + 8);
        for (int i = 0; i < kNumLines; ++i)
        {
            mDisp[(size_t)i].prepare((int)std::ceil(kDispMs[(size_t)i] * 0.001 * mFs) + 8);
            mDisp2[(size_t)i].prepare((int)std::ceil(kDispMs2[(size_t)i] * 0.001 * mFs) + 8);
        }
        mPredelay.prepare((int)std::ceil(0.2 * mFs) + 8);
        mEarlyApR.prepare((int)std::ceil(7.0 * 0.001 * mFs) + 8);
        mEarlyLenR = std::max(2, (int)std::round(3.7 * 0.001 * mFs));
        hadamardRow(1, mSignL); hadamardRow(2, mSignR); hadamardRow(13, mInj);
        for (int i=0;i<kHfN;++i) mHfTline[(size_t)i].prepare((int)std::ceil(kHfTms[(size_t)i]*0.001*mFs)+8);
        mHfTpre.prepare((int)std::ceil(0.03*mFs)+8);
        for (auto &b : mVlv) b.prepare(mFs);
        mVDiff.prepare(mFs);
        mPLfoA.prepare(mFs); mPLfoA.setRateHz(0.5f); mPLfoB.prepare(mFs); mPLfoB.setRateHz(0.83f);
        updateGeometry();
        reset();
        mPrepared = true;
    }

    void reset()
    {
        for (auto &l : mLine) l.reset();
        for (auto &d : mDiff) d.reset();
        for (auto &d : mDisp) d.reset();
        for (auto &d : mDisp2) d.reset();
        for (auto &b : mHs1) b.reset();
        for (auto &b : mHs2) b.reset();
        for (auto &b : mLo1) b.reset();
        for (auto &s : mDarkS1) s=0.0f; for (auto &s : mDarkS2) s=0.0f;
        mLmEq.reset(); mPresL.reset(); mPresR.reset(); mEReso1.reset(); mEReso2.reset(); mEReso3.reset(); mSide2Hp.reset(); mSide2Lp.reset(); mDip2kL.reset(); mDip2kR.reset(); mAirL.reset(); mAirR.reset();
        mPredelay.reset(); mEarlyApR.reset();
        for (auto &l : mHfTline) l.reset(); mHfTpre.reset(); for (auto &s : mHfTlpS) s=0.0f; for (auto &s : mHfTlpS2) s=0.0f; mHfThpS=mHfThpS2=0.0f;
        for (auto &b : mVlv) b.reset();
        mVDiff.reset();
        mBw1 = mBw2 = 0.0f; mLcL = mLcR = 0.0f; mEarlyLp = 0.0f; mHfLp = 0.0f; mSideLpA = mSideLpB = 0.0f; mSideLowS = 0.0f;
    }

    void setSize(float s) { s = std::clamp(s, kMinSize, kMaxSize); if (s != mSize) { mSize = s; if (mPrepared) updateGeometry(); } }
    void setDecaySeconds(float t60) { t60 = std::max(0.1f, t60); if (t60 != mT60) { mT60 = t60; if (mPrepared) updateGeometry(); } }
    void setDampHz(float hz) { if (hz != mDampHz) { mDampHz = hz; if (mPrepared) updateGeometry(); } } // Tone = brightness
    void setPredelayMs(float ms) { mPredelayMs = std::clamp(ms, 0.0f, 200.0f); }
    void setFreeze(bool f) { mFreeze = f; }
    void setEarlyTap(float g) { mEarly = g; }   // onset-fix: convolver supplies early -> drop FDN early tap

    void process(float *left, float *right, int numSamples)
    {
        using namespace reverb_detail;
        const bool stereo = (left != right);
        const int pre = (int)std::round((double)mPredelayMs * 0.001 * mFs);
        const float inGain = mFreeze ? 0.0f : 1.0f;
        const float invsq = 1.0f / std::sqrt((float)kNumLines);

        for (int n = 0; n < numSamples; ++n)
        {
            const float dryL = left[n];
            const float dryR = stereo ? right[n] : dryL;

            mPredelay.write(0.5f * (dryL + dryR));
            float x = inGain * mPredelay.readInt(std::max(1, pre));
            const float xWide = x;   // full-band bright tap (pre-driver) -> velvet HF feed
            x = mLmEq.processSample(x);                 // input low-mid body (plate resonance)
            mBw1 += mDrvK * (x - mBw1); mBw2 += mDrvK * (mBw1 - mBw2); x = mBw2; // 2-pole driver (Tone)
            for (size_t a = 0; a < mDiff.size(); ++a)
                x = allpassInt(mDiff[a], mDiffLen[a], 0.62f, x);
            if (mVDiffOn) { float xd=mVDiff.process(x); x=(1.0f-mVDiffMix)*x+mVDiffMix*xd; }   // instant-dense velvet pre-diffuser (blend)

            const float pmod = (float)(0.5 * 0.001 * mFs); // 0.5ms LIGHT modulation -> matches the reference lushness (modal_depth ~7.5 vs ref 7.4); set 0.0 for max lushness. Heavier smears the modes flat = thin.
            std::array<float, kNumLines> o;
            for (int i = 0; i < kNumLines; ++i) {
                const float mo = pmod * (0.6f*mPLfoA.value((double)i*0.0625) + 0.4f*mPLfoB.value((double)i*0.11+0.03));
                o[(size_t)i] = mLine[(size_t)i].readFrac((double)mLen[(size_t)i] + (double)mo);
            }
            mPLfoA.advance(); mPLfoB.advance();

            float wetL = 0.0f, wetR = 0.0f;
            for (int i = 0; i < kNumLines; ++i) { wetL += mSignL[i] * o[(size_t)i]; wetR += mSignR[i] * o[(size_t)i]; }
            // frequency-split early energy: LOW band correlated to both channels (instantly dense + NARROW
            // lows, like a real plate) and HIGH band decorrelated L/R (WIDE mids/highs) -> matches the
            // reference's per-band width profile AND front-loads the low-mid C80/EDT in one move.
            const float xEarly = mEReso3.processSample(mEReso2.processSample(mEReso1.processSample(x))); // early-ONLY 125 cut + 250 boost + 500 notch -> match the reference C80 micro-shape (tail untouched)
            mEarlyLp += mEarlyLpK * (xEarly - mEarlyLp);
            const float xLow = mEarlyLp, xHigh = xEarly - xLow;
            mHfLp += mHfLpK * (xHigh - mHfLp);
            const float xHighE = xHigh + mHfEarly * (xHigh - mHfLp);   // HF-emphasized high band -> two-slope bright attack (raises top C80)
            const float eHighRaw = allpassInt(mEarlyApR, mEarlyLenR, 0.6f, xHighE);
            const float eHigh = mHighCorr * xHighE + (1.0f - mHighCorr) * eHighRaw; // partial high-band width, preserved through the HF boost
            const float earlyLow = mEarlyLowGain * xLow;   // front-loaded, mono low-mid
            float oL = invsq * wetL + mEarlyTapScale * mEarly * (earlyLow + xHighE), oR = invsq * wetR + mEarlyTapScale * mEarly * (earlyLow + eHigh);
            oL = mPresL.processSample(oL); oR = mPresR.processSample(oR);  // output presence bell
            oL = mDip2kL.processSample(oL); oR = mDip2kR.processSample(oR); // 2k tonal notch
            oL = mAirL.processSample(oL); oR = mAirR.processSample(oR);     // >8k air trim -> drop centroid onto the reference
            mLcL += mLcK * (oL - mLcL); oL -= mLcL;     // plate low-cut (no deep sub)
            mLcR += mLcK * (oR - mLcR); oR -= mLcR;
            const float sideM = 0.5f * (oL - oR);                       // mid-band side widener: lift 500-1k width
            mSideLpA += mSideKA * (sideM - mSideLpA);
            mSideLpB += mSideKB * (sideM - mSideLpB);
            const float sideBand = mSideLpA - mSideLpB;                 // ~400-1300 Hz of the side signal
            oL += mMidWidth * sideBand; oR -= mMidWidth * sideBand;     // widen the low-mids to the reference profile
            const float side2k = mSide2Lp.processSample(mSide2Hp.processSample(sideM)); // 2nd-order 2k bandpass (steep skirts isolate 2k from 4k)
            oL -= mNarrow2k * side2k; oR += mNarrow2k * side2k;         // narrow the 2k width notch to the reference
            { const float sLow=0.5f*(oL-oR); mSideLowS += mSideLowK*(sLow-mSideLowS); // low band of the side signal
              oL -= mBassMono*mSideLowS; oR += mBassMono*mSideLowS; } // collapse low side -> tight mono lows (real-plate-like)
            if (mHfTlevel > 0.0f && !mFreeze) {   // single parallel diffuse HF tail -> non-exponential HF knee
                float Msrc=(mHfFromField>0.5f)?(invsq*0.5f*(wetL+wetR)):x; // populate reservoir from the DIFFUSE FIELD (FDN sum, no early tap) -> gradual -> sharp knee
                mHfThpS += mHfThpK*(Msrc-mHfThpS); float h1=Msrc-mHfThpS; mHfThpS2 += mHfThpK*(h1-mHfThpS2); float hin=h1-mHfThpS2; // 2-pole high-pass (12dB/oct)
                mHfTpre.write(hin); float hx = mHfTpre.readInt(mHfTpreLen);    // short predelay (no t=0 spike)
                std::array<float,kHfN> ho; for (int i=0;i<kHfN;++i) ho[(size_t)i]=mHfTline[(size_t)i].readInt(mHfTlen[(size_t)i]);
                float hL=0.0f,hR=0.0f; for (int i=0;i<kHfN;++i){ hL+=kHfSignL[(size_t)i]*ho[(size_t)i]; hR+=kHfSignR[(size_t)i]*ho[(size_t)i]; }
                const float hinv=1.0f/std::sqrt((float)kHfN);
                std::array<float,kHfN> hfb; for (int i=0;i<kHfN;++i){ float fbi=(mHfFbHi>0.0f)?(mHfFbLo+(mHfFbHi-mHfFbLo)*((float)i/(float)(kHfN-1))):mHfTfb; float v=fbi*ho[(size_t)i]; mHfTlpS[(size_t)i]+=mHfTlpK*(v-mHfTlpS[(size_t)i]); mHfTlpS2[(size_t)i]+=mHfTlpK*(mHfTlpS[(size_t)i]-mHfTlpS2[(size_t)i]); hfb[(size_t)i]=mHfTlpS[(size_t)i]; (void)mHfTlpS2[(size_t)i]; (void)mHfTilt; }
                fwhtHfCoupled(hfb.data(), mHfRCoup); for (int i=0;i<kHfN;++i) mHfTline[(size_t)i].write(mHfRNorm*hfb[(size_t)i]+hx); (void)hinv;
                oL += mHfTlevel*hinv*hL; oR += mHfTlevel*hinv*hR;
            }
            if (mVelvet && !mFreeze) {   // VELVET HF late-field: dense diffuse 6-12k rising shimmer, fed by the BRIGHT input
                for (auto &b : mVlv) b.tick(xWide, kHfSignL, kHfSignR, oL, oR);
            }
            left[n] = oL;
            if (stereo) right[n] = oR;

            std::array<float, kNumLines> fb;
            for (int i = 0; i < kNumLines; ++i)
            {
                float v = (mFreeze ? 1.0f : mGain[(size_t)i]) * o[(size_t)i];
                if (!mFreeze) { v = mHs1[(size_t)i].processSample(v); v = mHs2[(size_t)i].processSample(v); v = mLo1[(size_t)i].processSample(v); } // multiband absorptive damping (+low bloom shelf)
                if (!mFreeze && mDarkOn) { mDarkS1[(size_t)i]+=mDarkK*(v-mDarkS1[(size_t)i]); v = mDarkG*v + (1.0f-mDarkG)*mDarkS1[(size_t)i]; } // DARK-BODY: highs *mDarkG each pass (gentle), lows pass
                v = allpassInt(mDisp[(size_t)i], mDispLen[(size_t)i], mDispGRt, v);
                v = allpassInt(mDisp2[(size_t)i], mDispLen2[(size_t)i], mDispG2Rt, v);
                fb[(size_t)i] = v;
            }
            fwhtCoupled(fb.data(), mCoupling);
            const float injIn = 0.5f * x;
            for (int i = 0; i < kNumLines; ++i)
                mLine[(size_t)i].write(mMixNorm * fb[(size_t)i] + (float)mInj[i] * injIn);
        }
        for (auto &z : mDampStateUnused) reverb_detail::flush(z);
        reverb_detail::flush(mBw1); reverb_detail::flush(mBw2);
    }

private:
    static void fwhtN(float *a)
    {
        for (int len = 1; len < kNumLines; len <<= 1)
            for (int i = 0; i < kNumLines; i += len << 1)
                for (int j = i; j < i + len; ++j)
                { const float x = a[j], y = a[j + len]; a[j] = x + y; a[j + len] = x - y; }
    }
    static void fwhtCoupled(float *a, float gamma)
    {   // M(alpha): ALL butterfly stages scaled by gamma (orthonormal via mMixNorm). gamma=1 -> full
        // Hadamard (== fwhtN); gamma=0 -> identity (independent lines, lifetimes preserved).
        for (int len = 1; len < kNumLines; len <<= 1)
            for (int i = 0; i < kNumLines; i += len << 1)
                for (int j = i; j < i + len; ++j)
                { const float x = a[j], y = a[j + len]; a[j] = x + gamma*y; a[j + len] = gamma*x - y; }
    }
    static void fwhtHfCoupled(float *a, float g)
    {   // partial reservoir mixing (g=1 -> full fwhtHf); lower g preserves per-line lifetime spread
        for (int len = 1; len < kHfN; len <<= 1)
            for (int i = 0; i < kHfN; i += len << 1)
                for (int j = i; j < i + len; ++j)
                { const float x = a[j], y = a[j + len]; a[j] = x + g*y; a[j + len] = g*x - y; }
    }
    static void hadamardRow(int row, int *out)
    {
        for (int i = 0; i < kNumLines; ++i)
        { int b = 0, m = row & i; while (m) { b ^= 1; m &= m - 1; } out[i] = b ? -1 : 1; }
    }
    void updateGeometry()
    {
        using namespace reverb_detail;
        const double dec = std::max(0.1, (double)mT60 / kRef1kT60); // scales the matched curve; ~true 1 kHz seconds
        // DECAY-SCALED BLOOM: shift damping between broadband(lows) and shelf1 while holding their
        // SUM (the 1 kHz decay) fixed, so the low-mid bloom deepens + tone darkens with the Decay knob
        // like a real plate (ref 125/1k decay ratio runs ~1.0 short -> ~2.6 long). Anchored to the
        // committed 1.5-knob voicing at T60=2.45.
        const double kA1k = 0.70;                              // shelf1 magnitude fraction at 1 kHz (calibrated to pin T60@1k)
        const double kD1k = kDampBb + kDampG1 * kA1k;          // the actual 1 kHz damping (held constant -> T60@1k pinned)
        const double tt = (double)mT60;
        // LOW BLOOM (all-NEGATIVE -> loop gain stays <1, stable). dd>0 only (long side of anchor).
        //  rB  : extra loss-REDUCTION on the broadband (lows) -> Rspl grows -> lows ring longer (hinge term kicks in past knob ~3).
        //  lo1 : extra NEGATIVE high-shelf @kBloomF1 (~200Hz) -> steepens the 125->710 slope so mids dont over-bloom.
        // g1Eff is recomputed below from this bbEff so the 1kHz damping stays == kD1k (T60@1k pinned).
        const double dd = std::max(0.0, tt - 2.45);
        const double dh  = std::max(0.0, dd - 1.0);
        const double rB  = kBloomB1*dd*dd + kBloomC1*dh*dh;       // broadband loss-reduction (lows ring longer; hinge boosts the long tail)
        const double lo1 = -(kBloomA2*dd + kBloomB2*dd*dd);       // dB, <=0 absorptive low high-shelf
        const double Rspl = std::clamp(((0.12412*tt - 0.635407)*tt + 1.20743)*tt + 0.392912, 1.00, 3.05);
        const double bbEff = kD1k / (Rspl * (1.0 + rB)); // broadband (lows): rB extends the low ring further at long decay
        const double g1Eff = (kD1k - bbEff) / kA1k; // shelf1: keeps the 1 kHz damping == kD1k (T60@1k pinned)
        const double g2Eff = kDampG2 * std::pow(tt / 2.45, 1.7); // tone: top darkens with decay (bright short, dark long), anchored @2.45
        for (int i = 0; i < kNumLines; ++i)
        {
            int len = (int)std::round(mLineMsRt[(size_t)i] * (double)mSize * 0.001 * mFs);
            len |= 1;
            mLen[(size_t)i] = std::max(3, len);
            mDispLen[(size_t)i] = std::max(1, (int)std::round(kDispMs[(size_t)i] * 0.001 * mFs));
            mDispLen2[(size_t)i] = std::max(1, (int)std::round(kDispMs2[(size_t)i] * 0.001 * mFs));
            const double loopLen = mLen[(size_t)i] + mDispLen[(size_t)i] + mDispLen2[(size_t)i];
            const double s = loopLen / dec; // per-line dB-shape scale (length-scaled so all lines share one T60(f))
            mGain[(size_t)i] = (float)std::pow(10.0, s * bbEff / 20.0);
            mHs1[(size_t)i] = Biquad::highshelf(mFs, kDampF1, s * g1Eff, kDampS);
            mHs2[(size_t)i] = Biquad::highshelf(mFs, kDampF2, s * g2Eff, kDampS);
            mLo1[(size_t)i] = Biquad::highshelf(mFs, kBloomF1, s * lo1, kDampS);
        }
        for (size_t a = 0; a < mDiff.size(); ++a)
            mDiffLen[a] = std::max(2, (int)std::round(kDiffMs[a] * (double)mSize * 0.001 * mFs));
        mLmEq = Biquad::peaking(mFs, 160.0, 0.7, 5.0);     // low-mid body
        mPresL = Biquad::peaking(mFs, 3300.0, 0.5, 4.2);   // presence (wider/higher -> lifts the dull 2-6k vs reference)
        mDip2kL = Biquad::peaking(mFs, 2000.0, 2.4, -1.9); mDip2kR = mDip2kL; // tail 2k dip (narrow -> match 2k tonal notch, spare 2-6k lushness)
        mAirL = Biquad::highshelf(mFs, 10000.0, -1.7, 0.7); mAirR = mAirL; // trim excess >10k air -> centroid onto the reference (spares 8k octave)
        mPresR = mPresL;
        // Tone -> driver brightness (the matched damping stays fixed)
        const double drv = std::clamp((double)mDampHz * 0.5 + 2700.0, 3600.0, 10000.0);   // darker input -> lower centroid + lusher, decay untouched
        mDrvK = onePole(drv, mFs);
        mLcK = onePole(88.0, mFs);    // low-cut eased -> restores 125 Hz body (tonal) without re-lengthening the decay
        mEarlyLpK = onePole(420.0, mFs);   // early-tap low/high split (~420 Hz): below = narrow+dense, above = wide
        { double g=7.5;  mEReso1 = Biquad::peaking(mFs, 250.0, 4.2, g); } // early C80 peak 250
        { double g=-2.2;  mEReso3 = Biquad::peaking(mFs, 125.0, 2.6, g); } // early C80 cut 125
        mSideKA = onePole(1150.0, mFs); mSideKB = onePole(400.0, mFs);  // mid-band side widener band (~400-1150 Hz, catches 500)
        mSide2Hp = Biquad::highpass(mFs, 1550.0, 0.9); mSide2Lp = Biquad::lowpass(mFs, 2650.0, 0.9); // 2k side-narrower bandpass (~1.55-2.65k, steep)
        { double g=-5.2;  mEReso2 = Biquad::peaking(mFs, 500.0, 3.0, g); } // early C80 dip 500
        mHfLpK = onePole(5200.0, mFs);     // HF early-emphasis corner (targets 8k, leaves 4k intact)
        // parallel HF tail params (env-tunable; level 0 = OFF default so no-env reproduces current)
        mHfTlevel=0.15f; mHfTfb=0.994f; double hfhp=6500.0, hflp=22000.0, hfpre=6.0; mHfTilt=0.0f; // HF RESERVOIR (field-fed): dense HF late-field, populated from the decaying field -> non-exponential HF knee.
        
        
        
        
        mHfTilt=0.0f; 
        
        mHfThpK=onePole(hfhp,mFs); mHfTlpK=onePole(hflp,mFs);
        mHfTpreLen=std::max(1,(int)std::round(hfpre*0.001*mFs));
        for(int i=0;i<kHfN;++i) mHfTlen[(size_t)i]=std::max(3,(int)std::round(kHfTms[(size_t)i]*0.001*mFs));
        double bmhz=140.0; mBassMono=0.0f; // low-band mono tightening: tight mono lows like a real plate (env overrides)
        
        
        mSideLowK=onePole(bmhz,mFs);
        mCoupling=0.25f;  // alpha=0.25 re-fit baseline
        { double darkhz=5000.0; double darkg=0.85;   mDarkK=reverb_detail::onePole(darkhz,mFs); mDarkG=(float)darkg; mDarkOn=(darkg<0.999); } // DARK-BODY: gentle per-pass HF high-shelf (highs ->mDarkG each pass) -> body darkens over time, velvet supplies the top
        
        mHfRCoup=1.0f; mHfFbLo=0.0f; mHfFbHi=0.0f;
        
        
        
        mHfRNorm=(float)(1.0/std::pow(1.0+(double)mHfRCoup*mHfRCoup,1.5));
        double v_dt = std::clamp(((double)mT60-0.6)/1.0, 0.0, 1.0); // HF-tail decay taper: the ref HF-tail LENGTH tracks the knob; shrink the added tail at short decay (heavily-damped plate = no long HF). =1 at/above ~knob1.0.
        
        // ---- VELVET HF late-field config (3 sub-bands, rising t60) ----
        mVelvet=true; 
        double v_lo[3]={6500.0,8000.0,9800.0}, v_hi[3]={8500.0,10200.0,12500.0};
        double v_rat[3]={1.878,2.204,2.571};  // velvet t60 as a MULTIPLE of the 1k decay (mT60) -> tracks the knob (calibrated at the 2.45 anchor -> 4.6/5.4/6.3s)
        const double vM = 1.0 + std::clamp(((double)mT60-0.81)/(1.87-0.81),0.0,1.0); // velvet-t60 multiplier: taper 1.0(short)->2.0(anchor+)
        double v_t[3]={(double)mT60*v_rat[0]*vM,(double)mT60*v_rat[1]*vM,(double)mT60*v_rat[2]*vM}, v_lv[3]={0.020,0.018,0.015}, v_gl=0.6; // tuned: brightness-balanced (R3-off rebalance)
          
          
          
          
        
        double v_pre=45.0;  // velvet predelay (ms)
        for(int b=0;b<3;++b) mVlv[(size_t)b].config(mFs, v_lo[b], v_hi[b], v_t[b], v_gl*v_lv[b]*v_dt, v_pre);
        mVDiffOn=true;  // instant-dense front end (fixes echo-density buildup)
        double vd_ms=4.0; 
        mVDiffMix=0.5f; 
        double vd_dec=12.0; 
        double vd_lp=8000.0; 
        mVDiff.config(mFs, vd_ms, vd_dec, vd_lp, 12345u);
        mEarlyTapScale=0.6f;   // FDN early-tap gain
        mMixNorm=(float)(1.0/std::pow(1.0+(double)mCoupling*mCoupling, 2.5)); // =1/sqrt(32) at coupling=1 (all 5 stages scaled)
    }

    double mFs = 48000.0;
    std::array<FracDelayLine, kNumLines> mLine;
    std::array<FracDelayLine, 6> mDiff;
    std::array<FracDelayLine, kNumLines> mDisp, mDisp2;
    FracDelayLine mPredelay, mEarlyApR;
    std::array<Biquad, kNumLines> mHs1, mHs2;   // per-line multiband damping
    std::array<Biquad, kNumLines> mLo1;         // per-line LOW absorptive high-shelf (lows ring longer at long decay)
    std::array<float,kNumLines> mDarkS1{}, mDarkS2{}; float mDarkK=0.0f, mDarkG=1.0f; bool mDarkOn=false; // DARK-BODY feedback HF high-shelf: top loses mDarkG per pass -> body darkens gradually (velvet supplies the top)
    std::array<double, kNumLines> mLineMsRt{}; float mDispGRt=0.62f, mDispG2Rt=0.55f; // PROTO runtime overrides
    // ---- parallel diffuse HF tail (adds the non-exponential HF 'knee' a real plate has) ----
    static constexpr int kHfN = 8;
    static constexpr std::array<double,kHfN> kHfTms = {7.3,9.7,11.9,13.1,15.7,17.3,19.9,23.3};
    static constexpr std::array<int,kHfN> kHfSignL = {1,-1,1,-1,1,-1,1,-1};
    static constexpr std::array<int,kHfN> kHfSignR = {1,1,-1,-1,1,1,-1,-1};
    std::array<FracDelayLine,kHfN> mHfTline; std::array<int,kHfN> mHfTlen{}; std::array<float,kHfN> mHfTlpS{}; std::array<float,kHfN> mHfTlpS2{};
    FracDelayLine mHfTpre; int mHfTpreLen=0;
    float mHfTfb=0.974f, mHfThpS=0.0f, mHfThpS2=0.0f, mHfThpK=0.0f, mHfTlpK=0.0f, mHfTlevel=0.0f, mHfTilt=0.0f;
    float mSideLowS=0.0f, mSideLowK=0.0f, mBassMono=0.0f; // low-band mono tightening
    float mCoupling=1.0f, mMixNorm=0.0f; // FDN coupling strength (1=full Hadamard) + matched normalization
    float mHfFromField=1.0f; // reservoir fed from the decaying FIELD (gradual populate -> sharp knee) vs input
    float mHfRCoup=1.0f, mHfRNorm=0.353553f, mHfFbLo=0.0f, mHfFbHi=0.0f; // reservoir: internal coupling + per-line feedback GRADIENT (lifetime spread -> continuum)
    // ---- VELVET HF late-field (the diffuse 6-12k rising-t60 shimmer the FDN can't sustain) ----
    // Per sub-band: an 8-line PURE-GAIN FDN (decay set EXACTLY by t60, NO loop loss -> no top-damping),
    // fed from the BRIGHT full-band input (not the dark field -> no feed-starvation), input-bandpassed,
    // decorrelated L/R, summed low. 3 bands w/ rising t60 -> rising lifetime gradient. Statistically Rayleigh.
    struct VelvetBand
    {
        static constexpr int N = 8;
        std::array<FracDelayLine,N> line; std::array<int,N> len{};
        Biquad ihp1,ihp2,ilp1,ilp2;   // input 4th-order Butterworth bandpass (confine the band)
        FracDelayLine pre; int preLen=0;   // predelay: velvet emerges AFTER the early field -> raises C80/clarity
        float g=0.0f, level=0.0f;
        static constexpr std::array<double,N> ms() { return {13.7,17.9,21.3,26.1,30.7,35.3,40.9,46.7}; }
        void prepare(double fs){ const auto m=ms(); for(int i=0;i<N;++i) line[(size_t)i].prepare((int)std::ceil(m[(size_t)i]*0.001*fs)+8); pre.prepare((int)std::ceil(0.12*fs)+8); }
        void reset(){ for(auto&l:line) l.reset(); pre.reset(); ihp1.reset();ihp2.reset();ilp1.reset();ilp2.reset(); }
        void config(double fs,double lo,double hi,double t60,double lv,double prems){
            ihp1=Biquad::highpass(fs,lo,0.54119610); ihp2=Biquad::highpass(fs,lo,1.30656296);
            ilp1=Biquad::lowpass(fs,hi,0.54119610);  ilp2=Biquad::lowpass(fs,hi,1.30656296);
            const auto m=ms(); double md=0; for(double v:m) md+=v; md/=N*1000.0; // mean delay (s)
            g=(float)std::pow(10.0,-3.0*md/std::max(0.05,t60)); level=(float)lv;
            preLen=std::max(1,(int)std::round(prems*0.001*fs));
            for(int i=0;i<N;++i) len[(size_t)i]=std::max(3,(int)std::round(m[(size_t)i]*0.001*fs));
        }
        static void fwht(float*a){ for(int l=1;l<N;l<<=1) for(int i=0;i<N;i+=l<<1) for(int j=i;j<i+l;++j){ const float x=a[(size_t)j],y=a[(size_t)(j+l)]; a[(size_t)j]=x+y; a[(size_t)(j+l)]=x-y; } }
        void tick(float xin, const std::array<int,N>&sgnL, const std::array<int,N>&sgnR, float&oL, float&oR){
            if(level<=0.0f) return;
            float hin=ilp2.processSample(ilp1.processSample(ihp2.processSample(ihp1.processSample(xin)))); // 4th HP + 4th LP = bandpass
            pre.write(hin); hin=pre.readInt(preLen); // predelay -> shimmer emerges late (clarity)
            std::array<float,N> v; for(int i=0;i<N;++i) v[(size_t)i]=line[(size_t)i].readInt(len[(size_t)i]);
            float bL=0.0f,bR=0.0f; for(int i=0;i<N;++i){ bL+=sgnL[(size_t)i]*v[(size_t)i]; bR+=sgnR[(size_t)i]*v[(size_t)i]; }
            std::array<float,N> fb; for(int i=0;i<N;++i) fb[(size_t)i]=g*v[(size_t)i];
            fwht(fb.data()); const float nrm=1.0f/std::sqrt((float)N);
            for(int i=0;i<N;++i) line[(size_t)i].write(nrm*fb[(size_t)i]+hin*0.35f);
            oL+=level*nrm*bL; oR+=level*nrm*bR;
        }
    };
    std::array<VelvetBand,3> mVlv; bool mVelvet=false;
    Lfo mPLfoA, mPLfoB;   // per-line modal-smoothing modulation (de-bands the 1-3k low-mid modal field)  // velvet HF field (env VLV=1; stacks on multi-band)
    // ---- instant-dense VELVET PRE-DIFFUSER: collapse echo-density buildup (~9ms -> ~0ms) ----
    // Short dense velvet FIR on the tank input -> diffuse from sample 0 (real plate), energy-preserving
    // (1/sqrt(K)) so the matched Schroeder decay/tone is untouched. Every transient gets an instant bloom.
    struct VelvetDiff {
        static constexpr int K = 96;
        FracDelayLine buf; std::array<int,K> tp{}; std::array<float,K> sg{}; float norm=0.0f; Biquad olp1,olp2;
        void prepare(double fs){ buf.prepare((int)std::ceil(0.045*fs)+8); }
        void reset(){ buf.reset(); olp1.reset(); olp2.reset(); }
        void config(double fs,double lenMs,double decayDb,double lpHz,unsigned seed){
            unsigned s=seed?seed:1u; auto rnd=[&](){ s=s*1664525u+1013904223u; return (double)(s>>8)*(1.0/16777216.0); };
            int L=std::max(8,(int)std::round(lenMs*0.001*fs));
            double e2=0.0;
            for(int k=0;k<K;++k){ double frac=((double)k+rnd())/(double)K; tp[(size_t)k]=std::max(0,(int)std::round(frac*L));
                double env=std::pow(10.0,-decayDb/20.0*frac); // decaying envelope -> transient preserved (clarity) + dense
                sg[(size_t)k]=(float)(((rnd()<0.5)?-1.0:1.0)*env); e2+=env*env; }
            norm=1.0f/std::sqrt((float)e2); // energy-preserving
            olp1=Biquad::lowpass(fs,lpHz,0.54119610); olp2=Biquad::lowpass(fs,lpHz,1.30656296); // tame the velvet whitening
        }
        float process(float x){ buf.write(x); float o=0.0f; for(int k=0;k<K;++k) o+=sg[(size_t)k]*buf.readInt(tp[(size_t)k]); return olp2.processSample(olp1.processSample(norm*o)); }
    };
    VelvetDiff mVDiff; bool mVDiffOn=false; float mVDiffMix=1.0f; // instant-dense front end (env DIFF=1, DIFFMIX blend)
    float mEarlyTapScale=1.0f;                  // FDN early-tap gain (was mEarlyTapScale)
    Biquad mLmEq, mPresL, mPresR;               // input body + output presence
    Biquad mEReso1, mEReso2, mEReso3;           // early-path C80 resonance shaper (125 dip / 250 peak / 500 dip)
    Biquad mSide2Hp, mSide2Lp;                  // 2k side-narrower bandpass
    Biquad mDip2kL, mDip2kR;                    // 2k tail tonal notch
    Biquad mAirL, mAirR;                        // >8k air trim (centroid match)
    std::array<int, kNumLines> mLen{};
    std::array<int, 6> mDiffLen{};
    std::array<int, kNumLines> mDispLen{}, mDispLen2{};
    std::array<float, kNumLines> mGain{};
    std::array<float, 1> mDampStateUnused{};    // (kept for denormal-flush symmetry)
    int mEarlyLenR = 180;
    int mSignL[kNumLines]{}, mSignR[kNumLines]{}, mInj[kNumLines]{};
    float mBw1 = 0.0f, mBw2 = 0.0f, mLcL = 0.0f, mLcR = 0.0f, mEarlyLp = 0.0f;
    float mDrvK = 1.0f, mLcK = 0.0f, mEarly = 0.56f, mEarlyLowGain = 1.10f, mEarlyLpK = 0.05f, mHighCorr = 0.35f;
    float mHfLp = 0.0f, mHfLpK = 0.20f, mHfEarly = 1.15f;   // HF early emphasis (two-slope bright attack)
    float mSideLpA = 0.0f, mSideLpB = 0.0f, mSideKA = 0.1f, mSideKB = 0.03f, mMidWidth = 0.24f; // 500-1k side widener
    float mNarrow2k = 0.14f;                    // 2k side narrower (reference width notch)
    float mSize = 1.25f, mT60 = 2.92f, mDampHz = 5250.0f, mPredelayMs = 0.0f;
    bool mPrepared = false, mFreeze = false;
};

// ===========================================================================
// DispersionChain — the spring "boing" / sproing. A cascade of first-order
// STRETCHED allpasses A(z^K) applied to the INPUT, OUTSIDE the FDN feedback loop
// (dispersion INSIDE the loop always rings metallic — tested repeatedly). Each
// stage's K-sample internal delay makes the group delay frequency-dependent, so
// a transient smears into the descending chirp the ear reads as a spring. K is
// derived from a TIME constant (not a fixed sample count) so the chirp tone is
// identical at 44.1 / 48 / 96 kHz. The number of ACTIVE stages = boing amount,
// so 0 stages == bypass == the exact pre-boing signal. Ported faithfully
// from spring_tuner.html (APk: K=4 @48 kHz, coef 0.62, 120-stage cascade).
// ===========================================================================
class DispersionChain
{
public:
    static constexpr int kMaxStages = 200;  // full cascade length (longer = more developed chirp train)
    static constexpr int kMaxK      = 96;   // K-sample cap (>= stretchSec * 192 kHz)

    void prepare(double fs, double stretchSec, float coef)
    {
        mK    = std::clamp((int)std::lround(stretchSec * fs), 1, kMaxK);
        mCoef = coef;
        reset();
    }
    void reset()
    {
        for (auto &s : mStage) { s.xb.fill(0.0f); s.yb.fill(0.0f); s.w = 0; }
    }
    // nStages = active cascade length (clamped 0..kMaxStages); 0 -> passthrough.
    inline float process(float x, int nStages)
    {
        const int m = std::clamp(nStages, 0, kMaxStages);
        const float a = mCoef;
        const int k = mK;
        for (int i = 0; i < m; ++i)
        {
            Stage &s = mStage[(size_t)i];
            const float xk = s.xb[(size_t)s.w], yk = s.yb[(size_t)s.w];
            const float y = a * x + xk - a * yk;   // A(z^K) = (a + z^-K)/(1 + a z^-K)
            s.xb[(size_t)s.w] = x;  s.yb[(size_t)s.w] = y;
            if (++s.w >= k) s.w = 0;
            x = y;
        }
        return x;
    }

private:
    struct Stage { std::array<float, kMaxK> xb{}, yb{}; int w = 0; };
    std::array<Stage, kMaxStages> mStage{};
    int   mK    = 4;
    float mCoef = 0.62f;
};

// ===========================================================================
// SpringReverb — studio-spring-voiced reverb (FDN realization, 2026-06-19 rework).
// Matched to a studio-spring reference via the offline optimizer + ear
// voicing ("41_tone-fix"), then ported faithfully to real time: a single dense
// 8-line Householder FDN (same mixing/feedback as the offline model) fed by a
// DELAYED low-band injection so the low-mids BLOOM ~50-80 ms after the immediate
// highs (the "alive" swell). RAW (pre-damp) line reads go to the output so the
// attack keeps its highs; the feedback path is HF-damped so the highs decay
// faster than the lows (the studio-spring lows-ring-longest tilt). Voicing: 110 Hz low-cut
// + Tone-driven input darkening + a 2.5 kHz presence dip + a 2nd-order output
// band-limit give the spring's warm, mid-focused tone. Stereo = Python sign-pattern
// decorrelation with a width blend. Decay = RT60, Tone = brightness, Tension =
// bloom amount. Renders WET only. (Old parallel-spring engine kept in git history.)
// ===========================================================================
class SpringReverb
{
public:
    static constexpr int kLines = 8;
    // line lengths (ms): fdn_base 16.39 * (1 + (ratio-1)*spread), spread 0.831
    static constexpr std::array<double, kLines> kLineMs = {
        16.39, 18.84, 21.43, 23.74, 26.33, 28.78, 31.65, 34.24};
    static constexpr double kLowCutHz   = 110.0;   // input low-cut (sub the spring lacks)
    static constexpr double kBloomFc    = 422.0;   // split: lows delayed, highs immediate
    static constexpr double kBloomDelMs = 52.0;    // low-band injection delay -> the bloom
    static constexpr double kPresenceHz = 2550.0;  // -2.5 dB presence dip
    static constexpr double kPresenceDb = -2.5;
    static constexpr double kPresenceQ  = 1.0;
    static constexpr double kTiltDampHz = 20000.0; // feedback HF damping -> lows ring longest (eased: HF rings longer over the long tail, ear-approved)
    static constexpr double kCutMul     = 1.60;    // output band-limit = Tone * this (2nd-order)
    static constexpr double kModMs      = 0.4;     // subtle spring sag
    static constexpr double kDecayComp  = 1.05;    // gain T60 vs knob (offsets feedback damping)
    static constexpr double kDarkScale  = 1.10;    // input dark cutoff vs Tone knob
    static constexpr double kDispStretchSec = 16.0 / 48000.0; // K=16 @48k (333us) — dense in-band chirp streaks, SR-independent
    static constexpr float  kDispCoef       = 0.62f;          // chirp depth (tuner apa default)

    void prepare(double fs)
    {
        mFs = fs;
        const double head = kModMs * 0.001 * mFs + 8.0;
        for (int i = 0; i < kLines; ++i)
            mLine[(size_t)i].prepare((int)std::ceil(kLineMs[(size_t)i] * 0.001 * mFs + head) + 8);
        mBloomDelay.prepare((int)std::ceil(kBloomDelMs * 0.001 * mFs) + 16);
        mDecL.prepare((int)std::ceil(0.02 * mFs) + 16); mDecR.prepare((int)std::ceil(0.02 * mFs) + 16);
        mDecLLen = (int)std::round(0.0073 * mFs); mDecRLen = (int)std::round(0.0131 * mFs);
        mLfo.prepare(mFs); mLfo.setRateHz(0.4f);
        // Householder vector u (||u||=1): H8 = I - 2 u u^T  (same as the offline model)
        const float uv[kLines] = {1,1,1,1,1,1,1,-1};
        float nrm = 0.0f; for (int i = 0; i < kLines; ++i) nrm += uv[i]*uv[i];
        nrm = std::sqrt(nrm); for (int i = 0; i < kLines; ++i) mU[(size_t)i] = uv[i]/nrm;
        designFilters();
        updateGeometry();
        mDisp.prepare(mFs, kDispStretchSec, kDispCoef);
        reset();
        mPrepared = true;
    }

    void reset()
    {
        for (auto &l : mLine) l.reset();
        mBloomDelay.reset(); mDecL.reset(); mDecR.reset();
        mInHp = mInLp = mLowLp = 0.0f;
        std::fill(mDamp.begin(), mDamp.end(), 0.0f);
        for (auto &z : mPresL) z = 0.0f; for (auto &z : mPresR) z = 0.0f;
        for (auto &z : mCutL)  z = 0.0f; for (auto &z : mCutR)  z = 0.0f;
        mDisp.reset();
        mLfo.reset();
    }

    void setDecaySeconds(float t60) { mT60 = std::max(0.1f, t60); mDirty = true; }
    void setDampHz(float hz) { mDampHz = std::clamp(hz, 600.0f, 16000.0f); mDirty = true; } // Tone = brightness
    void setTension(float t) { mTension = std::clamp(t, 0.0f, 1.0f); }                       // 0 = loose (max bloom/sag), 1 = tight (no bloom)
    void setBoing(float b) { mBoing = std::clamp(b, 0.0f, 1.0f); }                            // 0 = none .. 1 = full sproing (input dispersion)
    void setFreeze(bool f) { mFreeze = f; }

    void process(float *left, float *right, int numSamples)
    {
        using namespace reverb_detail;
        if (mDirty) recompute();
        const bool stereo = (left != right);
        const float inGain = mFreeze ? 0.0f : 1.0f;
        const float bloomMix = 0.72f * (1.0f - mTension);   // Tension UP = tighter spring = less bloom
        const int dispStages = (int)std::lround(mBoing * (float)DispersionChain::kMaxStages); // boing -> cascade depth
        const float modS = (float)(kModMs * 0.001 * mFs);
        const int bloomLen = std::max(1, (int)std::round(kBloomDelMs * 0.001 * mFs));
        for (int n = 0; n < numSamples; ++n)
        {
            const float dryL = left[n];
            const float dryR = stereo ? right[n] : dryL;
            float x = 0.5f * (dryL + dryR);
            mInHp += mHpK * (x - mInHp); x = x - mInHp;          // one-pole high-pass (low-cut)
            mInLp += mDarkK * (x - mInLp); x = mInLp;            // one-pole low-pass (Tone/dark)
            x = mDisp.process(x, dispStages);                   // input-side dispersion = the boing (outside the FDN loop)
            mLowLp += mBloomK * (x - mLowLp);                    // split at bloomFc
            const float lo = mLowLp, hi = x - lo;
            mBloomDelay.write(lo);
            const float fdnIn = inGain * (hi + bloomMix * mBloomDelay.readInt(bloomLen));

            std::array<float, kLines> D, V;
            for (int i = 0; i < kLines; ++i)
            {
                const double md = (double)mLen[(size_t)i] + (double)(modS * mLfo.value((double)i * 0.25));
                D[(size_t)i] = mLine[(size_t)i].readFrac(md);    // RAW read -> output (keeps attack highs)
                auto &z = mDamp[(size_t)i]; z += mDampK * (D[(size_t)i] - z); V[(size_t)i] = z; // damped -> feedback
            }
            // output: width blend of common + sign-decorrelated (Python _stereo)
            float wetL = 0.0f, wetR = 0.0f;   // interleaved half-sums: decorrelated (wide) yet
            for (int i = 0; i < kLines; ++i) { if (i & 1) wetR += D[(size_t)i]; else wetL += D[(size_t)i]; } // sum-preserving (keeps low-mid body + bloom, unlike zero-sum rows)
            // feedback: Householder mix of damped V, scalar g, inject fdnIn equally
            std::array<float, kLines> fbv;
            for (int i = 0; i < kLines; ++i) fbv[(size_t)i] = (mFreeze ? 1.0f : mGain[(size_t)i]) * V[(size_t)i];
            float s = 0.0f; for (int i = 0; i < kLines; ++i) s += mU[(size_t)i]*fbv[(size_t)i];
            s *= 2.0f;
            for (int i = 0; i < kLines; ++i)
                mLine[(size_t)i].write((fbv[(size_t)i] - s*mU[(size_t)i]) + fdnIn);

            wetL = allpassInt(mDecL, mDecLLen, 0.7f, wetL);   // per-channel allpass decorrelation
            if (stereo) wetR = allpassInt(mDecR, mDecRLen, 0.7f, wetR);
            float yl = biquad(mCutL, mCutB, biquad(mPresL, mPresB, mOutGain*wetL));
            left[n] = yl;
            if (stereo) right[n] = biquad(mCutR, mCutB, biquad(mPresR, mPresB, mOutGain*wetR));
            mLfo.advance();
        }
        for (auto &z : mDamp) flush(z);
        flush(mInHp); flush(mInLp); flush(mLowLp);
    }

private:
    struct BQ { float b0=1,b1=0,b2=0,a1=0,a2=0; };
    static void peakCoef(BQ &c, double f, double q, double gdb, double fs)
    {
        using namespace reverb_detail;
        const double A=std::pow(10.0,gdb/40.0), w0=2.0*kPi*f/fs, al=std::sin(w0)/(2.0*q), cs=std::cos(w0);
        const double a0=1.0+al/A;
        c.b0=(float)((1.0+al*A)/a0); c.b1=(float)((-2.0*cs)/a0); c.b2=(float)((1.0-al*A)/a0);
        c.a1=(float)((-2.0*cs)/a0);  c.a2=(float)((1.0-al/A)/a0);
    }
    static void lpCoef(BQ &c, double f, double q, double fs)
    {
        using namespace reverb_detail;
        f=std::min(f,0.45*fs); const double w0=2.0*kPi*f/fs, al=std::sin(w0)/(2.0*q), cs=std::cos(w0);
        const double a0=1.0+al, b1=1.0-cs;
        c.b0=(float)((b1*0.5)/a0); c.b1=(float)(b1/a0); c.b2=(float)((b1*0.5)/a0);
        c.a1=(float)((-2.0*cs)/a0); c.a2=(float)((1.0-al)/a0);
    }
    static inline float biquad(std::array<float,4> &s, const BQ &c, float x)
    {
        const float y = c.b0*x + c.b1*s[0] + c.b2*s[1] - c.a1*s[2] - c.a2*s[3];
        s[1]=s[0]; s[0]=x; s[3]=s[2]; s[2]=y; return y;
    }
    void designFilters() { peakCoef(mPresB, kPresenceHz, kPresenceQ, kPresenceDb, mFs); }
    void recompute() { updateGeometry(); mDirty = false; }
    void updateGeometry()
    {
        using namespace reverb_detail;
        for (int i = 0; i < kLines; ++i) {
            mLen[(size_t)i] = std::max(3, (int)std::round(kLineMs[(size_t)i]*0.001*mFs));
            mGain[(size_t)i] = (float)std::pow(10.0, -3.0*(double)mLen[(size_t)i]/(((double)mT60*kDecayComp)*mFs));
        }
        mHpK    = onePole(kLowCutHz, mFs);
        mBloomK = onePole(kBloomFc, mFs);
        mDampK  = onePole(kTiltDampHz, mFs);
        mDarkK  = onePole((double)mDampHz*kDarkScale, mFs);
        lpCoef(mCutB, std::min((double)mDampHz*kCutMul, 0.45*mFs), 0.707, mFs);
    }

    double mFs = 48000.0;
    std::array<FracDelayLine, kLines> mLine;
    FracDelayLine mBloomDelay, mDecL, mDecR;
    DispersionChain mDisp;
    int mDecLLen=350, mDecRLen=629;
    Lfo mLfo;
    std::array<int, kLines> mLen{};
    std::array<float, kLines> mDamp{};
    std::array<float, kLines> mGain{};
    float mU[8]{};
    BQ mPresB, mCutB;
    std::array<float,4> mPresL{}, mPresR{}, mCutL{}, mCutR{};
    float mInHp=0, mInLp=0, mLowLp=0;
    float mHpK=0.01f, mBloomK=0.05f, mDampK=0.3f, mDarkK=0.4f;
    float mT60=3.0f, mDampHz=3871.0f, mTension=0.5f, mBoing=0.20f, mOutGain=2.0f;
    bool mPrepared=false, mDirty=true, mFreeze=false;
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
                { z += mDampK * (o - z); o = z; } // damp in freeze too (kills HF buildup)
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

            // freeze stops the octave re-injection so the wash holds instead of
            // climbing forever (the climbing was the bright freeze noise).
            const float fb = mFreeze ? 0.0f : std::tanh(mShimmer * 0.9f * pitched);
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
    float mT60 = 4.0f, mDampHz = 7000.0f, mPredelayMs = 30.0f, mShimmer = 0.5f, mMod = 0.4f; [[maybe_unused]] float mMix = 0.3f;
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

            // Equal-power crossfade: the wet is decorrelated from the dry, so a
            // linear blend dips ~3 dB mid-sweep; sqrt gains keep perceived loudness
            // flat across the knob. g==0 -> exactly dry (mix=0 stays bit-exact).
            const float g = std::clamp(mMixZ, 0.0f, 1.0f);
            const float gWet = std::sqrt(g);
            const float gDry = std::sqrt(1.0f - g);
            left[n] = gDry * dryL[n] + gWet * wL;
            if (stereo)
                right[n] = gDry * dryR[n] + gWet * wR;
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
// SmallRoomFdn — short-line 8-channel FWHT feedback delay network voiced as a
// small room. Smooth/diffuse by design (no comb coloration), but its SHORT delay
// lines let it decay fast and FLAT across frequency (RT60 ~0.15-0.8s) — matching
// a real small dead room (profiled from a Convology "House Den" sweep capture:
// RT60 ~0.29s flat, warm centroid ~2.4kHz, diffuse by ~15ms, wide). Tone darkens
// at the INPUT (like the plate's bandwidth) + a low-mid warmth shelf; no output EQ.
// Renders WET only.
// ===========================================================================
class SmallRoomFdn
{
public:
    static constexpr int kN = 16;

    void prepare(double fs)
    {
        mFs = fs;
        for (int i = 0; i < kN; ++i)
            mLine[(size_t)i].prepare((int)std::ceil(kBaseMs[(size_t)i] * 2.2 * 0.001 * fs) + 16);
        for (int k = 0; k < kNap; ++k) mAp[(size_t)k].prepare((int)std::ceil(kApMs[(size_t)k] * 0.001 * fs) + 8);
        for (int k = 0; k < 2; ++k) { mEarlyLen[(size_t)k] = std::max(2, (int)std::round(kEarlyMs[(size_t)k] * 0.001 * fs)); mEarly[(size_t)k].prepare(mEarlyLen[(size_t)k] + 8); }
        mPredelay.prepare((int)std::ceil(0.08 * fs) + 8);
        mLfo.prepare(fs); mLfo.setRateHz(0.6f);
        mUmidEq = Biquad::peaking(fs, 1850.0, 0.9, -4.5); // tame the FDN upper-mid (den dips there)
        mBoxEq = Biquad::peaking(fs, 540.0, 0.65, -3.0);  // guitar: scoop the boxy 400-900Hz (sits under the amp)
        mHiCut = Biquad::lowpass(fs, 10000.0, 0.5);       // guitar: gentle safety rolloff (cab already limits the input)
        mLoK = (float)reverb_detail::onePole(350.0, fs);
        setToneHz(1750.0f);
        setSize(1.0f);
        reset(); mPrepared = true;
    }
    void reset()
    {
        for (auto &l : mLine) l.reset();
        for (auto &l : mAp) l.reset();
        mPredelay.reset(); mLfo.reset(); for (auto &l : mEarly) l.reset(); mLoSh = 0.0f; mUmidEq.reset(); mBoxEq.reset(); mHiCut.reset();
        mInLp = 0.0f;
    }
    void setDecaySeconds(float t60) { mT60 = std::max(0.05f, t60); mDirty = true; }
    void setToneHz(float hz) { mToneHz = std::clamp(hz, 600.0f, 8000.0f); mInLpK = (float)reverb_detail::onePole(mToneHz, mFs); }
    void setPredelayMs(float ms) { mPredelayMs = std::clamp(ms, 0.0f, 80.0f); mPreSamp = std::max(1, (int)std::round((double)mPredelayMs * 0.001 * mFs)); }
    void setModDepth(float d) { mMod = std::clamp(d, 0.0f, 1.0f); }
    // Size = room dimensions: a curved scale of the delay lines (small tight booth ..
    // big roomy space) plus a size-dependent low-mid body. Default (1.0) = the House Den match.
    void setSize(float s)
    {
        mSize = std::clamp(s, 0.5f, 1.5f);
        const double scale = std::clamp(std::pow((double)mSize, 1.7), 0.42, 2.05); // dramatic but bounded
        for (int i = 0; i < kN; ++i) mLen[(size_t)i] = std::max(2, (int)std::round(kBaseMs[(size_t)i] * scale * 0.001 * mFs));
        for (int k = 0; k < kNap; ++k) mApLen[(size_t)k] = std::max(2, (int)std::round(kApMs[(size_t)k] * 0.001 * mFs));
        mLoG = (float)std::clamp(0.80 + (scale - 1.0) * 0.30, 0.30, 1.30); // low-shelf gain: den is ~5dB fuller below ~350Hz; more with Size
        mDirty = true;
    }
    void setFreeze(bool f) { mFreeze = f; }

    void process(float *left, float *right, int numSamples)
    {
        using namespace reverb_detail;
        if (mDirty) recompute();
        const bool stereo = (left != right);
        const float fb = mFreeze ? 1.0f : mFb;
        const float inG = mFreeze ? 0.0f : 1.0f;
        const float modS = mFreeze ? 2.0f : (mMod * 3.0f); // while frozen: a fixed gentle modulation slowly detunes the held FDN eigenmodes so they don't ring as fixed metallic pitches (the "kooky" freeze). Live path (mMod*3) unchanged.
        for (int n = 0; n < numSamples; ++n)
        {
            mPredelay.write(inG * 0.5f * (left[n] + (stereo ? right[n] : left[n])));
            float in = mPredelay.readInt(mPreSamp);            // predelay
            for (int k = 0; k < kNap; ++k) in = allpassInt(mAp[(size_t)k], mApLen[(size_t)k], 0.65f, in); // input diffuser cascade -> dense diffuse onset
            mInLp += mInLpK * (in - mInLp); in = mInLp;        // input darkening (Tone)
            mLoSh += mLoK * (in - mLoSh); in += mLoG * mLoSh; // low-shelf: den warmth below ~350Hz
            in = mUmidEq.processSample(in);                    // tame upper-mid
            in = mBoxEq.processSample(in);                     // scoop boxy low-mids (guitar)
            in = mHiCut.processSample(in);                     // cab-style top rolloff (guitar)
            float d[kN];
            for (int i = 0; i < kN; ++i) d[i] = mLine[(size_t)i].readFrac((double)mLen[(size_t)i] + (double)(modS * mLfo.value((double)i * 0.13)));
            float h[kN]; for (int i = 0; i < kN; ++i) h[i] = d[i];
            for (int s = 1; s < kN; s <<= 1) for (int i = 0; i < kN; i += s << 1) for (int j = i; j < i + s; ++j) { float a = h[j], b = h[j + s]; h[j] = a + b; h[j + s] = a - b; }
            for (int i = 0; i < kN; ++i) mLine[(size_t)i].write(fb * h[i] * kFwhtNorm + in * kInInject);
            float el = allpassInt(mEarly[0], mEarlyLen[0], 0.6f, in);  // immediate early energy (decorrelated)
            float er = allpassInt(mEarly[1], mEarlyLen[1], 0.6f, in);
            float l = 0.0f, r = 0.0f;
            for (int i = 0; i < kN; ++i) { float sgn = (i & 1) ? -1.0f : 1.0f; l += d[i] * ((i % 3) ? 1.0f : 0.7f); r += d[i] * sgn * ((i % 2) ? 0.8f : 1.0f); }
            left[n] = kEarlyG * el + kTailG * 0.5f * l; if (stereo) right[n] = kEarlyG * er + kTailG * 0.5f * r;
            mLfo.advance();
        }
        flush(mInLp);
    }

private:
    void recompute()
    {
        double loopMs = 0; for (int i = 0; i < kN; ++i) loopMs += mLen[(size_t)i] / mFs * 1000.0; loopMs /= kN;
        mFb = (float)std::clamp(std::pow(10.0, -3.0 * loopMs / ((double)mT60 * 1000.0)), 0.0, 0.995);
        mDirty = false;
    }
    static constexpr double kBaseMs[kN] = {6.7, 8.3, 9.7, 11.3, 12.9, 14.3, 15.9, 17.3,
                                            18.9, 20.3, 21.7, 23.3, 24.7, 26.1, 27.7, 29.3};
    static constexpr float kFwhtNorm = 0.25f; // 1/sqrt(16)
    static constexpr float kInInject = 0.35f;
    double mFs = 48000.0;
    std::array<FracDelayLine, kN> mLine;
    std::array<int, kN> mLen{};
    static constexpr int kNap = 3;
    static constexpr double kApMs[kNap] = {2.3, 3.7, 5.9};
    std::array<FracDelayLine, kNap> mAp;
    std::array<int, kNap> mApLen{};
    FracDelayLine mPredelay;
    Lfo mLfo;
    static constexpr double kEarlyMs[2] = {7.3, 9.7};
    std::array<FracDelayLine, 2> mEarly;
    std::array<int, 2> mEarlyLen{};
    static constexpr float kEarlyG = 0.85f, kTailG = 0.5f;
    float mInLp = 0.0f, mInLpK = 0.0f;
    Biquad mUmidEq, mBoxEq, mHiCut;
    float mLoSh = 0.0f, mLoK = 0.0f, mLoG = 0.8f;
    float mFb = 0.0f, mT60 = 0.3f, mToneHz = 1750.0f, mSize = 1.0f;
    float mPredelayMs = 0.0f, mMod = 0.0f; int mPreSamp = 1;
    bool mFreeze = false, mPrepared = false, mDirty = true;
};

// ===========================================================================
// HallReverb - dense dispersion-FDN concert hall (Plate-family engine, hall-voiced).
// Lush via strong in-loop HF damping (sparse distinct modes -> high modal depth);
// C80 clarity via an early-energy tap; bass-mono + 2.7kHz presence voicing; Tone ->
// HF damping (warm = lusher, bright = more present). Built from the dispersion-FDN
// that powers the Plate (Robbie's approved smooth engine), scaled/voiced to match
// the averaged real-hall metrics (Usina del Arte) + Bricasti Gold. Renders WET only.
// ===========================================================================
class HallReverb {
public:
    static constexpr int kNumLines = 32;
    // hall line lengths (ms @ size 1.0) ~9.75..98 ms (plate set x1.5) -> dense hall modes
    static constexpr std::array<double,kNumLines> kLineMs = {
        9.75,11.85,13.8,15.9,18.15,20.55,22.8,25.2,27.75,30.15,32.7,35.4,37.95,40.65,43.5,46.2,
        49.05,51.9,54.9,57.75,60.75,63.9,67.05,70.2,73.5,76.8,80.1,83.55,87.0,90.6,94.2,97.95};
    static constexpr std::array<double,6> kDiffMs = {0.7,1.3,2.1,3.1,4.3,5.9}; // immediate dense onset (Plate-style)
    static constexpr std::array<double,kNumLines> kDispMs = {
        0.7,1.0,1.3,1.6,0.8,1.1,1.4,1.7,0.9,1.2,1.5,1.8,2.1,2.4,2.0,2.3,
        2.6,2.9,1.9,2.2,2.5,2.8,3.1,3.4,2.7,3.0,3.3,1.5,1.8,2.1,2.4,2.7};
    static constexpr float kDispG = 0.62f;
    static constexpr std::array<double,kNumLines> kDispMs2 = {
        1.9,2.3,1.1,2.7,1.5,3.1,1.3,2.9,1.7,2.5,1.0,3.3,1.4,2.1,2.8,1.2,
        3.0,1.6,2.4,1.8,3.2,1.5,2.6,2.0,1.1,3.4,1.9,2.2,1.3,2.7,1.6,3.1};
    static constexpr float kDispG2 = 0.55f;
    static constexpr float kMinSize = 0.7f, kMaxSize = 1.5f;

    void prepare(double fs){
        mFs=fs;
        for(int i=0;i<kNumLines;++i) mLine[(size_t)i].prepare((int)std::ceil(kLineMs[(size_t)i]*kMaxSize*0.001*fs)+16);
        for(size_t i=0;i<mDiff.size();++i) mDiff[i].prepare((int)std::ceil(kDiffMs[i]*kMaxSize*0.001*fs)+8);
        for(int i=0;i<kNumLines;++i){ mDisp[(size_t)i].prepare((int)std::ceil(kDispMs[(size_t)i]*0.001*fs)+8);
            mDisp2[(size_t)i].prepare((int)std::ceil(kDispMs2[(size_t)i]*0.001*fs)+8); }
        mPredelay.prepare((int)std::ceil(0.2*fs)+8);
        mErApL.prepare((int)std::ceil(7.3*0.001*fs)+8); mErApLenL=(int)std::round(7.3*0.001*fs);
        mErApR.prepare((int)std::ceil(11.9*0.001*fs)+8); mErApLenR=(int)std::round(11.9*0.001*fs);
        mErApL2.prepare((int)std::ceil(3.1*0.001*fs)+8); mErApLenL2=(int)std::round(3.1*0.001*fs);
        mErApR2.prepare((int)std::ceil(4.7*0.001*fs)+8); mErApLenR2=(int)std::round(4.7*0.001*fs);
        mLfoA.prepare(fs); mLfoA.setRateHz(0.43f); mLfoB.prepare(fs); mLfoB.setRateHz(0.67f);
        hadamardRow(1,mSignL); hadamardRow(2,mSignR); hadamardRow(13,mInj);
        updateGeometry(); reset(); mPrepared=true;
    }
    void reset(){
        for(auto&l:mLine)l.reset(); for(auto&d:mDiff)d.reset(); for(auto&d:mDisp)d.reset(); for(auto&d:mDisp2)d.reset();
        mPredelay.reset(); std::fill(mDamp.begin(),mDamp.end(),0.0f); std::fill(mLf.begin(),mLf.end(),0.0f);
        mErApL.reset(); mErApR.reset(); mErApL2.reset(); mErApR2.reset();
        mBw1=mBw2=0; mLcL=mLcR=0; mLfoA.reset(); mLfoB.reset(); mSideHp=0; for(auto&v:mPzL)v=0; for(auto&v:mPzR)v=0;
    }
    void setSize(float s){ s=std::clamp(s,kMinSize,kMaxSize); if(s!=mSize){mSize=s; if(mPrepared)updateGeometry();} }
    void setDecaySeconds(float t){ t=std::max(0.1f,t); if(t!=mT60){mT60=t; if(mPrepared)updateGeometry();} }
    void setDampHz(float hz){ if(hz!=mDampHz){mDampHz=hz; if(mPrepared)updateGeometry();} }
    void setPredelayMs(float ms){ mPredelayMs=std::clamp(ms,0.0f,200.0f); }
    void setModDepth(float d){ mModSamp=std::clamp(d,0.0f,1.0f)*6.0f; } // 0..6 sample excursion
    void setFreeze(bool f){ mFreeze=f; }
    int lineLengthSamples(int i) const { return mLoopLen[(size_t)i]; } // full loop length (incl. dispersion)
    float lineGain(int i) const { return mGain[(size_t)i]; }

    void process(float* left,float* right,int n){
        using namespace reverb_detail;
        const bool stereo=(left!=right);
        const int pre=(int)std::round((double)mPredelayMs*0.001*mFs);
        const float inGain=mFreeze?0.0f:1.0f;
        const float invsq=1.0f/std::sqrt((float)kNumLines);
        const float modS=mFreeze?0.0f:mModSamp;
        for(int s=0;s<n;++s){
            const float dryL=left[s], dryR=stereo?right[s]:dryL;
            mPredelay.write(0.5f*(dryL+dryR));
            float x=inGain*mPredelay.readInt(std::max(1,pre));
            mBw1+=mDrvK*(x-mBw1); mBw2+=mDrvK*(mBw1-mBw2); x=mBw2;     // 2-pole wideband driver
            for(size_t a=0;a<mDiff.size();++a) x=allpassInt(mDiff[a],mDiffLen[a],0.62f,x); // input diffusion
            std::array<float,kNumLines> o;
            for(int i=0;i<kNumLines;++i){
                float r;
                if(modS>0.0f){ const float m=modS*(0.6f*mLfoA.value((double)i*0.033)+0.4f*mLfoB.value((double)i*0.051+0.02));
                              r=mLine[(size_t)i].readFrac((double)mLen[(size_t)i]+(double)m); }
                else r=mLine[(size_t)i].readInt(mLen[(size_t)i]);
                if(!mFreeze){ auto&z=mDamp[(size_t)i]; z+=mCornerK*(r-z); r-=mShelfG[(size_t)i]*(r-z);   // in-loop high-shelf HF damping
                              mLf[(size_t)i]+=mLfK*(r-mLf[(size_t)i]); r-=mLfDamp*mLf[(size_t)i]; } // LF damping -> hall arch
                else { auto&z=mDamp[(size_t)i]; z+=mFreezeK*(r-z); r=z; } // freeze: gentle damping -> stable infinite-ish sustain
                o[(size_t)i]=r;
            }
            float wetL=0,wetR=0; for(int i=0;i<kNumLines;++i){ wetL+=mSignL[i]*o[(size_t)i]; wetR+=mSignR[i]*o[(size_t)i]; }
            float eL=allpassInt(mErApL,mErApLenL,0.6f,x), eR=allpassInt(mErApR,mErApLenR,0.6f,x);
            eL=allpassInt(mErApL2,mErApLenL2,0.6f,eL); eR=allpassInt(mErApR2,mErApLenR2,0.6f,eR); // 2-stage decorrelation -> wide early field (matches real-hall stereo)
            float oL=invsq*wetL+mEarly*eL, oR=invsq*wetR+mEarly*eR;
            mLcL+=mLcK*(oL-mLcL); oL-=mLcL; mLcR+=mLcK*(oR-mLcR); oR-=mLcR; // low-cut
            // presence peak (per channel, RBJ biquad DF1)
            { float y=pb0*oL+pb1*mPzL[0]+pb2*mPzL[1]-pa1*mPzL[2]-pa2*mPzL[3]; mPzL[1]=mPzL[0];mPzL[0]=oL;mPzL[3]=mPzL[2];mPzL[2]=y; oL=y; }
            { float y=pb0*oR+pb1*mPzR[0]+pb2*mPzR[1]-pa1*mPzR[2]-pa2*mPzR[3]; mPzR[1]=mPzR[0];mPzR[0]=oR;mPzR[3]=mPzR[2];mPzR[2]=y; oR=y; }
            // bass-mono: high-pass the side so deep lows collapse to centre (real-hall diffuse-field signature)
            if(stereo){ float mid=0.5f*(oL+oR),sd=0.5f*(oL-oR); mSideHp+=mSideHpK*(sd-mSideHp); float sh=sd-mSideHp; oL=mid+sh; oR=mid-sh; }
            left[s]=mOutGain*oL; if(stereo) right[s]=mOutGain*oR;
            std::array<float,kNumLines> fb;
            for(int i=0;i<kNumLines;++i){
                float v=(mFreeze?1.0f:mGain[(size_t)i])*o[(size_t)i];
                v=allpassInt(mDisp[(size_t)i],mDispLen[(size_t)i],kDispG*mDispScale,v);    // in-loop dispersion (silky density)
                v=allpassInt(mDisp2[(size_t)i],mDispLen2[(size_t)i],kDispG2*mDispScale,v);
                fb[(size_t)i]=v;
            }
            fwhtN(fb.data());
            const float injIn=0.5f*x;
            for(int i=0;i<kNumLines;++i) mLine[(size_t)i].write(invsq*fb[(size_t)i]+(float)mInj[i]*injIn);
            mLfoA.advance(); mLfoB.advance();
        }
        for(auto&z:mDamp)flush(z); for(auto&z:mLf)flush(z); flush(mBw1); flush(mBw2);
    }
private:
    static void fwhtN(float*a){ for(int len=1;len<kNumLines;len<<=1) for(int i=0;i<kNumLines;i+=len<<1) for(int j=i;j<i+len;++j){const float x=a[j],y=a[j+len];a[j]=x+y;a[j+len]=x-y;} }
    static void hadamardRow(int row,int*out){ for(int i=0;i<kNumLines;++i){int b=0,m=row&i;while(m){b^=1;m&=m-1;}out[i]=b?-1:1;} }
    void updateGeometry(){
        using namespace reverb_detail;
        for(int i=0;i<kNumLines;++i){
            int len=(int)std::round(kLineMs[(size_t)i]*(double)mSize*0.001*mFs); len|=1; mLen[(size_t)i]=std::max(3,len);
            mDispLen[(size_t)i]=std::max(1,(int)std::round(kDispMs[(size_t)i]*0.001*mFs));
            mDispLen2[(size_t)i]=std::max(1,(int)std::round(kDispMs2[(size_t)i]*0.001*mFs));
            const int loopLen=mLen[(size_t)i]+mDispLen[(size_t)i]+mDispLen2[(size_t)i]; mLoopLen[(size_t)i]=loopLen;
            mGain[(size_t)i]=(float)std::pow(10.0,-3.0*(double)loopLen/((double)mT60*mFs));
            const double hfT=std::clamp(0.7+((double)mDampHz-2000.0)/10000.0*2.0,0.7,7.0); // Tone -> HF damping (warm<->bright)
            { const double t60HF=std::min(hfT,(double)mT60);
              const double rHF=std::pow(10.0,-3.0*(double)loopLen*(1.0/t60HF-1.0/(double)mT60)/mFs);
              mShelfG[(size_t)i]=(float)std::clamp(1.0-rHF,0.0,0.985); } // high-shelf HF attenuation per pass (warm tail = lush)
        }
        for(size_t a=0;a<mDiff.size();++a) mDiffLen[a]=std::max(2,(int)std::round(kDiffMs[a]*(double)mIDiff*(double)mSize*0.001*mFs));
        mDampOn=true; mCornerK=reverb_detail::onePole(1800.0,mFs); // shelf corner
        const double drv=std::clamp((double)mDampHz*(double)mDrvMul+1800.0,3000.0,13000.0); mDrvK=reverb_detail::onePole(drv,mFs);
        mLfK=reverb_detail::onePole(130.0,mFs); mLcK=reverb_detail::onePole(55.0,mFs); mOutGain=1.0f;
        mSideHpK=reverb_detail::onePole(120.0,mFs); mFreezeK=reverb_detail::onePole(20000.0,mFs); // bass-mono corner (mono lows only); freeze damping
        { const double f=2700.0,q=0.8,g=1.5; // +1.5dB presence voicing (articulation, not bright)
          const double A=std::pow(10.0,g/40.0),w=2*reverb_detail::kPi*f/mFs,al=std::sin(w)/(2*q),c=std::cos(w),a0=1+al/A;
          pb0=(float)((1+al*A)/a0);pb1=(float)((-2*c)/a0);pb2=(float)((1-al*A)/a0);pa1=(float)((-2*c)/a0);pa2=(float)((1-al/A)/a0); }
    }
    double mFs=48000.0;
    std::array<FracDelayLine,kNumLines> mLine,mDisp,mDisp2;
    std::array<FracDelayLine,6> mDiff;
    FracDelayLine mPredelay, mErApL, mErApR, mErApL2, mErApR2;
    int mErApLenL=0, mErApLenR=0, mErApLenL2=0, mErApLenR2=0;
    Lfo mLfoA,mLfoB;
    std::array<int,kNumLines> mLen{},mDispLen{},mDispLen2{},mLoopLen{};
    std::array<int,6> mDiffLen{};
    std::array<float,kNumLines> mGain{},mDamp{},mLf{},mShelfG{};
    float mCornerK=0;
    int mSignL[kNumLines]{},mSignR[kNumLines]{},mInj[kNumLines]{};
    float mBw1=0,mBw2=0,mLcL=0,mLcR=0;
    // output voicing: 2.7kHz presence peak (per ch) + bass-mono (HP the side)
    float mPzL[4]{},mPzR[4]{}; float pb0=1,pb1=0,pb2=0,pa1=0,pa2=0;
    float mSideHp=0,mSideHpK=0,mFreezeK=0;
    float mDrvK=1,mLfK=0,mLcK=0,mLfDamp=0.12f,mEarly=0.6f,mDrvMul=0.45f,mIDiff=1.3f,mOutGain=1.0f; [[maybe_unused]] float mDampK=1;
    float mSize=1.0f,mT60=2.0f,mDampHz=6000.0f,mPredelayMs=20.0f,mModSamp=3.0f,mDispScale=1.0f; [[maybe_unused]] float mHfT60=1.25f, mDecayVar=0.0f;
    bool mDampOn=true,mPrepared=false,mFreeze=false;
};

// ===========================================================================
// ReverbBlock — character selector + shared guardrail mixer.
// ===========================================================================
class ReverbBlock : public StereoBlock
{
public:
    enum Type { kRoom = 0, kHall, kPlate, kSpring, kShimmer, kAmbience, kBloom };
    static constexpr int kNumTypes = 7;

    // ---- shipped set (v1 commercial release) ----------------------------------
    // We SHIP a few fantastic characters and LOCK AWAY the rest (kept in code for a
    // v1.1 roadmap / paid expansion). Shipped = Room, Hall, Plate, Spring, Shimmer.
    // Ambience + Bloom are fully implemented but hidden from the selector. They sit
    // LAST in the enum, so the shipped set is exactly [0, kNumShipped), keeping
    // automation indices of shipped characters stable. UI + the revType choice param
    // build their lists from shipped()/kNumShipped — this is the single source of truth.
    static constexpr int kNumShipped = 5;
    static bool shipped(Type t) { return (int)t >= 0 && (int)t < kNumShipped; }

    static constexpr int kNumLines = FdnReverb::kNumLines;
    static constexpr float kMinSize = FdnReverb::kMinSize;
    static constexpr float kMaxSize = FdnReverb::kMaxSize;

    // Per-character parameter id (single source for layout/processor/UI), e.g.
    // paramId("Decay", kRoom) -> "revDecayRoom". Each character has its OWN params.
    static std::string paramId(const char *knob, int t) { return std::string("rev") + knob + typeName(t); }
    static const char *typeName(int t)
    {
        static const char *n[kNumTypes] = {"Room", "Hall", "Plate", "Spring", "Shimmer", "Ambience", "Bloom"};
        return (t >= 0 && t < kNumTypes) ? n[t] : "Room";
    }
    static bool usesFdn(Type t) { return t == kRoom || t == kHall || t == kAmbience || t == kBloom; }

    // ---- voicing introspection (UI + tests) ----
    static bool sizeExposed(Type t) { return t == kHall || t == kRoom; } // Room: scales the room size
    static bool toneExposed(Type) { return true; }
    static bool predelayExposed(Type t) { return t == kPlate || t == kBloom || t == kRoom; } // Hall folds it into Size; Shimmer/Room/Spring/Ambience hardwire
    static bool modExposed(Type t) { return t == kHall || t == kPlate || t == kBloom || t == kShimmer || t == kRoom; } // Ambience hardwires
    static bool shimmerExposed(Type t) { return t == kShimmer; }
    static bool pitchExposed(Type t) { return t == kShimmer; }
    static bool tensionExposed(Type t) { return t == kSpring; }
    static bool boingExposed(Type t) { return t == kSpring; } // dispersion/sproing amount, Spring only
    static bool swellExposed(Type t) { return t == kBloom; }
    static bool inputFilterExposed(Type t) { return t == kPlate; } // studio-style wet low-cut on the plate amp
    static bool freezeExposed(Type t) { return t == kHall || t == kShimmer || t == kBloom; } // infinite-sustain pad only makes sense on the lush/evolving characters
    static const char *toneCaption(Type) { return "Tone"; }

    // ---- per-character "sweet spot" knob windows ------------------------------
    // The shared Decay/Damping/Predelay knobs keep full 0..1 travel but map into a
    // curated musical window per character (below), so every position sounds good
    // and nothing unmusical is reachable. DECAY + PREDELAY read TRUE units inside
    // their window (clamp at the caps); Tone (0..1, dark->bright) maps across the window. Engine setters
    // stay literal; the host layer maps raw->window via mapped*(). Outer ranges == APVTS
    // (single source of truth, referenced by PluginProcessor).
    static constexpr float kDecayMin = 0.15f,   kDecayMax = 9.0f;      // s (Spring extended to long studio-spring tails)
    static constexpr float kDampMin  = 600.0f,  kDampMax  = 16000.0f;  // Hz (Room Tone goes to 600)
    static constexpr float kPreMin   = 0.0f,    kPreMax   = 160.0f;    // ms
    static constexpr float kMixMax   = 0.70f;                          // wet cap (Mix is universal)
    struct Range { float lo, hi; };
    static float rangeDefault(Range r) { return r.lo + 0.30f * (r.hi - r.lo); } // default 30%% up a window
    static Range decayRange(Type t)
    {
        switch (t) {
        case kRoom:     return {0.15f, 0.8f}; // small room: dead booth -> small room, reads true RT60
        case kHall:     return {0.8f, 6.0f};
        case kPlate:    return {0.5f, 5.5f};   // vintage plate spec
        case kSpring:   return {1.0f, 9.0f}; // long studio-spring tails (rings ~8s)
        case kShimmer:  return {1.5f, 8.0f};
        case kAmbience: return {0.3f, 1.2f};
        case kBloom:    return {2.5f, 8.0f};
        default:        return {kDecayMin, kDecayMax};
        }
    }
    static Range dampRange(Type t)
    {
        switch (t) {
        case kRoom:     return {600.0f, 3500.0f}; // Tone = input darkening (warm small room)
        case kHall:     return {2000.0f, 7000.0f}; // warm default (30%% knob = 3500 Hz) - the voiced lush hall
        case kPlate:    return {1500.0f, 14000.0f}; // full span; linear. Tone value 0.2 = 4000 Hz (warm); knob10c shows that as 5
        case kSpring:   return {1500.0f,  8000.0f};
        case kShimmer:  return {3000.0f, 16000.0f};
        case kAmbience: return {3000.0f, 13000.0f};
        case kBloom:    return {1500.0f, 10000.0f};
        default:        return {kDampMin, kDampMax};
        }
    }
    static Range predelayRange(Type t)
    {
        switch (t) {
        case kPlate: return {0.0f,  80.0f};
        case kBloom: return {0.0f, 160.0f};
        case kRoom:  return {0.0f,  60.0f};
        default:     return {0.0f, kPreMax};
        }
    }
    // Map a raw knob value (within [outMin,outMax]) into a per-character window.
    static float mapToRange(float raw, float outMin, float outMax, Range r)
    {
        const float t = (outMax > outMin) ? (raw - outMin) / (outMax - outMin) : 0.0f;
        return r.lo + std::clamp(t, 0.0f, 1.0f) * (r.hi - r.lo);
    }
    // Convenience mappers for the active character (host/UI layer calls these).
    float mappedDecay(float rawSec)   const { const Range r = decayRange(mType); return std::clamp(rawSec, r.lo, r.hi); } // exact seconds in-window, clamp at caps
    float mappedTone(float t01)       const { const Range r = dampRange(mType); return r.lo + std::clamp(t01, 0.0f, 1.0f) * (r.hi - r.lo); } // Tone: 0=dark .. 1=bright -> character Hz window (linear; knob readout centres the warm default)
    float mappedPredelay(float rawMs) const { const Range r = predelayRange(mType); return std::clamp(rawMs, r.lo, r.hi); } // exact ms in-window, clamp at caps

    const char *name() const override { return "Reverb"; }

    void prepare(const BlockContext &ctx) override
    {
        mFdn.prepare(ctx.sampleRate);
        mSmallRoom.prepare(ctx.sampleRate);
        mHall.prepare(ctx.sampleRate);
        mPlate.prepare(ctx.sampleRate);
        mPlateFdn.prepare(ctx.sampleRate);
#ifdef NAM_PLATE_EARLY_CONV
        mPlateEarly.prepare(ctx.sampleRate, ctx.maxBlockSize, 2);
        mPlateFdn.setEarlyTap(0.45f);   // convolver supplies the onset; keep a little FDN early for continuity (tunable)
        mFsRB = ctx.sampleRate;
        { const int ring = (int)(0.001 * 200.0 * ctx.sampleRate) + std::max(16, ctx.maxBlockSize) + 4;
          mEpL.assign((size_t)ring, 0.0f); mEpR.assign((size_t)ring, 0.0f); mEpW = 0; }
#endif
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
        mFdn.reset(); mPlate.reset(); mPlateFdn.reset(); mSpring.reset(); mShimmer.reset(); mSmallRoom.reset(); mHall.reset();
#ifdef NAM_PLATE_EARLY_CONV
        mPlateEarly.reset();
        std::fill(mEpL.begin(), mEpL.end(), 0.0f); std::fill(mEpR.begin(), mEpR.end(), 0.0f); mEpW = 0;
#endif
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
    void setBoing(float b) { mBoing = std::clamp(b, 0.0f, 1.0f); if (mPrepared) pushParams(); } // Spring dispersion/sproing
    void setWidth(float w) { mWidth = std::clamp(w, 0.0f, 1.0f); }
    void setInputFilterHz(float hz) { mInputFilterHz = std::clamp(hz, 20.0f, 400.0f); } // Plate Input Filter (wet low-cut corner)
    void setSwell(float s) { mSwell = std::clamp(s, 0.0f, 1.0f); }
    void setPitch(int p) { mPitch = std::clamp(p, 0, 2); if (mPrepared) pushParams(); }
    void setFreeze(bool f) { mFreeze = f && freezeExposed(mType); if (mPrepared) pushParams(); } // Freeze is gated to Hall/Shimmer/Bloom; inert on other characters even if the param/MIDI is on

    int lineLengthSamples(int i) const { return mType == kHall ? mHall.lineLengthSamples(i) : mFdn.lineLengthSamples(i); }
    float lineGain(int i) const { return mType == kHall ? mHall.lineGain(i) : mFdn.lineGain(i); }

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
        case kPlate:
            mPlateFdn.process(left, right, numSamples); // FDN tail
#ifdef NAM_PLATE_EARLY_CONV
            { const int pre = (int)std::round((double)mPredelayMs * 0.001 * mFsRB); const int R = (int)mEpL.size();
              if ((int)mEpoL.size() < numSamples) { mEpoL.resize((size_t)numSamples); mEpoR.resize((size_t)numSamples); }
              for (int n = 0; n < numSamples; ++n) { mEpL[(size_t)mEpW] = mDryL[(size_t)n]; mEpR[(size_t)mEpW] = mDryR[(size_t)n];
                  int rd = mEpW - pre; if (rd < 0) rd += R; mEpoL[(size_t)n] = mEpL[(size_t)rd]; mEpoR[(size_t)n] = mEpR[(size_t)rd]; if (++mEpW >= R) mEpW = 0; }
              mPlateEarly.addEarly(mEpoL.data(), mEpoR.data(), left, right, numSamples, 0.14f); } // predelayed dry -> kernel onset tracks predelay (matches FDN)
#endif
            break;
        case kSpring: mSpring.process(left, right, numSamples); break;
        case kShimmer: mShimmer.process(left, right, numSamples); break;
        case kRoom: mSmallRoom.process(left, right, numSamples); break;
        case kHall: mHall.process(left, right, numSamples); break; // dedicated dispersion-FDN hall
        default: mFdn.process(left, right, numSamples); break;
        }

        GuardMixer::Config c;
        c.mix = effMix();
        c.hpfHz = inputFilterExposed(mType) ? mInputFilterHz : hpfForType(mType);
        c.makeup = makeupForType(mType, effT60()) * levelTrim(mType);
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
        case kPlate: mPlateFdn.reset(); break;
        case kSpring: mSpring.reset(); break;
        case kShimmer: mShimmer.reset(); break;
        case kRoom: mSmallRoom.reset(); break;
        case kHall: mHall.reset(); break;
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
        case kSpring: return std::min(mT60, 15.0f); // uncapped: reach the long studio-spring tail (~8s)
        case kShimmer: return std::max(mT60, 1.5f);
        default: return mT60;
        }
    }
    // foolproofing tables -----------------------------------------------------
    // Gentle, mostly-transparent low-cut — just enough to stop mud, not to thin.
    static float hpfForType(Type t)
    {
        switch (t)
        {
        case kSpring: return 150.0f;
        case kRoom: return 90.0f; // gentle low-cut
        case kAmbience: return 130.0f;
        case kPlate: return 95.0f;
        case kShimmer: return 100.0f;
        case kBloom: return 55.0f;
        default: return 75.0f;        // Hall
        }
    }
    // Subtle ducking — a touch of clarity under playing, NOT an obvious pump.
    static float duckForType(Type t)
    {
        switch (t)
        {
        case kRoom: return 0.10f;
        case kSpring: return 0.08f;
        case kPlate: return 0.12f;
        case kHall: return 0.14f;
        case kBloom: return 0.20f;
        case kShimmer: return 0.22f;
        case kAmbience: return 0.18f;
        default: return 0.12f;
        }
    }
    // hold perceived wet level roughly constant as Decay changes (gentle range so
    // long, lush tails aren't pulled down too far).
    static float makeupForType(Type, float t60)
    {
        return std::clamp(std::sqrt(2.0f / std::max(0.2f, t60)), 0.8f, 1.25f);
    }
    // per-character wet-LEVEL match, ANCHORED TO THE DRY (measured 100%-wet RMS vs the
    // dry guitar at default decay): every character hits ~the dry guitar's loudness at
    // 100% wet, so with the equal-power crossfade the Mix knob is a true balance that
    // sounds equally loud on every character and doesn't jump level as you sweep it.
    static float levelTrim(Type t)
    {
        switch (t) {
        case kRoom:     return 0.88f;
        case kHall:     return 1.62f; // dispersion-FDN hall (re-measured at default Tone 3500, balanced wet/dry)
        case kPlate:    return 1.16f;  // v3 multiband plate ~1.6 dB hotter than v2; trimmed to keep the Mix knob level-matched (re-verify by ear)
        case kSpring:   return 0.138f;  // Spring intrinsically ~+20dB hot -> big cut
        case kShimmer:  return 1.82f;
        case kAmbience: return 1.81f;
        case kBloom:    return 3.10f;
        default:        return 1.39f;
        }
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
        case kPlate: // v2 FDN plate (no modulation -> no setModDepth)
            mPlateFdn.setDecaySeconds(effT60()); mPlateFdn.setDampHz(mDampHz);
            mPlateFdn.setPredelayMs(mPredelayMs); mPlateFdn.setFreeze(mFreeze);
            break;
        case kSpring:
        {
            // Tension as a "tightness" macro, CENTERED at 50% (default sound unchanged):
            // up = tighter (faster decay + brighter + less bloom), down = looser.
            const float tt = (mTension - 0.5f) * 2.0f;                       // -1 (loose) .. +1 (tight)
            mSpring.setDecaySeconds(effT60() * (1.0f - 0.20f * tt));         // +/-20% decay
            mSpring.setDampHz(std::clamp(mDampHz * (1.0f + 0.45f * tt), 1500.0f, 12000.0f)); // brighter when tight
            mSpring.setTension(mTension);                                   // bloom inversion (tight = less bloom)
            // Boing = the dedicated knob PLUS extra dispersion from Tension's LOOSE end
            // (a looser/longer spring sproings more; tightening removes it). At the
            // default Tension (50%) the loose term is 0, so the Boing knob acts alone.
            const float looseBoing = std::max(0.0f, -tt) * 0.30f;
            mSpring.setBoing(std::clamp(mBoing + looseBoing, 0.0f, 1.0f));
            mSpring.setFreeze(mFreeze);
            break;
        }
        case kShimmer:
            mShimmer.setDecaySeconds(effT60()); mShimmer.setDampHz(mDampHz);
            mShimmer.setPredelayMs(30.0f); // hardwired (folded out of the UI)
            mShimmer.setShimmer(mShimmerAmt); mShimmer.setModDepth(mMod);
            mShimmer.setPitch(mPitch); mShimmer.setFreeze(mFreeze);
            break;
        case kRoom: // dedicated short-line small-room FDN
            mSmallRoom.setDecaySeconds(effT60()); mSmallRoom.setToneHz(mDampHz);
            mSmallRoom.setSize(mSize); mSmallRoom.setPredelayMs(mPredelayMs);
            mSmallRoom.setModDepth(mMod); mSmallRoom.setFreeze(mFreeze);
            break;
        case kHall: // dedicated dispersion-FDN concert hall (lush, voiced to real-hall metrics)
            mHall.setDecaySeconds(effT60()); mHall.setDampHz(mDampHz);
            mHall.setSize(mSize); mHall.setModDepth(mMod);
            mHall.setPredelayMs(10.0f + (mSize - kMinSize) * 60.0f); // Hall folds predelay into Size
            mHall.setFreeze(mFreeze);
            break;
        default: // FDN family (Ambience, Bloom)
        {
            float size, mod, pre, diff;
            switch (mType)
            {
            case kAmbience: size = 0.45f; mod = 0.25f; pre = 0.0f;  diff = 0.78f; break;
            // Bloom hardwires a long predelay; Hall folds predelay into Size.
            case kBloom:    size = 1.45f; mod = std::max(0.45f, mMod); pre = mPredelayMs; diff = 0.60f; break;
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
    SmallRoomFdn mSmallRoom;
    HallReverb mHall;       // dedicated dispersion-FDN hall -> kHall
    PlateReverb mPlate;       // old Dattorro plate (kept for reference/A-B; no longer routed)
    PlateFdn mPlateFdn;       // v2 FDN plate -> kPlate
#ifdef NAM_PLATE_EARLY_CONV
    EarlyConvolver mPlateEarly;   // plate onset: dense early-reflection kernel
#endif
    SpringReverb mSpring;
    ShimmerReverb mShimmer;
    GuardMixer mMixer;
    std::vector<float> mDryL, mDryR;
    std::vector<float> mEpL, mEpR, mEpoL, mEpoR; int mEpW = 0; double mFsRB = 48000.0; // plate kernel predelay ring

    Type mType = kHall;
    float mSize = 1.0f, mT60 = 2.0f, mDampHz = 6000.0f, mPredelayMs = 0.0f, mMix = 0.25f;
    float mMod = 0.3f, mShimmerAmt = 0.5f, mTension = 0.5f, mBoing = 0.20f, mWidth = 1.0f, mSwell = 0.4f;
    float mInputFilterHz = 95.0f; // Plate Input Filter corner (20-400 Hz); 95 = prior hardwired Plate low-cut
    int mPitch = 0;
    bool mFreeze = false, mPrepared = false;
};

} // namespace nam_rig
