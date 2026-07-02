#pragma once
#include "PluginProcessor.h"
#include "ui/RigLookAndFeel.h"
#include "ui/SortableFileList.h"
#include "ui/SortableFolderTree.h"

namespace nam_rig::ui
{

// A "load into this amp" target. Accepts the browser's internal drag (a model row
// dragged from the file list) AND .nam files dragged from the OS file manager.
class AmpDropZone : public juce::Component,
                    public juce::DragAndDropTarget,
                    public juce::FileDragAndDropTarget
{
public:
    std::function<void(const juce::File &)> onFile;
    std::function<juce::File()> getFile; // the file list's currently-selected model
    AmpDropZone(juce::String label) : mLabel(std::move(label)) {}

    void setModelName(const juce::String &n) { if (n != mModel) { mModel = n; repaint(); } }

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
        for (auto &f : files) if (looksLikeNam(f)) return true;
        return false;
    }
    void fileDragEnter(const juce::StringArray &, int, int) override { mOver = true; repaint(); }
    void fileDragExit(const juce::StringArray &) override { mOver = false; repaint(); }
    void filesDropped(const juce::StringArray &files, int, int) override
    {
        mOver = false; repaint();
        for (auto &f : files)
            if (looksLikeNam(f)) { if (onFile) onFile(juce::File(f)); break; }
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
        g.setColour(mModel.isEmpty() ? colors::captionDim : colors::text);
        g.setFont(fonts::mono(11.0f));
        g.drawText(mModel.isEmpty() ? juce::String("drop a model here") : mModel, r,
                   juce::Justification::centredLeft, true);
    }

    static bool looksLikeNam(const juce::String &path)
    {
        return path.toLowerCase().endsWith(".nam");
    }

private:
    juce::String mLabel, mModel;
    bool mOver = false;
};

// Amp-model library overlay: TWO PANES — a sortable folder tree on the left, the
// selected folder's .nam models on the right (name-filtered + sortable). Drag a
// model onto Amp A / Amp B (or drop OS files).
class AmpBrowser : public juce::Component,
                   public juce::DragAndDropContainer
{
public:
    std::function<void(const juce::File &, int rig)> onLoad; // load model into rig
    std::function<void()> onClose;

    AmpBrowser() : mZoneA("AMP A"), mZoneB("AMP B")
    {
        mFolders.onFolderSelected = [this](const juce::File &d) { mFileList.setFolder(d); };
        addAndMakeVisible(mFolders);

        mFileList.extensions = juce::StringArray{".nam"};
        mFileList.onSelChange = [this] { fileSelected(); };
        addAndMakeVisible(mFileList);

        mSearch.setTextToShowWhenEmpty(juce::String::fromUTF8("Filter by name\xE2\x80\xA6"), colors::captionDim);
        mSearch.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff15181e));
        mSearch.setColour(juce::TextEditor::outlineColourId, colors::cardBorder);
        mSearch.onTextChange = [this] { mFileList.setSearch(mSearch.getText().trim()); };
        addAndMakeVisible(mSearch);

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
        mZoneA.onFile = [this](const juce::File &f) { if (onLoad) onLoad(f, 0); mZoneA.setModelName(f.getFileNameWithoutExtension()); };
        mZoneB.onFile = [this](const juce::File &f) { if (onLoad) onLoad(f, 1); mZoneB.setModelName(f.getFileNameWithoutExtension()); };
    }

    void openFor(int rig, const juce::File &root, const juce::String &ampA, const juce::String &ampB)
    {
        mActiveRig = rig;
        mZoneA.setModelName(ampA);
        mZoneB.setModelName(ampB);
        mNoRoot = !root.isDirectory();
        if (!mNoRoot)
        {
            mFolders.setRoot(root);    // subfolders (left pane)
            mFileList.setFolder(root); // root's models (right pane)
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
        g.drawText("AMP LIBRARY", getLocalBounds().reduced(20, 0).removeFromTop(44),
                   juce::Justification::centredLeft);

        if (mNoRoot)
        {
            g.setColour(colors::captionDim);
            g.setFont(fonts::mono(12.0f));
            g.drawText("No amp folder set - click \"Change folder\" to pick your model library",
                       mBodyRect, juce::Justification::centred);
        }
        else if (mHaveSel)
        {
            g.setColour(colors::text2);
            g.setFont(fonts::mono(10.0f, fonts::SemiBold));
            g.drawText(mSelName, mHintRect, juce::Justification::centredLeft, true);
        }
        else
        {
            g.setColour(colors::captionDim);
            g.setFont(fonts::mono(10.0f));
            g.drawText("Pick a folder on the left, then drag a model onto Amp A or Amp B",
                       mHintRect, juce::Justification::centredLeft);
        }
    }

private:
    void chooseRoot()
    {
        mChooser = std::make_unique<juce::FileChooser>("Choose your amp-model library folder",
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

    // Preview the selected model name in the hint line.
    void fileSelected()
    {
        auto f = mFileList.getSelectedFile();
        if (f.existsAsFile()) { mSelName = f.getFileNameWithoutExtension(); mHaveSel = true; }
        else mHaveSel = false;
        repaint();
    }

    SortableFolderTree mFolders;          // sortable folder tree (left pane)
    SortableFileList mFileList;           // sortable models (right pane)
    juce::TextEditor mSearch;
    juce::TextButton mChooseBtn, mCloseBtn;
    AmpDropZone mZoneA, mZoneB;
    juce::Rectangle<int> mHintRect, mBodyRect;
    juce::String mSelName;
    bool mHaveSel = false;
    std::unique_ptr<juce::FileChooser> mChooser;
    std::function<juce::File()> mGetRoot;
    std::function<void(const juce::File &)> mSetRoot;
    int mActiveRig = 0;
    bool mNoRoot = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AmpBrowser)
};

} // namespace nam_rig::ui
