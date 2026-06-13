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

    // --- model / IR loading (message thread). rig 0 = Rig A, 1 = Rig B. ---
    void loadModel(const juce::File &namFile, int rig = 0);
    void loadIr(const juce::File &irFile, int rig = 0);

    // Source content cached at load time (embedded into .namrig presets).
    const juce::String &modelText(int rig = 0) const { return mModelText[rig]; }
    const juce::String &modelBaseName(int rig = 0) const { return mModelBaseName[rig]; }
    const juce::MemoryBlock &irBytes(int rig = 0) const { return mIrBytes[rig]; }
    const juce::String &irBaseName(int rig = 0) const { return mIrBaseName[rig]; }

    // Preset manager lives on the processor so the current preset name
    // survives editor close/reopen (message thread only).
    nam_rig::PresetManager &presets() { return *mPresets; }
    juce::String getModelName(int rig = 0) const { return mModelName[rig]; }
    juce::String getIrName(int rig = 0) const { return cabFor(rig).irName(); }
    bool isModelLoaded(int rig = 0) const { return mModelLoaded[rig].load(); }
    bool isIrLoaded(int rig = 0) const { return cabFor(rig).isIrLoaded(); }
    bool isA2Model(int rig = 0) const { return ampFor(rig).engine().isA2(); }

    // Engaged amp factor on the last block (0 = passthrough). Editor status.
    int engagedFactor(int rig = 0) const { return ampFor(rig).engagedFactor(); }

    // Auto phase-align: probe both voices, cross-correlate, write the measured
    // lag (rigAlign) + polarity (rigPolB) to params. Suspends processing around
    // the offline render. Message thread; wired to the load/Align UI in 2d.
    void autoAlign();

    // --- Input calibration / output normalization (NAM-AA parity; CalNorm.h) ---
    // Driven by Rig A's model metadata (calibration is pre-split, normalization
    // post-mix); folded into the in/out gains in processBlock.
    bool hasInputCalibration() const { return mChain.amp.engine().hasInputLevelDbu(); }
    bool hasLoudness() const { return mChain.amp.engine().hasLoudness(); }
    float calibrationGainDb() const;
    float normalizationGainDb() const;

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

    // Per-rig block access (rig 0 = A, 1 = B).
    nam_rig::AmpBlock &ampFor(int rig) { return rig ? mChain.ampB : mChain.amp; }
    const nam_rig::AmpBlock &ampFor(int rig) const { return rig ? mChain.ampB : mChain.amp; }
    nam_rig::CabBlock &cabFor(int rig) { return rig ? mChain.cabB : mChain.cab; }
    const nam_rig::CabBlock &cabFor(int rig) const { return rig ? mChain.cabB : mChain.cab; }

    nam_rig::RigChain mChain;

    // Per-rig model/IR state (index 0 = Rig A, 1 = Rig B).
    juce::String mModelName[2]{"No model loaded", "No model loaded"};
    std::atomic<bool> mModelLoaded[2]{};

    // Paths persisted in plugin state and restored on load.
    juce::String mModelPath[2], mIrPath[2];

    // Embedding caches (message thread only; written by loadModel/loadIr).
    juce::String mModelText[2], mModelBaseName[2];
    juce::MemoryBlock mIrBytes[2];
    juce::String mIrBaseName[2];

    std::unique_ptr<nam_rig::PresetManager> mPresets;

    // Re-report PDC when these change (lookahead / align / mode shift latency).
    float mLastGateLookMs = -1.0f;
    float mLastRigAlign = -1.0e9f;
    int mLastRigMode = -1;

    double mSampleRate = 48000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NamRigProcessor)
};
