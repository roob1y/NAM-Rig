#include "PluginEditor.h"

NamRigEditor::NamRigEditor(NamRigProcessor &p)
    : juce::AudioProcessorEditor(&p), mProc(p)
{
    addAndMakeVisible(mLoadModelBtn);
    addAndMakeVisible(mLoadIrBtn);
    addAndMakeVisible(mModelLabel);
    addAndMakeVisible(mIrLabel);
    addAndMakeVisible(mStatusLabel);
    addAndMakeVisible(mOversampleBox);
    addAndMakeVisible(mOversampleLabel);
    addAndMakeVisible(mInGain);
    addAndMakeVisible(mOutGain);
    addAndMakeVisible(mInLabel);
    addAndMakeVisible(mOutLabel);

    mLoadModelBtn.onClick = [this]
    {
        mChooser = std::make_unique<juce::FileChooser>(
            "Select a NAM model", juce::File{}, "*.nam");
        mChooser->launchAsync(juce::FileBrowserComponent::openMode |
                                  juce::FileBrowserComponent::canSelectFiles,
                              [this](const juce::FileChooser &fc)
                              {
                                  if (fc.getResult().existsAsFile())
                                      mProc.loadModel(fc.getResult());
                              });
    };

    mLoadIrBtn.onClick = [this]
    {
        mChooser = std::make_unique<juce::FileChooser>(
            "Select a cab IR", juce::File{}, "*.wav;*.aif;*.aiff");
        mChooser->launchAsync(juce::FileBrowserComponent::openMode |
                                  juce::FileBrowserComponent::canSelectFiles,
                              [this](const juce::FileChooser &fc)
                              {
                                  if (fc.getResult().existsAsFile())
                                      mProc.loadIr(fc.getResult());
                              });
    };

    // Combo items must match the parameter's StringArray order, IDs are 1-based.
    mOversampleBox.addItemList({"Off", "2x", "4x", "8x", "16x", "32x"}, 1);
    mOversampleAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        mProc.apvts, "oversample", mOversampleBox);
    mInAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        mProc.apvts, "inputGain", mInGain);
    mOutAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        mProc.apvts, "outputGain", mOutGain);

    startTimerHz(4);
    setSize(520, 260);
}

void NamRigEditor::timerCallback()
{
    mModelLabel.setText("Amp: " + mProc.getModelName(), juce::dontSendNotification);
    mIrLabel.setText("Cab: " + (mProc.isIrLoaded() ? mProc.getIrName() : juce::String("No IR loaded")),
                     juce::dontSendNotification);

    juce::String status;
    const int engaged = mProc.engagedFactor();
    if (!mProc.isModelLoaded())
        status = "Chain: gate > comp > AMP > eq > cab > mod > delay > reverb";
    else if (engaged > 0)
        status = "Amp engaged at " + juce::String(engaged) + "x | PDC "
                 + juce::String(mProc.getLatencySamples()) + " smp";
    else
        status = "Amp passthrough";
    mStatusLabel.setText(status, juce::dontSendNotification);
}

void NamRigEditor::paint(juce::Graphics &g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void NamRigEditor::resized()
{
    auto area = getLocalBounds().reduced(12);
    auto row = [&](int h) { return area.removeFromTop(h); };

    {
        auto r = row(32);
        mLoadModelBtn.setBounds(r.removeFromLeft(160));
        r.removeFromLeft(8);
        mModelLabel.setBounds(r);
    }
    area.removeFromTop(6);
    {
        auto r = row(32);
        mLoadIrBtn.setBounds(r.removeFromLeft(160));
        r.removeFromLeft(8);
        mIrLabel.setBounds(r);
    }
    area.removeFromTop(6);
    {
        auto r = row(28);
        mOversampleLabel.setBounds(r.removeFromLeft(70));
        mOversampleBox.setBounds(r.removeFromLeft(120));
    }
    area.removeFromTop(6);
    {
        auto r = row(28);
        mInLabel.setBounds(r.removeFromLeft(70));
        mInGain.setBounds(r);
    }
    {
        auto r = row(28);
        mOutLabel.setBounds(r.removeFromLeft(70));
        mOutGain.setBounds(r);
    }
    area.removeFromTop(8);
    mStatusLabel.setBounds(row(24));
}

juce::AudioProcessorEditor *NamRigProcessor::createEditor()
{
    return new NamRigEditor(*this);
}
