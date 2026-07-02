#pragma once
// CompBlock — pedal-style compressor, mono, DAW rate, pre-amp.
//
// Scoped (June 2026) as the "front of amp" tool guitarists actually use:
// a Sustain knob + INSTANT AUTO-MAKEUP (computed from the knobs, not the signal,
// so it never drifts): a full-scale peak passes at unity with the Level knob at
// 0 dB. Ratio (Clean/FET) and Release (Clean/FET/Opto) knobs are
// exposed on the voicings whose hardware has them; see ratioExposed/releaseExposed.
//
// FOUR VOICINGS (compMode), each reinterpreting the same four knobs:
//   Clean : transparent VCA leveling. The original voicing — DEFAULT, and the
//           Clean path reduces to the pre-modes math bit-for-bit so existing
//           presets/automation and the SoloA regression are unchanged.
//   OTA   : Ross/Dyna-style squish. Higher ratio, soft knee, mid-forward
//           sidechain (bass doesn't trigger -> "pop"), program-dependent
//           release, and a circuit-accurate CA3080 gain cell: the grit is a
//           PRE-gain transconductance tanh driven by the *calibrated input*
//           level (so it tracks how hard you play, not the makeup output),
//           plus control-ripple IMD (the signature Dyna Comp odd-harmonic
//           grit, worse on low notes). See the OTA block in process().
//   Opto  : optical smoothness. Gentle ratio, RMS detector, slow dual-stage
//           program-dependent release (relaxes on sustained notes), minimal
//           pumping/colour.
//   FET   : 1176-style punch. Ultra-fast attack, aggressive ratio, harder knee,
//           odd-harmonic grit that bites harder when slammed.
//
// Topology (feedforward, log-domain — Giannoulis/Massberg/Reiss style):
//   detector(peak|RMS, optional sidechain HPF) -> dB -> soft-knee gain computer
//   -> attack / program-dependent release smoother on the GR signal
//   -> apply gain reduction -> INSTANT AUTO-MAKEUP (a constant gain from the
//      curve: adds back the GR at 0 dBFS) -> Level trim -> voicing colour.
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

    // Which voicings expose the user Ratio / Release knobs (single source of
    // truth, shared with the editor). Modelled on the real hardware: the 1176
    // (FET) and a clean VCA have both; the LA-2A-style Opto keeps its program-
    // dependent release adjustable but stays fixed-ratio; the Dyna/Ross OTA is a
    // fixed one-knob squish (neither).
    static bool ratioExposed(Mode m) { return m == Mode::Clean || m == Mode::FET; }
    static bool releaseExposed(Mode m)
    {
        return m == Mode::Clean || m == Mode::FET || m == Mode::Opto;
    }

    // ---- parameters (thread-safe) ----
    void setSustain(float v01) { mSustain.store(v01); } // 0..1
    void setAttackMs(float v) { mAttackMs.store(v); }   // 1..50 ms (knob)
    void setReleaseMs(float v) { mReleaseMs.store(v); } // scales exposed voicings (releaseExposed)
    void setRatio(float v) { mRatio.store(v); }         // ratio knob (ratioExposed)
    void setLevelDb(float v) { mLevelDb.store(v); }     // -12..+12 dB trim on top of auto-makeup
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
        float ripple; // OTA control-ripple depth (0 = off): detector ripple that
                      // amplitude-modulates the gain -> odd-harmonic Dyna grit
    };

    static Voicing voicingFor(Mode m)
    {
        switch (m)
        {
        case Mode::OTA: // CA3080 gain cell: odd-forward, pre-gain, + control ripple.
                        // drive = tanh input-drive scale (NOT the old post-gain depth);
                        // driveAsym = small even bias; driveTrack unused (input drives
                        // the grit now); ripple = detector-ripple IMD depth.
            return {10.0f, 10.0f, 0.50f, false, 120.0f, 0.70f, 1.0f,
                    80.0f, 400.0f, 0.6f, 1.20f, 0.02f, 0.00f, 0.00f, 0.50f};
        case Mode::Opto: // optical + tube: even-forward warmth, gentle iron
            return {3.5f, 12.0f, 0.60f, true, 0.0f, 1.60f, 8.0f,
                    120.0f, 900.0f, 1.0f, 0.10f, 0.45f, 0.00f, 0.40f, 0.00f};
        case Mode::FET: // 1176: odd+even grit, strong transformer iron
            return {12.0f, 3.0f, 0.45f, false, 60.0f, 0.25f, 0.2f,
                    50.0f, 250.0f, 0.4f, 0.22f, 0.15f, 0.60f, 0.70f, 0.00f};
        case Mode::Clean: // transparent VCA: no colour
        default:
            return {kRatio, kKneeDb, 0.60f, false, 0.0f, 1.0f, 1.0f,
                    kReleaseMs, kReleaseMs, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
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
        mRipCap = 0.0f;
        mRipMean = 0.0f;
    }

    void process(float *mono, int numSamples) override
    {
        if (!mPrepared)
            return;

        const double sr = mSampleRate;
        const Mode mode = (Mode)mMode.load();
        const Voicing v = voicingFor(mode);

        const float t = thresholdForSustain(mSustain.load());

        // Ratio: user knob on the voicings that expose it, else the fixed voicing
        // character. Release: a ms knob that scales the voicing's release times
        // (relScale == 1 reproduces the tuned default at kReleaseMs) on the
        // exposed voicings; untouched voicings keep their built-in timing.
        const float ratio = ratioExposed(mode) ? std::max(1.0f, mRatio.load()) : v.ratio;
        const float relScale = releaseExposed(mode)
                                   ? std::max(0.05f, mReleaseMs.load() / kReleaseMs)
                                   : 1.0f;
        const float relFastMs = v.relFastMs * relScale;
        const float relSlowMs = v.relSlowMs * relScale;

        // Instant auto-makeup: a CONSTANT gain (per block) that adds back exactly
        // the gain reduction the static curve applies at full scale (0 dBFS), so a
        // 0 dBFS peak passes at unity and everything below is lifted the same way.
        // It is a function of the knobs only (threshold + ratio + knee), NOT the
        // signal, so it never drifts while you play. Level trims on top. No boost.
        const float makeupDb = -computeGainDb(0.0f, t, ratio, v.kneeDb);
        const float outLin = std::pow(10.0f, (makeupDb + mLevelDb.load()) * 0.05f);

        const float attMs = std::max(v.attackFloorMs, mAttackMs.load() * v.attackScale);
        const float attCoef = coefForMs(attMs, sr);

        const bool simpleRelease = (v.relFastMs == v.relSlowMs && v.progDepth == 0.0f);
        const float relCoefConst = coefForMs(relSlowMs, sr);
        const float relMemCoef = coefForMs(150.0f, sr); // duration-memory follower

        const bool scOn = v.scHpfHz > 0.0f;
        const float scCoef = scOn ? coefForHz(v.scHpfHz, sr) : 0.0f;
        const bool useRms = v.useRms;
        const float rmsCoef = coefForMs(5.0f, sr);

        const float ch = mCharacter.load();             // analog colour amount
        const float ironCoef = coefForHz(400.0, sr);    // transformer low-band corner

        // ---- OTA (CA3080) gain-cell precompute -------------------------------
        // The OTA voicing distorts on the *pre-gain* signal, which is already the
        // calibration-referenced level: RigChain applies the global input
        // calibration (to CalNorm::kReferenceDbu) BEFORE the comp, so shaping the
        // input here means the grit tracks the player's true, calibrated level and
        // is consistent across interfaces. The old post-makeup shaper was
        // calibration-blind (makeup had already normalised the level away).
        const bool otaMode = (mode == Mode::OTA);
        const bool otaCell = otaMode && ch > 0.0f && v.drive > 0.0f;
        const float otaDrv = 1.0f + ch * v.drive;        // tanh input drive
        const float otaBias = ch * v.driveAsym;          // small even-harmonic bias
        const float otaTanhBias = std::tanh(otaBias * otaDrv);
        const float otaInvDrv = 1.0f / otaDrv;           // small-signal gain -> ~unity
        const float ripCapCoef = coefForMs(2.0f, sr);    // detector cap (leaves 2f ripple)
        const float ripMeanCoef = coefForMs(40.0f, sr);  // slow mean the ripple rides on

        float gr = mGrDb, relMem = mRelMem, scHp = mScHp, rms2 = mRms2, ironLpf = mIronLpf;
        float ripCap = mRipCap, ripMean = mRipMean; // OTA control-ripple detector state
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

            const float grTarget = -computeGainDb(aDb, t, ratio, v.kneeDb); // >= 0

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
                    float relMs = relSlowMs + (relFastMs - relSlowMs) * b;
                    // the longer it's been compressing, the longer the release
                    relMs *= (1.0f + v.progDepth * (relMem / kProgRefDb));
                    relCoef = coefForMs(relMs, sr);
                }
                gr += relCoef * (grTarget - gr);
            }
            if (!simpleRelease)
                relMem += relMemCoef * (gr - relMem);

            // ---- gain reduction + instant makeup + Level (one constant gain) ----
            const float grLin = (gr < 1.0e-4f) ? 1.0f : std::pow(10.0f, -gr * 0.05f);

            // ---- OTA (CA3080) gain cell: pre-gain grit + control-ripple IMD ----
            // Real hardware: I_out = I_abc * tanh(V_in / 2Vt). Gain is I_abc (grLin);
            // distortion is the tanh acting on the attenuated INPUT, so harmonics
            // grow with how hard you actually hit it. The detector's finite
            // smoothing cap leaves ripple at 2x the signal freq on I_abc, which
            // amplitude-modulates the gain -> the signature odd-harmonic Dyna grit
            // (worse on low notes, where the 2f ripple is smoothed less).
            float src = x;
            float grLinEff = grLin;
            if (otaCell)
            {
                src = (std::tanh((x + otaBias) * otaDrv) - otaTanhBias) * otaInvDrv;

                ripCap += ripCapCoef * (ax - ripCap);      // rectified follower (the cap)
                ripMean += ripMeanCoef * (ripCap - ripMean); // slow mean it rides on
                const float rip = (ripMean > 1.0e-6f) ? (ripCap - ripMean) / ripMean : 0.0f;
                // ripple bites harder the more the cell is working (low I_abc).
                const float ripAmt = ch * v.ripple * (0.25f + 0.75f * std::min(1.0f, gr * (1.0f / 8.0f)));
                grLinEff = grLin * (1.0f + ripAmt * rip);
            }

            float y = src * grLinEff * outLin;

            // ---- analog colour for the post-gain voicings (Opto/FET) ----
            // (OTA is handled by the pre-gain cell above; Clean has no colour.)
            if (!otaMode && ch > 0.0f && (v.drive > 0.0f || v.iron > 0.0f))
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
        mRipCap = flush(ripCap);
        mRipMean = flush(ripMean);
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
    std::atomic<float> mReleaseMs{150.0f}; // release knob (ms); scales exposed voicings
    std::atomic<float> mRatio{4.0f};       // ratio knob (Clean/FET)
    std::atomic<float> mLevelDb{0.0f};
    std::atomic<int> mMode{0};          // Clean
    std::atomic<float> mCharacter{0.0f}; // analog colour amount (param-driven)

    float mGrDb = 0.0f;
    float mRelMem = 0.0f;  // slow follower of GR (duration memory)
    float mScHp = 0.0f;    // sidechain HPF state
    float mRms2 = 0.0f;    // RMS detector state
    float mIronLpf = 0.0f; // transformer low-band state
    float mRipCap = 0.0f;  // OTA control-ripple: rectified cap follower
    float mRipMean = 0.0f; // OTA control-ripple: slow mean
    double mSampleRate = 48000.0;
    bool mPrepared = false;
};

} // namespace nam_rig
