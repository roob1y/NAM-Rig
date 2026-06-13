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
};

} // namespace nam_rig
