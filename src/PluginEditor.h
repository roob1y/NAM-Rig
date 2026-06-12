#pragma once
#include "PluginProcessor.h"
#include <juce_audio_utils/juce_audio_utils.h>

#include "ui/RigLookAndFeel.h"
#include "ui/Meter.h"
#include "ui/BlockStrip.h"
#include "ui/Panels.h"

// Block-strip editor: global header (I/O gains + meters + status), the chain
// as a row of tiles with bypass LEDs, and one block's full panel below.
// Logical size is fixed (920x540); the window is resizable and the whole UI
// scales uniformly via an AffineTransform on the content component.
class NamRigEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    static constexpr int kBaseW = 920, kBaseH = 540;

    explicit NamRigEditor(NamRigProcessor &);
    ~NamRigEditor() override;

    void paint(juce::Graphics &) override;
    void resized() override;

private:
    void timerCallback() override;
    void showPanel(int selectableIndex);

    NamRigProcessor &mProc;
    nam_rig::ui::RigLookAndFeel mLnf;

    // Everything lives on mContent (fixed kBaseW x kBaseH, scaled to fit).
    juce::Component mContent;

    // --- Header ---
    juce::Label mTitle{{}, "NAM RIG"};
    juce::Label mStatus;
    juce::Label mInLabel{{}, "IN"}, mOutLabel{{}, "OUT"};
    juce::Slider mInGain{juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox};
    juce::Slider mOutGain{juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox};
    nam_rig::ui::PeakMeter mInMeter, mOutMeter;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mInAtt, mOutAtt;

    // --- Chain strip + per-block panels ---
    // selectable: 0 gate, 1 comp, 2 amp, 3 eq, 4 cab, 5 mod, 6 delay, 7 reverb
    nam_rig::ui::BlockStrip mStrip;
    nam_rig::ui::GatePanel mGatePanel;
    nam_rig::ui::CompPanel mCompPanel;
    nam_rig::ui::AmpPanel mAmpPanel;
    nam_rig::ui::EqPanel mEqPanel;
    nam_rig::ui::CabPanel mCabPanel;
    nam_rig::ui::ModPanel mModPanel;
    nam_rig::ui::DelayPanel mDelayPanel;
    nam_rig::ui::ReverbPanel mReverbPanel;
    std::array<juce::Component *, 8> mPanels;

    double mLastTimerMs = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NamRigEditor)
};
