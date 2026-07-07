#pragma once
// LfResonance — estimate a captured cab IR's LOW-FREQUENCY resonance (the
// driver-in-box "thump" knee) from the mean-centred 1/6-octave magnitude curve
// that IrAnalysis::computeResponse already produces. JUCE-free (pure array math)
// so it is unit-testable in the offline g++ harness and #included by both
// IrAnalysis.h and the test. See docs/cabdyn/PHYSICS_UPGRADE.md §4.
//
// What this measures — honestly: the CAPTURED SYSTEM's LF resonance (driver in
// its box, as mic'd, proximity and all), NOT the driver's free-air datasheet Fs.
// That captured frequency is exactly what the thump/excursion model should key
// on, even when it differs from the datasheet number. CabDynamicsBlock consumes
// {fsHz, promDb} via setSpeakerResonance (fsHz retunes the excursion LP2 / A1 /
// B3 / modal centers; promDb couples the box-bump prominence into A1's base Q).
//
// Method (operates on the existing 200-pt log grid 25..8000 Hz):
//   1. one (1-2-1)/4 binomial smoothing pass (kills residual comb hash),
//   2. find the highest strictly-INTERIOR local max in 32..230 Hz (edge max is
//      not a resonance -> invalid),
//   3. prominence = peak - mean(260..600 Hz reference zone); require the curve
//      to FALL BELOW the peak on the low side. The low-side fall test is
//      EDGE-AWARE: with a full window (>=0.5 oct of data below the peak) it
//      still requires >=1.5 dB down so a mic-proximity bass SHELF is not
//      mistaken for a box knee; for edge-adjacent bass peaks (0.25..0.5 oct of
//      data, grid edge truncates the octave) it requires >=1.0 dB; below 0.25
//      oct of data it cannot be distinguished from a shelf -> invalid,
//   4. parabolic (3-point) refinement of the peak position in log-f,
//   5. valid <=> interior peak AND promDb >= 1.0 AND edge-aware low-side fall;
//      clamp fsHz to [32,200], promDb to [0,12].

#include <cmath>

namespace nam_rig::ir
{

struct LfEstimate
{
    float fsHz = 90.0f;   // captured LF resonance (Hz); default = generic 90
    float promDb = 0.0f;  // box-bump prominence vs the 260..600 Hz zone (dB)
    bool  valid = false;  // false => caller falls back to the 90 Hz default
};

// Estimate the LF resonance from a mean-centred dB response `respDb[0..nPts)`
// spanning [fLo, fHi] Hz log-spaced (the IrAnalysis grid: fLo=25, fHi=8000,
// nPts=200). Pure array math; safe for small/degenerate inputs.
inline LfEstimate estimateLfResonance(const float *respDb, int nPts,
                                      float fLo, float fHi)
{
    LfEstimate est; // defaults: 90 Hz / 0 dB / invalid
    if (respDb == nullptr || nPts < 8 || !(fHi > fLo) || fLo <= 0.0f)
        return est;

    // log-frequency helpers: index<->Hz on the log-spaced grid.
    const double lr = std::log((double)fHi / (double)fLo);
    auto freqAt = [&](double idx) {
        return (double)fLo * std::exp(lr * idx / (double)(nPts - 1));
    };
    auto idxAt = [&](double hz) {
        return (double)(nPts - 1) * std::log(hz / (double)fLo) / lr;
    };

    // 1) binomial (1-2-1)/4 smoothing pass into a local buffer (endpoints held).
    //    Bounded stack scratch (the grid is 200 pts; guard generously).
    constexpr int kMax = 512;
    if (nPts > kMax) return est; // never happens for the 200-pt grid
    float sm[kMax];
    sm[0] = respDb[0];
    sm[nPts - 1] = respDb[nPts - 1];
    for (int i = 1; i < nPts - 1; ++i)
        sm[i] = 0.25f * respDb[i - 1] + 0.5f * respDb[i] + 0.25f * respDb[i + 1];

    // search window 32..230 Hz, mapped to interior grid indices.
    int iLoSrch = (int)std::ceil(idxAt(32.0));
    int iHiSrch = (int)std::floor(idxAt(230.0));
    if (iLoSrch < 1) iLoSrch = 1;                 // interior only (need i-1)
    if (iHiSrch > nPts - 2) iHiSrch = nPts - 2;   // interior only (need i+1)
    if (iHiSrch < iLoSrch) return est;

    // 2) highest strictly-interior local max in the window.
    int pk = -1;
    float pkVal = -1.0e30f;
    for (int i = iLoSrch; i <= iHiSrch; ++i)
    {
        if (sm[i] > sm[i - 1] && sm[i] > sm[i + 1] && sm[i] > pkVal)
        {
            pkVal = sm[i];
            pk = i;
        }
    }
    if (pk < 0) return est; // no interior peak (monotone / edge max) -> invalid

    // 3a) prominence vs the 260..600 Hz reference zone (mean of the smoothed curve).
    {
        int z0 = (int)std::round(idxAt(260.0));
        int z1 = (int)std::round(idxAt(600.0));
        if (z0 < 0) z0 = 0;
        if (z1 > nPts - 1) z1 = nPts - 1;
        if (z1 < z0) z1 = z0;
        double zs = 0.0;
        for (int i = z0; i <= z1; ++i) zs += (double)sm[i];
        const double zoneMean = zs / (double)(z1 - z0 + 1);
        est.promDb = (float)((double)pkVal - zoneMean);
    }

    // 3b) low-side fall: the curve must fall BELOW the peak on the low side
    //     (distinguishes a box knee from a rising mic-proximity bass shelf).
    //     EDGE-AWARE: with the widened 25 Hz grid a low bass peak (35 Hz) has
    //     less than a full octave of data below it before the grid edge, so the
    //     required fall relaxes as the available window shrinks; below 0.25 oct
    //     of data the peak cannot be told apart from a shelf -> invalid.
    float lowFall = 0.0f;
    double winOct = 0.0;
    {
        const double fpk = freqAt((double)pk);
        int l0 = (int)std::round(idxAt(fpk / 2.0)); // one octave below the peak
        int l1 = pk - 1;
        if (l0 < 0) l0 = 0;                          // clamp to grid low edge
        if (l1 < l0) l1 = l0;
        winOct = std::log2(fpk / freqAt((double)l0)); // octaves actually below peak
        double ls = 0.0;
        for (int i = l0; i <= l1; ++i) ls += (double)sm[i];
        const double lowMean = ls / (double)(l1 - l0 + 1);
        lowFall = (float)((double)pkVal - lowMean); // >0 => curve falls below the peak
    }
    // edge-aware low-fall threshold (see header step 3):
    //   >= 0.5  oct data -> require >= 1.5 dB (full window, guitar cabs)
    //   >= 0.25 oct data -> require >= 1.0 dB (edge-adjacent bass peaks)
    //   <  0.25 oct data -> invalid (cannot distinguish from a shelf)
    bool lowFallOk;
    if      (winOct >= 0.5)  lowFallOk = (lowFall >= 1.5f);
    else if (winOct >= 0.25) lowFallOk = (lowFall >= 1.0f);
    else                     lowFallOk = false;

    // 4) parabolic refinement of the peak position in log-f (3-point vertex).
    double pkIdx = (double)pk;
    {
        const double ym1 = (double)sm[pk - 1];
        const double y0  = (double)sm[pk];
        const double yp1 = (double)sm[pk + 1];
        const double den = (ym1 - 2.0 * y0 + yp1);
        if (den < -1.0e-12) // concave (a real max)
        {
            double d = 0.5 * (ym1 - yp1) / den;
            if (d > 1.0) d = 1.0;
            if (d < -1.0) d = -1.0;
            pkIdx = (double)pk + d;
        }
    }
    double fs = freqAt(pkIdx);

    // 5) validity + clamps.
    est.valid = (est.promDb >= 1.0f) && lowFallOk;
    if (fs < 32.0)  fs = 32.0;
    if (fs > 200.0) fs = 200.0;
    est.fsHz = (float)fs;
    if (est.promDb < 0.0f)  est.promDb = 0.0f;
    if (est.promDb > 12.0f) est.promDb = 12.0f;
    return est;
}

} // namespace nam_rig::ir
