#pragma once
#include "PluginProcessor.h"
#include <juce_audio_utils/juce_audio_utils.h>

// Minimal v1 editor: load model / load IR, chain status line, amp AA combo,
// in/out gains. Real block UI (slots, per-block panels) comes once the chain
// has more than the amp + cab doing work.
class NamRigEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit NamRigEditor(NamRigProcessor &);
    ~NamRigEditor() override = default;

    void paint(juce::Graphics &) override;
    void resized() override;

private:
    void timerCallback() override;

    NamRigProcessor &mProc;

    juce::TextButton mLoadModelBtn{"Load NAM model..."};
    juce::TextButton mLoadIrBtn{"Load cab IR..."};
    juce::Label mModelLabel, mIrLabel, mStatusLabel;

    juce::ComboBox mOversampleBox;
    juce::Label mOversampleLabel{{}, "Amp AA"};
    juce::Slider mInGain{juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight};
    juce::Slider mOutGain{juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight};
    juce::Label mInLabel{{}, "Input"}, mOutLabel{{}, "Output"};

    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> mOversampleAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mInAtt, mOutAtt;

    std::unique_ptr<juce::FileChooser> mChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NamRigEditor)
};
