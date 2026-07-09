#pragma once
// CalNorm — NAM-AA parity input calibration + output normalization.
//
// Two metadata-driven gain corrections, ported verbatim from the NAM-AA
// plugin so a model sounds identical in either:
//
//   * Input calibration: when the model carries an input_level_dbu (the dBu
//     the capture rig saw at 0 dBFS), driving the model at the user's own
//     interface clip point requires (userDbu - modelDbu) dB of input gain so
//     the model sees the same level it was captured at.
//
//   * Output normalization: levels every model to a common perceived loudness
//     (target -18 dB, matching the official plugin) from its loudness metadata.
//
// The formulas are pure static functions so the processor and the offline
// test (tests/cal_test.cpp) call the SAME code — no copy drift, the codebase
// idiom shared by RoutingMath / CompBlock. Both return 0 dB (unity) when the
// feature is disabled or the model lacks the metadata, so they are always safe
// to add unconditionally to the user's input/output gain.

namespace nam_rig
{

struct CalNorm
{
    // Same loudness target as the official NAM plugin.
    static constexpr float kTargetLoudnessDb = -18.0f;

    // Internal reference level (dBu) the SHARED pre-amp section (drive rack,
    // gate, comp) is voiced at. Equal to the calDbu default so default settings
    // change nothing. The per-amp calibration is preserved by splitting the
    // input correction into a global stage (userDbu - reference) feeding the
    // pre-amp section + a per-rig residual (reference - modelDbu) into each amp;
    // the two sum back to calibrationGainDb(rig), so amp tone is unchanged.
    static constexpr float kReferenceDbu = 12.0f;

    // Global input calibration (dB): trims the incoming signal to the reference
    // so the shared pre-amp stages get a consistent level. Model-independent;
    // 0 dB (unity) when calibration is disabled.
    static float globalCalibrationGainDb(bool enabled, float userDbu)
    {
        return enabled ? userDbu - kReferenceDbu : 0.0f;
    }

    // Input-gain correction (dB). 0 when disabled or the model has no
    // input_level_dbu metadata.
    static float calibrationGainDb(bool enabled, bool hasInputLevelDbu,
                                   float userDbu, float modelInputLevelDbu)
    {
        if (!enabled || !hasInputLevelDbu)
            return 0.0f;
        return userDbu - modelInputLevelDbu;
    }

    // Output-gain correction (dB). 0 when disabled or the model has no
    // loudness metadata.
    static float normalizationGainDb(bool enabled, bool hasLoudness,
                                     float modelLoudnessDb)
    {
        if (!enabled || !hasLoudness)
            return 0.0f;
        return kTargetLoudnessDb - modelLoudnessDb;
    }

    // Compensation (dB) that corrects output normalization for the input-
    // calibration drive, ADDED to normalizationGainDb at the out-trim.
    //
    // The loudness metadata is measured at the model's CAPTURE drive, but input
    // calibration re-drives the amp by calibrationGainDb before playback. For a
    // clean (near-linear) amp the real output shifts by that same amount, which
    // static normalization ignores — so a +cal model comes out louder than a
    // -cal model even with Normalize on (a +5 dB vs -7 dB cal pair sits ~12 dB
    // apart). Subtracting the calibration gain lands both back on the target.
    //
    // Returns 0 unless BOTH normalization and calibration are active with their
    // metadata, so it's a bit-exact no-op whenever either feature is off (the
    // default). Exact for clean amps; an approximation for heavily-driven models
    // (their nonlinear gain doesn't track the drive 1:1) — still far better than
    // ignoring the drive entirely, and Match Levels remains the exact per-pair
    // fallback.
    static float calibrationCompensationDb(bool normEnabled, bool hasLoudness,
                                           bool calEnabled, bool hasInputLevelDbu,
                                           float userDbu, float modelInputLevelDbu)
    {
        if (!normEnabled || !hasLoudness)
            return 0.0f;
        return -calibrationGainDb(calEnabled, hasInputLevelDbu, userDbu,
                                  modelInputLevelDbu);
    }
};

} // namespace nam_rig
