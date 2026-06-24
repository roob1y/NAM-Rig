#include "PluginEditor.h"

using namespace nam_rig::ui;

NamRigEditor::NamRigEditor(NamRigProcessor &p)
    : juce::AudioProcessorEditor(&p), mProc(p),
      mPresetBar(p.presets()),
      mInKnob(p.apvts, "inputGain", "IN"),
      mOutKnob(p.apvts, "outputGain", "OUT"),
      mStrip(p.apvts),
      mGatePanel(p.apvts),
      mCompPanel(p.apvts),
      mDrivePanel(p.apvts),
      mAmpPanelA(p, 0),
      mAmpPanelB(p, 1),
      mEqPanelA(p.apvts, 0),
      mEqPanelB(p.apvts, 1),
      mCabPanelA(p, 0),
      mCabPanelB(p, 1),
      mMixPanel(p),
      mModPanel(p.apvts),
      mDelayPanel(p.apvts),
      mReverbPanel(p.apvts),
      mCalPanel(p.apvts),
      mPanels{&mGatePanel, &mCompPanel, &mDrivePanel, &mAmpPanelA, &mEqPanelA, &mCabPanelA,
              &mAmpPanelB, &mEqPanelB, &mCabPanelB, &mMixPanel,
              &mModPanel, &mDelayPanel, &mReverbPanel}
{
    setLookAndFeel(&mLnf);
    addAndMakeVisible(mContent);
    mContent.setSize(kBaseW, kBaseH);

    // Momentary mod-slot solo (dial-in): editor buttons -> processor state.
    mModPanel.onSetSolo = [this](int slot, bool on) { mProc.setModSolo(slot, on); };
    mModPanel.getSolo = [this](int slot) { return mProc.getModSolo(slot); };

    // --- Header ---
    mContent.addAndMakeVisible(mHeader); // behind the header widgets
    mContent.addAndMakeVisible(mPresetBar);
    mContent.addAndMakeVisible(mInKnob);
    mContent.addAndMakeVisible(mOutKnob);
    mContent.addAndMakeVisible(mInMeter);
    mContent.addAndMakeVisible(mOutMeter);
    mContent.addAndMakeVisible(mMenuBtn);
    mMenuBtn.onClick = [this] { showSettingsMenu(); };

    // --- Strip + panels ---
    mContent.addAndMakeVisible(mStrip);
    for (auto *panel : mPanels)
        mContent.addChildComponent(*panel); // visibility driven by selection

    // Global input-calibration overlay, toggled from the Settings menu.
    mContent.addChildComponent(mCalPanel);
    mCalPanel.onClose = [this] { mCalPanel.setVisible(false); };

    mStrip.onSelectionChanged = [this](int i) { showPanel(i); };
    mStrip.select(juce::jlimit(0, (int)mPanels.size() - 1, mProc.uiSelectedBlock));

    mAmpPanelA.refresh();
    mAmpPanelB.refresh();
    mCabPanelA.refresh();
    mCabPanelB.refresh();

    mLastTimerMs = juce::Time::getMillisecondCounterHiRes();
    startTimerHz(30); // gate scope reads smoothly at this rate; meters are dt-compensated

    // Uniform scaling: fixed aspect, remember the last size for this instance.
    setResizable(true, true);
    getConstrainer()->setFixedAspectRatio((double)kBaseW / kBaseH);
    setResizeLimits(kBaseW * 6 / 10, kBaseH * 6 / 10, kBaseW * 3 / 2, kBaseH * 3 / 2);
    const int w = mProc.uiWidth > 0 ? mProc.uiWidth : kBaseW;
    setSize(w, w * kBaseH / kBaseW);
}

NamRigEditor::~NamRigEditor()
{
    setLookAndFeel(nullptr);
}

void NamRigEditor::showPanel(int selectableIndex)
{
    mCalPanel.setVisible(false); // selecting a block dismisses the cal overlay
    for (int i = 0; i < (int)mPanels.size(); ++i)
        mPanels[(size_t)i]->setVisible(i == selectableIndex);
    mProc.uiSelectedBlock = selectableIndex;
}

void NamRigEditor::showSettingsMenu()
{
    const bool lowLat = mProc.apvts.getRawParameterValue("lowLatency")->load() >= 0.5f;
    juce::PopupMenu m;
    m.addSectionHeader("SETTINGS");
    m.addItem(1, "Input Calibration");
    m.addItem(2, "Low Latency", true, lowLat);

    mMenuBtn.open = true;
    mMenuBtn.repaint();
    m.showMenuAsync(juce::PopupMenu::Options()
                        .withTargetScreenArea(mMenuBtn.getScreenBounds())
                        .withMinimumWidth(220),
                    [this](int r)
                    {
                        mMenuBtn.open = false;
                        mMenuBtn.repaint();
                        if (r == 1)
                        {
                            const bool show = !mCalPanel.isVisible();
                            mCalPanel.setVisible(show);
                            if (show) mCalPanel.toFront(true);
                        }
                        else if (r == 2)
                        {
                            if (auto *p = mProc.apvts.getParameter("lowLatency"))
                                p->setValueNotifyingHost(p->getValue() < 0.5f ? 1.0f : 0.0f);
                        }
                    });
}

void NamRigEditor::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const float dt = (float)juce::jlimit(0.0, 0.5, (now - mLastTimerMs) * 0.001);
    mLastTimerMs = now;

    mInMeter.push(mProc.mInputPeakDb.load(), dt);
    mOutMeter.push(mProc.mOutputPeakDb.load(), dt);

    mAmpPanelA.refresh();
    mAmpPanelB.refresh();
    mCabPanelA.refresh();
    mCabPanelB.refresh();
    mDrivePanel.refresh();
    mMixPanel.refresh();
    mModPanel.refresh();
    mDelayPanel.refresh();
    mReverbPanel.refresh();
    mGatePanel.setLowLatency(mProc.apvts.getRawParameterValue("lowLatency")->load() >= 0.5f);
    mGatePanel.setBypassed(mProc.apvts.getRawParameterValue("gateOn")->load() < 0.5f);
    mGatePanel.pushActivity(mProc.gateInDb(), mProc.gateGainDb(),
                            mProc.apvts.getRawParameterValue("gateThresh")->load(), dt);
    mCompPanel.pushGr(mProc.compGrDb(), dt);
    mCompPanel.pushIn(mProc.compInDb(), dt);
    mCompPanel.pushOut(mProc.compOutDb(), dt);

    // Per-block bypass scrim: each panel reads inactive when its block is off.
    auto off = [this](const char *id) { return mProc.apvts.getRawParameterValue(id)->load() < 0.5f; };
    // Solo routing: the un-soloed rig's whole path (amp/EQ/cab) is silent, so its
    // panels read bypassed too. rigMode 0 = Solo A, 1 = Solo B, 2 = Dual.
    const int rigMode = (int)mProc.apvts.getRawParameterValue("rigMode")->load();
    const bool aOut = (rigMode == 1); // Solo B -> Rig A is bypassed
    const bool bOut = (rigMode == 0); // Solo A -> Rig B is bypassed

    mCompPanel.setBypassed(off("compOn"));
    mDrivePanel.setBypassed(off("driveOn"));
    mAmpPanelA.setBypassed(aOut);
    mAmpPanelB.setBypassed(bOut);
    mEqPanelA.setBypassed(aOut || off("eqOn"));
    mCabPanelA.setBypassed(aOut || off("cabOn"));
    mEqPanelB.setBypassed(bOut || off("eqOnB"));
    mCabPanelB.setBypassed(bOut || off("cabOnB"));
    mModPanel.setBypassed(off("modOn"));
    mDelayPanel.setBypassed(off("delayOn"));
    mReverbPanel.setBypassed(off("reverbOn"));

    mPresetBar.updateDirty();       // modified-asterisk on the preset name
    if (++mPresetRefreshTick >= 60) // rescan the preset folder ~every 2 s
    {
        mPresetRefreshTick = 0;
        mPresetBar.refresh();
    }

    // Loaded-capture rows: Rig A / Rig B amp model names.
    auto nameFor = [this](int rig) -> juce::String
    {
        if (!mProc.isModelLoaded(rig)) return "No model";
        auto n = mProc.getModelName(rig);
        return n.isNotEmpty() ? n : "Loaded";
    };
    mHeader.setCaptures(nameFor(0), mProc.isModelLoaded(0),
                        nameFor(1), mProc.isModelLoaded(1));
}

void NamRigEditor::paint(juce::Graphics &g)
{
    g.fillAll(colors::bg);
}

void NamRigEditor::resized()
{
    // Scale the fixed-size content to the window; layout itself never reflows.
    const float scale = (float)getWidth() / (float)kBaseW;
    mContent.setTransform(juce::AffineTransform::scale(scale));
    mContent.setTopLeftPosition(0, 0);
    mProc.uiWidth = getWidth();

    auto full = juce::Rectangle<int>(0, 0, kBaseW, kBaseH);

    // --- Header (100px, full-bleed) ---
    auto headerBounds = full.removeFromTop(100);
    mHeader.setBounds(headerBounds);

    auto h = headerBounds.reduced(24, 0);
    auto wordmark = h.removeFromLeft(150);
    h.removeFromLeft(20);
    mPresetBar.setBounds(h.removeFromLeft(330).withSizeKeepingCentre(330, 30));
    h.removeFromLeft(16);

    mMenuBtn.setBounds(h.removeFromRight(34).withSizeKeepingCentre(34, 34));
    h.removeFromRight(16);

    auto ioCluster = h.removeFromRight(210);
    auto io = ioCluster.reduced(0, 12);
    auto layoutIO = [](LabeledKnob &knob, PeakMeter &meter, juce::Rectangle<int> r)
    {
        auto m = r.removeFromRight(7);
        meter.setBounds(m.withSizeKeepingCentre(7, juce::jmax(20, r.getHeight() - 22))
                            .translated(0, -6));
        r.removeFromRight(7);
        knob.setBounds(r);
    };
    auto inGroup = io.removeFromLeft((io.getWidth() - 18) / 2);
    io.removeFromLeft(18);
    layoutIO(mInKnob, mInMeter, inGroup);
    layoutIO(mOutKnob, mOutMeter, io);

    auto captures = h; // remaining middle band
    mHeader.setLayout(wordmark, captures);

    // --- Content area (padding 20 sides / 20 top / 22 bottom) ---
    auto content = full.reduced(20, 0);
    content.removeFromTop(20);
    content.removeFromBottom(22);

    mStrip.setBounds(content.removeFromTop(108));
    content.removeFromTop(18);

    for (auto *panel : mPanels)
        panel->setBounds(content);
    mCalPanel.setBounds(content); // overlay occupies the block-panel area
}

juce::AudioProcessorEditor *NamRigProcessor::createEditor()
{
    return new NamRigEditor(*this);
}
