// cal_test — verification for NAM-AA-parity input calibration + output
// normalization (src/CalNorm.h, wired in PluginProcessor::processBlock).
// Exits nonzero on any FAIL.
//
// T1 CalNorm formulas: unity when disabled / no metadata; correct deltas + sign
// T2 end-to-end: a synthetic A2 .nam carrying input_level_dbu + loudness loads
//    through the SAME AaEngine the plugin runs; its metadata accessors feed
//    CalNorm and produce the expected dB corrections
// T3 a model WITHOUT those metadata fields yields unity corrections
// T4 the corrections fold into linear in/out gains exactly (processBlock math)
// T5 input cal splits into a global pre-amp stage + per-rig residual (sum == net)

#include "CalNorm.h"
#include "AaEngine.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>
#include <cstdio>
#include <filesystem>

using nam_rig::CalNorm;

static int gFails = 0;
#define CHECK(cond, ...)                                                        \
    do                                                                          \
    {                                                                           \
        const bool chkOk_ = (cond);                                             \
        std::printf("%s: ", chkOk_ ? "PASS" : "FAIL");                          \
        std::printf(__VA_ARGS__);                                               \
        std::printf("\n");                                                      \
        if (!chkOk_)                                                            \
            ++gFails;                                                           \
    } while (0)

static bool approx(float a, float b, float tol = 1.0e-4f)
{
    return std::fabs(a - b) <= tol;
}

int main(int argc, char **argv)
{
    // argv[1] = model WITH metadata, argv[2] = model WITHOUT metadata.
    const char *modelMeta = argc > 1 ? argv[1] : "with_meta.nam";
    const char *modelPlain = argc > 2 ? argv[2] : "no_meta.nam";

    // Injected metadata (see the python generator step in the build script).
    const float kModelDbu = 14.5f;  // input_level_dbu in with_meta.nam
    const float kModelLoud = -7.3f; // loudness in with_meta.nam
    const float kUserDbu = 12.0f;   // calDbu default

    // ---- T1: pure formula contract ----
    CHECK(CalNorm::calibrationGainDb(false, true, kUserDbu, kModelDbu) == 0.0f,
          "T1 cal disabled -> 0 dB");
    CHECK(CalNorm::calibrationGainDb(true, false, kUserDbu, kModelDbu) == 0.0f,
          "T1 cal no-metadata -> 0 dB");
    CHECK(approx(CalNorm::calibrationGainDb(true, true, kUserDbu, kModelDbu),
                 kUserDbu - kModelDbu),
          "T1 cal enabled -> user-model = %.2f dB", kUserDbu - kModelDbu);
    CHECK(CalNorm::normalizationGainDb(false, true, kModelLoud) == 0.0f,
          "T1 norm disabled -> 0 dB");
    CHECK(CalNorm::normalizationGainDb(true, false, kModelLoud) == 0.0f,
          "T1 norm no-metadata -> 0 dB");
    CHECK(approx(CalNorm::normalizationGainDb(true, true, kModelLoud),
                 CalNorm::kTargetLoudnessDb - kModelLoud),
          "T1 norm enabled -> -18-loudness = %.2f dB",
          CalNorm::kTargetLoudnessDb - kModelLoud);

    // ---- T2: real metadata path through the engine the plugin runs ----
    nam_aa::AaEngine eng;
    eng.prepare(48000.0, 512);
    const auto info = eng.loadModel(std::filesystem::path(modelMeta));
    CHECK(info.ok, "T2 model-with-metadata loads (%s)", info.ok ? "ok" : info.error.c_str());
    CHECK(info.isA2, "T2 model is A2");
    CHECK(eng.hasInputLevelDbu(), "T2 engine sees input_level_dbu");
    CHECK(approx(eng.inputLevelDbu(), kModelDbu),
          "T2 input_level_dbu = %.2f (got %.2f)", kModelDbu, eng.inputLevelDbu());
    CHECK(eng.hasLoudness(), "T2 engine sees loudness");
    CHECK(approx(eng.loudnessDb(), kModelLoud),
          "T2 loudness = %.2f (got %.2f)", kModelLoud, eng.loudnessDb());

    const float calDb = CalNorm::calibrationGainDb(true, eng.hasInputLevelDbu(),
                                                   kUserDbu, eng.inputLevelDbu());
    const float normDb = CalNorm::normalizationGainDb(true, eng.hasLoudness(),
                                                      eng.loudnessDb());
    CHECK(approx(calDb, kUserDbu - kModelDbu),
          "T2 end-to-end cal = %.2f dB", calDb);
    CHECK(approx(normDb, CalNorm::kTargetLoudnessDb - kModelLoud),
          "T2 end-to-end norm = %.2f dB", normDb);

    // ---- T3: a model without the metadata fields -> unity corrections ----
    nam_aa::AaEngine eng2;
    eng2.prepare(48000.0, 512);
    const auto info2 = eng2.loadModel(std::filesystem::path(modelPlain));
    CHECK(info2.ok, "T3 plain model loads");
    CHECK(!eng2.hasInputLevelDbu(), "T3 no input_level_dbu metadata");
    CHECK(!eng2.hasLoudness(), "T3 no loudness metadata");
    CHECK(CalNorm::calibrationGainDb(true, eng2.hasInputLevelDbu(), kUserDbu,
                                     eng2.inputLevelDbu()) == 0.0f,
          "T3 cal unity without metadata");
    CHECK(CalNorm::normalizationGainDb(true, eng2.hasLoudness(),
                                       eng2.loudnessDb()) == 0.0f,
          "T3 norm unity without metadata");

    // ---- T4: corrections fold into linear gains exactly (processBlock math) ----
    // processBlock: inGain = dB(userIn + cal); outGain = dB(userOut + norm).
    const float userIn = -3.0f, userOut = 2.0f;
    const float inGain = juce::Decibels::decibelsToGain(userIn + calDb);
    const float outGain = juce::Decibels::decibelsToGain(userOut + normDb);
    CHECK(approx(inGain, juce::Decibels::decibelsToGain(userIn + (kUserDbu - kModelDbu)), 1e-5f),
          "T4 input gain folds calibration");
    CHECK(approx(outGain,
                 juce::Decibels::decibelsToGain(userOut + (CalNorm::kTargetLoudnessDb - kModelLoud)),
                 1e-5f),
          "T4 output gain folds normalization");
    // A DC buffer scaled by inGain lands at exactly the expected level.
    juce::AudioBuffer<float> buf(1, 64);
    for (int i = 0; i < 64; ++i)
        buf.setSample(0, i, 0.5f);
    buf.applyGain(inGain);
    CHECK(approx(buf.getSample(0, 0), 0.5f * inGain, 1e-6f),
          "T4 buffer scaled by folded input gain");

    // ---- T5: input cal splits into a GLOBAL pre-amp stage + per-rig residual
    //          that sum back to the net. The global stage feeds the drive rack /
    //          gate / comp; the residual goes into each amp so its NET cal (and
    //          therefore tone) is unchanged. ----
    {
        const float g = CalNorm::globalCalibrationGainDb(true, kUserDbu);
        const float net = CalNorm::calibrationGainDb(true, true, kUserDbu, kModelDbu);
        const float residual = net - g;
        CHECK(approx(g + residual, net, 1e-5f), "T5 global + residual == net cal");
        CHECK(approx(g, kUserDbu - CalNorm::kReferenceDbu, 1e-5f),
              "T5 global = userDbu - reference (drives see this)");
        CHECK(approx(residual, CalNorm::kReferenceDbu - kModelDbu, 1e-5f),
              "T5 residual = reference - modelDbu (into amp)");
        CHECK(CalNorm::globalCalibrationGainDb(true, CalNorm::kReferenceDbu) == 0.0f,
              "T5 default calDbu == reference -> global 0 dB (no change)");
        const float netNo = CalNorm::calibrationGainDb(true, false, kUserDbu, kModelDbu);
        CHECK(netNo == 0.0f, "T5 no-metadata amp net cal 0 dB");
        CHECK(approx(g + (netNo - g), 0.0f, 1e-5f),
              "T5 no-metadata: residual cancels global at the amp (drives still trimmed)");
    }

    std::printf("\n%s (%d failure%s)\n", gFails == 0 ? "ALL PASS" : "FAILURES",
                gFails, gFails == 1 ? "" : "s");
    return gFails == 0 ? 0 : 1;
}
