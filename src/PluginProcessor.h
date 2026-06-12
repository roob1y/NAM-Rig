#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <memory>

#include "rig/RigChain.h"

namespace nam_rig { class PresetManager; }

class NamRigProcessor : public juce::AudioProcessor
{
public:
    NamRigProcessor();
    ~NamRigProcessor() override; // defined in .cpp (unique_ptr to fwd-decl type)

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;
    bool isBusesLayoutSupported(const BusesLayout &) const override;

    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor *createEditor() override;

    void getStateInformation(juce::MemoryBlock &) override;
    void setStateInformation(const void *, int) override;

    // Offline renders may bump the amp's AA factor; latency changes, re-report.
    void setNonRealtime(bool isNonRealtime) noexcept override;

    const juce::String getName() const override { return "NAM Rig"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String &) override {}

    // --- model / IR loading (message thread) ---
    void loadModel(const juce::File &namFile);
    void loadIr(const juce::File &irFile);

    // Source content cached at load time (embedded into .namrig presets).
    const juce::String &modelText() const { return mModelText; }
    const juce::String &modelBaseName() const { return mModelBaseName; }
    const juce::MemoryBlock &irBytes() const { return mIrBytes; }
    const juce::String &irBaseName() const { return mIrBaseName; }

    // Preset manager lives on the processor so the current preset name
    // survives editor close/reopen (message thread only).
    nam_rig::PresetManager &presets() { return *mPresets; }
    juce::String getModelName() const { return mModelName; }
    juce::String getIrName() const { return mChain.cab.irName(); }
    bool isModelLoaded() const { return mModelLoaded.load(); }
    bool isIrLoaded() const { return mChain.cab.isIrLoaded(); }
    bool isA2Model() const { return mChain.amp.engine().isA2(); }

    // Engaged amp factor on the last block (0 = passthrough). Editor status.
    int engagedFactor() const { return mChain.amp.engagedFactor(); }

    // Live gain-reduction telemetry for the editor meters.
    float gateGainDb() const { return mChain.gate.currentGainDb(); }
    float compGrDb() const { return mChain.comp.grDb(); }

    // --- Parameters ---
    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // --- Meters (atomic, read by editor) ---
    std::atomic<float> mInputPeakDb{-100.0f};
    std::atomic<float> mOutputPeakDb{-100.0f};

    // --- Editor session state (not persisted; survives editor close/reopen) ---
    int uiSelectedBlock = 2; // strip selection, default AMP
    int uiWidth = 0;         // last editor width, 0 = use default

private:
    void updateLatency();
    int requestedFactorNow() const; // oversample param + offline bump (NAM-AA logic)

    nam_rig::RigChain mChain;

    juce::String mModelName{"No model loaded"};
    std::atomic<bool> mModelLoaded{false};

    // Paths persisted in plugin state and restored on load.
    juce::String mModelPath, mIrPath;

    // Embedding caches (message thread only; written by loadModel/loadIr).
    juce::String mModelText, mModelBaseName;
    juce::MemoryBlock mIrBytes;
    juce::String mIrBaseName;

    std::unique_ptr<nam_rig::PresetManager> mPresets;

    // Last gate lookahead pushed to the chain (PDC re-report on change).
    float mLastGateLookMs = -1.0f;

    double mSampleRate = 48000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NamRigProcessor)
};
