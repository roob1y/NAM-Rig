#pragma once
// CompBlock — pedal-style compressor + clean boost, mono, DAW rate, pre-amp.
//
// Scoped (June 2026) as the "front of amp" tool guitarists actually use:
// one Sustain knob (not threshold/ratio) + a clean Boost to push the amp model.
//
// FOUR VOICINGS (compMode), each reinterpreting the same four knobs:
//   Clean : transparent VCA leveling. The original voicing — DEFAULT, and the
//           Clean path reduces to the pre-modes math bit-for-bit so existing
//           presets/automation and the SoloA regression are unchanged.
//   OTA   : Ross/Dyna-style squish. Higher ratio, soft knee, mid-forward
//           sidechain (bass doesn't trigger -> "pop"), program-dependent
//           release, gentle OTA warmth.
//   Opto  : optical smoothness. Gentle ratio, RMS detector, slow dual-stage
//           program-dependent release (relaxes on sustained notes), minimal
//           pumping/colour.
//   FET   : 1176-style punch. Ultra-fast attack, aggressive ratio, harder knee,
//           odd-harmonic grit that bites harder when slammed.
//
// Topology (feedforward, log-domain — Giannoulis/Massberg/Reiss style):
//   detector(peak|RMS, optional sidechain HPF) -> dB -> soft-knee gain computer
//   -> attack / program-dependent release smoother on the GR signal
//   -> gain = makeup + level - GR -> voicing colour -> boost.
//
// Latency: zero (chain-bypass via compOn is safe). Verified by tests/comp_test.cpp.

#include "Blocks.h"
#include <atomic>
#include <cmath>

namespace nam_rig
{

class CompBlock : public MonoBlock
{
public:
    const char *name() const override { return "Comp/Boost"; }

    enum class Mode { Clean = 0, OTA = 1, Opto = 2, FET = 3 };

    // ---- parameters (thread-safe) ----
    void setSustain(float v01) { mSustain.store(v01); } // 0..1
    void setAttackMs(float v) { mAttackMs.store(v); }   // 1..50 ms (knob)
    void setLevelDb(float v) { mLevelDb.store(v); }     // -12..+12 dB trim
    void setBoostDb(float v) { mBoostDb.store(v); }     // 0..+20 dB clean boost
    void setMode(int m) { mMode.store(m); }                // 0..3 (see Mode)
    void setCharacter(float v01) { mCharacter.store(v01); } // 0..1 analog colour amount

    // Clean ("pedal") character constants — also the back-compat curve defaults.
    static constexpr float kRatio = 6.0f;
    static constexpr float kKneeDb = 6.0f;
    static constexpr float kReleaseMs = 150.0f;

    // Release model references.
    static constexpr float kRelRefDb = 6.0f;   // GR at which release hits relFast
    static constexpr float kProgRefDb = 12.0f; // normalises the duration memory

    // Per-mode voicing constants. Static so process(), the tests and the UI
    // share one source of truth.
    struct Voicing
    {
        float ratio, kneeDb, makeupScale;
        bool useRms;
        float scHpfHz;                  // sidechain high-pass (0 = off)
        float attackScale, attackFloorMs;
        float relFastMs, relSlowMs, progDepth;
        float drive, driveAsym, driveTrack; // gain-cell colour: depth/even/GR-track
        float iron;                          // transformer saturation (0 = none)
    };

    static Voicing voicingFor(Mode m)
    {
        switch (m)
        {
        case Mode::OTA: // CA3080 grit: odd-forward, no transformer
            return {10.0f, 10.0f, 0.50f, false, 120.0f, 0.70f, 1.0f,
                    80.0f, 400.0f, 0.6f, 0.18f, 0.05f, 0.30f, 0.00f};
        case Mode::Opto: // optical + tube: even-forward warmth, gentle iron
            return {3.5f, 12.0f, 0.60f, true, 0.0f, 1.60f, 8.0f,
                    120.0f, 900.0f, 1.0f, 0.10f, 0.45f, 0.00f, 0.40f};
        case Mode::FET: // 1176: odd+even grit, strong transformer iron
            return {12.0f, 3.0f, 0.45f, false, 60.0f, 0.25f, 0.2f,
                    50.0f, 250.0f, 0.4f, 0.22f, 0.15f, 0.60f, 0.70f};
        case Mode::Clean: // transparent VCA: no colour
        default:
            return {kRatio, kKneeDb, 0.60f, false, 0.0f, 1.0f, 1.0f,
                    kReleaseMs, kReleaseMs, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
        }
    }

    // Soft-knee transfer (input dB -> gain dB, <= 0), generalised over ratio/knee.
    static float computeGainDb(float xDb, float thresholdDb, float ratio, float kneeDb)
    {
        const float over = xDb - thresholdDb;
        float yDb;
        if (over <= -kneeDb * 0.5f)
            yDb = xDb; // below knee: unity
        else if (over >= kneeDb * 0.5f)
            yDb = thresholdDb + over / ratio; // above knee: ratio
        else
        {
            const float k = over + kneeDb * 0.5f; // 0..knee
            yDb = xDb + (1.0f / ratio - 1.0f) * k * k / (2.0f * kneeDb);
        }
        return yDb - xDb;
    }
    // Back-compat (Clean voicing) — used by tests and the curve UI.
    static float computeGainDb(float xDb, float thresholdDb)
    {
        return computeGainDb(xDb, thresholdDb, kRatio, kKneeDb);
    }

    static float thresholdForSustain(float s01)
    {
        return -10.0f - 35.0f * std::min(std::max(s01, 0.0f), 1.0f);
    }
    static float makeupForThreshold(float tDb, float ratio, float scale)
    {
        return -tDb * (1.0f - 1.0f / ratio) * scale;
    }
    static float makeupForThreshold(float tDb) // Clean back-compat
    {
        return makeupForThreshold(tDb, kRatio, 0.60f);
    }

    void prepare(const BlockContext &ctx) override
    {
        mSampleRate = ctx.sampleRate;
        reset();
        mPrepared = true;
    }

    void reset() override
    {
        mGrDb = 0.0f;
        mRelMem = 0.0f;
        mScHp = 0.0f;
        mRms2 = 0.0f;
        mIronLpf = 0.0f;
    }

    void process(float *mono, int numSamples) override
    {
        if (!mPrepared)
            return;

        const double sr = mSampleRate;
        const Voicing v = voicingFor((Mode)mMode.load());

        const float t = thresholdForSustain(mSustain.load());
        const float makeup = makeupForThreshold(t, v.ratio, v.makeupScale);
        const float outDb = makeup + mLevelDb.load() + mBoostDb.load();
        const float outLin = std::pow(10.0f, outDb * 0.05f);

        const float attMs = std::max(v.attackFloorMs, mAttackMs.load() * v.attackScale);
        const float attCoef = coefForMs(attMs, sr);

        const bool simpleRelease = (v.relFastMs == v.relSlowMs && v.progDepth == 0.0f);
        const float relCoefConst = coefForMs(v.relSlowMs, sr);
        const float relMemCoef = coefForMs(150.0f, sr); // duration-memory follower

        const bool scOn = v.scHpfHz > 0.0f;
        const float scCoef = scOn ? coefForHz(v.scHpfHz, sr) : 0.0f;
        const bool useRms = v.useRms;
        const float rmsCoef = coefForMs(5.0f, sr);

        const float ch = mCharacter.load();             // analog colour amount
        const float ironCoef = coefForHz(400.0, sr);    // transformer low-band corner

        float gr = mGrDb, relMem = mRelMem, scHp = mScHp, rms2 = mRms2, ironLpf = mIronLpf;
        float inPk = 0.0f, outPk = 0.0f; // block peaks for the IN/OUT meter modes

        for (int i = 0; i < numSamples; ++i)
        {
            const float x = mono[i];
            const float ax = std::abs(x);
            if (ax > inPk)
                inPk = ax;

            // ---- detector ----
            float xdet = x;
            if (scOn)
            {
                scHp += scCoef * (x - scHp);
                xdet = x - scHp; // high-passed: ignore bass when judging level
            }
            float level;
            if (useRms)
            {
                rms2 += rmsCoef * (xdet * xdet - rms2);
                level = std::sqrt(std::max(rms2, 1.0e-18f));
            }
            else
                level = std::abs(xdet);
            const float aDb = 20.0f * std::log10(std::max(level, 1.0e-9f));

            const float grTarget = -computeGainDb(aDb, t, v.ratio, v.kneeDb); // >= 0

            // ---- attack / program-dependent release smoother ----
            if (grTarget > gr)
                gr += attCoef * (grTarget - gr);
            else
            {
                float relCoef;
                if (simpleRelease)
                    relCoef = relCoefConst;
                else
                {
                    // high GR -> fast recovery; low GR -> slow tail (optical feel)
                    const float b = std::min(1.0f, std::max(0.0f, gr / kRelRefDb));
                    float relMs = v.relSlowMs + (v.relFastMs - v.relSlowMs) * b;
                    // the longer it's been compressing, the longer the release
                    relMs *= (1.0f + v.progDepth * (relMem / kProgRefDb));
                    relCoef = coefForMs(relMs, sr);
                }
                gr += relCoef * (grTarget - gr);
            }
            if (!simpleRelease)
                relMem += relMemCoef * (gr - relMem);

            // ---- apply gain ----
            const float g = (gr < 1.0e-4f) ? outLin
                                           : outLin * std::pow(10.0f, -gr * 0.05f);
            float y = x * g;

            // ---- analog colour (scaled by Character; 0 = clean) ----
            if (ch > 0.0f && (v.drive > 0.0f || v.iron > 0.0f))
            {
                // gain cell: asymmetric soft saturation. k>1 = odd grit, the
                // DC bias b = even (tube/transformer) harmonics, both grow with
                // GR so it bites harder when slammed (driveTrack). /k keeps the
                // small-signal gain ~unity so gain staging is preserved.
                if (v.drive > 0.0f)
                {
                    const float drv = ch * v.drive * (1.0f + v.driveTrack * gr * (1.0f / 12.0f));
                    const float k = 1.0f + drv * 6.0f;
                    const float b = v.driveAsym * drv * 3.0f;
                    y = (std::tanh((y + b) * k) - std::tanh(b * k)) / k;
                }
                // transformer iron: low-frequency-weighted core saturation. Adds
                // low-order harmonics + thickness on bass/transients (the "big
                // iron" sound). Symmetric -> no DC. Opto/FET only.
                if (v.iron > 0.0f)
                {
                    ironLpf += ironCoef * (y - ironLpf);
                    const float flux = ironLpf * 2.0f;
                    y += (ch * v.iron) * (std::tanh(flux) - flux) * 0.5f;
                }
            }

            mono[i] = y;
            const float ay = std::abs(y);
            if (ay > outPk)
                outPk = ay;
        }

        mGrDb = (gr < 1.0e-7f) ? 0.0f : gr; // flush
        mRelMem = (relMem < 1.0e-7f) ? 0.0f : relMem;
        mScHp = flush(scHp);
        mRms2 = flush(rms2);
        mIronLpf = flush(ironLpf);
        mGrDbPub.store(mGrDb); // published for the editor's GR meter
        // Block peaks in dBFS for the IN/OUT meter modes (UI thread reads these).
        mInPeakDbPub.store(inPk > 1.0e-9f ? 20.0f * std::log10(inPk) : -120.0f);
        mOutPeakDbPub.store(outPk > 1.0e-9f ? 20.0f * std::log10(outPk) : -120.0f);
    }

    // Last block's gain reduction in dB (>= 0). UI thread.
    float grDb() const { return mGrDbPub.load(); }
    // Last block's input / output peak in dBFS (-120 = silence). UI thread.
    float inPeakDb() const { return mInPeakDbPub.load(); }
    float outPeakDb() const { return mOutPeakDbPub.load(); }

private:
    std::atomic<float> mGrDbPub{0.0f};
    std::atomic<float> mInPeakDbPub{-120.0f};
    std::atomic<float> mOutPeakDbPub{-120.0f};

    static float coefForMs(float ms, double sr)
    {
        return 1.0f - (float)std::exp(-1.0 / (std::max(0.01f, ms) * 0.001 * sr));
    }
    static float coefForHz(double hz, double sr)
    {
        return 1.0f - (float)std::exp(-2.0 * 3.14159265358979323846 * hz / sr);
    }
    static float flush(float v) { return std::abs(v) < 1.0e-30f ? 0.0f : v; }

    std::atomic<float> mSustain{0.5f};
    std::atomic<float> mAttackMs{15.0f};
    std::atomic<float> mLevelDb{0.0f};
    std::atomic<float> mBoostDb{0.0f};
    std::atomic<int> mMode{0};          // Clean
    std::atomic<float> mCharacter{0.0f}; // analog colour amount (param-driven)

    float mGrDb = 0.0f;
    float mRelMem = 0.0f;  // slow follower of GR (duration memory)
    float mScHp = 0.0f;    // sidechain HPF state
    float mRms2 = 0.0f;    // RMS detector state
    float mIronLpf = 0.0f; // transformer low-band state
    double mSampleRate = 48000.0;
    bool mPrepared = false;
};

} // namespace nam_rig
