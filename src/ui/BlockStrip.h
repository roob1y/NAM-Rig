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
        // NB: the lit glow is drawn by the parent BlockTile (this component is too
        // small to hold the blur without clipping it). Here we draw only the jewel.
        auto b = getLocalBounds().toFloat();
        const float d = juce::jmin(b.getWidth(), b.getHeight()) - 4.0f;
        auto circle = juce::Rectangle<float>(d, d).withCentre(b.getCentre());
        const bool on = getToggleState();
        g.setColour(on ? colors::accent : colors::ledOff);
        g.fillEllipse(circle);
        // Bezel ring around the jewel (design: 2px #1a1d22 border).
        g.setColour(juce::Colour(0xff1a1d22));
        g.drawEllipse(circle, 2.0f);
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

    // Manual LED (no bool-param attachment): its lit state is set via setLedOn()
    // and clicks are routed to onClick. Used for the Amp A/B "which rig" radios.
    void setManualLed(std::function<void()> onClick)
    {
        mLed = std::make_unique<BypassLed>();
        mLed->setClickingTogglesState(false);
        mLed->onClick = std::move(onClick);
        mLed->onStateChange = [this] { repaint(); };
        addAndMakeVisible(*mLed);
        resized();
    }
    void setLedOn(bool on)
    {
        if (mLed != nullptr && mLed->getToggleState() != on)
        {
            mLed->setToggleState(on, juce::dontSendNotification);
            repaint();
        }
    }

    void mouseUp(const juce::MouseEvent &e) override
    {
        if (!mFuture && onSelect && getLocalBounds().contains(e.getPosition()))
            onSelect();
    }

    void paint(juce::Graphics &g) override
    {
        auto b = getLocalBounds().toFloat().reduced(0.5f);
        g.setColour(mSelected ? colors::tileSel : colors::tile);
        g.fillRoundedRectangle(b, 9.0f);
        g.setColour(mSelected ? colors::accent : colors::outline);
        g.drawRoundedRectangle(b, 9.0f, mSelected ? 1.5f : 1.0f);

        const bool engaged = mLed == nullptr || mLed->getToggleState();
        g.setColour(mFuture ? colors::textDim
                            : (engaged ? colors::text : colors::textDim));
        g.setFont(fonts::archivo(13.0f, fonts::Bold, 0.04f));
        auto textArea = getLocalBounds().reduced(4);
        if (mFuture)
        {
            g.drawText(mName, textArea.removeFromTop(getHeight() * 6 / 10),
                       juce::Justification::centredBottom);
            g.setFont(fonts::archivo(10.0f));
            g.drawText("soon", textArea, juce::Justification::centredTop);
        }
        else
        {
            g.drawText(mName, textArea, juce::Justification::centred);
        }

        // Bypass-LED glow (drawn here, not in the tiny LED component, so the blur
        // has room). Behind the jewel itself, which the child paints on top.
        if (mLed != nullptr && mLed->getToggleState())
        {
            auto lc = mLed->getBounds().getCentre().toFloat();
            fx::glowEllipse(g, juce::Rectangle<float>(11.0f, 11.0f).withCentre(lc),
                            colors::accent, 9, 0.45f, 4, 0.38f);
        }
    }

    void resized() override
    {
        if (mLed != nullptr)
            mLed->setBounds(getLocalBounds().removeFromTop(19).removeFromRight(19)
                                .withSizeKeepingCentre(15, 15).translated(-2, 2));
    }

private:
    juce::String mName;
    bool mFuture = false, mSelected = false;
    std::unique_ptr<BypassLed> mLed;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> mLedAtt;
};

// Vertical two-position switch that sits on the rig branch (between DRIVE and the
// AMP A / AMP B split). Thumb UP = Single (one rig), thumb DOWN = Dual (both).
// Writes rigMode: 0/1 = single (Solo A / Solo B), 2 = Dual. Going Single restores
// the last-used solo rig. The Mix panel's A/B selector chooses which rig is single.
class RigModeSwitch : public juce::Component, private juce::Timer
{
public:
    explicit RigModeSwitch(juce::AudioProcessorValueTreeState &apvts) : mApvts(apvts)
    {
        const int m = mode();
        if (m != 2) mLastSingle = m;
        mLastDual = (m == 2);
        startTimerHz(20);
    }

    void mouseUp(const juce::MouseEvent &e) override
    {
        if (!getLocalBounds().contains(e.getPosition())) return;
        const bool wantDual = e.y > getHeight() / 2; // top half = Single, bottom = Dual
        if (auto *p = mApvts.getParameter("rigMode"))
            p->setValueNotifyingHost(p->convertTo0to1(wantDual ? 2.0f : (float)mLastSingle));
    }

    void paint(juce::Graphics &g) override
    {
        auto r = getLocalBounds().toFloat();
        const bool dual = (mode() == 2);
        const float bh = r.getHeight() * 0.5f;
        const float rad = 6.0f;

        auto cell = [&](juce::Rectangle<float> c, bool top, bool on, const juce::String &txt)
        {
            c = c.reduced(0.5f);
            juce::Path p;
            p.addRoundedRectangle(c.getX(), c.getY(), c.getWidth(), c.getHeight(), rad, rad,
                                  top, top, !top, !top);
            g.setColour(on ? colors::accent : colors::tile);
            g.fillPath(p);
            g.setColour(on ? colors::accent : colors::outline);
            g.strokePath(p, juce::PathStrokeType(1.0f));
            g.setColour(on ? colors::bg : colors::textDim);
            g.setFont(fonts::archivo(7.5f, fonts::Bold, 0.02f));
            g.drawText(txt, c, juce::Justification::centred);
        };
        cell(r.withHeight(bh), true, !dual, "SGL");                 // top = Single
        cell(r.withTrimmedTop(bh), false, dual, "DUAL");            // bottom = Dual
    }

private:
    int mode() const { return (int)mApvts.getRawParameterValue("rigMode")->load(); }
    void timerCallback() override
    {
        const int m = mode();
        if (m != 2) mLastSingle = m;
        if ((m == 2) != mLastDual) { mLastDual = (m == 2); repaint(); }
    }

    juce::AudioProcessorValueTreeState &mApvts;
    int mLastSingle = 0;
    bool mLastDual = false;
};

// The dual-rig chain as a branched tile diagram: shared full-height tiles for
// the mono pre (gate, comp) and stereo post (mix, mod, delay, verb), with the
// two rig voices as parallel half-height lanes (Rig A top, Rig B bottom) that
// split off the comp and merge at the MIX tile.
class BlockStrip : public juce::Component, private juce::Timer
{
public:
    enum Lane { Full = 0, Top = 1, Bot = 2 };
    std::function<void(int)> onSelectionChanged; // selectable-block index

    BlockStrip(juce::AudioProcessorValueTreeState &apvts) : mApvts(apvts)
    {
        struct Slot { const char *name, *param; int col, lane; };
        // Order MUST match the editor's mPanels array (selectable index).
        static const Slot slots[] = {
            {"GATE",  "gateOn",   0, Full},
            {"COMP",  "compOn",   1, Full},
            {"DRIVE", "driveOn",  2, Full},
            {"AMP A", "",         3, Top},
            {"EQ A",  "eqOn",     4, Top},
            {"CAB A", "cabOn",    5, Top},
            {"AMP B", "",         3, Bot},
            {"EQ B",  "eqOnB",    4, Bot},
            {"CAB B", "cabOnB",   5, Bot},
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
        mModeSwitch = std::make_unique<RigModeSwitch>(apvts);
        addAndMakeVisible(*mModeSwitch);

        // Amp A / Amp B indicators. Lit = that rig is active AND its amp engaged.
        // Click logic (see ampClick): an inactive rig's amp selects/solos that rig;
        // an active amp toggles its own bypass (Single) or both amps' (Dual).
        if (mTiles.size() > 6)
        {
            mTiles[3]->setManualLed([this] { ampClick(0); }); // Amp A
            mTiles[6]->setManualLed([this] { ampClick(1); }); // Amp B
        }
        updateAmpLeds();
        startTimerHz(20);
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
        // Extra-wide gutter at the DRIVE -> AMP A/B split to hold the Single/Dual
        // switch. Tiles shrink slightly to absorb it (window width is fixed).
        const int branchExtra = 30; // split gutter becomes gap + branchExtra (~40px) to fit the switch labels
        const int colW = juce::jmax(1, (getWidth() - gap * (kCols - 1) - branchExtra) / kCols);
        const int H = getHeight();
        const int laneGap = 6;
        const int laneH = (H - laneGap) / 2;
        for (size_t i = 0; i < mTiles.size(); ++i)
        {
            const int col = mLayout[i].first, lane = mLayout[i].second;
            const int x = col * (colW + gap) + (col >= 3 ? branchExtra : 0); // shift past the split
            int y = 0, h = H;
            if (lane == Top) { y = 0; h = laneH; }
            else if (lane == Bot) { y = H - laneH; h = laneH; }
            mTiles[i]->setBounds(x, y, colW, h);
        }
        // Single/Dual switch centred in the widened split gutter.
        if (mModeSwitch && mTiles.size() > 3)
        {
            const int splitX = (mTiles[2]->getRight() + mTiles[3]->getX()) / 2;
            const int sw = 34, sh = juce::jmin(H - 4, 46);
            mModeSwitch->setBounds(splitX - sw / 2, (H - sh) / 2, sw, sh);
        }
    }

    void paint(juce::Graphics &g) override
    {
        g.setColour(juce::Colour(0xff3a414c)); // branch lines
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
        g.setColour(juce::Colour(0xff5d646f));
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

    void timerCallback() override { updateAmpLeds(); }

    float getF(const char *id) const { return mApvts.getRawParameterValue(id)->load(); }
    void setF(const char *id, float v)
    {
        if (auto *p = mApvts.getParameter(id)) p->setValueNotifyingHost(p->convertTo0to1(v));
    }

    void updateAmpLeds()
    {
        if (mTiles.size() <= 6) return;
        const int mode = (int)getF("rigMode");
        // Invariant: in Dual the two amps move together — you can never end up
        // with exactly one active (e.g. after switching in from a Solo bypass).
        if (mode == 2 && (getF("ampOnA") >= 0.5f) != (getF("ampOnB") >= 0.5f))
        {
            setF("ampOnA", 1.0f);
            setF("ampOnB", 1.0f);
        }
        const bool aRig = (mode != 1), bRig = (mode != 0); // rig in the signal path
        mTiles[3]->setLedOn(aRig && getF("ampOnA") >= 0.5f);
        mTiles[6]->setLedOn(bRig && getF("ampOnB") >= 0.5f);
    }

    // which: 0 = Amp A, 1 = Amp B.
    void ampClick(int which)
    {
        const int mode = (int)getF("rigMode");
        const bool rigActive = (which == 0) ? (mode != 1) : (mode != 0);
        const char *ampId = (which == 0) ? "ampOnA" : "ampOnB";

        if (!rigActive)
        {
            // Inactive rig: select/solo it and make sure its amp is engaged.
            setF("rigMode", (float)which); // 0 = Solo A, 1 = Solo B
            setF(ampId, 1.0f);
        }
        else if (mode == 2)
        {
            // Dual: a click bypasses BOTH amps (or restores both).
            const bool anyOn = getF("ampOnA") >= 0.5f || getF("ampOnB") >= 0.5f;
            setF("ampOnA", anyOn ? 0.0f : 1.0f);
            setF("ampOnB", anyOn ? 0.0f : 1.0f);
        }
        else
        {
            // Single, this rig active: toggle just this amp's bypass.
            setF(ampId, getF(ampId) >= 0.5f ? 0.0f : 1.0f);
        }
        updateAmpLeds();
    }

    static constexpr int kCols = 10;
    juce::AudioProcessorValueTreeState &mApvts;
    std::vector<std::unique_ptr<BlockTile>> mTiles;
    std::vector<std::pair<int, int>> mLayout; // (col, lane) per tile
    std::unique_ptr<RigModeSwitch> mModeSwitch;
    int mSelected = -1;
};

} // namespace nam_rig::ui
