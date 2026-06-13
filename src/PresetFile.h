#pragma once
// PresetFile — the .namrig single-file rig preset format. JSON envelope (v2):
//
//   { "format": "nam-rig-preset", "version": 2, "name": "...",
//     "params": { "<paramID>": <real-world value>, ... },
//     "model":  { "name": "...", "nam": "<raw .nam text>" } | absent,   // Rig A
//     "ir":     { "name": "...", "wav64": "<base64 wav>" } | absent,     // Rig A
//     "modelB": { ... } | absent,                                        // Rig B
//     "irB":    { ... } | absent }                                       // Rig B
//
// v1 files (single rig, no modelB/irB) still load -> their model/ir go to Rig A.
// Rig A keeps the original "model"/"ir" keys so the v1 round-trip test is
// unaffected. The model is embedded as the ORIGINAL .nam text, the IR as the
// original wav bytes (base64); loading writes temp files and replays them
// through the same loadModel/loadIr paths the pickers use — no second parser.
// Verified byte-exact by tests/preset_test.cpp (juce_core only — no audio dep).

#include <juce_core/juce_core.h>

namespace nam_rig
{

struct PresetFile
{
    static constexpr int kVersion = 2; // v2 adds Rig B (modelB/irB); reads v1
    static constexpr const char *kFormatTag = "nam-rig-preset";
    static constexpr const char *kExtension = ".namrig";

    juce::String name;
    juce::var params; // DynamicObject: paramID -> double (real-world value)

    // Rig A (the v1 "model"/"ir" slot).
    juce::String modelName, modelText; // empty = no model embedded
    juce::String irName;
    juce::MemoryBlock irBytes;         // empty = no IR embedded

    // Rig B (v2 only).
    juce::String modelNameB, modelTextB;
    juce::String irNameB;
    juce::MemoryBlock irBytesB;

    bool hasModel() const { return modelText.isNotEmpty(); }
    bool hasIr() const { return irBytes.getSize() > 0; }
    bool hasModelB() const { return modelTextB.isNotEmpty(); }
    bool hasIrB() const { return irBytesB.getSize() > 0; }

    // ---- serialize ----
    static juce::var modelVar(const juce::String &nm, const juce::String &nam)
    {
        auto *m = new juce::DynamicObject();
        m->setProperty("name", nm);
        m->setProperty("nam", nam);
        return juce::var(m);
    }
    static juce::var irVar(const juce::String &nm, const juce::MemoryBlock &bytes)
    {
        auto *ir = new juce::DynamicObject();
        ir->setProperty("name", nm);
        ir->setProperty("wav64", juce::Base64::toBase64(bytes.getData(), bytes.getSize()));
        return juce::var(ir);
    }

    juce::String toJson() const
    {
        auto *root = new juce::DynamicObject();
        root->setProperty("format", kFormatTag);
        root->setProperty("version", kVersion);
        root->setProperty("name", name);
        root->setProperty("params", params);
        if (hasModel())
            root->setProperty("model", modelVar(modelName, modelText));
        if (hasIr())
            root->setProperty("ir", irVar(irName, irBytes));
        if (hasModelB())
            root->setProperty("modelB", modelVar(modelNameB, modelTextB));
        if (hasIrB())
            root->setProperty("irB", irVar(irNameB, irBytesB));
        return juce::JSON::toString(juce::var(root));
    }

    bool writeToFile(const juce::File &f) const
    {
        return f.replaceWithText(toJson());
    }

    // ---- parse (returns false on anything malformed) ----
    // model object -> (name,text); returns false if present but payload empty.
    static bool readModel(const juce::var &v, juce::String &nm, juce::String &text)
    {
        if (auto *m = v.getDynamicObject())
        {
            nm = m->getProperty("name").toString();
            text = m->getProperty("nam").toString();
            if (text.isEmpty())
                return false;
        }
        return true;
    }
    static bool readIr(const juce::var &v, juce::String &nm, juce::MemoryBlock &bytes)
    {
        if (auto *ir = v.getDynamicObject())
        {
            nm = ir->getProperty("name").toString();
            juce::MemoryOutputStream decoded;
            if (!juce::Base64::convertFromBase64(decoded, ir->getProperty("wav64").toString()))
                return false;
            bytes = decoded.getMemoryBlock();
            if (bytes.getSize() == 0)
                return false;
        }
        return true;
    }

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

        // Rig A (v1 keys). Rig B (v2 keys, absent in v1 files).
        if (!readModel(root->getProperty("model"), out.modelName, out.modelText))
            return false;
        if (!readIr(root->getProperty("ir"), out.irName, out.irBytes))
            return false;
        if (!readModel(root->getProperty("modelB"), out.modelNameB, out.modelTextB))
            return false;
        if (!readIr(root->getProperty("irB"), out.irNameB, out.irBytesB))
            return false;
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
