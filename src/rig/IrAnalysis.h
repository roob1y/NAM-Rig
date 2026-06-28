#pragma once
// IrAnalysis — single source of truth for a cab IR's tonal analysis: the
// 1/6-octave magnitude response, the per-zone energies, and the 1-2 word tone
// tag. Used by CabBlock (the loaded IR's graph) AND the IR-library tag cache, so
// a file shows the SAME tag in the browser as it does once loaded.

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <vector>
#include <cmath>

namespace nam_rig::ir
{

static constexpr int kResPts = 200;               // response points (log-spaced)
static constexpr double kResFLo = 40.0, kResFHi = 8000.0; // guitar band

// 1/6-octave-smoothed magnitude response (mean-centred dB) from raw IR samples.
// outDb must hold kResPts floats. A zero-padded FFT gives the dense spectrum;
// each point averages POWER over a +/-1/12-octave band (tames comb hash).
inline void computeResponse(const float *d, int len, double fs, float *outDb)
{
    constexpr int kOrder = 15;     // 32768-pt FFT
    constexpr int kN = 1 << kOrder;
    const int n = juce::jmin(len, kN);
    std::vector<float> buf((size_t)kN * 2, 0.0f); // real in [0,kN); FFT writes mags
    for (int i = 0; i < n; ++i) buf[(size_t)i] = d[i];
    juce::dsp::FFT(kOrder).performFrequencyOnlyForwardTransform(buf.data());

    const double binHz = fs / (double)kN;
    const int maxBin = kN / 2;
    const double edge = std::pow(2.0, 1.0 / 12.0); // half of a 1/6-octave band
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
        for (int b = lo; b <= hi; ++b) p += (double)buf[(size_t)b] * (double)buf[(size_t)b];
        const float v = (float)(10.0 * std::log10(p / (double)(hi - lo + 1) + 1.0e-24));
        outDb[k] = v;
        sum += v;
    }
    const float mean = (float)(sum / (double)kResPts);
    for (int k = 0; k < kResPts; ++k) outDb[k] -= mean; // 0 dB = IR's own average
}

// Average response over a frequency zone (relative dB).
inline float zoneAvg(const float *resp, double a, double b)
{
    const double lr = std::log(kResFHi / kResFLo);
    int i0 = juce::jlimit(0, kResPts - 1, (int)std::round((double)(kResPts - 1) * std::log(a / kResFLo) / lr));
    int i1 = juce::jlimit(0, kResPts - 1, (int)std::round((double)(kResPts - 1) * std::log(b / kResFLo) / lr));
    float s = 0.0f;
    for (int i = i0; i <= i1; ++i) s += resp[i];
    return s / (float)(i1 - i0 + 1);
}

// 1-2 word tone descriptor from a response (see CabPanel heat-map zones).
inline juce::String classify(const float *resp)
{
    const float lows = zoneAvg(resp, 40, 120), body = zoneAvg(resp, 120, 400),
                mids = zoneAvg(resp, 400, 1500), pres = zoneAvg(resp, 1500, 4000),
                fizz = zoneAvg(resp, 4000, 8000);
    const float tilt = 0.5f * (pres + fizz) - 0.5f * (lows + body);
    const float scoop = 0.5f * (body + pres) - mids;

    const char *t1 = (tilt > 3.0f) ? "Bright" : (tilt < -3.0f ? "Dark" : nullptr);
    struct C { const char *w; float s; };
    const C cs[] = {{"thick", body - 3.0f}, {"scooped", scoop - 4.0f}, {"present", pres - 3.0f},
                    {"fizzy", fizz - 4.0f}, {"boomy", lows - 5.0f}, {"mid-forward", mids - 3.0f}};
    const char *t2 = nullptr;
    float best = 0.0f;
    for (auto &c : cs) if (c.s > best) { best = c.s; t2 = c.w; }

    auto cap = [](const char *w) { juce::String s(w); return s.substring(0, 1).toUpperCase() + s.substring(1); };
    if (t1 == nullptr && t2 == nullptr) return "Balanced";
    if (t2 != nullptr && (juce::String(t2) == "fizzy" || juce::String(t2) == "present")) return cap(t2);
    if (t1 != nullptr && t2 != nullptr) return juce::String(t1) + ", " + t2;
    if (t1 != nullptr) return juce::String(t1);
    return cap(t2);
}

// Read an IR file (first ~0.68 s) and compute its response. outDb holds kResPts.
inline bool analyzeFile(const juce::File &f, float *outDb)
{
    if (!f.existsAsFile()) return false;
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r(fm.createReaderFor(f));
    if (r == nullptr || r->lengthInSamples <= 0) return false;
    const int len = (int)juce::jmin((juce::int64)(1 << 15), r->lengthInSamples);
    juce::AudioBuffer<float> ir(1, len);
    r->read(&ir, 0, len, 0, true, false);
    computeResponse(ir.getReadPointer(0), len, r->sampleRate, outDb);
    return true;
}

// Convenience: the tone tag for a file ("" if it can't be read).
inline juce::String tagForFile(const juce::File &f)
{
    std::array<float, kResPts> resp{};
    if (!analyzeFile(f, resp.data())) return {};
    return classify(resp.data());
}

} // namespace nam_rig::ir
