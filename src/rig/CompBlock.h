#pragma once
// CompBlock — pedal-style compressor, mono, DAW rate, pre-amp.
//
// Scoped (June 2026) as the "front of amp" tool guitarists actually use:
// a Sustain knob + INSTANT AUTO-MAKEUP (computed from the knobs, not the signal,
// so it never drifts): a signal at the nominal playing level (kMakeupRefDb) passes
// at unity with the Level knob at 0 dB. Analog colour is baked in per voicing (no
// Character knob). Ratio (Clean/FET) and Release (Clean/FET/Opto) knobs are
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
//           grit, worse on low notes). See the OTA block in processLane().
//   Opto  : LA-2A optical. Gentle ~3:1, RMS detector, ~10 ms fixed attack, and NO
//           release knob -- a fixed two-stage program-dependent T4 release (fast
//           initial recovery then a long tail that lengthens the longer/harder it
//           compresses, the photocell "memory"), smooth tube warmth, minimal pump.
//   FET   : 1176-style punch. FET-fast attack (~20 us-0.8 ms), aggressive ratio,
//           harder knee, program-dependent ratio (creeps up after the transient)
//           + program-dependent release, bright Class-A/transformer sheen, and
//           odd+even grit that bites harder when slammed.
//
// Topology (feedforward, log-domain — Giannoulis/Massberg/Reiss style):
//   detector(peak|RMS, optional sidechain HPF) -> dB -> soft-knee gain computer
//   -> attack / program-dependent release smoother on the GR signal
//   -> apply gain reduction -> INSTANT AUTO-MAKEUP (a constant gain from the
//      curve: adds back the GR at kMakeupRefDb) -> Level trim -> voicing colour.
//
// Latency: zero (chain-bypass via compOn is safe). Verified by tests/comp_test.cpp.
//
// STEREO (pedalboard pool, added 2026-07-11): mono process() drives Lane L ONLY,
// bit-exact to the pre-stereo block. processStereo() runs Lane L and Lane R fully
// INDEPENDENTLY (own detector/GR-follower/colour state each, from the SAME shared
// config) — UNLINKED gain reduction, not a stereo-bus-linked compressor. Matches
// this pedalboard's existing precedent (Mod's decorrelated LFO phase, Delay's
// independent ping-pong lanes): Amp A/Amp B are usually deliberately different
// tone paths here, not a stereo pair needing a shared detector. On the mono->
// stereo rising edge Lane R is reseeded from Lane L's current state so it doesn't
// resume from a stale/frozen GR value left over from the last time it ran.

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
        // LA-2A has no release control (fixed program-dependent), so Opto is out.
        return m == Mode::Clean || m == Mode::FET;
    }
    // The Cali76-style Dry / parallel-compression blend is a FET-only control (for
    // now). Single source of truth, shared with the editor so the knob only shows
    // on the FET voicing.
    static bool dryBlendExposed(Mode m) { return m == Mode::FET; }

    // OTA (Dyna Comp) and Opto (LA-2A) have no hardware attack control (fixed
    // timing), so the Attack knob is shown only on Clean/FET, like ratio/release.
    static bool attackExposed(Mode m) { return m == Mode::Clean || m == Mode::FET; }

    // ---- parameters (thread-safe) ----
    void setSustain(float v01) { mSustain.store(v01); } // 0..1
    void setAttackMs(float v) { mAttackMs.store(v); }   // 1..50 ms (knob)
    void setReleaseMs(float v) { mReleaseMs.store(v); } // scales exposed voicings (releaseExposed)
    void setRatio(float v) { mRatio.store(v); }         // ratio knob (ratioExposed)
    void setLevelDb(float v) { mLevelDb.store(v); }     // -12..+12 dB trim on top of auto-makeup
    void setMode(int m) { mMode.store(m); }                // 0..3 (see Mode)
    void setDryBlend(float v01) { mDryBlend.store(v01); }   // 0..1 FET parallel dry mix

    // Clean ("pedal") character constants — also the back-compat curve defaults.
    static constexpr float kRatio = 6.0f;
    static constexpr float kKneeDb = 6.0f;
    static constexpr float kReleaseMs = 150.0f;

    // Auto-makeup is referenced to this nominal playing peak (calibrated dBFS):
    // a signal at this level passes at unity, so normal playing stays ~unity and
    // only sustain lifts the quieter tails. Set this to where you actually play
    // (read the IN meter): a hotter reference over-boosts everything below it,
    // since the full gain reduction is added back at this point. (Was 0 dBFS,
    // then -15 dBFS — both still sat above real playing level, so normal playing
    // came out well above input at Level 0; -22 dBFS puts the unity pivot at the
    // calibrated guitar peak. Tune by ear against the IN meter.)
    static constexpr float kMakeupRefDb = -22.0f;

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
        float charAmt; // baked-in analog colour amount (replaces the old Character
                       // knob): each voicing's authentic, always-on colour depth
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
                    80.0f, 400.0f, 0.6f, 1.20f, 0.02f, 0.00f, 0.00f, 0.50f, 0.85f};
        case Mode::Opto: // LA-2A T4 opto+tube: ~10 ms fixed attack, two-stage program-
                         // dependent release (fast ~60 ms initial -> long 1.5 s+ tail
                         // that lengthens the longer/harder it compresses, the T4
                         // "memory"); no release knob (releaseExposed excludes Opto);
                         // even-forward tube warmth + gentle output-transformer iron.
            return {3.5f, 12.0f, 0.60f, true, 0.0f, 0.6f, 10.0f,
                    80.0f, 1500.0f, 1.0f, 0.10f, 0.45f, 0.00f, 0.40f, 0.00f, 0.35f};
        case Mode::FET: // Cali76-style guitar 1176: FET-fast attack, clean/transparent,
                        // bright Class-A presence, program-dependent ratio + release.
                        // attackScale 0.016 + floor 0.02 ms maps the Attack knob to the
                        // real 20 us-0.8 ms FET range; release reaches toward the 1.1 s
                        // max. The Cali76 is a TRANSFORMERLESS discrete Class-A pedal, so
                        // iron + grit are pulled right back (clean, presence-forward, not
                        // the slammed-transformer rack sound); the top-end sheen is added
                        // in processLane(). driveTrack keeps a hint of bite only when slammed.
            return {12.0f, 3.0f, 0.45f, false, 60.0f, 0.016f, 0.02f,
                    50.0f, 380.0f, 0.55f, 0.12f, 0.10f, 0.45f, 0.12f, 0.00f, 0.35f};
        case Mode::Clean: // transparent VCA: no colour
        default:
            return {kRatio, kKneeDb, 0.60f, false, 0.0f, 1.0f, 1.0f,
                    kReleaseMs, kReleaseMs, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
        }
    }

    // 1176 authentic ratio detents; the FET Ratio knob snaps to the nearest.
    static float snapRatioFet(float r)
    {
        const float d[4] = {4.0f, 8.0f, 12.0f, 20.0f};
        float best = d[0], bd = std::abs(r - d[0]);
        for (int i = 1; i < 4; ++i) { const float e = std::abs(r - d[i]); if (e < bd) { bd = e; best = d[i]; } }
        return best;
    }

    // Effective attack time (ms) for a voicing + Attack-knob value. OTA/Opto have no
    // hardware attack knob, so they use a fixed reference position (their timing is
    // baked into attackScale/floor). Shared by process() and the UI readout so the
    // number shown is always the real one.
    static float effectiveAttackMs(Mode m, float knobMs)
    {
        const Voicing v = voicingFor(m);
        const float k = attackExposed(m) ? knobMs : 15.0f;
        return std::max(v.attackFloorMs, k * v.attackScale);
    }

    // Effective (nominal, low-GR) release time (ms). FET is remapped onto the real
    // 1176 window (50 ms..1.1 s) across the whole knob travel; the others scale the
    // voicing release by the knob. Shared by process() and the UI readout.
    static float effectiveReleaseMs(Mode m, float knobMs)
    {
        if (m == Mode::FET)
        {
            const float rn = std::min(1.0f, std::max(0.0f, (knobMs - 20.0f) / 780.0f));
            return 50.0f + rn * (1100.0f - 50.0f);
        }
        const Voicing v = voicingFor(m);
        const float relScale = releaseExposed(m) ? std::max(0.05f, knobMs / kReleaseMs) : 1.0f;
        return v.relSlowMs * relScale;
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
        mLaneL = Lane{};
        mLaneR = Lane{};
        mStereoRunning = false; // next processStereo() is a fresh rising edge
    }

    // MONO: drive Lane L ONLY -> bit-exact to the pre-stereo block.
    void process(float *mono, int numSamples) override
    {
        if (!mPrepared)
            return;
        mStereoRunning = false; // record that the last pass was mono (only Lane L advanced)
        const Cfg cfg = computeCfg();
        float gr, inDb, outDb;
        processLane(mLaneL, mono, numSamples, cfg, gr, inDb, outDb);
        mGrDbPub.store(gr); // published for the editor's GR meter
        mInPeakDbPub.store(inDb);
        mOutPeakDbPub.store(outDb);
    }

    // Mono-in / stereo-out front pedal (pedalboard Stereo span): L -> Amp A, R ->
    // Amp B, each lane fully independent (own detector/GR-follower/colour state) —
    // UNLINKED gain reduction. See the class comment for why, and for the mono->
    // stereo reseed. Meter publishes reflect Lane L only (single-channel readout).
    void processStereo(float *L, float *R, int numSamples)
    {
        if (!mPrepared)
            return;
        if (!mStereoRunning) { mLaneR = mLaneL; mStereoRunning = true; }
        const Cfg cfg = computeCfg();
        float grL, inL, outL, grR, inR, outR;
        processLane(mLaneL, L, numSamples, cfg, grL, inL, outL);
        processLane(mLaneR, R, numSamples, cfg, grR, inR, outR);
        mGrDbPub.store(grL);
        mInPeakDbPub.store(inL);
        mOutPeakDbPub.store(outL);
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

    // Per-lane mutable state: everything that carries memory sample-to-sample.
    // process() uses mLaneL ONLY (bit-exact to the pre-stereo block); processStereo
    // runs both lanes fully independently from the SAME shared config (Cfg).
    struct Lane
    {
        float grDb = 0.0f;
        float relMem = 0.0f;  // slow follower of GR (duration memory)
        float scHp = 0.0f;    // sidechain HPF state
        float rms2 = 0.0f;    // RMS detector state
        float ironLpf = 0.0f; // transformer low-band state
        float ripCap = 0.0f;  // OTA control-ripple: rectified cap follower
        float ripMean = 0.0f; // OTA control-ripple: slow mean
        float fetHf = 0.0f;   // FET HF-sheen shelf state
    };

    // Per-block shared config, derived once from the atomic params + voicing and
    // identical for both lanes (only the per-sample STATE in Lane differs).
    struct Cfg
    {
        Mode mode; Voicing v;
        float t, ratio, relFastMs, relSlowMs, outLin, attCoef;
        bool simpleRelease; float relCoefConst, relMemCoef;
        bool scOn; float scCoef; bool useRms; float rmsCoef;
        float ch, ironCoef;
        bool otaMode, otaCell; float otaDrv, otaBias, otaTanhBias, otaInvDrv, ripCapCoef, ripMeanCoef;
        bool fetMode; float fetRatioProg, fetBright, fetHfCoef;
        float dryBlend;
    };

    Cfg computeCfg() const
    {
        const double sr = mSampleRate;
        const Mode mode = (Mode)mMode.load();
        const Voicing v = voicingFor(mode);

        const float t = thresholdForSustain(mSustain.load());

        // Ratio: user knob on the voicings that expose it, else the fixed voicing
        // character. Release: a ms knob that scales the voicing's release times
        // (relScale == 1 reproduces the tuned default at kReleaseMs) on the
        // exposed voicings; untouched voicings keep their built-in timing.
        float ratio = ratioExposed(mode) ? std::max(1.0f, mRatio.load()) : v.ratio;
        if (mode == Mode::FET) ratio = snapRatioFet(ratio); // 1176 detents 4/8/12/20
        const float relScale = releaseExposed(mode)
                                   ? std::max(0.05f, mReleaseMs.load() / kReleaseMs)
                                   : 1.0f;
        float relFastMs = v.relFastMs * relScale;
        float relSlowMs = v.relSlowMs * relScale;
        if (mode == Mode::FET)
        {
            // Remap the shared Release knob onto the real 1176 window (50 ms..1.1 s)
            // across the whole travel, keeping the voicing's fast:slow proportion.
            relSlowMs = effectiveReleaseMs(Mode::FET, mReleaseMs.load());
            relFastMs = relSlowMs * (v.relFastMs / v.relSlowMs);
        }

        // Instant auto-makeup: a CONSTANT gain (per block) that adds back the gain
        // reduction the static curve applies at the nominal playing level
        // (kMakeupRefDb), so a signal at that level passes at unity and only the
        // quieter tails are lifted. It is a function of the knobs only (threshold +
        // ratio + knee), NOT the signal, so it never drifts while you play. Level
        // trims on top.
        const float makeupDb = -computeGainDb(kMakeupRefDb, t, ratio, v.kneeDb);
        const float outLin = std::pow(10.0f, (makeupDb + mLevelDb.load()) * 0.05f);

        const float attMs = effectiveAttackMs(mode, mAttackMs.load());
        const float attCoef = coefForMs(attMs, sr);

        const bool simpleRelease = (v.relFastMs == v.relSlowMs && v.progDepth == 0.0f);
        const float relCoefConst = coefForMs(relSlowMs, sr);
        const float relMemCoef = coefForMs(150.0f, sr); // duration-memory follower

        const bool scOn = v.scHpfHz > 0.0f;
        const float scCoef = scOn ? coefForHz(v.scHpfHz, sr) : 0.0f;
        const bool useRms = v.useRms;
        const float rmsCoef = coefForMs(5.0f, sr);

        const float ch = v.charAmt;                     // baked-in colour (knob removed)
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

        // ---- FET (1176) authenticity: program-dependent ratio + transformer sheen ----
        const bool fetMode = (mode == Mode::FET);
        const float fetRatioProg = fetMode ? 0.5f : 0.0f; // ratio creep after transient
        const float fetBright = fetMode ? 0.12f : 0.0f;   // HF-sheen depth (0 = off)
        const float fetHfCoef = coefForHz(3500.0, sr);    // sheen shelf corner

        // Cali76 Dry / parallel-compression blend (FET only).
        const float dryBlend = fetMode ? std::min(1.0f, std::max(0.0f, mDryBlend.load())) : 0.0f;

        Cfg c;
        c.mode = mode; c.v = v;
        c.t = t; c.ratio = ratio; c.relFastMs = relFastMs; c.relSlowMs = relSlowMs;
        c.outLin = outLin; c.attCoef = attCoef;
        c.simpleRelease = simpleRelease; c.relCoefConst = relCoefConst; c.relMemCoef = relMemCoef;
        c.scOn = scOn; c.scCoef = scCoef; c.useRms = useRms; c.rmsCoef = rmsCoef;
        c.ch = ch; c.ironCoef = ironCoef;
        c.otaMode = otaMode; c.otaCell = otaCell; c.otaDrv = otaDrv; c.otaBias = otaBias;
        c.otaTanhBias = otaTanhBias; c.otaInvDrv = otaInvDrv; c.ripCapCoef = ripCapCoef; c.ripMeanCoef = ripMeanCoef;
        c.fetMode = fetMode; c.fetRatioProg = fetRatioProg; c.fetBright = fetBright; c.fetHfCoef = fetHfCoef;
        c.dryBlend = dryBlend;
        return c;
    }

    // Process one lane in place. Byte-identical maths to the pre-stereo single-lane
    // body (mGrDb/mRelMem/... -> ln.grDb/ln.relMem/...), so Lane L alone reproduces
    // the pre-refactor mono output exactly.
    void processLane(Lane &ln, float *buf, int numSamples, const Cfg &cfg,
                      float &grPubOut, float &inPkDbOut, float &outPkDbOut)
    {
        float gr = ln.grDb, relMem = ln.relMem, scHp = ln.scHp, rms2 = ln.rms2, ironLpf = ln.ironLpf;
        float ripCap = ln.ripCap, ripMean = ln.ripMean; // OTA control-ripple detector state
        float fetHf = ln.fetHf;                          // FET HF-sheen shelf state
        float inPk = 0.0f, outPk = 0.0f; // block peaks for the IN/OUT meter modes

        for (int i = 0; i < numSamples; ++i)
        {
            const float x = buf[i];
            const float ax = std::abs(x);
            if (ax > inPk)
                inPk = ax;

            // ---- detector ----
            float xdet = x;
            if (cfg.scOn)
            {
                scHp += cfg.scCoef * (x - scHp);
                xdet = x - scHp; // high-passed: ignore bass when judging level
            }
            float level;
            if (cfg.useRms)
            {
                rms2 += cfg.rmsCoef * (xdet * xdet - rms2);
                level = std::sqrt(std::max(rms2, 1.0e-18f));
            }
            else
                level = std::abs(xdet);
            const float aDb = 20.0f * std::log10(std::max(level, 1.0e-9f));

            // FET: program-dependent ratio — creeps up as compression sustains (relMem).
            const float ratEff = cfg.fetMode
                ? cfg.ratio * (1.0f + cfg.fetRatioProg * std::min(1.0f, relMem * (1.0f / kProgRefDb)))
                : cfg.ratio;
            const float grTarget = -computeGainDb(aDb, cfg.t, ratEff, cfg.v.kneeDb); // >= 0

            // ---- attack / program-dependent release smoother ----
            if (grTarget > gr)
                gr += cfg.attCoef * (grTarget - gr);
            else
            {
                float relCoef;
                if (cfg.simpleRelease)
                    relCoef = cfg.relCoefConst;
                else
                {
                    // high GR -> fast recovery; low GR -> slow tail (optical feel)
                    const float b = std::min(1.0f, std::max(0.0f, gr / kRelRefDb));
                    float relMs = cfg.relSlowMs + (cfg.relFastMs - cfg.relSlowMs) * b;
                    // the longer it's been compressing, the longer the release
                    relMs *= (1.0f + cfg.v.progDepth * (relMem / kProgRefDb));
                    if (cfg.fetMode) relMs = std::min(relMs, 1100.0f); // 1176 max release
                    relCoef = coefForMs(relMs, mSampleRate);
                }
                gr += relCoef * (grTarget - gr);
            }
            if (!cfg.simpleRelease)
                relMem += cfg.relMemCoef * (gr - relMem);

            // ---- gain reduction + instant makeup + Level (one constant gain) ----
            const float grLin = (gr < 1.0e-4f) ? 1.0f : std::pow(10.0f, -gr * 0.05f);

            // ---- OTA (CA3080) gain cell: pre-gain grit + control-ripple IMD ----
            float src = x;
            float grLinEff = grLin;
            if (cfg.otaCell)
            {
                src = (std::tanh((x + cfg.otaBias) * cfg.otaDrv) - cfg.otaTanhBias) * cfg.otaInvDrv;

                ripCap += cfg.ripCapCoef * (ax - ripCap);      // rectified follower (the cap)
                ripMean += cfg.ripMeanCoef * (ripCap - ripMean); // slow mean it rides on
                const float rip = (ripMean > 1.0e-6f) ? (ripCap - ripMean) / ripMean : 0.0f;
                // ripple bites harder the more the cell is working (low I_abc).
                const float ripAmt = cfg.ch * cfg.v.ripple * (0.25f + 0.75f * std::min(1.0f, gr * (1.0f / 8.0f)));
                grLinEff = grLin * (1.0f + ripAmt * rip);
            }

            float y = src * grLinEff * cfg.outLin;

            // ---- analog colour for the post-gain voicings (Opto/FET) ----
            // (OTA is handled by the pre-gain cell above; Clean has no colour.)
            if (!cfg.otaMode && cfg.ch > 0.0f && (cfg.v.drive > 0.0f || cfg.v.iron > 0.0f))
            {
                if (cfg.v.drive > 0.0f)
                {
                    const float drv = cfg.ch * cfg.v.drive * (1.0f + cfg.v.driveTrack * gr * (1.0f / 12.0f));
                    const float k = 1.0f + drv * 6.0f;
                    const float b = cfg.v.driveAsym * drv * 3.0f;
                    y = (std::tanh((y + b) * k) - std::tanh(b * k)) / k;
                }
                if (cfg.v.iron > 0.0f)
                {
                    ironLpf += cfg.ironCoef * (y - ironLpf);
                    const float flux = ironLpf * 2.0f;
                    y += (cfg.ch * cfg.v.iron) * (std::tanh(flux) - flux) * 0.5f;
                }
                if (cfg.fetBright > 0.0f)
                {
                    fetHf += cfg.fetHfCoef * (y - fetHf);
                    y += cfg.fetBright * (y - fetHf);
                }
            }

            // FET parallel dry blend: see class comment above process().
            const float out = (y + cfg.dryBlend * cfg.outLin * x) / (1.0f + cfg.dryBlend);
            buf[i] = out;
            const float ay = std::abs(out);
            if (ay > outPk)
                outPk = ay;
        }

        ln.grDb = (gr < 1.0e-7f) ? 0.0f : gr; // flush
        ln.relMem = (relMem < 1.0e-7f) ? 0.0f : relMem;
        ln.scHp = flush(scHp);
        ln.rms2 = flush(rms2);
        ln.ironLpf = flush(ironLpf);
        ln.ripCap = flush(ripCap);
        ln.ripMean = flush(ripMean);
        ln.fetHf = flush(fetHf);
        grPubOut = ln.grDb;
        // Block peaks in dBFS for the IN/OUT meter modes (UI thread reads these).
        inPkDbOut = inPk > 1.0e-9f ? 20.0f * std::log10(inPk) : -120.0f;
        outPkDbOut = outPk > 1.0e-9f ? 20.0f * std::log10(outPk) : -120.0f;
    }

    std::atomic<float> mSustain{0.5f};
    std::atomic<float> mAttackMs{15.0f};
    std::atomic<float> mReleaseMs{150.0f}; // release knob (ms); scales exposed voicings
    std::atomic<float> mRatio{4.0f};       // ratio knob (Clean/FET)
    std::atomic<float> mLevelDb{0.0f};
    std::atomic<float> mDryBlend{0.0f}; // FET parallel dry-blend (0..1)
    std::atomic<int> mMode{0};          // Clean

    Lane mLaneL, mLaneR;
    bool mStereoRunning = false; // was the last pass processStereo()? false -> next stereo block reseeds Lane R from Lane L
    double mSampleRate = 48000.0;
    bool mPrepared = false;
};

} // namespace nam_rig
