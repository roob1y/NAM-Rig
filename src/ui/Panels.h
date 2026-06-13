#pragma once
#include "PluginProcessor.h"
#include "ui/RigLookAndFeel.h"
#include "ui/GrMeter.h"

namespace nam_rig::ui
{

// Rotary knob + caption + value box, attached to one APVTS parameter.
class LabeledKnob : public juce::Component
{
public:
    LabeledKnob(juce::AudioProcessorValueTreeState &apvts, const juce::String &paramId,
                const juce::String &caption)
    {
        mLabel.setText(caption, juce::dontSendNotification);
        mLabel.setJustificationType(juce::Justification::centred);
        mLabel.setColour(juce::Label::textColourId, colors::textDim);
        addAndMakeVisible(mLabel);

        mSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        mSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 64, 16);
        addAndMakeVisible(mSlider);
        mAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            apvts, paramId, mSlider);
        if (auto *param = apvts.getParameter(paramId)) // double-click = default
            mSlider.setDoubleClickReturnValue(
                true, param->convertFrom0to1(param->getDefaultValue()));
    }

    void resized() override
    {
        auto b = getLocalBounds();
        mLabel.setBounds(b.removeFromTop(16));
        mSlider.setBounds(b);
    }

    juce::Slider &slider() { return mSlider; }

private:
    juce::Label mLabel;
    juce::Slider mSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mAtt;
};

// Common panel chrome: rounded body + title strip; content laid out by subclass.
class BlockPanel : public juce::Component
{
public:
    explicit BlockPanel(const juce::String &title) : mTitle(title) {}

    void paint(juce::Graphics &g) override
    {
        auto b = getLocalBounds().toFloat().reduced(0.5f);
        g.setColour(colors::panel);
        g.fillRoundedRectangle(b, 8.0f);
        g.setColour(colors::outline);
        g.drawRoundedRectangle(b, 8.0f, 1.0f);

        g.setColour(colors::accent);
        g.setFont(RigLookAndFeel::withHeight(15.0f).boldened());
        g.drawText(mTitle, getLocalBounds().removeFromTop(34).reduced(16, 0),
                   juce::Justification::centredLeft);
    }

protected:
    juce::Rectangle<int> contentArea() const
    {
        return getLocalBounds().withTrimmedTop(34).reduced(16, 6);
    }

private:
    juce::String mTitle;
};

//==============================================================================
class GatePanel : public BlockPanel
{
public:
    explicit GatePanel(juce::AudioProcessorValueTreeState &apvts) : BlockPanel("NOISE GATE")
    {
        const std::pair<const char *, const char *> defs[] = {
            {"gateThresh", "Threshold"}, {"gateRange", "Range"},
            {"gateAttack", "Attack"},    {"gateHold", "Hold"},
            {"gateRelease", "Release"},  {"gateLook", "Lookahead"}};
        for (const auto &[id, caption] : defs)
        {
            mKnobs.push_back(std::make_unique<LabeledKnob>(apvts, id, caption));
            addAndMakeVisible(*mKnobs.back());
        }
        mHint.setText("Lookahead preserves pick attacks but adds latency - keep 0 when monitoring live.",
                      juce::dontSendNotification);
        mHint.setColour(juce::Label::textColourId, colors::textDim);
        addAndMakeVisible(mHint);

        mGrLabel.setText("GR", juce::dontSendNotification);
        mGrLabel.setJustificationType(juce::Justification::centred);
        mGrLabel.setColour(juce::Label::textColourId, colors::textDim);
        addAndMakeVisible(mGrLabel);
        addAndMakeVisible(mGrMeter);
    }

    GrMeter &grMeter() { return mGrMeter; }

    void resized() override
    {
        auto area = contentArea();
        mHint.setBounds(area.removeFromBottom(20));
        auto meterCol = area.removeFromRight(46);
        mGrLabel.setBounds(meterCol.removeFromTop(16));
        mGrMeter.setBounds(meterCol.reduced(16, 4));
        area = area.withSizeKeepingCentre(area.getWidth(),
                                          juce::jmin(area.getHeight(), 170));
        const int w = area.getWidth() / (int)mKnobs.size();
        for (auto &k : mKnobs)
            k->setBounds(area.removeFromLeft(w).reduced(6, 0));
    }

private:
    std::vector<std::unique_ptr<LabeledKnob>> mKnobs;
    juce::Label mHint, mGrLabel;
    GrMeter mGrMeter;
};

//==============================================================================
class CompPanel : public BlockPanel
{
public:
    explicit CompPanel(juce::AudioProcessorValueTreeState &apvts)
        : BlockPanel("COMPRESSOR / BOOST")
    {
        const std::pair<const char *, const char *> defs[] = {
            {"compSustain", "Sustain"}, {"compAttack", "Attack"},
            {"compLevel", "Level"},     {"compBoost", "Boost"}};
        for (const auto &[id, caption] : defs)
        {
            mKnobs.push_back(std::make_unique<LabeledKnob>(apvts, id, caption));
            addAndMakeVisible(*mKnobs.back());
        }
        mHint.setText("Pedal-style: Sustain sets squash (auto makeup), Boost is a clean post-gain into the amp.",
                      juce::dontSendNotification);
        mHint.setColour(juce::Label::textColourId, colors::textDim);
        addAndMakeVisible(mHint);

        mGrLabel.setText("GR", juce::dontSendNotification);
        mGrLabel.setJustificationType(juce::Justification::centred);
        mGrLabel.setColour(juce::Label::textColourId, colors::textDim);
        addAndMakeVisible(mGrLabel);
        addAndMakeVisible(mGrMeter);
    }

    GrMeter &grMeter() { return mGrMeter; }

    void resized() override
    {
        auto area = contentArea();
        mHint.setBounds(area.removeFromBottom(20));
        auto meterCol = area.removeFromRight(46);
        mGrLabel.setBounds(meterCol.removeFromTop(16));
        mGrMeter.setBounds(meterCol.reduced(16, 4));
        const int w = juce::jmin(130, area.getWidth() / (int)mKnobs.size());
        auto row = area.withSizeKeepingCentre(w * (int)mKnobs.size(),
                                              juce::jmin(area.getHeight(), 170));
        for (auto &k : mKnobs)
            k->setBounds(row.removeFromLeft(w).reduced(6, 0));
    }

private:
    std::vector<std::unique_ptr<LabeledKnob>> mKnobs;
    juce::Label mHint, mGrLabel;
    GrMeter mGrMeter;
};

//==============================================================================
class AmpPanel : public BlockPanel
{
public:
    AmpPanel(NamRigProcessor &proc, int rig)
        : BlockPanel(rig == 0 ? "AMP A - NEURAL MODEL" : "AMP B - NEURAL MODEL"),
          mProc(proc), mRig(rig)
    {
        addAndMakeVisible(mLoadBtn);
        mLoadBtn.onClick = [this]
        {
            mChooser = std::make_unique<juce::FileChooser>("Select a NAM model",
                                                           juce::File{}, "*.nam");
            mChooser->launchAsync(juce::FileBrowserComponent::openMode |
                                      juce::FileBrowserComponent::canSelectFiles,
                                  [this](const juce::FileChooser &fc)
                                  {
                                      if (fc.getResult().existsAsFile())
                                          mProc.loadModel(fc.getResult(), mRig);
                                  });
        };

        mModelName.setColour(juce::Label::textColourId, colors::text);
        addAndMakeVisible(mModelName);

        auto initCombo = [this](juce::ComboBox &box, juce::Label &label,
                                const juce::StringArray &items, const char *paramId,
                                const char *caption,
                                std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> &att)
        {
            label.setText(caption, juce::dontSendNotification);
            label.setColour(juce::Label::textColourId, colors::textDim);
            addAndMakeVisible(label);
            box.addItemList(items, 1); // must match the parameter StringArray order
            addAndMakeVisible(box);
            att = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
                mProc.apvts, paramId, box);
        };
        initCombo(mLiveAa, mLiveAaLabel, {"Off", "2x", "4x", "8x", "16x", "32x"},
                  "oversample", "Live AA", mLiveAtt);
        initCombo(mOfflineAa, mOfflineAaLabel, {"Same as live", "8x", "16x", "32x"},
                  "offlineAA", "Offline AA", mOfflineAtt);

        mInfo.setColour(juce::Label::textColourId, colors::textDim);
        addAndMakeVisible(mInfo);

        // --- Input calibration + output normalization (NAM-AA parity) ---
        // Both are gated on the model carrying the matching .nam metadata; the
        // controls grey out otherwise (see refresh()).
        mCalToggle.setButtonText("Calibrate input (interface clip dBu)");
        addAndMakeVisible(mCalToggle);
        mCalToggleAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            mProc.apvts, "calEnable", mCalToggle);

        mCalSlider.setSliderStyle(juce::Slider::LinearHorizontal);
        mCalSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 22);
        mCalSlider.setTextValueSuffix(" dBu");
        addAndMakeVisible(mCalSlider);
        mCalSliderAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            mProc.apvts, "calDbu", mCalSlider);

        mNormToggle.setButtonText("Normalize output (-18 dB)");
        addAndMakeVisible(mNormToggle);
        mNormToggleAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            mProc.apvts, "normalize", mNormToggle);
    }

    // Called from the editor timer.
    void refresh()
    {
        const bool loaded = mProc.isModelLoaded(mRig);
        const bool a2 = loaded && mProc.isA2Model(mRig);
        mModelName.setText(loaded ? mProc.getModelName(mRig) : "No model loaded",
                           juce::dontSendNotification);

        // AA only exists for A2 models (dilation-scaled copies); grey it out otherwise.
        const bool aaAvailable = !loaded || a2;
        mLiveAa.setEnabled(aaAvailable);
        mOfflineAa.setEnabled(aaAvailable);

        // Calibration needs input_level_dbu; normalization needs loudness. The
        // slider is live only when calibration is both available and enabled.
        const bool hasCal = mProc.hasInputCalibration();
        mCalToggle.setEnabled(hasCal);
        mCalSlider.setEnabled(hasCal && mCalToggle.getToggleState());
        mNormToggle.setEnabled(mProc.hasLoudness());

        juce::String info;
        if (!loaded)
            info = "Load a .nam model to bring the amp online.";
        else if (!a2)
            info = "Standard model - anti-aliasing controls need an A2 model.";
        else
        {
            const int engaged = mProc.engagedFactor(mRig);
            info = engaged > 0 ? "Engaged at " + juce::String(engaged) + "x"
                               : "Passthrough";
            info << "  |  PDC " << mProc.getLatencySamples() << " smp";
        }
        // Surface the live cal/norm corrections (only when non-zero).
        const float calDb = mProc.calibrationGainDb(mRig);
        if (calDb != 0.0f)
            info << "  |  cal " << (calDb > 0 ? "+" : "") << juce::String(calDb, 1) << " dB";
        const float normDb = mProc.normalizationGainDb(mRig);
        if (normDb != 0.0f)
            info << "  |  norm " << (normDb > 0 ? "+" : "") << juce::String(normDb, 1) << " dB";
        if (info != mInfo.getText())
            mInfo.setText(info, juce::dontSendNotification);
    }

    void resized() override
    {
        auto area = contentArea();
        auto left = area.removeFromLeft(area.getWidth() / 2 - 10);
        mLoadBtn.setBounds(left.removeFromTop(34).removeFromLeft(180));
        left.removeFromTop(8);
        mModelName.setBounds(left.removeFromTop(22));
        left.removeFromTop(4);
        mInfo.setBounds(left.removeFromTop(22));

        area.removeFromLeft(20);
        auto comboRow = [&](juce::Label &l, juce::ComboBox &b)
        {
            auto r = area.removeFromTop(30);
            l.setBounds(r.removeFromLeft(80));
            b.setBounds(r.removeFromLeft(150));
            area.removeFromTop(10);
        };
        comboRow(mLiveAaLabel, mLiveAa);
        comboRow(mOfflineAaLabel, mOfflineAa);

        // Calibration + normalization sit below the AA combos in the right column.
        area.removeFromTop(6);
        mCalToggle.setBounds(area.removeFromTop(24));
        mCalSlider.setBounds(area.removeFromTop(26).removeFromLeft(240));
        area.removeFromTop(8);
        mNormToggle.setBounds(area.removeFromTop(24));
    }

private:
    NamRigProcessor &mProc;
    int mRig = 0;
    juce::TextButton mLoadBtn{"Load NAM model..."};
    juce::Label mModelName, mInfo;
    juce::ComboBox mLiveAa, mOfflineAa;
    juce::Label mLiveAaLabel, mOfflineAaLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> mLiveAtt, mOfflineAtt;
    juce::ToggleButton mCalToggle, mNormToggle;
    juce::Slider mCalSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> mCalToggleAtt, mNormToggleAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mCalSliderAtt;
    std::unique_ptr<juce::FileChooser> mChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AmpPanel)
};

//==============================================================================
class EqPanel : public BlockPanel
{
public:
    EqPanel(juce::AudioProcessorValueTreeState &apvts, int rig)
        : BlockPanel(rig == 0 ? "GRAPHIC EQ A - PRE-CAB" : "GRAPHIC EQ B - PRE-CAB"),
          mApvts(apvts)
    {
        static const char *idsA[] = {"eq62", "eq125", "eq250", "eq500",
                                     "eq1k", "eq2k", "eq4k", "eq8k"};
        static const char *idsB[] = {"rigBeq62", "rigBeq125", "rigBeq250", "rigBeq500",
                                     "rigBeq1k", "rigBeq2k", "rigBeq4k", "rigBeq8k"};
        const char *const *ids = (rig == 0) ? idsA : idsB;
        for (int b = 0; b < 8; ++b)
            mBandIds[b] = ids[b]; // remembered for the Flat button
        static const char *captions[] = {"62.5", "125", "250", "500",
                                         "1k", "2k", "4k", "8k"};
        for (int b = 0; b < 8; ++b)
        {
            auto slider = std::make_unique<juce::Slider>(juce::Slider::LinearVertical,
                                                         juce::Slider::NoTextBox);
            slider->setPopupDisplayEnabled(true, true, this);
            slider->onValueChange = [this] { repaint(); }; // redraw response curve
            slider->setDoubleClickReturnValue(true, 0.0); // double-click = flat
            addAndMakeVisible(*slider);
            mAtts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
                apvts, ids[b], *slider));
            mSliders.push_back(std::move(slider));

            auto label = std::make_unique<juce::Label>(juce::String{}, captions[b]);
            label->setJustificationType(juce::Justification::centred);
            label->setColour(juce::Label::textColourId, colors::textDim);
            addAndMakeVisible(*label);
            mLabels.push_back(std::move(label));
        }

        addAndMakeVisible(mFlatBtn);
        mFlatBtn.onClick = [this]
        {
            for (const char *id : mBandIds)
                if (auto *p = mApvts.getParameter(id))
                {
                    p->beginChangeGesture();
                    p->setValueNotifyingHost(p->getDefaultValue());
                    p->endChangeGesture();
                }
        };
    }

    void resized() override
    {
        auto area = contentArea();
        mCurveArea = area.removeFromTop(54).reduced(0, 2);
        mFlatBtn.setBounds(area.removeFromRight(64).withSizeKeepingCentre(56, 26));
        area.removeFromRight(8);
        const int w = area.getWidth() / (int)mSliders.size();
        for (size_t i = 0; i < mSliders.size(); ++i)
        {
            auto col = area.removeFromLeft(w);
            mLabels[i]->setBounds(col.removeFromBottom(16));
            mSliders[i]->setBounds(col.withSizeKeepingCentre(juce::jmin(40, w), col.getHeight()));
        }
    }

    void paint(juce::Graphics &g) override
    {
        BlockPanel::paint(g);

        // Combined response from the SAME RBJ designs the DSP runs (drawn at
        // nominal 48 kHz; the actual rate only shifts the extreme HF end).
        if (!mCurveArea.isEmpty())
        {
            const double fs = 48000.0;
            std::array<Biquad, EqBlock::kNumBands> filters;
            for (int b = 0; b < EqBlock::kNumBands; ++b)
                filters[(size_t)b] = Biquad::peaking(fs, EqBlock::kBandHz[(size_t)b],
                                                     EqBlock::kQ,
                                                     mSliders[(size_t)b]->getValue());

            g.setColour(colors::outline);
            g.drawHorizontalLine(mCurveArea.getCentreY(), (float)mCurveArea.getX(),
                                 (float)mCurveArea.getRight());

            juce::Path curve;
            const double fLo = 30.0, fHi = 16000.0;
            const int n = mCurveArea.getWidth();
            for (int x = 0; x < n; ++x)
            {
                const double f = fLo * std::pow(fHi / fLo, (double)x / (double)(n - 1));
                double db = 0.0;
                for (auto &bi : filters)
                    if (!bi.isIdentity())
                        db += 20.0 * std::log10(std::max(bi.magnitudeAt(fs, f), 1.0e-6));
                const float y = juce::jlimit(
                    (float)mCurveArea.getY(), (float)mCurveArea.getBottom(),
                    (float)mCurveArea.getCentreY()
                        - (float)(db / (double)EqBlock::kMaxGainDb)
                              * ((float)mCurveArea.getHeight() * 0.5f - 2.0f));
                if (x == 0)
                    curve.startNewSubPath((float)mCurveArea.getX(), y);
                else
                    curve.lineTo((float)(mCurveArea.getX() + x), y);
            }
            g.setColour(colors::accent);
            g.strokePath(curve, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved));
        }

        // 0 dB line across the slider field
        if (!mSliders.empty())
        {
            auto first = mSliders.front()->getBounds();
            auto last = mSliders.back()->getBounds();
            const float cy = (float)first.getCentreY();
            g.setColour(colors::outline);
            g.drawHorizontalLine((int)cy, (float)first.getX(), (float)last.getRight());
        }
    }

private:
    juce::AudioProcessorValueTreeState &mApvts;
    const char *mBandIds[8]{};
    std::vector<std::unique_ptr<juce::Slider>> mSliders;
    std::vector<std::unique_ptr<juce::Label>> mLabels;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> mAtts;
    juce::TextButton mFlatBtn{"Flat"};
    juce::Rectangle<int> mCurveArea;
};

//==============================================================================
class CabPanel : public BlockPanel
{
public:
    CabPanel(NamRigProcessor &proc, int rig)
        : BlockPanel(rig == 0 ? "CABINET A - IR" : "CABINET B - IR"), mProc(proc), mRig(rig)
    {
        addAndMakeVisible(mLoadBtn);
        mLoadBtn.onClick = [this]
        {
            mChooser = std::make_unique<juce::FileChooser>("Select a cab IR", juce::File{},
                                                           "*.wav;*.aif;*.aiff");
            mChooser->launchAsync(juce::FileBrowserComponent::openMode |
                                      juce::FileBrowserComponent::canSelectFiles,
                                  [this](const juce::FileChooser &fc)
                                  {
                                      if (fc.getResult().existsAsFile())
                                          mProc.loadIr(fc.getResult(), mRig);
                                  });
        };

        mIrName.setColour(juce::Label::textColourId, colors::text);
        addAndMakeVisible(mIrName);

        mHpf = std::make_unique<LabeledKnob>(mProc.apvts, rig == 0 ? "cabHpf" : "rigBcabHpf", "Low Cut");
        mLpf = std::make_unique<LabeledKnob>(mProc.apvts, rig == 0 ? "cabLpf" : "rigBcabLpf", "High Cut");
        addAndMakeVisible(*mHpf);
        addAndMakeVisible(*mLpf);

        mHint.setText("Cuts sit post-cab (12 dB/oct). Knob extremes = filter off, bit-exact.",
                      juce::dontSendNotification);
        mHint.setColour(juce::Label::textColourId, colors::textDim);
        addAndMakeVisible(mHint);
    }

    void refresh()
    {
        mIrName.setText(mProc.isIrLoaded(mRig) ? mProc.getIrName(mRig)
                                               : juce::String("No IR loaded - amp runs direct"),
                        juce::dontSendNotification);
    }

    void resized() override
    {
        auto area = contentArea();
        mHint.setBounds(area.removeFromBottom(20));

        auto left = area.removeFromLeft(area.getWidth() / 2 - 10);
        mLoadBtn.setBounds(left.removeFromTop(34).removeFromLeft(180));
        left.removeFromTop(8);
        mIrName.setBounds(left.removeFromTop(22));

        area.removeFromLeft(20);
        area = area.withSizeKeepingCentre(area.getWidth(),
                                          juce::jmin(area.getHeight(), 170));
        const int w = juce::jmin(130, area.getWidth() / 2);
        mHpf->setBounds(area.removeFromLeft(w).reduced(6, 0));
        mLpf->setBounds(area.removeFromLeft(w).reduced(6, 0));
    }

private:
    NamRigProcessor &mProc;
    int mRig = 0;
    juce::TextButton mLoadBtn{"Load cab IR..."};
    juce::Label mIrName, mHint;
    std::unique_ptr<LabeledKnob> mHpf, mLpf;
    std::unique_ptr<juce::FileChooser> mChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CabPanel)
};

//==============================================================================
class ModPanel : public BlockPanel
{
public:
    explicit ModPanel(juce::AudioProcessorValueTreeState &apvts)
        : BlockPanel("MODULATION"), mApvts(apvts)
    {
        mTypeLabel.setText("Type", juce::dontSendNotification);
        mTypeLabel.setColour(juce::Label::textColourId, colors::textDim);
        addAndMakeVisible(mTypeLabel);
        mType.addItemList({"Chorus", "Flanger", "Phaser", "Tremolo"}, 1);
        addAndMakeVisible(mType);
        mTypeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            apvts, "modType", mType);

        const std::pair<const char *, const char *> defs[] = {
            {"modRate", "Rate"}, {"modDepth", "Depth"},
            {"modFeedback", "Feedback"}, {"modMix", "Mix"}};
        for (const auto &[id, caption] : defs)
        {
            mKnobs.push_back(std::make_unique<LabeledKnob>(apvts, id, caption));
            addAndMakeVisible(*mKnobs.back());
        }
    }

    void refresh() // feedback only does something for flanger / phaser
    {
        const int type = (int)mApvts.getRawParameterValue("modType")->load();
        mKnobs[2]->setEnabled(type == 1 || type == 2);
    }

    void resized() override
    {
        auto area = contentArea();
        auto typeRow = area.removeFromTop(30);
        mTypeLabel.setBounds(typeRow.removeFromLeft(50));
        mType.setBounds(typeRow.removeFromLeft(150));
        area.removeFromTop(4);
        area = area.withSizeKeepingCentre(area.getWidth(),
                                          juce::jmin(area.getHeight(), 150));
        const int w = juce::jmin(130, area.getWidth() / (int)mKnobs.size());
        auto row = area.withSizeKeepingCentre(w * (int)mKnobs.size(), area.getHeight());
        for (auto &k : mKnobs)
            k->setBounds(row.removeFromLeft(w).reduced(6, 0));
    }

private:
    juce::AudioProcessorValueTreeState &mApvts;
    juce::ComboBox mType;
    juce::Label mTypeLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> mTypeAtt;
    std::vector<std::unique_ptr<LabeledKnob>> mKnobs;
};

//==============================================================================
class DelayPanel : public BlockPanel
{
public:
    explicit DelayPanel(juce::AudioProcessorValueTreeState &apvts)
        : BlockPanel("DELAY"), mApvts(apvts)
    {
        mSyncLabel.setText("Sync", juce::dontSendNotification);
        mSyncLabel.setColour(juce::Label::textColourId, colors::textDim);
        addAndMakeVisible(mSyncLabel);
        mSync.addItemList({"Free", "1/1", "1/2.", "1/2", "1/2T", "1/4.", "1/4",
                           "1/4T", "1/8.", "1/8", "1/8T", "1/16.", "1/16", "1/16T"},
                          1);
        addAndMakeVisible(mSync);
        mSyncAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            apvts, "delaySync", mSync);

        mPingPong.setButtonText("Ping-pong");
        addAndMakeVisible(mPingPong);
        mPpAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            apvts, "delayPingPong", mPingPong);

        const std::pair<const char *, const char *> defs[] = {
            {"delayTime", "Time"},        {"delayFeedback", "Feedback"},
            {"delayTone", "Tone"},        {"delayMod", "Wow/Flutter"},
            {"delayWidth", "Width"},      {"delayMix", "Mix"}};
        for (const auto &[id, caption] : defs)
        {
            mKnobs.push_back(std::make_unique<LabeledKnob>(apvts, id, caption));
            addAndMakeVisible(*mKnobs.back());
        }
    }

    void refresh() // time knob is owned by the sync division when sync != Free
    {
        const int sync = (int)mApvts.getRawParameterValue("delaySync")->load();
        mKnobs[0]->setEnabled(sync == 0);
    }

    void resized() override
    {
        auto area = contentArea();
        auto topRow = area.removeFromTop(30);
        mSyncLabel.setBounds(topRow.removeFromLeft(50));
        mSync.setBounds(topRow.removeFromLeft(110));
        topRow.removeFromLeft(20);
        mPingPong.setBounds(topRow.removeFromLeft(110));
        area.removeFromTop(4);
        area = area.withSizeKeepingCentre(area.getWidth(),
                                          juce::jmin(area.getHeight(), 150));
        const int w = area.getWidth() / (int)mKnobs.size();
        for (auto &k : mKnobs)
            k->setBounds(area.removeFromLeft(w).reduced(6, 0));
    }

private:
    juce::AudioProcessorValueTreeState &mApvts;
    juce::ComboBox mSync;
    juce::Label mSyncLabel;
    juce::ToggleButton mPingPong;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> mSyncAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> mPpAtt;
    std::vector<std::unique_ptr<LabeledKnob>> mKnobs;
};

//==============================================================================
class ReverbPanel : public BlockPanel
{
public:
    explicit ReverbPanel(juce::AudioProcessorValueTreeState &apvts) : BlockPanel("REVERB")
    {
        const std::pair<const char *, const char *> defs[] = {
            {"revSize", "Size"},         {"revDecay", "Decay"},
            {"revDamp", "Damping"},      {"revPredelay", "Pre-Delay"},
            {"revMix", "Mix"}};
        for (const auto &[id, caption] : defs)
        {
            mKnobs.push_back(std::make_unique<LabeledKnob>(apvts, id, caption));
            addAndMakeVisible(*mKnobs.back());
        }
    }

    void resized() override
    {
        auto area = contentArea();
        area = area.withSizeKeepingCentre(area.getWidth(),
                                          juce::jmin(area.getHeight(), 170));
        const int w = juce::jmin(140, area.getWidth() / (int)mKnobs.size());
        auto row = area.withSizeKeepingCentre(w * (int)mKnobs.size(), area.getHeight());
        for (auto &k : mKnobs)
            k->setBounds(row.removeFromLeft(w).reduced(6, 0));
    }

private:
    std::vector<std::unique_ptr<LabeledKnob>> mKnobs;
};

//==============================================================================
// MIX — dual-rig routing: mode (Solo A / Solo B / Dual), per-rig level + pan +
// polarity, the phase-align nudge, and the Auto-align button (probes both
// voices). The two rigs merge here into the shared stereo section.
class MixPanel : public BlockPanel
{
public:
    explicit MixPanel(NamRigProcessor &proc) : BlockPanel("MIX - DUAL RIG"), mProc(proc)
    {
        mModeLabel.setText("Mode", juce::dontSendNotification);
        mModeLabel.setColour(juce::Label::textColourId, colors::textDim);
        addAndMakeVisible(mModeLabel);
        mMode.addItemList({"Solo A", "Solo B", "Dual"}, 1); // matches rigMode order
        addAndMakeVisible(mMode);
        mModeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            mProc.apvts, "rigMode", mMode);

        auto rigTag = [this](juce::Label &l, const char *txt)
        {
            l.setText(txt, juce::dontSendNotification);
            l.setJustificationType(juce::Justification::centred);
            l.setColour(juce::Label::textColourId, colors::accent);
            addAndMakeVisible(l);
        };
        rigTag(mALabel, "RIG A");
        rigTag(mBLabel, "RIG B");

        mLevelA = std::make_unique<LabeledKnob>(mProc.apvts, "rigLevelA", "Level");
        mPanA = std::make_unique<LabeledKnob>(mProc.apvts, "rigPanA", "Pan");
        mLevelB = std::make_unique<LabeledKnob>(mProc.apvts, "rigLevelB", "Level");
        mPanB = std::make_unique<LabeledKnob>(mProc.apvts, "rigPanB", "Pan");
        mAlign = std::make_unique<LabeledKnob>(mProc.apvts, "rigAlign", "Align");
        for (auto *k : {mLevelA.get(), mPanA.get(), mLevelB.get(), mPanB.get(), mAlign.get()})
            addAndMakeVisible(*k);

        mPolA.setButtonText("Invert");
        mPolB.setButtonText("Invert");
        addAndMakeVisible(mPolA);
        addAndMakeVisible(mPolB);
        mPolAAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            mProc.apvts, "rigPolA", mPolA);
        mPolBAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            mProc.apvts, "rigPolB", mPolB);

        mAutoBtn.onClick = [this] { mProc.autoAlign(); };
        addAndMakeVisible(mAutoBtn);
        mHint.setColour(juce::Label::textColourId, colors::textDim);
        mHint.setText("Auto-align probes both voices and sets the nudge + Rig B polarity.",
                      juce::dontSendNotification);
        addAndMakeVisible(mHint);
    }

    // Called from the editor timer.
    void refresh()
    {
        const int mode = (int)mProc.apvts.getRawParameterValue("rigMode")->load();
        const bool dual = (mode == 2);
        // Pan / polarity only matter in Dual (Solo plays centered); grey otherwise.
        mPanA->setEnabled(dual);
        mPanB->setEnabled(dual);
        mPolA.setEnabled(dual);
        mPolB.setEnabled(dual);
        // Auto-align needs a model in BOTH rigs.
        mAutoBtn.setEnabled(mProc.isModelLoaded(0) && mProc.isModelLoaded(1));
    }

    void resized() override
    {
        auto area = contentArea();

        auto modeRow = area.removeFromTop(30);
        mModeLabel.setBounds(modeRow.removeFromLeft(50));
        mMode.setBounds(modeRow.removeFromLeft(150));
        area.removeFromTop(8);

        auto rigRow = [&](juce::Label &tag, LabeledKnob &lvl, LabeledKnob &pan,
                          juce::ToggleButton &pol)
        {
            auto r = area.removeFromTop(96);
            tag.setBounds(r.removeFromLeft(64).withSizeKeepingCentre(64, 20));
            lvl.setBounds(r.removeFromLeft(96).reduced(6, 0));
            pan.setBounds(r.removeFromLeft(96).reduced(6, 0));
            pol.setBounds(r.removeFromLeft(90).withSizeKeepingCentre(90, 24));
            area.removeFromTop(4);
        };
        rigRow(mALabel, *mLevelA, *mPanA, mPolA);
        rigRow(mBLabel, *mLevelB, *mPanB, mPolB);

        auto alignRow = area.removeFromTop(96);
        mAlign->setBounds(alignRow.removeFromLeft(96).reduced(6, 0));
        mAutoBtn.setBounds(alignRow.removeFromLeft(120).withSizeKeepingCentre(120, 28));
        alignRow.removeFromLeft(10);
        mHint.setBounds(alignRow.withSizeKeepingCentre(alignRow.getWidth(), 40));
    }

private:
    NamRigProcessor &mProc;
    juce::ComboBox mMode;
    juce::Label mModeLabel, mALabel, mBLabel, mHint;
    std::unique_ptr<LabeledKnob> mLevelA, mPanA, mLevelB, mPanB, mAlign;
    juce::ToggleButton mPolA, mPolB;
    juce::TextButton mAutoBtn{"Auto-align"};
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> mModeAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> mPolAAtt, mPolBAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixPanel)
};

} // namespace nam_rig::ui
