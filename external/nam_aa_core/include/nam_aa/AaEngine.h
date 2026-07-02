#pragma once
// AaEngine — the complete NAM AA amp engine: model set (1x..32x dilation-scaled
// A2 replicas), SRC layer (DAW rate <-> model family rate), half-band FIR
// oversamplers, routing + latency math. Mono, DAW-rate in/out, in-place.
//
// Extracted from the NAM-AA plugin's PluginProcessor (June 2026) so that the
// plugin, the rig plugin's amp block, and tests/chain_process.cpp all run the
// IDENTICAL chain — no copy drift. The process() body is a verbatim port of
// processBlock stages 1-3; treat any edit here as a behavior change to all
// consumers and re-run the chain equivalence check (tools/analyze_chain.py).
//
// Threading: loadModel() may run on any thread (model swap under a SpinLock).
// process() is RT-safe and try-locks: on contention it leaves the buffer
// untouched and returns kSkipped. prepare() must be called before process().
//
// JUCE: this header needs juce_audio_basics + juce_dsp include paths and the
// consuming target must link those modules. The nam_aa_core CMake target only
// carries the (JUCE-free) NAM sources; see core/CMakeLists.txt.

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <atomic>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "NAM/get_dsp.h"
#include "NAM/dsp.h"
#include "NAM/wavenet/model.h"
#include "NAM/wavenet/a2_fast.h"
#include "NAM/convnet.h"
#include "NAM/lstm.h"
#include "NAM/container.h"
#include "NAM/model_config.h"
#include "json.hpp"

#include "RoutingMath.h"
#include "SrcLayer.h"

namespace nam_aa
{

// Register the NAM config parsers once per process (idempotent).
inline void ensureNamRegistrations()
{
    auto &reg = nam::ConfigParserRegistry::instance();
    if (!reg.has("WaveNet"))
        reg.registerParser("WaveNet", nam::wavenet::create_config);
    if (!reg.has("ConvNet"))
        reg.registerParser("ConvNet", nam::convnet::create_config);
    if (!reg.has("LSTM"))
        reg.registerParser("LSTM", nam::lstm::create_config);
    if (!reg.has("Linear"))
        reg.registerParser("Linear", nam::linear::create_config);
    if (!reg.has("SlimmableContainer"))
        reg.registerParser("SlimmableContainer", nam::container::create_config);
}

class AaEngine
{
public:
    // process() return codes (values > 0 are the engaged model factor).
    static constexpr int kSkipped = -1; // model lock contended — buffer untouched
    static constexpr int kNoModel = 0;  // no model for the routed path — buffer untouched

    struct LoadInfo
    {
        bool ok = false;
        std::string error;            // valid when !ok
        bool isA2 = false;            // scaled (2x..32x) paths available
        double expectedSampleRate = 0.0; // from the .nam file (0 if absent)
    };

    AaEngine() { ensureNamRegistrations(); }

    // ----- model management (verbatim port of PluginProcessor::loadModel) -----
    LoadInfo loadModel(const std::filesystem::path &namPath)
    {
        LoadInfo info;
        try
        {
            // Load JSON and get the 1x model + config data in one shot
            nam::dspData data;
            auto new1x = nam::get_dsp(namPath, data);

            if (!new1x)
            {
                info.error = "Failed to create DSP";
                return info;
            }

            // Capture-rig input calibration from metadata (applies to A2 and generic models).
            {
                bool has = false;
                float dbu = 0.0f;
                if (!data.metadata.is_null())
                {
                    auto it = data.metadata.find("input_level_dbu");
                    if (it != data.metadata.end() && !it->is_null() && it->is_number())
                    {
                        dbu = it->get<float>();
                        has = true;
                    }
                }
                mInputLevelDbu.store(dbu);
                mHasInputLevelDbu.store(has);

                bool hasLoud = false;
                float loud = 0.0f;
                if (!data.metadata.is_null())
                {
                    auto it = data.metadata.find("loudness");
                    if (it != data.metadata.end() && !it->is_null() && it->is_number())
                    {
                        loud = it->get<float>();
                        hasLoud = true;
                    }
                }
                mLoudnessDb.store(loud);
                mHasLoudness.store(hasLoud);
            }

            // Try to build the dilation-scaled models if this is a direct A2 WaveNet
            std::unique_ptr<nam::DSP> new2x;
            std::unique_ptr<nam::DSP> new4x;
            std::unique_ptr<nam::DSP> new8x;
            std::unique_ptr<nam::DSP> new16x;
            std::unique_ptr<nam::DSP> new32x;
            bool isA2 = false;
            int a2ch = 0;

            // Resolve WaveNet config + weights for either direct WaveNet or SlimmableContainer
            nlohmann::json wavenetCfg = data.config;
            std::vector<float> wavenetWeights = data.weights;

            if (data.architecture == "SlimmableContainer")
            {
                auto &submodels = data.config["submodels"];
                auto &lastModel = submodels.back()["model"];
                wavenetCfg = lastModel["config"];
                if (lastModel.contains("weights"))
                    wavenetWeights = lastModel["weights"].get<std::vector<float>>();
            }

            if (nam::wavenet::a2_fast::is_a2_shape(wavenetCfg, &a2ch))
            {
                isA2 = true;

                // Metadata helper reused for all models.
                auto applyMeta = [&](nam::DSP *m)
                {
                    if (data.metadata.is_null() || !m)
                        return;
                    auto get = [&](const char *key) -> std::optional<double>
                    {
                        auto it = data.metadata.find(key);
                        if (it != data.metadata.end() && !it->is_null())
                            return it->get<double>();
                        return std::nullopt;
                    };
                    if (auto v = get("loudness"))
                        m->SetLoudness(*v);
                    if (auto v = get("input_level_dbu"))
                        m->SetInputLevel(*v);
                    if (auto v = get("output_level_dbu"))
                        m->SetOutputLevel(*v);
                };

                // Dilation-scaled models. 2x/4x are the live AA paths; 8x/16x/32x are
                // engaged only for offline (non-realtime) renders.
                auto buildScaled = [&](int factor)
                {
                    auto w = wavenetWeights;
                    auto m2 = nam::wavenet::a2_fast::build_a2_fast_model(
                        a2ch, std::move(w), 48000.0 * factor, factor);
                    m2->prewarm();
                    applyMeta(m2.get());
                    return m2;
                };
                new2x = buildScaled(2);
                new4x = buildScaled(4);
                new8x = buildScaled(8);
                new16x = buildScaled(16);
                new32x = buildScaled(32);
            }
            // Swap all models under the lock atomically
            {
                juce::SpinLock::ScopedLockType lock(mModelLock);
                mModel = std::move(new1x);
                mModel2x = std::move(new2x);
                mModel4x = std::move(new4x);
                mModel8x = std::move(new8x);
                mModel16x = std::move(new16x);
                mModel32x = std::move(new32x);
                mIsA2Model = isA2;
            }

            info.ok = true;
            info.isA2 = isA2;
            info.expectedSampleRate = data.expected_sample_rate;
            return info;
        }
        catch (const std::exception &e)
        {
            info.error = e.what();
            return info;
        }
        catch (...)
        {
            info.error = "Unknown load error";
            return info;
        }
    }

    // Unload the current model: drop every dilation-scaled model under the same
    // lock loadModel() swaps on, and clear the capture metadata. process() then
    // falls back to passthrough (anyModelLoaded() == false). Safe from any thread.
    void unloadModel()
    {
        {
            juce::SpinLock::ScopedLockType lock(mModelLock);
            mModel.reset();
            mModel2x.reset();
            mModel4x.reset();
            mModel8x.reset();
            mModel16x.reset();
            mModel32x.reset();
            mIsA2Model = false;
        }
        mHasInputLevelDbu.store(false);
        mInputLevelDbu.store(0.0f);
        mHasLoudness.store(false);
        mLoudnessDb.store(0.0f);
    }

    // Highest dilation-scaled model currently loaded (1 if not A2).
    int maxFactorAvailable() const
    {
        if (!mIsA2Model.load())
            return 1;
        if (mModel32x) return 32;
        if (mModel16x) return 16;
        if (mModel8x) return 8;
        if (mModel4x) return 4;
        if (mModel2x) return 2;
        return 1;
    }

    bool isA2() const { return mIsA2Model.load(); }
    bool anyModelLoaded() const
    {
        return mModel || mModel2x || mModel4x || mModel8x || mModel16x || mModel32x;
    }
    bool hasModelFor(int factor) const { return modelForFactor(factor) != nullptr; }

    // ----- model metadata (valid per the has* flag) -----
    bool hasInputLevelDbu() const { return mHasInputLevelDbu.load(); }
    float inputLevelDbu() const { return mInputLevelDbu.load(); }
    bool hasLoudness() const { return mHasLoudness.load(); }
    float loudnessDb() const { return mLoudnessDb.load(); }

    // ----- lifecycle -----
    // Call from prepareToPlay (or once before offline processing).
    void prepare(double dawRate, int maxBlock)
    {
        mDawRate = dawRate;

        const int maxBlockSafe = juce::jmax(maxBlock, 64);
        // Generous upper bound on mono Rsrc-rate samples produced from one DAW block for any
        // supported rate (worst expansion is well under 2.5x).
        const int maxSrc = (int)std::ceil(maxBlockSafe * 2.5) + 128;

        mSrcBuffer.setSize(1, maxSrc);
        mScratchBuffer.setSize(1, maxSrc);

        // Oversampler input is at Rsrc (48k); maxSrc comfortably covers it. It allocates 2x internally.
        for (auto *os : {&mOversampler2x, &mOversampler4x, &mOversampler8x,
                         &mOversampler16x, &mOversampler32x})
            os->initProcessing((size_t)maxSrc);
        mSrc.prepare(maxBlockSafe, maxSrc, dawRate);

        mPrepared = true;
    }

    bool isPrepared() const { return mPrepared; }
    double dawRate() const { return mDawRate; }

    // ----- routing / latency -----
    Routing routingFor(int requestedFactor) const
    {
        return computeRouting(mDawRate, requestedFactor, maxFactorAvailable());
    }

    // Latency of the resolved chain, in DAW samples. Valid after prepare().
    double latencySamples(int requestedFactor) const
    {
        const Routing r = routingFor(requestedFactor);
        const double osLatencyRsrc =
            (r.firFactor >= 2) ? oversamplerFor(r.firFactor).getLatencyInSamples() : 0.0;
        const double srcLat = r.useCatmull ? mSrc.chainLatencyDawSamples() : 0.0;
        return nam_aa::latencySamples(r, mDawRate, osLatencyRsrc, srcLat);
    }

    // SRC contribution helper (exposed for logging/diagnostics).
    const SrcLayer &srcLayer() const { return mSrc; }

    // ----- processing -----
    // Process `numSamples` mono samples in-place at the DAW rate.
    // Returns the engaged model factor (>= 1), kNoModel if no model exists for
    // the routed path, or kSkipped if the model lock was contended. In both
    // non-positive cases the buffer is left untouched (caller passes through).
    //
    // Verbatim port of PluginProcessor::processBlock stages 1-3.
    int process(float *mono, int numSamples, int requestedFactor)
    {
        juce::SpinLock::ScopedTryLockType lock(mModelLock);

        const Routing r = routingFor(requestedFactor);

        // Routing invariants (debug only).
        jassert(r.modelFactor >= 1 && r.modelFactor <= nam_aa::kMaxAAFactor
                && (r.modelFactor & (r.modelFactor - 1)) == 0);
        jassert(r.firFactor >= 1 && r.firFactor <= nam_aa::kMaxAAFactor
                && (r.firFactor & (r.firFactor - 1)) == 0);
        jassert(r.Rmodel == 48000.0 * r.modelFactor);
        jassert(r.firFactor == 1 || (r.firFactor * (int)r.Rsrc == (int)r.Rmodel));

        if (!lock.isLocked())
            return kSkipped;

        nam::DSP *const activeModel = modelForFactor(r.modelFactor);
        if (!activeModel)
            return kNoModel;

        const float *dawIn = mono;
        float *src = mSrcBuffer.getWritePointer(0);
        int L = 0; // number of Rsrc-rate samples currently in 'src'

        // ---- 1) DAW rate -> Rsrc (mono) ----
        if (r.useCatmull)
        {
            L = mSrc.up(dawIn, numSamples, src, mDawRate, r.Rsrc);
        }
        else
        {
            juce::FloatVectorOperations::copy(src, dawIn, numSamples);
            L = numSamples;
        }

        // ---- 2) Run the model at Rsrc (optionally via the 2x FIR oversampler) ----
        if (L > 0)
        {
            // Run activeModel in 4096-sample chunks, in-place.
            auto runModel = [&](float *buf, int n)
            {
                int pos = 0;
                while (pos < n)
                {
                    const int chunk = juce::jmin(4096, n - pos);
                    float *p = buf + pos;
                    float *inP[1] = {p};
                    float *outP[1] = {p};
                    activeModel->process(inP, outP, chunk);
                    pos += chunk;
                }
            };

            if (r.firFactor >= 2)
            {
                // FIR oversampled path: Rsrc → (firFactor×) → model → (÷firFactor) → Rsrc.
                auto &oversampler = oversamplerFor(r.firFactor);
                float *srcPtr = src;
                juce::dsp::AudioBlock<float> monoBlock(&srcPtr, 1, (size_t)L);
                auto osBlock = oversampler.processSamplesUp(monoBlock);
                runModel(osBlock.getChannelPointer(0), (int)osBlock.getNumSamples());
                oversampler.processSamplesDown(monoBlock);
            }
            else if (r.modelFactor >= 2)
            {
                // Direct path: DAW rate == Rmodel (e.g. 96k DAW + 2x model), no FIR needed.
                runModel(src, L);
            }
            else if (mModel)
            {
                // 1x model: separate output buffer then copy back.
                float *out = mScratchBuffer.getWritePointer(0);
                int pos = 0;
                while (pos < L)
                {
                    const int chunk = juce::jmin(4096, L - pos);
                    float *ip = src + pos;
                    float *op = out + pos;
                    float *inP[1] = {ip};
                    float *outP[1] = {op};
                    mModel->process(inP, outP, chunk);
                    pos += chunk;
                }
                juce::FloatVectorOperations::copy(src, out, L);
            }
        }

        // ---- 3) Rsrc -> DAW rate, producing exactly numSamples ----
        if (r.useCatmull)
        {
            mSrc.down(src, L, mono, numSamples, mDawRate, r.Rsrc);
        }
        else
        {
            juce::FloatVectorOperations::copy(mono, src, numSamples); // L == numSamples here
        }

        return r.modelFactor;
    }

private:
    nam::DSP *modelForFactor(int factor) const
    {
        switch (factor)
        {
        case 32: return mModel32x.get();
        case 16: return mModel16x.get();
        case 8: return mModel8x.get();
        case 4: return mModel4x.get();
        case 2: return mModel2x.get();
        default: return mModel.get();
        }
    }

    juce::dsp::Oversampling<float> &oversamplerFor(int firFactor)
    {
        switch (firFactor)
        {
        case 32: return mOversampler32x;
        case 16: return mOversampler16x;
        case 8: return mOversampler8x;
        case 4: return mOversampler4x;
        default: return mOversampler2x;
        }
    }
    const juce::dsp::Oversampling<float> &oversamplerFor(int firFactor) const
    {
        return const_cast<AaEngine *>(this)->oversamplerFor(firFactor);
    }

    juce::SpinLock mModelLock;
    std::unique_ptr<nam::DSP> mModel;    // 1x model  (always run at 48k)
    std::unique_ptr<nam::DSP> mModel2x;  // 2x model  (always run at 96k)
    std::unique_ptr<nam::DSP> mModel4x;  // 4x model (always at 192k)
    std::unique_ptr<nam::DSP> mModel8x;  // 8x  (384k)  — offline renders only
    std::unique_ptr<nam::DSP> mModel16x; // 16x (768k)  — offline renders only
    std::unique_ptr<nam::DSP> mModel32x; // 32x (1536k) — offline renders only

    std::atomic<bool> mIsA2Model{false}; // whether the scaled (2x..32x) paths are available

    // Model capture calibration from .nam metadata (valid when mHasInputLevelDbu).
    std::atomic<bool> mHasInputLevelDbu{false};
    std::atomic<float> mInputLevelDbu{0.0f};

    // Model loudness from .nam metadata (valid when mHasLoudness).
    std::atomic<bool> mHasLoudness{false};
    std::atomic<float> mLoudnessDb{0.0f};

    // 2x oversampler: 2^1=2x
    juce::dsp::Oversampling<float> mOversampler2x{
        1, 1,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
        true, true};

    juce::dsp::Oversampling<float> mOversampler4x{
        1, 2,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
        true, true};

    // 8x/16x/32x: only engaged for offline (non-realtime) rendering.
    juce::dsp::Oversampling<float> mOversampler8x{
        1, 3,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
        true, true};
    juce::dsp::Oversampling<float> mOversampler16x{
        1, 4,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
        true, true};
    juce::dsp::Oversampling<float> mOversampler32x{
        1, 5,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
        true, true};

    // --- Sample-rate conversion layer (DAW rate <-> model family rate) ---
    SrcLayer mSrc;

    juce::AudioBuffer<float> mSrcBuffer;     // mono Rsrc-rate working buffer
    juce::AudioBuffer<float> mScratchBuffer; // mono 1x-model output (kept separate from input)

    double mDawRate = 48000.0;
    bool mPrepared = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AaEngine)
};

} // namespace nam_aa
