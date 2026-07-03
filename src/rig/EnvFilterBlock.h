#pragma once
// EnvFilterBlock — envelope filter / auto-wah, mono, DAW rate, pre-amp. Sits in
// the shared mono pre section BEFORE the compressor so it tracks the un-squashed
// guitar dynamics. Zero latency.
//
// Two voices, each faithful to a real unit's control complement:
//   * FX25  = DOD FX25B — knobs SENSITIVITY, RANGE, BLEND. Fixed BP/Up/Q; full-
//     wave OTA detector, fast, 500k input Z.
//   * QTRON = EHX Q-Tron+ — knobs GAIN, PEAK; switches MODE [LP|BP|HP|MIX],
//     DRIVE [Up|Down], RANGE [Hi|Lo], BOOST [Normal|Boost], RESPONSE [Fast|Slow].
//     Half-wave photocell detector, self-osc-capable, 300k input Z. MIX = BP+dry.
//     BOOST engages a preamp that drives the filter with an ANTI-ALIASED cubic
//     soft-clip (the chewy Q-Tron Boost) + a level lift.
//
// ANTI-ALIASING: the Boost preamp is the only nonlinearity in the AUDIO path, so
// it uses the shared cubic soft-clip with ADAA (Saturation.h), BAKED IN at 2nd
// order (F2/Parker-Bilbao; ~16 dB less alias than naive, negligible CPU). The
// detector rectifier is NOT anti-aliased — its output feeds the heavily-smoothed
// envelope, so its aliasing never reaches the audio. setAdaaOrder stays only as a
// test hook (order 0 naive / 1 / 2) for the aliasing regression; not user-facing.
//
// CALIBRATION: runs AFTER RigChain's global input-cal scale (CalNorm.h). PEDAL
// FRONT-END: per-voice input-Z loading (high-shelf CUT = delta from the ~1 MΩ
// interface), input+output coupling HPs. DRY blend stays true -> mix=0 bit-exact.
//
// JUCE-free core; verified offline by tests/env_filter_test.cpp.

#include "Blocks.h"
#include "Svf.h"
#include "Biquad.h"
#include "Saturation.h"
#include <atomic>
#include <algorithm>
#include <cmath>
#include <vector>

namespace nam_rig
{

class EnvFilterBlock : public MonoBlock
{
public:
    enum Voice { kFX25 = 0, kQTron = 1 };
    enum Mode  { kLP = 0, kBP = 1, kHP = 2, kMix = 3 }; // Q-Tron modes (MIX = BP + dry)

    static constexpr float kIfaceOhms = 1.0e6f;   // interface the DI was captured through (1 MΩ)
    static constexpr double kBoostDrive = 3.0;    // preamp gain into the Boost soft-clip

    EnvFilterBlock() { setBypassed(true); } // off until enabled (raw chain stays bit-exact)

    const char *name() const override { return "EnvFilter"; }

    // ---- parameters (thread-safe; 0..1 unless noted) ----
    void setSensitivity(float v) { mSensitivity.store(clamp01(v)); } // FX25 Sensitivity / Q-Tron Gain
    void setRange(float v)       { mRange.store(clamp01(v)); }       // sweep centre
    void setResonance(float v)   { mResonance.store(clamp01(v)); }   // Q-Tron Peak
    void setDepth(float v)       { mDepth.store(clamp01(v)); }       // sweep width (fixed per voice)
    void setAttackMs(float v)    { mAttackMs.store(v < 0.1f ? 0.1f : v); } // Q-Tron Response
    void setMode(int m)          { mMode.store(m < 0 ? 0 : (m > 3 ? 3 : m)); }
    void setDirectionUp(bool up) { mUp.store(up); }
    void setBoost(bool b)        { mBoost.store(b); }                // Q-Tron Boost
    void setAdaaOrder(int o)     { mAdaaOrder.store(o < 0 ? 0 : (o > 2 ? 2 : o)); } // 0 naive,1,2
    void setMix(float v)         { mMix.store(clamp01(v)); }         // FX25 Blend (0 = dry, bit-exact)
    void setVoice(int v)         { mVoice.store(v == kQTron ? kQTron : kFX25); }

    void prepare(const BlockContext &ctx) override
    {
        mSampleRate = ctx.sampleRate;
        mSvf.prepare(mSampleRate);
        mWork.assign((size_t)std::max(1, ctx.maxBlockSize), 0.0f);
        mInHp = Biquad::highpass1(mSampleRate, 8.0);
        mOutHp = Biquad::highpass1(mSampleRate, 12.0);
        mLastVoice = -1;
        reset();
        mPrepared = true;
    }

    void reset() override
    {
        mSvf.reset(); mInHp.reset(); mInShelf.reset(); mOutHp.reset();
        mEnv = 0.0f; mLag = 0.0f;
        mU1 = 0.0; mU2 = 0.0;
        mCutoffPub.store(200.0f);
    }

    double latencySamples() const override { return 0.0; }

    void process(float *mono, int numSamples) override
    {
        if (!mPrepared) return;
        if (numSamples > (int)mWork.size()) return;

        const double sr = mSampleRate;
        const int voice = mVoice.load();
        const bool up = mUp.load();
        const int mode = mMode.load();
        const bool boost = mBoost.load() && voice == kQTron;
        const int adaa = mAdaaOrder.load();
        const float mix = mMix.load();

        const float inShelfHz = (voice == kQTron) ? 3500.0f : 4000.0f;
        const float inShelfDb = (voice == kQTron) ? -1.2f   : -0.7f;
        if (voice != mLastVoice)
        {
            mInShelf.copyCoeffsFrom(Biquad::highshelf(sr, inShelfHz, inShelfDb));
            mLastVoice = voice;
        }

        const float sensGain = std::pow(2.0f, mSensitivity.load() * 6.0f);
        const float baseCut = 80.0f * std::pow(2.0f, mRange.load() * 4.0f);
        const float sweepOct = 1.5f + mDepth.load() * 2.5f;
        const float qMax = (voice == kQTron) ? 16.0f : 8.0f;
        const float Q = 0.7f * std::pow(qMax / 0.7f, mResonance.load());
        const float relMs = (voice == kQTron) ? 250.0f : 150.0f;
        const float lagMs = (voice == kQTron) ? 25.0f  : 2.0f;
        const float attCoef = coefForMs(mAttackMs.load(), sr);
        const float relCoef = coefForMs(relMs, sr);
        const float lagCoef = coefForMs(lagMs, sr);
        const float outLevel = boost ? 1.3f : 1.0f;

        // ---- input front-end: coupling HP + input-Z shelf (detector + filter see THIS) ----
        float *w = mWork.data();
        for (int i = 0; i < numSamples; ++i) w[i] = mono[i];
        mInHp.process(w, numSamples);
        mInShelf.process(w, numSamples);

        float env = mEnv, lag = mLag;
        double u1 = mU1, u2 = mU2; // Boost soft-clip ADAA history
        float lastCut = mCutoffPub.load();

        for (int i = 0; i < numSamples; ++i)
        {
            const float x = w[i];

            const float driven = x * sensGain;
            const float det = (voice == kQTron) ? (driven > 0.0f ? driven : 0.0f)
                                                : std::abs(driven);
            env += (det > env ? attCoef : relCoef) * (det - env);
            lag += lagCoef * (env - lag);

            const float env01 = lag > 1.0f ? 1.0f : lag;
            const float envEff = up ? env01 : (1.0f - env01);
            const float fc = baseCut * std::pow(2.0f, envEff * sweepOct);

            mSvf.setCoeffs(fc, Q);
            // BOOST: anti-aliased cubic soft-clip of the preamp-driven signal.
            const double u = (double)x * kBoostDrive;
            double finD = (double)x;
            if (boost)
                finD = (adaa >= 2) ? sat::cubicADAA2(u, u1, u2)
                     : (adaa == 1) ? sat::cubicADAA1(u, u1)
                     : sat::cubF(u); // 0 = naive (for A/B)
            u2 = u1; u1 = u; // keep history fresh even when not boosting

            const Svf::Out o = mSvf.tick((float)finD);
            const float wet = (mode == kLP) ? o.lp
                            : (mode == kHP) ? o.hp
                            : (mode == kMix) ? (0.5f * o.bp + 0.5f * (float)finD)
                            : o.bp;
            w[i] = wet;
            lastCut = fc;
        }

        mOutHp.process(w, numSamples);
        if (outLevel != 1.0f)
            for (int i = 0; i < numSamples; ++i) w[i] *= outLevel;

        if (mix >= 1.0f)
            for (int i = 0; i < numSamples; ++i) mono[i] = w[i];
        else if (mix > 0.0f)
            for (int i = 0; i < numSamples; ++i) mono[i] = mono[i] * (1.0f - mix) + w[i] * mix;

        mSvf.flushDenorms();
        mEnv = flush(env); mLag = flush(lag);
        mU1 = u1; mU2 = u2;
        mCutoffPub.store(lastCut);
    }

    float currentCutoffHz() const { return mCutoffPub.load(); }

private:
    static float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
    static float flush(float v) { return std::abs(v) < 1.0e-30f ? 0.0f : v; }
    static float coefForMs(float ms, double sr)
    {
        return 1.0f - (float)std::exp(-1.0 / (std::max(0.05f, ms) * 0.001 * sr));
    }

    std::atomic<float> mSensitivity{0.5f};
    std::atomic<float> mRange{0.35f};
    std::atomic<float> mResonance{0.6f};
    std::atomic<float> mDepth{0.7f};
    std::atomic<float> mAttackMs{8.0f};
    std::atomic<int>   mMode{kBP};
    std::atomic<bool>  mUp{true};
    std::atomic<bool>  mBoost{false};
    std::atomic<int>   mAdaaOrder{2};
    std::atomic<float> mMix{1.0f};
    std::atomic<int>   mVoice{kFX25};

    Svf mSvf;
    Biquad mInHp, mInShelf, mOutHp;
    std::vector<float> mWork;
    int mLastVoice = -1;
    float mEnv = 0.0f, mLag = 0.0f;
    double mU1 = 0.0, mU2 = 0.0; // Boost ADAA history
    std::atomic<float> mCutoffPub{200.0f};
    double mSampleRate = 48000.0;
    bool mPrepared = false;
};

} // namespace nam_rig
