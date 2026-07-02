#pragma once
// Pure routing/latency math shared by the plugin (PluginProcessor) and the
// offline latency test (tests/latency_test.cpp). No JUCE dependencies.

#include <algorithm>
#include <cmath>

namespace nam_aa
{

// Extra Rsrc-rate samples held in the down-resampler's working buffer to cover the
// interpolator kernel width + fractional jitter, so the down stage never under-reads.
inline constexpr int kSrcKernelGuard = 4;

// Resolved signal routing for a given DAW rate / AA setting / model availability.
// The models always run at fixed power-of-2 multiples of 48k (1x=48k .. 32x=1.536M);
// only this layer adapts.
struct Routing
{
    double Rmodel = 48000.0; // rate the model runs at (48k .. 1536k)
    double Rsrc = 48000.0;   // DAW-family rate at the FIR input (48k or 96k)
    int firFactor = 1;       // 1=bypass, 2/4/8/16/32 = FIR oversampling Rsrc -> Rmodel
    bool useCatmull = false; // DAW rate needs SRC to reach Rsrc
    int modelFactor = 1;     // which dilation-scaled model runs (1,2,4,8,16,32)
};

inline constexpr int kMaxAAFactor = 32;

// Resolve how the signal flows for the current DAW rate. The models are fixed:
//   factor f -> 48k*f. Everything else is conversion. Factors >= 8 are intended
//   for offline rendering (isNonRealtime).
// maxFactor: highest dilation-scaled model available (1 if not an A2 model).
inline Routing computeRouting(double dawRate, int requestedFactor, int maxFactor)
{
    Routing r;

    // At >72k (88.2/96k), running the 2x model directly is free AA — bump to at
    // least 2x when available, regardless of the toggle.
    int factor = requestedFactor;
    if (dawRate > 72000.0 && maxFactor >= 2 && factor < 2)
        factor = 2;

    // Clamp to what's loaded (power of 2, halving fallback).
    factor = std::min(factor, std::min(maxFactor, kMaxAAFactor));
    while (factor > 1 && (factor & (factor - 1)) != 0)
        factor--; // non-pow2 request: fall to the next pow2 below

    r.modelFactor = factor;
    r.Rmodel = 48000.0 * factor;

    // Rsrc: DAW-family rate at the FIR input (sinc SRC target).
    // 44.1/48k → 48k family; 88.2/96k → 96k family.
    r.Rsrc = (dawRate > 72000.0) ? 96000.0 : 48000.0;

    // FIR factor: integer ratio from Rsrc to Rmodel.
    // e.g. 48k→96k=2, 48k→192k=4, 96k→192k=2, 96k→96k=1 (direct).
    if (factor <= 1)
        r.firFactor = 1;
    else
        r.firFactor = static_cast<int>(std::round(r.Rmodel / r.Rsrc));

    // SRC: needed when DAW rate doesn't match Rsrc.
    r.useCatmull = std::abs(dawRate - r.Rsrc) > 1.0;

    return r;
}

// Latency, in DAW-rate samples, of the resolved chain.
//   osLatencyRsrc:       oversampler latency reported at Rsrc rate (0 if firFactor == 1)
//   srcChainLatencyDaw:  SrcLayer::chainLatencyDawSamples() — the full SRC
//                        contribution in DAW samples, mode-aware (polyphase or
//                        sinc fallback). 0 if !useCatmull.
// Verified by tests/latency_test.cpp: measured latency matches this expression
// to < 0.5 samples at all rates and block-size patterns.
inline double latencySamples(const Routing &r, double dawRate,
                             double osLatencyRsrc, double srcChainLatencyDaw)
{
    double latency = 0.0;

    if (r.firFactor >= 2)
        latency += osLatencyRsrc * (dawRate / r.Rsrc);

    if (r.useCatmull)
        latency += srcChainLatencyDaw;

    return latency;
}

} // namespace nam_aa
