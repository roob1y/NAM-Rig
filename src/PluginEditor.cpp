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
      mCabPanel(p),
      mMixPanel(p),
      mModPanel(p.apvts),
      mDelayPanel(p.apvts),
      mReverbPanel(p.apvts),
      mCalPanel(p.apvts),
      // Single CAB tile/panel (idx 7) fed by both lanes; AMP/EQ stay per-rig.
      mPanels{&mGatePanel, &mCompPanel, &mDrivePanel, &mAmpPanelA, &mEqPanelA, &mAmpPanelB,
              &mEqPanelB, &mCabPanel, &mMixPanel,
              &mModPanel, &mDelayPanel, &mReverbPanel}
{
    setLookAndFeel(&mLnf);
    // Manually-shown PopupMenus (drive/delay pickers, presets, etc.) don't inherit
    // a component's LookAndFeel the way ComboBox dropdowns do -- they fall back to
    // the default. Point the default at our LookAndFeel so every menu in the plugin
    // gets the one styled dropdown look.
    juce::LookAndFeel::setDefaultLookAndFeel(&mLnf);
    addAndMakeVisible(mContent);
    mContent.setSize(kBaseW, kBaseH);

    // Momentary mod-slot solo (dial-in): editor buttons -> processor state.
    mModPanel.onSetSolo = [this](int slot, bool on) { mProc.setModSolo(slot, on); };
    mModPanel.getSolo = [this](int slot) { return mProc.getModSolo(slot); };

    // Delay tap visualiser: read the sync-resolved effective times (per side).
    mDelayPanel.setTimeProvider([this] { return mProc.delayTimeMs(); });
    mDelayPanel.setTimeProviderR([this] { return mProc.delayTimeMsR(); });

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
        if (panel->getParentComponent() != &mContent) // mCabPanel appears twice
            mContent.addChildComponent(*panel);        // visibility driven by selection

    // Global input-calibration overlay, toggled from the Settings menu.
    mContent.addChildComponent(mCalPanel);
    mCalPanel.onClose = [this] { mCalPanel.setVisible(false); };

    // IR library overlay, opened from either cab's Browse button.
    mContent.addChildComponent(mIrBrowser);
    mIrBrowser.onClose = [this] { mIrBrowser.setVisible(false); };
    mIrBrowser.onLoad = [this](const juce::File &f, int rig) { mProc.loadIr(f, rig); };
    mIrBrowser.setRootChooser([this] { return mProc.irLibraryRoot(); },
                              [this](const juce::File &d) { mProc.setIrLibraryRoot(d); });
    mCabPanel.onBrowse = [this] { openIrBrowser(0); };

    mStrip.onSelectionChanged = [this](int i) { showPanel(i); };
    mStrip.select(juce::jlimit(0, (int)mPanels.size() - 1, mProc.uiSelectedBlock));

    mAmpPanelA.refresh();
    mAmpPanelB.refresh();
    mCabPanel.refresh();

    mLastTimerMs = juce::Time::getMillisecondCounterHiRes();
    startTimerHz(30); // gate scope reads smoothly at this rate; meters are dt-compensated

    // Uniform scaling: fixed aspect, remember the last size for this instance.
    setResizable(true, true);
    getConstrainer()->setFixedAspectRatio((double)kBaseW / kBaseH);
    const int minW = kBaseW * 6 / 10; // 708
    const int maxW = kBaseW * 5 / 2;  // 2950 — headroom so 4K / high-DPI can scale up
    setResizeLimits(minW, kBaseH * 6 / 10, maxW, kBaseH * 5 / 2);

    // Provisional size now (state may not be loaded yet, display info is flaky in
    // the constructor). The authoritative pass runs on the first timer tick, when
    // the saved state is loaded and the display is reliable. mPersistSize stays
    // false until then so our own setSize calls don't clobber the restored uiWidth.
    applyRememberedOrFitSize();
}

// The work area of the display the window is on. Display info is flaky in the
// editor constructor (no peer yet), so prefer the on-screen bounds when we have
// them, falling back to the mouse's display, then the primary.
juce::Rectangle<int> NamRigEditor::screenWorkArea() const
{
    auto &displays = juce::Desktop::getInstance().getDisplays();
    if (!getScreenBounds().isEmpty())
        if (auto *disp = displays.getDisplayForRect(getScreenBounds()))
            return disp->userArea;
    if (auto *disp = displays.getDisplayForPoint(juce::Desktop::getMousePosition()))
        return disp->userArea;
    if (auto *disp = displays.getPrimaryDisplay())
        return disp->userArea;
    return {};
}

// Choose a default size for the current screen. JUCE reports the work area in
// LOGICAL pixels — the same space setSize uses — so Windows display scaling is
// handled for free (at 150% a 4K screen reads ~2560x1440, and the host applies
// the content scale to the window). Target ~85% of the work-area height (≈ the
// 1180x808 design on a 1080p screen), don't spill past ~94% of either dimension,
// clamp to the resize limits. Implausible/empty work areas (the constructor's
// flaky query) fall back to the design width — never to the minimum.
void NamRigEditor::resizeToFitScreen()
{
    const double aspect = (double)kBaseW / kBaseH;
    const int minW = kBaseW * 6 / 10, maxW = kBaseW * 5 / 2;
    const auto work = screenWorkArea();

    int w = kBaseW; // safe fallback when the screen size is unknown/bogus
    if (work.getWidth() >= 1000 && work.getHeight() >= 700)
        w = juce::jmin(juce::roundToInt(work.getHeight() * 0.85 * aspect),
                       juce::roundToInt(work.getWidth() * 0.94));
    w = juce::jlimit(minW, maxW, w);
    setSize(w, juce::roundToInt((double)w / aspect));
}

// Use the saved window size if it's a real one, otherwise fit to the screen. A
// cached width at/below the minimum is a stale collapse from an earlier build
// (nobody picks the 60%-scale minimum on purpose), so it's treated as "no size".
void NamRigEditor::applyRememberedOrFitSize()
{
    const int minW = kBaseW * 6 / 10;
    if (mProc.uiWidth > minW + 4)
    {
        setSize(mProc.uiWidth, juce::roundToInt((double)mProc.uiWidth * kBaseH / kBaseW));
        clampSizeToScreen(); // shrink if that remembered size doesn't fit this screen
    }
    else
    {
        resizeToFitScreen();
    }
}

// Keep a *remembered* size as-is, but if it's larger than the current screen (a
// project saved on a big display, reopened on a small one) shrink it to fit so the
// title bar can't end up off-screen. Only ever shrinks — never enlarges the user's
// chosen size.
void NamRigEditor::clampSizeToScreen()
{
    const auto work = screenWorkArea();
    if (work.getWidth() < 1000 || work.getHeight() < 700)
        return; // screen size unknown -> leave the remembered size alone
    const double aspect = (double)kBaseW / kBaseH;
    const int fitW = juce::jmin(juce::roundToInt(work.getWidth() * 0.96),
                                juce::roundToInt(work.getHeight() * 0.96 * aspect));
    if (getWidth() > fitW)
        setSize(fitW, juce::roundToInt((double)fitW / aspect));
}

NamRigEditor::~NamRigEditor()
{
    if (&juce::LookAndFeel::getDefaultLookAndFeel() == &mLnf)
        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
    setLookAndFeel(nullptr);
}

void NamRigEditor::showPanel(int selectableIndex)
{
    mCalPanel.setVisible(false);   // selecting a block dismisses the overlays
    mIrBrowser.setVisible(false);
    // Compare by identity, not index: the combined cab panel sits at two indices
    // (CAB A and CAB B), so either tile must reveal it.
    auto *sel = mPanels[(size_t)selectableIndex];
    for (auto *panel : mPanels)
        panel->setVisible(panel == sel);
    mProc.uiSelectedBlock = selectableIndex;
}

void NamRigEditor::openIrBrowser(int rig)
{
    const auto nameOrEmpty = [this](int r) {
        return mProc.isIrLoaded(r) ? mProc.getIrName(r) : juce::String();
    };
    mIrBrowser.openFor(rig, mProc.irLibraryRoot(), nameOrEmpty(0), nameOrEmpty(1));
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
    // Re-fit the default to the actual screen once display info is trustworthy.
    // It's unreliable in the editor constructor (no peer) AND can stay bogus for a
    // while after the window appears in some hosts, so we KEEP RETRYING each tick
    // until we get a plausible work area (or give up after ~3s and accept the
    // fallback). Only when we own the size — a remembered size is left untouched.
    if (!mSizedToScreen)
    {
        const auto work = screenWorkArea();
        const bool valid = work.getWidth() >= 1000 && work.getHeight() >= 700;
        if (valid || ++mFitTries > 90)
        {
            mSizedToScreen = true;
            applyRememberedOrFitSize(); // saved state is loaded now -> authoritative
            mPersistSize = true;        // from here, user resizes are remembered
        }
    }

    const double now = juce::Time::getMillisecondCounterHiRes();
    const float dt = (float)juce::jlimit(0.0, 0.5, (now - mLastTimerMs) * 0.001);
    mLastTimerMs = now;

    mInMeter.push(mProc.mInputPeakDb.load(), dt);
    mOutMeter.push(mProc.mOutputPeakDb.load(), dt);

    mAmpPanelA.refresh();
    mAmpPanelB.refresh();
    mCabPanel.refresh();
    mDrivePanel.refresh();
    mMixPanel.refresh(dt);
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
    mAmpPanelA.setBypassed(aOut || off("ampOnA"));
    mAmpPanelB.setBypassed(bOut || off("ampOnB"));
    mEqPanelA.setBypassed(aOut || off("eqOn"));
    mEqPanelB.setBypassed(bOut || off("eqOnB"));
    // Cab: each side dims independently (keeps its Low/High cuts live), but when
    // BOTH cabs are out the whole CAB panel gets the full "BYPASSED" veil like the
    // other blocks. In Solo, the un-soloed rig's cab counts as out.
    const bool cabAOff = aOut || off("cabOn");
    const bool cabBOff = bOut || off("cabOnB");
    mCabPanel.cabA().setBypassed(cabAOff);
    mCabPanel.cabB().setBypassed(cabBOff);
    mCabPanel.setBypassed(cabAOff && cabBOff);
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
    // Only remember the size once initial sizing is done, so our own provisional
    // setSize calls don't overwrite a just-restored uiWidth before we've used it.
    if (mPersistSize)
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
    mCalPanel.setBounds(content);   // overlay occupies the block-panel area
    mIrBrowser.setBounds(content);  // IR library overlay shares that area
}

juce::AudioProcessorEditor *NamRigProcessor::createEditor()
{
    return new NamRigEditor(*this);
}
