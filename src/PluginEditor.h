#pragma once
#include "PluginProcessor.h"
#include <juce_audio_utils/juce_audio_utils.h>

#include "ui/RigLookAndFeel.h"
#include "ui/Meter.h"
#include "ui/HeaderPanel.h"
#include "ui/BlockStrip.h"
#include "ui/Panels.h"
#include "ui/PresetBar.h"

// Block-strip editor: global header (wordmark + presets + loaded captures + I/O
// + settings), the dual-rig chain as a branched row of tiles, and one block's
// full panel below. Logical size is fixed (1180x808); the window is resizable
// and the whole UI scales uniformly via an AffineTransform on the content.
class NamRigEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    static constexpr int kBaseW = 1180, kBaseH = 808;

    explicit NamRigEditor(NamRigProcessor &);
    ~NamRigEditor() override;

    void paint(juce::Graphics &) override;
    void resized() override;

private:
    void timerCallback() override;
    void showPanel(int selectableIndex);
    void showSettingsMenu();

    NamRigProcessor &mProc;
    nam_rig::ui::RigLookAndFeel mLnf;

    // Everything lives on mContent (fixed kBaseW x kBaseH, scaled to fit).
    juce::Component mContent;

    // --- Header ---
    nam_rig::ui::HeaderPanel mHeader;
    nam_rig::ui::PresetBar mPresetBar;
    nam_rig::ui::LabeledKnob mInKnob, mOutKnob;
    nam_rig::ui::PeakMeter mInMeter, mOutMeter;
    nam_rig::ui::HamburgerButton mMenuBtn;

    // --- Chain strip + per-block panels ---
    // selectable: 0 gate, 1 comp, 2 drive, 3 ampA, 4 eqA, 5 cabA, 6 ampB, 7 eqB,
    //             8 cabB, 9 mix, 10 mod, 11 delay, 12 reverb
    nam_rig::ui::BlockStrip mStrip;
    nam_rig::ui::GatePanel mGatePanel;
    nam_rig::ui::CompPanel mCompPanel;
    nam_rig::ui::DrivePanel mDrivePanel;
    nam_rig::ui::AmpPanel mAmpPanelA, mAmpPanelB;
    nam_rig::ui::EqPanel mEqPanelA, mEqPanelB;
    nam_rig::ui::CabPanel mCabPanelA, mCabPanelB;
    nam_rig::ui::MixPanel mMixPanel;
    nam_rig::ui::ModPanel mModPanel;
    nam_rig::ui::DelayPanel mDelayPanel;
    nam_rig::ui::ReverbPanel mReverbPanel;
    nam_rig::ui::CalPanel mCalPanel;       // global input-cal overlay (settings menu)
    std::array<juce::Component *, 13> mPanels;

    double mLastTimerMs = 0.0;
    int mPresetRefreshTick = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NamRigEditor)
};
