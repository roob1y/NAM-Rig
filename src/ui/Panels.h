#pragma once
#include "PluginProcessor.h"
#include "ui/RigLookAndFeel.h"
#include "ui/GrMeter.h"
#include "ui/CompMeter.h"
#include "ui/CompCurve.h"
#include "ui/ModFxIcon.h"

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

    // Relabel at runtime (the mod flanger renames Depth->Width to match the M-126).
    void setCaption(const juce::String &caption)
    {
        mLabel.setText(caption, juce::dontSendNotification);
    }

    // Show the value box as a 0..top reading of the knob's ROTATION (pedal-style,
    // "everything goes to 10") instead of raw parameter units -- so a lane of
    // mixed params (Hz, 0..1, ratios) all read on one friendly scale. Display
    // only: the underlying parameter, presets and automation are untouched. Reads
    // the slider's live range/skew, so a per-effect Speed cap still maps full
    // rotation to the top of the scale. Call updateReadout() after a range change.
    void setRotationReadout(double top = 10.0)
    {
        auto *s = &mSlider;
        mSlider.textFromValueFunction = [s, top](double v) {
            return juce::String(s->valueToProportionOfLength(v) * top, 1);
        };
        mSlider.valueFromTextFunction = [s, top](const juce::String &t) {
            return s->proportionOfLengthToValue(juce::jlimit(0.0, 1.0, t.getDoubleValue() / top));
        };
        mSlider.updateText();
    }
    void updateReadout() { mSlider.updateText(); }

    // Re-point this knob at a different parameter (used by the drive pedal to
    // give each pedal TYPE its own Drive/Tone/Level instead of sharing one set).
    void rebind(juce::AudioProcessorValueTreeState &apvts, const juce::String &paramId)
    {
        mAtt.reset(); // destroy the old attachment before making the new one
        mAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            apvts, paramId, mSlider);
        if (auto *param = apvts.getParameter(paramId))
            mSlider.setDoubleClickReturnValue(
                true, param->convertFrom0to1(param->getDefaultValue()));
    }

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
            {"compLevel", "Level"},     {"compBoost", "Boost"},
            {"compCharacter", "Character"}};
        for (const auto &[id, caption] : defs)
        {
            mKnobs.push_back(std::make_unique<LabeledKnob>(apvts, id, caption));
            addAndMakeVisible(*mKnobs.back());
        }

        mHint.setText("GR shows squash + history trail; IN/OUT track gain staging. Advanced adds the transfer curve.",
                      juce::dontSendNotification);
        mHint.setColour(juce::Label::textColourId, colors::textDim);
        addAndMakeVisible(mHint);

        addAndMakeVisible(mMeter);
        addChildComponent(mCurve); // Advanced view only

        // Live input level drives the transfer-curve operating dot.
        mMeter.onInput = [this](float inDb) { mCurve.setInputDb(inDb); };

        mAdvBtn.setClickingTogglesState(true);
        mAdvBtn.onClick = [this] { setAdvanced(mAdvBtn.getToggleState()); };
        addAndMakeVisible(mAdvBtn);

        // Sustain knob drives the curve's threshold marker.
        mKnobs[0]->slider().onValueChange = [this] {
            mCurve.setSustain((float)mKnobs[0]->slider().getValue());
        };
        mCurve.setSustain((float)mKnobs[0]->slider().getValue());

        // Voicing selector (Clean / OTA / Opto / FET) -> compMode param.
        mModeBox.addItemList(juce::StringArray{"Clean", "OTA", "Opto", "FET"}, 1);
        mModeBox.setJustificationType(juce::Justification::centred);
        mModeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            apvts, "compMode", mModeBox);
        mModeBox.onChange = [this] { updateCurveShape(); };
        addAndMakeVisible(mModeBox);
        updateCurveShape();

        setAdvanced(false);
    }

    // Drop-in for the editor's existing comp line (CompMeter::push(grDb, dt)).
    CompMeter &grMeter() { return mMeter; }

    void resized() override
    {
        auto area = contentArea();

        auto bottom = area.removeFromBottom(22);
        mAdvBtn.setBounds(bottom.removeFromRight(88).reduced(0, 2));
        bottom.removeFromRight(8);
        mModeBox.setBounds(bottom.removeFromLeft(96).reduced(0, 1));
        bottom.removeFromLeft(8);
        mHint.setBounds(bottom);

        auto meterCol = area.removeFromRight(96); // GR + IN + OUT columns
        mMeter.setBounds(meterCol.reduced(4, 2));

        if (mAdvanced)
        {
            const int curveW = juce::jlimit(120, 210, area.getWidth() / 2);
            mCurve.setBounds(area.removeFromLeft(curveW).reduced(2));
        }

        auto row = area.withSizeKeepingCentre(
            juce::jmin(area.getWidth(), 120 * (int)mKnobs.size()),
            juce::jmin(area.getHeight(), 170));
        const int w = row.getWidth() / (int)mKnobs.size();
        for (auto &k : mKnobs)
            k->setBounds(row.removeFromLeft(w).reduced(6, 0));
    }

private:
    void setAdvanced(bool adv)
    {
        mAdvanced = adv;
        mCurve.setVisible(adv);
        mAdvBtn.setButtonText(adv ? "Advanced" : "Simple");
        mAdvBtn.setToggleState(adv, juce::dontSendNotification);
        resized();
    }

    void updateCurveShape()
    {
        const int idx = juce::jmax(0, mModeBox.getSelectedItemIndex());
        const auto v = nam_rig::CompBlock::voicingFor((nam_rig::CompBlock::Mode)idx);
        mCurve.setShape(v.ratio, v.kneeDb);
    }

    std::vector<std::unique_ptr<LabeledKnob>> mKnobs;
    juce::Label mHint;
    CompMeter mMeter;
    CompCurve mCurve;
    juce::TextButton mAdvBtn;
    juce::ComboBox mModeBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> mModeAtt;
    bool mAdvanced = false;
};

//==============================================================================
// DrivePanel — the 3-slot series drive rack (shared, feeds both rigs). Each row
// is one pedal: Type selector (Off/Boost/Overdrive/Distortion/Fuzz) + Drive /
// Tone / Level. Type=Off bypasses that slot; an all-Off rack is bit-exact.
class DrivePanel : public BlockPanel
{
public:
    explicit DrivePanel(juce::AudioProcessorValueTreeState &apvts)
        : BlockPanel("DRIVE RACK")
    {
        for (int sIdx = 0; sIdx < nam_rig::DriveBlock::kSlots; ++sIdx)
        {
            Row &row = mRows[sIdx];
            const juce::String pid = "drv" + juce::String(sIdx + 1);

            row.label.setText("PEDAL " + juce::String(sIdx + 1), juce::dontSendNotification);
            row.label.setJustificationType(juce::Justification::centredLeft);
            row.label.setColour(juce::Label::textColourId, colors::textDim);
            addAndMakeVisible(row.label);

            row.type.addItemList(
                juce::StringArray{"Off", "Treble Boost", "Overdrive", "Distortion", "Fuzz"}, 1);
            row.type.setJustificationType(juce::Justification::centred);
            row.typeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
                apvts, pid + "Type", row.type);
            addAndMakeVisible(row.type);

            row.drive = std::make_unique<LabeledKnob>(apvts, pid + "Drive", "Drive");
            row.tone  = std::make_unique<LabeledKnob>(apvts, pid + "Tone", "Tone");
            row.level = std::make_unique<LabeledKnob>(apvts, pid + "Level", "Level");
            addAndMakeVisible(*row.drive);
            addAndMakeVisible(*row.tone);
            addAndMakeVisible(*row.level);
        }

        mHint.setText("Three pedals in series feeding both rigs. Stack a boost into an "
                      "overdrive, or set Type to Off to skip a slot.",
                      juce::dontSendNotification);
        mHint.setColour(juce::Label::textColourId, colors::textDim);
        addAndMakeVisible(mHint);
    }

    void resized() override
    {
        auto area = contentArea();
        mHint.setBounds(area.removeFromBottom(20));
        const int rowH = area.getHeight() / nam_rig::DriveBlock::kSlots;
        for (int sIdx = 0; sIdx < nam_rig::DriveBlock::kSlots; ++sIdx)
        {
            auto r = area.removeFromTop(rowH).reduced(0, 4);
            auto left = r.removeFromLeft(116);
            mRows[sIdx].label.setBounds(left.removeFromTop(18));
            mRows[sIdx].type.setBounds(left.removeFromTop(26).reduced(0, 2));
            const int w = juce::jmax(1, r.getWidth() / 3);
            mRows[sIdx].drive->setBounds(r.removeFromLeft(w).reduced(6, 0));
            mRows[sIdx].tone->setBounds(r.removeFromLeft(w).reduced(6, 0));
            mRows[sIdx].level->setBounds(r.removeFromLeft(w).reduced(6, 0));
        }
    }

private:
    struct Row
    {
        juce::Label label;
        juce::ComboBox type;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> typeAtt;
        std::unique_ptr<LabeledKnob> drive, tone, level;
    };
    Row mRows[nam_rig::DriveBlock::kSlots];
    juce::Label mHint;
};

//==============================================================================
// CalPanel - global INPUT calibration, opened from the header INPUT button.
// Sets the level of the incoming signal (your interface's dBu at 0 dBFS) so the
// shared pre-amp section (drive rack, gate, comp) is driven consistently; each
// amp still lands on its own capture level automatically (CalNorm.h splits the
// correction into this global stage + a per-rig residual). Shown as an overlay
// over the block-panel area; Close (or selecting any block) hides it.
class CalPanel : public BlockPanel
{
public:
    std::function<void()> onClose;

    explicit CalPanel(juce::AudioProcessorValueTreeState &apvts)
        : BlockPanel("INPUT CALIBRATION")
    {
        mEnable.setButtonText("Calibrate input level");
        addAndMakeVisible(mEnable);
        mEnableAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            apvts, "calEnable", mEnable);

        mDbu.setSliderStyle(juce::Slider::LinearHorizontal);
        mDbu.setTextBoxStyle(juce::Slider::TextBoxRight, false, 64, 22);
        mDbu.setTextValueSuffix(" dBu");
        addAndMakeVisible(mDbu);
        mDbuAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            apvts, "calDbu", mDbu);

        mHint.setJustificationType(juce::Justification::topLeft);
        mHint.setColour(juce::Label::textColourId, colors::textDim);
        mHint.setText("Tells the rig what your interface sends at 0 dBFS so the drive pedals "
                      "and both amps are driven at a consistent level. Each amp still hits its "
                      "own captured sweet spot automatically. Reference is 12 dBu, so the "
                      "default leaves your sound unchanged - raise or lower it to match a "
                      "hotter or quieter interface.",
                      juce::dontSendNotification);
        addAndMakeVisible(mHint);

        mClose.setButtonText("Close");
        mClose.onClick = [this] { if (onClose) onClose(); };
        addAndMakeVisible(mClose);
    }

    void resized() override
    {
        auto area = contentArea();
        auto bottom = area.removeFromBottom(30);
        mClose.setBounds(bottom.removeFromRight(96).reduced(0, 3));
        mEnable.setBounds(area.removeFromTop(28));
        area.removeFromTop(8);
        mDbu.setBounds(area.removeFromTop(28).removeFromLeft(340));
        area.removeFromTop(14);
        mHint.setBounds(area.removeFromTop(96));
    }

private:
    juce::ToggleButton mEnable;
    juce::Slider mDbu;
    juce::Label mHint;
    juce::TextButton mClose;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> mEnableAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mDbuAtt;
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
                  rig == 0 ? "oversample" : "oversampleB", "Live AA", mLiveAtt);
        initCombo(mOfflineAa, mOfflineAaLabel, {"Same as live", "8x", "16x", "32x"},
                  rig == 0 ? "offlineAA" : "offlineAAB", "Offline AA", mOfflineAtt);

        mInfo.setColour(juce::Label::textColourId, colors::textDim);
        addAndMakeVisible(mInfo);

        // --- Output normalization (NAM-AA parity; per-amp loudness). Input
        //     calibration now lives in the global INPUT panel (header button). ---
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

        // Normalization needs loudness metadata; grey out otherwise.
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

        // Normalization sits below the AA combos in the right column.
        area.removeFromTop(6);
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
    juce::ToggleButton mNormToggle;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> mNormToggleAtt;
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
// One slot's controls. Shows only the controls the selected effect actually
// has: universal Rate/Sync/Mix/Width/On always; Depth (all but Phaser);
// Feedback (Flanger/Phaser); Shape waveform (Tremolo). Everything else is
// hardwired in ModVoice, so there are no bad-sound knobs.
// One slot as a horizontal lane: number+LED, animated effect icon, Type/Sync,
// only the effect's real knobs, Shape (tremolo), and an On toggle. All three
// lanes are shown at once (no tabs) so the whole section reads at a glance.
class ModSlotLane : public juce::Component
{
public:
    // prefix = APVTS id prefix ("mod1".."mod3" for front slots, "post" for the
    // post block). soloSlot = 0-based slot for the solo button, or -1 for the
    // post lane (no solo). label = the small lane badge ("1".."3" or "P").
    ModSlotLane(juce::AudioProcessorValueTreeState &apvts, juce::String prefix,
                juce::String label, int soloSlot)
        : mApvts(apvts), mPrefix(std::move(prefix)), mSoloSlot(soloSlot)
    {
        const juce::String p = mPrefix;
        const bool isFront = (soloSlot >= 0);

        mNum.setText(label, juce::dontSendNotification);
        mNum.setJustificationType(juce::Justification::centred);
        mNum.setColour(juce::Label::textColourId, colors::textDim);
        addAndMakeVisible(mNum);
        addAndMakeVisible(mIcon);

        // Only the effects valid for this position appear in the list (item id =
        // enum + 1, so the id carries the ModVoice::Type). Front = the 6 front
        // effects; post = the 3 amp/speaker effects. The param stays the full
        // 9-choice enum (presets unaffected), so the combo is synced by hand --
        // a filtered list can't use a ComboBoxAttachment.
        if (isFront)
        {
            mType.addItem("Chorus", 1);
            mType.addItem("Flanger", 2);
            mType.addItem("Phaser", 3);
            mType.addItem("Vibrato", 5);
            mType.addItem("Uni-Vibe", 7);
            mType.addItem("Bi-Phase", 9);
        }
        else
        {
            mType.addItem("Tremolo", 4);
            mType.addItem("Rotary", 6);
            mType.addItem("Harm Trem", 8);
        }
        addAndMakeVisible(mType);
        mType.onChange = [this] { syncTypeParam(); };

        mSync.addItemList({"Off", "1/1", "1/2", "1/4", "1/4.", "1/4T",
                           "1/8", "1/8.", "1/8T", "1/16"}, 1);
        addAndMakeVisible(mSync);
        mSyncAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            apvts, p + "Sync", mSync);
        mSync.onChange = [this] { refresh(); };

        mWave.addItemList({"Sine", "Triangle", "Square", "S&H"}, 1);
        addChildComponent(mWave); // Tremolo only
        mWaveAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            apvts, p + "Wave", mWave);

        mOn.setButtonText("On");
        addAndMakeVisible(mOn);
        mOnAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            apvts, p + "On", mOn);
        mOn.onClick = [this] { refresh(); };

        // Solo = momentary dial-in (front slots only; NOT an APVTS param).
        if (isFront)
        {
            mSolo.setButtonText("S");
            mSolo.setClickingTogglesState(true);
            addAndMakeVisible(mSolo);
            mSolo.onClick = [this] { if (onSolo) onSolo(mSoloSlot, mSolo.getToggleState()); };
        }

        mRate = std::make_unique<LabeledKnob>(apvts, p + "Rate", "Rate");
        mDepth = std::make_unique<LabeledKnob>(apvts, p + "Depth", "Depth");
        mFeedback = std::make_unique<LabeledKnob>(apvts, p + "Feedback", "Feedback");
        mMix = std::make_unique<LabeledKnob>(apvts, p + "Mix", "Mix");
        mWidth = std::make_unique<LabeledKnob>(apvts, p + "Width", "Width");
        addAndMakeVisible(*mRate);
        addChildComponent(*mDepth);
        addChildComponent(*mFeedback);
        addChildComponent(*mMix); // chorus only (others hardwire their blend)
        addAndMakeVisible(*mWidth);

        mDrive = std::make_unique<LabeledKnob>(apvts, p + "Drive", "Drive");
        addChildComponent(*mDrive); // rotary only (Leslie tube amp)
        mHornDrum = std::make_unique<LabeledKnob>(apvts, p + "HornDrum", "Horn/Drum");
        addChildComponent(*mHornDrum); // rotary only (horn<->drum balance)
        mRotFast.setButtonText("Fast");
        addChildComponent(mRotFast); // rotary only (slow/fast rotor)
        mRotFastAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            apvts, p + "RotFast", mRotFast);

        mManual = std::make_unique<LabeledKnob>(apvts, p + "Manual", "Manual");
        addChildComponent(*mManual); // flanger only (M-126 static comb position)
        mInvert.setButtonText("Inv");
        addChildComponent(mInvert); // flanger only (phase invert)
        mInvertAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            apvts, p + "Invert", mInvert);

        mP2Ratio = std::make_unique<LabeledKnob>(apvts, p + "P2Ratio", "Sweep 2");
        addChildComponent(*mP2Ratio); // bi-phase only (Sweep Gen 2 rate ratio)
        mSeries.setButtonText("Series");
        addChildComponent(mSeries); // bi-phase only (series/parallel routing)
        mSeriesAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            apvts, p + "Series", mSeries);

        // Every knob in the lane reads 0..10 by rotation (pedal-style), so the
        // mixed underlying params (Speed in Hz, the rest 0..1, Sweep 2 a ratio)
        // all show on one consistent scale instead of exposing raw units.
        for (LabeledKnob *k : {mRate.get(), mDepth.get(), mFeedback.get(), mMix.get(),
                               mWidth.get(), mDrive.get(), mManual.get(), mP2Ratio.get(),
                               mHornDrum.get()})
            k->setRotationReadout(10.0);

        refresh();
    }

    // Momentary solo: reports clicks to the owner; the owner pushes the live state
    // back so the button reflects it after an editor reopen / external change.
    std::function<void(int, bool)> onSolo;
    void setSoloState(bool on) { mSolo.setToggleState(on, juce::dontSendNotification); }

    // Write the filtered type combo's selection (item id = enum + 1) back to the
    // full 9-choice Type parameter.
    void syncTypeParam()
    {
        const int id = mType.getSelectedId();
        if (id <= 0)
            return;
        if (auto *prm = mApvts.getParameter(mPrefix + "Type"))
        {
            const float norm = prm->convertTo0to1((float)(id - 1));
            if (std::abs(prm->getValue() - norm) > 1.0e-6f)
            {
                prm->beginChangeGesture();
                prm->setValueNotifyingHost(norm);
                prm->endChangeGesture();
            }
        }
        refresh();
    }

    void refresh()
    {
        const juce::String p = mPrefix;
        const int type = (int)mApvts.getRawParameterValue(p + "Type")->load();
        if (mType.getSelectedId() != type + 1) // keep the filtered combo in sync
            mType.setSelectedId(type + 1, juce::dontSendNotification);
        const int sync = (int)mApvts.getRawParameterValue(p + "Sync")->load();
        const bool on = mApvts.getRawParameterValue(p + "On")->load() >= 0.5f;
        mIcon.setType(type);
        mIcon.setActive(on);
        if (type == mLastType && sync == mLastSync && on == mLastOn)
            return;
        mLastType = type;
        mLastSync = sync;
        mLastOn = on;
        const bool rotary = (type == 5);
        mDepth->setVisible(type != 2);                 // phaser: no depth knob
        mFeedback->setVisible(type == 1 || type == 2 || type == 8); // flanger/phaser/bi-phase
        mMix->setVisible(nam_rig::ModVoice::mixExposed((nam_rig::ModVoice::Type)type)); // chorus + flanger
        mWave.setVisible(type == 3);                   // tremolo shape
        mRate->setVisible(!rotary);                    // rotary: slow/fast toggle, not a rate
        if (!rotary) // knob ends exactly at this effect's rate ceiling (no dead travel past the internal cap)
        {
            mRate->slider().setNormalisableRange(
                {0.03, (double)nam_rig::ModVoice::maxRateHz((nam_rig::ModVoice::Type)type), 0.01, 0.35});
            mRate->updateReadout(); // re-evaluate the 0..10 text against the new range
        }
        mSync.setVisible(!rotary);
        mDrive->setVisible(rotary);                    // rotary: Leslie tube drive
        mHornDrum->setVisible(rotary);                 // rotary: horn<->drum balance
        mRotFast.setVisible(rotary);
        mManual->setVisible(type == 1);                // flanger: static comb position
        mInvert.setVisible(type == 1);                 // flanger: phase invert
        // Per-effect knob naming. M-126 flanger: the sweep-amount knob is "Sweep"
        // (the M-126 "Width" term reads as the stereo control here, so call the
        // sweep "Sweep") and the stereo knob is "Spread" (so there aren't two
        // "Width"s). Uni-Vibe uses the authentic vibe terms: Rate -> "Speed",
        // Depth -> "Intensity". Rotary: Depth drives the swirl intensity (doppler
        // + directional pulse + drum throb) -> labelled "Wom" after the Leslie's
        // directional amplitude pulse.
        const bool flanger = (type == 1);
        const bool uniVibe = (type == 6);
        mRate->setCaption(uniVibe ? "Speed" : "Rate");
        mDepth->setCaption(flanger ? "Sweep" : uniVibe ? "Intensity" : rotary ? "Wom" : "Depth");
        mWidth->setCaption(flanger ? "Spread" : "Width");
        mP2Ratio->setVisible(type == 8);               // bi-phase: Sweep Gen 2 ratio
        mSeries.setVisible(type == 8);                  // bi-phase: series/parallel
        mRate->setEnabled(sync == 0);                  // rate greyed when synced
        repaint();                                     // LED
        resized();
    }

    void paint(juce::Graphics &g) override
    {
        auto b = getLocalBounds().toFloat().reduced(0.5f);
        g.setColour(colors::panel);
        g.fillRoundedRectangle(b, 8.0f);
        g.setColour(colors::outline);
        g.drawRoundedRectangle(b, 8.0f, 1.0f);
        g.setColour(mLastOn ? colors::accent : colors::ledOff);
        g.fillEllipse(mLedX - 3.5f, mLedY - 3.5f, 7.0f, 7.0f);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(9, 7);
        auto numCol = area.removeFromLeft(16);
        mNum.setBounds(numCol.removeFromTop(numCol.getHeight() / 2));
        mLedX = (float)numCol.getCentreX();
        mLedY = (float)numCol.getCentreY();
        area.removeFromLeft(6);

        auto iconCol = area.removeFromLeft(62);
        mIcon.setBounds(iconCol.withSizeKeepingCentre(62, juce::jmin(iconCol.getHeight(), 46)));
        area.removeFromLeft(10);

        auto meta = area.removeFromLeft(112).withSizeKeepingCentre(112, juce::jmin(area.getHeight(), 50));
        mType.setBounds(meta.removeFromTop(24));
        meta.removeFromTop(4);
        auto bottomMeta = meta.removeFromTop(22);
        mSync.setBounds(bottomMeta.withWidth(88));         // non-rotary
        mRotFast.setBounds(bottomMeta.removeFromLeft(54)); // rotary: slow/fast
        area.removeFromLeft(10);

        auto onCol = area.removeFromRight(44);
        mOn.setBounds(onCol.withSizeKeepingCentre(44, 22));
        area.removeFromRight(6);
        if (mSoloSlot >= 0) // front slots only
        {
            mSolo.setBounds(area.removeFromRight(30).withSizeKeepingCentre(30, 22));
            area.removeFromRight(8);
        }
        if (mInvert.isVisible())
        {
            mInvert.setBounds(area.removeFromRight(50).withSizeKeepingCentre(50, 22));
            area.removeFromRight(8);
        }
        if (mSeries.isVisible())
        {
            mSeries.setBounds(area.removeFromRight(64).withSizeKeepingCentre(64, 22));
            area.removeFromRight(8);
        }
        if (mWave.isVisible())
        {
            mWave.setBounds(area.removeFromRight(84).withSizeKeepingCentre(84, 24));
            area.removeFromRight(8);
        }

        std::vector<juce::Component *> vis;
        for (juce::Component *k : {(juce::Component *)mRate.get(), (juce::Component *)mDepth.get(),
                                   (juce::Component *)mFeedback.get(), (juce::Component *)mP2Ratio.get(),
                                   (juce::Component *)mManual.get(), (juce::Component *)mMix.get(),
                                   (juce::Component *)mWidth.get(), (juce::Component *)mDrive.get(),
                                   (juce::Component *)mHornDrum.get()})
            if (k->isVisible())
                vis.push_back(k);
        const int nk = (int)vis.size();
        if (nk > 0)
        {
            const int w = juce::jmin(62, area.getWidth() / nk);
            auto row = area.withSizeKeepingCentre(w * nk, juce::jmin(area.getHeight(), 62));
            for (auto *k : vis)
                k->setBounds(row.removeFromLeft(w).reduced(4, 0));
        }
    }

private:
    juce::AudioProcessorValueTreeState &mApvts;
    juce::String mPrefix;
    int mSoloSlot = -1; // 0-based front slot for solo; -1 = post lane (no solo)
    int mLastType = -1, mLastSync = -1;
    bool mLastOn = true;
    float mLedX = 0.0f, mLedY = 0.0f;
    juce::Label mNum;
    ModFxIcon mIcon;
    juce::ComboBox mType, mWave, mSync;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> mWaveAtt, mSyncAtt; // (Type combo synced by hand)
    juce::ToggleButton mOn;
    juce::ToggleButton mSolo; // momentary dial-in (not APVTS-attached)
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> mOnAtt;
    std::unique_ptr<LabeledKnob> mRate, mDepth, mFeedback, mMix, mWidth, mDrive, mManual, mP2Ratio, mHornDrum;
    juce::ToggleButton mRotFast, mInvert, mSeries;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> mRotFastAtt, mInvertAtt, mSeriesAtt;
};

// Draggable blend pad for PARALLEL routing. The puck sets the slot weights
// (barycentric, same geometry as ModBlock::padWeights). Backed by two hidden
// sliders attached to modPadX/modPadY, so host automation and saved state work
// for free: dragging writes the params; external moves repaint the puck.
//
// The geometry follows the ENABLED slots: 3 on = triangle (node1 top-centre /
// node2 bottom-left / node3 bottom-right); 2 on = a line crossfading those two;
// 1 on = a point. The puck is always CONSTRAINED to that geometry, so it can't
// wander into dead square corners.
class BlendPad : public juce::Component, private juce::Slider::Listener
{
public:
    explicit BlendPad(juce::AudioProcessorValueTreeState &apvts)
    {
        for (auto *s : {&mX, &mY})
        {
            s->setRange(0.0, 1.0, 0.0);
            addChildComponent(*s); // hidden: the pad is the visible control
            s->addListener(this);
        }
        mXAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "modPadX", mX);
        mYAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, "modPadY", mY);
    }

    // Which slots are enabled -> drives triangle/line/point geometry. Re-projects
    // the puck onto the new shape when the set changes (so it never sits off it).
    // When a slot is DISABLED the puck position is remembered; if the slot comes
    // back before the puck is dragged, that original position is restored.
    void setActiveSlots(bool a0, bool a1, bool a2)
    {
        if (a0 == mActive[0] && a1 == mActive[1] && a2 == mActive[2])
            return;
        const int oldN = (mActive[0] ? 1 : 0) + (mActive[1] ? 1 : 0) + (mActive[2] ? 1 : 0);
        const int newN = (a0 ? 1 : 0) + (a1 ? 1 : 0) + (a2 ? 1 : 0);
        mActive[0] = a0;
        mActive[1] = a1;
        mActive[2] = a2;

        float px = (float)mX.getValue(), py = (float)mY.getValue();
        if (newN < oldN) // a slot was disabled -> remember the puck to restore later
        {
            if (!mHasSaved) { mSavedX = px; mSavedY = py; mSavedCount = oldN; mHasSaved = true; }
        }
        else if (newN > oldN && mHasSaved && newN >= mSavedCount) // back, puck not moved -> restore
        {
            px = mSavedX;
            py = mSavedY;
            mHasSaved = false;
        }
        const auto p = constrain(px, py); // clamp onto the (new) shape
        setPuck(p.x, p.y);
        repaint();
    }

    void paint(juce::Graphics &g) override
    {
        const auto r = padRect();
        int act[3], n = 0;
        for (int i = 0; i < 3; ++i)
            if (mActive[i]) act[n++] = i;

        if (n >= 3 || n == 0) // triangle (n==0 shouldn't happen; show the full shape)
        {
            juce::Path tri;
            tri.startNewSubPath(scr(r, 0));
            tri.lineTo(scr(r, 1));
            tri.lineTo(scr(r, 2));
            tri.closeSubPath();
            g.setColour(colors::panel.brighter(0.06f));
            g.fillPath(tri);
            g.setColour(colors::outline);
            g.strokePath(tri, juce::PathStrokeType(1.2f));
        }
        else if (n == 2) // crossfade line between the two enabled nodes
        {
            g.setColour(colors::outline);
            g.drawLine(juce::Line<float>(scr(r, act[0]), scr(r, act[1])), 1.4f);
        }

        g.setFont(RigLookAndFeel::withHeight(10.0f));
        for (int i = 0; i < 3; ++i)
        {
            const auto p = scr(r, i);
            g.setColour(mActive[i] ? colors::accentDim : colors::outline);
            g.fillEllipse(p.x - 3.0f, p.y - 3.0f, 6.0f, 6.0f);
            g.setColour(mActive[i] ? colors::textDim : colors::outline);
            const float ly = (i == 0) ? p.y - 14.0f : p.y + 3.0f;
            g.drawText(juce::String(i + 1), (int)(p.x - 8.0f), (int)ly, 16, 11, juce::Justification::centred);
        }

        const auto pk = scr(r, (float)mX.getValue(), (float)mY.getValue());
        g.setColour(colors::accent);
        g.fillEllipse(pk.x - 5.0f, pk.y - 5.0f, 10.0f, 10.0f);
        g.setColour(colors::panel);
        g.drawEllipse(pk.x - 5.0f, pk.y - 5.0f, 10.0f, 10.0f, 1.5f);
    }

    void mouseDown(const juce::MouseEvent &e) override { drag(e); }
    void mouseDrag(const juce::MouseEvent &e) override { drag(e); }

private:
    void sliderValueChanged(juce::Slider *) override
    {
        // An EXTERNAL move (host automation / preset load) invalidates the saved
        // restore position; our own moves set mInternalSet and are exempt.
        if (!mInternalSet)
            mHasSaved = false;
        repaint();
    }
    void setPuck(float x, float y) // write both params as an internal move
    {
        mInternalSet = true;
        mX.setValue(x, juce::sendNotificationSync);
        mY.setValue(y, juce::sendNotificationSync);
        mInternalSet = false;
    }

    // Node positions in (padX, padY) parameter space (matches ModBlock::padWeights).
    static juce::Point<float> node(int i)
    {
        if (i == 0) return {0.5f, 1.0f};
        if (i == 1) return {0.0f, 0.0f};
        return {1.0f, 0.0f};
    }
    juce::Rectangle<float> padRect() const
    {
        auto b = getLocalBounds().toFloat().reduced(12.0f);
        const float s = juce::jmin(b.getWidth(), b.getHeight());
        return juce::Rectangle<float>(s, s).withCentre(b.getCentre());
    }
    juce::Point<float> scr(juce::Rectangle<float> r, float x, float y) const
    {
        return {r.getX() + x * r.getWidth(), r.getY() + (1.0f - y) * r.getHeight()};
    }
    juce::Point<float> scr(juce::Rectangle<float> r, int nodeIdx) const
    {
        const auto v = node(nodeIdx);
        return scr(r, v.x, v.y);
    }
    // Project a raw (0..1) point onto the active geometry (triangle/line/point).
    juce::Point<float> constrain(float x, float y) const
    {
        int act[3], n = 0;
        for (int i = 0; i < 3; ++i)
            if (mActive[i]) act[n++] = i;
        if (n >= 3 || n == 0)
        {
            float w[3];
            nam_rig::ModBlock::padWeights(x, y, w); // clamp + renormalise into the triangle
            return {0.5f * w[0] + w[2], w[0]};      // barycentric point back to (x,y)
        }
        if (n == 1)
            return node(act[0]);
        const auto A = node(act[0]), B = node(act[1]); // n == 2: project onto the segment
        const auto AB = B - A;
        const float denom = AB.x * AB.x + AB.y * AB.y;
        float t = denom > 1.0e-9f ? ((x - A.x) * AB.x + (y - A.y) * AB.y) / denom : 0.0f;
        t = juce::jlimit(0.0f, 1.0f, t);
        return {A.x + t * AB.x, A.y + t * AB.y};
    }
    void drag(const juce::MouseEvent &e)
    {
        const auto r = padRect();
        const float rx = juce::jlimit(0.0f, 1.0f, (e.position.x - r.getX()) / r.getWidth());
        const float ry = juce::jlimit(0.0f, 1.0f, 1.0f - (e.position.y - r.getY()) / r.getHeight());
        const auto p = constrain(rx, ry);
        mHasSaved = false; // user moved the puck -> don't restore an old position
        setPuck(p.x, p.y);
    }

    juce::Slider mX, mY;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mXAtt, mYAtt;
    bool mActive[3] = {true, true, true};
    // Remember the puck when a slot is disabled, to restore it if the slot comes
    // back before the puck is moved.
    float mSavedX = 0.5f, mSavedY = 1.0f / 3.0f;
    int mSavedCount = 0;
    bool mHasSaved = false;
    bool mInternalSet = false; // true while WE move the puck (vs host/preset)
};

class ModPanel : public BlockPanel
{
public:
    explicit ModPanel(juce::AudioProcessorValueTreeState &apvts)
        : BlockPanel("MODULATION"), mApvts(apvts)
    {
        for (int s = 0; s < nam_rig::ModBlock::kSlots; ++s)
        {
            mLanes[(size_t)s] = std::make_unique<ModSlotLane>(
                apvts, "mod" + juce::String(s + 1), juce::String(s + 1), s);
            addAndMakeVisible(*mLanes[(size_t)s]);
            mLanes[(size_t)s]->onSolo = [this](int slot, bool on) {
                if (!onSetSolo) return;
                // Exclusive solo: turning one on clears the others (radio style);
                // clicking the lit one again clears it. Audio only, blend untouched.
                if (on)
                    for (int k = 0; k < nam_rig::ModBlock::kSlots; ++k)
                        onSetSolo(k, k == slot);
                else
                    onSetSolo(slot, false);
                if (getSolo)
                    for (int k = 0; k < nam_rig::ModBlock::kSlots; ++k)
                        if (mLanes[(size_t)k]) mLanes[(size_t)k]->setSoloState(getSolo(k));
            };
        }
        // Dedicated POST lane: runs at the END of the section (rotary/tremolo/
        // harm-trem). No solo; only the post effects are selectable.
        mPostLane = std::make_unique<ModSlotLane>(apvts, "post", "P", -1);
        addAndMakeVisible(*mPostLane);

        mRouting.addItemList({"Series", "Parallel"}, 1);
        addAndMakeVisible(mRouting);
        mRoutingAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            apvts, "modRouting", mRouting);
        mRouting.onChange = [this] { refreshRouting(); };

        mPad = std::make_unique<BlendPad>(apvts);
        addChildComponent(*mPad); // parallel only

        mModMix = std::make_unique<LabeledKnob>(apvts, "modMix", "Mix");
        mModMix->setRotationReadout(10.0);
        addChildComponent(*mModMix); // parallel only

        refreshRouting();
    }

    // Editor wires these to the processor's momentary solo (not an APVTS param).
    std::function<void(int, bool)> onSetSolo; // -> processor.setModSolo
    std::function<bool(int)> getSolo;         // -> processor.getModSolo

    void refresh()
    {
        for (int s = 0; s < nam_rig::ModBlock::kSlots; ++s)
            if (mLanes[(size_t)s])
            {
                mLanes[(size_t)s]->refresh();
                if (getSolo) mLanes[(size_t)s]->setSoloState(getSolo(s)); // reflect live solo
            }
        if (mPostLane) mPostLane->refresh();
        updatePadActive();
        refreshRouting();
    }

    // Pad geometry follows the ENABLED (On) slots only. Solo is momentary and
    // must NOT move the puck / rewrite the blend, so it is deliberately excluded
    // here -- soloing leaves the blend exactly as the user set it.
    void updatePadActive()
    {
        auto enabled = [&](int s) {
            return mApvts.getRawParameterValue("mod" + juce::String(s + 1) + "On")->load() >= 0.5f;
        };
        mPad->setActiveSlots(enabled(0), enabled(1), enabled(2));
    }

    // Show/hide the parallel-only controls (pad + Mod Mix) and relayout when the
    // Series/Parallel routing changes (from the toggle or host automation).
    void refreshRouting()
    {
        const bool par = (mRouting.getSelectedId() == 2);
        if (par == mParallel)
            return;
        mParallel = par;
        mPad->setVisible(par);
        mModMix->setVisible(par);
        resized();
        repaint();
    }

    void paint(juce::Graphics &g) override
    {
        BlockPanel::paint(g); // panel body + "MODULATION" title
        if (mSpine.getHeight() <= 0)
            return;
        const float x = (float)mSpine.getCentreX();
        const float top = (float)mSpine.getY();
        const float bot = mPostLaneY - 7.0f; // line ends just above the OUT label
        g.setColour(colors::outline);
        g.drawLine(x, top, x, bot, 1.5f);
        g.setColour(colors::accentDim);
        if (mParallel)
        {
            // Parallel: the input bus feeds each lane in parallel (right-pointing
            // triangles, matching the series ones).
            for (float yC : mLaneCenters)
            {
                juce::Path tri;
                tri.addTriangle(x - 2.0f, yC - 3.0f, x - 2.0f, yC + 3.0f, x + 4.0f, yC);
                g.fillPath(tri);
            }
        }
        else
        {
            // Series: signal flows lane to lane (down-arrows between slots).
            for (float fy : mArrowYs)
            {
                juce::Path tri;
                tri.addTriangle(x - 3.0f, fy - 2.0f, x + 3.0f, fy - 2.0f, x, fy + 3.0f);
                g.fillPath(tri);
            }
        }
        g.setColour(colors::textDim);
        g.setFont(RigLookAndFeel::withHeight(9.0f));
        g.drawText("IN", mSpine.getX() - 3, mSpine.getY() - 13, 26, 11, juce::Justification::centred);
        g.drawText("OUT", mSpine.getX() - 3, (int)mPostLaneY - 5, 26, 11, juce::Justification::centred);
    }

    void resized() override
    {
        auto area = contentArea();
        // right control strip: routing toggle always; pad + Mod Mix in parallel.
        auto strip = area.removeFromRight(108);
        area.removeFromRight(8);
        mRouting.setBounds(strip.removeFromTop(24));
        strip.removeFromTop(8);
        if (mParallel)
        {
            mModMix->setBounds(strip.removeFromTop(62).reduced(20, 0));
            strip.removeFromTop(6);
            const int sq = juce::jmin(strip.getWidth(), strip.getHeight());
            mPad->setBounds(strip.removeFromTop(sq).withSizeKeepingCentre(sq, sq));
        }

        const int n = nam_rig::ModBlock::kSlots, gap = 8;
        // Spine column spans the FULL height: IN at the top, down through the
        // slots, to the OUT (post) stage at the bottom.
        mSpine = area.removeFromLeft(20).withTrimmedTop(14).withTrimmedBottom(16);
        area.removeFromLeft(4);
        // Reserve the bottom for the dedicated POST lane, under an "OUT" divider.
        auto postRegion = area.removeFromBottom(area.getHeight() / 4);
        mPostDivider = postRegion.removeFromTop(16);
        mPostLaneY = (float)postRegion.getCentreY(); // OUT label sits next to the post lane
        area.removeFromBottom(gap);
        if (mPostLane) mPostLane->setBounds(postRegion.reduced(0, 2));
        // Front lanes fill the rest.
        const int laneH = (area.getHeight() - gap * (n - 1)) / n;
        mArrowYs.clear();
        for (int s = 0; s < n; ++s)
        {
            auto lane = area.removeFromTop(laneH);
            mLanes[(size_t)s]->setBounds(lane);
            mLaneCenters[(size_t)s] = (float)lane.getCentreY(); // for the parallel spine taps
            if (s < n - 1)
            {
                mArrowYs.push_back((float)area.removeFromTop(gap).getCentreY());
            }
        }
    }

private:
    std::array<std::unique_ptr<ModSlotLane>, (size_t)nam_rig::ModBlock::kSlots> mLanes;
    std::unique_ptr<ModSlotLane> mPostLane;
    juce::Rectangle<int> mSpine, mPostDivider;
    std::vector<float> mArrowYs;
    std::array<float, (size_t)nam_rig::ModBlock::kSlots> mLaneCenters{}; // parallel spine taps
    float mPostLaneY = 0.0f;                                             // OUT label / line end
    juce::ComboBox mRouting;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> mRoutingAtt;
    std::unique_ptr<BlendPad> mPad;
    std::unique_ptr<LabeledKnob> mModMix;
    juce::AudioProcessorValueTreeState &mApvts;
    bool mParallel = false;
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
        mMatchBtn.onClick = [this] { mProc.matchLevels(); };
        addAndMakeVisible(mMatchBtn);
        mHint.setColour(juce::Label::textColourId, colors::textDim);
        mHint.setText("Auto-align matches timing/polarity; Match Levels matches loudness.",
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
        // Auto-align + Match Levels need a model in BOTH rigs.
        const bool bothLoaded = mProc.isModelLoaded(0) && mProc.isModelLoaded(1);
        mAutoBtn.setEnabled(bothLoaded);
        mMatchBtn.setEnabled(bothLoaded);
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
        alignRow.removeFromLeft(8);
        mMatchBtn.setBounds(alignRow.removeFromLeft(120).withSizeKeepingCentre(120, 28));
        alignRow.removeFromLeft(10);
        mHint.setBounds(alignRow.withSizeKeepingCentre(alignRow.getWidth(), 40));
    }

private:
    NamRigProcessor &mProc;
    juce::ComboBox mMode;
    juce::Label mModeLabel, mALabel, mBLabel, mHint;
    std::unique_ptr<LabeledKnob> mLevelA, mPanA, mLevelB, mPanB, mAlign;
    juce::ToggleButton mPolA, mPolB;
    juce::TextButton mAutoBtn{"Auto-align"}, mMatchBtn{"Match Levels"};
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> mModeAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> mPolAAtt, mPolBAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixPanel)
};

} // namespace nam_rig::ui
