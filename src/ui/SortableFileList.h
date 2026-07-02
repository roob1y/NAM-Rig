#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "ui/RigLookAndFeel.h"

namespace nam_rig::ui
{

// A flat, sortable file pane for the library overlays (replaces JUCE's
// FileTreeComponent, which has no sort control). Lists one folder's files that
// match `extensions`, filtered by a name substring and an optional predicate,
// ordered by a Sort combo (Name A-Z / Z-A, Newest / Oldest by modified time).
// Rows are draggable onto the drop zones via getDragSourceDescription (the file
// path), matching the zones' existing drop handling.
class SortableFileList : public juce::Component, private juce::ListBoxModel
{
public:
    enum class Sort { NameAsc = 0, NameDesc, DateNew, DateOld };

    // Configure before use.
    juce::StringArray extensions;                              // lowercase, incl dot
    std::function<void()> onSelChange;                        // selection changed
    std::function<bool(const juce::File &)> extraFilter;      // optional (e.g. tone tag)

    SortableFileList()
    {
        addAndMakeVisible(mList);
        mList.setModel(this);
        mList.setRowHeight(22);
        mList.setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff121419));
        mList.setColour(juce::ListBox::outlineColourId, colors::cardBorder);
        mList.setOutlineThickness(1);

        mSort.addItemList({juce::String::fromUTF8("Name A\xE2\x80\x93" "Z"),
                           juce::String::fromUTF8("Name Z\xE2\x80\x93" "A"),
                           "Newest", "Oldest"}, 1);
        mSort.setSelectedId(1, juce::dontSendNotification);
        mSort.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff15181e));
        mSort.setColour(juce::ComboBox::outlineColourId, colors::cardBorder);
        mSort.onChange = [this] {
            mMode = (Sort)(mSort.getSelectedId() - 1);
            resort(getSelectedFile());
        };
        addAndMakeVisible(mSort);
    }

    void setFolder(const juce::File &dir) { mFolder = dir; rescan(); }
    void setSearch(const juce::String &s) { mSearch = s; rescan(); }
    void refresh() { rescan(); } // re-apply filters (e.g. after a tag chip change)

    juce::File getSelectedFile() const
    {
        const int r = mList.getSelectedRow();
        return juce::isPositiveAndBelow(r, mFiles.size()) ? mFiles[r] : juce::File();
    }
    void clearSelection() { mList.deselectAllRows(); }

    void resized() override
    {
        auto r = getLocalBounds();
        auto top = r.removeFromTop(26);
        mSort.setBounds(top.removeFromRight(150).withSizeKeepingCentre(150, 24));
        top.removeFromRight(8);
        mSortLabelRect = top;
        r.removeFromTop(4);
        mList.setBounds(r);
    }

    void paint(juce::Graphics &g) override
    {
        g.setColour(colors::caption);
        g.setFont(fonts::archivo(10.0f, fonts::SemiBold, 0.12f));
        g.drawText("SORT", mSortLabelRect, juce::Justification::centredRight);
    }

private:
    void rescan()
    {
        const auto prev = getSelectedFile();
        mFiles.clearQuick();
        if (mFolder.isDirectory())
        {
            for (auto &f : mFolder.findChildFiles(juce::File::findFiles, false))
            {
                if (!extensions.contains(f.getFileExtension().toLowerCase())) continue;
                if (mSearch.isNotEmpty() && !f.getFileName().containsIgnoreCase(mSearch)) continue;
                if (extraFilter && !extraFilter(f)) continue;
                mFiles.add(f);
            }
        }
        resort(prev);
    }

    void resort(const juce::File &keepSelected = {})
    {
        Cmp cmp{mMode};
        mFiles.sort(cmp);
        mList.updateContent();
        // Preserve the selection across a re-sort / re-scan when possible.
        int sel = -1;
        if (keepSelected != juce::File())
            sel = mFiles.indexOf(keepSelected);
        if (sel >= 0) mList.selectRow(sel, juce::dontSendNotification);
        else mList.deselectAllRows();
        mList.repaint();
    }

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
            const bool newest = (m == Sort::DateNew);
            const bool aFirst = newest ? (ta > tb) : (ta < tb);
            return aFirst ? -1 : 1;
        }
    };

    // ListBoxModel.
    int getNumRows() override { return mFiles.size(); }
    void paintListBoxItem(int row, juce::Graphics &g, int w, int h, bool sel) override
    {
        if (!juce::isPositiveAndBelow(row, mFiles.size())) return;
        if (sel)
        {
            g.setColour(colors::accent.withAlpha(0.16f));
            g.fillRect(0, 0, w, h);
            g.setColour(colors::accent);
            g.fillRect(0, 0, 2, h);
        }
        g.setColour(sel ? colors::text : colors::text2);
        g.setFont(fonts::mono(12.0f));
        g.drawText(mFiles[row].getFileNameWithoutExtension(),
                   juce::Rectangle<int>(10, 0, w - 18, h),
                   juce::Justification::centredLeft, true);
    }
    void selectedRowsChanged(int) override { if (onSelChange) onSelChange(); }
    juce::var getDragSourceDescription(const juce::SparseSet<int> &rows) override
    {
        if (rows.size() > 0 && juce::isPositiveAndBelow(rows[0], mFiles.size()))
        {
            const auto f = mFiles[rows[0]];
            if (f.existsAsFile()) return f.getFullPathName();
        }
        return {};
    }

    juce::ListBox mList;
    juce::ComboBox mSort;
    juce::Array<juce::File> mFiles;
    juce::File mFolder;
    juce::String mSearch;
    Sort mMode = Sort::NameAsc;
    juce::Rectangle<int> mSortLabelRect;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SortableFileList)
};

} // namespace nam_rig::ui
