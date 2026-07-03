#pragma once
// PitchTracker — real-time monophonic f0 tracker (McLeod MPM / NSDF), factored so
// the octaver's OC-2 divider can be DEBOUNCED by a reliable period estimate (the
// research fix for "won't track / octave-jumps": use the tracked period to reject
// harmonic double-triggers). Same NSDF core as Tuner.h, but re-shaped for the
// AUDIO thread: cheap enough to run inline via three moves from the RT literature
// (docs/pitch_env/PITCH_TRACKING_RESEARCH.md §C/§D):
//
//   1. DECIMATE to ~12 kHz before correlating (guitar tops out ~1 kHz + a few
//      harmonics) — ~4x less work, no resolution loss (Derrien DAFx-14).
//   2. Per-HOP analysis (~5 ms), not per-sample.
//   3. SEED the lag search +/-1 semitone around the previous estimate; full-range
//      key-maximum re-acquire only on onset / clarity-drop. This is the biggest
//      single CPU AND octave-error win (Pardue NIME-14: "nearly eliminates
//      harmonic errors").
//
// Octave-robustness = MPM's amplitude-bounded NSDF in [-1,1] + "first key maximum
// above k*n_max" picking (k=0.9), plus a 3-point median and an explicit reject of
// low-confidence exact x2 / x0.5 jumps (Cycfi "Bias" stage). CLARITY (the NSDF
// value at the chosen peak) is a free, amplitude-independent confidence the block
// uses to crossfade the octave out on chords/mutes.
//
// JUCE-free (uses Biquad.h). Verified offline by tests/pitch_test.cpp.

#include "Biquad.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <vector>

namespace nam_rig
{

class PitchTracker
{
public:
    static constexpr float kMinHz = 55.0f;   // below low E (82) w/ margin for drop tunings
    static constexpr float kMaxHz = 1400.0f; // high E 24th fret ~1319
    static constexpr float kClarityGate = 0.60f; // "voiced" threshold on NSDF clarity
    static constexpr float kPeakFraction = 0.90f; // MPM first-key-maximum fraction (k)

    void prepare(double sampleRate)
    {
        mFs = sampleRate > 0.0 ? sampleRate : 48000.0;
        mDecim = std::max(1, (int)std::lround(mFs / 12000.0)); // ~12 kHz correlation rate
        mDecFs = mFs / (double)mDecim;
        // Anti-alias LPF at ~0.4*decimated-rate before downsampling.
        mAa = Biquad::lowpass(mFs, 0.40 * mDecFs);
        mW = 512;                                   // decimated analysis window
        mMinLag = std::max(2, (int)(mDecFs / kMaxHz));
        mMaxLag = std::min(mW - 1, (int)(mDecFs / kMinHz) + 1);
        mHop = 64;                                  // decimated hop (~5 ms)
        mRing.assign((size_t)mW, 0.0f);
        mScratch.assign((size_t)mW, 0.0f);
        mNsdf.assign((size_t)mMaxLag + 1, 0.0f);
        reset();
    }

    void reset()
    {
        mAa.reset();
        std::fill(mRing.begin(), mRing.end(), 0.0f);
        mWritePos = 0; mFilled = 0; mDecCount = 0; mSinceHop = 0;
        mLastLag = 0.0f; mHist[0] = mHist[1] = mHist[2] = 0.0f; mHistN = 0;
        mPeriodInput.store(0.0f); mHz.store(0.0f); mClarity.store(0.0f); mVoiced.store(false);
    }

    // AUDIO THREAD: feed the block's input (read-only). Decimates, and every hop
    // runs one NSDF analysis. Allocation-free.
    void process(const float *x, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            const float s = mAa.processSample(x[i]);
            if (++mDecCount >= mDecim)
            {
                mDecCount = 0;
                mRing[(size_t)mWritePos] = s;
                mWritePos = (mWritePos + 1) % mW;
                if (mFilled < mW) ++mFilled;
                if (++mSinceHop >= mHop) { mSinceHop = 0; if (mFilled >= mW) analyze(); }
            }
        }
    }

    // Latest estimate. periodSamples() is in INPUT samples (0 = unvoiced).
    float periodSamples() const { return mPeriodInput.load(); }
    float hz() const { return mHz.load(); }
    float clarity() const { return mClarity.load(); }
    bool  voiced() const { return mVoiced.load(); }

private:
    void analyze()
    {
        // Copy the last W decimated samples in chronological order.
        const int start = mWritePos; // oldest sample (next write slot)
        for (int k = 0; k < mW; ++k)
            mScratch[(size_t)k] = mRing[(size_t)((start + k) % mW)];

        double power = 0.0;
        for (int i = 0; i < mW; ++i) power += (double)mScratch[i] * mScratch[i];
        if (power < 1.0e-6) { publishUnvoiced(); return; }

        // SEEDED search: if we had a confident lock, look only +/-1 semitone (~6%)
        // around the previous lag; accept if that peak still clears the gate.
        float lag = -1.0f, clar = 0.0f;
        if (mLastLag > 0.0f)
        {
            const int lo = std::max(mMinLag, (int)std::floor(mLastLag * 0.94f));
            const int hi = std::min(mMaxLag, (int)std::ceil(mLastLag * 1.06f));
            const int peak = nsdfPeakInBand(lo, hi);
            if (peak > 0)
            {
                const float c = mNsdf[(size_t)peak];
                if (c >= kClarityGate) { lag = refine(peak); clar = c; }
            }
        }
        // Full re-acquire (onset / lock lost): key-maximum picking over the band.
        if (lag < 0.0f)
        {
            const int key = fullAcquire();
            if (key > 0) { lag = refine(key); clar = mNsdf[(size_t)key]; }
        }
        if (lag <= 0.0f || clar < kClarityGate) { publishUnvoiced(); return; }

        // Octave-jump reject (Cycfi "Bias"): if the new lag is ~half/double a stable
        // previous lag and we're not extremely confident, keep the previous octave.
        if (mLastLag > 0.0f && clar < 0.93f)
        {
            const float ratio = lag / mLastLag;
            if (std::abs(ratio - 0.5f) < 0.06f || std::abs(ratio - 2.0f) < 0.12f)
                lag = mLastLag;
        }

        // 3-point median on the accepted lag (kills spurious single-frame spikes).
        mHist[2] = mHist[1]; mHist[1] = mHist[0]; mHist[0] = lag;
        if (mHistN < 3) ++mHistN;
        const float med = (mHistN >= 3) ? median3(mHist[0], mHist[1], mHist[2]) : lag;

        const double hz = mDecFs / (double)med;
        if (hz < kMinHz || hz > kMaxHz) { publishUnvoiced(); return; }

        mLastLag = med;
        mPeriodInput.store((float)(med * (double)mDecim)); // decimated -> input samples
        mHz.store((float)hz);
        mClarity.store(std::min(1.0f, std::max(0.0f, clar)));
        mVoiced.store(true);
    }

    // NSDF over [lo,hi]; returns the lag of the maximum (or -1).
    int nsdfPeakInBand(int lo, int hi)
    {
        int best = -1; float bestV = -2.0f;
        for (int tau = lo; tau <= hi; ++tau)
        {
            const float v = nsdfAt(tau);
            mNsdf[(size_t)tau] = v;
            if (v > bestV) { bestV = v; best = tau; }
        }
        return (bestV > 0.0f) ? best : -1;
    }

    // Full NSDF + "first key maximum above k*globalMax" (octave-up-robust).
    int fullAcquire()
    {
        for (int tau = mMinLag; tau <= mMaxLag; ++tau)
            mNsdf[(size_t)tau] = nsdfAt(tau);
        float globalMax = 0.0f;
        for (int tau = mMinLag; tau <= mMaxLag; ++tau)
            globalMax = std::max(globalMax, mNsdf[(size_t)tau]);
        if (globalMax <= 0.0f) return -1;
        const float thresh = kPeakFraction * globalMax;
        // Walk positive lobes; take the first whose peak clears the threshold.
        int tau = mMinLag;
        while (tau <= mMaxLag)
        {
            if (mNsdf[(size_t)tau] > 0.0f)
            {
                float lobeMax = mNsdf[(size_t)tau]; int lobePos = tau;
                while (tau <= mMaxLag && mNsdf[(size_t)tau] > 0.0f)
                {
                    if (mNsdf[(size_t)tau] > lobeMax) { lobeMax = mNsdf[(size_t)tau]; lobePos = tau; }
                    ++tau;
                }
                if (lobeMax >= thresh) return lobePos;
            }
            else ++tau;
        }
        return -1;
    }

    // Single NSDF value at lag tau over the scratch window.
    float nsdfAt(int tau)
    {
        double r = 0.0, m = 0.0;
        const int lim = mW - tau;
        for (int i = 0; i < lim; ++i)
        {
            const double a = mScratch[(size_t)i];
            const double b = mScratch[(size_t)(i + tau)];
            r += a * b; m += a * a + b * b;
        }
        return (m > 0.0) ? (float)(2.0 * r / m) : 0.0f;
    }

    // Parabolic interpolation of the NSDF peak lag (sub-sample accuracy).
    float refine(int tau)
    {
        if (tau <= mMinLag || tau >= mMaxLag) return (float)tau;
        const float y0 = mNsdf[(size_t)(tau - 1)], y1 = mNsdf[(size_t)tau], y2 = mNsdf[(size_t)(tau + 1)];
        const float d = y0 - 2.0f * y1 + y2;
        return (std::abs(d) > 1.0e-9f) ? (float)tau + 0.5f * (y0 - y2) / d : (float)tau;
    }

    static float median3(float a, float b, float c)
    {
        return std::max(std::min(a, b), std::min(std::max(a, b), c));
    }

    void publishUnvoiced()
    {
        mPeriodInput.store(0.0f); mHz.store(0.0f); mClarity.store(0.0f); mVoiced.store(false);
        // keep mLastLag so a brief dropout re-seeds fast, but a long gap re-acquires.
    }

    double mFs = 48000.0, mDecFs = 12000.0;
    int mDecim = 4, mW = 512, mMinLag = 8, mMaxLag = 218, mHop = 64;
    Biquad mAa;
    std::vector<float> mRing, mScratch, mNsdf;
    int mWritePos = 0, mFilled = 0, mDecCount = 0, mSinceHop = 0;
    float mLastLag = 0.0f;
    float mHist[3] = {0.0f, 0.0f, 0.0f};
    int mHistN = 0;
    std::atomic<float> mPeriodInput{0.0f};
    std::atomic<float> mHz{0.0f};
    std::atomic<float> mClarity{0.0f};
    std::atomic<bool>  mVoiced{false};
};

} // namespace nam_rig
