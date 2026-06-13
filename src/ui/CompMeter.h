#pragma once
// CompMeter — the compressor's metering cluster: GR, IN and OUT shown together.
//
// Three columns, side by side:
//   GR  : gain reduction, fills DOWNWARD from the top. Coloured in zones
//         (green 0..-3 dB transparent, yellow -3..-6 noticeable, red beyond
//         -6 heavily squashed) with faint tick labels at 0/-3/-6/-12 dB, plus
//         a fading "reduction history" ghost trail of the last ~1.5 s so fast
//         playing shows the RHYTHM of compression, not just the current value.
//   IN  : input peak, -60..0 dB, fills upward (lo/mid/hi colours).
//   OUT : output peak, -60..0 dB, fills upward — sits next to IN so you can
//         read gain staging at a glance.
//
// Fed from the editor timer: push(grDb,dt) (gain reduction, also the drop-in
// for the old GrMeter::push), pushIn(db,dt), pushOut(db,dt). pushIn also fires
// onInput(rawPeakDb) so the panel can drive the transfer-curve dot.

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <functional>
#include "RigLookAndFeel.h"

namespace nam_rig::ui
{

class CompMeter : public juce::Component
{
public:
    static constexpr float kGrMaxDb = 24.0f;        // GR display span
    static constexpr float kGrFallDbPerSec = 60.0f; // GR release ballistics
    static constexpr float kPkMinDb = -60.0f;       // IN/OUT floor
    static constexpr float kPkFallDbPerSec = 36.0f;
    static constexpr int kTrail = 24;               // history-ghost length (frames)

    std::function<void(float)> onInput; // raw input peak dB, for the curve dot

    // Gain reduction (>= 0 dB). Drop-in for the editor's existing comp line.
    void push(float grDb, float dtSeconds)
    {
        grDb = juce::jlimit(0.0f, kGrMaxDb, grDb);
        const float fallen = juce::jmax(mGrShown - kGrFallDbPerSec * dtSeconds, 0.0f);
        mGrShown = juce::jmax(grDb, fallen);

        mTrail[(size_t)mTrailW] = grDb; // history ring for the rhythm ghost
        mTrailW = (mTrailW + 1) % kTrail;
        repaint();
    }

    void pushIn(float peakDb, float dt)
    {
        mInShown = juce::jlimit(kPkMinDb, 6.0f, juce::jmax(peakDb, mInShown - kPkFallDbPerSec * dt));
        if (onInput)
            onInput(peakDb);
        repaint();
    }

    void pushOut(float peakDb, float dt)
    {
        mOutShown = juce::jlimit(kPkMinDb, 6.0f, juce::jmax(peakDb, mOutShown - kPkFallDbPerSec * dt));
        repaint();
    }

    void paint(juce::Graphics &g) override
    {
        auto area = getLocalBounds().toFloat().reduced(1.0f);

        const float grW = juce::jmax(34.0f, area.getWidth() * 0.46f);
        auto grCol = area.removeFromLeft(grW);
        area.removeFromLeft(4.0f);
        const float each = juce::jmax(12.0f, (area.getWidth() - 3.0f) * 0.5f);
        auto inCol = area.removeFromLeft(each);
        area.removeFromLeft(3.0f);
        auto outCol = area;

        paintGrColumn(g, grCol);
        paintPeakColumn(g, inCol, "IN", mInShown);
        paintPeakColumn(g, outCol, "OUT", mOutShown);
    }

private:
    // GR dB -> y within rect r (0 dB at top, kGrMaxDb at bottom).
    static float grY(const juce::Rectangle<float> &r, float db)
    {
        return r.getY() + juce::jlimit(0.0f, 1.0f, db / kGrMaxDb) * r.getHeight();
    }

    void paintGrColumn(juce::Graphics &g, juce::Rectangle<float> col)
    {
        auto header = col.removeFromTop(13.0f);
        g.setColour(colors::textDim);
        g.setFont(RigLookAndFeel::withHeight(10.0f).boldened());
        g.drawText("GR", header.toNearestInt(), juce::Justification::centredLeft);
        g.setFont(RigLookAndFeel::withHeight(10.0f));
        g.drawText(grText(), header.toNearestInt(), juce::Justification::centredRight);

        auto t = col;
        const float x = t.getX(), w = t.getWidth();
        g.setColour(colors::track);
        g.fillRoundedRectangle(t, 2.0f);

        // history ghost: faint lines at each recent GR level, fading with age.
        for (int k = 0; k < kTrail; ++k)
        {
            const int idx = (mTrailW - 1 - k + kTrail * 2) % kTrail;
            const float db = mTrail[(size_t)idx];
            if (db < 0.05f)
                continue;
            const float a = 0.30f * (1.0f - (float)k / (float)kTrail);
            g.setColour(zoneColour(db).withAlpha(a));
            g.fillRect(x, grY(t, db) - 0.5f, w, 1.0f);
        }

        // live fill, segmented by zone (green/yellow/red).
        auto seg = [&](float fromDb, float toDb, juce::Colour c) {
            if (mGrShown <= fromDb)
                return;
            const float y0 = grY(t, fromDb);
            const float y1 = grY(t, juce::jmin(mGrShown, toDb));
            g.setColour(c);
            g.fillRect(x, y0, w, y1 - y0);
        };
        seg(0.0f, 3.0f, colors::meterLo);
        seg(3.0f, 6.0f, colors::meterMid);
        seg(6.0f, kGrMaxDb, colors::meterHi);

        // faint scale ticks + labels at 0 / -3 / -6 / -12 dB.
        g.setFont(RigLookAndFeel::withHeight(9.0f));
        const std::array<float, 4> ticks = {0.0f, 3.0f, 6.0f, 12.0f};
        for (float d : ticks)
        {
            const float yy = grY(t, d);
            g.setColour(colors::outline.withAlpha(0.7f));
            g.fillRect(x, yy, w, 1.0f);
            g.setColour(colors::textDim.withAlpha(0.7f));
            const juce::String lbl = (d == 0.0f) ? juce::String("0")
                                                 : juce::String("-") + juce::String((int)d);
            g.drawText(lbl, juce::Rectangle<float>(x + 2.0f, yy + 1.0f, w - 4.0f, 9.0f).toNearestInt(),
                       juce::Justification::topLeft);
        }
    }

    void paintPeakColumn(juce::Graphics &g, juce::Rectangle<float> col,
                         const char *name, float shownDb)
    {
        auto header = col.removeFromTop(13.0f);
        g.setColour(colors::textDim);
        g.setFont(RigLookAndFeel::withHeight(9.5f));
        g.drawText(name, header.toNearestInt(), juce::Justification::centred);

        g.setColour(colors::track);
        g.fillRoundedRectangle(col, 2.0f);

        const float norm = juce::jlimit(0.0f, 1.0f, (shownDb - kPkMinDb) / (0.0f - kPkMinDb));
        if (norm > 0.001f)
        {
            auto fill = col.removeFromBottom(col.getHeight() * norm);
            juce::Colour c = colors::meterLo;
            if (shownDb > -3.0f)
                c = colors::meterHi;
            else if (shownDb > -12.0f)
                c = colors::meterMid;
            g.setColour(c);
            g.fillRoundedRectangle(fill, 2.0f);
        }
    }

    juce::String grText() const
    {
        return mGrShown < 0.05f ? juce::String("0.0")
                                : juce::String("-") + juce::String(mGrShown, 1);
    }

    static juce::Colour zoneColour(float grDb)
    {
        return grDb > 6.0f ? colors::meterHi : grDb > 3.0f ? colors::meterMid : colors::meterLo;
    }

    float mGrShown = 0.0f;
    float mInShown = kPkMinDb, mOutShown = kPkMinDb;
    std::array<float, kTrail> mTrail{};
    int mTrailW = 0;
};

} // namespace nam_rig::ui
