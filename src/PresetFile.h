#pragma once
// PresetFile — the .namrig single-file rig preset format. JSON envelope:
//
//   { "format": "nam-rig-preset", "version": 1, "name": "...",
//     "params": { "<paramID>": <real-world value>, ... },
//     "model":  { "name": "...", "nam": "<raw .nam file text>" } | absent,
//     "ir":     { "name": "...", "wav64": "<base64 of original wav>" } | absent }
//
// The model is embedded as the ORIGINAL FILE TEXT (a .nam is itself JSON);
// the IR as the original wav bytes, base64. Loading writes the bytes back to
// temp files and goes through the exact same loadModel/loadIr paths the file
// pickers use — no second parser to drift. Format round-trip is verified
// byte-exact by tests/preset_test.cpp (juce_core only — this header must not
// depend on any audio module).

#include <juce_core/juce_core.h>

namespace nam_rig
{

struct PresetFile
{
    static constexpr int kVersion = 1;
    static constexpr const char *kFormatTag = "nam-rig-preset";
    static constexpr const char *kExtension = ".namrig";

    juce::String name;
    juce::var params; // DynamicObject: paramID -> double (real-world value)
    juce::String modelName, modelText; // empty = no model embedded
    juce::String irName;
    juce::MemoryBlock irBytes;         // empty = no IR embedded

    bool hasModel() const { return modelText.isNotEmpty(); }
    bool hasIr() const { return irBytes.getSize() > 0; }

    // ---- serialize ----
    juce::String toJson() const
    {
        auto *root = new juce::DynamicObject();
        root->setProperty("format", kFormatTag);
        root->setProperty("version", kVersion);
        root->setProperty("name", name);
        root->setProperty("params", params);
        if (hasModel())
        {
            auto *m = new juce::DynamicObject();
            m->setProperty("name", modelName);
            m->setProperty("nam", modelText);
            root->setProperty("model", juce::var(m));
        }
        if (hasIr())
        {
            auto *ir = new juce::DynamicObject();
            ir->setProperty("name", irName);
            ir->setProperty("wav64",
                            juce::Base64::toBase64(irBytes.getData(), irBytes.getSize()));
            root->setProperty("ir", juce::var(ir));
        }
        return juce::JSON::toString(juce::var(root));
    }

    bool writeToFile(const juce::File &f) const
    {
        return f.replaceWithText(toJson());
    }

    // ---- parse (returns false on anything malformed) ----
    static bool parse(const juce::String &json, PresetFile &out)
    {
        const juce::var v = juce::JSON::parse(json);
        auto *root = v.getDynamicObject();
        if (root == nullptr)
            return false;
        if (root->getProperty("format").toString() != kFormatTag)
            return false;
        if ((int)root->getProperty("version") > kVersion)
            return false; // from a newer plugin — refuse rather than misread

        out = PresetFile{};
        out.name = root->getProperty("name").toString();
        out.params = root->getProperty("params");
        if (out.params.getDynamicObject() == nullptr)
            return false;

        if (auto *m = root->getProperty("model").getDynamicObject())
        {
            out.modelName = m->getProperty("name").toString();
            out.modelText = m->getProperty("nam").toString();
            if (out.modelText.isEmpty())
                return false; // model object present but no payload
        }
        if (auto *ir = root->getProperty("ir").getDynamicObject())
        {
            out.irName = ir->getProperty("name").toString();
            juce::MemoryOutputStream decoded;
            if (!juce::Base64::convertFromBase64(decoded,
                                                 ir->getProperty("wav64").toString()))
                return false;
            out.irBytes = decoded.getMemoryBlock();
            if (out.irBytes.getSize() == 0)
                return false;
        }
        return true;
    }

    static bool readFromFile(const juce::File &f, PresetFile &out)
    {
        if (!f.existsAsFile())
            return false;
        return parse(f.loadFileAsString(), out);
    }
};

} // namespace nam_rig
