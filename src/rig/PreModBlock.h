#pragma once
// PreModBlock — a MONO modulation pedal that sits IN FRONT OF THE AMP, in the
// shared pre section (after the drive rack, before the A/B split). This is the
// "pedalboard modulation" position: a real chorus/phaser/flanger/tremolo/uni-vibe
// stompbox feeding the amp, so the modulated signal is coloured by the amp's
// nonlinearity — deliberately DISTINCT from the post-cab STEREO ModBlock, which
// only ever sees the finished, cabinet-filtered tone.
//
// Because it feeds one amp it is strictly MONO (one channel). No Width / stereo
// spread lives here — width belongs after the cab. Voicing follows the same
// house rule as ModBlock: each effect is modelled on the real pedal, and only the
// controls that pedal actually has are exposed; everything else is hardwired to
// that pedal's sweet spot.
//
// SCAFFOLD STATE (2026-07-02): the full type/param FRAME is in place for all five
// pedals, but only CHORUS is voiced so far (a single-voice CE-2-style BBD chorus).
// Phaser / Flanger / Tremolo / Uni-Vibe are clean passthrough stubs — see the
// switch in processSample; they are the next pedals to voice (per-pedal circuit
// analysis + ear method). Selecting an un-voiced type is transparent, not silent.
//
// Zero reported latency (the chorus base delay is part of the effect, not PDC —
// same convention as ModBlock). JUCE-free core DSP: verified offline by
// tests/premod_test.cpp with the juce_audio_basics stub.

#include "Blocks.h"
#include "Lfo.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace nam_rig
{

class PreModBlock : public MonoBlock
{
public:
    enum Type { kChorus = 0, kPhaser, kFlanger, kTremolo, kUniVibe, kNumTypes };

    // ---- voicing constants (fixed; the knobs scale within these) ----
    // Mono CE-2-style chorus: one BBD-delayed voice mixed with dry. The nominal
    // bucket-brigade delay is ~5 ms; the LFO sweeps it up by kChorusSpreadMs. A
    // single voice (not the post-cab section's 3) is the authentic MONO output of
    // a real analog chorus — the doubling/shimmer, not an artificial stereo spread.
    static constexpr double kChorusBaseMs = 5.0;
    static constexpr double kChorusSpreadMs = 4.0;
    static constexpr float kChorusMaxRateHz = 5.0f;  // keep it a chorus (faster -> vibrato/warble)
    static constexpr double kChorusBbdHz = 2800.0;   // BBD/clock HF rolloff corner on the wet path
    static constexpr float kChorusBbdDrive = 1.15f;  // gentle bucket-brigade soft-clip

    // LFO period in beats for each sync choice (index 0 = Off = free rate). Same
    // 10-entry division table the ModBlock / DelayBlock use, so tempo-sync feels
    // identical across the rig.
    static constexpr int kNumSync = 10;
    static double syncBeats(int i)
    {
        static const double beats[kNumSync] = {0.0, 4.0, 2.0, 1.0, 1.5, 2.0 / 3.0,
                                               0.5, 0.75, 1.0 / 3.0, 0.25};
        return (i > 0 && i < kNumSync) ? beats[i] : 0.0;
    }

    // Free-rate ceiling per pedal (sync ignores this and honours the host division).
    static float maxRateHz(Type t)
    {
        if (t == kChorus) return kChorusMaxRateHz;
        return 10.0f; // phaser / flanger / tremolo / uni-vibe (voiced later)
    }

    const char *name() const override { return "Pre Mod"; }

    void prepare(const BlockContext &ctx) override
    {
        mFs = ctx.sampleRate;
        // Size the delay line for the deepest chorus tap plus a little guard.
        const int maxDelay =
            (int)std::ceil((kChorusBaseMs + kChorusSpreadMs + 2.0) * 0.001 * mFs);
        mLine.prepare(maxDelay);
        mLfo.prepare(mFs);
        mSmoothK = 1.0f - std::exp((float)(-1.0 / (0.010 * mFs))); // 10 ms de-zip
        mBbdCoef = coefForHz(kChorusBbdHz, mFs);
        reset();
        mPrepared = true;
    }

    void reset() override
    {
        mLine.reset();
        mLfo.reset();
        mBbdLp = 0.0f;
        mDepthZ = mDepth;
        mMixZ = mMix;
    }

    // ---- parameters (audio thread) ----
    void setType(int t)
    {
        const Type ty = (Type)std::min(std::max(t, 0), (int)kNumTypes - 1);
        if (ty != mType)
        {
            mType = ty;
            if (mPrepared)
                reset(); // state from another algorithm is meaningless
        }
    }
    void setRateHz(float hz) { mFreeRateHz = hz; }
    void setSyncIndex(int i) { mSyncIndex = i; } // 0 = Off (free)
    void setBpm(double bpm) { if (bpm > 0.0) mBpm = bpm; }
    void setDepth(float d) { mDepth = d; }
    void setMix(float m) { mMix = m; }
    void setFeedback(float f) { mFeedback = f; } // reserved for phaser/flanger (voiced later)

    // Resolved LFO rate: honour the host division when synced, else the free knob
    // capped to keep each pedal in character.
    float effectiveRateHz() const
    {
        const double beats = syncBeats(mSyncIndex);
        if (beats > 0.0)
            return (float)((mBpm / 60.0) / beats);
        return std::min(mFreeRateHz, maxRateHz(mType));
    }

    void process(float *mono, int numSamples) override
    {
        mLfo.setRateHz(effectiveRateHz());
        mLfo.setWaveform(Lfo::Sine); // chorus is sinusoidal; other pedals set their own later

        for (int i = 0; i < numSamples; ++i)
        {
            mDepthZ += mSmoothK * (mDepth - mDepthZ);
            mMixZ += mSmoothK * (mMix - mMixZ);
            mono[i] = processSample(mono[i]);
            mLfo.advance();
        }
        flushDenormals();
    }

    double latencySamples() const override { return 0.0; }

private:
    float processSample(float x)
    {
        const float lfo = mLfo.value();
        switch (mType)
        {
        case kChorus:
        {
            // Single-voice mono BBD chorus (CE-2). Write dry, read one LFO-swept
            // tap with the 6-point Lagrange interpolator (FIR -> no click at the
            // integer-sample crossings a swept delay walks through), colour it with
            // the bucket-brigade voice (HF rolloff + gentle soft clip), then blend
            // against dry. Mix default 0.5 = the classic analog-chorus depth.
            mLine.write(x);
            const double sweepMs =
                kChorusBaseMs + (double)mDepthZ * kChorusSpreadMs * (0.5 + 0.5 * (double)lfo);
            float wet = mLine.readFrac6(sweepMs * 0.001 * mFs);
            wet = bbdColor(wet);
            return (1.0f - mMixZ) * x + mMixZ * wet;
        }
        case kPhaser:
        case kFlanger:
        case kTremolo:
        case kUniVibe:
        default:
            // TODO(premod): voice these front-of-amp pedals next (per-pedal circuit
            // analysis + ear method). Passthrough until then so an un-voiced
            // selection is transparent rather than silent.
            return x;
        }
    }

    // Bucket-brigade colour for the chorus wet path: a one-pole HF rolloff (the
    // BBD clock/anti-alias filtering) followed by a gentle soft-clip (the analog
    // compander/BBD saturation warmth). No-op-ish at unity drive on quiet signals.
    float bbdColor(float wet)
    {
        mBbdLp += mBbdCoef * (wet - mBbdLp); // HF rolloff
        const float u = mBbdLp * kChorusBbdDrive;
        return std::tanh(u) / kChorusBbdDrive; // makeup keeps the level ~constant
    }

    void flushDenormals()
    {
        if (std::abs(mBbdLp) < 1.0e-30f) mBbdLp = 0.0f;
    }

    static float coefForHz(double hz, double fs)
    {
        const double fc = std::min(std::max(hz, 1.0), 0.45 * fs);
        return (float)(1.0 - std::exp(-2.0 * 3.14159265358979323846 * fc / fs));
    }

    double mFs = 48000.0;
    Type mType = kChorus;
    Lfo mLfo;
    FracDelayLine mLine;
    float mBbdLp = 0.0f;
    float mBbdCoef = 1.0f;

    float mDepth = 0.5f, mMix = 0.5f, mFeedback = 0.0f;
    float mDepthZ = 0.5f, mMixZ = 0.5f, mSmoothK = 0.01f;
    float mFreeRateHz = 1.0f;
    int mSyncIndex = 0;
    double mBpm = 120.0;
    bool mPrepared = false;
};

} // namespace nam_rig
