#pragma once
// CabDriveCal — one-shot calibration of the Dynamic Cab's level DETECTORS to a
// loaded amp's real pre-cab output level.
//
// CabDynamicsBlock's physics sidechain (excursion / push / thermal knees) is
// tuned to a fixed internal PRE-cab level (kRefPreCabDbFS, see
// CabDynamicsBlock::setSpeakerDriveDb). A capture chain that runs hotter or
// quieter than that reference silently mis-scales the whole level-dependent
// breakup: a quiet capture never reaches the breakup knee, a hot one idles past
// it. Output normalization can't fix this — it sits AFTER the cab, downstream of
// the detectors. The fix is the SpeakerDrive trim, which re-scales ONLY what the
// model thinks the level is (never the audio), so it corrects breakup with zero
// tonal side-effect.
//
// This computes that trim (dB) from a measured pre-cab RMS. Pure/static so the
// processor and tests/cabdrive_test.cpp share the SAME code (idiom from CalNorm
// / RoutingMath). It is a calibration, not loudness compensation and not a tone
// control; the caller runs it ONCE (a button / on demand), never per block.

#include <cmath>

namespace nam_rig
{

struct CabDriveCal
{
    // Internal pre-cab RMS the CabDynamicsBlock physics knees are calibrated to
    // (see CabDynamicsBlock::setSpeakerDriveDb doc: "~ -14 dBFS RMS"). This is
    // the SINGLE tuning constant. If the whole amp set reads biased by ear, move
    // it; the RELATIVE match between amps is independent of its exact value, so
    // amps still line up with each other either way. Validation: run the cal on
    // the amp the Dynamic Cab was voiced against — SpeakerDrive should read ~0.
    static constexpr float kRefPreCabDbFS = -14.0f;

    // SpeakerDrive param range (must match the cabDynSpkrDrive /
    // rigBcabDynSpkrDrive AudioParameterFloat range in PluginProcessor).
    static constexpr float kMaxTrimDb = 18.0f;

    // SpeakerDrive trim (dB) that re-anchors the detectors for a measured pre-cab
    // RMS (linear, 0..). Quiet capture (below ref) -> positive trim (detectors
    // scaled up); hot capture -> negative. Returns 0 dB for a silent/invalid
    // measurement (nothing meaningful to calibrate -> leave detectors as-is).
    static float speakerDriveDb(double preCabRmsLinear)
    {
        if (!(preCabRmsLinear > 1.0e-9))
            return 0.0f;
        const float measuredDbFS = (float)(20.0 * std::log10(preCabRmsLinear));
        float trim = kRefPreCabDbFS - measuredDbFS;
        if (trim < -kMaxTrimDb) trim = -kMaxTrimDb;
        if (trim >  kMaxTrimDb) trim =  kMaxTrimDb;
        return trim;
    }
};

} // namespace nam_rig
