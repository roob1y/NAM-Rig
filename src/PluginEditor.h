#pragma once
#include "PluginProcessor.h"
#include <juce_audio_utils/juce_audio_utils.h>

#include "ui/RigLookAndFeel.h"
#include "ui/Meter.h"
#include "ui/HeaderPanel.h"
#include "ui/BlockStrip.h"
#include "ui/Panels.h"
#include "ui/IrBrowser.h"
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

    // Default-size-to-screen. Display info is unreliable at editor construction
    // inside a host, so we pick a best-effort size in the constructor and then
    // re-fit once on the first timer tick (when the window is really on screen).
    void resizeToFitScreen();
    void clampSizeToScreen();       // shrink a remembered size that's too big for this screen
    void applyRememberedOrFitSize(); // use the saved size if real, else fit to screen
    juce::Rectangle<int> screenWorkArea() const;
    bool mSizedToScreen = false;    // the reliable post-show sizing pass has run
    bool mPersistSize = false;      // gate: only write uiWidth back after initial sizing
    int mFitTries = 0;              // ticks spent waiting for valid display info

    NamRigProcessor &mProc;
    // Shared across all plugin instances: one LookAndFeel object, ref-counted, so
    // the bundled typeface cache and the process default-LnF pointer are owned by a
    // single instance whose lifetime spans "any editor open" -> both are torn down
    // cleanly (while JUCE is alive) when the last editor closes, not at DLL unload.
    juce::SharedResourcePointer<nam_rig::ui::RigLookAndFeel> mLnf;

    // Everything lives on mContent (fixed kBaseW x kBaseH, scaled to fit).
    juce::Component mContent;

    // --- Header ---
    nam_rig::ui::HeaderPanel mHeader;
    nam_rig::ui::PresetBar mPresetBar;
    nam_rig::ui::LabeledKnob mInKnob, mOutKnob;
    nam_rig::ui::PeakMeter mInMeter, mOutMeter;
    nam_rig::ui::HamburgerButton mMenuBtn;

    // --- Chain strip + per-block panels ---
    // selectable: 0 gate, 1 comp, 2 drive, 3 ampA, 4 eqA, 5 ampB, 6 eqB,
    //             7 cab (both), 8 mix, 9 mod, 10 delay, 11 reverb
    nam_rig::ui::BlockStrip mStrip;
    nam_rig::ui::GatePanel mGatePanel;
    nam_rig::ui::CompPanel mCompPanel;
    nam_rig::ui::DrivePanel mDrivePanel;
    nam_rig::ui::AmpPanel mAmpPanelA, mAmpPanelB;
    nam_rig::ui::EqPanel mEqPanelA, mEqPanelB;
    nam_rig::ui::CombinedCabPanel mCabPanel; // both cabs (A | B) in one panel
    nam_rig::ui::MixPanel mMixPanel;
    nam_rig::ui::ModPanel mModPanel;
    nam_rig::ui::DelayPanel mDelayPanel;
    nam_rig::ui::ReverbPanel mReverbPanel;
    nam_rig::ui::CalPanel mCalPanel;       // global input-cal overlay (settings menu)
    nam_rig::ui::IrBrowser mIrBrowser;     // IR library overlay (opened from a cab)
    std::array<juce::Component *, 12> mPanels;

    void openIrBrowser(int rig);

    double mLastTimerMs = 0.0;
    int mPresetRefreshTick = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NamRigEditor)
};
