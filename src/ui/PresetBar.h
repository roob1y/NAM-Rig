#pragma once
// PresetBar — header strip: [<][>] [preset combo] [A][B][>] [Save]. Scans the
// presets folder (Documents/NAM Rig/Presets), loads on selection, cycles with
// prev/next, saves via dialog. Right-click the combo for Rename.../Delete.
// A/B swap two in-memory full-state snapshots (params + model + IR) for quick
// tone comparison; the arrow button copies the active slot onto the other.

#include "PresetManager.h"
#include "ui/RigLookAndFeel.h"

namespace nam_rig::ui
{

// ComboBox that forwards right-clicks to a callback instead of opening its list,
// so the bar can show a Rename/Delete context menu on the preset name.
class PresetCombo : public juce::ComboBox
{
public:
    std::function<void()> onRightClick;
    void mouseDown(const juce::MouseEvent &e) override
    {
        if (e.mods.isPopupMenu())
        {
            if (onRightClick)
                onRightClick();
            return;
        }
        juce::ComboBox::mouseDown(e);
    }
};

class PresetBar : public juce::Component
{
public:
    explicit PresetBar(PresetManager &manager) : mManager(manager)
    {
        mPrev.setButtonText("<");
        mNext.setButtonText(">");
        mSave.setButtonText("Save");
        mA.setButtonText("A");
        mB.setButtonText("B");
        mCopy.setButtonText("A>B");
        for (auto *b : {&mPrev, &mNext, &mSave, &mA, &mB, &mCopy})
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
        mCombo.onRightClick = [this] { showContextMenu(); };
        mPrev.onClick = [this] { step(-1); };
        mNext.onClick = [this] { step(+1); };
        mSave.onClick = [this] { saveDialog(); };
        // Switching recalls the slot's preset identity too, so refresh the combo
        // to show that slot's name / asterisk.
        mA.onClick = [this] { mManager.abSwitch(0); updateAbButtons(); refresh(); };
        mB.onClick = [this] { mManager.abSwitch(1); updateAbButtons(); refresh(); };
        mCopy.onClick = [this] { mManager.abCopyToOther(); };

        refresh();
        updateAbButtons();
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
        mPrev.setBounds(r.removeFromLeft(22));
        r.removeFromLeft(2);
        mNext.setBounds(r.removeFromLeft(22));
        r.removeFromLeft(4);
        // Right cluster: Save | A/B compare.
        mSave.setBounds(r.removeFromRight(46));
        r.removeFromRight(4);
        mCopy.setBounds(r.removeFromRight(34));
        r.removeFromRight(2);
        mB.setBounds(r.removeFromRight(24));
        mA.setBounds(r.removeFromRight(24));
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

    // Highlight the active A/B slot and point the copy arrow at the other.
    void updateAbButtons()
    {
        const int active = mManager.abActive();
        mA.setColour(juce::TextButton::buttonColourId,
                     active == 0 ? colors::accent : colors::panel);
        mB.setColour(juce::TextButton::buttonColourId,
                     active == 1 ? colors::accent : colors::panel);
        mCopy.setButtonText(active == 0 ? "A>B" : "B>A");
    }

    void showContextMenu()
    {
        const bool haveCurrent = mManager.currentFile().existsAsFile();
        juce::PopupMenu m;
        m.addItem(1, "Rename...", haveCurrent);
        m.addItem(2, "Delete", haveCurrent);
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&mCombo),
                        [this](int choice)
                        {
                            if (choice == 1)
                                renameDialog();
                            else if (choice == 2)
                                deleteDialog();
                        });
    }

    void renameDialog()
    {
        if (!mManager.currentFile().existsAsFile())
            return;
        mAlert = std::make_unique<juce::AlertWindow>(
            "Rename preset", "New name:", juce::MessageBoxIconType::NoIcon);
        mAlert->addTextEditor("name", mManager.currentName());
        mAlert->addButton("Rename", 1, juce::KeyPress(juce::KeyPress::returnKey));
        mAlert->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        mAlert->enterModalState(
            true,
            juce::ModalCallbackFunction::create([this](int result)
                                                {
                                                    if (result == 1)
                                                    {
                                                        const auto newName =
                                                            mAlert->getTextEditorContents("name").trim();
                                                        if (newName.isNotEmpty())
                                                            mManager.renameCurrent(newName);
                                                    }
                                                    mAlert.reset();
                                                    refresh();
                                                }),
            false);
    }

    void deleteDialog()
    {
        if (!mManager.currentFile().existsAsFile())
            return;
        juce::AlertWindow::showOkCancelBox(
            juce::MessageBoxIconType::WarningIcon, "Delete preset",
            "Delete \"" + mManager.currentName() + "\"? This cannot be undone.",
            "Delete", "Cancel", this,
            juce::ModalCallbackFunction::create([this](int result)
                                                {
                                                    if (result == 1)
                                                    {
                                                        mManager.deleteCurrent();
                                                        refresh();
                                                    }
                                                }));
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
    PresetCombo mCombo;
    juce::TextButton mPrev, mNext, mSave, mA, mB, mCopy;
    juce::Array<juce::File> mFiles;
    std::unique_ptr<juce::FileChooser> mChooser;
    std::unique_ptr<juce::AlertWindow> mAlert;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresetBar)
};

} // namespace nam_rig::ui
