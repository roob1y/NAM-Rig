#pragma once
// IrTagCache — cheap, persistent map from IR file -> tone tag. Analyzing an IR
// (read + FFT + classify) is done once per file; the result is cached in memory
// AND on disk (Documents/NAM Rig/ir_tags.cache), keyed by path + size + mtime, so
// re-opening the library is instant and only new/changed files get re-analyzed.
// Thread-safe: tagFor() is safe to call from the directory-scan background thread.

#include "rig/IrAnalysis.h"
#include <map>

namespace nam_rig::ui
{

class IrTagCache
{
public:
    IrTagCache()
        : mFile(juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                    .getChildFile("NAM Rig").getChildFile("ir_tags.cache"))
    {
        load();
    }
    ~IrTagCache() { save(); }

    // Cached tag for a file; on a miss it analyzes (heavy) and stores. Empty
    // result (unreadable file) is not cached, so it can retry later.
    juce::String tagFor(const juce::File &f)
    {
        const juce::String key = f.getFullPathName();
        const juce::int64 sz = f.getSize();
        const juce::int64 mt = f.getLastModificationTime().toMilliseconds();
        {
            const juce::ScopedLock sl(mLock);
            auto it = mMap.find(key);
            if (it != mMap.end() && it->second.size == sz && it->second.mtime == mt)
                return it->second.tag;
        }
        const juce::String tag = nam_rig::ir::tagForFile(f); // heavy, computed unlocked
        if (tag.isNotEmpty())
        {
            const juce::ScopedLock sl(mLock);
            mMap[key] = {sz, mt, tag};
            mDirty = true;
        }
        return tag;
    }

    void save()
    {
        const juce::ScopedLock sl(mLock);
        if (!mDirty) return;
        juce::String out;
        for (auto &e : mMap)
            out << e.first << "\t" << e.second.size << "\t" << e.second.mtime << "\t" << e.second.tag << "\n";
        mFile.getParentDirectory().createDirectory();
        mFile.replaceWithText(out);
        mDirty = false;
    }

private:
    struct Entry { juce::int64 size = 0, mtime = 0; juce::String tag; };

    void load()
    {
        if (!mFile.existsAsFile()) return;
        juce::StringArray lines;
        lines.addLines(mFile.loadFileAsString());
        const juce::ScopedLock sl(mLock);
        for (auto &ln : lines)
        {
            auto parts = juce::StringArray::fromTokens(ln, "\t", "");
            if (parts.size() >= 4)
                mMap[parts[0]] = {parts[1].getLargeIntValue(), parts[2].getLargeIntValue(), parts[3]};
        }
    }

    juce::File mFile;
    juce::CriticalSection mLock;
    std::map<juce::String, Entry> mMap;
    bool mDirty = false;
};

} // namespace nam_rig::ui
