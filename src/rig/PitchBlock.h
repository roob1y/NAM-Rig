#pragma once
// PitchBlock — WHAMMY pitch-bend, mono, DAW rate, pre-amp. After the gate and BEFORE
// the envelope filter / compressor. Default-bypassed (raw chain stays bit-exact).
//
// A continuous pitch-bend (DigiTech Whammy style): a Mode selects the interval reached
// at the toe (+2/+1/+5th/-1/-2 oct) and the treadle sweeps from heel (unison) to that
// interval. Mono granular shifter (OctaveShifter.h), ZERO latency. The MPM tracker
// (PitchTracker.h) only sets the grain window w = 2*T0 so the two crossfading taps stay
// one period apart (coherent). Crossfades to pure dry at the heel and fades out on
// unclear input (chords/mutes) so it fails gracefully. JUCE-free; pitch_test.cpp.

#include "Blocks.h"
#include "PitchTracker.h"
#include "OctaveShifter.h"
#include <atomic>
#include <algorithm>
#include <cmath>
#include <vector>

namespace nam_rig
{

class PitchBlock : public MonoBlock
{
public:
    PitchBlock() { setBypassed(true); }

    const char *name() const override { return "Pitch"; }

    void setWhammyMode(int m) { mWMode.store(m < 0 ? 0 : (m > 4 ? 4 : m)); } // target interval
    void setWhammy(float v)   { mWhammyPos.store(clamp01(v)); }              // treadle 0..1

    void prepare(const BlockContext &ctx) override
    {
        mSampleRate = ctx.sampleRate;
        mTracker.prepare(mSampleRate);
        mWhammy.prepare(mSampleRate);
        mWSmooth = coefForMs(15.0f, mSampleRate);
        reset();
        mPrepared = true;
    }

    void reset() override
    {
        mTracker.reset();
        mWhammy.reset();
        mWhammyRatio = 1.0f; mWhammyWet = 0.0f;
        mClarityGate = 0.0f;
        mLastW = (float)(2.0 * mSampleRate / 120.0);
        mTrackedHz.store(0.0f);
    }

    // Mono granular shift = zero latency.
    double latencySamples() const override { return 0.0; }

    void process(float *mono, int numSamples) override
    {
        if (!mPrepared) return;
        whammy(mono, numSamples);
        // Foolproof output guard: transparent below ~0.9, soft-limits peaks so the shifted
        // voice can never hard-clip the amp input.
        for (int i = 0; i < numSamples; ++i) mono[i] = softLimit(mono[i]);
    }

    float trackedHz() const { return mTrackedHz.load(); }

private:
    // ---------- WHAMMY (continuous pitch bend), mono granular, zero latency ----------
    void whammy(float *mono, int numSamples)
    {
        static constexpr float kCents[5] = { 2400.0f, 1200.0f, 700.0f, -1200.0f, -2400.0f };
        const float targetCents = kCents[mWMode.load()];
        const float treadle = mWhammyPos.load();
        // Treadle -> shift ratio in the LOG (cents) domain: heel (0) = unison,
        // toe (1) = the full mode interval.
        const float targetRatio = std::pow(2.0f, treadle * targetCents / 1200.0f);
        // Blend to PURE DRY at the heel so "no whammy" is clean — the shifter itself
        // colours/warbles even at unison, so we crossfade it in as the treadle moves.
        // The wet blend is SMOOTHED per-sample: the knob is control-rate, so a raw
        // per-block wet stepped audibly (crackle in the bottom range of the knob).
        const float wetTarget = clamp01(treadle * 10.0f); // fully wet by ~0.1 treadle
        const float wetCoef = coefForMs(6.0f, mSampleRate);
        float wetS = mWhammyWet;

        // The MPM tracker is the OCTAVE AUTHORITY for the grain window w = 2*T0, which keeps
        // the two crossfading taps exactly one period apart (coherent = no warble) — a wrong/
        // abrupt window is what smears them. Only trust T0 when clearly pitched, and SMOOTH
        // the window so it never jumps.
        mTracker.process(mono, numSamples);
        const bool voiced = mTracker.voiced();
        const float t0 = mTracker.periodSamples();
        const float clarity = mTracker.clarity();
        if (voiced && t0 > 4.0f && clarity > kClarLo)
            mLastW += 0.4f * (2.0f * t0 - mLastW);   // smoothed, confidence-gated window update
        const float wT = mLastW, ws = mWSmooth;
        const float rSlew = coefForMs(20.0f, mSampleRate);
        // Clarity crossfade: fade the shifter OUT toward dry on chords / mutes / noise (where
        // the granular engine warbles) so it fails gracefully. LENIENT thresholds keep it fully
        // present through expressive bends/vibrato on a clearly-pitched single note.
        const float clarTarget = (voiced && clarity > kWhamClarLo)
            ? clamp01((clarity - kWhamClarLo) / (kWhamClarHi - kWhamClarLo)) : 0.0f;
        const float clarCoef = coefForMs(20.0f, mSampleRate);
        float ratio = mWhammyRatio;
        for (int i = 0; i < numSamples; ++i)
        {
            const float dry = mono[i];
            ratio += rSlew * (targetRatio - ratio);
            const float sh = mWhammy.process(mono[i], wT, ratio, ws);
            wetS += wetCoef * (wetTarget - wetS);
            mClarityGate += clarCoef * (clarTarget - mClarityGate);
            const float wet = wetS * mClarityGate;      // treadle wet, faded by clarity
            mono[i] = (1.0f - wet) * dry + wet * sh;     // heel / unclear = dry, no warble
        }
        mWhammyRatio = ratio;
        mWhammyWet = wetS;
        mTrackedHz.store(voiced ? mTracker.hz() : 0.0f);
    }

    static float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
    static float softLimit(float x)
    {
        const float a = std::abs(x);
        if (a <= 0.9f) return x;
        const float s = x < 0.0f ? -1.0f : 1.0f;
        return s * (0.9f + 0.1f * std::tanh((a - 0.9f) * 10.0f));
    }
    static float coefForMs(float ms, double sr)
    {
        return 1.0f - (float)std::exp(-1.0 / (std::max(0.05f, ms) * 0.001 * sr));
    }

    static constexpr float kClarLo = 0.55f;      // window-update confidence gate
    static constexpr float kWhamClarLo = 0.35f;  // clarity fade (LENIENT: keep present through bends)
    static constexpr float kWhamClarHi = 0.60f;

    PitchTracker  mTracker;   // sets the grain window (octave-robust period)
    OctaveShifter mWhammy;    // mono granular shifter
    std::atomic<int>   mWMode{1};        // target interval (default +1 oct)
    std::atomic<float> mWhammyPos{0.0f}; // treadle 0..1
    float mWhammyRatio = 1.0f; // slewed shift ratio
    float mWhammyWet = 0.0f;   // smoothed dry/wet blend (anti-crackle)
    float mClarityGate = 0.0f, mLastW = 800.0f, mWSmooth = 0.0014f;
    std::atomic<float> mTrackedHz{0.0f};
    double mSampleRate = 48000.0;
    bool mPrepared = false;
};

} // namespace nam_rig
