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
// Verified by tests/eq_test.cpp against Biquad::magnitudeAt (the harness
// evaluates the SAME coefficients the audio path runs).

#include "Blocks.h"
#include "Biquad.h"
#include <array>
#include <atomic>

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

    void prepare(const BlockContext &ctx) override
    {
        mFs = ctx.sampleRate;
        for (auto &f : mFilters)
            f = Biquad::identity();
        for (auto &g : mApplied)
            g = 1.0e9f; // force coefficient rebuild on first block
        mPrepared = true;
    }

    void reset() override
    {
        for (auto &f : mFilters)
            f.reset();
    }

    void process(float *mono, int numSamples) override
    {
        if (!mPrepared)
            return;

        bool anyActive = false;
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
            }
            anyActive = anyActive || !mFilters[(size_t)b].isIdentity();
        }

        if (!anyActive)
            return; // all flat: bit-exact passthrough

        for (int b = 0; b < kNumBands; ++b)
            if (!mFilters[(size_t)b].isIdentity())
                mFilters[(size_t)b].process(mono, numSamples);
    }

    // Verification access: the live filter cascade.
    const Biquad &filter(int band) const { return mFilters[(size_t)band]; }

private:
    std::array<std::atomic<float>, kNumBands> mGainsDb{}; // default 0 = flat
    std::array<Biquad, kNumBands> mFilters{};
    std::array<float, kNumBands> mApplied{};
    double mFs = 48000.0;
    bool mPrepared = false;
};

} // namespace nam_rig
