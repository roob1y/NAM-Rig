#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "PresetManager.h"
#include "CalNorm.h"
#include <cmath>

juce::AudioProcessorValueTreeState::ParameterLayout NamRigProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    // 0..1 knobs that read best as a percentage (Tone, Mix, Mod, Shimmer, etc.).
    auto pct = [] {
        return juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction([](float v, int) { return juce::String(juce::roundToInt(v * 100.0f)) + "%"; })
            .withValueFromStringFunction([](const juce::String &t) { return t.getFloatValue() * 0.01f; });
    };

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("inputGain", 1), "Input Gain",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("outputGain", 1), "Output Gain",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    // Rig A amp AA oversampling (NAM-AA semantics). Rig B has its own
    // oversampleB / offlineAAB; the chain delay-compensates differing factors.
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID("oversample", 1), "Amp Oversampling",
        juce::StringArray{"Off", "2x", "4x", "8x", "16x", "32x"}, 0)); // default Off

    // Offline renders bump the amp to a high-rate model regardless of the live
    // setting (NAM-AA parity).
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID("offlineAA", 1), "Offline AA",
        juce::StringArray{"Same as live", "8x", "16x", "32x"}, 1)); // default 8x

    // Block bypasses (stub blocks are passthrough anyway; params reserved now
    // so automation indices stay stable as blocks gain DSP).
    for (const char *id : {"gateOn", "compOn", "eqOn", "cabOn", "modOn", "delayOn", "reverbOn"})
        params.push_back(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID(id, 1), juce::String(id).dropLastCharacters(2) + " Enable", true));

    // --- Gate (see rig/GateBlock.h; verified by tests/gate_test.cpp) ---
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("gateThresh", 1), "Gate Threshold",
        juce::NormalisableRange<float>(-90.0f, -20.0f, 0.5f), -50.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("gateRange", 1), "Gate Range",
        juce::NormalisableRange<float>(20.0f, 100.0f, 1.0f), 80.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("gateAttack", 1), "Gate Attack",
        juce::NormalisableRange<float>(0.05f, 5.0f, 0.01f, 0.4f), 0.1f,
        juce::AudioParameterFloatAttributes().withLabel("ms")));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("gateHold", 1), "Gate Hold",
        juce::NormalisableRange<float>(5.0f, 500.0f, 1.0f, 0.4f), 50.0f,
        juce::AudioParameterFloatAttributes().withLabel("ms")));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("gateRelease", 1), "Gate Release",
        juce::NormalisableRange<float>(10.0f, 1000.0f, 1.0f, 0.4f), 100.0f,
        juce::AudioParameterFloatAttributes().withLabel("ms")));
    // Lookahead preserves pick attacks but adds PDC — keep 0 for live monitoring.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("gateLook", 1), "Gate Lookahead",
        juce::NormalisableRange<float>(0.0f, nam_rig::GateBlock::kMaxLookaheadMs, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel("ms")));

    // --- Comp/Boost (see rig/CompBlock.h; verified by tests/comp_test.cpp) ---
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("compSustain", 1), "Comp Sustain",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("compAttack", 1), "Comp Attack",
        juce::NormalisableRange<float>(1.0f, 50.0f, 0.5f, 0.5f), 15.0f,
        juce::AudioParameterFloatAttributes().withLabel("ms")));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("compLevel", 1), "Comp Level",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("compBoost", 1), "Boost",
        juce::NormalisableRange<float>(0.0f, 20.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    // --- Drive rack: 3 series slots, shared/pre-split (rig/DriveBlock.h;
    //     verified by tests/drive_test.cpp). Type "Off" leaves a slot out of
    //     the path; an all-Off rack is bypassed (bit-exact). ---
    for (int s = 1; s <= nam_rig::DriveBlock::kSlots; ++s)
    {
        const juce::String pid = "drv" + juce::String(s);
        const juce::String lbl = "Drive " + juce::String(s) + " ";
        params.push_back(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID(pid + "Type", 1), lbl + "Type",
            juce::StringArray{"Off", "Boost", "Overdrive", "Distortion", "Fuzz"}, 0));
        params.push_back(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID(pid + "On", 1), lbl + "On", true)); // footswitch
        // Per-TYPE controls: each pedal type keeps its own knobs (no sharing
        // across types within a slot). Drive/Tone are 0..1; Level/Volume in dB.
        // Boost: one Boost knob + input-cap Range + model select.
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(pid + "bDrive", 1), lbl + "Boost",
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f));
        params.push_back(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID(pid + "bRange", 1), lbl + "Boost Range",
            juce::StringArray{"Treble", "Mid", "Full"}, 0)); // treble-boost cap switch
        params.push_back(std::make_unique<juce::AudioParameterInt>(
            juce::ParameterID(pid + "bModel", 1), lbl + "Boost Model", 0, 3, 0));
        // Overdrive: Drive / Tone / Level.
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(pid + "oDrive", 1), lbl + "OD Drive",
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(pid + "oTone", 1), lbl + "OD Tone",
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(pid + "oLevel", 1), lbl + "OD Level",
            juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 0.0f,
            juce::AudioParameterFloatAttributes().withLabel("dB")));
        // Distortion: Distortion / Filter / Volume.
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(pid + "dDrive", 1), lbl + "Dist Drive",
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(pid + "dTone", 1), lbl + "Dist Filter",
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(pid + "dLevel", 1), lbl + "Dist Volume",
            juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 0.0f,
            juce::AudioParameterFloatAttributes().withLabel("dB")));
        // Fuzz: Fuzz / Volume (no tone control).
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(pid + "fDrive", 1), lbl + "Fuzz",
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(pid + "fLevel", 1), lbl + "Fuzz Volume",
            juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 0.0f,
            juce::AudioParameterFloatAttributes().withLabel("dB")));
    }
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID("driveAutoGain", 1), "Drive Auto Gain", false));

    // --- Graphic EQ, pre-cab (see rig/EqBlock.h; verified by tests/eq_test.cpp) ---
    {
        static const char *ids[] = {"eq62", "eq125", "eq250", "eq500",
                                    "eq1k", "eq2k", "eq4k", "eq8k"};
        static const char *names[] = {"EQ 62.5 Hz", "EQ 125 Hz", "EQ 250 Hz", "EQ 500 Hz",
                                      "EQ 1 kHz", "EQ 2 kHz", "EQ 4 kHz", "EQ 8 kHz"};
        for (int b = 0; b < nam_rig::EqBlock::kNumBands; ++b)
            params.push_back(std::make_unique<juce::AudioParameterFloat>(
                juce::ParameterID(ids[b], 1), names[b],
                juce::NormalisableRange<float>(-nam_rig::EqBlock::kMaxGainDb,
                                               nam_rig::EqBlock::kMaxGainDb, 0.1f),
                0.0f, juce::AudioParameterFloatAttributes().withLabel("dB")));
    }

    // --- Post-cab cuts (in CabBlock; knob extremes = off/bit-exact) ---
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("cabHpf", 1), "Cab Low Cut",
        juce::NormalisableRange<float>(20.0f, 300.0f, 1.0f, 0.5f), 20.0f,
        juce::AudioParameterFloatAttributes().withLabel("Hz")));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("cabLpf", 1), "Cab High Cut",
        juce::NormalisableRange<float>(2000.0f, 20000.0f, 10.0f, 0.5f), 20000.0f,
        juce::AudioParameterFloatAttributes().withLabel("Hz")));

    // --- Modulation: 3-slot series section (rig/ModBlock.h; mod_test.cpp).
    // Per-slot bank (superset; the panel shows only each effect's real
    // controls). Slot 1 on by default = the old single-chorus default.
    for (int s = 1; s <= nam_rig::ModBlock::kSlots; ++s)
    {
        const juce::String p = "mod" + juce::String(s);
        const juce::String n = "Mod " + juce::String(s) + " ";
        params.push_back(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID(p + "Type", 1), n + "Type",
            juce::StringArray{"Chorus", "Flanger", "Phaser", "Tremolo",
                              "Vibrato", "Rotary", "Uni-Vibe", "Harm Trem"},
            0));
        params.push_back(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID(p + "Wave", 1), n + "Waveform",
            juce::StringArray{"Sine", "Triangle", "Square", "S&H"}, 0));
        params.push_back(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID(p + "Sync", 1), n + "Sync",
            juce::StringArray{"Off", "1/1", "1/2", "1/4", "1/4.", "1/4T",
                              "1/8", "1/8.", "1/8T", "1/16"},
            0));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(p + "Rate", 1), n + "Rate",
            juce::NormalisableRange<float>(0.05f, 10.0f, 0.01f, 0.4f), 0.8f,
            juce::AudioParameterFloatAttributes().withLabel("Hz")));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(p + "Depth", 1), n + "Depth",
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(p + "Feedback", 1), n + "Feedback",
            juce::NormalisableRange<float>(0.0f, 0.9f, 0.01f), 0.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(p + "Mix", 1), n + "Mix",
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(p + "Width", 1), n + "Width",
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 1.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(p + "Drive", 1), n + "Drive",
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.0f)); // Rotary tube amp
        params.push_back(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID(p + "RotFast", 1), n + "Rotary Fast", false));
        params.push_back(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID(p + "On", 1), n + "Enable", s == 1));
    }

    // --- Delay (rig/DelayBlock.h; verified by tests/delay_test.cpp) ---
    // Order must match DelayBlock::kSyncBeats — append only.
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID("delaySync", 1), "Delay Sync",
        juce::StringArray{"Free", "1/1", "1/2.", "1/2", "1/2T", "1/4.", "1/4",
                          "1/4T", "1/8.", "1/8", "1/8T", "1/16.", "1/16", "1/16T"},
        0));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("delayTime", 1), "Delay Time",
        juce::NormalisableRange<float>(nam_rig::DelayBlock::kMinTimeMs,
                                       nam_rig::DelayBlock::kMaxTimeMs, 1.0f, 0.4f),
        350.0f, juce::AudioParameterFloatAttributes().withLabel("ms")));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("delayFeedback", 1), "Delay Feedback",
        juce::NormalisableRange<float>(0.0f, nam_rig::DelayBlock::kMaxFeedback, 0.01f),
        0.35f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("delayTone", 1), "Delay Tone",
        juce::NormalisableRange<float>(1000.0f, 20000.0f, 10.0f, 0.5f), 8000.0f,
        juce::AudioParameterFloatAttributes().withLabel("Hz")));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID("delayPingPong", 1), "Delay Ping-Pong", false));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("delayWidth", 1), "Delay Width",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("delayMod", 1), "Delay Wow/Flutter",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.15f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("delayMix", 1), "Delay Mix",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.25f));

    // --- Reverb (rig/ReverbBlock.h; verified by tests/reverb_test.cpp) ---
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("revSize", 1), "Reverb Size",
        juce::NormalisableRange<float>(nam_rig::ReverbBlock::kMinSize,
                                       nam_rig::ReverbBlock::kMaxSize, 0.01f),
        1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("revDecay", 1), "Reverb Decay",
        juce::NormalisableRange<float>(nam_rig::ReverbBlock::kDecayMin,
                                       nam_rig::ReverbBlock::kDecayMax, 0.05f, 0.5f), 2.0f,
        juce::AudioParameterFloatAttributes().withLabel("s")));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("revDamp", 1), "Reverb Tone",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.4f, pct()));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("revPredelay", 1), "Reverb Pre-Delay",
        juce::NormalisableRange<float>(nam_rig::ReverbBlock::kPreMin,
                                       nam_rig::ReverbBlock::kPreMax, 1.0f, 0.5f), 20.0f,
        juce::AudioParameterFloatAttributes().withLabel("ms")));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("revMix", 1), "Reverb Mix",
        juce::NormalisableRange<float>(0.0f, nam_rig::ReverbBlock::kMixMax, 0.01f, 0.6f), 0.25f, pct()));

    // --- Input calibration + output normalization (NAM-AA parity; see CalNorm.h).
    // Both are metadata-driven and unity when disabled / metadata absent.
    // Appended last so existing sessions keep their automation indices.
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID("calEnable", 1), "Calibrate Input", false));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("calDbu", 1), "Input Calibration",
        juce::NormalisableRange<float>(0.0f, 30.0f, 0.1f), 12.0f,
        juce::AudioParameterFloatAttributes().withLabel("dBu")));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID("normalize", 1), "Normalize Output", false));

    // --- Dual-rig mixer + Rig B voice (RigChain dual core; see rig/RigChain.h).
    // Appended last so existing sessions keep their automation indices. Rig B
    // shares the amp AA setting with Rig A (v1) -> no separate oversample param.
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID("rigMode", 1), "Rig Mode",
        juce::StringArray{"Solo A", "Solo B", "Dual"}, 0)); // default Solo A
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("rigLevelA", 1), "Rig A Level",
        juce::NormalisableRange<float>(-24.0f, 12.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("rigLevelB", 1), "Rig B Level",
        juce::NormalisableRange<float>(-24.0f, 12.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("rigPanA", 1), "Rig A Pan",
        juce::NormalisableRange<float>(-1.0f, 1.0f, 0.01f), -1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("rigPanB", 1), "Rig B Pan",
        juce::NormalisableRange<float>(-1.0f, 1.0f, 0.01f), 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID("rigPolA", 1), "Rig A Polarity", false));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID("rigPolB", 1), "Rig B Polarity", false));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("rigAlign", 1), "Rig Align",
        juce::NormalisableRange<float>(-256.0f, 256.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel("smp")));
    {
        static const char *idsB[] = {"rigBeq62", "rigBeq125", "rigBeq250", "rigBeq500",
                                     "rigBeq1k", "rigBeq2k", "rigBeq4k", "rigBeq8k"};
        static const char *namesB[] = {"Rig B EQ 62.5 Hz", "Rig B EQ 125 Hz",
                                       "Rig B EQ 250 Hz", "Rig B EQ 500 Hz", "Rig B EQ 1 kHz",
                                       "Rig B EQ 2 kHz", "Rig B EQ 4 kHz", "Rig B EQ 8 kHz"};
        for (int b = 0; b < nam_rig::EqBlock::kNumBands; ++b)
            params.push_back(std::make_unique<juce::AudioParameterFloat>(
                juce::ParameterID(idsB[b], 1), namesB[b],
                juce::NormalisableRange<float>(-nam_rig::EqBlock::kMaxGainDb,
                                               nam_rig::EqBlock::kMaxGainDb, 0.1f),
                0.0f, juce::AudioParameterFloatAttributes().withLabel("dB")));
    }
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("rigBcabHpf", 1), "Rig B Cab Low Cut",
        juce::NormalisableRange<float>(20.0f, 300.0f, 1.0f, 0.5f), 20.0f,
        juce::AudioParameterFloatAttributes().withLabel("Hz")));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("rigBcabLpf", 1), "Rig B Cab High Cut",
        juce::NormalisableRange<float>(2000.0f, 20000.0f, 10.0f, 0.5f), 20000.0f,
        juce::AudioParameterFloatAttributes().withLabel("Hz")));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID("oversampleB", 1), "Rig B Oversampling",
        juce::StringArray{"Off", "2x", "4x", "8x", "16x", "32x"}, 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID("offlineAAB", 1), "Rig B Offline AA",
        juce::StringArray{"Same as live", "8x", "16x", "32x"}, 1));

    // Comp voicing (see rig/CompBlock.h). Appended last for automation
    // stability; default Clean reproduces the original comp behaviour.
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID("compMode", 1), "Comp Mode",
        juce::StringArray{"Clean", "OTA", "Opto", "FET"}, 0));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("compCharacter", 1), "Comp Character",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.35f));

    // Reverb character + per-character voicing knobs (see rig/ReverbBlock.h).
    // Appended last for automation stability; default Hall + mod 0 reproduces the
    // original 8-line FDN reverb. revSize/revDecay/revDamp/revPredelay/revMix are
    // shared across characters and reinterpreted per type by ReverbBlock.
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID("revType", 1), "Reverb Character",
        juce::StringArray{"Room", "Hall", "Plate", "Spring", "Shimmer", "Ambience", "Bloom"},
        nam_rig::ReverbBlock::kHall));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("revMod", 1), "Reverb Modulation",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.3f, pct()));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("revShimmer", 1), "Reverb Shimmer",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f, pct()));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("revTension", 1), "Reverb Tension",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f, pct()));

    // Reverb guardrail + extra controls (see rig/ReverbBlock.h). Width and Freeze
    // are global; Swell is Bloom-only; Pitch is Shimmer-only. Appended last.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("revWidth", 1), "Reverb Width",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 1.0f, pct()));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("revSwell", 1), "Reverb Swell",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.4f, pct()));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID("revPitch", 1), "Reverb Shimmer Pitch",
        juce::StringArray{"Octave", "+2 Oct", "Fifth+Oct"}, 0));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID("revFreeze", 1), "Reverb Freeze", false));
    // Plate Input Filter (vintage plate/studio-style wet low-cut at the plate amp). Plate-only
    // in the UI via ReverbBlock::inputFilterExposed; default 95 Hz = prior hardwired
    // Plate low-cut, so existing Plate sessions are unchanged. Appended last.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("revInputFilter", 1), "Reverb Input Filter",
        juce::NormalisableRange<float>(20.0f, 400.0f, 1.0f, 0.5f), 95.0f,
        juce::AudioParameterFloatAttributes().withLabel("Hz")));

    return {params.begin(), params.end()};
}

NamRigProcessor::NamRigProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Parameters", createParameterLayout())
{
    mPresets = std::make_unique<nam_rig::PresetManager>(*this);
}

NamRigProcessor::~NamRigProcessor() = default;

float NamRigProcessor::calibrationGainDb(int rig) const
{
    const auto &eng = ampFor(rig).engine();
    return nam_rig::CalNorm::calibrationGainDb(
        apvts.getRawParameterValue("calEnable")->load() >= 0.5f,
        eng.hasInputLevelDbu(),
        apvts.getRawParameterValue("calDbu")->load(),
        eng.inputLevelDbu());
}

float NamRigProcessor::normalizationGainDb(int rig) const
{
    const auto &eng = ampFor(rig).engine();
    return nam_rig::CalNorm::normalizationGainDb(
        apvts.getRawParameterValue("normalize")->load() >= 0.5f,
        eng.hasLoudness(),
        eng.loudnessDb());
}

void NamRigProcessor::autoAlign()
{
    if (!isModelLoaded(0) || !isModelLoaded(1))
        return;
    suspendProcessing(true);
    const auto r = mChain.measureAlignment();
    suspendProcessing(false);
    if (auto *p = apvts.getParameter("rigAlign"))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost(
            p->convertTo0to1((float)juce::jlimit(-256.0, 256.0, r.lagSamples)));
        p->endChangeGesture();
    }
    if (auto *p = apvts.getParameter("rigPolB"))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost(r.invert ? 1.0f : 0.0f);
        p->endChangeGesture();
    }
}

void NamRigProcessor::matchLevels()
{
    if (!isModelLoaded(0) || !isModelLoaded(1))
        return;

    suspendProcessing(true);
    const auto lv = mChain.measureLevels();
    suspendProcessing(false);

    if (lv.rmsA <= 1.0e-9 || lv.rmsB <= 1.0e-9)
        return; // a silent voice -> nothing meaningful to match

    // Bring the louder rig down to the quieter (reference) so neither is boosted
    // into clipping; both end at the same loudness.
    const double ref = juce::jmin(lv.rmsA, lv.rmsB);
    auto setLevel = [this](const char *id, double linearRatio)
    {
        if (auto *p = apvts.getParameter(id))
        {
            const float db = juce::jlimit(-24.0f, 12.0f,
                                          (float)juce::Decibels::gainToDecibels(linearRatio));
            p->beginChangeGesture();
            p->setValueNotifyingHost(p->convertTo0to1(db));
            p->endChangeGesture();
        }
    };
    setLevel("rigLevelA", ref / lv.rmsA);
    setLevel("rigLevelB", ref / lv.rmsB);
}

int NamRigProcessor::requestedFactorNow(int rig) const
{
    const char *osId = rig == 0 ? "oversample" : "oversampleB";
    const char *offId = rig == 0 ? "offlineAA" : "offlineAAB";
    const int choice = static_cast<int>(apvts.getRawParameterValue(osId)->load());
    int requested = 1 << juce::jlimit(0, 5, choice); // Off..32x -> 1..32
    const int offline = static_cast<int>(apvts.getRawParameterValue(offId)->load());
    if (isNonRealtime() && offline > 0 && ampFor(rig).engine().isA2())
        requested = juce::jmax(requested, 8 << (offline - 1)); // 8x / 16x / 32x
    return requested;
}

void NamRigProcessor::setNonRealtime(bool isNonRealtimeNow) noexcept
{
    juce::AudioProcessor::setNonRealtime(isNonRealtimeNow);
    updateLatency();
}

void NamRigProcessor::updateLatency()
{
    if (!mChain.isPrepared())
        return;
    mChain.amp.setRequestedFactor(requestedFactorNow(0));
    mChain.ampB.setRequestedFactor(requestedFactorNow(1));
    mChain.gate.setLookaheadMs(apvts.getRawParameterValue("gateLook")->load());
    setLatencySamples((int)std::round(mChain.latencySamples()));
}

void NamRigProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    mSampleRate = sampleRate;
    mChain.prepare(sampleRate, samplesPerBlock);
    updateLatency();
}

void NamRigProcessor::processBlock(juce::AudioBuffer<float> &buffer, juce::MidiBuffer &midi)
{
    juce::ignoreUnused(midi);
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    if (numSamples == 0)
        return;

    // User input gain plus dBu calibration correction (0 dB when disabled/absent).
    const float inGain = juce::Decibels::decibelsToGain(
        apvts.getRawParameterValue("inputGain")->load());
    const float outGain = juce::Decibels::decibelsToGain(
        apvts.getRawParameterValue("outputGain")->load());

    buffer.applyGain(inGain);

    float inPeak = 0.0f;
    for (int ch = 0; ch < numChannels; ++ch)
        inPeak = juce::jmax(inPeak, buffer.getMagnitude(ch, 0, numSamples));
    mInputPeakDb.store(juce::Decibels::gainToDecibels(inPeak, -100.0f));

    const int fA = requestedFactorNow(0);
    const int fB = requestedFactorNow(1);
    mChain.amp.setRequestedFactor(fA);
    mChain.ampB.setRequestedFactor(fB);
    if (fA != mLastFactorA || fB != mLastFactorB)
    {
        mLastFactorA = fA;
        mLastFactorB = fB;
        updateLatency();
    }

    // Gate parameters (atomics; cheap to push every block). Lookahead changes
    // PDC, so re-report latency when it moves.
    mChain.gate.setThresholdDb(apvts.getRawParameterValue("gateThresh")->load());
    mChain.gate.setRangeDb(apvts.getRawParameterValue("gateRange")->load());
    mChain.gate.setAttackMs(apvts.getRawParameterValue("gateAttack")->load());
    mChain.gate.setHoldMs(apvts.getRawParameterValue("gateHold")->load());
    mChain.gate.setReleaseMs(apvts.getRawParameterValue("gateRelease")->load());
    const float gateLookMs = apvts.getRawParameterValue("gateLook")->load();
    if (gateLookMs != mLastGateLookMs)
    {
        mLastGateLookMs = gateLookMs;
        updateLatency(); // sets the block's lookahead + re-reports PDC
    }

    // gateOn does NOT chain-bypass: the lookahead delay must keep running or
    // PDC breaks. Disabled gate = forced open (passthrough + constant delay).
    mChain.gate.setEnabled(apvts.getRawParameterValue("gateOn")->load() >= 0.5f);
    // Comp/Boost parameters (zero latency, so plain chain bypass is safe).
    mChain.comp.setSustain(apvts.getRawParameterValue("compSustain")->load());
    mChain.comp.setAttackMs(apvts.getRawParameterValue("compAttack")->load());
    mChain.comp.setLevelDb(apvts.getRawParameterValue("compLevel")->load());
    mChain.comp.setBoostDb(apvts.getRawParameterValue("compBoost")->load());
    mChain.comp.setMode((int)apvts.getRawParameterValue("compMode")->load());
    mChain.comp.setCharacter(apvts.getRawParameterValue("compCharacter")->load());
    mChain.comp.setBypassed(apvts.getRawParameterValue("compOn")->load() < 0.5f);

    // Drive rack (shared, before the split). Block is bypassed -> skipped
    // entirely when every slot is Off, keeping the no-drive path bit-exact.
    for (int s = 0; s < nam_rig::DriveBlock::kSlots; ++s)
    {
        const juce::String pid = "drv" + juce::String(s + 1);
        auto g = [&](const char *suf) { return apvts.getRawParameterValue(pid + suf)->load(); };
        const int type = (int)g("Type");
        mChain.drive.setKind(s, type);
        mChain.drive.setOn(s, g("On") >= 0.5f);
        // Read only the ACTIVE type's own params (each type is independent).
        switch (type)
        {
        case 1: // Boost
            mChain.drive.setDrive(s, g("bDrive"));
            mChain.drive.setRange(s, (int)g("bRange"));
            mChain.drive.setModel(s, (int)g("bModel"));
            mChain.drive.setTone(s, 0.5f); mChain.drive.setLevelDb(s, 0.0f);
            break;
        case 2: // Overdrive
            mChain.drive.setDrive(s, g("oDrive"));
            mChain.drive.setTone(s, g("oTone"));
            mChain.drive.setLevelDb(s, g("oLevel"));
            mChain.drive.setRange(s, 0); mChain.drive.setModel(s, 0);
            break;
        case 3: // Distortion
            mChain.drive.setDrive(s, g("dDrive"));
            mChain.drive.setTone(s, g("dTone"));
            mChain.drive.setLevelDb(s, g("dLevel"));
            mChain.drive.setRange(s, 0); mChain.drive.setModel(s, 0);
            break;
        case 4: // Fuzz (no tone control)
            mChain.drive.setDrive(s, g("fDrive"));
            mChain.drive.setTone(s, 0.5f);
            mChain.drive.setLevelDb(s, g("fLevel"));
            mChain.drive.setRange(s, 0); mChain.drive.setModel(s, 0);
            break;
        default: break; // Off
        }
    }
    mChain.drive.setAutoGain(apvts.getRawParameterValue("driveAutoGain")->load() >= 0.5f);
    mChain.drive.setBypassed(!mChain.drive.anyActive());
    // Graphic EQ band gains (Rig A; zero latency; chain bypass via eqOn is safe).
    {
        static const char *ids[] = {"eq62", "eq125", "eq250", "eq500",
                                    "eq1k", "eq2k", "eq4k", "eq8k"};
        for (int b = 0; b < nam_rig::EqBlock::kNumBands; ++b)
            mChain.eq.setBandGainDb(b, apvts.getRawParameterValue(ids[b])->load());
    }
    mChain.eq.setBypassed(apvts.getRawParameterValue("eqOn")->load() < 0.5f);

    // Post-cab cuts ride with the cab block (Rig A).
    mChain.cab.setHpfHz(apvts.getRawParameterValue("cabHpf")->load());
    mChain.cab.setLpfHz(apvts.getRawParameterValue("cabLpf")->load());
    mChain.cab.setBypassed(apvts.getRawParameterValue("cabOn")->load() < 0.5f);

    // ---- Rig B voice (amp shares AA with Rig A; EQ + cab cuts independent) ----
    {
        static const char *idsB[] = {"rigBeq62", "rigBeq125", "rigBeq250", "rigBeq500",
                                     "rigBeq1k", "rigBeq2k", "rigBeq4k", "rigBeq8k"};
        for (int b = 0; b < nam_rig::EqBlock::kNumBands; ++b)
            mChain.eqB.setBandGainDb(b, apvts.getRawParameterValue(idsB[b])->load());
    }
    mChain.cabB.setHpfHz(apvts.getRawParameterValue("rigBcabHpf")->load());
    mChain.cabB.setLpfHz(apvts.getRawParameterValue("rigBcabLpf")->load());

    // ---- Dual-rig mixer (mode / per-rig level + pan + polarity + align) ----
    mChain.setLevelA(juce::Decibels::decibelsToGain(apvts.getRawParameterValue("rigLevelA")->load()));
    mChain.setLevelB(juce::Decibels::decibelsToGain(apvts.getRawParameterValue("rigLevelB")->load()));
    mChain.setPanA(apvts.getRawParameterValue("rigPanA")->load());
    mChain.setPanB(apvts.getRawParameterValue("rigPanB")->load());
    mChain.setPolarityA(apvts.getRawParameterValue("rigPolA")->load() >= 0.5f);
    mChain.setPolarityB(apvts.getRawParameterValue("rigPolB")->load() >= 0.5f);
    // Input calibration is split: a GLOBAL stage trims the incoming signal to the
    // reference level so the drive rack + gate/comp get a consistent level, then
    // each amp gets only the RESIDUAL so its NET calibration is unchanged
    // (global + residual == calibrationGainDb(rig); see CalNorm.h).
    const float globalCalDb = nam_rig::CalNorm::globalCalibrationGainDb(
        apvts.getRawParameterValue("calEnable")->load() >= 0.5f,
        apvts.getRawParameterValue("calDbu")->load());
    mChain.setInputCal(juce::Decibels::decibelsToGain(globalCalDb));
    mChain.setInTrimA(juce::Decibels::decibelsToGain(calibrationGainDb(0) - globalCalDb));
    mChain.setInTrimB(juce::Decibels::decibelsToGain(calibrationGainDb(1) - globalCalDb));
    mChain.setOutTrimA(juce::Decibels::decibelsToGain(normalizationGainDb(0)));
    mChain.setOutTrimB(juce::Decibels::decibelsToGain(normalizationGainDb(1)));
    const int rigMode = (int)apvts.getRawParameterValue("rigMode")->load();
    const float rigAlign = apvts.getRawParameterValue("rigAlign")->load();
    mChain.setMode(rigMode);
    mChain.setAlignmentLag(rigAlign);
    // Mode (Solo<->Dual) and align both shift the reported PDC.
    if (rigMode != mLastRigMode || rigAlign != mLastRigAlign)
    {
        mLastRigMode = rigMode;
        mLastRigAlign = rigAlign;
        updateLatency();
    }

    // Stereo section (all zero-latency; plain chain bypass is safe).
    // 3-slot mod section (ids prebuilt = no per-block string alloc; cf. EQ bands)
    static const char *const modIds[3][11] = {
        {"mod1Type", "mod1Wave", "mod1Sync", "mod1Rate", "mod1Depth", "mod1Feedback", "mod1Mix", "mod1Width", "mod1Drive", "mod1RotFast", "mod1On"},
        {"mod2Type", "mod2Wave", "mod2Sync", "mod2Rate", "mod2Depth", "mod2Feedback", "mod2Mix", "mod2Width", "mod2Drive", "mod2RotFast", "mod2On"},
        {"mod3Type", "mod3Wave", "mod3Sync", "mod3Rate", "mod3Depth", "mod3Feedback", "mod3Mix", "mod3Width", "mod3Drive", "mod3RotFast", "mod3On"}};
    for (int s = 0; s < nam_rig::ModBlock::kSlots; ++s)
    {
        mChain.mod.setType(s, (int)apvts.getRawParameterValue(modIds[s][0])->load());
        mChain.mod.setWaveform(s, (int)apvts.getRawParameterValue(modIds[s][1])->load());
        mChain.mod.setSyncIndex(s, (int)apvts.getRawParameterValue(modIds[s][2])->load());
        mChain.mod.setRateHz(s, apvts.getRawParameterValue(modIds[s][3])->load());
        mChain.mod.setDepth(s, apvts.getRawParameterValue(modIds[s][4])->load());
        mChain.mod.setFeedback(s, apvts.getRawParameterValue(modIds[s][5])->load());
        mChain.mod.setMix(s, apvts.getRawParameterValue(modIds[s][6])->load());
        mChain.mod.setWidth(s, apvts.getRawParameterValue(modIds[s][7])->load());
        mChain.mod.setDrive(s, apvts.getRawParameterValue(modIds[s][8])->load());
        mChain.mod.setRotFast(s, apvts.getRawParameterValue(modIds[s][9])->load() >= 0.5f);
        mChain.mod.setSlotBypassed(s, apvts.getRawParameterValue(modIds[s][10])->load() < 0.5f);
    }
    mChain.mod.setBypassed(apvts.getRawParameterValue("modOn")->load() < 0.5f);

    if (auto *ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto bpm = pos->getBpm())
            {
                mChain.delay.setBpm(*bpm);
                mChain.mod.setBpm(*bpm);
            }
    mChain.delay.setSyncIndex((int)apvts.getRawParameterValue("delaySync")->load());
    mChain.delay.setTimeMs(apvts.getRawParameterValue("delayTime")->load());
    mChain.delay.setFeedback(apvts.getRawParameterValue("delayFeedback")->load());
    mChain.delay.setToneHz(apvts.getRawParameterValue("delayTone")->load());
    mChain.delay.setPingPong(apvts.getRawParameterValue("delayPingPong")->load() >= 0.5f);
    mChain.delay.setWidth(apvts.getRawParameterValue("delayWidth")->load());
    mChain.delay.setModAmount(apvts.getRawParameterValue("delayMod")->load());
    mChain.delay.setMix(apvts.getRawParameterValue("delayMix")->load());
    mChain.delay.setBypassed(apvts.getRawParameterValue("delayOn")->load() < 0.5f);

    mChain.reverb.setType((int)apvts.getRawParameterValue("revType")->load());
    mChain.reverb.setSize(apvts.getRawParameterValue("revSize")->load());
    mChain.reverb.setDecaySeconds(mChain.reverb.mappedDecay(apvts.getRawParameterValue("revDecay")->load()));
    mChain.reverb.setDampHz(mChain.reverb.mappedTone(apvts.getRawParameterValue("revDamp")->load()));
    mChain.reverb.setPredelayMs(mChain.reverb.mappedPredelay(apvts.getRawParameterValue("revPredelay")->load()));
    mChain.reverb.setMix(apvts.getRawParameterValue("revMix")->load());
    mChain.reverb.setMod(apvts.getRawParameterValue("revMod")->load());
    mChain.reverb.setShimmer(apvts.getRawParameterValue("revShimmer")->load());
    mChain.reverb.setTension(apvts.getRawParameterValue("revTension")->load());
    mChain.reverb.setWidth(apvts.getRawParameterValue("revWidth")->load());
    mChain.reverb.setSwell(apvts.getRawParameterValue("revSwell")->load());
    mChain.reverb.setPitch((int)apvts.getRawParameterValue("revPitch")->load());
    mChain.reverb.setFreeze(apvts.getRawParameterValue("revFreeze")->load() >= 0.5f);
    mChain.reverb.setInputFilterHz(apvts.getRawParameterValue("revInputFilter")->load());
    mChain.reverb.setBypassed(apvts.getRawParameterValue("reverbOn")->load() < 0.5f);

    mChain.process(buffer);

    buffer.applyGain(outGain);

    float outPeak = 0.0f;
    for (int ch = 0; ch < numChannels; ++ch)
        outPeak = juce::jmax(outPeak, buffer.getMagnitude(ch, 0, numSamples));
    mOutputPeakDb.store(juce::Decibels::gainToDecibels(outPeak, -100.0f));
}

bool NamRigProcessor::isBusesLayoutSupported(const BusesLayout &layouts) const
{
    const auto &mainIn = layouts.getMainInputChannelSet();
    const auto &mainOut = layouts.getMainOutputChannelSet();
    if (mainOut != mainIn)
        return false;
    return mainOut == juce::AudioChannelSet::mono() || mainOut == juce::AudioChannelSet::stereo();
}

void NamRigProcessor::loadModel(const juce::File &namFile, int rig)
{
    rig = juce::jlimit(0, 1, rig);
    if (!namFile.existsAsFile())
    {
        mModelName[rig] = "File not found";
        return;
    }

    const auto info = ampFor(rig).engine().loadModel(
        std::filesystem::path(namFile.getFullPathName().toStdString()));

    if (!info.ok)
    {
        mModelName[rig] = juce::String("Error: ") + info.error;
        mModelLoaded[rig].store(false);
        return;
    }

    juce::String suffix = info.isA2 ? " [AA up to 32x]" : " [1x only]";
    if (info.expectedSampleRate > 0.0 && std::abs(info.expectedSampleRate - 48000.0) > 1.0)
        suffix += " [warn: native " + juce::String(info.expectedSampleRate, 0) + "Hz; AA assumes 48k]";

    mModelName[rig] = namFile.getFileNameWithoutExtension() + suffix;
    // Cache the source text so presets can embed the model (single-file rigs).
    mModelText[rig] = namFile.loadFileAsString();
    mModelBaseName[rig] = namFile.getFileNameWithoutExtension();
    mModelPath[rig] = namFile.getFullPathName();
    mModelLoaded[rig].store(true);

    updateLatency(); // available factors may have changed
}

void NamRigProcessor::loadIr(const juce::File &irFile, int rig)
{
    rig = juce::jlimit(0, 1, rig);
    if (cabFor(rig).loadIr(irFile))
    {
        mIrPath[rig] = irFile.getFullPathName();
        // Cache the source bytes so presets can embed the IR (single-file rigs).
        irFile.loadFileAsData(mIrBytes[rig]);
        mIrBaseName[rig] = irFile.getFileNameWithoutExtension();
        updateLatency();
    }
}

void NamRigProcessor::getStateInformation(juce::MemoryBlock &destData)
{
    auto state = apvts.copyState();
    state.setProperty("modelPath", mModelPath[0], nullptr);
    state.setProperty("irPath", mIrPath[0], nullptr);
    state.setProperty("modelPathB", mModelPath[1], nullptr);
    state.setProperty("irPathB", mIrPath[1], nullptr);
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void NamRigProcessor::setStateInformation(const void *data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (!xml || !xml->hasTagName(apvts.state.getType()))
        return;

    auto state = juce::ValueTree::fromXml(*xml);
    apvts.replaceState(state);

    const juce::String modelPath = state.getProperty("modelPath", "");
    if (modelPath.isNotEmpty())
        loadModel(juce::File(modelPath), 0);
    const juce::String irPath = state.getProperty("irPath", "");
    if (irPath.isNotEmpty())
        loadIr(juce::File(irPath), 0);
    const juce::String modelPathB = state.getProperty("modelPathB", "");
    if (modelPathB.isNotEmpty())
        loadModel(juce::File(modelPathB), 1);
    const juce::String irPathB = state.getProperty("irPathB", "");
    if (irPathB.isNotEmpty())
        loadIr(juce::File(irPathB), 1);
}

juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter()
{
    return new NamRigProcessor();
}