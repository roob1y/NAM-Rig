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

// The fixed serial chain as a row of tiles with flow chevrons between them.
class BlockStrip : public juce::Component
{
public:
    std::function<void(int)> onSelectionChanged; // selectable-block index

    BlockStrip(juce::AudioProcessorValueTreeState &apvts)
    {
        struct Slot { const char *name, *param; bool future; };
        static const Slot slots[] = {
            {"GATE",  "gateOn",   false},
            {"COMP",  "compOn",   false},
            {"AMP",   "",         false},
            {"EQ",    "eqOn",     false},
            {"CAB",   "cabOn",    false},
            {"MOD",   "modOn",    false},
            {"DELAY", "delayOn",  false},
            {"VERB",  "reverbOn", false},
        };
        int selectable = 0;
        for (const auto &s : slots)
        {
            auto tile = std::make_unique<BlockTile>(s.name, s.param, apvts, s.future);
            if (!s.future)
            {
                const int index = selectable++;
                tile->onSelect = [this, index] { select(index); };
            }
            addAndMakeVisible(*tile);
            mTiles.push_back(std::move(tile));
        }
    }

    void select(int selectableIndex)
    {
        mSelected = selectableIndex;
        int i = 0;
        for (auto &t : mTiles)
            if (!t->isFuture())
                t->setSelected(i++ == selectableIndex);
        if (onSelectionChanged)
            onSelectionChanged(selectableIndex);
    }

    void resized() override
    {
        const int n = (int)mTiles.size();
        const int gap = 16;
        const int tileW = (getWidth() - gap * (n - 1)) / n;
        auto area = getLocalBounds();
        for (int i = 0; i < n; ++i)
        {
            mTiles[(size_t)i]->setBounds(area.removeFromLeft(tileW));
            if (i < n - 1)
                area.removeFromLeft(gap);
        }
    }

    void paint(juce::Graphics &g) override
    {
        // Chevrons in the gaps: signal flow direction.
        g.setColour(colors::textDim);
        for (size_t i = 0; i + 1 < mTiles.size(); ++i)
        {
            const float x0 = (float)mTiles[i]->getRight();
            const float x1 = (float)mTiles[i + 1]->getX();
            const float cx = (x0 + x1) * 0.5f, cy = (float)getHeight() * 0.5f;
            juce::Path p;
            p.startNewSubPath(cx - 2.5f, cy - 4.5f);
            p.lineTo(cx + 2.5f, cy);
            p.lineTo(cx - 2.5f, cy + 4.5f);
            g.strokePath(p, juce::PathStrokeType(1.6f));
        }
    }

private:
    std::vector<std::unique_ptr<BlockTile>> mTiles;
    int mSelected = -1;
};

} // namespace nam_rig::ui
