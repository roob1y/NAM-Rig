#pragma once
#include "PluginProcessor.h"
#include "ui/RigLookAndFeel.h"
#include "ui/IrGraph.h"
#include "ui/IrTagCache.h"
#include "ui/SortableFileList.h"
#include "ui/SortableFolderTree.h"

namespace nam_rig::ui
{

// A "load into this cab" target. Accepts the browser's internal drag (a file row
// dragged from the file list) AND files dragged from the OS file manager.
class IrDropZone : public juce::Component,
                   public juce::DragAndDropTarget,
                   public juce::FileDragAndDropTarget
{
public:
    std::function<void(const juce::File &)> onFile;
    std::function<juce::File()> getFile; // the file list's currently-selected file
    IrDropZone(juce::String label) : mLabel(std::move(label)) {}

    void setIrName(const juce::String &n) { if (n != mIr) { mIr = n; repaint(); } }

    // --- internal drag (a row dragged out of the file list) ---
    bool isInterestedInDragSource(const SourceDetails &) override
    {
        return getFile && getFile().existsAsFile();
    }
    void itemDragEnter(const SourceDetails &) override { mOver = true; repaint(); }
    void itemDragExit(const SourceDetails &) override { mOver = false; repaint(); }
    void itemDropped(const SourceDetails &d) override
    {
        mOver = false; repaint();
        juce::File f(d.description.toString());          // the row carries its path
        if (!f.existsAsFile() && getFile) f = getFile(); // otherwise use the selection
        if (f.existsAsFile() && onFile) onFile(f);
    }

    // --- external OS file drag ---
    bool isInterestedInFileDrag(const juce::StringArray &files) override
    {
        for (auto &f : files) if (looksLikeIr(f)) return true;
        return false;
    }
    void fileDragEnter(const juce::StringArray &, int, int) override { mOver = true; repaint(); }
    void fileDragExit(const juce::StringArray &) override { mOver = false; repaint(); }
    void filesDropped(const juce::StringArray &files, int, int) override
    {
        mOver = false; repaint();
        for (auto &f : files)
            if (looksLikeIr(f)) { if (onFile) onFile(juce::File(f)); break; }
    }

    void paint(juce::Graphics &g) override
    {
        auto b = getLocalBounds().toFloat().reduced(1.0f);
        g.setColour(mOver ? colors::accent.withAlpha(0.18f) : juce::Colour(0xff181b21));
        g.fillRoundedRectangle(b, 9.0f);
        g.setColour(mOver ? colors::accent : colors::cardBorder);
        g.drawRoundedRectangle(b, 9.0f, mOver ? 1.5f : 1.0f);
        auto r = getLocalBounds().reduced(12, 6);
        g.setColour(colors::caption);
        g.setFont(fonts::archivo(10.0f, fonts::SemiBold, 0.12f));
        g.drawText(mLabel, r.removeFromTop(14), juce::Justification::centredLeft);
        g.setColour(mIr.isEmpty() ? colors::captionDim : colors::text);
        g.setFont(fonts::mono(11.0f));
        g.drawText(mIr.isEmpty() ? juce::String("drop an IR here") : mIr, r,
                   juce::Justification::centredLeft, true);
    }

    static bool looksLikeIr(const juce::String &path)
    {
        const auto l = path.toLowerCase();
        return l.endsWith(".wav") || l.endsWith(".aif") || l.endsWith(".aiff");
    }

private:
    juce::String mLabel, mIr;
    bool mOver = false;
};

// IR library overlay: TWO PANES — a sortable folder tree on the left, the
// selected folder's IRs on the right (name-filtered, tone-tag-filtered and
// sortable). Drag a file onto Cab A / Cab B (or drop OS files); selecting a file
// previews its tone tag.
class IrBrowser : public juce::Component,
                  public juce::DragAndDropContainer
{
public:
    std::function<void(const juce::File &, int rig)> onLoad; // load file into rig
    std::function<void()> onClose;

    IrBrowser() : mZoneA("CAB A"), mZoneB("CAB B")
    {
        mFolders.onFolderSelected = [this](const juce::File &d) { mFileList.setFolder(d); };
        addAndMakeVisible(mFolders);

        mFileList.extensions = juce::StringArray{".wav", ".aif", ".aiff"};
        mFileList.onSelChange = [this] { fileSelected(); };
        // Tone-tag chip filter (cached analysis; cheap after the first pass, and
        // only runs when a non-"All" chip is active).
        mFileList.extraFilter = [this](const juce::File &f) {
            return mTagFilter.isEmpty() || mCache.tagFor(f).containsIgnoreCase(mTagFilter);
        };
        addAndMakeVisible(mFileList);

        mSearch.setTextToShowWhenEmpty(juce::String::fromUTF8("Filter by name\xE2\x80\xA6"), colors::captionDim);
        mSearch.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff15181e));
        mSearch.setColour(juce::TextEditor::outlineColourId, colors::cardBorder);
        mSearch.onTextChange = [this] { mFileList.setSearch(mSearch.getText().trim()); };
        addAndMakeVisible(mSearch);

        // Tone-tag filter chips.
        mTags = juce::StringArray{"All", "Dark", "Bright", "Scooped", "Thick", "Present", "Fizzy"};
        for (int i = 0; i < mTags.size(); ++i)
        {
            auto *c = new juce::ToggleButton(mTags[i]);
            c->setButtonText(mTags[i]);
            c->getProperties().set("pill", true);
            c->setRadioGroupId(7001);
            c->setClickingTogglesState(true);
            c->onClick = [this, i] {
                if (mTagChips[i]->getToggleState())
                {
                    mTagFilter = (i == 0) ? juce::String() : mTags[i];
                    mFileList.refresh();
                }
            };
            addAndMakeVisible(c);
            mTagChips.add(c);
        }
        mTagChips[0]->setToggleState(true, juce::dontSendNotification); // "All"

        mChooseBtn.setButtonText(juce::String::fromUTF8("Change folder\xE2\x80\xA6"));
        mChooseBtn.onClick = [this] { chooseRoot(); };
        addAndMakeVisible(mChooseBtn);
        mCloseBtn.setButtonText("Close");
        mCloseBtn.onClick = [this] { if (onClose) onClose(); };
        addAndMakeVisible(mCloseBtn);

        addAndMakeVisible(mZoneA);
        addAndMakeVisible(mZoneB);
        auto selected = [this] { return mFileList.getSelectedFile(); };
        mZoneA.getFile = selected;
        mZoneB.getFile = selected;
        mZoneA.onFile = [this](const juce::File &f) { if (onLoad) onLoad(f, 0); mZoneA.setIrName(f.getFileNameWithoutExtension()); };
        mZoneB.onFile = [this](const juce::File &f) { if (onLoad) onLoad(f, 1); mZoneB.setIrName(f.getFileNameWithoutExtension()); };
    }

    void openFor(int rig, const juce::File &root, const juce::String &irA, const juce::String &irB)
    {
        mActiveRig = rig;
        mZoneA.setIrName(irA);
        mZoneB.setIrName(irB);
        mNoRoot = !root.isDirectory();
        if (!mNoRoot)
        {
            mFolders.setRoot(root);    // subfolders (left pane)
            mFileList.setFolder(root); // root's IRs (right pane)
        }
        mHaveSel = false;
        mFileList.clearSelection();
        setVisible(true);
        toFront(true);
        repaint();
    }

    void setRootChooser(std::function<juce::File()> get, std::function<void(const juce::File &)> set)
    {
        mGetRoot = std::move(get);
        mSetRoot = std::move(set);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced(16, 14);
        auto top = r.removeFromTop(30);
        mCloseBtn.setBounds(top.removeFromRight(84).withSizeKeepingCentre(84, 26));
        top.removeFromRight(8);
        mChooseBtn.setBounds(top.removeFromRight(140).withSizeKeepingCentre(140, 26));
        r.removeFromTop(8);

        mSearch.setBounds(r.removeFromTop(30));
        r.removeFromTop(8);

        auto chips = r.removeFromTop(24);
        for (auto *c : mTagChips)
        {
            const int w = 22 + c->getButtonText().length() * 7;
            c->setBounds(chips.removeFromLeft(w));
            chips.removeFromLeft(6);
        }
        r.removeFromTop(8);

        auto zones = r.removeFromTop(50);
        const int gap = 12;
        mZoneA.setBounds(zones.removeFromLeft((zones.getWidth() - gap) / 2));
        zones.removeFromLeft(gap);
        mZoneB.setBounds(zones);
        r.removeFromTop(6);
        mHintRect = r.removeFromTop(14);
        r.removeFromTop(6);

        // Frequency-curve preview of the selected IR, pinned full-width to the
        // bottom: a caption row above the response well.
        auto preview = r.removeFromBottom(120);
        r.removeFromBottom(10);
        mPreviewLabelRect = preview.removeFromTop(14);
        mPreviewRect = preview;

        // Two panes: folders (left ~38%) | files (right).
        mBodyRect = r;
        auto left = r.removeFromLeft(juce::roundToInt((float)r.getWidth() * 0.38f));
        r.removeFromLeft(12);
        mFolders.setBounds(left);
        mFileList.setBounds(r);
    }

    void paint(juce::Graphics &g) override
    {
        auto b = getLocalBounds().toFloat().reduced(0.5f);
        g.setColour(colors::panel);
        g.fillRoundedRectangle(b, 11.0f);
        g.setColour(colors::outline);
        g.drawRoundedRectangle(b, 11.0f, 1.0f);

        g.setColour(colors::titleAccent);
        g.setFont(fonts::archivo(13.0f, fonts::Bold, 0.15f));
        g.drawText("IR LIBRARY", getLocalBounds().reduced(20, 0).removeFromTop(44),
                   juce::Justification::centredLeft);

        if (mNoRoot)
        {
            g.setColour(colors::captionDim);
            g.setFont(fonts::mono(12.0f));
            g.drawText("No IR folder set - click \"Change folder\" to pick your IR library",
                       mBodyRect, juce::Justification::centred);
        }
        else if (mHaveSel)
        {
            g.setColour(colors::text2);
            g.setFont(fonts::mono(10.0f, fonts::SemiBold));
            g.drawText(mSelName + juce::String::fromUTF8("  \xE2\x80\x94  ") + mSelTag, mHintRect,
                       juce::Justification::centredLeft, true);
        }
        else
        {
            g.setColour(colors::captionDim);
            g.setFont(fonts::mono(10.0f));
            g.drawText("Pick a folder on the left, then drag an IR onto Cab A or Cab B",
                       mHintRect, juce::Justification::centredLeft);
        }

        // Preview well: the selected IR's frequency curve, drawn with the same
        // painter the loaded cab uses, so it looks identical once dropped on a
        // cab. Empty prompt until a file is picked.
        g.setColour(colors::caption);
        g.setFont(fonts::archivo(10.0f, fonts::SemiBold, 0.12f));
        g.drawText("FREQUENCY RESPONSE", mPreviewLabelRect, juce::Justification::centredLeft);
        if (mHaveSel)
        {
            g.setColour(colors::text2);
            g.setFont(fonts::mono(10.0f, fonts::SemiBold));
            g.drawText(mSelTag, mPreviewLabelRect, juce::Justification::centredRight, true);
            drawIrResponse(g, mPreviewRect.toFloat(), mSelResp.data(), true, {});
        }
        else
        {
            drawIrResponse(g, mPreviewRect.toFloat(), mSelResp.data(), false,
                           juce::String::fromUTF8("Select an IR to preview its curve"));
        }
    }

private:
    void chooseRoot()
    {
        mChooser = std::make_unique<juce::FileChooser>("Choose your IR library folder",
                                                       mGetRoot ? mGetRoot() : juce::File{});
        mChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [this](const juce::FileChooser &fc)
                              {
                                  auto dir = fc.getResult();
                                  if (dir.isDirectory())
                                  {
                                      if (mSetRoot) mSetRoot(dir);
                                      mFolders.setRoot(dir);
                                      mFileList.setFolder(dir);
                                      mNoRoot = false;
                                      repaint();
                                  }
                              });
    }

    // Preview the selected IR's tone tag in the hint line AND its frequency curve
    // in the bottom well (same graph the cab shows once the file is loaded).
    void fileSelected()
    {
        auto f = mFileList.getSelectedFile();
        if (f.existsAsFile() && IrDropZone::looksLikeIr(f.getFullPathName())
            && nam_rig::ir::analyzeFile(f, mSelResp.data()))
        {
            mSelName = f.getFileNameWithoutExtension();
            mSelTag = nam_rig::ir::classify(mSelResp.data());
            mHaveSel = true;
        }
        else { mHaveSel = false; mSelTag = {}; }
        repaint();
    }

    SortableFolderTree mFolders;          // sortable folder tree (left pane)
    SortableFileList mFileList;           // sortable IRs (right pane)
    juce::TextEditor mSearch;
    IrTagCache mCache;
    juce::String mTagFilter;              // active tone-tag chip ("" = All)
    juce::OwnedArray<juce::ToggleButton> mTagChips;
    juce::StringArray mTags;
    juce::TextButton mChooseBtn, mCloseBtn;
    IrDropZone mZoneA, mZoneB;
    juce::Rectangle<int> mHintRect, mBodyRect, mPreviewRect, mPreviewLabelRect;
    juce::String mSelName, mSelTag;
    std::array<float, nam_rig::ir::kResPts> mSelResp{}; // selected IR's response curve
    bool mHaveSel = false;
    std::unique_ptr<juce::FileChooser> mChooser;
    std::function<juce::File()> mGetRoot;
    std::function<void(const juce::File &)> mSetRoot;
    int mActiveRig = 0;
    bool mNoRoot = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IrBrowser)
};

} // namespace nam_rig::ui
