#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "ui/RigLookAndFeel.h"

namespace nam_rig::ui
{

// Sortable folder pane for the library overlays. A real TreeView (nested
// subfolders stay navigable), but with a Sort combo the stock FileTreeComponent
// lacks — Name A-Z / Z-A, Newest / Oldest by modified time. Selecting a folder
// fires onFolderSelected so the file pane can list it. Defaults to Newest.
class SortableFolderTree : public juce::Component
{
public:
    enum class Sort { NameAsc = 0, NameDesc, DateNew, DateOld };

    std::function<void(const juce::File &)> onFolderSelected;

    SortableFolderTree()
    {
        addAndMakeVisible(mTree);
        mTree.setColour(juce::TreeView::backgroundColourId, juce::Colour(0xff121419));
        mTree.setRootItemVisible(false);
        mTree.setDefaultOpenness(false);
        mTree.setIndentSize(14);

        mSort.addItemList({juce::String::fromUTF8("Name A\xE2\x80\x93" "Z"),
                           juce::String::fromUTF8("Name Z\xE2\x80\x93" "A"),
                           "Newest", "Oldest"}, 1);
        mSort.setSelectedId((int)mMode + 1, juce::dontSendNotification);
        mSort.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff15181e));
        mSort.setColour(juce::ComboBox::outlineColourId, colors::cardBorder);
        mSort.onChange = [this] { mMode = (Sort)(mSort.getSelectedId() - 1); rebuild(); };
        addAndMakeVisible(mSort);
    }

    ~SortableFolderTree() override { mTree.setRootItem(nullptr); }

    void setRoot(const juce::File &root)
    {
        mRoot = root;
        rebuild();
    }

    // Sort an array of folders by the current mode (shared by the tree items).
    void sortFolders(juce::Array<juce::File> &a) const
    {
        Cmp cmp{mMode};
        a.sort(cmp);
    }
    Sort mode() const { return mMode; }
    void notifySelected(const juce::File &f) const { if (onFolderSelected) onFolderSelected(f); }

    void resized() override
    {
        auto r = getLocalBounds();
        auto top = r.removeFromTop(26);
        mSort.setBounds(top.removeFromRight(150).withSizeKeepingCentre(150, 24));
        top.removeFromRight(8);
        mSortLabelRect = top;
        r.removeFromTop(4);
        mTree.setBounds(r);
    }

    void paint(juce::Graphics &g) override
    {
        g.setColour(colors::caption);
        g.setFont(fonts::archivo(10.0f, fonts::SemiBold, 0.12f));
        g.drawText("SORT", mSortLabelRect, juce::Justification::centredRight);
    }

private:
    struct Cmp
    {
        Sort m;
        int compareElements(const juce::File &a, const juce::File &b) const
        {
            switch (m)
            {
                case Sort::NameAsc:  return a.getFileName().compareIgnoreCase(b.getFileName());
                case Sort::NameDesc: return b.getFileName().compareIgnoreCase(a.getFileName());
                default: break;
            }
            const auto ta = a.getLastModificationTime().toMilliseconds();
            const auto tb = b.getLastModificationTime().toMilliseconds();
            if (ta == tb) return a.getFileName().compareIgnoreCase(b.getFileName());
            const bool aFirst = (m == Sort::DateNew) ? (ta > tb) : (ta < tb);
            return aFirst ? -1 : 1;
        }
    };

    // One folder node. Lazily lists its (sorted) subfolders when opened.
    struct FolderItem : juce::TreeViewItem
    {
        juce::File dir;
        SortableFolderTree &owner;
        bool isRoot;
        FolderItem(juce::File d, SortableFolderTree &o, bool root) : dir(std::move(d)), owner(o), isRoot(root) {}

        // A real folder we want to show: a directory, not hidden, and not an
        // archive "compressed folder" (.zip/.rar/... which the OS presents like a
        // folder but we can't browse into).
        static bool isArchive(const juce::File &f)
        {
            const auto e = f.getFileExtension().toLowerCase();
            return e == ".zip" || e == ".rar" || e == ".7z" || e == ".tar"
                || e == ".gz" || e == ".tgz" || e == ".bz2" || e == ".xz";
        }
        static bool showable(const juce::File &f)
        {
            return f.isDirectory() && !f.isHidden() && !isArchive(f);
        }

        bool mightContainSubItems() override
        {
            if (isRoot) return true;
            for (auto &s : dir.findChildFiles(juce::File::findDirectories | juce::File::ignoreHiddenFiles, false))
                if (showable(s)) return true;
            return false;
        }
        juce::String getUniqueName() const override { return dir.getFullPathName(); }

        void itemOpennessChanged(bool nowOpen) override
        {
            if (nowOpen && getNumSubItems() == 0)
            {
                juce::Array<juce::File> subs;
                for (auto &s : dir.findChildFiles(juce::File::findDirectories | juce::File::ignoreHiddenFiles, false))
                    if (showable(s)) subs.add(s);
                owner.sortFolders(subs);
                for (auto &s : subs) addSubItem(new FolderItem(s, owner, false));
            }
        }

        void paintItem(juce::Graphics &g, int w, int h) override
        {
            if (isSelected())
            {
                g.setColour(colors::accent.withAlpha(0.16f));
                g.fillRect(0, 0, w, h);
                g.setColour(colors::accent);
                g.fillRect(0, 0, 2, h);
            }
            g.setColour(isSelected() ? colors::text : colors::text2);
            g.setFont(fonts::mono(12.0f));
            g.drawText(dir.getFileName(), 6, 0, w - 10, h, juce::Justification::centredLeft, true);
        }

        void itemSelectionChanged(bool nowSelected) override
        {
            if (nowSelected && !isRoot) owner.notifySelected(dir);
        }
    };

    void rebuild()
    {
        mTree.setRootItem(nullptr);
        mRootItem.reset();
        if (!mRoot.isDirectory()) return;
        mRootItem = std::make_unique<FolderItem>(mRoot, *this, true);
        mTree.setRootItem(mRootItem.get());
        mRootItem->setOpen(true); // populate the first level of subfolders
    }

    juce::TreeView mTree;
    juce::ComboBox mSort;
    juce::File mRoot;
    std::unique_ptr<FolderItem> mRootItem;
    Sort mMode = Sort::DateNew; // default: Newest
    juce::Rectangle<int> mSortLabelRect;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SortableFolderTree)
};

} // namespace nam_rig::ui
