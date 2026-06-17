#pragma once
// PresetManager — message-thread glue between PresetFile and the processor.
// Save: snapshot every APVTS parameter (real-world values) + the cached
// model text / IR bytes. Load: reset params to defaults, apply the file's
// values, write embedded payloads to temp files and run them through the
// SAME loadModel/loadIr paths the file pickers use.
//
// Presets live in Documents/NAM Rig/Presets (created on first save); the
// editor's preset bar scans this folder.

#include "PluginProcessor.h"
#include "PresetFile.h"
#include "FactoryPresets.h"

namespace nam_rig
{

class PresetManager
{
public:
    explicit PresetManager(NamRigProcessor &p) : mProc(p) { installFactory(); }

    static juce::File presetsDir()
    {
        return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
            .getChildFile("NAM Rig")
            .getChildFile("Presets");
    }

    juce::Array<juce::File> scan() const
    {
        auto files = presetsDir().findChildFiles(juce::File::findFiles, false,
                                                 "*" + juce::String(PresetFile::kExtension));
        files.sort();
        return files;
    }

    const juce::String &currentName() const { return mCurrentName; }
    const juce::File &currentFile() const { return mCurrentFile; }

    // True when any parameter differs from the loaded/saved preset snapshot.
    bool isModified() const
    {
        auto *obj = mLoadedParams.getDynamicObject();
        if (mCurrentName.isEmpty() || obj == nullptr)
            return false;
        for (auto *p : mProc.getParameters())
            if (auto *rp = dynamic_cast<juce::RangedAudioParameter *>(p))
            {
                const double now = (double)rp->convertFrom0to1(rp->getValue());
                const double ref = obj->hasProperty(rp->paramID)
                                       ? (double)obj->getProperty(rp->paramID)
                                       : (double)rp->convertFrom0to1(rp->getDefaultValue());
                if (std::abs(now - ref) > 1.0e-4)
                    return true;
            }
        return false;
    }

    // Snapshot the live rig into an in-memory PresetFile (params + embedded
    // model text + IR bytes). Shared by saveToFile and the A/B slots.
    PresetFile captureState() const
    {
        PresetFile preset;
        preset.name = mCurrentName;

        auto *params = new juce::DynamicObject();
        for (auto *p : mProc.getParameters())
            if (auto *rp = dynamic_cast<juce::RangedAudioParameter *>(p))
                params->setProperty(rp->paramID,
                                    (double)rp->convertFrom0to1(rp->getValue()));
        preset.params = juce::var(params);

        if (mProc.isModelLoaded() && mProc.modelText().isNotEmpty())
        {
            preset.modelName = mProc.modelBaseName();
            preset.modelText = mProc.modelText();
        }
        if (mProc.isIrLoaded() && mProc.irBytes().getSize() > 0)
        {
            preset.irName = mProc.irBaseName();
            preset.irBytes = mProc.irBytes();
        }
        if (mProc.isModelLoaded(1) && mProc.modelText(1).isNotEmpty())
        {
            preset.modelNameB = mProc.modelBaseName(1);
            preset.modelTextB = mProc.modelText(1);
        }
        if (mProc.isIrLoaded(1) && mProc.irBytes(1).getSize() > 0)
        {
            preset.irNameB = mProc.irBaseName(1);
            preset.irBytesB = mProc.irBytes(1);
        }
        return preset;
    }

    // Apply an in-memory state to the live rig: defaults first (so params added
    // after the snapshot keep their defaults), then the snapshot's values, then
    // its embedded model/IR through the SAME loadModel/loadIr paths the file
    // pickers use (temp files keep the original base names for the UI).
    void applyState(const PresetFile &preset)
    {
        auto *obj = preset.params.getDynamicObject();
        // Suppress the mod type-change knob reset while we apply saved values, so
        // a slot's saved knobs aren't wiped when its Type param is set.
        mProc.beginStateLoad();
        for (auto *p : mProc.getParameters())
            if (auto *rp = dynamic_cast<juce::RangedAudioParameter *>(p))
            {
                float norm = rp->getDefaultValue();
                if (obj != nullptr && obj->hasProperty(rp->paramID))
                    norm = rp->convertTo0to1((float)(double)obj->getProperty(rp->paramID));
                rp->beginChangeGesture();
                rp->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, norm));
                rp->endChangeGesture();
            }
        mProc.endStateLoadDeferred();

        const auto tmpDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                .getChildFile("NAMRigPreset");
        tmpDir.createDirectory();
        // Only reload the model/IR when it actually differs from what's live.
        // A model reload rebuilds every 1x..32x dilation copy (~0.3 s / 17 MB at
        // 32x), so same-amp A/B must stay instant — compare the embedded payload
        // to the cached source and skip the round-trip when unchanged.
        if (preset.hasModel())
        {
            if (!mProc.isModelLoaded() || preset.modelText != mProc.modelText())
            {
                const auto tmp = tmpDir.getChildFile(sanitize(preset.modelName) + ".nam");
                if (tmp.replaceWithText(preset.modelText))
                    mProc.loadModel(tmp);
            }
        }
        if (preset.hasIr())
        {
            if (!mProc.isIrLoaded() || preset.irBytes != mProc.irBytes())
            {
                const auto tmp = tmpDir.getChildFile(sanitize(preset.irName) + ".wav");
                if (tmp.replaceWithData(preset.irBytes.getData(), preset.irBytes.getSize()))
                    mProc.loadIr(tmp);
            }
        }
        if (preset.hasModelB())
        {
            if (!mProc.isModelLoaded(1) || preset.modelTextB != mProc.modelText(1))
            {
                const auto tmp = tmpDir.getChildFile(sanitize(preset.modelNameB) + "_B.nam");
                if (tmp.replaceWithText(preset.modelTextB))
                    mProc.loadModel(tmp, 1);
            }
        }
        if (preset.hasIrB())
        {
            if (!mProc.isIrLoaded(1) || preset.irBytesB != mProc.irBytes(1))
            {
                const auto tmp = tmpDir.getChildFile(sanitize(preset.irNameB) + "_B.wav");
                if (tmp.replaceWithData(preset.irBytesB.getData(), preset.irBytesB.getSize()))
                    mProc.loadIr(tmp, 1);
            }
        }
    }

    bool saveToFile(const juce::File &f)
    {
        PresetFile preset = captureState();
        preset.name = f.getFileNameWithoutExtension();

        presetsDir().createDirectory();
        if (!preset.writeToFile(f))
            return false;
        mCurrentFile = f;
        mCurrentName = preset.name;
        mLoadedParams = preset.params; // snapshot for the modified indicator
        return true;
    }

    bool loadFromFile(const juce::File &f)
    {
        PresetFile preset;
        if (!PresetFile::readFromFile(f, preset))
            return false;

        applyState(preset);

        mCurrentFile = f;
        mCurrentName = preset.name;
        mLoadedParams = preset.params; // snapshot for the modified indicator
        return true;
    }

    // ---- Rename / delete the current preset file (message thread) ----
    // Rename keeps the preset selected; returns false on no current file, an
    // illegal name, or a name collision with another preset.
    bool renameCurrent(const juce::String &newName)
    {
        if (!mCurrentFile.existsAsFile())
            return false;
        const auto clean = sanitize(newName);
        const auto target = presetsDir().getChildFile(clean + PresetFile::kExtension);
        if (target == mCurrentFile)
            return true; // no-op
        if (target.existsAsFile())
            return false; // would clobber another preset
        if (!mCurrentFile.moveFileTo(target))
            return false;
        // Keep the embedded "name" field in sync with the file name.
        PresetFile pf;
        if (PresetFile::readFromFile(target, pf))
        {
            pf.name = clean;
            pf.writeToFile(target);
        }
        mCurrentFile = target;
        mCurrentName = clean;
        return true;
    }

    bool deleteCurrent()
    {
        if (!mCurrentFile.existsAsFile())
            return false;
        if (!mCurrentFile.deleteFile())
            return false;
        mCurrentFile = juce::File{};
        mCurrentName = {};
        mLoadedParams = juce::var{};
        return true;
    }

private:
    static juce::String sanitize(const juce::String &s)
    {
        const auto cleaned = juce::File::createLegalFileName(s);
        return cleaned.isEmpty() ? juce::String("preset") : cleaned;
    }

    // Write the built-in factory presets to the presets folder once (guarded by
    // a marker so a user can delete one without it coming back). Params-only:
    // they apply over whatever amps are loaded.
    void installFactory()
    {
        const auto marker = presetsDir().getChildFile(".factory_v1");
        if (marker.existsAsFile())
            return;
        presetsDir().createDirectory();
        for (const auto &f : FactoryPresets::all())
        {
            const auto file = presetsDir().getChildFile(sanitize(f.name) + PresetFile::kExtension);
            if (!file.existsAsFile())
                f.writeToFile(file);
        }
        marker.replaceWithText("nam-rig factory v1");
    }

    NamRigProcessor &mProc;
    juce::File mCurrentFile;
    juce::String mCurrentName;
    juce::var mLoadedParams;
};

} // namespace nam_rig
