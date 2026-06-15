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
    void setCaption(const juce::String &c) { mLabel.setText(c, juce::dontSendNotification); }

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
// A round metal footswitch (toggles the pedal On/Off; ring lights amber on).
class Footswitch : public juce::Button
{
public:
    Footswitch() : juce::Button({}) { setClickingTogglesState(true); }
    void paintButton(juce::Graphics &g, bool, bool down) override
    {
        auto bb = getLocalBounds().toFloat();
        const float dd = juce::jmin(bb.getWidth(), bb.getHeight());
        auto c = juce::Rectangle<float>(dd, dd).withCentre(bb.getCentre()).reduced(2.0f);
        g.setColour(getToggleState() ? colors::accent : colors::ledOff); // ring
        g.fillEllipse(c);
        auto cap = c.reduced(3.0f);
        juce::ColourGradient grad(colors::text.withAlpha(0.80f), cap.getX(), cap.getY(),
                                  colors::outline, cap.getX(), cap.getBottom(), false);
        g.setGradientFill(grad);
        g.fillEllipse(cap);
        g.setColour(colors::bg.withAlpha(down ? 0.30f : 0.14f)); // dimple
        g.fillEllipse(cap.reduced(cap.getWidth() * 0.30f));
    }
};

// One drive "stomp" in the pedalboard. Top-to-bottom: Type (category), a model
// selector (when the category has several models), the model's descriptive
// subtitle, the authentic knobs, the model's mode switch (treble-boost Range),
// then the footswitch. Only the controls that pedal actually has are shown.
class DrivePedal : public juce::Component
{
public:
    DrivePedal(juce::AudioProcessorValueTreeState &apvts, int slot)
        : mApvts(apvts), mSlot(slot)
    {
        const juce::String p = "drv" + juce::String(slot + 1);

        mType.addItemList({"Off", "Boost", "Overdrive", "Distortion", "Fuzz"}, 1);
        mType.setJustificationType(juce::Justification::centred);
        mTypeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            apvts, p + "Type", mType);
        mType.onChange = [this] { refresh(); };
        addAndMakeVisible(mType);

        mModel.setJustificationType(juce::Justification::centred);
        mModel.onChange = [this] { onModelPicked(); }; // manual sync (items are dynamic per category)
        addChildComponent(mModel);

        mSub.setJustificationType(juce::Justification::centred);
        mSub.setColour(juce::Label::textColourId, colors::textDim);
        mSub.setFont(RigLookAndFeel::withHeight(10.5f));
        addAndMakeVisible(mSub);

        mRange.addItemList({"Treble", "Mid", "Full"}, 1);
        mRange.setJustificationType(juce::Justification::centred);
        mRangeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            apvts, p + "Range", mRange);
        addChildComponent(mRange);

        mOnAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            apvts, p + "On", mOn);
        mOn.onClick = [this] { refresh(); };
        addChildComponent(mOn);

        mDrive = std::make_unique<LabeledKnob>(apvts, p + "Drive", "Drive");
        mTone  = std::make_unique<LabeledKnob>(apvts, p + "Tone", "Tone");
        mLevel = std::make_unique<LabeledKnob>(apvts, p + "Level", "Level");
        addAndMakeVisible(*mDrive);
        addAndMakeVisible(*mTone);
        addAndMakeVisible(*mLevel);

        refresh();
    }

    void refresh()
    {
        const int type = mType.getSelectedItemIndex();
        const juce::String pid = "drv" + juce::String(mSlot + 1);
        const bool on = mApvts.getRawParameterValue(pid + "On")->load() >= 0.5f;
        if (type != mLastType)
            populateModels(type);
        int model = (int)mApvts.getRawParameterValue(pid + "Model")->load();
        const int count = nam_rig::DriveBlock::modelCount((nam_rig::DriveBlock::Kind)type);
        if (count > 0 && model >= count) { model = 0; setModelParam(0); }
        if (mModel.getSelectedItemIndex() != model)
            mModel.setSelectedItemIndex(juce::jmax(0, model), juce::dontSendNotification);
        mActive = (type != 0) && on;
        if (type == mLastType && on == mLastOn && model == mLastModel) { repaint(); return; }
        mLastType = type; mLastOn = on; mLastModel = model;
        configure();
        resized();
        repaint();
    }

    void paint(juce::Graphics &g) override
    {
        auto b = getLocalBounds().toFloat().reduced(1.0f);
        juce::ColourGradient eg(colors::tile.brighter(0.05f), 0.0f, b.getY(),
                                colors::tile.darker(0.18f), 0.0f, b.getBottom(), false);
        g.setGradientFill(eg);
        g.fillRoundedRectangle(b, 10.0f);
        g.setColour(mActive ? colors::accent : colors::outline);
        g.drawRoundedRectangle(b, 10.0f, mActive ? 1.6f : 1.1f);
        auto screw = [&](float x, float y) {
            g.setColour(colors::outline.brighter(0.35f));
            g.fillEllipse(x - 3.0f, y - 3.0f, 6.0f, 6.0f);
            g.setColour(colors::bg);
            g.drawLine(x - 2.0f, y - 1.5f, x + 2.0f, y + 1.5f, 1.0f);
        };
        screw(12.0f, 12.0f); screw((float)getWidth() - 12.0f, 12.0f);
        screw(12.0f, (float)getHeight() - 12.0f); screw((float)getWidth() - 12.0f, (float)getHeight() - 12.0f);
        g.setColour(mActive ? colors::accent : colors::ledOff);
        g.fillEllipse((float)getWidth() - 34.0f, 16.0f, 7.0f, 7.0f); // status LED
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(10, 9);
        mType.setBounds(area.removeFromTop(24));
        if (mModel.isVisible())
        {
            area.removeFromTop(5);
            mModel.setBounds(area.removeFromTop(24));
        }
        area.removeFromTop(3);
        mSub.setBounds(area.removeFromTop(13));
        if (mOn.isVisible())
        {
            auto bot = area.removeFromBottom(46);
            mOn.setBounds(bot.withSizeKeepingCentre(42, 42));
            area.removeFromBottom(2);
        }
        if (mRange.isVisible())
        {
            auto rr = area.removeFromBottom(24);
            mRange.setBounds(rr.withSizeKeepingCentre(juce::jmin(rr.getWidth(), 150), 22));
            area.removeFromBottom(6);
        }
        area.removeFromTop(6);
        LabeledKnob *ks[3] = {mDrive.get(), mTone.get(), mLevel.get()};
        int nVis = 0;
        for (auto *k : ks) if (k->isVisible()) ++nVis;
        if (nVis == 0) return;
        const int kw = juce::jmin(86, juce::jmax(1, area.getWidth() / nVis));
        auto grp = area.withSizeKeepingCentre(kw * nVis, juce::jmin(area.getHeight(), 104));
        for (auto *k : ks)
            if (k->isVisible())
                k->setBounds(grp.removeFromLeft(kw).reduced(4, 0));
    }

private:
    void populateModels(int type)
    {
        const auto cat = (nam_rig::DriveBlock::Kind)type;
        const int n = nam_rig::DriveBlock::modelCount(cat);
        mModel.clear(juce::dontSendNotification);
        for (int i = 0; i < n; ++i)
            mModel.addItem(nam_rig::DriveBlock::modelName(cat, i), i + 1);
        mModel.setVisible(type != 0 && n > 1); // only when there is a real choice
    }
    void onModelPicked()
    {
        setModelParam(mModel.getSelectedItemIndex());
        refresh();
    }
    void setModelParam(int idx)
    {
        if (auto *prm = mApvts.getParameter("drv" + juce::String(mSlot + 1) + "Model"))
            prm->setValueNotifyingHost(prm->convertTo0to1((float)juce::jmax(0, idx)));
    }
    void configure()
    {
        const int type = mType.getSelectedItemIndex();
        const int model = juce::jmax(0, mModel.getSelectedItemIndex());
        const auto cat = (nam_rig::DriveBlock::Kind)type;
        const char *d = "", *t = "", *l = "";
        switch (type)
        {
        case 1: d = "Boost"; break;
        case 2: d = "Drive";      t = "Tone";   l = "Level";  break;
        case 3: d = "Distortion"; t = "Filter"; l = "Volume"; break;
        case 4: d = "Fuzz";                     l = "Volume"; break;
        default: break;
        }
        mSub.setText(type == 0 ? juce::String("select a pedal")
                               : juce::String(nam_rig::DriveBlock::modelSub(cat, model)),
                     juce::dontSendNotification);
        mDrive->setCaption(d); mDrive->setVisible(*d != '\0');
        mTone->setCaption(t);  mTone->setVisible(*t != '\0');
        mLevel->setCaption(l); mLevel->setVisible(*l != '\0');
        mRange.setVisible(nam_rig::DriveBlock::modelHasRange(cat, model));
        mOn.setVisible(type != 0);
    }

    juce::AudioProcessorValueTreeState &mApvts;
    int mSlot;
    juce::Label mSub;
    juce::ComboBox mType, mModel, mRange;
    Footswitch mOn;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> mTypeAtt, mRangeAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> mOnAtt;
    std::unique_ptr<LabeledKnob> mDrive, mTone, mLevel;
    int mLastType = -1, mLastModel = -1;
    bool mLastOn = true, mActive = false;
};

//==============================================================================
// DrivePanel - the drive section as a left-to-right PEDALBOARD: three stomps in
// series (IN left, OUT right) joined by patch cables, contrasting with the
// rack-style (top-down) amp + mod sections.
class DrivePanel : public BlockPanel
{
public:
    explicit DrivePanel(juce::AudioProcessorValueTreeState &apvts)
        : BlockPanel("DRIVE PEDALBOARD")
    {
        for (int s = 0; s < nam_rig::DriveBlock::kSlots; ++s)
        {
            mPedals[(size_t)s] = std::make_unique<DrivePedal>(apvts, s);
            addAndMakeVisible(*mPedals[(size_t)s]);
        }
        mAutoGain.setButtonText("Auto Gain");
        addAndMakeVisible(mAutoGain);
        mAutoGainAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            apvts, "driveAutoGain", mAutoGain);
    }

    void refresh()
    {
        for (auto &p : mPedals)
            if (p) p->refresh();
    }

    void paint(juce::Graphics &g) override
    {
        BlockPanel::paint(g);
        g.setColour(colors::text);
        g.setFont(RigLookAndFeel::withHeight(12.0f).boldened());
        g.drawText("IN", (int)mInX - 44, (int)mFlowY - 9, 28, 18, juce::Justification::centredRight);
        g.drawText("OUT", (int)mOutX + 18, (int)mFlowY - 9, 32, 18, juce::Justification::centredLeft);
        g.setColour(colors::accentDim);
        for (auto &c : mCables)
            g.drawLine(c.getStartX(), c.getStartY(), c.getEndX(), c.getEndY(), 2.0f);
    }

    void resized() override
    {
        auto area = contentArea();
        auto bottom = area.removeFromBottom(22);
        mAutoGain.setBounds(bottom.removeFromRight(110));

        const int inM = 52, outM = 58, cable = 22;
        area.removeFromLeft(inM);
        area.removeFromRight(outM);
        const int n = nam_rig::DriveBlock::kSlots;
        const int pedalW = juce::jmax(1, (area.getWidth() - cable * (n - 1)) / n);
        mFlowY = (float)area.getCentreY();
        mInX = (float)area.getX();
        mCables.clear();
        for (int s = 0; s < n; ++s)
        {
            mPedals[(size_t)s]->setBounds(area.removeFromLeft(pedalW));
            if (s < n - 1)
            {
                auto gap = area.removeFromLeft(cable);
                mCables.push_back({{(float)gap.getX(), mFlowY}, {(float)gap.getRight(), mFlowY}});
            }
        }
        mOutX = (float)area.getRight();
        mCables.push_back({{mInX - 14.0f, mFlowY}, {mInX, mFlowY}});   // IN stub
        mCables.push_back({{mOutX, mFlowY}, {mOutX + 14.0f, mFlowY}}); // OUT stub
    }

private:
    std::array<std::unique_ptr<DrivePedal>, (size_t)nam_rig::DriveBlock::kSlots> mPedals;
    juce::ToggleButton mAutoGain;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> mAutoGainAtt;
    std::vector<juce::Line<float>> mCables;
    float mFlowY = 0.0f, mInX = 0.0f, mOutX = 0.0f;
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
    ModSlotLane(juce::AudioProcessorValueTreeState &apvts, int slot)
        : mApvts(apvts), mSlot(slot)
    {
        const juce::String p = "mod" + juce::String(slot + 1);

        mNum.setText(juce::String(slot + 1), juce::dontSendNotification);
        mNum.setJustificationType(juce::Justification::centred);
        mNum.setColour(juce::Label::textColourId, colors::textDim);
        addAndMakeVisible(mNum);
        addAndMakeVisible(mIcon);

        mType.addItemList({"Chorus", "Flanger", "Phaser", "Tremolo",
                           "Vibrato", "Rotary", "Uni-Vibe", "Harm Trem"}, 1);
        addAndMakeVisible(mType);
        mTypeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            apvts, p + "Type", mType);
        mType.onChange = [this] { refresh(); };

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
        mRotFast.setButtonText("Fast");
        addChildComponent(mRotFast); // rotary only (slow/fast rotor)
        mRotFastAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            apvts, p + "RotFast", mRotFast);

        refresh();
    }

    void refresh()
    {
        const juce::String p = "mod" + juce::String(mSlot + 1);
        const int type = (int)mApvts.getRawParameterValue(p + "Type")->load();
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
        mFeedback->setVisible(type == 1 || type == 2); // flanger/phaser
        mMix->setVisible(nam_rig::ModVoice::mixExposed((nam_rig::ModVoice::Type)type)); // chorus only
        mWave.setVisible(type == 3);                   // tremolo shape
        mRate->setVisible(!rotary);                    // rotary: slow/fast toggle, not a rate
        mSync.setVisible(!rotary);
        mDrive->setVisible(rotary);                    // rotary: Leslie tube drive
        mRotFast.setVisible(rotary);
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
        mSync.setBounds(bottomMeta.withWidth(88));   // shown for non-rotary
        mRotFast.setBounds(bottomMeta.withWidth(72)); // shown for rotary (overlaps; only one visible)
        area.removeFromLeft(10);

        auto onCol = area.removeFromRight(44);
        mOn.setBounds(onCol.withSizeKeepingCentre(44, 22));
        area.removeFromRight(8);
        if (mWave.isVisible())
        {
            mWave.setBounds(area.removeFromRight(84).withSizeKeepingCentre(84, 24));
            area.removeFromRight(8);
        }

        std::vector<juce::Component *> vis;
        for (juce::Component *k : {(juce::Component *)mRate.get(), (juce::Component *)mDepth.get(),
                                   (juce::Component *)mFeedback.get(), (juce::Component *)mMix.get(),
                                   (juce::Component *)mWidth.get(), (juce::Component *)mDrive.get()})
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
    int mSlot;
    int mLastType = -1, mLastSync = -1;
    bool mLastOn = true;
    float mLedX = 0.0f, mLedY = 0.0f;
    juce::Label mNum;
    ModFxIcon mIcon;
    juce::ComboBox mType, mWave, mSync;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> mTypeAtt, mWaveAtt, mSyncAtt;
    juce::ToggleButton mOn;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> mOnAtt;
    std::unique_ptr<LabeledKnob> mRate, mDepth, mFeedback, mMix, mWidth, mDrive;
    juce::ToggleButton mRotFast;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> mRotFastAtt;
};

class ModPanel : public BlockPanel
{
public:
    explicit ModPanel(juce::AudioProcessorValueTreeState &apvts) : BlockPanel("MODULATION")
    {
        for (int s = 0; s < nam_rig::ModBlock::kSlots; ++s)
        {
            mLanes[(size_t)s] = std::make_unique<ModSlotLane>(apvts, s);
            addAndMakeVisible(*mLanes[(size_t)s]);
        }
    }

    void refresh()
    {
        for (auto &l : mLanes)
            if (l) l->refresh();
    }

    void paint(juce::Graphics &g) override
    {
        BlockPanel::paint(g); // panel body + "MODULATION" title
        if (mSpine.getHeight() <= 0)
            return;
        const float x = (float)mSpine.getCentreX();
        g.setColour(colors::outline);
        g.drawLine(x, (float)mSpine.getY(), x, (float)mSpine.getBottom(), 1.5f);
        g.setColour(colors::accentDim);
        for (float fy : mArrowYs)
        {
            juce::Path tri;
            tri.addTriangle(x - 3.0f, fy - 2.0f, x + 3.0f, fy - 2.0f, x, fy + 3.0f);
            g.fillPath(tri);
        }
        g.setColour(colors::textDim);
        g.setFont(RigLookAndFeel::withHeight(9.0f));
        g.drawText("IN", mSpine.getX() - 3, mSpine.getY() - 13, 26, 11, juce::Justification::centred);
        g.drawText("OUT", mSpine.getX() - 3, mSpine.getBottom() + 3, 26, 11, juce::Justification::centred);
    }

    void resized() override
    {
        auto area = contentArea();
        auto spine = area.removeFromLeft(20);
        area.removeFromLeft(4);
        const int n = nam_rig::ModBlock::kSlots, gap = 8;
        const int laneH = (area.getHeight() - gap * (n - 1)) / n;
        mArrowYs.clear();
        mSpine = spine.withTrimmedTop(14).withTrimmedBottom(16);
        for (int s = 0; s < n; ++s)
        {
            auto lane = area.removeFromTop(laneH);
            mLanes[(size_t)s]->setBounds(lane);
            if (s < n - 1)
            {
                mArrowYs.push_back((float)area.removeFromTop(gap).getCentreY());
            }
        }
    }

private:
    std::array<std::unique_ptr<ModSlotLane>, (size_t)nam_rig::ModBlock::kSlots> mLanes;
    juce::Rectangle<int> mSpine;
    std::vector<float> mArrowYs;
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
