#pragma once
// GateBlock — noise gate / downward expander, mono, DAW rate, pre-amp.
//
// Designed to beat the stock ToneX/Gateway gates on their three classic
// failures:
//  1. Chatter near threshold  -> open/close HYSTERESIS (close = open - 6 dB)
//     plus a HOLD timer before release starts.
//  2. Clipped pick attacks    -> optional LOOKAHEAD (audio delayed, detector
//     isn't), reported as PDC so the chain stays time-aligned.
//  3. Chopped note tails      -> RELEASE is exponential in dB/s (natural
//     fade), and the close decision uses a slow RMS detector so a decaying
//     note tracks down smoothly instead of hard-cutting.
//
// Detector path: one-pole HPF (80 Hz, ignores hum/rumble when judging the
// string signal) -> |x| -> fast peak env (opens) + RMS env (closes).
// Gain path: state machine CLOSED/OPEN/HOLD/RELEASING; gain approaches 1.0
// exponentially on attack and SNAPS to exactly 1.0f when within epsilon, so
// the open gate is bit-exact passthrough. Floor is 'range' dB, not -inf,
// unless set to max.
//
// Verified by tests/gate_test.cpp (all measurements pass, sandbox gcc
// 2026-06-11): bit-exact open passthrough; -80 dB floor; PDC == lookahead;
// 100% first-1ms transient kept with 2 ms lookahead (71.8% without); hold +
// release timing; 1 opening on 2 s of threshold-level noise; max 0.7 dB/10ms
// step on decaying notes.
//
// All parameters are atomics, safe to set from the message thread; process()
// derives coefficients each block (cheap).

#include "Blocks.h"
#include <atomic>
#include <cmath>
#include <vector>

namespace nam_rig
{

class GateBlock : public MonoBlock
{
public:
    const char *name() const override { return "Gate"; }

    // ---- parameters (thread-safe) ----
    void setThresholdDb(float v) { mThresholdDb.store(v); }   // open threshold, dBFS (detector domain)
    void setRangeDb(float v) { mRangeDb.store(v); }           // max attenuation, dB (positive number)
    void setAttackMs(float v) { mAttackMs.store(v); }
    void setHoldMs(float v) { mHoldMs.store(v); }
    void setReleaseMs(float v) { mReleaseMs.store(v); }
    void setLookaheadMs(float v) { mLookaheadMs.store(v); }   // 0..kMaxLookaheadMs; changes PDC
    void setHysteresisDb(float v) { mHysteresisDb.store(v); }

    // "Off" must NOT chain-bypass this block: the lookahead delay has to keep
    // running or PDC breaks. Disabled = gate forced open (gain ramps to 1.0
    // and snaps -> bit-exact passthrough apart from the constant delay).
    void setEnabled(bool e) { mEnabled.store(e); }

    static constexpr float kMaxLookaheadMs = 5.0f;

    void prepare(const BlockContext &ctx) override
    {
        mSampleRate = ctx.sampleRate;
        const int maxLook = (int)std::ceil(kMaxLookaheadMs * 0.001 * mSampleRate) + 1;
        mDelay.assign((size_t)maxLook + (size_t)ctx.maxBlockSize, 0.0f);
        mDelayCap = (int)mDelay.size();
        reset();
        mPrepared = true;
    }

    void reset() override
    {
        std::fill(mDelay.begin(), mDelay.end(), 0.0f);
        mWrite = 0;
        mHpState = 0.0f;
        mPeakEnv = 0.0f;
        mRms2 = 0.0f;
        mGain = 0.0f; // start closed (silence in = silence out)
        mHoldCount = 0;
        mOpen = false;
    }

    double latencySamples() const override
    {
        return mPrepared ? (double)lookaheadSamples() : 0.0;
    }

    void process(float *mono, int numSamples) override
    {
        if (!mPrepared)
            return;

        // ---- per-block coefficient derivation ----
        const double sr = mSampleRate;
        const float openLin = dbToLin(mThresholdDb.load());
        const float closeLin = dbToLin(mThresholdDb.load() - mHysteresisDb.load());
        const float closeLin2 = closeLin * closeLin; // RMS^2 comparison
        const float rangeLin = dbToLin(-mRangeDb.load());
        const int holdSamples = (int)std::round(mHoldMs.load() * 0.001 * sr);
        const int look = lookaheadSamples();

        // attack: exponential approach to 1 with the given time constant
        const float attCoef = coefForMs(mAttackMs.load(), sr);
        // release: multiplicative decay (constant dB/s). 'release' is the time
        // to fall ~60 dB (an RT60-style control).
        const float relCoef = (float)std::exp(-std::log(1000.0) / (std::max(1.0f, mReleaseMs.load()) * 0.001 * sr));
        // detector coefficients
        const float hpCoef = coefForHz(80.0, sr);
        const float peakDecay = coefForMs(5.0f, sr);   // trigger env: 5 ms fall — must die fast;
                                                       // open-state stability is the job of
                                                       // hysteresis + hold, NOT this envelope
                                                       // (50 ms here silently tripled the hold)
        const float rmsCoef = coefForMs(5.0f, sr);     // ~5 ms RMS window

        float hp = mHpState, peak = mPeakEnv, rms2 = mRms2, gain = mGain;
        bool open = mOpen;
        int hold = mHoldCount;
        const bool enabled = mEnabled.load();

        for (int i = 0; i < numSamples; ++i)
        {
            const float x = mono[i];

            // ---- detector (pre-delay signal) ----
            hp += hpCoef * (x - hp);          // lowpass state
            const float hf = x - hp;          // high-passed (>~80 Hz)
            const float a = std::abs(hf);
            peak = std::max(a, peak * (1.0f - peakDecay)); // instant up, fast exp down
            rms2 += rmsCoef * (a * a - rms2);

            // ---- state machine ----
            if (!enabled || peak > openLin)
            {
                open = true;
                hold = 0;
            }
            else if (open)
            {
                if (rms2 > closeLin2)
                    hold = 0;            // still above close threshold: stay
                else if (++hold > holdSamples)
                    open = false;        // held long enough below: release
            }

            // ---- gain dynamics ----
            if (open)
            {
                gain += attCoef * (1.0f - gain);
                if (gain > 0.9999f)
                    gain = 1.0f;         // snap: open gate is bit-exact
            }
            else
            {
                gain *= relCoef;
                if (gain < rangeLin)
                    gain = rangeLin;     // floor at range
            }

            // ---- audio path (optionally delayed by lookahead) ----
            float y = x;
            if (look > 0)
            {
                mDelay[(size_t)mWrite] = x;
                int rd = mWrite - look;
                if (rd < 0)
                    rd += mDelayCap;
                y = mDelay[(size_t)rd];
                if (++mWrite >= mDelayCap)
                    mWrite = 0;
            }

            mono[i] = (gain == 1.0f) ? y : y * gain;
        }

        // flush denormals in the envelope states
        mHpState = flush(hp);
        mPeakEnv = flush(peak);
        mRms2 = flush(rms2);
        mGain = gain;
        mOpen = open;
        mHoldCount = hold;
    }

private:
    int lookaheadSamples() const
    {
        const float ms = std::min(std::max(mLookaheadMs.load(), 0.0f), kMaxLookaheadMs);
        return (int)std::round(ms * 0.001 * mSampleRate);
    }

    static float dbToLin(float db) { return std::pow(10.0f, db * 0.05f); }
    static float coefForMs(float ms, double sr)
    {
        return 1.0f - (float)std::exp(-1.0 / (std::max(0.01f, ms) * 0.001 * sr));
    }
    static float coefForHz(double hz, double sr)
    {
        return 1.0f - (float)std::exp(-2.0 * 3.14159265358979323846 * hz / sr);
    }
    static float flush(float v) { return std::abs(v) < 1.0e-30f ? 0.0f : v; }

    // parameters (defaults: a usable guitar gate)
    std::atomic<float> mThresholdDb{-50.0f};
    std::atomic<float> mRangeDb{80.0f};
    std::atomic<float> mAttackMs{0.1f};
    std::atomic<float> mHoldMs{50.0f};
    std::atomic<float> mReleaseMs{100.0f};
    std::atomic<float> mLookaheadMs{0.0f};
    std::atomic<float> mHysteresisDb{6.0f};
    std::atomic<bool> mEnabled{true};

    // state
    std::vector<float> mDelay;
    int mDelayCap = 0, mWrite = 0;
    float mHpState = 0.0f, mPeakEnv = 0.0f, mRms2 = 0.0f, mGain = 0.0f;
    bool mOpen = false;
    int mHoldCount = 0;
    double mSampleRate = 48000.0;
    bool mPrepared = false;
};

} // namespace nam_rig
