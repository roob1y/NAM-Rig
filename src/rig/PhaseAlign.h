#pragma once
// PhaseAlign — broadband time/polarity alignment for the two dual-rig voices.
//
// Both rigs are fed the SAME split signal, so a delay mismatch between their
// outputs (different cab IRs / models / AA filters) shows up as a lag in the
// cross-correlation of the two rendered voices. We align on the OUTPUTS, not on
// the IR files, so it works whether the cab is a separate IR or baked into the
// .nam (a full-rig capture). The amp is nonlinear, but time delay is
// level-independent and both voices see identical input, so the correlation
// peak still lands on the true offset.
//
// This is broadband TIME + POLARITY alignment only (kills comb-filter
// cancellation when summed/centered); it deliberately does NOT match phase
// per-frequency, which would flatten the tonal difference between the amps.
//
// Pure (no JUCE) so the measurement is unit-tested directly (tests/dualrig_test).

#include <cmath>

namespace nam_rig
{

struct AlignResult
{
    double lagSamples = 0.0; // how far B is DELAYED vs A (>0 => B later than A)
    bool invert = false;     // true => flip one rig's polarity (peak corr < 0)
    double peakCorr = 0.0;   // signed correlation at the peak (for diagnostics)
};

struct PhaseAlign
{
    // Signed cross-correlation of b shifted by L against a, over the overlap.
    static double corrAtLag(const float *a, const float *b, int n, int L)
    {
        const int i0 = (L < 0) ? -L : 0;
        const int i1 = (L < 0) ? n : (n - L);
        double s = 0.0;
        for (int i = i0; i < i1; ++i)
            s += (double)a[i] * (double)b[i + L];
        return s;
    }

    // Find the integer lag of maximum |correlation| in [-maxLag, maxLag], then
    // refine to sub-sample with a 3-point parabolic fit. lagSamples > 0 means
    // B lags A (delay A to align); < 0 means A lags B (delay B).
    static AlignResult measure(const float *a, const float *b, int n, int maxLag)
    {
        AlignResult r;
        int bestLag = 0;
        double bestAbs = -1.0, bestSigned = 0.0;
        for (int L = -maxLag; L <= maxLag; ++L)
        {
            const double s = corrAtLag(a, b, n, L);
            if (std::fabs(s) > bestAbs)
            {
                bestAbs = std::fabs(s);
                bestSigned = s;
                bestLag = L;
            }
        }
        r.invert = (bestSigned < 0.0);
        r.peakCorr = bestSigned;

        // Parabolic interpolation on the sign-corrected correlation curve.
        const double sgn = (bestSigned < 0.0) ? -1.0 : 1.0;
        const double cL = sgn * corrAtLag(a, b, n, bestLag - 1);
        const double c0 = sgn * bestSigned; // == bestAbs
        const double cR = sgn * corrAtLag(a, b, n, bestLag + 1);
        const double denom = (cL - 2.0 * c0 + cR);
        double frac = 0.0;
        if (std::fabs(denom) > 1e-12)
            frac = 0.5 * (cL - cR) / denom; // vertex offset in [-0.5, 0.5]
        if (frac > 1.0) frac = 1.0;
        if (frac < -1.0) frac = -1.0;
        r.lagSamples = (double)bestLag + frac;
        return r;
    }
};

} // namespace nam_rig
