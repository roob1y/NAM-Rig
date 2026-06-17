#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "RigLookAndFeel.h"

namespace nam_rig::ui
{

// Round bypass LED. Attached straight to the block's On parameter, so DAW
// automation and the tile stay in sync. Lit = block engaged.
class BypassLed : public juce::ToggleButton
{
public:
    BypassLed() { setClickingTogglesState(true); }

    void paintButton(juce::Graphics &g, bool highlighted, bool) override
    {
        auto b = getLocalBounds().toFloat();
        const float d = juce::jmin(b.getWidth(), b.getHeight()) - 2.0f;
        auto circle = juce::Rectangle<float>(d, d).withCentre(b.getCentre());
        const bool on = getToggleState();
        g.setColour(on ? colors::accent : colors::ledOff);
        g.fillEllipse(circle);
        if (on)
        {
            g.setColour(colors::accent.withAlpha(0.35f));
            g.fillEllipse(circle.expanded(2.5f)); // glow
            g.setColour(colors::accent);
            g.fillEllipse(circle);
        }
        if (highlighted && isEnabled())
        {
            g.setColour(colors::text.withAlpha(0.5f));
            g.drawEllipse(circle.expanded(1.5f), 1.0f);
        }
    }
};

// One chain slot in the strip: block name + optional bypass LED.
class BlockTile : public juce::Component
{
public:
    std::function<void()> onSelect;

    // bypassParamId empty = always-on block (amp); future = stub, dimmed.
    BlockTile(const juce::String &name, const juce::String &bypassParamId,
              juce::AudioProcessorValueTreeState &apvts, bool future)
        : mName(name), mFuture(future)
    {
        if (bypassParamId.isNotEmpty() && !future)
        {
            mLed = std::make_unique<BypassLed>();
            addAndMakeVisible(*mLed);
            mLedAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
                apvts, bypassParamId, *mLed);
            mLed->onStateChange = [this] { repaint(); };
        }
        if (future)
            setAlpha(0.35f);
        setInterceptsMouseClicks(true, true);
    }

    void setSelected(bool selected)
    {
        if (mSelected != selected) { mSelected = selected; repaint(); }
    }

    bool isFuture() const { return mFuture; }

    void mouseUp(const juce::MouseEvent &e) override
    {
        if (!mFuture && onSelect && getLocalBounds().contains(e.getPosition()))
            onSelect();
    }

    void paint(juce::Graphics &g) override
    {
        auto b = getLocalBounds().toFloat().reduced(0.5f);
        g.setColour(mSelected ? colors::tileSel : colors::tile);
        g.fillRoundedRectangle(b, 7.0f);
        g.setColour(mSelected ? colors::accent : colors::outline);
        g.drawRoundedRectangle(b, 7.0f, mSelected ? 1.5f : 1.0f);

        const bool engaged = mLed == nullptr || mLed->getToggleState();
        g.setColour(mFuture ? colors::textDim
                            : (engaged ? colors::text : colors::textDim));
        g.setFont(RigLookAndFeel::withHeight(14.0f).boldened());
        auto textArea = getLocalBounds().reduced(4);
        if (mFuture)
        {
            g.drawText(mName, textArea.removeFromTop(getHeight() * 6 / 10),
                       juce::Justification::centredBottom);
            g.setFont(RigLookAndFeel::withHeight(10.0f));
            g.drawText("soon", textArea, juce::Justification::centredTop);
        }
        else
        {
            g.drawText(mName, textArea, juce::Justification::centred);
        }
    }

    void resized() override
    {
        if (mLed != nullptr)
            mLed->setBounds(getLocalBounds().removeFromTop(16).removeFromRight(16)
                                .withSizeKeepingCentre(12, 12).translated(-2, 2));
    }

private:
    juce::String mName;
    bool mFuture = false, mSelected = false;
    std::unique_ptr<BypassLed> mLed;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> mLedAtt;
};

// The dual-rig chain as a branched tile diagram: shared full-height tiles for
// the mono pre (gate, comp) and stereo post (mix, mod, delay, verb), with the
// two rig voices as parallel half-height lanes (Rig A top, Rig B bottom) that
// split off the comp and merge at the MIX tile.
class BlockStrip : public juce::Component
{
public:
    enum Lane { Full = 0, Top = 1, Bot = 2 };
    std::function<void(int)> onSelectionChanged; // selectable-block index

    BlockStrip(juce::AudioProcessorValueTreeState &apvts)
    {
        struct Slot { const char *name, *param; int col, lane; };
        // Order MUST match the editor's mPanels array (selectable index).
        // Rig B's eq/cab have no per-rig bypass param -> no LED (always on).
        static const Slot slots[] = {
            {"GATE",  "gateOn",   0, Full},
            {"COMP",  "compOn",   1, Full},
            {"DRIVE", "",         2, Full},
            {"AMP A", "",         3, Top},
            {"EQ A",  "eqOn",     4, Top},
            {"CAB A", "cabOn",    5, Top},
            {"AMP B", "",         3, Bot},
            {"EQ B",  "",         4, Bot},
            {"CAB B", "",         5, Bot},
            {"MIX",   "",         6, Full},
            {"MOD",   "modOn",    7, Full},
            {"DELAY", "delayOn",  8, Full},
            {"VERB",  "reverbOn", 9, Full},
        };
        for (const auto &s : slots)
        {
            const int index = (int)mTiles.size();
            auto tile = std::make_unique<BlockTile>(s.name, s.param, apvts, false);
            tile->onSelect = [this, index] { select(index); };
            addAndMakeVisible(*tile);
            mTiles.push_back(std::move(tile));
            mLayout.push_back({s.col, s.lane});
        }
    }

    void select(int selectableIndex)
    {
        mSelected = selectableIndex;
        for (int i = 0; i < (int)mTiles.size(); ++i)
            mTiles[(size_t)i]->setSelected(i == selectableIndex);
        if (onSelectionChanged)
            onSelectionChanged(selectableIndex);
    }

    void resized() override
    {
        const int gap = 10;
        const int colW = juce::jmax(1, (getWidth() - gap * (kCols - 1)) / kCols);
        const int H = getHeight();
        const int laneGap = 6;
        const int laneH = (H - laneGap) / 2;
        for (size_t i = 0; i < mTiles.size(); ++i)
        {
            const int col = mLayout[i].first, lane = mLayout[i].second;
            const int x = col * (colW + gap);
            int y = 0, h = H;
            if (lane == Top) { y = 0; h = laneH; }
            else if (lane == Bot) { y = H - laneH; h = laneH; }
            mTiles[i]->setBounds(x, y, colW, h);
        }
    }

    void paint(juce::Graphics &g) override
    {
        g.setColour(colors::textDim);
        const float yC = (float)getHeight() * 0.5f;
        auto cy = [](BlockTile &t) { return (float)t.getBounds().getCentreY(); };

        // Split: DRIVE -> AMP A (top) and AMP B (bottom).
        const float splitX = ((float)mTiles[2]->getRight() + (float)mTiles[3]->getX()) * 0.5f;
        branch(g, (float)mTiles[2]->getRight(), yC, splitX, cy(*mTiles[3]),
               (float)mTiles[3]->getX(), cy(*mTiles[6]), (float)mTiles[6]->getX());
        // Merge: CAB A / CAB B -> MIX.
        const float mergeX = ((float)mTiles[5]->getRight() + (float)mTiles[9]->getX()) * 0.5f;
        branch(g, (float)mTiles[9]->getX(), yC, mergeX, cy(*mTiles[3]),
               (float)mTiles[5]->getRight(), cy(*mTiles[6]), (float)mTiles[5]->getRight());

        // Flow chevrons between adjacent same-lane tiles.
        chevron(g, *mTiles[0], *mTiles[1]);   // GATE -> COMP
        chevron(g, *mTiles[1], *mTiles[2]);   // COMP -> DRIVE
        chevron(g, *mTiles[3], *mTiles[4]);   // AMP A -> EQ A
        chevron(g, *mTiles[4], *mTiles[5]);   // EQ A -> CAB A
        chevron(g, *mTiles[6], *mTiles[7]);   // AMP B -> EQ B
        chevron(g, *mTiles[7], *mTiles[8]);   // EQ B -> CAB B
        chevron(g, *mTiles[9], *mTiles[10]);  // MIX -> MOD
        chevron(g, *mTiles[10], *mTiles[11]); // MOD -> DELAY
        chevron(g, *mTiles[11], *mTiles[12]); // DELAY -> VERB
    }

private:
    // Y-connector: trunk at (x0,yC)->(xMid,yC), then to two lane stubs at xTop/xBot.
    static void branch(juce::Graphics &g, float x0, float yC, float xMid,
                       float yTop, float xTopEnd, float yBot, float xBotEnd)
    {
        juce::Path p;
        p.startNewSubPath(x0, yC);
        p.lineTo(xMid, yC);
        p.startNewSubPath(xMid, yTop);
        p.lineTo(xMid, yBot);
        p.startNewSubPath(xMid, yTop);
        p.lineTo(xTopEnd, yTop);
        p.startNewSubPath(xMid, yBot);
        p.lineTo(xBotEnd, yBot);
        g.strokePath(p, juce::PathStrokeType(1.4f));
    }

    static void chevron(juce::Graphics &g, BlockTile &a, BlockTile &b)
    {
        const float cx = ((float)a.getRight() + (float)b.getX()) * 0.5f;
        const float cyv = (float)a.getBounds().getCentreY();
        juce::Path p;
        p.startNewSubPath(cx - 2.5f, cyv - 4.0f);
        p.lineTo(cx + 2.5f, cyv);
        p.lineTo(cx - 2.5f, cyv + 4.0f);
        g.strokePath(p, juce::PathStrokeType(1.5f));
    }

    static constexpr int kCols = 10;
    std::vector<std::unique_ptr<BlockTile>> mTiles;
    std::vector<std::pair<int, int>> mLayout; // (col, lane) per tile
    int mSelected = -1;
};

} // namespace nam_rig::ui
