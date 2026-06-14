#pragma once
// FactoryPresets — a small built-in bank of PARAMS-ONLY dual-rig starting
// points (mode / pan / FX / EQ "vibes"). They carry NO model or IR, so they
// apply on top of whatever amps you've loaded: load two amps, pick a vibe,
// tweak. PresetManager writes these into the presets folder once on first run
// (guarded by a marker), so they appear in the normal preset list.

#include "PresetFile.h"

namespace nam_rig
{

struct FactoryPresets
{
    // Build a params var from a list of (paramID, real-world value) pairs.
    // Unlisted params fall back to their defaults when the preset is applied.
    static juce::var mk(std::initializer_list<std::pair<const char *, double>> kv)
    {
        auto *o = new juce::DynamicObject();
        for (const auto &p : kv)
            o->setProperty(p.first, p.second);
        return juce::var(o);
    }

    static std::vector<PresetFile> all()
    {
        std::vector<PresetFile> v;
        auto add = [&](const char *name, juce::var p)
        {
            PresetFile f;
            f.name = name;
            f.params = std::move(p);
            v.push_back(std::move(f));
        };

        add("Wide Clean Dual",
            mk({{"rigMode", 2}, {"rigPanA", -0.7}, {"rigPanB", 0.7},
                {"reverbOn", 1}, {"revMix", 0.22}, {"revDecay", 2.2}, {"revSize", 1.1},
                {"delayOn", 1}, {"delayMix", 0.1}, {"delayTime", 300.0},
                {"gateOn", 0}, {"compOn", 0}}));

        add("Tight Dual Stack",
            mk({{"rigMode", 2}, {"rigPanA", -0.45}, {"rigPanB", 0.45},
                {"gateOn", 1}, {"gateThresh", -46.0}, {"gateRelease", 110.0},
                {"compOn", 1}, {"compSustain", 0.55}, {"compBoost", 3.0},
                {"eq500", -3.0}, {"eq1k", -1.5}, {"eq2k", 1.5}, {"eq4k", 2.0},
                {"rigBeq500", -3.0}, {"rigBeq1k", -1.5}, {"rigBeq2k", 1.5}, {"rigBeq4k", 2.0},
                {"reverbOn", 0}}));

        add("Ambient Spread",
            mk({{"rigMode", 2}, {"rigPanA", -1.0}, {"rigPanB", 1.0},
                {"reverbOn", 1}, {"revMix", 0.4}, {"revDecay", 4.5}, {"revSize", 1.6},
                {"revPredelay", 45.0}, {"delayOn", 1}, {"delaySync", 3},
                {"delayMix", 0.32}, {"delayFeedback", 0.5}, {"delayPingPong", 1}}));

        add("Solo Crunch Boost",
            mk({{"rigMode", 0}, {"compOn", 1}, {"compSustain", 0.4}, {"compBoost", 6.0},
                {"eq500", 2.0}, {"eq1k", 3.0}, {"eq2k", 1.0},
                {"reverbOn", 1}, {"revMix", 0.12}}));

        add("Dual Chorus Doubler",
            mk({{"rigMode", 2}, {"rigPanA", -0.6}, {"rigPanB", 0.6},
                {"modOn", 1}, {"modType", 0}, {"modRate", 0.35}, {"modDepth", 0.28},
                {"modMix", 0.45}, {"reverbOn", 1}, {"revMix", 0.15}}));

        return v;
    }
};

} // namespace nam_rig
