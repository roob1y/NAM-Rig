#pragma once
// CabDynamicsBlock — the "Dynamic Cab" DELTA wrapper around the existing static
// IR convolution (CabBlock). 3Sigma captures are full speaker+cabinet+mic
// snapshots: box resonance, cone colour and the small-signal impedance curve are
// ALREADY in the IR. This block adds ONLY what a static linear IR physically
// cannot: LEVEL-DEPENDENT and TIME-VARIANT deviation. Every stage is a delta on
// top of the IR and collapses to nothing at rest.
//
// Full derivation: docs/cabdyn/DESIGN.md (written before this code).
//
// Signal flow (the block owns NO convolver — it wraps the engine already in the
// codebase via two process stages):
//
//   processPre():  [in] -> A. Reactive Impedance Delta -> B. Cone Breakup Delta -> [out to IR conv]
//   ( existing CabBlock::process runs the static IR here )
//   processPost(): [in from IR conv] -> C. Enclosure Air/Damping Delta -> [out]
//
// Two instances run (Cab A / Cab B lanes); ALL state is per-instance.
//
// The DELTA / bit-exact contract: every stage is y = x + (macro*envelope)*delta(x).
// When the controlling macro is 0 the delta term is *0, so the stage is skipped
// (early return, buffer untouched). Therefore ALL THREE MACROS AT 0 => byte-
// identical output (memcmp==0). A stage engages/disengages by ramping to the
// transparent edge THEN leaving the path (house CutFilters pattern), so turning a
// macro to 0 fades out click-free rather than snapping.
//
// NO loudness compensation, EVER. Envelope drive is physical modelling only (a
// real speaker behaves differently when pushed). There is no RMS makeup, no
// auto-level, no broadband gain that restores level. The single gain-*reduction*
// (low-band cone power compression) drops level when pushed and never adds it back.
//
// Modulation ceilings (all small, all bounded): A1 90Hz resonance +3 dB; A2 2kHz
// inductance shelf -3 dB; B midband drive <=3.2x (band 0.8-3.8 kHz); B low-band
// compression -2 dB; C diffuse air wet mix <=0.12 (~ -18 dB).
//
// JUCE-free core (Biquad.h only) so it compiles in a plain offline g++ harness,
// verified by tests/cabdyn_test.cpp. Zero added latency on the dry path (deltas
// are ADDED; dry passes straight through). Not wired into RigChain yet.

#include "Biquad.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace nam_rig
{

class CabDynamicsBlock
{
public:
    // ======================= 2x polyphase halfband oversampler =======================
    // Linear-phase halfband FIR, coefficients derived in design() from a Kaiser-
    // windowed sinc with the even offset taps forced to exactly zero (the halfband
    // property). Run as two polyphase branches: up() turns one base sample into two
    // 2x samples, down() turns two 2x samples into one base sample. No library.
    //
    // The clipper's odd harmonics of a <=3.8 kHz fundamental reach the 7th (>24 kHz)
    // which would fold at 48 k; at 2x (96 k) they stay below the intermediate Nyquist
    // and the decimator's deep stopband removes them on the way down. L=65 taps
    // (center index 32 is even -> the even-offset taps are the zeroed ones); the
    // 30 kHz decimator stopband measures ~ -96 dB (test T2a).
    struct Halfband
    {
        static constexpr int L = 65;        // taps (L % 4 == 1 -> center C is even)
        static constexpr int C = (L - 1) / 2; // 32
        float h[L] = {0};                   // prototype: DC gain 1, halfband
        float xh[C + 1] = {0};              // up: input history (newest at 0)
        float wh[L] = {0};                  // down: 2x history (newest at 0)

        static double i0(double x) // modified Bessel I0 (Kaiser window)
        {
            double s = 1.0, t = 1.0, x2 = x * x / 4.0;
            for (int k = 1; k < 60; ++k)
            {
                t *= x2 / ((double)k * k);
                s += t;
                if (t < 1.0e-14 * s) break;
            }
            return s;
        }

        void design()
        {
            constexpr double kPi = 3.14159265358979323846;
            const double beta = 9.0, i0b = i0(beta); // deep, narrow-transition halfband
            double sum = 0.0;
            for (int k = 0; k < L; ++k)
            {
                const int m = k - C;
                const double t = 0.5 * (double)m;
                const double s = (m == 0) ? 1.0 : std::sin(kPi * t) / (kPi * t); // sinc
                double v = 0.5 * s; // ideal halfband LP, cutoff pi/2, DC gain 1
                const double r = (double)m / (double)C;
                const double w = i0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0b;
                v *= w;
                if (m != 0 && (m % 2 == 0)) v = 0.0; // force even-offset taps to zero
                h[k] = (float)v;
                sum += v;
            }
            for (int k = 0; k < L; ++k) h[k] = (float)(h[k] / sum); // normalize DC to 1
            reset();
        }

        void reset()
        {
            for (int i = 0; i <= C; ++i) xh[i] = 0.0f;
            for (int i = 0; i < L; ++i) wh[i] = 0.0f;
        }

        // one base sample -> two 2x samples (interpolator, gain-2 halfband)
        inline void up(float x, float &u0, float &u1)
        {
            for (int i = C; i > 0; --i) xh[i] = xh[i - 1];
            xh[0] = x;
            u0 = 2.0f * h[C] * xh[C / 2];            // even phase = single center tap
            float s = 0.0f;
            for (int k = 1; k < L; k += 2) s += 2.0f * h[k] * xh[(k - 1) / 2]; // odd phase
            u1 = s;
        }

        // two 2x samples -> one base sample (decimator, DC-gain-1 halfband)
        inline float down(float w0, float w1)
        {
            for (int i = L - 1; i > 1; --i) wh[i] = wh[i - 2];
            wh[1] = w0;
            wh[0] = w1;
            float y = h[C] * wh[C];                  // center even tap
            for (int k = 1; k < L; k += 2) y += h[k] * wh[k]; // odd taps
            return y;
        }
    };

    // Integer group delay (base samples) of an up()->down() round trip. Only the
    // ADDED harmonic delta carries this delay; the dry path is undelayed.
    static constexpr int kOsGroupDelay = Halfband::C; // 32 samples @ any fs (~0.67 ms @48k)

    // ======================= Schroeder allpass w/ in-loop LP damping =======================
    struct DampAllpass
    {
        std::vector<float> buf;
        int size = 1, pos = 0;
        float lp = 0.0f, dampCoef = 1.0f;

        void prepare(int n, double dampHz, double fs)
        {
            size = std::max(1, n);
            buf.assign((size_t)size, 0.0f);
            pos = 0;
            lp = 0.0f;
            constexpr double kPi = 3.14159265358979323846;
            dampCoef = (float)(1.0 - std::exp(-2.0 * kPi * dampHz / fs));
        }
        void reset()
        {
            std::fill(buf.begin(), buf.end(), 0.0f);
            pos = 0;
            lp = 0.0f;
        }
        inline float process(float x, float g)
        {
            float d = buf[(size_t)pos];
            lp += dampCoef * (d - lp); // one-pole LP damping inside the loop
            d = lp;
            const float w = x + g * d;
            const float y = d - g * w; // allpass
            buf[(size_t)pos] = w;
            if (++pos >= size) pos = 0;
            return y;
        }
    };

    // ================================ lifecycle ================================
    void prepare(double sampleRate, int /*maxBlockSize*/ = 512)
    {
        mFs = sampleRate > 0.0 ? sampleRate : 48000.0;

        // Smoothers: ~25 ms macro de-zip; ~25 ms mix/size de-zip.
        mMacroK = onePoleK(0.025);
        mMixK   = onePoleK(0.025);
        // Envelope: 8 ms attack / 160 ms release (slow, program-dependent).
        mEnvAtk = onePoleK(0.008);
        mEnvRel = onePoleK(0.160);
        mEnvAtkPost = mEnvAtk;
        mEnvRelPost = mEnvRel;

        // Stage B fixed crossover (isolate the driven midband) + low-comp band.
        mHp800  = Biquad::highpass(mFs, std::min(800.0, 0.45 * mFs));
        mLp3800 = Biquad::lowpass(mFs, std::min(3800.0, 0.45 * mFs));
        mLp180  = Biquad::lowpass(mFs, std::min(180.0, 0.45 * mFs));

        mHb.design();

        // Stage C: two archetype networks, prime delays (<10 ms) scaled to fs.
        static const int a48[3] = {113, 179, 251};  // tight 1x12 open-back
        static const int b48[3] = {211, 331, 461};  // big 4x12 closed-back
        const double sc = mFs / 48000.0;
        for (int i = 0; i < 3; ++i)
        {
            mApA[i].prepare(std::max(2, (int)std::lround(a48[i] * sc)), kDampA_Hz, mFs);
            mApB[i].prepare(std::max(2, (int)std::lround(b48[i] * sc)), kDampB_Hz, mFs);
        }

        reset();
        mPrepared = true;
    }

    void reset()
    {
        mAgeSm = mThumpSm = mSizeSm = 0.0f;
        mEnvPre = mEnvPost = 0.0f;
        mEncMixSm = 0.0f;
        mA1.reset();
        mA2.reset();
        mHp800.reset();
        mLp3800.reset();
        mLp180.reset();
        mHb.reset();
        for (int i = 0; i < 3; ++i) { mApA[i].reset(); mApB[i].reset(); }
        mCtrl = 0;
        rebuildStageA(0.0f); // identity at rest
        mA1Db = mA2Db = 0.0f;
        mDriveDbg = 1.0f;
    }

    // =============================== parameters ===============================
    // Exactly three macros, floats 0..1, all default 0 = bit-exact bypass.
    void setAgeDrive(float v) { mAgeTarget   = clamp01(v); }
    void setThump(float v)    { mThumpTarget = clamp01(v); }
    void setCabSize(float v)  { mSizeTarget  = clamp01(v); }

    double latencySamples() const { return 0.0; } // dry path undelayed

    // ============================ pre-conv processing ============================
    // Stage A (reactive impedance delta) + Stage B (band-limited cone breakup).
    void processPre(float *buf, int numSamples)
    {
        if (!preActive()) return; // bit-exact bypass (buffer untouched)

        for (int i = 0; i < numSamples; ++i)
        {
            const float x = buf[i];

            // ---- smoothers + envelope (per sample) ----
            mAgeSm   += mMacroK * (mAgeTarget - mAgeSm);
            mThumpSm += mMacroK * (mThumpTarget - mThumpSm);
            const float r = std::fabs(x);
            mEnvPre += (r > mEnvPre ? mEnvAtk : mEnvRel) * (r - mEnvPre);
            const float push = pushOf(mEnvPre);

            // ---- Stage A coefficients at control rate (env moves slowly) ----
            if (mCtrl == 0) rebuildStageA(push);
            if (++mCtrl >= kCtrl) mCtrl = 0;

            // ---- Stage A: series impedance-delta filters (identity at rest) ----
            float s = mA2.processSample(mA1.processSample(x));

            // ---- Stage B: band-limited cone breakup (parallel delta) ----
            const float Wb = mAgeSm; // engage = Age (0 at rest)
            if (mAgeTarget > 0.0f || Wb > 1.0e-6f)
            {
                const float drive = 1.0f + mAgeSm * (1.0f + kDriveEnv * push);
                mDriveDbg = drive;
                // isolate driven midband (low band stays perfectly linear)
                const float band = mLp3800.processSample(mHp800.processSample(s));
                // form the harmonic delta at 2x, band-limit on the way down
                float b0, b1;
                mHb.up(band, b0, b1);
                const float invd = 1.0f / drive;
                const float n0 = std::tanh(b0 * drive) * invd - b0;
                const float n1 = std::tanh(b1 * drive) * invd - b1;
                const float db = mHb.down(n0, n1);
                // low-band cone power compression (gain reduction only, <= -2 dB)
                float grDb = -(kAgeComp * mAgeSm + kEnvComp * mAgeSm * push);
                grDb = std::max(grDb, -2.0f);
                const float gLin = dbToLin(grDb);
                const float low = mLp180.processSample(s);
                s = s + Wb * db + Wb * (gLin - 1.0f) * low;
            }
            else
            {
                // keep the crossover/low-comp filter states warm so re-engaging is
                // click-free (advance them with the current signal, discard output).
                mLp3800.processSample(mHp800.processSample(s));
                mLp180.processSample(s);
            }

            buf[i] = s;
        }
        flushDenormalsPre();
    }

    // ============================ post-conv processing ============================
    // Stage C (enclosure air / damping delta). Post-conv is physically correct:
    // panels are excited by the driver's ACOUSTIC output = the post-conv signal.
    void processPost(float *buf, int numSamples)
    {
        if (!postActive()) return; // bit-exact bypass (buffer untouched)

        for (int i = 0; i < numSamples; ++i)
        {
            const float x = buf[i];

            mSizeSm  += mMixK * (mSizeTarget - mSizeSm);
            mThumpSm += mMacroK * (mThumpTarget - mThumpSm);
            mAgeSm   += mMacroK * (mAgeTarget - mAgeSm);
            const float r = std::fabs(x);
            mEnvPost += (r > mEnvPost ? mEnvAtkPost : mEnvRelPost) * (r - mEnvPost);
            const float push = pushOf(mEnvPost);

            // wet mix: level-dependent, bounded <= 0.12; default 0
            float encTarget = kEncBase * (0.5f * mThumpSm + 0.5f * mThumpSm * push
                                          + 0.25f * mAgeSm * push);
            encTarget = std::min(encTarget, kEncMax);
            mEncMixSm += mMixK * (encTarget - mEncMixSm);

            // enclosure feedback scaled by Thump (bounded)
            const float gA = std::min(kGmaxHard, kGsetA + kGthump * mThumpSm);
            const float gB = std::min(kGmaxHard, kGsetB + kGthump * mThumpSm);

            // two parallel diffusion networks, crossfaded by Cab Size (artifact-free:
            // we crossfade OUTPUTS, never delay times -> no pitch warble)
            float a = x, b = x;
            for (int k = 0; k < 3; ++k) a = mApA[k].process(a, gA);
            for (int k = 0; k < 3; ++k) b = mApB[k].process(b, gB);
            const float net = (1.0f - mSizeSm) * a + mSizeSm * b;

            buf[i] = x + mEncMixSm * net;
        }
        flushDenormalsPost();
    }

    // ============================ verification hooks ============================
    float dbgA1Db()   const { return mA1Db; }   // last-built 90 Hz resonance gain (dB)
    float dbgA2Db()   const { return mA2Db; }   // last-built 2 kHz shelf gain (dB)
    float dbgEncMix() const { return mEncMixSm; } // current enclosure wet mix
    float dbgDrive()  const { return mDriveDbg; } // current Stage B drive multiplier
    float dbgEnvPre() const { return mEnvPre; }
    double sampleRate() const { return mFs; }

private:
    // ------------------------------- helpers -------------------------------
    static float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
    float onePoleK(double tauSec) const { return (float)(1.0 - std::exp(-1.0 / (tauSec * mFs))); }
    static float dbToLin(float db) { return std::pow(10.0f, db / 20.0f); }

    // env -> push in [0,1] with a soft knee (quiet barely modulates, loud pushes)
    static float pushOf(float env)
    {
        const float p = (env - kEnvLo) / (kEnvHi - kEnvLo);
        return p < 0.0f ? 0.0f : (p > 1.0f ? 1.0f : p);
    }

    // Stage A biquads from the current push (+ static Age HF ease). gain 0 => identity.
    void rebuildStageA(float push)
    {
        // A1: 90 Hz Fs resonance peak — deviates with Thump*envelope, ceiling +3 dB.
        const float f0 = 90.0f * (1.0f - 0.12f * mThumpSm);
        const float Q  = 0.9f + 0.9f * mThumpSm;
        float g1 = kDepthA1 * mThumpSm * push;
        g1 = std::min(g1, 3.0f);
        const Biquad n1 = Biquad::peaking(mFs, std::min((double)f0, 0.45 * mFs), (double)Q, (double)g1);
        mA1.copyCoeffsFrom(n1);
        mA1Db = g1;

        // A2: 2 kHz Le inductance shelf — dynamic droop (env) + static Age ease. -3 dB floor.
        float g2 = -(kDepthA2 * push + kHfEase * mAgeSm);
        g2 = std::max(g2, -3.0f);
        const Biquad n2 = Biquad::highshelf(mFs, std::min(2000.0, 0.45 * mFs), (double)g2, 0.7);
        mA2.copyCoeffsFrom(n2);
        mA2Db = g2;
    }

    bool preActive() const
    {
        return mAgeTarget > 0.0f || mThumpTarget > 0.0f
               || std::fabs(mAgeSm - mAgeTarget) > kSettle
               || std::fabs(mThumpSm - mThumpTarget) > kSettle;
    }
    bool postActive() const
    {
        return mThumpTarget > 0.0f || mAgeTarget > 0.0f
               || std::fabs(mThumpSm - mThumpTarget) > kSettle
               || std::fabs(mAgeSm - mAgeTarget) > kSettle
               || std::fabs(mSizeSm - mSizeTarget) > kSettle
               || mEncMixSm > kSettle;
    }

    void flushDenormalsPre()
    {
        for (Biquad *b : {&mA1, &mA2, &mHp800, &mLp3800, &mLp180})
        {
            if (std::fabs(b->z1) < 1.0e-30f) b->z1 = 0.0f;
            if (std::fabs(b->z2) < 1.0e-30f) b->z2 = 0.0f;
        }
        if (std::fabs(mEnvPre) < 1.0e-30f) mEnvPre = 0.0f;
    }
    void flushDenormalsPost()
    {
        for (int i = 0; i < 3; ++i)
        {
            if (std::fabs(mApA[i].lp) < 1.0e-30f) mApA[i].lp = 0.0f;
            if (std::fabs(mApB[i].lp) < 1.0e-30f) mApB[i].lp = 0.0f;
        }
        if (std::fabs(mEnvPost) < 1.0e-30f) mEnvPost = 0.0f;
        if (std::fabs(mEncMixSm) < 1.0e-30f) mEncMixSm = 0.0f;
    }

    // ------------------------------- constants -------------------------------
    static constexpr int   kCtrl = 32;      // Stage A coeff recompute period
    static constexpr float kSettle = 1.0e-6f;
    static constexpr float kEnvLo = 0.03f, kEnvHi = 0.50f; // push knee (~ -30 .. -6 dBFS)
    // Stage A depths
    static constexpr float kDepthA1 = 4.0f; // 90 Hz peak scale (clamped +3 dB)
    static constexpr float kDepthA2 = 2.0f; // 2 kHz dynamic droop scale
    static constexpr float kHfEase  = 1.5f; // static Age HF ease (into the -3 dB shelf)
    // Stage B
    static constexpr float kDriveEnv = 1.2f; // envelope contribution to drive
    static constexpr float kAgeComp  = 0.6f; // static low-comp from Age
    static constexpr float kEnvComp  = 1.8f; // dynamic low-comp from Age*env
    // Stage C
    static constexpr float kEncBase  = 0.12f;
    static constexpr float kEncMax   = 0.12f;
    static constexpr float kGsetA = 0.50f, kGsetB = 0.62f, kGthump = 0.10f, kGmaxHard = 0.72f;
    static constexpr double kDampA_Hz = 6000.0, kDampB_Hz = 3000.0;

    // -------------------------------- state --------------------------------
    double mFs = 48000.0;
    bool mPrepared = false;

    // macro targets + smoothers
    float mAgeTarget = 0.0f, mThumpTarget = 0.0f, mSizeTarget = 0.0f;
    float mAgeSm = 0.0f, mThumpSm = 0.0f, mSizeSm = 0.0f;

    // smoothing coefficients
    float mMacroK = 0.02f, mMixK = 0.02f;
    float mEnvAtk = 0.02f, mEnvRel = 0.005f, mEnvAtkPost = 0.02f, mEnvRelPost = 0.005f;

    // envelopes
    float mEnvPre = 0.0f, mEnvPost = 0.0f;

    // Stage A
    Biquad mA1, mA2;
    int mCtrl = 0;
    float mA1Db = 0.0f, mA2Db = 0.0f, mDriveDbg = 1.0f;

    // Stage B
    Biquad mHp800, mLp3800, mLp180;
    Halfband mHb;

    // Stage C
    DampAllpass mApA[3], mApB[3];
    float mEncMixSm = 0.0f;
};

} // namespace nam_rig
