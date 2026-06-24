#pragma once
// EqBlock — 8-band graphic EQ, mono, DAW rate, post-amp / PRE-CAB.
//
// Placement rationale (June 2026): this is the classic guitar graphic-EQ
// position (MXR in the loop, Mesa 5-band) — tone shaping that the cab then
// smooths. Corrective duties (rumble/fizz) belong to the post-cab HPF/LPF in
// CabBlock, which is also why this EQ has no bands above 8k (the cab IR owns
// everything up there).
//
// Bands: 62.5, 125, 250, 500, 1k, 2k, 4k, 8k Hz — octave spacing trimmed to
// where a guitar rig lives. RBJ peaking, constant Q = 1.41 (one octave),
// +/-12 dB. All-flat = the block snaps out of the path entirely (bit-exact
// passthrough). Zero latency; chain-bypass via eqOn is safe.
//
// Auto-gain (output loudness-lock): an INSTANT makeup gain computed directly
// from the band settings — the average of |H(f)|^2 over the guitar range with
// equal-energy-per-octave weighting, inverted (= 1/sqrt(meanPower)). Because it
// is a function of the curve, not of the signal, it lands the moment a band
// moves (no envelope lag, no pumping, works in silence); a 15 ms one-pole only
// de-zippers the applied gain. OFF by default so the harness measures the raw
// band gains; the plugin turns it on.
//
// Verified by tests/eq_test.cpp against Biquad::magnitudeAt (the harness
// evaluates the SAME coefficients the audio path runs).

#include "Blocks.h"
#include "Biquad.h"
#include <array>
#include <atomic>
#include <algorithm>
#include <cmath>

namespace nam_rig
{

class EqBlock : public MonoBlock
{
public:
    static constexpr int kNumBands = 8;
    static constexpr std::array<double, kNumBands> kBandHz = {
        62.5, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0};
    static constexpr double kQ = 1.41;       // one-octave bandwidth
    static constexpr float kMaxGainDb = 12.0f;

    const char *name() const override { return "Graphic EQ"; }

    // Thread-safe; coefficients are re-derived in process() when changed.
    void setBandGainDb(int band, float gainDb)
    {
        if (band >= 0 && band < kNumBands)
            mGainsDb[(size_t)band].store(gainDb);
    }

    // Output loudness-lock: instant curve-based makeup (see file header). OFF by
    // default so the verification harness measures the raw band gains.
    void setAutoGain(bool on) { mAutoGain = on; }
    bool autoGain() const { return mAutoGain; }
    float makeupGain() const { return mMakeupZ; } // current applied makeup (linear)

    void prepare(const BlockContext &ctx) override
    {
        mFs = ctx.sampleRate;
        for (auto &f : mFilters)
            f = Biquad::identity();
        for (auto &g : mApplied)
            g = 1.0e9f; // force coefficient rebuild on first block
        mGainCoef = 1.0f - (float)std::exp(-1.0 / (0.015 * ctx.sampleRate)); // 15 ms de-zip
        mMakeupTarget = 1.0f;
        mMakeupZ = 1.0f;
        mMakeupValid = false;
        mPrepared = true;
    }

    void reset() override
    {
        for (auto &f : mFilters)
            f.reset();
        mMakeupZ = 1.0f;
        mMakeupValid = false;
    }

    void process(float *mono, int numSamples) override
    {
        if (!mPrepared)
            return;

        bool anyActive = false;
        bool coeffsChanged = false;
        for (int b = 0; b < kNumBands; ++b)
        {
            const float g = mGainsDb[(size_t)b].load();
            if (g != mApplied[(size_t)b])
            {
                // preserve filter state across coefficient changes (no zipper
                // worse than a parameter step; bands move slowly by hand)
                const float z1 = mFilters[(size_t)b].z1, z2 = mFilters[(size_t)b].z2;
                mFilters[(size_t)b] = Biquad::peaking(mFs, kBandHz[(size_t)b], kQ, (double)g);
                mFilters[(size_t)b].z1 = z1;
                mFilters[(size_t)b].z2 = z2;
                mApplied[(size_t)b] = g;
                coeffsChanged = true;
            }
            anyActive = anyActive || !mFilters[(size_t)b].isIdentity();
        }

        if (!anyActive)
            return; // all flat: bit-exact passthrough (makeup idles at unity)

        for (int b = 0; b < kNumBands; ++b)
            if (!mFilters[(size_t)b].isIdentity())
                mFilters[(size_t)b].process(mono, numSamples);

        if (!mAutoGain)
            return; // raw shaped output (harness path / auto-gain disabled)

        // Recompute the instant makeup only when the curve actually moved.
        if (coeffsChanged || !mMakeupValid)
        {
            mMakeupTarget = computeMakeupGain();
            mMakeupValid = true;
        }
        // Apply with a 15 ms one-pole so dragging a band doesn't click.
        float g = mMakeupZ;
        const float c = mGainCoef, tgt = mMakeupTarget;
        for (int i = 0; i < numSamples; ++i)
        {
            g += c * (tgt - g);
            mono[i] *= g;
        }
        mMakeupZ = g;
    }

    // The instant makeup gain for the current band settings: 1/sqrt of the
    // equal-energy-per-octave mean of |H(f)|^2 across the guitar range. Public
    // so the verification harness can check it directly.
    float computeMakeupGain() const
    {
        constexpr int kPts = 160;
        constexpr double fLo = 50.0, fHi = 10000.0;
        double sumPow = 0.0;
        for (int i = 0; i < kPts; ++i)
        {
            const double f = fLo * std::pow(fHi / fLo, (double)i / (double)(kPts - 1));
            double magSq = 1.0;
            for (int b = 0; b < kNumBands; ++b)
                if (!mFilters[(size_t)b].isIdentity())
                {
                    const double m = mFilters[(size_t)b].magnitudeAt(mFs, f);
                    magSq *= m * m; // cascade: powers multiply
                }
            sumPow += magSq;
        }
        const double mean = sumPow / (double)kPts;                  // mean power per octave
        const float gain = (float)(1.0 / std::sqrt(std::max(mean, 1.0e-9)));
        return std::min(4.0f, std::max(0.25f, gain));               // ±12 dB safety clamp
    }

    // Verification access: the live filter cascade.
    const Biquad &filter(int band) const { return mFilters[(size_t)band]; }

private:
    std::array<std::atomic<float>, kNumBands> mGainsDb{}; // default 0 = flat
    std::array<Biquad, kNumBands> mFilters{};
    std::array<float, kNumBands> mApplied{};
    float mGainCoef = 0.0f;        // 15 ms applied-gain de-zipper
    float mMakeupTarget = 1.0f;    // instant makeup for the current curve
    float mMakeupZ = 1.0f;         // smoothed applied makeup
    bool mMakeupValid = false;     // recompute target when the curve moves
    bool mAutoGain = false;        // OFF by default; the plugin enables it
    double mFs = 48000.0;
    bool mPrepared = false;
};

} // namespace nam_rig
