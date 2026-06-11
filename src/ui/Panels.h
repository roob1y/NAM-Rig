#pragma once
#include "PluginProcessor.h"
#include "ui/RigLookAndFeel.h"

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
    }

    void resized() override
    {
        auto area = contentArea();
        mHint.setBounds(area.removeFromBottom(20));
        area = area.withSizeKeepingCentre(area.getWidth(),
                                          juce::jmin(area.getHeight(), 170));
        const int w = area.getWidth() / (int)mKnobs.size();
        for (auto &k : mKnobs)
            k->setBounds(area.removeFromLeft(w).reduced(6, 0));
    }

private:
    std::vector<std::unique_ptr<LabeledKnob>> mKnobs;
    juce::Label mHint;
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
    }

    void resized() override
    {
        auto area = contentArea();
        mHint.setBounds(area.removeFromBottom(20));
        const int w = juce::jmin(130, area.getWidth() / (int)mKnobs.size());
        auto row = area.withSizeKeepingCentre(w * (int)mKnobs.size(),
                                              juce::jmin(area.getHeight(), 170));
        for (auto &k : mKnobs)
            k->setBounds(row.removeFromLeft(w).reduced(6, 0));
    }

private:
    std::vector<std::unique_ptr<LabeledKnob>> mKnobs;
    juce::Label mHint;
};

//==============================================================================
class AmpPanel : public BlockPanel
{
public:
    explicit AmpPanel(NamRigProcessor &proc) : BlockPanel("AMP - NEURAL MODEL"), mProc(proc)
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
                                          mProc.loadModel(fc.getResult());
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
    }

    // Called from the editor timer.
    void refresh()
    {
        const bool loaded = mProc.isModelLoaded();
        const bool a2 = loaded && mProc.isA2Model();
        mModelName.setText(loaded ? mProc.getModelName() : "No model loaded",
                           juce::dontSendNotification);

        // AA only exists for A2 models (dilation-scaled copies); grey it out otherwise.
        const bool aaAvailable = !loaded || a2;
        mLiveAa.setEnabled(aaAvailable);
        mOfflineAa.setEnabled(aaAvailable);

        juce::String info;
        if (!loaded)
            info = "Load a .nam model to bring the amp online.";
        else if (!a2)
            info = "Standard model - anti-aliasing controls need an A2 model.";
        else
        {
            const int engaged = mProc.engagedFactor();
            info = engaged > 0 ? "Engaged at " + juce::String(engaged) + "x"
                               : "Passthrough";
            info << "  |  PDC " << mProc.getLatencySamples() << " smp";
        }
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
    }

private:
    NamRigProcessor &mProc;
    juce::TextButton mLoadBtn{"Load NAM model..."};
    juce::Label mModelName, mInfo;
    juce::ComboBox mLiveAa, mOfflineAa;
    juce::Label mLiveAaLabel, mOfflineAaLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> mLiveAtt, mOfflineAtt;
    std::unique_ptr<juce::FileChooser> mChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AmpPanel)
};

//==============================================================================
class EqPanel : public BlockPanel
{
public:
    explicit EqPanel(juce::AudioProcessorValueTreeState &apvts)
        : BlockPanel("GRAPHIC EQ - PRE-CAB"), mApvts(apvts)
    {
        static const char *ids[] = {"eq62", "eq125", "eq250", "eq500",
                                    "eq1k", "eq2k", "eq4k", "eq8k"};
        static const char *captions[] = {"62.5", "125", "250", "500",
                                         "1k", "2k", "4k", "8k"};
        for (int b = 0; b < 8; ++b)
        {
            auto slider = std::make_unique<juce::Slider>(juce::Slider::LinearVertical,
                                                         juce::Slider::NoTextBox);
            slider->setPopupDisplayEnabled(true, true, this);
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
            static const char *bandIds[] = {"eq62", "eq125", "eq250", "eq500",
                                            "eq1k", "eq2k", "eq4k", "eq8k"};
            for (const char *id : bandIds)
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
    std::vector<std::unique_ptr<juce::Slider>> mSliders;
    std::vector<std::unique_ptr<juce::Label>> mLabels;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> mAtts;
    juce::TextButton mFlatBtn{"Flat"};
};

//==============================================================================
class CabPanel : public BlockPanel
{
public:
    explicit CabPanel(NamRigProcessor &proc) : BlockPanel("CABINET - IR"), mProc(proc)
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
                                          mProc.loadIr(fc.getResult());
                                  });
        };

        mIrName.setColour(juce::Label::textColourId, colors::text);
        addAndMakeVisible(mIrName);

        mHpf = std::make_unique<LabeledKnob>(mProc.apvts, "cabHpf", "Low Cut");
        mLpf = std::make_unique<LabeledKnob>(mProc.apvts, "cabLpf", "High Cut");
        addAndMakeVisible(*mHpf);
        addAndMakeVisible(*mLpf);

        mHint.setText("Cuts sit post-cab (12 dB/oct). Knob extremes = filter off, bit-exact.",
                      juce::dontSendNotification);
        mHint.setColour(juce::Label::textColourId, colors::textDim);
        addAndMakeVisible(mHint);
    }

    void refresh()
    {
        mIrName.setText(mProc.isIrLoaded() ? mProc.getIrName()
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
    juce::TextButton mLoadBtn{"Load cab IR..."};
    juce::Label mIrName, mHint;
    std::unique_ptr<LabeledKnob> mHpf, mLpf;
    std::unique_ptr<juce::FileChooser> mChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CabPanel)
};

} // namespace nam_rig::ui
