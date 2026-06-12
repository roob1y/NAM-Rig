#pragma once
// PresetBar — header strip: [<] [preset combo] [>] [Save]. Scans the
// presets folder (Documents/NAM Rig/Presets), loads on selection, cycles
// with prev/next, saves via dialog defaulting into the folder.

#include "PresetManager.h"
#include "ui/RigLookAndFeel.h"

namespace nam_rig::ui
{

class PresetBar : public juce::Component
{
public:
    explicit PresetBar(PresetManager &manager) : mManager(manager)
    {
        mPrev.setButtonText("<");
        mNext.setButtonText(">");
        mSave.setButtonText("Save");
        for (auto *b : {&mPrev, &mNext, &mSave})
            addAndMakeVisible(*b);

        mCombo.setTextWhenNothingSelected("Presets...");
        mCombo.setTextWhenNoChoicesAvailable("No presets yet");
        addAndMakeVisible(mCombo);

        mCombo.onChange = [this]
        {
            const int idx = mCombo.getSelectedId() - 1;
            if (idx >= 0 && idx < mFiles.size())
                load(mFiles[idx]);
        };
        mPrev.onClick = [this] { step(-1); };
        mNext.onClick = [this] { step(+1); };
        mSave.onClick = [this] { saveDialog(); };

        refresh();
    }

    // Called from the editor timer (cheap if the folder hasn't changed).
    void refresh()
    {
        auto files = mManager.scan();
        if (files != mFiles)
        {
            mFiles = std::move(files);
            mCombo.clear(juce::dontSendNotification);
            for (int i = 0; i < mFiles.size(); ++i)
                mCombo.addItem(mFiles[i].getFileNameWithoutExtension(), i + 1);
        }
        // Reflect the current preset without firing a (re)load.
        const int current = mFiles.indexOf(mManager.currentFile());
        if (current >= 0)
            mCombo.setSelectedId(current + 1, juce::dontSendNotification);
        else if (mCombo.getSelectedId() != 0)
            mCombo.setSelectedId(0, juce::dontSendNotification);
    }

    // Called every editor tick (no folder scan): " *" suffix while the rig
    // differs from the loaded preset.
    void updateDirty()
    {
        const int current = mFiles.indexOf(mManager.currentFile());
        if (current < 0)
            return;
        const auto base = mFiles[current].getFileNameWithoutExtension();
        const auto want = mManager.isModified() ? base + " *" : base;
        if (mCombo.getText() != want)
            mCombo.setText(want, juce::dontSendNotification);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        mPrev.setBounds(r.removeFromLeft(24));
        r.removeFromLeft(4);
        mSave.setBounds(r.removeFromRight(52));
        r.removeFromRight(4);
        mNext.setBounds(r.removeFromRight(24));
        r.removeFromRight(4);
        mCombo.setBounds(r);
    }

private:
    void load(const juce::File &f)
    {
        if (!mManager.loadFromFile(f))
            mCombo.setSelectedId(0, juce::dontSendNotification); // bad file
        refresh();
    }

    void step(int delta)
    {
        if (mFiles.isEmpty())
            return;
        const int current = mFiles.indexOf(mManager.currentFile());
        const int n = mFiles.size();
        const int next = current < 0 ? (delta > 0 ? 0 : n - 1)
                                     : (current + delta + n) % n;
        load(mFiles[next]);
    }

    void saveDialog()
    {
        PresetManager::presetsDir().createDirectory();
        const auto defaultName =
            mManager.currentName().isNotEmpty() ? mManager.currentName()
                                                : juce::String("MyRig");
        mChooser = std::make_unique<juce::FileChooser>(
            "Save rig preset",
            PresetManager::presetsDir().getChildFile(defaultName + PresetFile::kExtension),
            "*" + juce::String(PresetFile::kExtension));
        mChooser->launchAsync(juce::FileBrowserComponent::saveMode |
                                  juce::FileBrowserComponent::canSelectFiles |
                                  juce::FileBrowserComponent::warnAboutOverwriting,
                              [this](const juce::FileChooser &fc)
                              {
                                  auto f = fc.getResult();
                                  if (f == juce::File{})
                                      return;
                                  if (!f.hasFileExtension(
                                          juce::String(PresetFile::kExtension).substring(1)))
                                      f = f.withFileExtension(PresetFile::kExtension);
                                  mManager.saveToFile(f);
                                  refresh();
                              });
    }

    PresetManager &mManager;
    juce::ComboBox mCombo;
    juce::TextButton mPrev, mNext, mSave;
    juce::Array<juce::File> mFiles;
    std::unique_ptr<juce::FileChooser> mChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresetBar)
};

} // namespace nam_rig::ui
