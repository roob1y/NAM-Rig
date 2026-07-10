#pragma once
// PedalboardPanel — INTERIM panel for the pedalboard POOL (Stage B).
//
// Stage B replaced the board's DSP + params: it is now an OWNING pool (a locked Env+Comp
// front pair + 8 free slots, each a Drive/Mod/Delay carrying the full param union — see
// rig/PedalboardBlock.h + the pbS{i}* params). The first-cut node-graph editor was keyed
// to the retired pbSlot{i}Engine/Lane params, so it is parked here as a minimal ENABLE
// panel until Stage C builds the AmpliTube-style 3-zone editor (palette + chain + detail)
// on top of the new pool. Until then the per-slot params are editable via host automation
// / the generic editor, and the board can be toggled on/off here.
//
//   - ENABLE switch -> pbEnabled (off = legacy fixed chain runs, byte-exact).
//
// Kept as a BlockPanel with the same ctor(apvts) + refresh() interface so the editor
// wiring is unchanged.

#include <juce_audio_processors/juce_audio_processors.h>
#include "RigLookAndFeel.h"
#include "Panels.h" // BlockPanel, ToggleSwitch

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
        startTimerHz(8);
    }

    void refresh() { repaint(); }

    void resized() override
    {
        auto body = contentArea();
        auto top = body.removeFromTop(30);
        mEnable->setBounds(top.removeFromRight(52).withSizeKeepingCentre(46, 24));
        body.removeFromTop(8);
        mBody = body;
    }

    void paint(juce::Graphics &g) override
    {
        BlockPanel::paint(g);

        g.setColour(colors::caption);
        g.setFont(fonts::mono(9.0f, fonts::Medium, 0.06f));
        g.drawText("ENABLE", juce::Rectangle<int>(mEnable->getX() - 66, mEnable->getY(),
                                                  62, mEnable->getHeight()),
                   juce::Justification::centredRight);

        const bool on = enabled();
        g.setColour(on ? colors::text : colors::textDim);
        g.setFont(fonts::archivo(12.0f, fonts::SemiBold, 0.01f));
        auto bodyRect = mBody;
        auto title = bodyRect.removeFromTop(bodyRect.getHeight() / 2);
        g.drawText(on ? "Pedalboard POOL active"
                      : "Pedalboard off - legacy front chain running (byte-exact)",
                   title, juce::Justification::centredBottom);
        g.setColour(colors::textDim);
        g.setFont(fonts::mono(9.5f, fonts::Medium, 0.03f));
        g.drawText(on ? "edit slots via host automation - visual editor arrives in Stage C"
                      : "enable to use the reorderable Env+Comp + 8-slot Drive/Mod/Delay pool",
                   bodyRect, juce::Justification::centredTop);
    }

private:
    bool enabled() const { return mApvts.getRawParameterValue("pbEnabled")->load() >= 0.5f; }

    void timerCallback() override
    {
        const bool on = enabled();
        if (on != mLast) { mLast = on; setHeaderRight(on ? "ON" : "OFF"); repaint(); }
    }

    juce::AudioProcessorValueTreeState &mApvts;
    std::unique_ptr<ToggleSwitch> mEnable;
    juce::Rectangle<int> mBody;
    bool mLast = false;
};

} // namespace nam_rig::ui
