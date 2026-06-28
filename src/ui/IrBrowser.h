#pragma once
#include "PluginProcessor.h"
#include "ui/RigLookAndFeel.h"
#include "ui/IrTagCache.h"

namespace nam_rig::ui
{

// .wav/.aif filter for the file pane, with an optional name substring and tone
// tag (the search box + chips). Folders always pass.
class IrFileFilter : public juce::FileFilter
{
public:
    IrFileFilter() : juce::FileFilter("Impulse responses") {}
    juce::String search;      // name substring
    juce::String tagFilter;   // tone tag substring ("" = no tag filter)
    IrTagCache *cache = nullptr;
    bool isFileSuitable(const juce::File &f) const override
    {
        const auto e = f.getFileExtension().toLowerCase();
        if (e != ".wav" && e != ".aif" && e != ".aiff") return false;
        if (search.isNotEmpty() && !f.getFileName().containsIgnoreCase(search)) return false;
        // Tag check runs on the directory-scan thread and is cached, so the first
        // filtered pass analyzes (background), later ones are instant.
        if (tagFilter.isNotEmpty() && cache != nullptr && !cache->tagFor(f).containsIgnoreCase(tagFilter))
            return false;
        return true;
    }
    bool isDirectorySuitable(const juce::File &) const override { return false; } // files pane: no folders
};

// Folders-only filter for the left pane.
class DirOnlyFilter : public juce::FileFilter
{
public:
    DirOnlyFilter() : juce::FileFilter("Folders") {}
    bool isFileSuitable(const juce::File &) const override { return false; }
    bool isDirectorySuitable(const juce::File &) const override { return true; }
};

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
        juce::File f(d.description.toString());          // some builds carry the path
        if (!f.existsAsFile() && getFile) f = getFile(); // otherwise use the dragged row
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

// IR library overlay: TWO PANES — folders on the left, the selected folder's IRs
// on the right. Drag a file onto Cab A / Cab B (or drop OS files); name box + tag
// chips filter the file pane; selecting a file previews its tone tag.
class IrBrowser : public juce::Component,
                  public juce::DragAndDropContainer,
                  private juce::FileBrowserListener
{
public:
    std::function<void(const juce::File &, int rig)> onLoad; // load file into rig
    std::function<void()> onClose;

    IrBrowser()
        : mThread("ir-scan"),
          mFolderList(&mFolderFilter, mThread), mFolders(mFolderList),
          mFileContents(&mFilter, mThread), mFiles(mFileContents),
          mZoneA("CAB A"), mZoneB("CAB B")
    {
        mThread.startThread();

        addAndMakeVisible(mFolders);
        mFolders.setColour(juce::TreeView::backgroundColourId, juce::Colour(0xff121419));
        mFolderSel.onSel = [this] { folderSelected(); };
        mFolders.addListener(&mFolderSel);

        addAndMakeVisible(mFiles);
        mFiles.setColour(juce::TreeView::backgroundColourId, juce::Colour(0xff121419));
        mFiles.setDragAndDropDescription("ir"); // file rows draggable onto the cab zones
        mFiles.addListener(this);

        mSearch.setTextToShowWhenEmpty(juce::String::fromUTF8("Filter by name\xE2\x80\xA6"), colors::captionDim);
        mSearch.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff15181e));
        mSearch.setColour(juce::TextEditor::outlineColourId, colors::cardBorder);
        mSearch.onTextChange = [this] { mFilter.search = mSearch.getText().trim(); mFileContents.refresh(); };
        addAndMakeVisible(mSearch);

        // Tone-tag filter chips (cached analysis; cheap after the first pass).
        mFilter.cache = &mCache;
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
                    mFilter.tagFilter = (i == 0) ? juce::String() : mTags[i];
                    mFileContents.refresh();
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
        auto selected = [this] { return mFiles.getSelectedFile(0); };
        mZoneA.getFile = selected;
        mZoneB.getFile = selected;
        mZoneA.onFile = [this](const juce::File &f) { if (onLoad) onLoad(f, 0); mZoneA.setIrName(f.getFileNameWithoutExtension()); };
        mZoneB.onFile = [this](const juce::File &f) { if (onLoad) onLoad(f, 1); mZoneB.setIrName(f.getFileNameWithoutExtension()); };
    }

    ~IrBrowser() override
    {
        mFiles.removeListener(this);
        mFolders.removeListener(&mFolderSel);
        mThread.stopThread(2000);
    }

    void openFor(int rig, const juce::File &root, const juce::String &irA, const juce::String &irB)
    {
        mActiveRig = rig;
        mZoneA.setIrName(irA);
        mZoneB.setIrName(irB);
        mNoRoot = !root.isDirectory();
        if (!mNoRoot)
        {
            mFolderList.setDirectory(root, true, false);   // subfolders (left pane)
            mFileContents.setDirectory(root, false, true); // root's IRs (right pane)
        }
        mHaveSel = false;
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
        r.removeFromTop(4);

        // Two panes: folders (left ~38%) | files (right).
        mBodyRect = r;
        auto left = r.removeFromLeft(juce::roundToInt((float)r.getWidth() * 0.38f));
        r.removeFromLeft(12);
        mFolders.setBounds(left);
        mFiles.setBounds(r);
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
                                      mFolderList.setDirectory(dir, true, false);
                                      mFileContents.setDirectory(dir, false, true);
                                      mNoRoot = false;
                                      repaint();
                                  }
                              });
    }

    void folderSelected()
    {
        auto d = mFolders.getSelectedFile(0);
        if (d.isDirectory())
            mFileContents.setDirectory(d, false, true);
    }

    // FileBrowserListener (attached to the FILE list) — preview the selected IR.
    void selectionChanged() override
    {
        auto f = mFiles.getSelectedFile(0);
        std::array<float, nam_rig::ir::kResPts> resp{};
        if (f.existsAsFile() && IrDropZone::looksLikeIr(f.getFullPathName())
            && nam_rig::ir::analyzeFile(f, resp.data()))
        {
            mSelName = f.getFileNameWithoutExtension();
            mSelTag = nam_rig::ir::classify(resp.data());
            mHaveSel = true;
        }
        else { mHaveSel = false; mSelTag = {}; }
        repaint();
    }
    void fileClicked(const juce::File &, const juce::MouseEvent &) override {}
    void fileDoubleClicked(const juce::File &) override {} // load is drag-only
    void browserRootChanged(const juce::File &) override {}

    // Adapter so the folder tree's selection drives the file pane.
    struct FolderSel : juce::FileBrowserListener
    {
        std::function<void()> onSel;
        void selectionChanged() override { if (onSel) onSel(); }
        void fileClicked(const juce::File &, const juce::MouseEvent &) override {}
        void fileDoubleClicked(const juce::File &) override {}
        void browserRootChanged(const juce::File &) override {}
    };

    juce::TimeSliceThread mThread;
    DirOnlyFilter mFolderFilter;
    juce::DirectoryContentsList mFolderList;
    juce::FileTreeComponent mFolders;
    IrFileFilter mFilter;
    juce::DirectoryContentsList mFileContents;
    juce::FileTreeComponent mFiles;        // files-only (right pane)
    FolderSel mFolderSel;
    juce::TextEditor mSearch;
    IrTagCache mCache;
    juce::OwnedArray<juce::ToggleButton> mTagChips;
    juce::StringArray mTags;
    juce::TextButton mChooseBtn, mCloseBtn;
    IrDropZone mZoneA, mZoneB;
    juce::Rectangle<int> mHintRect, mBodyRect;
    juce::String mSelName, mSelTag;
    bool mHaveSel = false;
    std::unique_ptr<juce::FileChooser> mChooser;
    std::function<juce::File()> mGetRoot;
    std::function<void(const juce::File &)> mSetRoot;
    int mActiveRig = 0;
    bool mNoRoot = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IrBrowser)
};

} // namespace nam_rig::ui
