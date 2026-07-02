#pragma once
// Tuner — monophonic guitar/bass pitch detector for the header tuner overlay.
//
// Method: McLeod Pitch Method (MPM) — the normalized square-difference function
// (NSDF), which is autocorrelation normalised by the local energy so its peaks
// sit in [-1, 1] and clean pitch reads ~1. We pick the FIRST NSDF peak that
// clears a fraction of the global peak (the "first key maximum"), which is what
// makes MPM robust against octave-up errors on harmonic-rich guitar tone, then
// refine the lag with parabolic interpolation.
//
// RT contract: prepare() (message thread) allocates; push() and analyse() are
// allocation- and lock-free. push() accumulates samples and analyses one fixed
// window at a time, publishing frequency (Hz) + clarity (0..1) as atomics for the
// UI thread. No JUCE dependency, so it is verified offline by tests/tuner_test.cpp.

#include <atomic>
#include <cmath>
#include <vector>
#include <algorithm>

namespace nam_rig
{

class Tuner
{
public:
    // Guitar/bass fundamental range: below a 5-string bass low B (~31 Hz) up past
    // the high E 24th fret (~1319 Hz). Generous margins on both ends.
    static constexpr float kMinHz = 28.0f;
    static constexpr float kMaxHz = 1500.0f;

    void prepare(double sampleRate, int windowSize = 4096)
    {
        mFs = sampleRate;
        mN = windowSize;
        mBuf.assign((size_t)mN, 0.0f);
        // NSDF only needs lags up to the lowest-frequency period.
        mMaxLag = std::min(mN - 1, (int)(mFs / kMinHz) + 1);
        mMinLag = std::max(2, (int)(mFs / kMaxHz));
        mNsdf.assign((size_t)mMaxLag + 1, 0.0f);
        reset();
    }

    void reset()
    {
        std::fill(mBuf.begin(), mBuf.end(), 0.0f);
        mWrite = 0;
        mFreq.store(0.0f);
        mClarity.store(0.0f);
    }

    // Push a block of mono samples (audio thread). Analyses whenever a full window
    // has accumulated; windows are non-overlapping (a tuner needs ~10-20 Hz update,
    // not every sample).
    void push(const float *x, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            mBuf[(size_t)mWrite++] = x[i];
            if (mWrite >= mN)
            {
                analyze();
                mWrite = 0;
            }
        }
    }

    // Latest estimate. frequency() is 0 when no pitch is found; clarity() in [0,1]
    // (≈1 = confident, clean pitch) lets the UI decide when to trust the reading.
    float frequency() const { return mFreq.load(); }
    float clarity() const { return mClarity.load(); }

private:
    void analyze()
    {
        const int N = mN;

        // Silence gate: ignore near-nothing so a decaying note doesn't chase noise.
        double power = 0.0;
        for (int i = 0; i < N; ++i)
            power += (double)mBuf[(size_t)i] * mBuf[(size_t)i];
        if (power < 1.0e-5)
        {
            mFreq.store(0.0f);
            mClarity.store(0.0f);
            return;
        }

        // NSDF(tau) = 2 * r(tau) / m(tau), where r is the autocorrelation at lag
        // tau and m is the summed energy of the two overlapping windows.
        for (int tau = 0; tau <= mMaxLag; ++tau)
        {
            double r = 0.0, m = 0.0;
            const int lim = N - tau;
            for (int i = 0; i < lim; ++i)
            {
                const double a = mBuf[(size_t)i];
                const double b = mBuf[(size_t)(i + tau)];
                r += a * b;
                m += a * a + b * b;
            }
            mNsdf[(size_t)tau] = (m > 0.0) ? (float)(2.0 * r / m) : 0.0f;
        }

        // Global peak over the search band sets the "key maximum" threshold.
        float globalMax = 0.0f;
        for (int tau = mMinLag; tau <= mMaxLag; ++tau)
            globalMax = std::max(globalMax, mNsdf[(size_t)tau]);
        if (globalMax <= 0.0f)
        {
            mFreq.store(0.0f);
            mClarity.store(0.0f);
            return;
        }
        const float thresh = kPeakFraction * globalMax;

        // Walk positive lobes; take the first lobe whose peak clears the threshold
        // (the fundamental, even if a later harmonic lobe is marginally taller).
        // Skip the always-positive zero-lag hump FROM tau = 1 (starting at minLag
        // could land inside the fundamental's own lobe and skip it on high notes);
        // enforce the max-frequency limit when accepting a peak instead.
        int tau = 1;
        while (tau <= mMaxLag && mNsdf[(size_t)tau] > 0.0f)
            ++tau; // past the zero-lag hump
        int bestTau = -1;
        float bestVal = 0.0f;
        while (tau <= mMaxLag)
        {
            if (mNsdf[(size_t)tau] > 0.0f)
            {
                float lobeMax = mNsdf[(size_t)tau];
                int lobePos = tau;
                while (tau <= mMaxLag && mNsdf[(size_t)tau] > 0.0f)
                {
                    if (mNsdf[(size_t)tau] > lobeMax)
                    {
                        lobeMax = mNsdf[(size_t)tau];
                        lobePos = tau;
                    }
                    ++tau;
                }
                if (lobeMax >= thresh && lobePos >= mMinLag)
                {
                    bestTau = lobePos;
                    bestVal = lobeMax;
                    break;
                }
            }
            else
                ++tau;
        }
        if (bestTau <= 0)
        {
            mFreq.store(0.0f);
            mClarity.store(0.0f);
            return;
        }

        // Parabolic interpolation of the peak lag for sub-sample (sub-cent) accuracy.
        float t = (float)bestTau;
        if (bestTau > 0 && bestTau < mMaxLag)
        {
            const float y0 = mNsdf[(size_t)(bestTau - 1)];
            const float y1 = mNsdf[(size_t)bestTau];
            const float y2 = mNsdf[(size_t)(bestTau + 1)];
            const float denom = y0 - 2.0f * y1 + y2;
            if (std::abs(denom) > 1.0e-9f)
                t = (float)bestTau + 0.5f * (y0 - y2) / denom;
        }

        const float freq = (t > 0.0f) ? (float)(mFs / t) : 0.0f;
        if (freq < kMinHz || freq > kMaxHz)
        {
            mFreq.store(0.0f);
            mClarity.store(0.0f);
            return;
        }
        mFreq.store(freq);
        mClarity.store(std::min(1.0f, std::max(0.0f, bestVal)));
    }

    static constexpr float kPeakFraction = 0.8f; // "first key maximum" fraction

    double mFs = 48000.0;
    int mN = 4096, mWrite = 0, mMinLag = 2, mMaxLag = 2048;
    std::vector<float> mBuf, mNsdf;
    std::atomic<float> mFreq{0.0f};
    std::atomic<float> mClarity{0.0f};
};

} // namespace nam_rig
