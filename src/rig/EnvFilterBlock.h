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
// STEREO (pedalboard pool, added 2026-07-11): mono process() drives Lane L ONLY,
// bit-exact to the pre-stereo block. processStereo() runs Lane L and Lane R fully
// INDEPENDENTLY (own envelope/SVF/filter state each, from the SAME shared coeffs)
// — no cross-lane linking, the same "independent per-lane" approach PreModBlock /
// PreDelayBlock already use for their pedalboard stereo span. On the mono->stereo
// rising edge Lane R is reseeded from Lane L's CURRENT state (mStereoRunning edge
// detect) so it starts from a sensible value instead of resuming whatever it was
// frozen at the last time it ran (a stale envelope would otherwise snap the filter
// to an old cutoff for a moment).
//
// JUCE-free core; verified offline by tests/env_filter_test.cpp + tests/pedalboard_test.cpp.

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
        for (Lane *ln : {&mLaneL, &mLaneR})
        {
            ln->svf.prepare(mSampleRate);
            ln->work.assign((size_t)std::max(1, ctx.maxBlockSize), 0.0f);
            ln->inHp = Biquad::highpass1(mSampleRate, 8.0);
            ln->outHp = Biquad::highpass1(mSampleRate, 12.0);
        }
        mLastVoice = -1;
        reset();
        mPrepared = true;
    }

    void reset() override
    {
        for (Lane *ln : {&mLaneL, &mLaneR})
        {
            ln->svf.reset(); ln->inHp.reset(); ln->inShelf.reset(); ln->outHp.reset();
            ln->env = 0.0f; ln->lag = 0.0f;
            ln->u1 = 0.0; ln->u2 = 0.0;
        }
        mCutoffPub.store(200.0f);
        mStereoRunning = false; // next processStereo() is a fresh rising edge
    }

    double latencySamples() const override { return 0.0; }

    // MONO: drive Lane L ONLY -> bit-exact to the pre-stereo block.
    void process(float *mono, int numSamples) override
    {
        if (!mPrepared) return;
        if (numSamples > (int)mLaneL.work.size()) return;
        mStereoRunning = false; // record that the last pass was mono (only Lane L advanced)
        const Cfg cfg = computeCfg();
        float lastCut = mCutoffPub.load();
        processLane(mLaneL, mono, numSamples, cfg, lastCut);
        mCutoffPub.store(lastCut);
    }

    // Mono-in / stereo-out front pedal (pedalboard Stereo span): L -> Amp A, R ->
    // Amp B, each lane fully independent (own envelope/SVF/filter state) — no
    // cross-lane linking. See the class comment for the mono->stereo reseed.
    void processStereo(float *L, float *R, int numSamples)
    {
        if (!mPrepared) return;
        if (numSamples > (int)mLaneL.work.size()) return;
        if (!mStereoRunning) { mLaneR = mLaneL; mStereoRunning = true; }
        const Cfg cfg = computeCfg();
        float lastCutL = mCutoffPub.load();
        float lastCutR = lastCutL;
        processLane(mLaneL, L, numSamples, cfg, lastCutL);
        processLane(mLaneR, R, numSamples, cfg, lastCutR);
        mCutoffPub.store(lastCutL); // meter reflects Lane L (single-channel readout)
    }

    float currentCutoffHz() const { return mCutoffPub.load(); }

private:
    static float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
    static float flush(float v) { return std::abs(v) < 1.0e-30f ? 0.0f : v; }
    static float coefForMs(float ms, double sr)
    {
        return 1.0f - (float)std::exp(-1.0 / (std::max(0.05f, ms) * 0.001 * sr));
    }

    // Per-lane mutable state: everything that carries memory sample-to-sample.
    // process() uses mLaneL ONLY (bit-exact to the pre-stereo block); processStereo
    // runs both lanes fully independently from the SAME shared coefficients (Cfg).
    struct Lane
    {
        Svf svf;
        Biquad inHp, inShelf, outHp;
        std::vector<float> work;
        float env = 0.0f, lag = 0.0f;
        double u1 = 0.0, u2 = 0.0; // Boost ADAA history
    };

    // Per-block shared config, derived once from the atomic params and identical
    // for both lanes (only the per-sample STATE in Lane differs between them).
    struct Cfg
    {
        int voice; bool up; int mode; bool boost; int adaa; float mix;
        float sensGain, baseCut, sweepOct, Q;
        float attCoef, relCoef, lagCoef, outLevel;
    };

    Cfg computeCfg()
    {
        const double sr = mSampleRate;
        const int voice = mVoice.load();
        const bool boost = mBoost.load() && voice == kQTron;

        const float inShelfHz = (voice == kQTron) ? 3500.0f : 4000.0f;
        const float inShelfDb = (voice == kQTron) ? -1.2f   : -0.7f;
        if (voice != mLastVoice)
        {
            const Biquad shelf = Biquad::highshelf(sr, inShelfHz, inShelfDb);
            mLaneL.inShelf.copyCoeffsFrom(shelf);
            mLaneR.inShelf.copyCoeffsFrom(shelf);
            mLastVoice = voice;
        }

        const float qMax = (voice == kQTron) ? 16.0f : 8.0f;
        const float relMs = (voice == kQTron) ? 250.0f : 150.0f;
        const float lagMs = (voice == kQTron) ? 25.0f  : 2.0f;

        Cfg c;
        c.voice = voice;
        c.up = mUp.load();
        c.mode = mMode.load();
        c.boost = boost;
        c.adaa = mAdaaOrder.load();
        c.mix = mMix.load();
        c.sensGain = std::pow(2.0f, mSensitivity.load() * 6.0f);
        c.baseCut = 80.0f * std::pow(2.0f, mRange.load() * 4.0f);
        c.sweepOct = 1.5f + mDepth.load() * 2.5f;
        c.Q = 0.7f * std::pow(qMax / 0.7f, mResonance.load());
        c.attCoef = coefForMs(mAttackMs.load(), sr);
        c.relCoef = coefForMs(relMs, sr);
        c.lagCoef = coefForMs(lagMs, sr);
        c.outLevel = boost ? 1.3f : 1.0f;
        return c;
    }

    // Process one lane in place. Byte-identical maths to the pre-stereo single-lane
    // body (mSvf/mInHp/mInShelf/mOutHp -> ln.svf/ln.inHp/ln.inShelf/ln.outHp), so
    // Lane L alone reproduces the pre-refactor mono output exactly.
    void processLane(Lane &ln, float *buf, int numSamples, const Cfg &cfg, float &lastCutInOut)
    {
        float *w = ln.work.data();
        for (int i = 0; i < numSamples; ++i) w[i] = buf[i];
        ln.inHp.process(w, numSamples);
        ln.inShelf.process(w, numSamples);

        float env = ln.env, lag = ln.lag;
        double u1 = ln.u1, u2 = ln.u2; // Boost soft-clip ADAA history
        float lastCut = lastCutInOut;

        for (int i = 0; i < numSamples; ++i)
        {
            const float x = w[i];

            const float driven = x * cfg.sensGain;
            const float det = (cfg.voice == kQTron) ? (driven > 0.0f ? driven : 0.0f)
                                                     : std::abs(driven);
            env += (det > env ? cfg.attCoef : cfg.relCoef) * (det - env);
            lag += cfg.lagCoef * (env - lag);

            const float env01 = lag > 1.0f ? 1.0f : lag;
            const float envEff = cfg.up ? env01 : (1.0f - env01);
            const float fc = cfg.baseCut * std::pow(2.0f, envEff * cfg.sweepOct);

            ln.svf.setCoeffs(fc, cfg.Q);
            // BOOST: anti-aliased cubic soft-clip of the preamp-driven signal.
            const double u = (double)x * kBoostDrive;
            double finD = (double)x;
            if (cfg.boost)
                finD = (cfg.adaa >= 2) ? sat::cubicADAA2(u, u1, u2)
                     : (cfg.adaa == 1) ? sat::cubicADAA1(u, u1)
                     : sat::cubF(u); // 0 = naive (for A/B)
            u2 = u1; u1 = u; // keep history fresh even when not boosting

            const Svf::Out o = ln.svf.tick((float)finD);
            const float wet = (cfg.mode == kLP) ? o.lp
                            : (cfg.mode == kHP) ? o.hp
                            : (cfg.mode == kMix) ? (0.5f * o.bp + 0.5f * (float)finD)
                            : o.bp;
            w[i] = wet;
            lastCut = fc;
        }

        ln.outHp.process(w, numSamples);
        if (cfg.outLevel != 1.0f)
            for (int i = 0; i < numSamples; ++i) w[i] *= cfg.outLevel;

        if (cfg.mix >= 1.0f)
            for (int i = 0; i < numSamples; ++i) buf[i] = w[i];
        else if (cfg.mix > 0.0f)
            for (int i = 0; i < numSamples; ++i) buf[i] = buf[i] * (1.0f - cfg.mix) + w[i] * cfg.mix;

        ln.svf.flushDenorms();
        ln.env = flush(env); ln.lag = flush(lag);
        ln.u1 = u1; ln.u2 = u2;
        lastCutInOut = lastCut;
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

    Lane mLaneL, mLaneR;
    int mLastVoice = -1;
    bool mStereoRunning = false; // was the last pass processStereo()? false -> next stereo block reseeds Lane R from Lane L
    std::atomic<float> mCutoffPub{200.0f};
    double mSampleRate = 48000.0;
    bool mPrepared = false;
};

} // namespace nam_rig
