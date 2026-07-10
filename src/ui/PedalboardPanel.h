#pragma once
// PedalboardPanel — the AmpliTube-style 3-zone editor for the front-of-amp POOL
// (SCOPE_PEDALBOARD Stage C). Drives the pool params on rig/PedalboardBlock.h:
//
//   DETAIL (top-left)  : the selected slot's controls, param-bound (knobs + selectors).
//   PALETTE (right)    : categorised add — Drive / Mod / Delay -> a model menu.
//   CHAIN (bottom)     : locked Env+Comp front pair + the free slots as nodes, with the
//                        A/B split and lane-coloured cables. Click a node to select it.
//
// Interaction (click-based, matches the first-cut hybrid: param-bound child widgets for
// the knobs/selectors + custom-drawn/hit-tested nodes & buttons):
//   - click a chain node        -> select it (the DETAIL zone rebuilds for that slot)
//   - a palette category        -> model menu -> fills the target free slot (selected if
//                                  empty, else the first empty) + enables the board
//   - DETAIL Route/On/Remove     -> pbS{i}Lane / pbS{i}On / set Type=Off
//   - ENABLE switch              -> pbEnabled (off = legacy fixed chain runs, byte-exact)
//
// Env + Comp are the LOCKED front pair (always first two, on the trunk, order swappable
// via pbFrontOrder); their controls edit the shared comp*/envfilter* params. Bespoke
// per-pedal enclosure ART is a later polish pass — nodes/cards are clean styled shapes.
//
// The board still lives at the editor's BOARD tile / mPanels[15]; no strip surgery here
// (that + preset-v3 migration is Stage D).

#include <juce_audio_processors/juce_audio_processors.h>
#include <cmath>
#include "RigLookAndFeel.h"
#include "Panels.h" // BlockPanel, LabeledKnob, SegmentedControl, ToggleSwitch, DriveBlock, paintDriveGlyph

namespace nam_rig::ui
{

class PedalboardPanel : public BlockPanel, private juce::Timer
{
public:
    explicit PedalboardPanel(juce::AudioProcessorValueTreeState &apvts)
        : BlockPanel("PEDALBOARD"), mApvts(apvts)
    {
        mEnable = std::make_unique<ToggleSwitch>(apvts, "pbEnabled");
        addAndMakeVisible(*mEnable);
        mFrontOrder = std::make_unique<SegmentedControl>(apvts, "pbFrontOrder",
                                                         juce::StringArray{"Env 1st", "Comp 1st"});
        addAndMakeVisible(*mFrontOrder);
        buildDetail();
        startTimerHz(20);
    }

    void refresh() { repaint(); }

    // ==================================================================== layout
    void resized() override
    {
        auto body = contentArea();

        auto top = body.removeFromTop(28);
        mEnable->setBounds(top.removeFromRight(52).withSizeKeepingCentre(46, 24));
        top.removeFromRight(70); // room for the "ENABLE" caption drawn in paint()
        mFrontOrder->setBounds(top.removeFromLeft(150).withSizeKeepingCentre(140, 24));
        body.removeFromTop(10);

        mChainRect = body.removeFromBottom(112);
        body.removeFromBottom(12);
        mPaletteRect = body.removeFromRight(150);
        body.removeFromRight(14);
        mDetailRect = body;

        layoutChain();
        layoutPalette();
        layoutDetail();
    }

    // ==================================================================== paint
    void paint(juce::Graphics &g) override
    {
        BlockPanel::paint(g);

        g.setColour(colors::caption);
        g.setFont(fonts::mono(9.0f, fonts::Medium, 0.06f));
        g.drawText("ENABLE", juce::Rectangle<int>(mEnable->getX() - 66, mEnable->getY(),
                                                  62, mEnable->getHeight()),
                   juce::Justification::centredRight);

        paintDetailCard(g);
        paintPalette(g);
        paintChain(g);

        if (!enabled())
        {
            g.setColour(colors::textDim);
            g.setFont(fonts::mono(9.5f, fonts::Medium, 0.04f));
            g.drawText("board off - legacy chain running (byte-exact). enable to route the pool.",
                       juce::Rectangle<int>(mChainRect.getX(), mChainRect.getBottom() - 14,
                                            mChainRect.getWidth(), 14),
                       juce::Justification::centred);
        }
    }

    // ================================================================ interaction
    void mouseDown(const juce::MouseEvent &e) override
    {
        const auto p = e.getPosition();

        // palette category buttons
        if (mPalDrive.contains(p)) { showAddMenu(1); return; }
        if (mPalMod.contains(p))   { showAddMenu(2); return; }
        if (mPalDelay.contains(p)) { showAddMenu(3); return; }
        if (mImportRect.contains(p)) { importLegacy(); return; }

        // detail model button / remove button
        if (mHasModel && mModelRect.contains(p)) { showModelMenu(); return; }
        if (mHasRemove && mRemoveRect.contains(p)) { removeSelected(); return; }

        // chain nodes -> select
        for (const auto &n : mNodes)
            if (n.rect.contains(p)) { selectNode(n); return; }
    }

private:
    // -------------------------------------------------------------- param helpers
    float getF(const juce::String &id) const
    {
        if (auto *a = mApvts.getRawParameterValue(id)) return a->load();
        return 0.0f;
    }
    int getC(const juce::String &id) const { return (int)getF(id); }
    bool enabled() const { return getF("pbEnabled") >= 0.5f; }
    void setC(const juce::String &id, int idx)
    {
        if (auto *pr = mApvts.getParameter(id))
        {
            pr->beginChangeGesture();
            pr->setValueNotifyingHost(pr->convertTo0to1((float)juce::jmax(0, idx)));
            pr->endChangeGesture();
        }
    }
    juce::String sid(int slot, const char *suf) const { return "pbS" + juce::String(slot) + suf; }

    // slot Type: 0 Off, 1 Drive, 2 Mod, 3 Delay
    int slotType(int i) const { return getC(sid(i, "Type")); }
    int slotLane(int i) const { return getC(sid(i, "Lane")); }
    bool slotOn(int i) const { return getF(sid(i, "On")) >= 0.5f; }

    static juce::Colour typeAccent(int type) // 1 drive,2 mod,3 delay
    {
        switch (type)
        {
        case 1: return colors::accent;            // drive = amber
        case 2: return juce::Colour(0xffc7a6f0);  // mod = violet
        case 3: return juce::Colour(0xff7fd08a);  // delay = green
        default: return colors::outline;
        }
    }
    static const char *typeShort(int type)
    {
        switch (type) { case 1: return "DRIVE"; case 2: return "MOD"; case 3: return "DELAY"; default: return ""; }
    }
    // Short model label for a free slot node.
    juce::String slotModelName(int i) const
    {
        const int t = slotType(i);
        if (t == 1)
        {
            const auto k = (nam_rig::DriveBlock::Kind)(getC(sid(i, "dCat")));
            return nam_rig::DriveBlock::modelName(k, getC(sid(i, "bModel")));
        }
        if (t == 2)
        {
            static const char *m[] = {"Chorus", "Phaser", "Flanger", "Tremolo", "Uni-Vibe"};
            return m[juce::jlimit(0, 4, getC(sid(i, "mType")))];
        }
        if (t == 3)
        {
            static const char *m[] = {"DD-7", "Carbon Copy", "Memory Man"};
            return m[juce::jlimit(0, 2, getC(sid(i, "pModel")))];
        }
        return {};
    }

    // ------------------------------------------------------------------ selection
    // mSelType: 0 Env, 1 Comp, 2 Free ; mSelSlot valid when Free.
    int mSelType = 0;
    int mSelSlot = 0;

    struct Node { int kind; int slot; juce::Rectangle<int> rect; }; // kind: 0 env,1 comp,2 free
    std::vector<Node> mNodes;

    void selectNode(const Node &n)
    {
        if (n.kind == 0) { mSelType = 0; }
        else if (n.kind == 1) { mSelType = 1; }
        else { mSelType = 2; mSelSlot = n.slot; }
        buildDetail();
        repaint();
    }
    bool isSelected(const Node &n) const
    {
        if (n.kind == 0) return mSelType == 0;
        if (n.kind == 1) return mSelType == 1;
        return mSelType == 2 && n.slot == mSelSlot;
    }

    int firstFreeSlot() const
    {
        for (int i = 0; i < 8; ++i) if (slotType(i) == 0) return i;
        return -1;
    }

    // ---------------------------------------------------------------- chain layout
    void layoutChain()
    {
        mNodes.clear();
        if (mChainRect.isEmpty()) return;
        const int nodeW = 84, nodeH = 40, gap = 16, inW = 34, ampW = 56;
        const int cy = mChainRect.getCentreY();
        const int yMid = cy - nodeH / 2;
        const int yTop = mChainRect.getY() + mChainRect.getHeight() / 4 - nodeH / 2;
        const int yBot = mChainRect.getY() + (3 * mChainRect.getHeight()) / 4 - nodeH / 2;

        mInRect = juce::Rectangle<int>(mChainRect.getX(), yMid, inW, nodeH);
        int x = mChainRect.getX() + inW + gap;

        // locked pair in the chosen order, then trunk free slots.
        const bool envFirst = getC("pbFrontOrder") == 0;
        const int firstKind = envFirst ? 0 : 1, secondKind = envFirst ? 1 : 0;
        mNodes.push_back({firstKind, -1, {x, yMid, nodeW, nodeH}}); x += nodeW + gap;
        mNodes.push_back({secondKind, -1, {x, yMid, nodeW, nodeH}}); x += nodeW + gap;
        for (int i = 0; i < 8; ++i)
            if (slotType(i) != 0 && slotLane(i) == 0)
            { mNodes.push_back({2, i, {x, yMid, nodeW, nodeH}}); x += nodeW + gap; }

        mSplitX = x + gap / 2;
        const int laneStart = mSplitX + gap;
        int xa = laneStart, xb = laneStart;
        for (int i = 0; i < 8; ++i)
            if (slotType(i) != 0 && slotLane(i) == 1)
            { mNodes.push_back({2, i, {xa, yTop, nodeW, nodeH}}); xa += nodeW + gap; }
        for (int i = 0; i < 8; ++i)
            if (slotType(i) != 0 && slotLane(i) == 2)
            { mNodes.push_back({2, i, {xb, yBot, nodeW, nodeH}}); xb += nodeW + gap; }

        mAmpX = juce::jmax(juce::jmax(xa, xb) + gap, mSplitX + gap * 2);
        mAmpX = juce::jmin(mAmpX, mChainRect.getRight() - ampW);
        mAmpARect = {mAmpX, yTop, ampW, nodeH};
        mAmpBRect = {mAmpX, yBot, ampW, nodeH};
    }

    void paintChain(juce::Graphics &g)
    {
        if (mChainRect.isEmpty()) return;
        const int cy = mChainRect.getCentreY();

        drawPill(g, mInRect, colors::tile, colors::outline, "IN", colors::textDim);

        // trunk cable IN -> split
        g.setColour(colors::laneColour(0).withAlpha(0.9f));
        g.drawLine((float)mInRect.getRight(), (float)cy, (float)mSplitX, (float)cy, 2.0f);
        // split dot
        g.setColour(colors::accent);
        g.fillEllipse((float)mSplitX - 3.0f, (float)cy - 3.0f, 6.0f, 6.0f);
        // lane cables
        drawLaneCable(g, mAmpARect.getCentreY(), colors::laneColour(0));
        drawLaneCable(g, mAmpBRect.getCentreY(), colors::laneColour(1));
        // amps
        drawPill(g, mAmpARect, colors::tile, colors::laneColour(0), "A", colors::laneColour(0));
        drawPill(g, mAmpBRect, colors::tile, colors::laneColour(1), "B", colors::laneColour(1));

        for (const auto &n : mNodes) drawNode(g, n);
    }

    void drawLaneCable(juce::Graphics &g, int laneY, juce::Colour c)
    {
        g.setColour(c.withAlpha(0.9f));
        juce::Path p;
        p.startNewSubPath((float)mSplitX, (float)mChainRect.getCentreY());
        p.lineTo((float)mSplitX, (float)laneY);
        p.lineTo((float)mAmpX, (float)laneY);
        g.strokePath(p, juce::PathStrokeType(2.0f));
    }

    // ---- per-pedal livery (drives reuse the real-pedal palette; the others get a
    //      family tint so the board reads at a glance) ----
    colors::AccentPair slotAccent(int type, int i) const
    {
        using AP = colors::AccentPair; using C = juce::Colour;
        if (type == 1) return colors::driveModelAccent(getC(sid(i, "dCat")), getC(sid(i, "bModel")));
        if (type == 2) // Modulation — violet family per model
        {
            static const C mv[5] = {C(0xffc7a6f0), C(0xff9b8ce8), C(0xffb69af0), C(0xffa6c0f0), C(0xffcaa6f0)};
            const C a = mv[juce::jlimit(0, 4, getC(sid(i, "mType")))];
            return AP{a, a.darker(0.72f)};
        }
        if (type == 3) // Delay — green family per model
        {
            static const C dv[3] = {C(0xff7fd08a), C(0xff62c69a), C(0xff9ad07f)};
            const C a = dv[juce::jlimit(0, 2, getC(sid(i, "pModel")))];
            return AP{a, a.darker(0.72f)};
        }
        return AP{colors::outline, colors::outline.darker(0.4f)};
    }
    static colors::AccentPair lockedPair(int which) // 0 env (teal), 1 comp (blue)
    {
        using C = juce::Colour;
        const C a = which == 0 ? C(0xff6fd0c9) : C(0xff8fb3ff);
        return colors::AccentPair{a, a.darker(0.72f)};
    }

    // Family art glyphs (drives delegate to the shared paintDriveGlyph).
    void paintSlotGlyph(juce::Graphics &g, int nodeKind, int type, int i, juce::Rectangle<float> box, juce::Colour col) const
    {
        if (nodeKind == 0) { paintEnvGlyph(g, box, col); return; }
        if (nodeKind == 1) { paintCompGlyph(g, box, col); return; }
        if (type == 1) { paintDriveGlyph(g, getC(sid(i, "dCat")), getC(sid(i, "bModel")), box, col); return; }
        if (type == 2) { paintModGlyph(g, box, col); return; }
        if (type == 3) { paintDelayGlyph(g, box, col); return; }
    }
    static juce::PathStrokeType glyphStroke(float w) { return {w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded}; }
    static void paintModGlyph(juce::Graphics &g, juce::Rectangle<float> b, juce::Colour c)
    {
        g.setColour(c);
        juce::Path p; const float y = b.getCentreY(), amp = b.getHeight() * 0.32f; const int N = 40;
        for (int k = 0; k <= N; ++k)
        {
            const float x = b.getX() + b.getWidth() * (float)k / N;
            const float yy = y - amp * std::sin((float)k / N * juce::MathConstants<float>::twoPi * 1.5f);
            if (k == 0) p.startNewSubPath(x, yy); else p.lineTo(x, yy);
        }
        g.strokePath(p, glyphStroke(juce::jmax(1.4f, b.getHeight() * 0.06f)));
    }
    static void paintDelayGlyph(juce::Graphics &g, juce::Rectangle<float> b, juce::Colour c)
    {
        const int n = 4; const float bw = b.getWidth() * 0.11f;
        for (int k = 0; k < n; ++k)
        {
            const float h = b.getHeight() * (0.92f - 0.19f * k);
            const float x = b.getX() + b.getWidth() * (0.14f + 0.24f * k);
            g.setColour(c.withAlpha(1.0f - 0.18f * k));
            g.fillRoundedRectangle(x, b.getCentreY() - h * 0.5f, bw, h, bw * 0.4f);
        }
    }
    static void paintCompGlyph(juce::Graphics &g, juce::Rectangle<float> b, juce::Colour c)
    {
        g.setColour(c); // classic diamond
        juce::Path d;
        d.startNewSubPath(b.getCentreX(), b.getY());
        d.lineTo(b.getRight(), b.getCentreY());
        d.lineTo(b.getCentreX(), b.getBottom());
        d.lineTo(b.getX(), b.getCentreY());
        d.closeSubPath();
        g.strokePath(d, glyphStroke(juce::jmax(1.4f, b.getHeight() * 0.06f)));
    }
    static void paintEnvGlyph(juce::Graphics &g, juce::Rectangle<float> b, juce::Colour c)
    {
        g.setColour(c); // resonant filter sweep (rise to a peak, then roll off)
        juce::Path p;
        p.startNewSubPath(b.getX(), b.getBottom());
        p.quadraticTo(b.getX() + b.getWidth() * 0.45f, b.getBottom(),
                      b.getX() + b.getWidth() * 0.60f, b.getY());
        p.quadraticTo(b.getX() + b.getWidth() * 0.74f, b.getBottom() - b.getHeight() * 0.15f,
                      b.getRight(), b.getBottom() - b.getHeight() * 0.05f);
        g.strokePath(p, glyphStroke(juce::jmax(1.4f, b.getHeight() * 0.06f)));
    }

    // Draw the enclosure body (gradient + tint border), the DrivePedal look.
    void paintEnclosure(juce::Graphics &g, juce::Rectangle<float> r, juce::Colour tint,
                        bool on, bool selected, float radius) const
    {
        const juce::Colour t = on ? tint : juce::Colour(0xff343a43);
        const juce::Colour encTop = juce::Colour(0xff262b33).overlaidWith(t.withAlpha(0.20f));
        const juce::Colour encBot = juce::Colour(0xff15181d).overlaidWith(t.withAlpha(0.05f));
        juce::ColourGradient base(encTop, 0.0f, r.getY(), encBot, 0.0f, r.getBottom(), false);
        dither::fillRoundedRectangle(g, base, r, radius);
        g.setColour((selected ? colors::accent : t).withAlpha(selected ? 1.0f : (on ? 0.55f : 0.34f)));
        g.drawRoundedRectangle(r.reduced(0.5f), radius, selected ? 2.0f : 1.4f);
    }

    void drawNode(juce::Graphics &g, const Node &n)
    {
        colors::AccentPair pair; juce::String name, sub; bool on = true; int type = 2;
        if (n.kind == 0) { pair = lockedPair(0); name = "ENV"; on = getF("envfilterOn") >= 0.5f; }
        else if (n.kind == 1) { pair = lockedPair(1); name = "COMP"; on = getF("compOn") >= 0.5f; }
        else { type = slotType(n.slot); pair = slotAccent(type, n.slot); name = typeShort(type); sub = slotModelName(n.slot); on = slotOn(n.slot); }

        auto r = n.rect.toFloat();
        paintEnclosure(g, r, pair.tint, on, isSelected(n), 8.0f);

        // silkscreen glyph (centre, faint)
        auto gbox = r.reduced(r.getWidth() * 0.22f, 0.0f).withTrimmedTop(15.0f).withTrimmedBottom(sub.isEmpty() ? 6.0f : 15.0f);
        paintSlotGlyph(g, n.kind, type, n.slot, gbox, (on ? pair.led : juce::Colour(0xff5a616b)).withAlpha(0.85f));

        // name (top, accent)
        g.setColour(on ? pair.accent : colors::textDim);
        g.setFont(fonts::archivo(9.0f, fonts::Bold, 0.10f));
        g.drawText(name, n.rect.withHeight(14).translated(0, 3), juce::Justification::centred);
        // model sub (bottom)
        if (sub.isNotEmpty())
        {
            juce::Rectangle<int> subR = n.rect;
            subR = subR.removeFromBottom(12).withTrimmedBottom(1);
            g.setColour(colors::textDim);
            g.setFont(fonts::mono(7.5f, fonts::Medium, 0.02f));
            g.drawText(sub, subR, juce::Justification::centred);
        }
        // jewel LED (top-right), glow when on
        auto jewel = juce::Rectangle<float>(7.0f, 7.0f).withPosition(r.getRight() - 12.0f, r.getY() + 5.0f);
        if (on) fx::glowEllipse(g, jewel, pair.led, 9, 0.55f, 5, 0.5f);
        g.setColour(on ? pair.led : colors::ledOff);
        g.fillEllipse(jewel);
    }

    static void drawPill(juce::Graphics &g, juce::Rectangle<int> ri, juce::Colour fill,
                         juce::Colour border, const juce::String &txt, juce::Colour txtCol)
    {
        auto r = ri.toFloat();
        g.setColour(fill); g.fillRoundedRectangle(r, 7.0f);
        g.setColour(border); g.drawRoundedRectangle(r.reduced(0.5f), 7.0f, 1.2f);
        g.setColour(txtCol); g.setFont(fonts::archivo(10.5f, fonts::Bold, 0.06f));
        g.drawText(txt, ri, juce::Justification::centred);
    }

    // -------------------------------------------------------------- palette (right)
    juce::Rectangle<int> mPalDrive, mPalMod, mPalDelay, mImportRect;
    void layoutPalette()
    {
        auto r = mPaletteRect;
        r.removeFromTop(4);
        const int h = 44, gap = 10;
        mPalDrive = r.removeFromTop(h); r.removeFromTop(gap);
        mPalMod = r.removeFromTop(h); r.removeFromTop(gap);
        mPalDelay = r.removeFromTop(h); r.removeFromTop(gap * 2);
        mImportRect = r.removeFromTop(38);
    }
    void paintPalette(juce::Graphics &g)
    {
        g.setColour(colors::caption);
        g.setFont(fonts::mono(8.5f, fonts::Medium, 0.08f));
        g.drawText("ADD PEDAL", juce::Rectangle<int>(mPaletteRect.getX(), mPaletteRect.getY() - 14,
                                                     mPaletteRect.getWidth(), 12),
                   juce::Justification::centredLeft);
        paintPaletteBtn(g, mPalDrive, "DRIVE", typeAccent(1));
        paintPaletteBtn(g, mPalMod, "MODULATION", typeAccent(2));
        paintPaletteBtn(g, mPalDelay, "DELAY", typeAccent(3));

        // Import-legacy button: copies the (still-live) legacy front chain into the pool.
        {
            auto rr = mImportRect.toFloat();
            g.setColour(colors::tile); g.fillRoundedRectangle(rr, 8.0f);
            g.setColour(colors::accent.withAlpha(0.7f)); g.drawRoundedRectangle(rr.reduced(0.5f), 8.0f, 1.2f);
            g.setColour(colors::textDim); g.setFont(fonts::mono(8.5f, fonts::Medium, 0.04f));
            g.drawText("IMPORT LEGACY", mImportRect, juce::Justification::centred);
        }
    }
    static void paintPaletteBtn(juce::Graphics &g, juce::Rectangle<int> ri, const juce::String &txt, juce::Colour ac)
    {
        auto r = ri.toFloat();
        g.setColour(colors::tile); g.fillRoundedRectangle(r, 8.0f);
        g.setColour(ac.withAlpha(0.8f)); g.drawRoundedRectangle(r.reduced(0.5f), 8.0f, 1.3f);
        g.setColour(ac); g.fillRoundedRectangle(r.withWidth(4.0f).reduced(0.0f, 8.0f).translated(4.0f, 0.0f), 2.0f);
        g.setColour(colors::text); g.setFont(fonts::archivo(11.0f, fonts::SemiBold, 0.02f));
        g.drawText("+  " + txt, ri.reduced(14, 0), juce::Justification::centredLeft);
    }

    void showAddMenu(int family) // 1 drive, 2 mod, 3 delay
    {
        juce::PopupMenu m;
        if (family == 1)
        {
            for (int cat = 1; cat <= 4; ++cat) // Boost/OD/Dist/Fuzz -> dCat = cat-1
            {
                const auto k = (nam_rig::DriveBlock::Kind)cat;
                const int n = nam_rig::DriveBlock::modelCount(k);
                juce::PopupMenu sub;
                for (int mdl = 0; mdl < n; ++mdl)
                    sub.addItem(cat * 100 + mdl + 1, nam_rig::DriveBlock::modelName(k, mdl));
                static const char *catNm[] = {"", "Boost", "Overdrive", "Distortion", "Fuzz"};
                m.addSubMenu(catNm[cat], sub);
            }
        }
        else if (family == 2)
        {
            static const char *m2[] = {"Chorus", "Phaser", "Flanger", "Tremolo", "Uni-Vibe"};
            for (int i = 0; i < 5; ++i) m.addItem(200 + i + 1, m2[i]);
        }
        else
        {
            static const char *m3[] = {"Boss DD-7", "Carbon Copy", "Memory Man"};
            for (int i = 0; i < 3; ++i) m.addItem(300 + i + 1, m3[i]);
        }
        juce::Component::SafePointer<PedalboardPanel> sp(this);
        auto anchor = (family == 1 ? mPalDrive : family == 2 ? mPalMod : mPalDelay);
        m.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(localAreaToGlobalRect(anchor)),
                        [sp, family](int r)
                        {
                            if (sp == nullptr || r <= 0) return;
                            sp->addPedal(family, r);
                        });
    }

    void addPedal(int family, int r)
    {
        // target slot: the selected free slot if it's empty, else the first empty.
        int slot = (mSelType == 2 && slotType(mSelSlot) == 0) ? mSelSlot : firstFreeSlot();
        if (slot < 0) return; // board full
        if (family == 1)
        {
            const int cat = r / 100, mdl = (r % 100) - 1; // cat 1..4 = Boost..Fuzz = dCat/Kind
            setC(sid(slot, "Type"), 1);
            setC(sid(slot, "dCat"), cat);
            setC(sid(slot, "bModel"), mdl);
        }
        else if (family == 2)
        {
            setC(sid(slot, "Type"), 2);
            setC(sid(slot, "mType"), r - 201);
        }
        else
        {
            setC(sid(slot, "Type"), 3);
            setC(sid(slot, "pModel"), r - 301);
        }
        setC(sid(slot, "Lane"), 0); // Both
        if (auto *pr = mApvts.getParameter(sid(slot, "On"))) { pr->beginChangeGesture(); pr->setValueNotifyingHost(1.0f); pr->endChangeGesture(); }
        if (!enabled()) setBool("pbEnabled", true);
        mSelType = 2; mSelSlot = slot;
        buildDetail();
        repaint();
    }
    void setBool(const juce::String &id, bool on)
    {
        if (auto *pr = mApvts.getParameter(id)) { pr->beginChangeGesture(); pr->setValueNotifyingHost(on ? 1.0f : 0.0f); pr->endChangeGesture(); }
    }
    // Write a raw (natural-units) value to a param, mapped through its range.
    void setFloatP(const juce::String &id, float val)
    {
        if (auto *pr = mApvts.getParameter(id)) { pr->beginChangeGesture(); pr->setValueNotifyingHost(pr->convertTo0to1(val)); pr->endChangeGesture(); }
    }
    // Copy one param's value to another (same range/type mirrored across legacy<->pool).
    void copyP(const juce::String &src, const juce::String &dst) { setFloatP(dst, getF(src)); }

    // ---- Import the (still-live) legacy front chain into the pool (opt-in; nothing is
    // deleted — the legacy params stay put, this just mirrors them into the pool + turns
    // the board on so enabling it reproduces the current sound). Env/Comp are shared, so
    // only the drive rack + premod + predelay need copying. ----
    void importLegacy()
    {
        static const char *driveSuf[] = {"bDrive", "bRange", "bModel", "oDrive", "oTone", "oLevel",
                                         "dDrive", "dTone", "dLevel", "dMigrate", "fDrive", "fTone", "fLevel", "fGate"};
        // driveSend: 0 Amp A, 1 Amp B, 2 Both -> Lane: 0 Both, 1 Amp A, 2 Amp B
        const int ds = getC("driveSend");
        const int driveLane = ds == 0 ? 1 : ds == 1 ? 2 : 0;
        for (int n = 1; n <= 3; ++n)
        {
            const int slot = n - 1;
            const juce::String dp = "drv" + juce::String(n);
            for (auto *suf : driveSuf) copyP(dp + suf, sid(slot, suf));
            const int dt = getC(dp + "Type"); // 0 Off, 1 Boost, 2 OD, 3 Dist, 4 Fuzz == dCat/Kind
            if (dt == 0) { setC(sid(slot, "Type"), 0); }
            else
            {
                setC(sid(slot, "Type"), 1);
                setC(sid(slot, "dCat"), dt);
                setC(sid(slot, "Lane"), driveLane);
                setBool(sid(slot, "On"), getF(dp + "On") >= 0.5f);
            }
        }
        // premod -> slot 3 (Mod)
        copyP("premodRate", sid(3, "mRate")); copyP("premodDepth", sid(3, "mDepth"));
        copyP("premodMix", sid(3, "mMix")); copyP("premodFeedback", sid(3, "mFeedback"));
        copyP("premodWave", sid(3, "mWave"));
        setC(sid(3, "mType"), getC("premodType")); setC(sid(3, "mSync"), getC("premodSync"));
        setC(sid(3, "mPhaserVoice"), getC("premodPhaserVoice"));
        const bool premodOn = getF("premodOn") >= 0.5f;
        setC(sid(3, "Type"), premodOn ? 2 : 0); setC(sid(3, "Lane"), 0); setBool(sid(3, "On"), premodOn);
        // predelay -> slot 4 (Delay)
        copyP("predelayTime", sid(4, "pTime")); copyP("predelayFeedback", sid(4, "pFeedback"));
        copyP("predelayMix", sid(4, "pMix")); copyP("predelayMod", sid(4, "pMod"));
        copyP("predelayTone", sid(4, "pTone")); copyP("predelayLevel", sid(4, "pLevel"));
        setC(sid(4, "pModel"), getC("predelayModel")); setC(sid(4, "pMode"), getC("predelayMode"));
        setC(sid(4, "pSync"), getC("predelaySync")); setC(sid(4, "pChorusVib"), getC("predelayChorusVib"));
        const bool predelayOn = getF("predelayOn") >= 0.5f;
        setC(sid(4, "Type"), predelayOn ? 3 : 0); setC(sid(4, "Lane"), 0); setBool(sid(4, "On"), predelayOn);

        setBool("pbEnabled", true); // now the board reproduces the imported chain
        mSelType = 2; mSelSlot = 0;
        buildDetail();
        repaint();
    }
    void removeSelected()
    {
        if (mSelType != 2) return;
        setC(sid(mSelSlot, "Type"), 0);
        buildDetail();
        repaint();
    }

    juce::Rectangle<int> localAreaToGlobalRect(juce::Rectangle<int> r)
    {
        return localAreaToGlobal(r);
    }

    // ---------------------------------------------------------------- detail zone
    std::vector<std::unique_ptr<juce::Component>> mDetailOwned;
    std::vector<LabeledKnob *> mKnobs;
    std::unique_ptr<SegmentedControl> mTypeSeg;  // category / mod-type / delay-model
    std::unique_ptr<SegmentedControl> mAuxSeg;   // range / gate / migrate / mode / voice / phaser-voice
    std::unique_ptr<SegmentedControl> mRouteSeg; // pbS{i}Lane (bound)
    std::unique_ptr<ToggleSwitch> mOnSw;
    DrivePedal *mDrivePedal = nullptr; // the REAL drive-pedal widget, hosted for drive slots
    bool mHasModel = false, mHasRemove = false;
    juce::Rectangle<int> mModelRect, mRemoveRect;
    juce::String mModelLabel, mDetailTitle;

    LabeledKnob *makeKnob(const juce::String &id, const juce::String &cap)
    {
        auto k = std::make_unique<LabeledKnob>(mApvts, id, cap);
        auto *raw = k.get();
        addAndMakeVisible(*raw);
        mKnobs.push_back(raw);
        mDetailOwned.push_back(std::move(k));
        return raw;
    }

    void buildDetail()
    {
        // tear down previous detail widgets
        mKnobs.clear();
        mTypeSeg.reset(); mAuxSeg.reset(); mRouteSeg.reset(); mOnSw.reset();
        mDrivePedal = nullptr; // owned via mDetailOwned; cleared below
        mDetailOwned.clear();
        mHasModel = mHasRemove = false;
        mModelLabel.clear();

        if (mSelType == 0) buildEnv();
        else if (mSelType == 1) buildComp();
        else buildFree(mSelSlot);

        layoutDetail();
        repaint();
    }

    void buildEnv()
    {
        mDetailTitle = "ENV FILTER  (locked front)";
        mTypeSeg = std::make_unique<SegmentedControl>(mApvts, "envfilterVoice", juce::StringArray{"FX25", "Q-Tron"});
        addAndMakeVisible(*mTypeSeg);
        mAuxSeg = std::make_unique<SegmentedControl>(mApvts, "envfilterMode", juce::StringArray{"LP", "BP", "HP", "MIX"});
        addAndMakeVisible(*mAuxSeg);
        mOnSw = std::make_unique<ToggleSwitch>(mApvts, "envfilterOn"); addAndMakeVisible(*mOnSw);
        makeKnob("envfilterSens", "SENS");
        makeKnob("envfilterRange", "RANGE");
        makeKnob("envfilterReso", "PEAK");
        makeKnob("envfilterMix", "BLEND");
    }
    void buildComp()
    {
        mDetailTitle = "COMPRESSOR  (locked front)";
        mTypeSeg = std::make_unique<SegmentedControl>(mApvts, "compMode", juce::StringArray{"Clean", "OTA", "Opto", "FET"});
        addAndMakeVisible(*mTypeSeg);
        mOnSw = std::make_unique<ToggleSwitch>(mApvts, "compOn"); addAndMakeVisible(*mOnSw);
        makeKnob("compSustain", "SUSTAIN");
        makeKnob("compRatio", "RATIO");
        makeKnob("compAttack", "ATTACK");
        makeKnob("compRelease", "RELEASE");
        makeKnob("compLevel", "LEVEL");
        makeKnob("compDry", "DRY");
    }
    void buildFree(int i)
    {
        const int type = slotType(i);
        if (type == 0) { mDetailTitle = "EMPTY SLOT  -  add a pedal from the palette"; return; }

        mHasRemove = true;
        mRouteSeg = std::make_unique<SegmentedControl>(mApvts, sid(i, "Lane"), juce::StringArray{"Both", "Amp A", "Amp B"});
        addAndMakeVisible(*mRouteSeg);

        if (type == 1) { buildDrive(i); return; } // the real DrivePedal owns Type/model/knobs/On

        // mod / delay get their own footswitch (drive's lives inside the DrivePedal)
        mOnSw = std::make_unique<ToggleSwitch>(mApvts, sid(i, "On")); addAndMakeVisible(*mOnSw);
        if (type == 2) buildMod(i);
        else buildDelay(i);
    }

    // manual selector (unbound) that writes a param + rebuilds the detail on change.
    std::unique_ptr<SegmentedControl> makeRebuildSeg(const juce::String &paramId, juce::StringArray opts)
    {
        auto seg = std::make_unique<SegmentedControl>(opts); // manual mode
        seg->setActive(juce::jlimit(0, opts.size() - 1, getC(paramId)));
        juce::Component::SafePointer<PedalboardPanel> sp(this);
        seg->onChange = [sp, paramId](int idx)
        {
            if (sp == nullptr) return;
            sp->setC(paramId, idx);
            sp->scheduleRebuild();
        };
        addAndMakeVisible(*seg);
        return seg;
    }
    void scheduleRebuild()
    {
        juce::Component::SafePointer<PedalboardPanel> sp(this);
        juce::MessageManager::callAsync([sp] { if (sp) sp->buildDetail(); });
    }

    void buildDrive(int i)
    {
        // Host the REAL DrivePedal widget (identical to the old DRIVE panel), bound to this
        // slot's pool params via the prefix + the 5-value dCat selector. It owns the pedal
        // enclosure, silkscreen art, model pill/picker, Drive/Tone/Level knobs, footswitch,
        // and the Range/Gate/Migrate segs. The pool only adds Route + Remove around it.
        mDetailTitle = "DRIVE  -  slot " + juce::String(i + 1);
        auto dp = std::make_unique<DrivePedal>(mApvts, "pbS" + juce::String(i), sid(i, "dCat"));
        mDrivePedal = dp.get();
        addAndMakeVisible(*dp);
        mDetailOwned.push_back(std::move(dp));
    }
    void buildMod(int i)
    {
        const int mt = juce::jlimit(0, 4, getC(sid(i, "mType")));
        mDetailTitle = "MODULATION  -  slot " + juce::String(i + 1);
        mTypeSeg = makeRebuildSeg(sid(i, "mType"), juce::StringArray{"Chorus", "Phaser", "Flanger", "Tremolo", "Uni-Vibe"});
        makeKnob(sid(i, "mRate"), "RATE");
        if (mt == 3) makeKnob(sid(i, "mWave"), "SHAPE"); else makeKnob(sid(i, "mDepth"), "DEPTH");
        makeKnob(sid(i, "mMix"), "MIX");
        if (mt == 1 || mt == 2) makeKnob(sid(i, "mFeedback"), mt == 2 ? "REGEN" : "RESO");
        if (mt == 1)
        {
            mAuxSeg = std::make_unique<SegmentedControl>(mApvts, sid(i, "mPhaserVoice"), juce::StringArray{"Script", "Block"});
            addAndMakeVisible(*mAuxSeg);
        }
    }
    void buildDelay(int i)
    {
        mDetailTitle = "DELAY  -  slot " + juce::String(i + 1);
        mTypeSeg = makeRebuildSeg(sid(i, "pModel"), juce::StringArray{"DD-7", "Carbon", "Mem Man"});
        makeKnob(sid(i, "pTime"), "TIME");
        makeKnob(sid(i, "pFeedback"), "FEEDBK");
        makeKnob(sid(i, "pMix"), "MIX");
        makeKnob(sid(i, "pMod"), "MOD");
        makeKnob(sid(i, "pTone"), "TONE");
        makeKnob(sid(i, "pLevel"), "LEVEL");
    }

    void showModelMenu()
    {
        if (mSelType != 2 || slotType(mSelSlot) != 1) return;
        const int i = mSelSlot;
        const auto k = (nam_rig::DriveBlock::Kind)(getC(sid(i, "dCat")));
        const int n = nam_rig::DriveBlock::modelCount(k);
        juce::PopupMenu m;
        for (int mdl = 0; mdl < n; ++mdl) m.addItem(mdl + 1, nam_rig::DriveBlock::modelName(k, mdl));
        juce::Component::SafePointer<PedalboardPanel> sp(this);
        m.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(localAreaToGlobalRect(mModelRect)),
                        [sp, i](int r) { if (sp && r > 0) { sp->setC(sp->sid(i, "bModel"), r - 1); sp->scheduleRebuild(); } });
    }

    // ---------------------------------------------------------------- detail layout
    // Everything sits on ONE centred pedal-shaped enclosure (a real stompbox on the dark
    // stage): header -> category selector -> model -> big silkscreen glyph -> knob row ->
    // aux selector -> bottom route/on/remove. Mirrors the DrivePedal face so it reads as
    // a pedal instead of a lonely knob in a void.
    void layoutDetail()
    {
        if (mDetailRect.isEmpty()) return;
        const int pw = juce::jmin(520, mDetailRect.getWidth() - 40);
        const int ph = juce::jmax(60, mDetailRect.getHeight() - 14);
        mPedalRect = {mDetailRect.getCentreX() - pw / 2, mDetailRect.getY() + 7, pw, ph};

        // Drive slots host the real DrivePedal (it paints/lays out its own face); the pool
        // only frames it with a Route + Remove row along the bottom.
        if (mDrivePedal)
        {
            auto area = mDetailRect.reduced(8, 8);
            auto bot = area.removeFromBottom(30);
            auto brow = bot.withSizeKeepingCentre(juce::jmin(bot.getWidth(), 380), 26);
            mRemoveRect = {};
            if (mHasRemove) { mRemoveRect = brow.removeFromRight(74).withSizeKeepingCentre(70, 26); brow.removeFromRight(10); }
            if (mRouteSeg) { const int w = juce::jmin(brow.getWidth(), 220); mRouteSeg->setBounds(brow.removeFromLeft(w).withSizeKeepingCentre(w, 24)); }
            area.removeFromBottom(8);
            const int dw = juce::jmin(360, area.getWidth());
            mDrivePedal->setBounds(area.withSizeKeepingCentre(dw, area.getHeight()));
            return;
        }

        auto in = mPedalRect.reduced(24, 16);
        in.removeFromTop(24); // header band (drawn in paint)

        if (mTypeSeg)
        {
            auto row = in.removeFromTop(26);
            const int w = juce::jlimit(200, row.getWidth(), mTypeSeg->idealWidth() > 0 ? mTypeSeg->idealWidth() + 12 : 280);
            mTypeSeg->setBounds(row.withSizeKeepingCentre(w, 24));
            in.removeFromTop(9);
        }
        mModelRect = {};
        if (mHasModel) { auto row = in.removeFromTop(26); mModelRect = row.withSizeKeepingCentre(juce::jmin(row.getWidth(), 240), 24); in.removeFromTop(12); }

        mGlyphRect = in.removeFromTop(juce::jlimit(40, 130, in.getHeight() / 3));
        in.removeFromTop(6);

        // bottom row reserved FIRST (so it hugs the base of the pedal), then knobs/aux fill the middle.
        auto bot = in.removeFromBottom(28);
        mRemoveRect = {};
        if (mHasRemove) { mRemoveRect = bot.removeFromRight(74).withSizeKeepingCentre(70, 26); bot.removeFromRight(8); }
        if (mOnSw) { mOnSw->setBounds(bot.removeFromRight(50).withSizeKeepingCentre(46, 24)); bot.removeFromRight(10); }
        if (mRouteSeg) { const int w = juce::jmin(bot.getWidth(), 220); mRouteSeg->setBounds(bot.removeFromLeft(w).withSizeKeepingCentre(w, 24)); }
        in.removeFromBottom(8);

        if (mAuxSeg)
        {
            auto ar = in.removeFromBottom(26);
            mAuxSeg->setBounds(ar.withSizeKeepingCentre(juce::jmin(ar.getWidth(), 220), 22));
            in.removeFromBottom(8);
        }

        // knob row centred in whatever's left
        if (!mKnobs.empty())
        {
            const int rh = juce::jlimit(60, 108, in.getHeight());
            auto row = in.withSizeKeepingCentre(in.getWidth(), rh);
            const int n = (int)mKnobs.size();
            const int kw = juce::jmin(92, row.getWidth() / juce::jmax(1, n));
            int x = row.getCentreX() - kw * n / 2;
            for (auto *k : mKnobs) { k->setBounds(x, row.getY(), kw, row.getHeight()); x += kw; }
        }
    }

    void paintDetailCard(juce::Graphics &g)
    {
        // Drive slots: the hosted DrivePedal paints its own enclosure/art/knobs; the pool
        // only owns the Remove button here (Route seg is a child component).
        if (mDrivePedal)
        {
            if (mHasRemove && !mRemoveRect.isEmpty())
            {
                auto rr = mRemoveRect.toFloat();
                g.setColour(colors::tile); g.fillRoundedRectangle(rr, 6.0f);
                g.setColour(colors::red.withAlpha(0.6f)); g.drawRoundedRectangle(rr.reduced(0.5f), 6.0f, 1.0f);
                g.setColour(colors::red); g.setFont(fonts::archivo(10.0f, fonts::SemiBold, 0.03f));
                g.drawText("REMOVE", mRemoveRect, juce::Justification::centred);
            }
            return;
        }

        colors::AccentPair pair; int selType = 2;
        if (mSelType == 0) pair = lockedPair(0);
        else if (mSelType == 1) pair = lockedPair(1);
        else { selType = slotType(mSelSlot); pair = slotAccent(selType, mSelSlot); }

        if (mPedalRect.isEmpty()) return;
        auto r = mPedalRect.toFloat();
        paintEnclosure(g, r, pair.tint, true, false, 16.0f); // the pedal enclosure

        // header: title (accent silkscreen) + jewel LED
        auto head = mPedalRect.reduced(22, 13).removeFromTop(22);
        g.setColour(pair.accent);
        g.setFont(fonts::archivo(13.5f, fonts::Bold, 0.07f));
        g.drawText(mDetailTitle, head, juce::Justification::centredLeft);
        auto jewel = juce::Rectangle<float>(12.0f, 12.0f).withCentre({(float)head.getRight() - 3.0f, (float)head.getCentreY()});
        fx::glowEllipse(g, jewel, pair.led, 12, 0.6f, 6, 0.5f);
        g.setColour(pair.led); g.fillEllipse(jewel);

        // big silkscreen art glyph on the pedal face
        if (!mGlyphRect.isEmpty())
        {
            const float gw = juce::jmin(130.0f, (float)mGlyphRect.getWidth());
            const float gh = juce::jmin(96.0f, (float)mGlyphRect.getHeight());
            paintSlotGlyph(g, mSelType == 0 ? 0 : mSelType == 1 ? 1 : 2, selType, mSelSlot,
                           mGlyphRect.toFloat().withSizeKeepingCentre(gw, gh), pair.led.withAlpha(0.9f));
        }

        if (mHasModel && !mModelRect.isEmpty())
        {
            auto mr = mModelRect.toFloat();
            g.setColour(colors::tile); g.fillRoundedRectangle(mr, 6.0f);
            g.setColour(colors::outline); g.drawRoundedRectangle(mr.reduced(0.5f), 6.0f, 1.0f);
            g.setColour(colors::text); g.setFont(fonts::mono(9.5f, fonts::Medium, 0.02f));
            g.drawText(mModelLabel, mModelRect.reduced(8, 0), juce::Justification::centredLeft);
        }
        if (mHasRemove && !mRemoveRect.isEmpty())
        {
            auto rr = mRemoveRect.toFloat();
            g.setColour(colors::tile); g.fillRoundedRectangle(rr, 6.0f);
            g.setColour(colors::red.withAlpha(0.6f)); g.drawRoundedRectangle(rr.reduced(0.5f), 6.0f, 1.0f);
            g.setColour(colors::red); g.setFont(fonts::archivo(10.0f, fonts::SemiBold, 0.03f));
            g.drawText("REMOVE", mRemoveRect, juce::Justification::centred);
        }
    }

    // ------------------------------------------------------------------- live sync
    void timerCallback() override
    {
        // repaint on any change that affects the chain / node LEDs / selectors.
        std::uint64_t sig = enabled() ? 1u : 0u;
        sig = sig * 131u + (std::uint64_t)(getC("pbFrontOrder") + 1);
        for (int i = 0; i < 8; ++i)
            sig = sig * 131u + (std::uint64_t)(slotType(i) * 8 + slotLane(i) * 2 + (slotOn(i) ? 1 : 0));
        sig = sig * 131u + (std::uint64_t)((getF("envfilterOn") >= 0.5f ? 2 : 0) + (getF("compOn") >= 0.5f ? 1 : 0));
        if (sig != mLastSig)
        {
            mLastSig = sig;
            setHeaderRight(enabled() ? "ON" : "OFF");
            layoutChain();
            repaint();
        }
    }

    juce::AudioProcessorValueTreeState &mApvts;
    std::unique_ptr<ToggleSwitch> mEnable;
    std::unique_ptr<SegmentedControl> mFrontOrder;

    juce::Rectangle<int> mChainRect, mPaletteRect, mDetailRect;
    juce::Rectangle<int> mPedalRect, mGlyphRect; // the focused pedal enclosure + its silkscreen art
    juce::Rectangle<int> mInRect, mAmpARect, mAmpBRect;
    int mSplitX = 0, mAmpX = 0;
    std::uint64_t mLastSig = ~0ull;
};

} // namespace nam_rig::ui
