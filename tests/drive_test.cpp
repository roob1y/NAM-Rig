// drive_test — offline verification harness for DriveBlock (measurement-first).
// Exits nonzero on any FAIL. Covers the reworked voicings: Off bit-exactness,
// the Overdrive mid-hump, Treble-Boost brightness, Fuzz asymmetry, the tone
// tilt, and that the clippers' ANTIDERIVATIVE anti-aliasing (ADAA) suppresses
// fold-back vs a naive memoryless shaper using identical voicing constants
// (DriveBlock::voicingFor is the shared source of truth).
#include "rig/DriveBlock.h"
#include "rig/Biquad.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>

using nam_rig::DriveBlock;
using nam_rig::Biquad;
using Kind = nam_rig::DriveBlock::Kind;

static int gFails = 0;
#define CHECK(cond, ...) do { \
    const bool ok_ = (cond); \
    std::printf("%s: ", ok_ ? "PASS" : "FAIL"); std::printf(__VA_ARGS__); std::printf("\n"); \
    if (!ok_) ++gFails; } while (0)

static constexpr double SR = 48000.0;
static constexpr int BLK = 512;

static void run(DriveBlock &d, std::vector<float> &x)
{
    for (size_t p = 0; p < x.size(); p += BLK)
        d.process(x.data() + p, (int)std::min<size_t>(BLK, x.size() - p));
}
static std::vector<float> sine(double f, float amp, int n)
{
    std::vector<float> v((size_t)n);
    for (int i = 0; i < n; ++i)
        v[(size_t)i] = amp * (float)std::sin(2.0 * M_PI * f * i / SR);
    return v;
}
static double goertzel(const std::vector<float> &x, double freq)
{
    const int n = (int)x.size();
    const double w = 2.0 * M_PI * freq / SR, coeff = 2.0 * std::cos(w);
    double s1 = 0, s2 = 0;
    for (int i = 0; i < n; ++i) { double s0 = x[(size_t)i] + coeff * s1 - s2; s2 = s1; s1 = s0; }
    return std::sqrt(s1 * s1 + s2 * s2 - coeff * s1 * s2) / (n * 0.5);
}
static double rms(const std::vector<float> &x)
{
    double e = 0; for (float v : x) e += (double)v * v; return std::sqrt(e / x.size());
}

// Naive (memoryless) mirror of one DriveBlock slot, sharing the exact voicing
// constants — identical to the block EXCEPT the shaper is pointwise (no ADAA).
static std::vector<float> naiveSlot(Kind k, float drive, const std::vector<float> &in)
{
    const auto v = DriveBlock::voicingFor(k);
    const float preGain = v.gMin * std::pow(v.gMax / v.gMin, drive);
    const float hpCoef = (v.lowCutHz > 0.0f) ? 1.0f - (float)std::exp(-2.0 * M_PI * v.lowCutHz / SR) : 0.0f;
    const float lpCoef = (v.lpHz > 0.0f) ? 1.0f - (float)std::exp(-2.0 * M_PI * v.lpHz / SR) : 0.0f;
    const bool useMid = (v.midDb != 0.0f), useLp = (v.lpHz > 0.0f);
    Biquad mid = useMid ? Biquad::peaking(SR, v.midHz, v.midQ, v.midDb) : Biquad::identity();
    const double asym = (v.clip == 2) ? (double)v.bias : 0.0;
    const double inBias = (v.clip == 2) ? 0.0 : (double)v.bias;
    const float shapeAmt = 1.0f - v.shapeTrack * (1.0f - drive);
    const bool midPost = (v.midPost > 0.5f);
    auto clipF = [&](double x) -> double {
        if (v.clip == 0) return std::tanh(x);
        if (v.clip == 1) return x > 1.0 ? 1.0 : (x < -1.0 ? -1.0 : x);
        const double lo = 1.0 - asym; return x >= 0.0 ? std::tanh(x) : (x < -lo ? -lo : x);
    };
    const float kDcR = 0.9995f;
    std::vector<float> y(in.size());
    float hp = 0, lpz = 0, dcx = 0, dcy = 0;
    for (size_t i = 0; i < in.size(); ++i)
    {
        float u = in[i] * preGain;
        if (hpCoef > 0.0f) { hp += hpCoef * (u - hp); float hipassed = u - hp; u += shapeAmt * (hipassed - u); }
        if (useMid && !midPost) { float m = mid.processSample(u); u += shapeAmt * (m - u); }
        float c = (float)clipF((double)u + inBias);
        if (useMid && midPost) { float m = mid.processSample(c); c += shapeAmt * (m - c); }
        if (useLp) { lpz += lpCoef * (c - lpz); c += shapeAmt * (lpz - c); }
        float dcOut = c - dcx + kDcR * dcy; dcx = c; dcy = dcOut; c = dcOut;
        y[i] = c * v.outTrim; // tone flat, level 0 dB
    }
    return y;
}

static std::vector<float> realSlot(Kind k, float drive, const std::vector<float> &in)
{
    DriveBlock d;
    d.setKind(0, (int)k); d.setDrive(0, drive); d.setTone(0, 0.5f); d.setLevelDb(0, 0.0f);
    d.prepare({SR, BLK});
    auto x = in; run(d, x); return x;
}

// Same, but selects a MODEL within the category (A/B of OD v1 vs v2).
static std::vector<float> realSlotM(Kind k, int model, float drive, const std::vector<float> &in)
{
    DriveBlock d;
    d.setKind(0, (int)k); d.setModel(0, model); d.setDrive(0, drive);
    d.setTone(0, 0.5f); d.setLevelDb(0, 0.0f);
    d.prepare({SR, BLK});
    auto x = in; run(d, x); return x;
}

// Memoryless mirror of the cubic core (Overdrive model 1), sharing its voicing
// pre-gain -- NO 2nd-order ADAA. Alias baseline for the cubic.
static std::vector<float> naiveCubic(float drive, const std::vector<float> &in)
{
    const auto v = DriveBlock::voicingFor(Kind::Overdrive, 1);
    const float pg = v.gMin * std::pow(v.gMax / v.gMin, drive);
    auto f = [](double x) { if (x > 1.0) return 2.0 / 3.0; if (x < -1.0) return -2.0 / 3.0; return x - x * x * x / 3.0; };
    std::vector<float> y(in.size());
    for (size_t i = 0; i < in.size(); ++i) y[i] = (float)f((double)in[i] * pg);
    return y;
}

// harmonic energy (n=2..N) over the fundamental -- a THD-ish "how dirty" measure.
static double harmRatio(const std::vector<float> &y, double f0, int N)
{
    double fund = goertzel(y, f0), h = 0.0;
    for (int n = 2; n <= N; ++n) h += goertzel(y, f0 * n);
    return h / (fund + 1e-9);
}

// Small-signal magnitude at one freq relative to a reference freq (dB), at a
// given Drive — reveals the EQ shape (low amplitude keeps the shaper ~linear).
static double respDb(Kind k, float drive, double f, double refF)
{
    auto inF = sine(f, 0.01f, 16384), inR = sine(refF, 0.01f, 16384);
    auto yF = realSlot(k, drive, inF), yR = realSlot(k, drive, inR);
    const double gF = goertzel(yF, f) / goertzel(inF, f);
    const double gR = goertzel(yR, refF) / goertzel(inR, refF);
    return 20.0 * std::log10(gF / gR);
}

int main()
{
    // ---- T1: all-Off rack is bit-exact passthrough ----
    {
        DriveBlock d; d.prepare({SR, BLK});
        auto in = sine(220.0, 0.3f, 4096);
        auto x = in; run(d, x);
        bool exact = true;
        for (size_t i = 0; i < in.size(); ++i) exact = exact && (x[i] == in[i]);
        CHECK(exact, "T1 all-Off rack is bit-exact passthrough");
        CHECK(!d.anyActive(), "T1 anyActive() false when all slots Off");
    }

    // ---- T2: Overdrive has a midrange hump (the mid-hump signature) ----
    {
        const auto v = DriveBlock::voicingFor(Kind::Overdrive);
        Biquad mid = Biquad::peaking(SR, v.midHz, v.midQ, v.midDb);
        const double m720 = mid.magnitudeAt(SR, 720.0);
        const double m200 = mid.magnitudeAt(SR, 200.0);
        const double m5k  = mid.magnitudeAt(SR, 5000.0);
        CHECK(m720 > m200 * 1.3 && m720 > m5k * 1.3,
              "T2 OD mid-hump: |H|@720=%.2f > 200=%.2f / 5k=%.2f", m720, m200, m5k);
    }

    // ---- T3: Treble Boost is brighter (3 kHz passes, 150 Hz is cut) ----
    {
        auto loIn = sine(150.0, 0.05f, 8192), hiIn = sine(3000.0, 0.05f, 8192);
        auto lo = realSlot(Kind::Boost, 0.3f, loIn);
        auto hi = realSlot(Kind::Boost, 0.3f, hiIn);
        const double g150 = goertzel(lo, 150.0), g3k = goertzel(hi, 3000.0);
        CHECK(g3k > g150 * 1.5, "T3 treble boost brighter: 3k %.3f > 150Hz %.3f", g3k, g150);
    }

    // ---- T4: ADAA suppresses aliasing vs naive (Fuzz hard clip, hot) ----
    {
        auto in = sine(5000.0, 0.05f, 48000); // odd harmonics fold to 3 k / 13 k
        auto adaa = realSlot(Kind::Fuzz, 1.0f, in);
        auto naive = naiveSlot(Kind::Fuzz, 1.0f, in);
        const double a3 = goertzel(adaa, 3000.0),  n3 = goertzel(naive, 3000.0);
        const double a13 = goertzel(adaa, 13000.0), n13 = goertzel(naive, 13000.0);
        CHECK(a3 < n3 * 0.6 && a13 < n13 * 0.6,
              "T4 ADAA cuts alias: 3k %.2e<%.2e, 13k %.2e<%.2e", a3, n3, a13, n13);
        const double redDb = 20.0 * std::log10(n3 / std::max(a3, 1e-12));
        CHECK(redDb > 4.0, "T4 alias@3k reduced by %.1f dB", redDb);
    }

    // ---- T5: ADAA preserves the wanted low-freq signal (matches naive) ----
    {
        auto in = sine(100.0, 0.1f, 8192);
        auto adaa = realSlot(Kind::Fuzz, 0.5f, in);
        auto naive = naiveSlot(Kind::Fuzz, 0.5f, in);
        std::vector<float> diff(in.size());
        for (size_t i = 0; i < in.size(); ++i) diff[i] = adaa[i] - naive[i];
        const double rel = rms(diff) / std::max(rms(adaa), 1e-9);
        CHECK(rel < 0.05, "T5 ADAA ~= naive at 100 Hz (rel err %.3f)", rel);
    }

    // ---- T6: Fuzz is more asymmetric (even-harmonic) than Distortion ----
    {
        auto in = sine(220.0, 0.1f, 24000);
        auto fz = realSlot(Kind::Fuzz, 0.7f, in);
        auto ds = realSlot(Kind::Distortion, 0.7f, in);
        // Even-harmonic content vs the fundamental = asymmetry. Fuzz (bias 0.45)
        // should sit clearly above the symmetric Distortion (bias 0).
        auto h2overH1 = [&](const std::vector<float> &y) {
            return goertzel(y, 440.0) / (goertzel(y, 220.0) + 1e-9);
        };
        CHECK(h2overH1(fz) > 0.03 && h2overH1(fz) > h2overH1(ds) * 5.0,
              "T6 Fuzz asymmetric (soft/hard) vs symmetric Dist (h2/h1 %.3f vs %.3f)", h2overH1(fz), h2overH1(ds));
    }

    // ---- T7: tone tilt up = brighter (small-signal, above the pivot) ----
    {
        auto in = sine(2000.0, 0.01f, 16384);
        auto hi = in, lo = in;
        DriveBlock a, b;
        a.setKind(0, (int)Kind::Overdrive); a.setDrive(0, 0.5f); a.setTone(0, 0.9f); a.prepare({SR, BLK}); run(a, hi);
        b.setKind(0, (int)Kind::Overdrive); b.setDrive(0, 0.5f); b.setTone(0, 0.1f); b.prepare({SR, BLK}); run(b, lo);
        const double hiT = goertzel(hi, 2000.0), loT = goertzel(lo, 2000.0);
        CHECK(hiT > loT * 1.5, "T7 tone up = brighter at 2kHz (%.2e > %.2e)", hiT, loT);
    }

    // ---- T8: Overdrive at DRIVE 0 stays full-range (no band-pass scoop) ----
    {
        const double lo = respDb(Kind::Overdrive, 0.0f, 100.0, 700.0);   // 100 Hz vs 700 Hz
        const double hi = respDb(Kind::Overdrive, 0.0f, 3000.0, 700.0);  // 3 kHz vs 700 Hz
        CHECK(lo > -3.0 && hi > -4.0,
              "T8 OD@drive0 full-range: 100Hz %.1f dB, 3k %.1f dB (vs 700Hz)", lo, hi);
    }

    // ---- T9: Overdrive at DRIVE 1 blooms the 720 Hz mid-hump ----
    {
        const double vsLow = respDb(Kind::Overdrive, 1.0f, 700.0, 100.0);  // 700 vs 100
        const double vsHigh = respDb(Kind::Overdrive, 1.0f, 700.0, 3000.0); // 700 vs 3k
        CHECK(vsLow > 3.0 && vsHigh > 3.0,
              "T9 OD@drive1 mid-hump: 700Hz +%.1f vs 100Hz, +%.1f vs 3k", vsLow, vsHigh);
    }

    // ---- T10: Black Rodent (Distortion) blooms like the real RAT gain stage ----
    // Bass-cut + hump live in the gain feedback: flat at Drive 0, tightening
    // (mid-forward, bass pulled out) as Drive climbs.
    {
        const double lo0 = respDb(Kind::Distortion, 0.0f, 100.0, 1000.0); // ~flat at drive 0
        const double lo1 = respDb(Kind::Distortion, 1.0f, 100.0, 1000.0); // bass cut when driven
        CHECK(lo0 > -2.0, "T10 Dist@drive0 full-range: 100Hz %.1f dB (vs 1k)", lo0);
        CHECK(lo1 < -6.0 && lo1 < lo0 - 4.0,
              "T10 Dist@drive1 tightens: 100Hz %.1f dB (vs 1k), %.1f at drive0", lo1, lo0);
    }

    // ====== Green Drive II (Overdrive model 1): reworked feedback-clip OD ======

    // ---- T11: model 0 (tanh) byte-for-byte unchanged; category now has 2 models ----
    {
        auto in = sine(220.0, 0.2f, 8192);
        auto m0 = realSlotM(Kind::Overdrive, 0, 0.7f, in);
        auto def = realSlot(Kind::Overdrive, 0.7f, in); // default model == 0
        bool same = true;
        for (size_t i = 0; i < in.size(); ++i) same = same && (m0[i] == def[i]);
        CHECK(same, "T11 OD model 0 == legacy default (A/B preserves the original)");
        CHECK(DriveBlock::modelCount(Kind::Overdrive) == 2, "T11 Overdrive holds 2 models (v1 + v2)");
    }

    // ---- T12: 2nd-order ADAA on the cubic crushes alias vs a naive cubic ----
    {
        auto in = sine(5000.0, 0.05f, 48000); // odd harmonics fold to 3 k / 13 k
        auto adaa = realSlotM(Kind::Overdrive, 1, 1.0f, in);
        auto naive = naiveCubic(1.0f, in);
        const double a3 = goertzel(adaa, 3000.0), n3 = goertzel(naive, 3000.0);
        const double a13 = goertzel(adaa, 13000.0), n13 = goertzel(naive, 13000.0);
        CHECK(a3 < n3 * 0.2 && a13 < n13 * 0.5,
              "T12 ADAA2 cuts alias: 3k %.2e<%.2e, 13k %.2e<%.2e", a3, n3, a13, n13);
        const double redDb = 20.0 * std::log10(n3 / std::max(a3, 1e-12));
        CHECK(redDb > 15.0, "T12 alias@3k reduced by %.1f dB (2nd-order)", redDb);
    }

    // ---- T13: envelope dynamics -- soft picking cleans up, digging in bites ----
    // Strongest at the edge of breakup around the mid hump. v2 (dynDepth>0) opens
    // up far more between a quiet and a loud burst than v1 (no dynamics) does.
    {
        auto quiet = sine(660.0, 0.03f, 24000), loud = sine(660.0, 0.40f, 24000);
        const double q1 = harmRatio(realSlotM(Kind::Overdrive, 1, 0.35f, quiet), 660.0, 8);
        const double l1 = harmRatio(realSlotM(Kind::Overdrive, 1, 0.35f, loud),  660.0, 8);
        const double q0 = harmRatio(realSlotM(Kind::Overdrive, 0, 0.35f, quiet), 660.0, 8);
        const double l0 = harmRatio(realSlotM(Kind::Overdrive, 0, 0.35f, loud),  660.0, 8);
        const double spread1 = l1 / std::max(q1, 1e-9), spread0 = l0 / std::max(q0, 1e-9);
        CHECK(l1 > q1 * 10.0, "T13 v2 touch-responsive: loud/quiet harm spread %.1fx", spread1);
        CHECK(spread1 > spread0 * 1.3,
              "T13 v2 more dynamic than v1: spread %.1fx vs %.1fx", spread1, spread0);
    }

    // ---- T14: v2 is a midrange SHAPER even at DRIVE 0 (static TS voicing) ----
    // shapeTrack=0 keeps the ~780 Hz hump + bass-cut present with the drive off,
    // so the pedal works as an always-on mid shaper (drive off, tone past noon).
    {
        auto g = [&](double f) {
            auto in = sine(f, 0.01f, 16384);
            return goertzel(realSlotM(Kind::Overdrive, 1, 0.0f, in), f) / goertzel(in, f);
        };
        const double midVs100 = 20.0 * std::log10(g(780.0) / g(100.0));
        const double midVs3k  = 20.0 * std::log10(g(780.0) / g(3000.0));
        CHECK(midVs100 > 6.0 && midVs3k > 3.0,
              "T14 v2 shaper @drive0: 780Hz +%.1f vs 100Hz, +%.1f vs 3k", midVs100, midVs3k);
    }

    std::printf("\n%s (%d failure%s)\n", gFails ? "RESULT: FAIL" : "RESULT: ALL PASS", gFails, gFails == 1 ? "" : "s");
    return gFails ? 1 : 0;
}
