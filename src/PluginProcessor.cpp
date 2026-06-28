#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "PresetManager.h"
#include "CalNorm.h"
#include <cmath>

juce::AudioProcessorValueTreeState::ParameterLayout NamRigProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    // 0..1 knobs that read best as a percentage (Tone, Mix, Mod, Shimmer, etc.).
    auto knob10 = [](float lo, float hi) {
        return juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction([lo, hi](float v, int) { return juce::String(juce::roundToInt(9.0f * (v - lo) / (hi - lo)) + 1); })
            .withValueFromStringFunction([lo, hi](const juce::String &t) { return juce::jlimit(lo, hi, lo + (hi - lo) * (t.getFloatValue() - 1.0f) / 9.0f); });
    };
    auto mix100 = [](float lo, float hi) {
        return juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction([lo, hi](float v, int) { return juce::String(juce::roundToInt(100.0f * (v - lo) / (hi - lo))); })   // 0..wetcap -> "0".."100"
            .withValueFromStringFunction([lo, hi](const juce::String &t) { return juce::jlimit(lo, hi, lo + (hi - lo) * t.getFloatValue() * 0.01f); });
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

    // Low-latency monitoring: forces gate lookahead to 0 and caps LIVE amp
    // oversampling at 4x (offline renders are unaffected). Lowest round-trip.
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID("lowLatency", 1), "Low Latency", false));

    // Block bypasses (stub blocks are passthrough anyway; params reserved now
    // so automation indices stay stable as blocks gain DSP).
    for (const char *id : {"gateOn", "compOn", "eqOn", "cabOn", "modOn", "delayOn", "reverbOn"})
        params.push_back(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID(id, 1), juce::String(id).dropLastCharacters(2) + " Enable", true));
    // Rig B's EQ + cab have their own bypass (Rig A uses eqOn / cabOn).
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID("eqOnB", 1), "EQ B Enable", true));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID("cabOnB", 1), "Cab B Enable", true));
    // Master enable for the drive rack (on top of the per-pedal footswitches).
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID("driveOn", 1), "Drive Enable", true));
    // Per-amp enable (latency-preserving bypass; A=rig A, B=rig B).
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID("ampOnA", 1), "Amp A Enable", true));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID("ampOnB", 1), "Amp B Enable", true));

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
        // Shared per-slot model index across ALL drive categories. Range 0..4 so the
        // largest category (Overdrive: 5 models incl. Breaker Drive) is reachable; the
        // model menu clamps to each category's modelCount, so smaller categories ignore
        // the extra index. Default 0.
        params.push_back(std::make_unique<juce::AudioParameterInt>(
            juce::ParameterID(pid + "bModel", 1), lbl + "Boost Model", 0, 4, 0));
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
        // Fuzz: Fuzz / Volume (+ Tone, used only by the Big Muff model 2).
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(pid + "fDrive", 1), lbl + "Fuzz",
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(pid + "fTone", 1), lbl + "Fuzz Tone",
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f)); // Muff Tone scoop (Round Fuzz models ignore it)
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(pid + "fLevel", 1), lbl + "Fuzz Volume",
            juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 0.0f,
            juce::AudioParameterFloatAttributes().withLabel("dB")));
        params.push_back(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID(pid + "fGate", 1), lbl + "Fuzz Gate", true)); // bias-starved splat (Round Fuzz II)
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
                              "Vibrato", "Rotary", "Uni-Vibe", "Harm Trem", "Bi-Phase",
                              "Ring Mod"},
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
            juce::NormalisableRange<float>(0.03f, 20.0f, 0.01f, 0.35f), 0.8f, // ModVoice caps per effect
            juce::AudioParameterFloatAttributes().withLabel("Hz")));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(p + "Depth", 1), n + "Depth",
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(p + "Feedback", 1), n + "Feedback",
            juce::NormalisableRange<float>(0.0f, 0.95f, 0.01f), 0.0f)); // Regen (flanger/phaser)
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
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(p + "Manual", 1), n + "Manual",
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.3f)); // Flanger static comb position
        params.push_back(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID(p + "Invert", 1), n + "Invert", false)); // Flanger phase invert
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(p + "P2Ratio", 1), n + "Sweep 2",
            juce::NormalisableRange<float>(0.5f, 2.0f, 0.01f, 0.63f), 1.5f)); // Bi-Phase Gen 2 ratio (musical detune, taper centred on 1.0)
        params.push_back(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID(p + "Series", 1), n + "Series", false)); // Bi-Phase series/parallel
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(p + "HornDrum", 1), n + "Horn/Drum",
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f)); // Rotary horn<->drum balance (0.5 = neutral)
        params.push_back(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID(p + "Extreme", 1), n + "Extreme", false)); // reassigns controls to their wild range (phaser/uni-vibe/bi-phase)
        params.push_back(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID(p + "On", 1), n + "Enable", s == 1));
    }
    // Mod section routing (whole-section, not per-slot): Series chains the slots;
    // Parallel runs each slot on the dry input and blends them on a Cartesian pad
    // (modPadX/modPadY), then one global Mod Mix vs dry. Default Series keeps the
    // existing behaviour. (Distinct from each slot's Bi-Phase "Series" toggle.)
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID("modRouting", 1), "Mod Routing",
        juce::StringArray{"Series", "Parallel"}, 0));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("modPadX", 1), "Mod Blend X",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("modPadY", 1), "Mod Blend Y",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 1.0f / 3.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("modMix", 1), "Mod Mix",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 1.0f));
    // Series chain order: which slot runs at each position. One choice param (the
    // six permutations of the three slots) so it saves/automates as a single value
    // without the inconsistency a per-position param set could hit. Default
    // "1-2-3" = the fixed order, so old presets are unchanged. Only acts in Series.
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID("modChainOrder", 1), "Mod Chain Order",
        juce::StringArray{"1-2-3", "1-3-2", "2-1-3", "2-3-1", "3-1-2", "3-2-1"}, 0));

    // POST modulation block: a dedicated end-of-section effect (rotary/tremolo/
    // harm-trem "speaker/amp" stage) that processes the combined output of the 3
    // front slots. Same control superset as a slot (enum-aligned Type; the editor
    // only offers the post effects). Off by default; Rotary when first enabled.
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID("postType", 1), "Post Type",
        juce::StringArray{"Chorus", "Flanger", "Phaser", "Tremolo",
                          "Vibrato", "Rotary", "Uni-Vibe", "Harm Trem", "Bi-Phase",
                          "Ring Mod"},
        5)); // default Rotary
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID("postWave", 1), "Post Waveform",
        juce::StringArray{"Sine", "Triangle", "Square", "S&H"}, 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID("postSync", 1), "Post Sync",
        juce::StringArray{"Off", "1/1", "1/2", "1/4", "1/4.", "1/4T",
                          "1/8", "1/8.", "1/8T", "1/16"},
        0));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("postRate", 1), "Post Rate",
        juce::NormalisableRange<float>(0.03f, 20.0f, 0.01f, 0.35f), 0.8f,
        juce::AudioParameterFloatAttributes().withLabel("Hz")));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("postDepth", 1), "Post Depth",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("postFeedback", 1), "Post Feedback",
        juce::NormalisableRange<float>(0.0f, 0.95f, 0.01f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("postMix", 1), "Post Mix",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("postWidth", 1), "Post Width",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("postDrive", 1), "Post Drive",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID("postRotFast", 1), "Post Rotary Fast", false));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("postManual", 1), "Post Manual",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.3f));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID("postInvert", 1), "Post Invert", false));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("postP2Ratio", 1), "Post Sweep 2",
        juce::NormalisableRange<float>(0.5f, 2.0f, 0.01f, 0.63f), 1.5f));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID("postSeries", 1), "Post Series", false));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("postHornDrum", 1), "Post Horn/Drum",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID("postOn", 1), "Post Enable", false)); // off by default

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
    // Right-side FREE time: the independent R delay when DUAL is on and the main
    // delay is unsynced (the synced case uses delaySyncR instead).
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("delayTimeR", 1), "Delay Time R",
        juce::NormalisableRange<float>(nam_rig::DelayBlock::kMinTimeMs,
                                       nam_rig::DelayBlock::kMaxTimeMs, 1.0f, 0.4f),
        350.0f, juce::AudioParameterFloatAttributes().withLabel("ms")));
    // Feedback runs to 1.1 so the TAPE character can self-oscillate: its authentic
    // band-pass loop is lossy, so the mid-band only takes off above ~unity, and the
    // in-loop saturation bounds the runaway. The clean delay is internally clamped
    // to kMaxFeedback (0.95) in DelayBlock::process, so the top of the range is
    // tape-only and clean can never run away.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("delayFeedback", 1), "Delay Feedback",
        juce::NormalisableRange<float>(0.0f, 1.1f, 0.01f),
        0.40f)); // classic 'add-and-play' default: a few clear repeats
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
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.0f)); // 0 on the clean delay; the tape delay will expose this
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("delayMix", 1), "Delay Mix",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f, 0.5f), 0.30f)); // skew: finer control low-mix; classic ~30% wet default
    // Right-side division: index 0 = Link (R mirrors L); 1..13 mirror
    // DelayBlock::kSyncBeats[1..13]. Unlinking = dual independent L/R delay.
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID("delaySyncR", 1), "Delay Sync R",
        juce::StringArray{"Link", "1/1", "1/2.", "1/2", "1/2T", "1/4.", "1/4",
                          "1/4T", "1/8.", "1/8", "1/8T", "1/16.", "1/16", "1/16T"},
        0));
    // Feedback Low Cut (high-pass in the loop); kMinLowCutHz default = off.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("delayLowCut", 1), "Delay Low Cut",
        juce::NormalisableRange<float>(nam_rig::DelayBlock::kMinLowCutHz, 2000.0f, 1.0f, 0.45f),
        90.0f, juce::AudioParameterFloatAttributes().withLabel("Hz"))); // gentle feedback HPF so stacked repeats don't build up muddy
    // Delay CHARACTER (like the reverb voicings): Clean = transparent engine;
    // Tape Echo = tape-style echo; Space Tape = multi-head tape echo (3 playback
    // heads at 1x/2x/3x, mono). Order must match DelayBlock::Character -- append
    // only. Default Clean keeps the shipped delay unchanged.
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID("delayCharacter", 1), "Delay Character",
        juce::StringArray{"Clean", "Tape Echo", "Space Tape"}, 0));
    // Space Tape MODE dial (the multi-head tape echo's 11 echo modes + Reverb-only). Modes 5-11
    // and Reverb engage the rig's Spring reverb (auto-wired in the processor); modes
    // 1-4 are echo only. Order must match DelayBlock::kStHeadMask -- append only.
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID("delayHeadMode", 1), "Delay Mode",
        juce::StringArray{"1 Head 1", "2 Head 2", "3 Head 3", "4 Heads 2+3",
                          "5 Head 1 +Rev", "6 Head 2 +Rev", "7 Head 3 +Rev",
                          "8 Heads 1+2 +Rev", "9 Heads 2+3 +Rev", "10 Heads 1+3 +Rev",
                          "11 All +Rev", "12 Reverb Only"}, 3));

    // --- Reverb (rig/ReverbBlock.h; verified by tests/reverb_test.cpp) ---
    // Reverb Decay/Tone/Predelay/Mod/Size are PER-CHARACTER (generated below); the UI
    // rebinds the knobs to the active character's params (see ui/Panels.h). Mix stays global.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("revMix", 1), "Reverb Mix",
        juce::NormalisableRange<float>(0.0f, nam_rig::ReverbBlock::kMixMax, 0.01f, 0.6f), 0.25f, mix100(0.0f, nam_rig::ReverbBlock::kMixMax)));

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
    // has its own oversampleB / offlineAAB params; the chain delay-compensates.
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
    // Only the SHIPPED characters are offered in the selector (Ambience/Bloom are
    // implemented but locked away — see ReverbBlock::shipped()/kNumShipped). Shipped
    // characters are the first kNumShipped enum entries, so choice index == Type and
    // automation indices stay stable.
    {
        using RB = nam_rig::ReverbBlock;
        juce::StringArray revTypes;
        for (int t = 0; t < RB::kNumShipped; ++t)
            revTypes.add(RB::typeName(t));
        params.push_back(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID("revType", 1), "Reverb Character", revTypes, RB::kHall));
    }
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("revShimmer", 1), "Reverb Shimmer",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f, knob10(0.0f, 1.0f)));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("revTension", 1), "Reverb Tension",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f, knob10(0.0f, 1.0f)));

    // Reverb guardrail + extra controls (see rig/ReverbBlock.h). Width and Freeze
    // are global; Swell is Bloom-only; Pitch is Shimmer-only. Appended last.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("revWidth", 1), "Reverb Width",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 1.0f, knob10(0.0f, 1.0f)));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("revSwell", 1), "Reverb Swell",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.4f, knob10(0.0f, 1.0f)));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID("revPitch", 1), "Reverb Shimmer Pitch",
        juce::StringArray{"Octave", "+2 Oct", "Fifth+Oct"}, 0));
    // Plate Input Filter (studio-style wet low-cut at the plate amp). Plate-only
    // in the UI via ReverbBlock::inputFilterExposed; default 95 Hz = prior hardwired
    // Plate low-cut, so existing Plate sessions are unchanged. Appended last.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("revInputFilter", 1), "Reverb Input Filter",
        juce::NormalisableRange<float>(20.0f, 400.0f, 1.0f, 0.5f), 95.0f,
        juce::AudioParameterFloatAttributes().withLabel("Hz")));

    // --- Per-character reverb knobs (independent state + own ranges, like the drive
    // pedal). Appended last for automation stability. IDs come from ReverbBlock::paramId
    // so the layout, processor and UI can never disagree.
    {
        using RB = nam_rig::ReverbBlock;
        for (int t = 0; t < RB::kNumTypes; ++t)
        {
            const RB::Type T = (RB::Type)t;
            const juce::String nm = RB::typeName(t);
            const auto dr = RB::decayRange(T);
            params.push_back(std::make_unique<juce::AudioParameterFloat>(
                juce::ParameterID(juce::String(RB::paramId("Decay", t)), 1), nm + " Decay",
                juce::NormalisableRange<float>(dr.lo, dr.hi, 0.01f, 0.5f), RB::rangeDefault(dr),
                juce::AudioParameterFloatAttributes().withLabel("s")));
            params.push_back(std::make_unique<juce::AudioParameterFloat>(
                juce::ParameterID(juce::String(RB::paramId("Tone", t)), 1), nm + " Tone",
                juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), (T == RB::kPlate ? 0.2f : 0.4f), knob10(0.0f, 1.0f)));
            if (RB::predelayExposed(T))
            {
                const auto pr = RB::predelayRange(T);
                params.push_back(std::make_unique<juce::AudioParameterFloat>(
                    juce::ParameterID(juce::String(RB::paramId("Predelay", t)), 1), nm + " Pre-Delay",
                    juce::NormalisableRange<float>(pr.lo, pr.hi, 1.0f, 0.5f), 0.0f,
                    juce::AudioParameterFloatAttributes().withLabel("ms")));
            }
            if (RB::modExposed(T))
                params.push_back(std::make_unique<juce::AudioParameterFloat>(
                    juce::ParameterID(juce::String(RB::paramId("Mod", t)), 1), nm + " Modulation",
                    juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.3f, knob10(0.0f, 1.0f)));
            if (RB::sizeExposed(T))
                params.push_back(std::make_unique<juce::AudioParameterFloat>(
                    juce::ParameterID(juce::String(RB::paramId("Size", t)), 1), nm + " Size",
                    juce::NormalisableRange<float>(RB::kMinSize, RB::kMaxSize, 0.01f), 1.0f));
            if (RB::freezeExposed(T)) // independent Freeze on/off per lush character
                params.push_back(std::make_unique<juce::AudioParameterBool>(
                    juce::ParameterID(juce::String(RB::paramId("Freeze", t)), 1), nm + " Freeze", false));
        }
    }

    // Spring "Boing" = dispersion / sproing amount (0..1). Spring-only in the UI
    // via ReverbBlock::boingExposed; a touch is baked in by default (0.30) so
    // Spring sproings out of the box. Appended last for automation-index stability.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("revBoing", 1), "Reverb Boing",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.20f, knob10(0.0f, 1.0f)));

    return {params.begin(), params.end()};
}

NamRigProcessor::NamRigProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Parameters", createParameterLayout())
{
    mPresets = std::make_unique<nam_rig::PresetManager>(*this);
    // Listen for mod-slot type changes so a USER type switch resets that slot's
    // knobs to the new type's defaults (suppressed during state/preset loads).
    for (int s = 1; s <= nam_rig::ModBlock::kSlots; ++s)
        apvts.addParameterListener("mod" + juce::String(s) + "Type", this);
    apvts.addParameterListener("postType", this); // the post block resets too
}

NamRigProcessor::~NamRigProcessor()
{
    for (int s = 1; s <= nam_rig::ModBlock::kSlots; ++s)
        apvts.removeParameterListener("mod" + juce::String(s) + "Type", this);
    apvts.removeParameterListener("postType", this);
}

void NamRigProcessor::parameterChanged(const juce::String &paramID, float)
{
    if (mSuppressTypeReset.load())
        return; // a state/preset load is in progress -> keep the saved knobs
    if (paramID == "postType")
    {
        mPendingTypeReset.fetch_or(1 << nam_rig::ModBlock::kSlots); // post = bit kSlots
        triggerAsyncUpdate();
    }
    else if (paramID.startsWith("mod") && paramID.endsWith("Type"))
    {
        const int slot = paramID[3] - '1'; // "mod1Type".."mod3Type" -> 0..2
        if (slot >= 0 && slot < nam_rig::ModBlock::kSlots)
        {
            mPendingTypeReset.fetch_or(1 << slot);
            triggerAsyncUpdate(); // do the actual param writes on the message thread
        }
    }
}

void NamRigProcessor::handleAsyncUpdate()
{
    const int mask = mPendingTypeReset.exchange(0);
    // Reset everything on the slot EXCEPT its Type and On (enable) to defaults.
    static const char *const suffix[] = {"Wave", "Sync", "Rate", "Depth", "Feedback",
                                         "Mix", "Width", "Drive", "RotFast", "Manual",
                                         "Invert", "P2Ratio", "Series", "HornDrum"};
    auto resetPrefix = [&](const juce::String &p, int newType) {
        // Tremolo (3) / Harm Trem (7) are amp-style effects -> default to MONO width
        // (Width is one shared per-slot param, so the per-type default lives here).
        const bool monoWidth = (newType == 3 || newType == 7);
        for (auto *sfx : suffix)
            if (auto *prm = apvts.getParameter(p + sfx))
            {
                const float v = (monoWidth && juce::String(sfx) == "Width")
                                    ? prm->convertTo0to1(0.0f)
                                    : prm->getDefaultValue();
                prm->beginChangeGesture();
                prm->setValueNotifyingHost(v);
                prm->endChangeGesture();
            }
    };
    for (int s = 0; s < nam_rig::ModBlock::kSlots; ++s)
        if (mask & (1 << s))
            resetPrefix("mod" + juce::String(s + 1),
                        (int)apvts.getRawParameterValue("mod" + juce::String(s + 1) + "Type")->load());
    if (mask & (1 << nam_rig::ModBlock::kSlots))
        resetPrefix("post", (int)apvts.getRawParameterValue("postType")->load());
}

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
    // Low Latency caps LIVE oversampling at 4x (offline renders override below).
    if (apvts.getRawParameterValue("lowLatency")->load() >= 0.5f)
        requested = juce::jmin(requested, 4);
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
    const bool lowLat = apvts.getRawParameterValue("lowLatency")->load() >= 0.5f;
    mChain.gate.setLookaheadMs(lowLat ? 0.0f : apvts.getRawParameterValue("gateLook")->load());
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
    // Per-amp bypass (latency-preserving; does not change PDC).
    mChain.amp.setBypass(apvts.getRawParameterValue("ampOnA")->load() < 0.5f);
    mChain.ampB.setBypass(apvts.getRawParameterValue("ampOnB")->load() < 0.5f);
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
    const bool lowLat = apvts.getRawParameterValue("lowLatency")->load() >= 0.5f;
    const float gateLookMs = lowLat ? 0.0f : apvts.getRawParameterValue("gateLook")->load();
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
        case 2: // Overdrive (4 models: 0 Green Drive v1 / 1 Green Drive II / 2 Super Drive = SD-1 / 3 Gold Horse = Klon)
            mChain.drive.setDrive(s, g("oDrive"));
            mChain.drive.setTone(s, g("oTone"));
            mChain.drive.setLevelDb(s, g("oLevel"));
            mChain.drive.setRange(s, 0); mChain.drive.setModel(s, (int)g("bModel"));
            break;
        case 3: // Distortion (2 models: 0 Black Rodent / 1 Black Rodent II)
            mChain.drive.setDrive(s, g("dDrive"));
            mChain.drive.setTone(s, g("dTone"));
            mChain.drive.setLevelDb(s, g("dLevel"));
            mChain.drive.setRange(s, 0); mChain.drive.setModel(s, (int)g("bModel"));
            break;
        case 4: // Fuzz (3 models: 0 Round Fuzz / 1 Round Fuzz II / 2 Violet Ram)
            mChain.drive.setDrive(s, g("fDrive"));
            // Round Fuzz models have no tone (pinned 0.5); the Big Muff (model 2)
            // exposes the Muff Tone scoop via fTone.
            mChain.drive.setTone(s, (int)g("bModel") == 2 ? g("fTone") : 0.5f);
            mChain.drive.setLevelDb(s, g("fLevel"));
            mChain.drive.setRange(s, 0); mChain.drive.setModel(s, (int)g("bModel"));
            mChain.drive.setGateOn(s, g("fGate") > 0.5f); // bias-starved gate (Round Fuzz II)
            break;
        default: break; // Off
        }
    }
    mChain.drive.setAutoGain(apvts.getRawParameterValue("driveAutoGain")->load() >= 0.5f);
    mChain.drive.setBypassed(apvts.getRawParameterValue("driveOn")->load() < 0.5f
                             || !mChain.drive.anyActive());
    // Graphic EQ band gains (Rig A; zero latency; chain bypass via eqOn is safe).
    {
        static const char *ids[] = {"eq62", "eq125", "eq250", "eq500",
                                    "eq1k", "eq2k", "eq4k", "eq8k"};
        for (int b = 0; b < nam_rig::EqBlock::kNumBands; ++b)
            mChain.eq.setBandGainDb(b, apvts.getRawParameterValue(ids[b])->load());
    }
    mChain.eq.setAutoGain(true); // always-on output loudness-lock (no UI toggle)
    mChain.eq.setBypassed(apvts.getRawParameterValue("eqOn")->load() < 0.5f);

    // Post-cab cuts ride with the cab block (Rig A).
    mChain.cab.setHpfHz(apvts.getRawParameterValue("cabHpf")->load());
    mChain.cab.setLpfHz(apvts.getRawParameterValue("cabLpf")->load());
    // cabOn bypasses only the IR convolution; the Low/High cuts always run, so
    // they stay usable with a baked-in-speaker NAM (no IR).
    mChain.cab.setConvBypassed(apvts.getRawParameterValue("cabOn")->load() < 0.5f);

    // ---- Rig B voice (independent amp AA + EQ + cab cuts; see oversampleB) ----
    {
        static const char *idsB[] = {"rigBeq62", "rigBeq125", "rigBeq250", "rigBeq500",
                                     "rigBeq1k", "rigBeq2k", "rigBeq4k", "rigBeq8k"};
        for (int b = 0; b < nam_rig::EqBlock::kNumBands; ++b)
            mChain.eqB.setBandGainDb(b, apvts.getRawParameterValue(idsB[b])->load());
    }
    mChain.eqB.setAutoGain(true); // always-on output loudness-lock (no UI toggle)
    mChain.eqB.setBypassed(apvts.getRawParameterValue("eqOnB")->load() < 0.5f);
    mChain.cabB.setHpfHz(apvts.getRawParameterValue("rigBcabHpf")->load());
    mChain.cabB.setLpfHz(apvts.getRawParameterValue("rigBcabLpf")->load());
    mChain.cabB.setConvBypassed(apvts.getRawParameterValue("cabOnB")->load() < 0.5f);

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
    static const char *const modIds[3][17] = {
        {"mod1Type", "mod1Wave", "mod1Sync", "mod1Rate", "mod1Depth", "mod1Feedback", "mod1Mix", "mod1Width", "mod1Drive", "mod1RotFast", "mod1Manual", "mod1Invert", "mod1P2Ratio", "mod1Series", "mod1HornDrum", "mod1Extreme", "mod1On"},
        {"mod2Type", "mod2Wave", "mod2Sync", "mod2Rate", "mod2Depth", "mod2Feedback", "mod2Mix", "mod2Width", "mod2Drive", "mod2RotFast", "mod2Manual", "mod2Invert", "mod2P2Ratio", "mod2Series", "mod2HornDrum", "mod2Extreme", "mod2On"},
        {"mod3Type", "mod3Wave", "mod3Sync", "mod3Rate", "mod3Depth", "mod3Feedback", "mod3Mix", "mod3Width", "mod3Drive", "mod3RotFast", "mod3Manual", "mod3Invert", "mod3P2Ratio", "mod3Series", "mod3HornDrum", "mod3Extreme", "mod3On"}};
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
        mChain.mod.setManual(s, apvts.getRawParameterValue(modIds[s][10])->load());
        mChain.mod.setInvert(s, apvts.getRawParameterValue(modIds[s][11])->load() >= 0.5f);
        mChain.mod.setP2Ratio(s, apvts.getRawParameterValue(modIds[s][12])->load());
        mChain.mod.setSeries(s, apvts.getRawParameterValue(modIds[s][13])->load() >= 0.5f);
        mChain.mod.setHornDrum(s, apvts.getRawParameterValue(modIds[s][14])->load());
        mChain.mod.setExtreme(s, apvts.getRawParameterValue(modIds[s][15])->load() >= 0.5f);
        mChain.mod.setSlotBypassed(s, apvts.getRawParameterValue(modIds[s][16])->load() < 0.5f);
        mChain.mod.setSlotSolo(s, modSolo[(size_t)s].load()); // momentary dial-in solo
    }
    mChain.mod.setBypassed(apvts.getRawParameterValue("modOn")->load() < 0.5f);
    // Section routing + parallel blend pad + global mod-mix (whole-section).
    mChain.mod.setParallel(apvts.getRawParameterValue("modRouting")->load() >= 0.5f);
    mChain.mod.setPad(apvts.getRawParameterValue("modPadX")->load(),
                      apvts.getRawParameterValue("modPadY")->load());
    mChain.mod.setModMix(apvts.getRawParameterValue("modMix")->load());
    // Map the chain-order choice (six permutations) to the slot sequence.
    {
        static const int kPerm[6][3] = {
            {0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
        const int oi = juce::jlimit(0, 5, (int)apvts.getRawParameterValue("modChainOrder")->load());
        mChain.mod.setChainOrder(kPerm[oi][0], kPerm[oi][1], kPerm[oi][2]);
    }
    // Post block (end-of-section effect).
    mChain.mod.setPostType((int)apvts.getRawParameterValue("postType")->load());
    mChain.mod.setPostWaveform((int)apvts.getRawParameterValue("postWave")->load());
    mChain.mod.setPostSyncIndex((int)apvts.getRawParameterValue("postSync")->load());
    mChain.mod.setPostRateHz(apvts.getRawParameterValue("postRate")->load());
    mChain.mod.setPostDepth(apvts.getRawParameterValue("postDepth")->load());
    mChain.mod.setPostFeedback(apvts.getRawParameterValue("postFeedback")->load());
    mChain.mod.setPostMix(apvts.getRawParameterValue("postMix")->load());
    mChain.mod.setPostWidth(apvts.getRawParameterValue("postWidth")->load());
    mChain.mod.setPostDrive(apvts.getRawParameterValue("postDrive")->load());
    mChain.mod.setPostRotFast(apvts.getRawParameterValue("postRotFast")->load() >= 0.5f);
    mChain.mod.setPostManual(apvts.getRawParameterValue("postManual")->load());
    mChain.mod.setPostInvert(apvts.getRawParameterValue("postInvert")->load() >= 0.5f);
    mChain.mod.setPostP2Ratio(apvts.getRawParameterValue("postP2Ratio")->load());
    mChain.mod.setPostSeries(apvts.getRawParameterValue("postSeries")->load() >= 0.5f);
    mChain.mod.setPostHornDrum(apvts.getRawParameterValue("postHornDrum")->load());
    mChain.mod.setPostBypassed(apvts.getRawParameterValue("postOn")->load() < 0.5f);

    if (auto *ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto bpm = pos->getBpm())
            {
                mChain.delay.setBpm(*bpm);
                mChain.mod.setBpm(*bpm);
            }
    const int delayChar = (int)apvts.getRawParameterValue("delayCharacter")->load();
    // Ping-pong + dual (independent L/R, via Sync R unlinked) are STEREO digital-delay
    // tricks; both tape characters are authentically mono (Space Tape already ignores
    // them), so they are Clean-only -- forced off for a tape character regardless of the
    // stored param, so the controls can't contradict the character.
    const bool delayTape = (delayChar != (int)nam_rig::DelayBlock::Character::Clean);
    mChain.delay.setSyncIndex((int)apvts.getRawParameterValue("delaySync")->load());
    mChain.delay.setSyncIndexR(delayTape ? 0 : (int)apvts.getRawParameterValue("delaySyncR")->load());
    mChain.delay.setTimeMs(apvts.getRawParameterValue("delayTime")->load());
    mChain.delay.setTimeMsR(apvts.getRawParameterValue("delayTimeR")->load());
    mChain.delay.setFeedback(apvts.getRawParameterValue("delayFeedback")->load());
    mChain.delay.setToneHz(apvts.getRawParameterValue("delayTone")->load());
    mChain.delay.setLowCutHz(apvts.getRawParameterValue("delayLowCut")->load());
    // Character drives the per-voicing loop stages + time-change feel. Clean = the
    // transparent engine, forced to Digital (crossfade, no repitch); a tape
    // character sets its own Tape glide inside setCharacter, so only force Digital
    // for Clean.
    mChain.delay.setCharacter(delayChar);
    mChain.delay.setHeadMode((int)apvts.getRawParameterValue("delayHeadMode")->load());
    if (delayChar == (int)nam_rig::DelayBlock::Character::Clean)
        mChain.delay.setTimeMode(nam_rig::DelayBlock::TimeMode::Digital);
    mChain.delay.setPingPong(!delayTape && apvts.getRawParameterValue("delayPingPong")->load() >= 0.5f);
    mChain.delay.setWidth(apvts.getRawParameterValue("delayWidth")->load());
    mChain.delay.setModAmount(apvts.getRawParameterValue("delayMod")->load());
    mChain.delay.setMix(apvts.getRawParameterValue("delayMix")->load());
    mChain.delay.setBypassed(apvts.getRawParameterValue("delayOn")->load() < 0.5f);

    // Space Tape auto-spring: the multi-head tape echo mode dial engages the spring for modes
    // 5-11 + Reverb (echo-only modes 1-4 force it off). When Space Tape is active it
    // drives the rig's Spring reverb from the head mode, overriding the manual reverb.
    int stReverbOverride = 0; // 0 none, +1 force Spring on, -1 force off
    if (delayChar == (int)nam_rig::DelayBlock::Character::SpaceTape)
        stReverbOverride = nam_rig::DelayBlock::spaceTapeReverbOn(
            (int)apvts.getRawParameterValue("delayHeadMode")->load()) ? 1 : -1;
    {
        using RB = nam_rig::ReverbBlock;
        int rt = (int)apvts.getRawParameterValue("revType")->load();
        if (stReverbOverride == 1) rt = (int)RB::kSpring; // Space Tape -> rig Spring
        const RB::Type T = (RB::Type)rt;
        mChain.reverb.setType(rt);
        auto pv = [&](const char *knob) { return apvts.getRawParameterValue(juce::String(RB::paramId(knob, rt)))->load(); };
        mChain.reverb.setDecaySeconds(pv("Decay"));                 // per-character param is already true seconds in-range
        mChain.reverb.setDampHz(mChain.reverb.mappedTone(pv("Tone")));
        mChain.reverb.setPredelayMs(RB::predelayExposed(T) ? pv("Predelay") : 0.0f);
        mChain.reverb.setMod(RB::modExposed(T) ? pv("Mod") : 0.0f);
        mChain.reverb.setSize(RB::sizeExposed(T) ? pv("Size") : 1.0f);
        mChain.reverb.setFreeze(RB::freezeExposed(T) && pv("Freeze") >= 0.5f); // per-character Freeze state
    }
    mChain.reverb.setMix(apvts.getRawParameterValue("revMix")->load());
    mChain.reverb.setShimmer(apvts.getRawParameterValue("revShimmer")->load());
    mChain.reverb.setTension(apvts.getRawParameterValue("revTension")->load());
    mChain.reverb.setBoing(apvts.getRawParameterValue("revBoing")->load());
    mChain.reverb.setWidth(apvts.getRawParameterValue("revWidth")->load());
    mChain.reverb.setSwell(apvts.getRawParameterValue("revSwell")->load());
    mChain.reverb.setPitch((int)apvts.getRawParameterValue("revPitch")->load());
    mChain.reverb.setInputFilterHz(apvts.getRawParameterValue("revInputFilter")->load());
    // Reverb on/off: user param, unless Space Tape's mode dial overrides it.
    const bool userReverbOff = apvts.getRawParameterValue("reverbOn")->load() < 0.5f;
    mChain.reverb.setBypassed(stReverbOverride == 1 ? false
                              : stReverbOverride == -1 ? true
                                                       : userReverbOff);

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
    beginStateLoad(); // don't let the type-change reset wipe the restored knobs
    apvts.replaceState(state);
    endStateLoadDeferred();

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