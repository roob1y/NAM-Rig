#pragma once
// CabBlock — mono cabinet IR loader/convolver (priority #2 after the amp).
// Uniform-partitioned juce::dsp::Convolution with zero added latency, so PDC
// is unaffected; normalised to unity energy on load. Stereo fan-out happens
// AFTER this block (scoping decision: mono through cab; dual-IR stereo is a
// later option).
//
// loadImpulseResponse is internally async + lock-free swapped by JUCE, so it's
// safe to call from the message thread while audio runs.

#include "Blocks.h"
#include "Biquad.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

namespace nam_rig
{

class CabBlock : public MonoBlock
{
public:
    const char *name() const override { return "Cab IR"; }

    void prepare(const BlockContext &ctx) override
    {
        mSpec.sampleRate = ctx.sampleRate;
        mSpec.maximumBlockSize = (juce::uint32)ctx.maxBlockSize;
        mSpec.numChannels = 1;
        mConv.prepare(mSpec);
        mCuts.prepare(ctx.sampleRate);
        mPrepared = true;
    }

    void reset() override
    {
        mConv.reset();
        mCuts.reset();
    }

    // Post-cab corrective cuts (Butterworth 12 dB/oct). Convention: HPF <= 20 Hz
    // is OFF, LPF >= 20 kHz is OFF (knob extremes = out of the path, bit-exact).
    void setHpfHz(float hz) { mHpfHz.store(hz); }
    void setLpfHz(float hz) { mLpfHz.store(hz); }
    const CutFilters &cuts() const { return mCuts; } // verification access

    // IR magnitude-response curve for the Cab panel display: kResPts points,
    // log-spaced kResFLo..kResFHi, 1/6-octave smoothed, in dB, centred on its own
    // mean (0 dB = the IR's average level). The range is the guitar band, not the
    // full 20-20k, since a cab does nothing musical above ~6 kHz. Computed once at
    // load (message thread); copyResponseDb returns false until an IR is loaded.
    static constexpr int kResPts = 200;
    static constexpr double kResFLo = 40.0, kResFHi = 8000.0;
    bool copyResponseDb(float *dst) const
    {
        if (!mResValid.load())
            return false;
        for (int i = 0; i < kResPts; ++i)
            dst[i] = mResDb[(size_t)i];
        return true;
    }

    // Load a cab IR (wav/aiff), resampled to the current rate by JUCE.
    // Normalised to UNITY ENERGY (1/sqrt(sum(ir^2))) — a unit impulse stays a
    // unit impulse and overall loudness is consistent across IRs. (JUCE's
    // Normalise::yes scales everything to 0.125 ≈ -18 dB instead — verified
    // empirically — so we normalise ourselves and pass Normalise::no.)
    // Returns false if the file doesn't exist or can't be read.
    bool loadIr(const juce::File &irFile)
    {
        if (!irFile.existsAsFile())
            return false;

        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(irFile));
        if (!reader || reader->lengthInSamples <= 0)
            return false;

        juce::AudioBuffer<float> ir(1, (int)reader->lengthInSamples);
        reader->read(&ir, 0, (int)reader->lengthInSamples, 0, true, false);

        double energy = 0.0;
        const float *d = ir.getReadPointer(0);
        for (int i = 0; i < ir.getNumSamples(); ++i)
            energy += (double)d[i] * d[i];
        if (energy > 0.0)
            ir.applyGain((float)(1.0 / std::sqrt(energy)));

        computeResponse(ir, reader->sampleRate); // capture the curve before the move

        mConv.loadImpulseResponse(std::move(ir), reader->sampleRate,
                                  juce::dsp::Convolution::Stereo::no,
                                  juce::dsp::Convolution::Trim::yes,
                                  juce::dsp::Convolution::Normalise::no);
        mIrName = irFile.getFileNameWithoutExtension();
        mIrLoaded.store(true);
        return true;
    }

    bool isIrLoaded() const { return mIrLoaded.load(); }
    juce::String irName() const { return mIrName; }

    void process(float *mono, int numSamples) override
    {
        if (!mPrepared)
            return;

        if (mIrLoaded.load()) // convolve only once an IR is loaded
        {
            juce::dsp::AudioBlock<float> block(&mono, 1, (size_t)numSamples);
            juce::dsp::ProcessContextReplacing<float> ctx(block);
            mConv.process(ctx);
        }

        // Post-cab cuts apply regardless of IR (they're the block's output
        // filters; with both at their extremes this is a no-op, bit-exact).
        mCuts.update((double)mHpfHz.load(), (double)mLpfHz.load());
        if (mCuts.engaged())
            mCuts.process(mono, numSamples);
    }

    double latencySamples() const override
    {
        // Uniform-partitioned convolution: zero added latency by construction.
        return mPrepared ? (double)mConv.getLatency() : 0.0;
    }

private:
    // 1/6-octave-smoothed magnitude response. A zero-padded FFT gives the dense
    // magnitude spectrum; each display point averages the POWER over a +/-1/12-
    // octave band (= 1/6 octave wide), which is what tames a cab IR's comb-filter
    // hash into a readable tonal curve. Then centre on the mean. Computed once at
    // load (message thread); the FFT length caps the IR used (~0.68 s) which
    // covers any real cab IR — the audio convolution still uses the full IR.
    void computeResponse(const juce::AudioBuffer<float> &ir, double fs)
    {
        constexpr int kOrder = 15;          // 32768-pt FFT
        constexpr int kN = 1 << kOrder;
        const int len = juce::jmin(ir.getNumSamples(), kN);
        std::vector<float> buf((size_t)kN * 2, 0.0f); // real in [0,kN); FFT writes mags
        const float *d = ir.getReadPointer(0);
        for (int i = 0; i < len; ++i)
            buf[(size_t)i] = d[i];
        juce::dsp::FFT(kOrder).performFrequencyOnlyForwardTransform(buf.data());

        const double binHz = fs / (double)kN;
        const int maxBin = kN / 2;
        const double edge = std::pow(2.0, 1.0 / 12.0); // half of a 1/6-octave band
        std::array<float, kResPts> db{};
        double sum = 0.0;
        for (int k = 0; k < kResPts; ++k)
        {
            const double f = kResFLo * std::pow(kResFHi / kResFLo, (double)k / (double)(kResPts - 1));
            int lo = (int)std::floor(f / edge / binHz);
            int hi = (int)std::ceil(f * edge / binHz);
            lo = juce::jlimit(1, maxBin, lo);
            hi = juce::jlimit(1, maxBin, hi);
            if (hi < lo) hi = lo;
            double p = 0.0;
            for (int b = lo; b <= hi; ++b)
                p += (double)buf[(size_t)b] * (double)buf[(size_t)b];
            const double meanP = p / (double)(hi - lo + 1);
            const float v = (float)(10.0 * std::log10(meanP + 1.0e-24));
            db[(size_t)k] = v;
            sum += v;
        }
        const float mean = (float)(sum / (double)kResPts);
        for (auto &v : db)
            v -= mean; // 0 dB = the IR's own average level
        mResDb = db;
        mResValid.store(true);
    }

    juce::dsp::Convolution mConv; // default: uniform-partitioned, zero latency
    juce::dsp::ProcessSpec mSpec{48000.0, 512, 1};
    CutFilters mCuts;
    std::array<float, kResPts> mResDb{};   // IR magnitude curve (message thread)
    std::atomic<bool> mResValid{false};
    std::atomic<float> mHpfHz{20.0f};    // <= 20 = off
    std::atomic<float> mLpfHz{20000.0f}; // >= 20k = off
    juce::String mIrName;
    std::atomic<bool> mIrLoaded{false};
    bool mPrepared = false;
};

} // namespace nam_rig
