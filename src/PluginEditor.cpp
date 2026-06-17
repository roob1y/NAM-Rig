#include "PluginEditor.h"

using namespace nam_rig::ui;

NamRigEditor::NamRigEditor(NamRigProcessor &p)
    : juce::AudioProcessorEditor(&p), mProc(p),
      mPresetBar(p.presets()),
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
    mTitle.setFont(RigLookAndFeel::withHeight(22.0f).boldened());
    mTitle.setColour(juce::Label::textColourId, colors::text);
    mContent.addAndMakeVisible(mTitle);
    mContent.addAndMakeVisible(mPresetBar);

    mStatus.setJustificationType(juce::Justification::centredRight);
    mStatus.setColour(juce::Label::textColourId, colors::textDim);
    mContent.addAndMakeVisible(mStatus);

    for (auto *l : {&mInLabel, &mOutLabel})
    {
        l->setJustificationType(juce::Justification::centred);
        l->setColour(juce::Label::textColourId, colors::textDim);
        mContent.addAndMakeVisible(*l);
    }
    for (auto *s : {&mInGain, &mOutGain})
    {
        s->setPopupDisplayEnabled(true, true, this);
        s->setDoubleClickReturnValue(true, 0.0); // double-click = 0 dB
        mContent.addAndMakeVisible(*s);
    }
    mInAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        mProc.apvts, "inputGain", mInGain);
    mOutAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        mProc.apvts, "outputGain", mOutGain);
    mContent.addAndMakeVisible(mInMeter);
    mContent.addAndMakeVisible(mOutMeter);

    // --- Strip + panels ---
    mContent.addAndMakeVisible(mStrip);
    for (auto *panel : mPanels)
        mContent.addChildComponent(*panel); // visibility driven by selection

    // Global input-calibration overlay, toggled by the header INPUT button.
    mContent.addChildComponent(mCalPanel);
    mCalPanel.onClose = [this] { mCalPanel.setVisible(false); };
    mContent.addAndMakeVisible(mCalBtn);
    mCalBtn.onClick = [this]
    {
        const bool show = !mCalPanel.isVisible();
        mCalPanel.setVisible(show);
        if (show)
            mCalPanel.toFront(true);
    };

    mStrip.onSelectionChanged = [this](int i)
    { showPanel(i); };
    mStrip.select(juce::jlimit(0, (int)mPanels.size() - 1, mProc.uiSelectedBlock));

    mAmpPanelA.refresh();
    mAmpPanelB.refresh();
    mCabPanelA.refresh();
    mCabPanelB.refresh();

    mLastTimerMs = juce::Time::getMillisecondCounterHiRes();
    startTimerHz(15);

    // Uniform scaling: fixed aspect, remember the last size for this instance.
    setResizable(true, true);
    getConstrainer()->setFixedAspectRatio((double)kBaseW / kBaseH);
    setResizeLimits(kBaseW * 7 / 10, kBaseH * 7 / 10, kBaseW * 2, kBaseH * 2);
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
    mGatePanel.grMeter().push(-mProc.gateGainDb(), dt);
    mCompPanel.grMeter().push(mProc.compGrDb(), dt);
    mCompPanel.grMeter().pushIn(mProc.compInDb(), dt);
    mCompPanel.grMeter().pushOut(mProc.compOutDb(), dt);
    mPresetBar.updateDirty();       // modified-asterisk on the preset name
    if (++mPresetRefreshTick >= 30) // rescan the preset folder ~every 2 s
    {
        mPresetRefreshTick = 0;
        mPresetBar.refresh();
    }

    juce::String status;
    if (!mProc.isModelLoaded())
        status = "No model";
    else
    {
        status = mProc.getModelName();
        const int engaged = mProc.engagedFactor();
        if (engaged > 1)
            status << "  |  " << engaged << "x";
        status << "  |  PDC " << mProc.getLatencySamples() << " smp";
    }
    if (mProc.isIrLoaded())
        status << "  |  IR: " << mProc.getIrName();
    if (status != mStatus.getText())
        mStatus.setText(status, juce::dontSendNotification);
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

    auto area = juce::Rectangle<int>(0, 0, kBaseW, kBaseH).reduced(12);

    // --- Header (64 px): title | presets | status | IN + OUT knob/meter ---
    auto header = area.removeFromTop(64);
    mTitle.setBounds(header.removeFromLeft(130));
    mPresetBar.setBounds(header.removeFromLeft(380).withSizeKeepingCentre(380, 26));
    header.removeFromLeft(12);
    mCalBtn.setBounds(header.removeFromLeft(60).withSizeKeepingCentre(58, 26));
    header.removeFromLeft(12);

    auto ioCluster = header.removeFromRight(220);
    auto laidOut = [&](juce::Label &label, juce::Slider &knob, PeakMeter &meter,
                       juce::Rectangle<int> r)
    {
        meter.setBounds(r.removeFromRight(8).reduced(0, 6));
        r.removeFromRight(4);
        label.setBounds(r.removeFromTop(14));
        knob.setBounds(r.withSizeKeepingCentre(juce::jmin(r.getWidth(), r.getHeight()),
                                               juce::jmin(r.getWidth(), r.getHeight())));
    };
    auto inArea = ioCluster.removeFromLeft(ioCluster.getWidth() / 2);
    laidOut(mInLabel, mInGain, mInMeter, inArea.reduced(10, 0));
    laidOut(mOutLabel, mOutGain, mOutMeter, ioCluster.reduced(10, 0));

    header.removeFromRight(12);
    mStatus.setBounds(header);

    area.removeFromTop(8);

    // --- Chain strip ---
    mStrip.setBounds(area.removeFromTop(76));
    area.removeFromTop(12);

    // --- Block panel ---
    for (auto *panel : mPanels)
        panel->setBounds(area);
    mCalPanel.setBounds(area); // overlay occupies the block-panel area
}

juce::AudioProcessorEditor *NamRigProcessor::createEditor()
{
    return new NamRigEditor(*this);
}
