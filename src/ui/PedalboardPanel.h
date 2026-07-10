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
#include "RigLookAndFeel.h"
#include "Panels.h" // BlockPanel, LabeledKnob, SegmentedControl, ToggleSwitch, nam_rig::DriveBlock

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
    static juce::Colour lockedAccent(int which) // 0 env, 1 comp
    {
        return which == 0 ? juce::Colour(0xff6fd0c9) /*env teal*/ : juce::Colour(0xff8fb3ff) /*comp blue*/;
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
            const auto k = (nam_rig::DriveBlock::Kind)(getC(sid(i, "dCat")) + 1);
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

    void drawNode(juce::Graphics &g, const Node &n)
    {
        juce::Colour ac;
        juce::String name, sub;
        bool on = true;
        if (n.kind == 0) { ac = lockedAccent(0); name = "ENV"; on = getF("envfilterOn") >= 0.5f; }
        else if (n.kind == 1) { ac = lockedAccent(1); name = "COMP"; on = getF("compOn") >= 0.5f; }
        else { const int t = slotType(n.slot); ac = typeAccent(t); name = typeShort(t); sub = slotModelName(n.slot); on = slotOn(n.slot); }

        auto r = n.rect.toFloat();
        g.setColour(colors::wellTop);
        g.fillRoundedRectangle(r, 8.0f);
        g.setColour((isSelected(n) ? colors::accent : ac).withAlpha(isSelected(n) ? 1.0f : 0.85f));
        g.drawRoundedRectangle(r.reduced(0.5f), 8.0f, isSelected(n) ? 2.0f : 1.3f);
        g.setColour(ac.withAlpha(on ? 0.9f : 0.3f));
        g.fillRoundedRectangle(r.withHeight(4.0f).reduced(6.0f, 0.0f).translated(0, 4.0f), 2.0f);

        g.setColour(on ? colors::text : colors::textDim);
        g.setFont(fonts::archivo(10.5f, fonts::Bold, 0.02f));
        g.drawText(name, n.rect.withTrimmedTop(8).withTrimmedBottom(sub.isEmpty() ? 0 : 12),
                   juce::Justification::centred);
        if (sub.isNotEmpty())
        {
            juce::Rectangle<int> subR = n.rect;                 // local copy (n is const)
            subR = subR.removeFromBottom(13).withTrimmedBottom(2);
            g.setColour(colors::textDim);
            g.setFont(fonts::mono(8.0f, fonts::Medium, 0.02f));
            g.drawText(sub, subR, juce::Justification::centred);
        }
        auto jewel = juce::Rectangle<float>(6.0f, 6.0f).withPosition(r.getRight() - 11.0f, r.getY() + 5.0f);
        g.setColour(on ? colors::accent : colors::ledOff);
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
    juce::Rectangle<int> mPalDrive, mPalMod, mPalDelay;
    void layoutPalette()
    {
        auto r = mPaletteRect;
        r.removeFromTop(4);
        const int h = 44, gap = 10;
        mPalDrive = r.removeFromTop(h); r.removeFromTop(gap);
        mPalMod = r.removeFromTop(h); r.removeFromTop(gap);
        mPalDelay = r.removeFromTop(h);
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
            const int cat = r / 100, mdl = (r % 100) - 1; // cat 1..4
            setC(sid(slot, "Type"), 1);
            setC(sid(slot, "dCat"), cat - 1);
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
        mHasRemove = true;
        mRouteSeg = std::make_unique<SegmentedControl>(mApvts, sid(i, "Lane"), juce::StringArray{"Both", "Amp A", "Amp B"});
        addAndMakeVisible(*mRouteSeg);
        mOnSw = std::make_unique<ToggleSwitch>(mApvts, sid(i, "On")); addAndMakeVisible(*mOnSw);

        if (type == 0) { mDetailTitle = "EMPTY SLOT  -  add a pedal from the palette"; mHasRemove = false; mRouteSeg.reset(); mOnSw.reset(); return; }

        if (type == 1) buildDrive(i);
        else if (type == 2) buildMod(i);
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
        const int cat = juce::jlimit(0, 3, getC(sid(i, "dCat")));
        mDetailTitle = "DRIVE  -  slot " + juce::String(i + 1);
        mTypeSeg = makeRebuildSeg(sid(i, "dCat"), juce::StringArray{"Boost", "OD", "Dist", "Fuzz"});
        mHasModel = true;
        const auto k = (nam_rig::DriveBlock::Kind)(cat + 1);
        mModelLabel = "MODEL:  " + juce::String(nam_rig::DriveBlock::modelName(k, getC(sid(i, "bModel"))));

        switch (cat)
        {
        case 0: // Boost
            makeKnob(sid(i, "bDrive"), "BOOST");
            mAuxSeg = makeRebuildSeg(sid(i, "bRange"), juce::StringArray{"Treble", "Mid", "Full"});
            break;
        case 1: // Overdrive
            makeKnob(sid(i, "oDrive"), "DRIVE");
            makeKnob(sid(i, "oTone"), "TONE");
            makeKnob(sid(i, "oLevel"), "LEVEL");
            break;
        case 2: // Distortion
            makeKnob(sid(i, "dDrive"), "DRIVE");
            makeKnob(sid(i, "dTone"), "FILTER");
            makeKnob(sid(i, "dLevel"), "VOLUME");
            mAuxSeg = makeRebuildSeg(sid(i, "dMigrate"), juce::StringArray{"Tight", "Full"});
            break;
        default: // Fuzz
            makeKnob(sid(i, "fDrive"), "FUZZ");
            if (getC(sid(i, "bModel")) == 1) makeKnob(sid(i, "fTone"), "TONE"); // Big Muff only
            makeKnob(sid(i, "fLevel"), "VOLUME");
            mAuxSeg = std::make_unique<SegmentedControl>(mApvts, sid(i, "fGate"), juce::StringArray{"Off", "Gate"});
            addAndMakeVisible(*mAuxSeg);
            break;
        }
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
        const auto k = (nam_rig::DriveBlock::Kind)(getC(sid(i, "dCat")) + 1);
        const int n = nam_rig::DriveBlock::modelCount(k);
        juce::PopupMenu m;
        for (int mdl = 0; mdl < n; ++mdl) m.addItem(mdl + 1, nam_rig::DriveBlock::modelName(k, mdl));
        juce::Component::SafePointer<PedalboardPanel> sp(this);
        m.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(localAreaToGlobalRect(mModelRect)),
                        [sp, i](int r) { if (sp && r > 0) { sp->setC(sp->sid(i, "bModel"), r - 1); sp->scheduleRebuild(); } });
    }

    // ---------------------------------------------------------------- detail layout
    void layoutDetail()
    {
        if (mDetailRect.isEmpty()) return;
        auto area = mDetailRect.reduced(10, 8);
        area.removeFromTop(24); // title band drawn in paint

        // control row: type/category selector + (model button) + spacer + route + on + remove
        auto ctl = area.removeFromTop(30);
        if (mTypeSeg) mTypeSeg->setBounds(ctl.removeFromLeft(juce::jmin(190, mTypeSeg->idealWidth() > 0 ? mTypeSeg->idealWidth() : 190)).withSizeKeepingCentre(juce::jmin(190, ctl.getWidth()), 24));
        ctl.removeFromLeft(10);
        mModelRect = {};
        if (mHasModel) { mModelRect = ctl.removeFromLeft(150).withSizeKeepingCentre(146, 26); ctl.removeFromLeft(10); }

        auto rightCtl = ctl;
        mRemoveRect = {};
        if (mHasRemove) { mRemoveRect = rightCtl.removeFromRight(72).withSizeKeepingCentre(66, 26); rightCtl.removeFromRight(8); }
        if (mOnSw) { mOnSw->setBounds(rightCtl.removeFromRight(50).withSizeKeepingCentre(46, 24)); rightCtl.removeFromRight(8); }
        if (mRouteSeg) mRouteSeg->setBounds(rightCtl.removeFromRight(juce::jmin(150, rightCtl.getWidth())).withSizeKeepingCentre(juce::jmin(150, rightCtl.getWidth()), 24));

        area.removeFromTop(10);

        // aux seg row (range/gate/migrate/mode/voice/phaser), if any
        if (mAuxSeg)
        {
            auto ar = area.removeFromTop(26);
            mAuxSeg->setBounds(ar.removeFromLeft(juce::jmin(180, ar.getWidth())).withSizeKeepingCentre(juce::jmin(180, ar.getWidth()), 22));
            area.removeFromTop(8);
        }

        // knob row
        if (!mKnobs.empty())
        {
            auto row = area.removeFromTop(juce::jmin(96, area.getHeight()));
            const int n = (int)mKnobs.size();
            const int kw = juce::jmin(96, row.getWidth() / juce::jmax(1, n));
            const int total = kw * n;
            int x = row.getCentreX() - total / 2;
            for (auto *k : mKnobs) { k->setBounds(x, row.getY(), kw, row.getHeight()); x += kw; }
        }
    }

    void paintDetailCard(juce::Graphics &g)
    {
        auto r = mDetailRect.toFloat();
        g.setColour(colors::inset);
        g.fillRoundedRectangle(r, 10.0f);
        g.setColour(colors::outline);
        g.drawRoundedRectangle(r.reduced(0.5f), 10.0f, 1.0f);

        // title band
        juce::Colour ac = mSelType == 0 ? lockedAccent(0) : mSelType == 1 ? lockedAccent(1) : typeAccent(slotType(mSelSlot));
        auto title = mDetailRect.reduced(12, 8).removeFromTop(20);
        g.setColour(ac);
        g.setFont(fonts::archivo(12.5f, fonts::Bold, 0.03f));
        g.drawText(mDetailTitle, title, juce::Justification::centredLeft);

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
    juce::Rectangle<int> mInRect, mAmpARect, mAmpBRect;
    int mSplitX = 0, mAmpX = 0;
    std::uint64_t mLastSig = ~0ull;
};

} // namespace nam_rig::ui
