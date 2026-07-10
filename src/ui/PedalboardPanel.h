#pragma once
// PedalboardPanel — the unified front-of-amp PEDALBOARD as a horizontal signal-flow
// node graph (AmpliTube-style): IN -> trunk pedals -> SPLIT -> Amp A lane / Amp B lane.
//
//   - Pedals on the TRUNK feed both amps; pedals on the A / B lane feed one amp.
//   - Cable colour follows the lane (trunk amber, Amp A amber, Amp B teal), so the
//     routing is readable at a glance — the same picture the DSP uses (PedalboardBlock).
//   - This panel drives the pbEnabled + pbSlot{i}Engine / pbSlot{i}Lane params. The
//     per-pedal KNOBS are still edited on each pedal's own block tile for now (Stage 2
//     will fold the detail panels into the nodes); this panel owns ENABLE + placement,
//     ORDER and ROUTING.
//
// Interaction (v1, click-based; drag-reorder is the next polish):
//   - click a node            -> popup: Route (Both / Amp A / Amp B), Move </ >, Remove
//   - click the "+ Add pedal" -> popup of the not-yet-placed pedal types
//   - the ENABLE switch        -> pbEnabled (off = legacy fixed chain runs, bit-exact)
//
// Only five engine types exist (Env, Comp, Drive, Pre-Mod, Pre-Delay), each usable
// once, so the board is at most five nodes across three lanes — laid out left to right
// in slot-index order within each lane.

#include <juce_audio_processors/juce_audio_processors.h>
#include "RigLookAndFeel.h"
#include "Panels.h" // BlockPanel, SegmentedControl, ToggleSwitch

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
        startTimerHz(20);
    }

    // Editor timer also calls this; the internal timer keeps it live regardless.
    void refresh() { repaint(); }

    void resized() override
    {
        auto body = contentArea();
        // Top control row: ENABLE switch (right) + caption; "+ Add pedal" (left).
        auto top = body.removeFromTop(30);
        mEnable->setBounds(top.removeFromRight(52).withSizeKeepingCentre(46, 24));
        mAddRect = top.removeFromLeft(120).reduced(2, 3);
        body.removeFromTop(8);
        mGraph = body;
        layoutNodes();
    }

    void paint(juce::Graphics &g) override
    {
        BlockPanel::paint(g);

        // "ENABLE" caption to the left of the switch.
        g.setColour(colors::caption);
        g.setFont(fonts::mono(9.0f, fonts::Medium, 0.06f));
        g.drawText("ENABLE", juce::Rectangle<int>(mEnable->getX() - 66, mEnable->getY(),
                                                  62, mEnable->getHeight()),
                   juce::Justification::centredRight);

        // "+ Add pedal" button.
        {
            auto r = mAddRect.toFloat();
            const bool canAdd = firstFreeSlot() >= 0;
            g.setColour(canAdd ? colors::tile : colors::inset);
            g.fillRoundedRectangle(r, 7.0f);
            g.setColour(canAdd ? colors::accent : colors::outline);
            g.drawRoundedRectangle(r, 7.0f, 1.0f);
            g.setColour(canAdd ? colors::text : colors::textDim);
            g.setFont(fonts::archivo(11.0f, fonts::SemiBold, 0.02f));
            g.drawText("+  Add pedal", mAddRect, juce::Justification::centred);
        }

        paintGraph(g);
        if (mDragging && mDragEngine != 0) paintDragFeedback(g);

        if (!enabled())
        {
            auto hintRow = juce::Rectangle<int>(mGraph.getX(), mGraph.getBottom() - 16,
                                                mGraph.getWidth(), 16);
            g.setColour(colors::textDim);
            g.setFont(fonts::mono(9.5f, fonts::Medium, 0.04f));
            g.drawText("board off - legacy chain is running (enable to route pedals to Amp A / B)",
                       hintRow, juce::Justification::centred);
        }
    }

    void mouseDown(const juce::MouseEvent &e) override
    {
        mDragEngine = 0; mDragSlot = -1; mDragging = false;
        if (mAddRect.contains(e.getPosition())) { showAddMenu(); return; }
        for (const auto &n : mNodes)
            if (n.rect.contains(e.getPosition()))
            {
                mDragSlot = n.slot; mDragEngine = n.engine;
                mDragStart = e.getPosition(); mDragCurr = e.getPosition();
                return;
            }
    }

    void mouseDrag(const juce::MouseEvent &e) override
    {
        if (mDragEngine == 0) return;
        mDragCurr = e.getPosition();
        if (!mDragging && mDragStart.getDistanceFrom(mDragCurr) > 5) mDragging = true;
        if (mDragging)
        {
            mDropLane = laneFromY(mDragCurr.y);
            mDropIndex = dropIndexFor(mDropLane, mDragCurr.x, mDragEngine);
            repaint();
        }
    }

    void mouseUp(const juce::MouseEvent &) override
    {
        if (mDragEngine != 0 && mDragging) { commitDrop(); repaint(); }
        else if (mDragEngine != 0) showNodeMenu(mDragSlot); // a click (no drag) = the node menu
        mDragEngine = 0; mDragSlot = -1; mDragging = false;
    }

private:
    // ---- one drawn pedal node ----
    struct Node { int slot; int engine; int lane; juce::Rectangle<int> rect; };

    // ---- param helpers ----
    bool enabled() const { return mApvts.getRawParameterValue("pbEnabled")->load() >= 0.5f; }
    int slotEngine(int i) const
    {
        return (int)mApvts.getRawParameterValue("pbSlot" + juce::String(i) + "Engine")->load();
    }
    int slotLane(int i) const
    {
        return (int)mApvts.getRawParameterValue("pbSlot" + juce::String(i) + "Lane")->load();
    }
    void setChoice(const juce::String &id, int idx)
    {
        if (auto *p = mApvts.getParameter(id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost(p->convertTo0to1((float)juce::jmax(0, idx)));
            p->endChangeGesture();
        }
    }
    int firstFreeSlot() const
    {
        for (int i = 0; i < kSlots; ++i)
            if (slotEngine(i) == 0) return i;
        return -1;
    }
    bool enginePlaced(int engineId) const
    {
        for (int i = 0; i < kSlots; ++i)
            if (slotEngine(i) == engineId) return true;
        return false;
    }

    static const char *engineName(int id)
    {
        switch (id)
        {
        case 1: return "ENV";
        case 2: return "COMP";
        case 3: return "DRIVE";
        case 4: return "PRE-MOD";
        case 5: return "PRE-DELAY";
        default: return "";
        }
    }
    static const char *engineOnParam(int id)
    {
        switch (id)
        {
        case 1: return "envfilterOn";
        case 2: return "compOn";
        case 3: return "driveOn";
        case 4: return "premodOn";
        case 5: return "predelayOn";
        default: return nullptr;
        }
    }
    bool engineOn(int id) const
    {
        const char *p = engineOnParam(id);
        return p != nullptr && mApvts.getRawParameterValue(p)->load() >= 0.5f;
    }
    // Node accent by engine (category-ish colours drawn from the shared palette).
    static juce::Colour engineAccent(int id)
    {
        switch (id)
        {
        case 3: return colors::accent;          // drive = amber
        case 1: return juce::Colour(0xff6fd0c9); // env = teal-ish
        case 2: return juce::Colour(0xff8fb3ff); // comp = blue
        case 4: return juce::Colour(0xffc7a6f0); // pre-mod = violet
        case 5: return juce::Colour(0xff7fd08a); // pre-delay = green
        default: return colors::outline;
        }
    }
    static juce::Colour laneCable(int lane) // 0 trunk, 1 A, 2 B
    {
        if (lane == 2) return colors::laneColour(1); // Amp B tag colour (teal)
        return colors::laneColour(0);                // trunk / Amp A (amber)
    }

    // Ordered slot indices per lane (position = slot index order).
    std::vector<int> laneSlots(int lane) const
    {
        std::vector<int> v;
        for (int i = 0; i < kSlots; ++i)
            if (slotEngine(i) != 0 && slotLane(i) == lane) v.push_back(i);
        return v;
    }

    // ---- layout ----
    void layoutNodes()
    {
        mNodes.clear();
        if (mGraph.isEmpty()) return;

        const int nodeW = 92, nodeH = 40, gap = 20;
        const int inW = 40, ampW = 64;
        const int cy = mGraph.getCentreY();
        const int yTop = mGraph.getY() + mGraph.getHeight() / 4 - nodeH / 2;
        const int yBot = mGraph.getY() + (3 * mGraph.getHeight()) / 4 - nodeH / 2;
        const int yMid = cy - nodeH / 2;

        const int xStart = mGraph.getX() + inW + gap;

        // Trunk nodes left of the split.
        int x = xStart;
        for (int slot : laneSlots(0))
        {
            mNodes.push_back({slot, slotEngine(slot), 0,
                              juce::Rectangle<int>(x, yMid, nodeW, nodeH)});
            x += nodeW + gap;
        }
        mSplitX = x + gap / 2;                 // split junction x
        const int laneStart = mSplitX + gap;

        // Amp A lane (top).
        int xa = laneStart;
        for (int slot : laneSlots(1))
        {
            mNodes.push_back({slot, slotEngine(slot), 1,
                              juce::Rectangle<int>(xa, yTop, nodeW, nodeH)});
            xa += nodeW + gap;
        }
        // Amp B lane (bottom).
        int xb = laneStart;
        for (int slot : laneSlots(2))
        {
            mNodes.push_back({slot, slotEngine(slot), 2,
                              juce::Rectangle<int>(xb, yBot, nodeW, nodeH)});
            xb += nodeW + gap;
        }
        mAmpX = juce::jmax(xa, xb) + gap;
        mAmpX = juce::jmax(mAmpX, mSplitX + gap * 2);
        mAmpX = juce::jmin(mAmpX, mGraph.getRight() - ampW);
        mInRect = juce::Rectangle<int>(mGraph.getX(), yMid, inW, nodeH);
        mAmpARect = juce::Rectangle<int>(mAmpX, yTop, ampW, nodeH);
        mAmpBRect = juce::Rectangle<int>(mAmpX, yBot, ampW, nodeH);
    }

    // ---- graph drawing ----
    void paintGraph(juce::Graphics &g)
    {
        if (mGraph.isEmpty()) return; // not laid out yet (no valid bounds)
        const int cy = mGraph.getCentreY();

        // IN box.
        drawPill(g, mInRect, colors::tile, colors::outline, "IN", colors::textDim);

        // Cables. Trunk: IN -> each trunk node -> split. Then split -> lanes -> amps.
        g.setColour(laneCable(0).withAlpha(0.9f));
        {
            juce::Path trunk;
            trunk.startNewSubPath((float)mInRect.getRight(), (float)cy);
            trunk.lineTo((float)mSplitX, (float)cy);
            g.strokePath(trunk, juce::PathStrokeType(2.0f));
        }

        // Split junction dot.
        g.setColour(colors::accent);
        g.fillEllipse((float)mSplitX - 3.0f, (float)cy - 3.0f, 6.0f, 6.0f);

        // Amp A cable (top) + Amp B cable (bottom) from the split.
        drawLaneCable(g, 1, mAmpARect.getCentreY());
        drawLaneCable(g, 2, mAmpBRect.getCentreY());

        // Amp boxes.
        drawPill(g, mAmpARect, colors::tile, colors::laneColour(0), "AMP A", colors::laneColour(0));
        drawPill(g, mAmpBRect, colors::tile, colors::laneColour(1), "AMP B", colors::laneColour(1));

        // Nodes on top of the cables.
        for (const auto &n : mNodes)
            drawNode(g, n);
    }

    void drawLaneCable(juce::Graphics &g, int lane, int ampCy)
    {
        g.setColour(laneCable(lane).withAlpha(0.9f));
        const int laneY = (lane == 1) ? mAmpARect.getCentreY() : mAmpBRect.getCentreY();
        juce::Path p;
        p.startNewSubPath((float)mSplitX, (float)mGraph.getCentreY());
        p.lineTo((float)mSplitX, (float)laneY);            // vertical drop from the split
        p.lineTo((float)mAmpX, (float)laneY);              // run to the amp
        juce::ignoreUnused(ampCy);
        g.strokePath(p, juce::PathStrokeType(2.0f));
    }

    void drawNode(juce::Graphics &g, const Node &n)
    {
        const juce::Colour ac = engineAccent(n.engine);
        auto r = n.rect.toFloat();
        // enclosure
        g.setColour(colors::wellTop);
        g.fillRoundedRectangle(r, 8.0f);
        g.setColour(ac.withAlpha(0.85f));
        g.drawRoundedRectangle(r.reduced(0.5f), 8.0f, 1.4f);
        // top accent strip
        g.setColour(ac.withAlpha(engineOn(n.engine) ? 0.9f : 0.3f));
        g.fillRoundedRectangle(r.withHeight(5.0f).reduced(6.0f, 0.0f).translated(0, 4.0f), 2.0f);
        // name
        g.setColour(engineOn(n.engine) ? colors::text : colors::textDim);
        g.setFont(fonts::archivo(11.0f, fonts::Bold, 0.02f));
        g.drawText(engineName(n.engine), n.rect.withTrimmedTop(8), juce::Justification::centred);
        // on/off jewel
        auto jewel = juce::Rectangle<float>(7.0f, 7.0f).withPosition(r.getRight() - 12.0f, r.getY() + 6.0f);
        g.setColour(engineOn(n.engine) ? colors::accent : colors::ledOff);
        g.fillEllipse(jewel);
    }

    // Ghost of the dragged node under the cursor + a caret showing where it will drop.
    void paintDragFeedback(juce::Graphics &g)
    {
        // Highlight the target lane band.
        if (!mGraph.isEmpty())
        {
            const int h3 = mGraph.getHeight() / 3;
            int by = mGraph.getY() + h3;                 // trunk band (middle)
            if (mDropLane == 1) by = mGraph.getY();       // Amp A (top)
            else if (mDropLane == 2) by = mGraph.getY() + 2 * h3; // Amp B (bottom)
            g.setColour(colors::accent.withAlpha(0.06f));
            g.fillRect(juce::Rectangle<int>(mGraph.getX(), by, mGraph.getWidth(), h3));
        }
        // Drop caret (vertical accent bar at the cursor x, in the target lane).
        g.setColour(colors::accent.withAlpha(0.8f));
        g.fillRect(juce::Rectangle<float>((float)mDragCurr.x - 1.0f, (float)mDragCurr.y - 24.0f, 2.0f, 48.0f));
        // Ghost node.
        auto gr = juce::Rectangle<int>(92, 40).withCentre(mDragCurr).toFloat();
        const juce::Colour ac = engineAccent(mDragEngine);
        g.setColour(colors::wellTop.withAlpha(0.9f));
        g.fillRoundedRectangle(gr, 8.0f);
        g.setColour(ac.withAlpha(0.9f));
        g.drawRoundedRectangle(gr.reduced(0.5f), 8.0f, 1.6f);
        g.setColour(colors::text);
        g.setFont(fonts::archivo(11.0f, fonts::Bold, 0.02f));
        g.drawText(engineName(mDragEngine), gr.toNearestInt(), juce::Justification::centred);
    }

    static void drawPill(juce::Graphics &g, juce::Rectangle<int> ri, juce::Colour fill,
                         juce::Colour border, const juce::String &txt, juce::Colour txtCol)
    {
        auto r = ri.toFloat();
        g.setColour(fill);
        g.fillRoundedRectangle(r, 7.0f);
        g.setColour(border);
        g.drawRoundedRectangle(r.reduced(0.5f), 7.0f, 1.2f);
        g.setColour(txtCol);
        g.setFont(fonts::archivo(10.5f, fonts::Bold, 0.06f));
        g.drawText(txt, ri, juce::Justification::centred);
    }

    // ---- menus ----
    void showAddMenu()
    {
        const int free = firstFreeSlot();
        if (free < 0) return;
        juce::PopupMenu m;
        for (int id = 1; id <= 5; ++id)
            m.addItem(id, engineName(id), !enginePlaced(id), false);
        m.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(localAreaToGlobal(mAddRect)),
                        [this, free](int r)
                        {
                            if (r <= 0) return;
                            setChoice("pbSlot" + juce::String(free) + "Engine", r);
                            setChoice("pbSlot" + juce::String(free) + "Lane", 0); // Both
                            if (!enabled()) setChoice("pbEnabled", 1);            // engaging a pedal turns the board on
                            layoutNodes();
                            repaint();
                        });
    }

    void showNodeMenu(int slot)
    {
        const int lane = slotLane(slot);
        juce::Rectangle<int> anchor = mAddRect;
        for (const auto &n : mNodes)
            if (n.slot == slot) { anchor = n.rect; break; }
        juce::PopupMenu route;
        route.addItem(101, "Both (trunk)", true, lane == 0);
        route.addItem(102, "Amp A only", true, lane == 1);
        route.addItem(103, "Amp B only", true, lane == 2);

        juce::PopupMenu m;
        m.addSectionHeader(juce::String(engineName(slotEngine(slot))));
        m.addSubMenu("Route", route);
        m.addItem(201, "Move left", canMove(slot, -1));
        m.addItem(202, "Move right", canMove(slot, +1));
        m.addSeparator();
        m.addItem(203, "Remove from board");

        m.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(localAreaToGlobal(anchor)),
                        [this, slot](int r)
                        {
                            switch (r)
                            {
                            case 101: setChoice("pbSlot" + juce::String(slot) + "Lane", 0); break;
                            case 102: setChoice("pbSlot" + juce::String(slot) + "Lane", 1); break;
                            case 103: setChoice("pbSlot" + juce::String(slot) + "Lane", 2); break;
                            case 201: moveSlot(slot, -1); break;
                            case 202: moveSlot(slot, +1); break;
                            case 203: setChoice("pbSlot" + juce::String(slot) + "Engine", 0); break;
                            default: return;
                            }
                            layoutNodes();
                            repaint();
                        });
    }

    // ---- drag-to-reorder / move-lane ----
    // Target lane from the cursor Y: top third = Amp A, bottom third = Amp B, middle = trunk.
    int laneFromY(int y) const
    {
        if (mGraph.isEmpty()) return 0;
        const int t1 = mGraph.getY() + mGraph.getHeight() / 3;
        const int t2 = mGraph.getY() + (2 * mGraph.getHeight()) / 3;
        if (y < t1) return 1;  // Amp A (top)
        if (y > t2) return 2;  // Amp B (bottom)
        return 0;              // trunk (middle)
    }
    // Insertion index within a lane: how many of that lane's OTHER nodes sit left of x.
    int dropIndexFor(int lane, int x, int excludeEngine) const
    {
        int idx = 0;
        for (const auto &n : mNodes)
            if (n.lane == lane && n.engine != excludeEngine && n.rect.getCentreX() < x) ++idx;
        return idx;
    }
    // Rebuild the whole slot table from the current arrangement with the dragged pedal
    // moved to (mDropLane, mDropIndex), then serialize: trunk pedals into the first
    // slots, then Amp-A, then Amp-B (so trunk always processes before the lanes, and
    // within-lane order = slot order — exactly the DSP model). Unused slots -> Empty.
    void commitDrop()
    {
        std::vector<std::pair<int, int>> out; // (engine, lane) in final slot order
        for (int L = 0; L < 3; ++L)
        {
            std::vector<int> engines;
            for (const auto &n : mNodes)
                if (n.lane == L && n.engine != mDragEngine) engines.push_back(n.engine);
            if (L == mDropLane)
            {
                const int ins = juce::jlimit(0, (int)engines.size(), mDropIndex);
                engines.insert(engines.begin() + ins, mDragEngine);
            }
            for (int e2 : engines) out.push_back({e2, L});
        }
        for (int i = 0; i < kSlots; ++i)
        {
            if (i < (int)out.size())
            {
                setChoice("pbSlot" + juce::String(i) + "Engine", out[(size_t)i].first);
                setChoice("pbSlot" + juce::String(i) + "Lane", out[(size_t)i].second);
            }
            else
                setChoice("pbSlot" + juce::String(i) + "Engine", 0);
        }
        if (!enabled()) setChoice("pbEnabled", 1);
        layoutNodes();
    }

    // Adjacent occupied slot in the given direction (order = slot index order).
    int neighbour(int slot, int dir) const
    {
        for (int i = slot + dir; i >= 0 && i < kSlots; i += dir)
            if (slotEngine(i) != 0) return i;
        return -1;
    }
    bool canMove(int slot, int dir) const { return neighbour(slot, dir) >= 0; }
    void moveSlot(int slot, int dir)
    {
        const int j = neighbour(slot, dir);
        if (j < 0) return;
        // swap engine + lane between the two positions (keeps each pedal's routing).
        const int eA = slotEngine(slot), lA = slotLane(slot);
        const int eB = slotEngine(j), lB = slotLane(j);
        setChoice("pbSlot" + juce::String(slot) + "Engine", eB);
        setChoice("pbSlot" + juce::String(slot) + "Lane", lB);
        setChoice("pbSlot" + juce::String(j) + "Engine", eA);
        setChoice("pbSlot" + juce::String(j) + "Lane", lA);
    }

    void timerCallback() override
    {
        // Cheap change-detect so LEDs / routing repaint without the editor timer.
        std::uint64_t sig = enabled() ? 1u : 0u;
        for (int i = 0; i < kSlots; ++i)
            sig = sig * 131u + (std::uint64_t)(slotEngine(i) * 4 + slotLane(i)) + (engineOn(slotEngine(i)) ? 1u : 0u);
        if (sig != mLastSig)
        {
            mLastSig = sig;
            setHeaderRight(enabled() ? "ON" : "OFF");
            layoutNodes();
            repaint();
        }
    }

    static constexpr int kSlots = 8;
    juce::AudioProcessorValueTreeState &mApvts;
    std::unique_ptr<ToggleSwitch> mEnable;
    juce::Rectangle<int> mGraph, mAddRect, mInRect, mAmpARect, mAmpBRect;
    int mSplitX = 0, mAmpX = 0;
    std::vector<Node> mNodes;
    std::uint64_t mLastSig = ~0ull;

    // drag-to-reorder / move-lane state
    int mDragEngine = 0, mDragSlot = -1;
    bool mDragging = false;
    juce::Point<int> mDragStart, mDragCurr;
    int mDropLane = 0, mDropIndex = 0;
};

} // namespace nam_rig::ui
