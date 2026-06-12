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

namespace nam_rig
{

class PresetManager
{
public:
    explicit PresetManager(NamRigProcessor &p) : mProc(p) {}

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

    bool saveToFile(const juce::File &f)
    {
        PresetFile preset;
        preset.name = f.getFileNameWithoutExtension();

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

        presetsDir().createDirectory();
        if (!preset.writeToFile(f))
            return false;
        mCurrentFile = f;
        mCurrentName = preset.name;
        return true;
    }

    bool loadFromFile(const juce::File &f)
    {
        PresetFile preset;
        if (!PresetFile::readFromFile(f, preset))
            return false;

        // Defaults first, then the file's values: params added after the
        // preset was saved keep their defaults instead of stale state.
        auto *obj = preset.params.getDynamicObject();
        for (auto *p : mProc.getParameters())
            if (auto *rp = dynamic_cast<juce::RangedAudioParameter *>(p))
            {
                float norm = rp->getDefaultValue();
                if (obj->hasProperty(rp->paramID))
                    norm = rp->convertTo0to1((float)(double)obj->getProperty(rp->paramID));
                rp->beginChangeGesture();
                rp->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, norm));
                rp->endChangeGesture();
            }

        // Embedded payloads -> temp files -> the verified load paths. Temp
        // files keep the original base names so the UI shows the right names.
        const auto tmpDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                .getChildFile("NAMRigPreset");
        tmpDir.createDirectory();
        if (preset.hasModel())
        {
            const auto tmp = tmpDir.getChildFile(sanitize(preset.modelName) + ".nam");
            if (tmp.replaceWithText(preset.modelText))
                mProc.loadModel(tmp);
        }
        if (preset.hasIr())
        {
            const auto tmp = tmpDir.getChildFile(sanitize(preset.irName) + ".wav");
            if (tmp.replaceWithData(preset.irBytes.getData(), preset.irBytes.getSize()))
                mProc.loadIr(tmp);
        }

        mCurrentFile = f;
        mCurrentName = preset.name;
        return true;
    }

private:
    static juce::String sanitize(const juce::String &s)
    {
        const auto cleaned = juce::File::createLegalFileName(s);
        return cleaned.isEmpty() ? juce::String("preset") : cleaned;
    }

    NamRigProcessor &mProc;
    juce::File mCurrentFile;
    juce::String mCurrentName;
};

} // namespace nam_rig
