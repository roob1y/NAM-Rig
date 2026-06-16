#pragma once
// ReverbBlock — a typed CHARACTER reverb (post-cab, stereo). Like ModBlock, each
// character is voiced like the real thing rather than being one generic engine,
// and the UI shows only the controls a character actually has; everything else is
// hardwired to that character's sweet spot, so you can't dial a bad sound.
//
// FOUR engines render the WET signal for seven characters:
//   - Modulated Householder FDN  -> Room, Hall, Ambience, Bloom
//   - FDTD thin-plate mesh        -> Plate
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
// PlateReverb — FDTD thin-plate mesh (Plate). Renders WET only.
// ===========================================================================
// A finite-difference (wave-digital) model of an ACTUAL tensioned steel plate,
// not a delay-network emulation. It integrates the Kirchhoff-Love thin-plate
// equation on a 2D grid:
//   u_tt = -kappa^2 (Lap*Lap) u + c^2 Lap u - 2 sig0 u_t + 2 sig1 d/dt(Lap u) + force
// using a 13-point biharmonic stencil (Lap of Lap) + a 5-point Laplacian, an
// explicit time step, and simply-supported edges (the fixed 0 border). The
// stiffness term -kappa^2 Lap^2 makes bending waves DISPERSIVE — high frequencies
// propagate FASTER than low — which is the defining signature of a real plate, and
// the ~860 interior nodes give a very dense modal field (measured: echo density
// ~1.0 within ~50 ms, no discrete echoes). The plate is driven at one node and
// read at two well-separated nodes, so L/R are genuinely decorrelated (corr ~0.1).
//   sig0  -> overall T60   (sig0 = 3 ln10 / T60)        [setDecaySeconds]
//   sig1  -> HF damping    (lower Damp Hz = more sig1)   [setDampHz]
//   predelay : integer delay line feeding the driver (exact predelay shift)
//   mod      : a tiny LFO wander on the (fractional) pickups, de-metallicises
//   freeze   : sig0 -> 0 (lossless plate) + input muted = infinite sustain
// Pickups read BENDING STRAIN (curvature = Laplacian at the node) like a real piezo
// contact mic, not raw displacement: a force-driven plate's displacement rolls off
// ~12 dB/oct (dark/low-endy); curvature adds ~+12 dB/oct and restores the bright vintage plate
// balance. A gentle output low-shelf trims residual low boom. Stability: explicit
// biharmonic needs mu = kappa*k/h^2 < 0.25 (we sit at 0.24); the explicit sig1 term
// tightens that bound, so sig1 is clamped to kS1Max.
class PlateReverb
{
public:
    static constexpr int kNx = 38, kNy = 26;        // grid incl. fixed 0 border
    static constexpr int kN = kNx * kNy;
    static constexpr double kMu = 0.24;             // kappa*k/h^2, stability < 0.25
    static constexpr double kTension = 0.04;        // small tension (Courant) term
    static constexpr float kOutGain = 0.22f;        // engine -> GuardMixer level
    static constexpr double kS1Max = 0.006;         // sig1 stability clamp (blow-up ~0.015 @ mu 0.24)
    static constexpr double kFreezeS1 = 1.0e-6;     // freeze: hold full-range (not an LF drone)
    static constexpr float kShelfHz = 220.0f;       // output low-shelf corner
    static constexpr float kShelfGain = 0.7f;       // ~ -3 dB below kShelfHz (trim residual low boom)

    void prepare(double fs)
    {
        mFs = fs;
        mU.assign((size_t)kN, 0.0); mU1.assign((size_t)kN, 0.0); mU2.assign((size_t)kN, 0.0);
        mLap.assign((size_t)kN, 0.0); mLap1.assign((size_t)kN, 0.0); mBih.assign((size_t)kN, 0.0);
        mDriver = idx(kNx / 3, kNy / 3);
        mPickL = idx(kNx / 4, (3 * kNy) / 5);
        mPickR = idx((3 * kNx) / 4, kNy / 3 + 1);
        mMu2 = kMu * kMu; mLam2 = kTension * kTension;
        mPredelay.prepare((int)std::ceil(0.2 * mFs) + 8);
        mLfo.prepare(mFs); mLfo.setRateHz(0.7f); mShelfA = reverb_detail::onePole(kShelfHz, mFs);
        recompute();
        reset();
        mPrepared = true;
    }

    void reset()
    {
        std::fill(mU.begin(), mU.end(), 0.0); std::fill(mU1.begin(), mU1.end(), 0.0);
        std::fill(mU2.begin(), mU2.end(), 0.0);
        std::fill(mLap.begin(), mLap.end(), 0.0); std::fill(mLap1.begin(), mLap1.end(), 0.0);
        std::fill(mBih.begin(), mBih.end(), 0.0);
        mPredelay.reset(); mLfo.reset(); mShelfL = mShelfR = 0.0f;
    }

    void setDecaySeconds(float t60) { mT60 = std::max(0.1f, t60); mDirty = true; }
    void setDampHz(float hz) { mDampHz = hz; mDirty = true; }
    void setPredelayMs(float ms) { mPredelayMs = std::clamp(ms, 0.0f, 200.0f); }
    void setModDepth(float d) { mMod = std::clamp(d, 0.0f, 1.0f); }
    void setFreeze(bool f) { mFreeze = f; }

    void process(float *left, float *right, int numSamples)
    {
        if (mDirty) recompute();
        const bool stereo = (left != right);
        const int pre = (int)std::round((double)mPredelayMs * 0.001 * mFs);
        const double s0 = mFreeze ? 0.0 : mS0;
        const float inGain = mFreeze ? 0.0f : 1.0f;
        const double inv = 1.0 / (1.0 + s0);
        const double s1 = mFreeze ? kFreezeS1 : mS1;   // freeze holds full-range, not an LF drone
        const float md = mMod * 0.4f;               // <= 0.4-node subtle pickup wander
        const int blx = mPickL % kNx, bly = mPickL / kNx;
        const int brx = mPickR % kNx, bry = mPickR / kNx;

        for (int n = 0; n < numSamples; ++n)
        {
            const float dryL = left[n];
            const float dryR = stereo ? right[n] : dryL;
            mPredelay.write(0.5f * (dryL + dryR));
            const double f = (double)(inGain * mPredelay.readInt(std::max(1, pre)));

            // Lap(u^n) over the interior (border stays 0 = simply-supported)
            for (int y = 1; y < kNy - 1; ++y)
                for (int x = 1; x < kNx - 1; ++x)
                { const int i = y * kNx + x; mLap[(size_t)i] = mU[(size_t)(i-1)] + mU[(size_t)(i+1)] + mU[(size_t)(i-kNx)] + mU[(size_t)(i+kNx)] - 4.0 * mU[(size_t)i]; }
            // biharmonic = Lap(Lap(u^n))
            for (int y = 1; y < kNy - 1; ++y)
                for (int x = 1; x < kNx - 1; ++x)
                { const int i = y * kNx + x; mBih[(size_t)i] = mLap[(size_t)(i-1)] + mLap[(size_t)(i+1)] + mLap[(size_t)(i-kNx)] + mLap[(size_t)(i+kNx)] - 4.0 * mLap[(size_t)i]; }
            // explicit plate update -> u^{n+1} into mU2
            for (int y = 1; y < kNy - 1; ++y)
                for (int x = 1; x < kNx - 1; ++x)
                {
                    const int i = y * kNx + x;
                    const double force = (i == mDriver) ? f : 0.0;
                    const double un1 = mU1[(size_t)i];
                    mU2[(size_t)i] = inv * (2.0 * mU[(size_t)i] - un1 - mMu2 * mBih[(size_t)i]
                                            + mLam2 * mLap[(size_t)i] + s0 * un1
                                            + 2.0 * s1 * (mLap[(size_t)i] - mLap1[(size_t)i]) + force);
                }
            // rotate state (O(1) pointer swaps): mU<-u^{n+1}, mU1<-u^n, mLap1<-Lap(u^n)
            mU.swap(mU1); mU.swap(mU2); mLap.swap(mLap1);

            float oL, oR;
            if (md > 0.0f)
            {
                oL = bilin((float)blx + md * mLfo.value(0.0),  (float)bly + md * mLfo.value(0.27));
                oR = bilin((float)brx + md * mLfo.value(0.5),  (float)bry + md * mLfo.value(0.13));
            }
            else { oL = (float)mLap1[(size_t)mPickL]; oR = (float)mLap1[(size_t)mPickR]; } // curvature = bending strain

            // output low-shelf (~ -3 dB below kShelfHz) then engine level
            mShelfL += mShelfA * (oL - mShelfL); oL = kShelfGain * mShelfL + (oL - mShelfL);
            reverb_detail::flush(mShelfL);
            left[n] = kOutGain * oL;
            if (stereo) { mShelfR += mShelfA * (oR - mShelfR); oR = kShelfGain * mShelfR + (oR - mShelfR); reverb_detail::flush(mShelfR); right[n] = kOutGain * oR; }
            mLfo.advance();
        }
    }

private:
    static int idx(int x, int y) { return y * kNx + x; }
    float bilin(float fx, float fy) const
    {
        int x0 = std::clamp((int)std::floor(fx), 1, kNx - 2);
        int y0 = std::clamp((int)std::floor(fy), 1, kNy - 2);
        const float tx = fx - (float)x0, ty = fy - (float)y0;
        const int i = y0 * kNx + x0;
        const double a = mLap1[(size_t)i], b = mLap1[(size_t)(i + 1)];
        const double c = mLap1[(size_t)(i + kNx)], d = mLap1[(size_t)(i + kNx + 1)];
        return (float)((a * (1.0 - tx) + b * tx) * (1.0 - ty) + (c * (1.0 - tx) + d * tx) * ty);
    }
    void recompute()
    {
        mS0 = (3.0 * std::log(10.0) / (double)std::max(0.1f, mT60)) / mFs; // T60 -> sig0
        const double hz = std::clamp((double)mDampHz, 500.0, 20000.0);
        mS1 = (0.6 * (12000.0 / hz)) / mFs;                                // Damp Hz -> sig1
        mS1 = std::min(mS1, kS1Max);                                       // explicit-sig1 stability clamp
        mDirty = false;
    }

    double mFs = 48000.0;
    std::vector<double> mU, mU1, mU2, mLap, mLap1, mBih;
    int mDriver = 0, mPickL = 0, mPickR = 0;
    double mMu2 = 0.0, mLam2 = 0.0, mS0 = 0.0, mS1 = 0.0;
    FracDelayLine mPredelay;
    Lfo mLfo;
    float mT60 = 2.0f, mDampHz = 7000.0f, mPredelayMs = 10.0f, mMod = 0.3f;
    float mShelfL = 0.0f, mShelfR = 0.0f, mShelfA = 0.0f; // output low-shelf state/coef
    bool mPrepared = false, mDirty = true, mFreeze = false;
};

// ===========================================================================
// SpringReverb — studio spring-voiced studio spring. Four long "springs" run in parallel,
// each = a dispersion cascade (first-order allpasses -> the spring chirp) + two
// in-loop allpass diffusers + a slow drift, inside a feedback loop. Their sum is
// then smeared by an output allpass diffuser bank, which dissolves the discrete
// chirp-repeats into ONE smooth, dense, lush wash (the AKG studio spring character) rather
// than the boingy discrete pings of a cheap amp tank. Tension = Density (how much
// in-loop diffusion: low = open/springy, high = dense/smooth). Renders WET only.
// (The boingy chirp-delay v1 is kept in history for the future in-amp spring.)
// ===========================================================================
class SpringReverb
{
public:
    static constexpr int kSprings = 4;
    static constexpr int kMaxStages = 160;
    static constexpr int kOut = 4;

    void prepare(double fs)
    {
        mFs = fs;
        const double lms[kSprings] = {28.0, 33.0, 39.0, 46.0};
        const double d0[kSprings] = {6.7, 7.9, 9.1, 10.3};
        const double d1[kSprings] = {11.3, 12.9, 14.7, 16.1};
        for (int s = 0; s < kSprings; ++s)
        {
            mBaseLoop[(size_t)s] = lms[s] * 0.001 * mFs;
            mLoop[(size_t)s].prepare((int)std::ceil(mBaseLoop[(size_t)s] * 1.45) + 64);
            mDif0Len[(size_t)s] = (int)std::round(d0[s] * 0.001 * mFs);
            mDif1Len[(size_t)s] = (int)std::round(d1[s] * 0.001 * mFs);
            mDif0[(size_t)s].prepare(mDif0Len[(size_t)s] + 8);
            mDif1[(size_t)s].prepare(mDif1Len[(size_t)s] + 8);
        }
        const double odl[kOut] = {9.7, 14.2, 20.9, 28.3};
        const double odr[kOut] = {8.3, 12.7, 18.9, 25.1};
        for (int k = 0; k < kOut; ++k)
        {
            mOdLLen[(size_t)k] = (int)std::round(odl[k] * 0.001 * mFs); mOdL[(size_t)k].prepare(mOdLLen[(size_t)k] + 8);
            mOdRLen[(size_t)k] = (int)std::round(odr[k] * 0.001 * mFs); mOdR[(size_t)k].prepare(mOdRLen[(size_t)k] + 8);
        }
        mPredelay.prepare((int)std::ceil(0.05 * mFs) + 8);
        mLfo.prepare(mFs); mLfo.setRateHz(0.4f);
        reset();
        mPrepared = true;
    }

    void reset()
    {
        for (auto &l : mLoop) l.reset();
        for (auto &l : mDif0) l.reset();
        for (auto &l : mDif1) l.reset();
        for (auto &l : mOdL) l.reset();
        for (auto &l : mOdR) l.reset();
        for (auto &c : mApx) std::fill(c.begin(), c.end(), 0.0f);
        for (auto &c : mApy) std::fill(c.begin(), c.end(), 0.0f);
        mPredelay.reset();
        for (int s = 0; s < kSprings; ++s) { mLp[(size_t)s]=0; mDc[(size_t)s]=0; mZ[(size_t)s]=0; }
        mLfo.reset();
    }

    void setDecaySeconds(float t60) { mT60 = std::max(0.1f, t60); mDirty = true; }
    void setDampHz(float hz) { mDampHz = hz; mDirty = true; }
    void setTension(float t) { mTension = std::clamp(t, 0.0f, 1.0f); mDirty = true; } // = Density
    void setFreeze(bool f) { mFreeze = f; }

    void process(float *left, float *right, int numSamples)
    {
        using namespace reverb_detail;
        if (mDirty) recompute();
        const bool stereo = (left != right);
        const float fb = mFreeze ? 0.99f : mFb;
        const float inGain = mFreeze ? 0.0f : 1.0f;
        const float modS = (float)(0.5 * 0.001 * mFs); // subtle spring sag, not a warble
        for (int n = 0; n < numSamples; ++n)
        {
            const float dryL = left[n];
            const float dryR = stereo ? right[n] : dryL;
            mPredelay.write(0.5f * (dryL + dryR));
            const float in = inGain * mPredelay.readInt(std::max(1, mPreLen));
            float sum = 0.0f;
            for (int s = 0; s < kSprings; ++s)
            {
                float v = in + fb * mZ[(size_t)s];
                auto &X = mApx[(size_t)s]; auto &Y = mApy[(size_t)s];
                for (int k = 0; k < mStages; ++k)
                { const float y = mApA * v + X[(size_t)k] - mApA * Y[(size_t)k]; X[(size_t)k]=v; Y[(size_t)k]=y; v=y; }
                v = allpassInt(mDif0[(size_t)s], mDif0Len[(size_t)s], mDensG, v); // in-loop density
                v = allpassInt(mDif1[(size_t)s], mDif1Len[(size_t)s], mDensG, v);
                const double md = (double)mLoopLen[(size_t)s] + (double)(modS * mLfo.value((double)s * 0.25));
                mLoop[(size_t)s].write(v);
                float d = mLoop[(size_t)s].readFrac(md);
                mLp[(size_t)s] += mDampK * (d - mLp[(size_t)s]); d = mLp[(size_t)s];
                mDc[(size_t)s] += mDcK * (d - mDc[(size_t)s]); d = d - mDc[(size_t)s];
                mZ[(size_t)s] = d; sum += d;
            }
            sum *= 0.5f;
            float wl = sum, wr = sum;
            for (int k = 0; k < kOut; ++k) wl = allpassInt(mOdL[(size_t)k], mOdLLen[(size_t)k], 0.55f, wl); // smooth the wash
            for (int k = 0; k < kOut; ++k) wr = allpassInt(mOdR[(size_t)k], mOdRLen[(size_t)k], 0.55f, wr);
            left[n] = 1.6f * wl;
            if (stereo) right[n] = 1.6f * wr;
            mLfo.advance();
        }
        for (int s = 0; s < kSprings; ++s) { flush(mZ[(size_t)s]); flush(mLp[(size_t)s]); flush(mDc[(size_t)s]); }
    }

private:
    void recompute()
    {
        using namespace reverb_detail;
        mPreLen = std::max(1, (int)std::round(0.004 * mFs));
        mDensG = 0.55f;                            // in-loop diffusion fixed (the lush wash)
        // Tension = spring tightness: scales the loop lengths, shifting the spring's
        // resonant pitch/brightness audibly (~1 kHz of centroid travel).
        const float sc = 0.6f + 0.8f * mTension;
        for (int s = 0; s < kSprings; ++s) mLoopLen[(size_t)s] = std::max(2, (int)std::round(mBaseLoop[(size_t)s] * sc));
        const double eff = (28.0 + 46.0) * 0.5 * (double)sc + 8.0;
        mFb = (float)std::clamp(std::pow(10.0, -3.0 * eff / ((double)mT60 * 1000.0)), 0.0, 0.9);
        mDampK = onePole((double)mDampHz, mFs);
        mDcK = onePole(20.0, mFs);
        mDirty = false;
    }

    double mFs = 48000.0;
    std::array<FracDelayLine, kSprings> mLoop, mDif0, mDif1;
    std::array<std::array<float, kMaxStages>, kSprings> mApx{}, mApy{};
    FracDelayLine mPredelay;
    std::array<FracDelayLine, kOut> mOdL, mOdR;
    std::array<int, kSprings> mLoopLen{}, mDif0Len{}, mDif1Len{};
    std::array<double, kSprings> mBaseLoop{};
    std::array<int, kOut> mOdLLen{}, mOdRLen{};
    std::array<float, kSprings> mLp{}, mDc{}, mZ{};
    Lfo mLfo;
    int mPreLen = 1, mStages = 140;
    float mApA = 0.6f, mDensG = 0.55f, mFb = 0.8f, mDampK = 1.0f, mDcK = 0.01f;
    float mT60 = 3.0f, mDampHz = 5200.0f, mTension = 0.5f;
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
        case kSpring: return std::min(mT60, 6.0f); // studio spring-style long studio spring
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
        case kRoom: return 110.0f;
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
