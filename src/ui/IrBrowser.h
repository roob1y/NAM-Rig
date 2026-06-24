#pragma once
#include "PluginProcessor.h"
#include "ui/RigLookAndFeel.h"

namespace nam_rig::ui
{

// .wav/.aif filter with an optional live name substring (the search box). Folders
// always pass so the user can navigate the whole tree at any depth.
class IrFileFilter : public juce::FileFilter
{
public:
    IrFileFilter() : juce::FileFilter("Impulse responses") {}
    juce::String search;
    bool isFileSuitable(const juce::File &f) const override
    {
        const auto e = f.getFileExtension().toLowerCase();
        if (e != ".wav" && e != ".aif" && e != ".aiff") return false;
        return search.isEmpty() || f.getFileName().containsIgnoreCase(search);
    }
    bool isDirectorySuitable(const juce::File &) const override { return true; }
};

// A "load into this cab" target. Accepts the browser's internal drag (a file path
// in the drag description) AND files dragged from the OS file manager.
class IrDropZone : public juce::Component,
                   public juce::DragAndDropTarget,
                   public juce::FileDragAndDropTarget
{
public:
    std::function<void(const juce::File &)> onFile;
    std::function<juce::File()> getFile; // the tree's currently-selected file
    IrDropZone(juce::String label) : mLabel(std::move(label)) {}

    void setIrName(const juce::String &n) { if (n != mIr) { mIr = n; repaint(); } }

    // --- internal drag (a row dragged out of the folder tree) ---
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

// IR library overlay: a folder tree over a user-chosen root + a name filter, with
// Cab A / Cab B drop targets. Drag the selected-IR chip onto a cab, double-click a
// file to load it into the cab you opened this from, or drop files from the OS.
class IrBrowser : public juce::Component,
                  public juce::DragAndDropContainer,
                  private juce::FileBrowserListener
{
public:
    std::function<void(const juce::File &, int rig)> onLoad; // load file into rig
    std::function<void()> onClose;

    IrBrowser()
        : mThread("ir-scan"), mList(&mFilter, mThread), mTree(mList),
          mZoneA("CAB A"), mZoneB("CAB B")
    {
        mThread.startThread();

        addAndMakeVisible(mTree);
        mTree.addListener(this);
        mTree.setColour(juce::TreeView::backgroundColourId, juce::Colour(0xff121419));
        mTree.setDragAndDropDescription("ir"); // makes tree rows draggable onto the cab zones

        mSearch.setTextToShowWhenEmpty(juce::String::fromUTF8("Filter by name\xE2\x80\xA6"), colors::captionDim);
        mSearch.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff15181e));
        mSearch.setColour(juce::TextEditor::outlineColourId, colors::cardBorder);
        mSearch.onTextChange = [this] { mFilter.search = mSearch.getText().trim(); mList.refresh(); };
        addAndMakeVisible(mSearch);

        mChooseBtn.setButtonText(juce::String::fromUTF8("Change folder\xE2\x80\xA6"));
        mChooseBtn.onClick = [this] { chooseRoot(); };
        addAndMakeVisible(mChooseBtn);

        mCloseBtn.setButtonText("Close");
        mCloseBtn.onClick = [this] { if (onClose) onClose(); };
        addAndMakeVisible(mCloseBtn);

        addAndMakeVisible(mZoneA);
        addAndMakeVisible(mZoneB);
        auto selected = [this] { return mTree.getSelectedFile(0); };
        mZoneA.getFile = selected;
        mZoneB.getFile = selected;
        mZoneA.onFile = [this](const juce::File &f) { if (onLoad) onLoad(f, 0); mZoneA.setIrName(f.getFileNameWithoutExtension()); };
        mZoneB.onFile = [this](const juce::File &f) { if (onLoad) onLoad(f, 1); mZoneB.setIrName(f.getFileNameWithoutExtension()); };
    }

    ~IrBrowser() override
    {
        mTree.removeListener(this);
        mThread.stopThread(2000);
    }

    // Open over the rig that asked, syncing the drop zones with what's loaded.
    void openFor(int rig, const juce::File &root,
                 const juce::String &irA, const juce::String &irB)
    {
        mActiveRig = rig;
        mZoneA.setIrName(irA);
        mZoneB.setIrName(irB);
        if (root.isDirectory())
        {
            mList.setDirectory(root, true, true);
            mNoRoot = false;
        }
        else
        {
            mNoRoot = true;
        }
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
        // title drawn left in paint
        r.removeFromTop(8);

        mSearch.setBounds(r.removeFromTop(30));
        r.removeFromTop(10);

        auto zones = r.removeFromTop(50);
        const int gap = 12;
        mZoneA.setBounds(zones.removeFromLeft((zones.getWidth() - gap) / 2));
        zones.removeFromLeft(gap);
        mZoneB.setBounds(zones);
        r.removeFromTop(6);
        mHintRect = r.removeFromTop(14); // "drag a file onto a cab" hint
        r.removeFromTop(4);

        mTree.setBounds(r);
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
                       mTree.getBounds(), juce::Justification::centred);
        }
        else
        {
            g.setColour(colors::captionDim);
            g.setFont(fonts::mono(10.0f));
            g.drawText("Drag a file onto a cab above, or double-click to load",
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
                                      mList.setDirectory(dir, true, true);
                                      mNoRoot = false;
                                      repaint();
                                  }
                              });
    }

    // FileBrowserListener
    void selectionChanged() override {} // drag/double-click read the selection live
    void fileClicked(const juce::File &, const juce::MouseEvent &) override {}
    void fileDoubleClicked(const juce::File &f) override
    {
        if (f.existsAsFile() && IrDropZone::looksLikeIr(f.getFullPathName()) && onLoad)
        {
            onLoad(f, mActiveRig);
            (mActiveRig == 0 ? mZoneA : mZoneB).setIrName(f.getFileNameWithoutExtension());
            if (onClose) onClose();
        }
    }
    void browserRootChanged(const juce::File &) override {}

    juce::TimeSliceThread mThread;
    IrFileFilter mFilter;
    juce::DirectoryContentsList mList;
    juce::FileTreeComponent mTree;
    juce::TextEditor mSearch;
    juce::TextButton mChooseBtn, mCloseBtn;
    IrDropZone mZoneA, mZoneB;
    juce::Rectangle<int> mHintRect;
    std::unique_ptr<juce::FileChooser> mChooser;
    std::function<juce::File()> mGetRoot;
    std::function<void(const juce::File &)> mSetRoot;
    int mActiveRig = 0;
    bool mNoRoot = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IrBrowser)
};

} // namespace nam_rig::ui
