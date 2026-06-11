#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

juce::AudioProcessorValueTreeState::ParameterLayout NamRigProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("inputGain", 1), "Input Gain",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("outputGain", 1), "Output Gain",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    // Amp AA oversampling — identical semantics to NAM-AA's parameter.
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID("oversample", 1), "Amp Oversampling",
        juce::StringArray{"Off", "2x", "4x", "8x", "16x", "32x"}, 0)); // default Off

    // Offline renders bump the amp to a high-rate model regardless of the live
    // setting (NAM-AA parity).
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID("offlineAA", 1), "Offline AA",
        juce::StringArray{"Same as live", "8x", "16x", "32x"}, 1)); // default 8x

    // Block bypasses (stub blocks are passthrough anyway; params reserved now
    // so automation indices stay stable as blocks gain DSP).
    for (const char *id : {"gateOn", "compOn", "eqOn", "cabOn", "modOn", "delayOn", "reverbOn"})
        params.push_back(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID(id, 1), juce::String(id).dropLastCharacters(2) + " Enable", true));

    // --- Gate (see rig/GateBlock.h; verified by tests/gate_test.cpp) ---
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("gateThresh", 1), "Gate Threshold",
        juce::NormalisableRange<float>(-90.0f, -20.0f, 0.5f), -50.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("gateRange", 1), "Gate Range",
        juce::NormalisableRange<float>(20.0f, 100.0f, 1.0f), 80.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("gateAttack", 1), "Gate Attack",
        juce::NormalisableRange<float>(0.05f, 5.0f, 0.01f, 0.4f), 0.1f,
        juce::AudioParameterFloatAttributes().withLabel("ms")));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("gateHold", 1), "Gate Hold",
        juce::NormalisableRange<float>(5.0f, 500.0f, 1.0f, 0.4f), 50.0f,
        juce::AudioParameterFloatAttributes().withLabel("ms")));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("gateRelease", 1), "Gate Release",
        juce::NormalisableRange<float>(10.0f, 1000.0f, 1.0f, 0.4f), 100.0f,
        juce::AudioParameterFloatAttributes().withLabel("ms")));
    // Lookahead preserves pick attacks but adds PDC — keep 0 for live monitoring.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("gateLook", 1), "Gate Lookahead",
        juce::NormalisableRange<float>(0.0f, nam_rig::GateBlock::kMaxLookaheadMs, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel("ms")));

    // --- Comp/Boost (see rig/CompBlock.h; verified by tests/comp_test.cpp) ---
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("compSustain", 1), "Comp Sustain",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("compAttack", 1), "Comp Attack",
        juce::NormalisableRange<float>(1.0f, 50.0f, 0.5f, 0.5f), 15.0f,
        juce::AudioParameterFloatAttributes().withLabel("ms")));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("compLevel", 1), "Comp Level",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("compBoost", 1), "Boost",
        juce::NormalisableRange<float>(0.0f, 20.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    // --- Graphic EQ, pre-cab (see rig/EqBlock.h; verified by tests/eq_test.cpp) ---
    {
        static const char *ids[] = {"eq62", "eq125", "eq250", "eq500",
                                    "eq1k", "eq2k", "eq4k", "eq8k"};
        static const char *names[] = {"EQ 62.5 Hz", "EQ 125 Hz", "EQ 250 Hz", "EQ 500 Hz",
                                      "EQ 1 kHz", "EQ 2 kHz", "EQ 4 kHz", "EQ 8 kHz"};
        for (int b = 0; b < nam_rig::EqBlock::kNumBands; ++b)
            params.push_back(std::make_unique<juce::AudioParameterFloat>(
                juce::ParameterID(ids[b], 1), names[b],
                juce::NormalisableRange<float>(-nam_rig::EqBlock::kMaxGainDb,
                                               nam_rig::EqBlock::kMaxGainDb, 0.1f),
                0.0f, juce::AudioParameterFloatAttributes().withLabel("dB")));
    }

    // --- Post-cab cuts (in CabBlock; knob extremes = off/bit-exact) ---
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("cabHpf", 1), "Cab Low Cut",
        juce::NormalisableRange<float>(20.0f, 300.0f, 1.0f, 0.5f), 20.0f,
        juce::AudioParameterFloatAttributes().withLabel("Hz")));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("cabLpf", 1), "Cab High Cut",
        juce::NormalisableRange<float>(2000.0f, 20000.0f, 10.0f, 0.5f), 20000.0f,
        juce::AudioParameterFloatAttributes().withLabel("Hz")));

    return {params.begin(), params.end()};
}

NamRigProcessor::NamRigProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Parameters", createParameterLayout())
{
}

int NamRigProcessor::requestedFactorNow() const
{
    const int choice = static_cast<int>(apvts.getRawParameterValue("oversample")->load());
    int requested = 1 << juce::jlimit(0, 5, choice); // Off..32x -> 1..32
    const int offline = static_cast<int>(apvts.getRawParameterValue("offlineAA")->load());
    if (isNonRealtime() && offline > 0 && mChain.amp.engine().isA2())
        requested = juce::jmax(requested, 8 << (offline - 1)); // 8x / 16x / 32x
    return requested;
}

void NamRigProcessor::setNonRealtime(bool isNonRealtimeNow) noexcept
{
    juce::AudioProcessor::setNonRealtime(isNonRealtimeNow);
    updateLatency();
}

void NamRigProcessor::updateLatency()
{
    if (!mChain.isPrepared())
        return;
    mChain.amp.setRequestedFactor(requestedFactorNow());
    mChain.gate.setLookaheadMs(apvts.getRawParameterValue("gateLook")->load());
    setLatencySamples((int)std::round(mChain.latencySamples()));
}

void NamRigProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    mSampleRate = sampleRate;
    mChain.prepare(sampleRate, samplesPerBlock);
    updateLatency();
}

void NamRigProcessor::processBlock(juce::AudioBuffer<float> &buffer, juce::MidiBuffer &midi)
{
    juce::ignoreUnused(midi);
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    if (numSamples == 0)
        return;

    const float inGain = juce::Decibels::decibelsToGain(
        apvts.getRawParameterValue("inputGain")->load());
    const float outGain = juce::Decibels::decibelsToGain(
        apvts.getRawParameterValue("outputGain")->load());

    buffer.applyGain(inGain);

    float inPeak = 0.0f;
    for (int ch = 0; ch < numChannels; ++ch)
        inPeak = juce::jmax(inPeak, buffer.getMagnitude(ch, 0, numSamples));
    mInputPeakDb.store(juce::Decibels::gainToDecibels(inPeak, -100.0f));

    // Bypass flags + amp AA factor from parameters.
    mChain.amp.setRequestedFactor(requestedFactorNow());

    // Gate parameters (atomics; cheap to push every block). Lookahead changes
    // PDC, so re-report latency when it moves.
    mChain.gate.setThresholdDb(apvts.getRawParameterValue("gateThresh")->load());
    mChain.gate.setRangeDb(apvts.getRawParameterValue("gateRange")->load());
    mChain.gate.setAttackMs(apvts.getRawParameterValue("gateAttack")->load());
    mChain.gate.setHoldMs(apvts.getRawParameterValue("gateHold")->load());
    mChain.gate.setReleaseMs(apvts.getRawParameterValue("gateRelease")->load());
    const float gateLookMs = apvts.getRawParameterValue("gateLook")->load();
    if (gateLookMs != mLastGateLookMs)
    {
        mLastGateLookMs = gateLookMs;
        updateLatency(); // sets the block's lookahead + re-reports PDC
    }

    // gateOn does NOT chain-bypass: the lookahead delay must keep running or
    // PDC breaks. Disabled gate = forced open (passthrough + constant delay).
    mChain.gate.setEnabled(apvts.getRawParameterValue("gateOn")->load() >= 0.5f);
    // Comp/Boost parameters (zero latency, so plain chain bypass is safe).
    mChain.comp.setSustain(apvts.getRawParameterValue("compSustain")->load());
    mChain.comp.setAttackMs(apvts.getRawParameterValue("compAttack")->load());
    mChain.comp.setLevelDb(apvts.getRawParameterValue("compLevel")->load());
    mChain.comp.setBoostDb(apvts.getRawParameterValue("compBoost")->load());
    mChain.comp.setBypassed(apvts.getRawParameterValue("compOn")->load() < 0.5f);
    // Graphic EQ band gains (zero latency; chain bypass via eqOn is safe).
    {
        static const char *ids[] = {"eq62", "eq125", "eq250", "eq500",
                                    "eq1k", "eq2k", "eq4k", "eq8k"};
        for (int b = 0; b < nam_rig::EqBlock::kNumBands; ++b)
            mChain.eq.setBandGainDb(b, apvts.getRawParameterValue(ids[b])->load());
    }
    mChain.eq.setBypassed(apvts.getRawParameterValue("eqOn")->load() < 0.5f);

    // Post-cab cuts ride with the cab block.
    mChain.cab.setHpfHz(apvts.getRawParameterValue("cabHpf")->load());
    mChain.cab.setLpfHz(apvts.getRawParameterValue("cabLpf")->load());
    mChain.cab.setBypassed(apvts.getRawParameterValue("cabOn")->load() < 0.5f);
    mChain.mod.setBypassed(apvts.getRawParameterValue("modOn")->load() < 0.5f);
    mChain.delay.setBypassed(apvts.getRawParameterValue("delayOn")->load() < 0.5f);
    mChain.reverb.setBypassed(apvts.getRawParameterValue("reverbOn")->load() < 0.5f);

    mChain.process(buffer);

    buffer.applyGain(outGain);

    float outPeak = 0.0f;
    for (int ch = 0; ch < numChannels; ++ch)
        outPeak = juce::jmax(outPeak, buffer.getMagnitude(ch, 0, numSamples));
    mOutputPeakDb.store(juce::Decibels::gainToDecibels(outPeak, -100.0f));
}

bool NamRigProcessor::isBusesLayoutSupported(const BusesLayout &layouts) const
{
    const auto &mainIn = layouts.getMainInputChannelSet();
    const auto &mainOut = layouts.getMainOutputChannelSet();
    if (mainOut != mainIn)
        return false;
    return mainOut == juce::AudioChannelSet::mono() || mainOut == juce::AudioChannelSet::stereo();
}

void NamRigProcessor::loadModel(const juce::File &namFile)
{
    if (!namFile.existsAsFile())
    {
        mModelName = "File not found";
        return;
    }

    const auto info = mChain.amp.engine().loadModel(
        std::filesystem::path(namFile.getFullPathName().toStdString()));

    if (!info.ok)
    {
        mModelName = juce::String("Error: ") + info.error;
        mModelLoaded.store(false);
        return;
    }

    juce::String suffix = info.isA2 ? " [AA up to 32x]" : " [1x only]";
    if (info.expectedSampleRate > 0.0 && std::abs(info.expectedSampleRate - 48000.0) > 1.0)
        suffix += " [warn: native " + juce::String(info.expectedSampleRate, 0) + "Hz; AA assumes 48k]";

    mModelName = namFile.getFileNameWithoutExtension() + suffix;
    mModelPath = namFile.getFullPathName();
    mModelLoaded.store(true);

    updateLatency(); // available factors may have changed
}

void NamRigProcessor::loadIr(const juce::File &irFile)
{
    if (mChain.cab.loadIr(irFile))
    {
        mIrPath = irFile.getFullPathName();
        updateLatency();
    }
}

void NamRigProcessor::getStateInformation(juce::MemoryBlock &destData)
{
    auto state = apvts.copyState();
    state.setProperty("modelPath", mModelPath, nullptr);
    state.setProperty("irPath", mIrPath, nullptr);
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void NamRigProcessor::setStateInformation(const void *data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (!xml || !xml->hasTagName(apvts.state.getType()))
        return;

    auto state = juce::ValueTree::fromXml(*xml);
    apvts.replaceState(state);

    const juce::String modelPath = state.getProperty("modelPath", "");
    if (modelPath.isNotEmpty())
        loadModel(juce::File(modelPath));
    const juce::String irPath = state.getProperty("irPath", "");
    if (irPath.isNotEmpty())
        loadIr(juce::File(irPath));
}

juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter()
{
    return new NamRigProcessor();
}
