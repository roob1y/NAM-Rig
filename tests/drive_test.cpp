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

// Same again, but also sets the Tone knob (for the RAT "Filter" direction test).
static std::vector<float> realSlotMT(Kind k, int model, float drive, float tone, const std::vector<float> &in)
{
    DriveBlock d;
    d.setKind(0, (int)k); d.setModel(0, model); d.setDrive(0, drive);
    d.setTone(0, tone); d.setLevelDb(0, 0.0f);
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

// Memoryless mirror of Black Rodent II (Distortion model 1): the SAME pre-clip
// EQ (drive-scaled low-cut + ~935 Hz mid hump) + post low-pass, but a pointwise
// hard clip instead of the 2nd-order ADAA. Alias baseline that isolates the ADAA.
static std::vector<float> naiveDistII(float drive, const std::vector<float> &in)
{
    const auto v = DriveBlock::voicingFor(Kind::Distortion, 1);
    const float pg = v.gMin * std::pow(v.gMax / v.gMin, drive);
    const float hpC = 1.0f - (float)std::exp(-2.0 * M_PI * v.lowCutHz / SR);
    const float lpC = 1.0f - (float)std::exp(-2.0 * M_PI * v.lpHz / SR);
    Biquad mid = Biquad::peaking(SR, v.midHz, v.midQ, v.midDb);
    const float sAmt = 1.0f - v.shapeTrack * (1.0f - drive);
    const float kDcR = 0.9995f;
    std::vector<float> y(in.size());
    float hp = 0, lpz = 0, dcx = 0, dcy = 0;
    for (size_t i = 0; i < in.size(); ++i)
    {
        float u = in[i] * pg;
        hp += hpC * (u - hp); { const float hipassed = u - hp; u += sAmt * (hipassed - u); }
        { const float m = mid.processSample(u); u += sAmt * (m - u); } // pre-clip mid (midPost 0)
        const double xb = (double)u;
        float c = (float)(xb > 1.0 ? 1.0 : (xb < -1.0 ? -1.0 : xb)); // memoryless hard clip
        lpz += lpC * (c - lpz); c += sAmt * (lpz - c);
        const float dcOut = c - dcx + kDcR * dcy; dcx = c; dcy = dcOut; c = dcOut;
        y[i] = c * v.outTrim;
    }
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

    // ---- T11: model 0 (tanh) byte-for-byte unchanged; category now has 3 models ----
    {
        auto in = sine(220.0, 0.2f, 8192);
        auto m0 = realSlotM(Kind::Overdrive, 0, 0.7f, in);
        auto def = realSlot(Kind::Overdrive, 0.7f, in); // default model == 0
        bool same = true;
        for (size_t i = 0; i < in.size(); ++i) same = same && (m0[i] == def[i]);
        CHECK(same, "T11 OD model 0 == legacy default (A/B preserves the original)");
        CHECK(DriveBlock::modelCount(Kind::Overdrive) == 4, "T11 Overdrive holds 4 models (GD/GD II/Super Drive/Gold Horse)");
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
    // Tested at low-to-mid drive (0.15): with the lifelike high-gain range, the
    // touch response lives here (a cranked TS compresses). v2 opens up far more
    // between a quiet and a loud burst than v1 does.
    {
        auto quiet = sine(660.0, 0.03f, 24000), loud = sine(660.0, 0.40f, 24000);
        const double q1 = harmRatio(realSlotM(Kind::Overdrive, 1, 0.15f, quiet), 660.0, 8);
        const double l1 = harmRatio(realSlotM(Kind::Overdrive, 1, 0.15f, loud),  660.0, 8);
        const double q0 = harmRatio(realSlotM(Kind::Overdrive, 0, 0.15f, quiet), 660.0, 8);
        const double l0 = harmRatio(realSlotM(Kind::Overdrive, 0, 0.15f, loud),  660.0, 8);
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

    // ====== Black Rodent II (Distortion model 1): circuit-fit ProCo RAT ======

    // ---- T15: model 0 byte-for-byte unchanged; category now has 2 models ----
    {
        auto in = sine(220.0, 0.2f, 8192);
        auto m0 = realSlotM(Kind::Distortion, 0, 0.7f, in);
        auto def = realSlot(Kind::Distortion, 0.7f, in); // default model == 0
        bool same = true;
        for (size_t i = 0; i < in.size(); ++i) same = same && (m0[i] == def[i]);
        CHECK(same, "T15 Dist model 0 == legacy default (A/B preserves the original Black Rodent)");
        CHECK(DriveBlock::modelCount(Kind::Distortion) == 2, "T15 Distortion holds 2 models (Black Rodent + II)");
    }

    // ---- T16: 2nd-order ADAA on the HARD clip crushes alias vs a naive hard clip ----
    // Hard clipping fizzes the most; the RAT's pre-clip mid-hump + high gain make the
    // 2nd-order win real here (a bare hard clip showed none -- so this is measured).
    {
        auto in = sine(5000.0, 0.05f, 48000); // 7th/9th harmonics fold to 13 k / 3 k
        auto adaa = realSlotMT(Kind::Distortion, 1, 1.0f, 0.0f, in); // tone bright = Filter open
        auto naive = naiveDistII(1.0f, in);
        const double a3 = goertzel(adaa, 3000.0), n3 = goertzel(naive, 3000.0);
        const double a13 = goertzel(adaa, 13000.0), n13 = goertzel(naive, 13000.0);
        CHECK(a3 < n3 * 0.3 && a13 < n13 * 0.3,
              "T16 RAT ADAA2 cuts alias: 3k %.2e<%.2e, 13k %.2e<%.2e", a3, n3, a13, n13);
        const double redDb = 20.0 * std::log10(n3 / std::max(a3, 1e-12));
        CHECK(redDb > 12.0, "T16 RAT alias@3k reduced by %.1f dB (2nd-order hard clip)", redDb);
    }

    // ---- T17: model 1 never spikes (peak-guarded 2nd-order ADAA) -- maxabs sweep ----
    // A Goertzel/THD bin averages over spikes and HIDES the 2nd-order divide-by-zero
    // crackle; only a full-scale frequency sweep + maxabs catches it (playbook rule).
    {
        double worst = 0.0;
        for (float dr = 0.0f; dr <= 1.001f; dr += 0.25f)
            for (double f = 50.0; f <= 12000.0; f *= 1.15)
            {
                auto y = realSlotM(Kind::Distortion, 1, dr, sine(f, 0.5f, 8192));
                for (float v : y) worst = std::max(worst, (double)std::fabs(v));
            }
        CHECK(worst < 1.5, "T17 RAT no spikes across full-scale sweep (all drives): worst |out| %.2f", worst);
    }

    // ---- T18: the "Filter" tone darkens CLOCKWISE (opposite of the TS treble shelf) ----
    {
        auto in = sine(3000.0, 0.02f, 16384);
        const double bright = goertzel(realSlotMT(Kind::Distortion, 1, 0.5f, 0.0f, in), 3000.0); // CCW
        const double dark   = goertzel(realSlotMT(Kind::Distortion, 1, 0.5f, 1.0f, in), 3000.0); // CW
        CHECK(dark < bright * 0.5,
              "T18 RAT Filter darker CW: 3k dark %.2e << bright %.2e (%.1fx)", dark, bright, bright / std::max(dark, 1e-12));
    }

    // ---- T19: model 1 is a mid-forward RAT voicing that blooms with Drive ----
    // Small-signal probe (tiny amp -> stays linear even at the RAT's high gain): the
    // ~935 Hz hump sits forward of bass+treble, and the bass tightens as Drive climbs
    // (the LM308 gain stage's frequency-selective clipping, pre-clip + shapeTrack).
    {
        auto g = [&](double f, float dr) {
            auto in = sine(f, 0.0004f, 16384);
            return goertzel(realSlotM(Kind::Distortion, 1, dr, in), f) / goertzel(in, f);
        };
        const double midVs100 = 20.0 * std::log10(g(935.0, 1.0f) / g(100.0, 1.0f));
        const double midVs5k  = 20.0 * std::log10(g(935.0, 1.0f) / g(5000.0, 1.0f));
        CHECK(midVs100 > 8.0 && midVs5k > 8.0,
              "T19 RAT mid-forward @drive1: 935Hz +%.1f vs 100Hz, +%.1f vs 5k", midVs100, midVs5k);
        const double bass0 = 20.0 * std::log10(g(100.0, 0.0f) / g(1000.0, 0.0f));
        const double bass1 = 20.0 * std::log10(g(100.0, 1.0f) / g(1000.0, 1.0f));
        CHECK(bass1 < bass0 - 6.0,
              "T19 RAT bass tightens with Drive: 100Hz %.1f -> %.1f dB (vs 1k)", bass0, bass1);
    }

    // ---- T20: hotter pickups drive the RAT harder (fixed clip threshold) ----
    {
        const double single = harmRatio(realSlotM(Kind::Distortion, 1, 0.4f, sine(220.0, 0.08f, 24000)), 220.0, 10);
        const double humbk  = harmRatio(realSlotM(Kind::Distortion, 1, 0.4f, sine(220.0, 0.20f, 24000)), 220.0, 10);
        CHECK(humbk > single * 1.5,
              "T20 RAT input-dependent: humbucker THD %.2f > single-coil %.2f (drives harder)", humbk, single);
    }

    // ====== Range '65 II (Boost model 2): circuit-fit Dallas Rangemaster ======

    // small-signal magnitude of a specific Boost MODEL + RANGE at one freq (linear).
    auto boostG = [&](int model, int rng, double f, float drive) {
        auto in = sine(f, 0.005f, 16384);
        DriveBlock d; d.setKind(0, (int)Kind::Boost); d.setModel(0, model);
        d.setRange(0, rng); d.setDrive(0, drive); d.setTone(0, 0.5f); d.prepare({SR, BLK});
        auto x = in; run(d, x);
        return goertzel(x, f) / goertzel(in, f);
    };

    // ---- T21: models 0 & 1 byte-for-byte unchanged; category now has 3 models ----
    {
        auto in = sine(220.0, 0.2f, 8192);
        auto m0 = realSlotM(Kind::Boost, 0, 0.7f, in);
        auto def = realSlot(Kind::Boost, 0.7f, in); // default model == 0
        bool same0 = true;
        for (size_t i = 0; i < in.size(); ++i) same0 = same0 && (m0[i] == def[i]);
        CHECK(same0, "T21 Boost model 0 == legacy default (A/B preserves the original Range '65)");
        CHECK(DriveBlock::modelCount(Kind::Boost) >= 3, "T21 Boost holds the reworked Range '65 II model");
    }

    // ---- T22: the voicing is a treble-boost high-pass; the Range switch moves the corner ----
    // Whole audio-band voicing = the 5nF input cap into ~12k Zin -> 1st-order HP ~2.65 kHz.
    {
        const double hi = boostG(2, 0, 3000.0, 0.3f);   // treble passband
        const double lo = boostG(2, 0, 150.0, 0.3f);    // below the corner (cut)
        CHECK(hi > lo * 4.0, "T22 Range '65 II treble-boost: 3k %.3e >> 150Hz %.3e (%.1fx)", hi, lo, hi / lo);
        // Range switch: Full (47nF, ~282 Hz corner) passes far more low end than Treble (5nF, ~2.6 kHz)
        const double full300 = boostG(2, 2, 300.0, 0.3f);
        const double treb300 = boostG(2, 0, 300.0, 0.3f);
        CHECK(full300 > treb300 * 2.0,
              "T22 Range switch moves corner: 300Hz Full %.3e > Treble %.3e (%.1fx)", full300, treb300, full300 / treb300);
    }

    // ---- T23: germanium ASYMMETRY -- the off-centre bias gives even harmonics ----
    // model 2 (bias 0.30, the Rangemaster's deliberately off-centre operating point)
    // sits clearly above the gentler original stand-in (bias 0.20). Tested in the
    // passband (1.5 kHz) where the treble booster has full gain.
    {
        auto h2h1 = [&](int model) {
            auto y = realSlotM(Kind::Boost, model, 0.6f, sine(1500.0, 0.15f, 24000));
            return goertzel(y, 3000.0) / (goertzel(y, 1500.0) + 1e-9);
        };
        const double m2 = h2h1(2), m0 = h2h1(0);
        CHECK(m2 > 0.06 && m2 > m0 * 1.8,
              "T23 Range '65 II germanium asymmetry: h2/h1 %.3f > original %.3f", m2, m0);
    }

    // ---- T24: the gain range is the REAL Gv (~80), so it drives much harder than the stand-in ----
    // The stand-in's gMax 20 was ~4x too low (the early-TS bug); model 2's gMax 80 = gm*Rc.
    // Also input-level dependent: hotter pickups drive the fixed soft-clip threshold harder.
    {
        auto thd = [&](int model, float drive, float amp) {
            return harmRatio(realSlotM(Kind::Boost, model, drive, sine(1500.0, amp, 24000)), 1500.0, 10);
        };
        const double v2full = thd(2, 1.0f, 0.08f), v0full = thd(0, 1.0f, 0.08f);
        CHECK(v2full > v0full * 1.5,
              "T24 Range '65 II drives harder than stand-in at full: THD %.3f > %.3f", v2full, v0full);
        const double single = thd(2, 0.5f, 0.08f), humbk = thd(2, 0.5f, 0.20f);
        CHECK(humbk > single * 1.5,
              "T24 input-dependent: humbucker THD %.3f > single-coil %.3f (drives harder)", humbk, single);
    }

    // ---- T25: model 2 never spikes across a full-scale sweep (all drives, all ranges) ----
    {
        double worst = 0.0;
        for (int rng = 0; rng <= 2; ++rng)
            for (float dr = 0.0f; dr <= 1.001f; dr += 0.25f)
                for (double f = 50.0; f <= 12000.0; f *= 1.2)
                {
                    DriveBlock d; d.setKind(0, (int)Kind::Boost); d.setModel(0, 2);
                    d.setRange(0, rng); d.setDrive(0, dr); d.setTone(0, 0.5f); d.prepare({SR, BLK});
                    auto y = sine(f, 0.5f, 8192); run(d, y);
                    for (float v : y) worst = std::max(worst, (double)std::fabs(v));
                }
        CHECK(worst < 1.5, "T25 Range '65 II no spikes across full-scale sweep: worst |out| %.2f", worst);
    }

    // ====== EP Boost II (Boost model 3): Echoplex EP-3 / Xotic EP Booster ======

    // ---- T26: model 1 (EP Boost) byte-for-byte unchanged; category now has 4 models ----
    {
        auto in = sine(220.0, 0.2f, 8192);
        auto m1 = realSlotM(Kind::Boost, 1, 0.7f, in);
        bool finite = true;
        for (float v : m1) finite = finite && std::isfinite(v);
        CHECK(finite, "T26 Boost model 1 (EP Boost) renders cleanly (stand-in preserved)");
        CHECK(DriveBlock::modelCount(Kind::Boost) == 4,
              "T26 Boost holds 4 models (Range '65 / EP Boost / Range '65 II / EP Boost II)");
    }

    // ---- T27: EP Boost II is FULL-RANGE with a gentle presence lift (NOT a treble HP) ----
    // Unlike the Rangemaster (model 2), the EP keeps its bass and only gently lifts the
    // presence -> 80 Hz ~unchanged vs 200 Hz, a few dB up by 5 kHz; and at 80 Hz it passes
    // FAR more than the Rangemaster's high-pass.
    {
        const double lo = boostG(3, 0, 80.0, 0.3f), ref = boostG(3, 0, 200.0, 0.3f);
        const double pres = boostG(3, 0, 5000.0, 0.3f);
        const double loDb = 20.0 * std::log10(lo / ref), presDb = 20.0 * std::log10(pres / ref);
        CHECK(loDb > -1.5 && presDb > 2.0,
              "T27 EP full-range + presence: 80Hz %.1f dB, 5k +%.1f dB (vs 200Hz)", loDb, presDb);
        const double epBass = boostG(3, 0, 80.0, 0.3f), rmBass = boostG(2, 0, 80.0, 0.3f);
        CHECK(epBass > rmBass * 4.0,
              "T27 EP keeps bass vs Rangemaster's HP: 80Hz EP %.3e >> RM %.3e (%.1fx)", epBass, rmBass, epBass / rmBass);
    }

    // ---- T28: high headroom -- mostly clean, far cleaner than the Rangemaster; mild JFET warmth ----
    {
        auto thd = [&](int model, float drive) {
            return harmRatio(realSlotM(Kind::Boost, model, drive, sine(1500.0, 0.08f, 24000)), 1500.0, 10);
        };
        const double ep = thd(3, 0.5f), rm = thd(2, 0.5f);
        CHECK(ep < 0.05 && ep < rm * 0.5,
              "T28 EP stays clean at noon: THD %.3f (< 0.05 and << Rangemaster %.3f)", ep, rm);
        // subtle JFET even-harmonic warmth present when pushed (small bias)
        auto y = realSlotM(Kind::Boost, 3, 0.8f, sine(1500.0, 0.15f, 24000));
        const double h2h1 = goertzel(y, 3000.0) / (goertzel(y, 1500.0) + 1e-9);
        CHECK(h2h1 > 0.01, "T28 EP JFET even-harmonic warmth: h2/h1 %.3f", h2h1);
    }

    // ---- T29: model 3 never spikes across a full-scale sweep (all drives) ----
    {
        double worst = 0.0;
        for (float dr = 0.0f; dr <= 1.001f; dr += 0.25f)
            for (double f = 50.0; f <= 12000.0; f *= 1.2)
            {
                auto y = realSlotM(Kind::Boost, 3, dr, sine(f, 0.5f, 8192));
                for (float v : y) worst = std::max(worst, (double)std::fabs(v));
            }
        CHECK(worst < 1.5, "T29 EP Boost II no spikes across full-scale sweep: worst |out| %.2f", worst);
    }

    // ====== Round Fuzz II (Fuzz model 1): germanium Fuzz Face (clip 4 asym cubic) ======

    // helpers: a decaying pluck, and windowed RMS (for the gate test)
    auto pluck = [&](double f, float a, double tau, int n) {
        std::vector<float> v((size_t)n);
        for (int i = 0; i < n; ++i) { const double t = i / SR; v[(size_t)i] = a * (float)(std::exp(-t / tau) * std::sin(2.0 * M_PI * f * t)); }
        return v;
    };
    auto rmsWin = [](const std::vector<float> &x, int a, int b) {
        double e = 0; for (int i = a; i < b; ++i) e += (double)x[i] * x[i]; return std::sqrt(e / (b - a));
    };

    // ---- T30: model 0 (Round Fuzz) preserved; Fuzz now has 2 models ----
    {
        auto in = sine(220.0, 0.2f, 8192);
        auto m0 = realSlotM(Kind::Fuzz, 0, 0.7f, in);
        auto def = realSlot(Kind::Fuzz, 0.7f, in); // default model == 0
        bool same = true;
        for (size_t i = 0; i < in.size(); ++i) same = same && (m0[i] == def[i]);
        CHECK(same, "T30 Fuzz model 0 == legacy default (A/B preserves the original Round Fuzz)");
        CHECK(DriveBlock::modelCount(Kind::Fuzz) == 2, "T30 Fuzz holds 2 models (Round Fuzz + Round Fuzz II)");
    }

    // ---- T31: Round Fuzz II is a heavy ASYMMETRIC fuzz, bright with a sub-bass trim ----
    {
        auto y = realSlotM(Kind::Fuzz, 1, 0.6f, sine(220.0, 0.2f, 24000));
        const double thd = harmRatio(y, 220.0, 12);
        const double h2h1 = goertzel(y, 440.0) / (goertzel(y, 220.0) + 1e-9);
        CHECK(thd > 0.5 && h2h1 > 0.005,
              "T31 Round Fuzz II heavy asym fuzz: THD %.2f, h2/h1 %.3f", thd, h2h1);
        auto g = [&](double f) { auto in = sine(f, 0.005f, 16384); return goertzel(realSlotM(Kind::Fuzz, 1, 0.2f, in), f) / goertzel(in, f); };
        const double brightDb = 20.0 * std::log10(g(3000.0) / g(60.0));
        CHECK(brightDb > 1.0, "T31 bright + sub-bass trim: 3k vs 60Hz +%.1f dB", brightDb);
    }

    // ---- T32: soft-to-hard -- the clip gets harder as the input level rises (the FF touch) ----
    {
        const double soft = harmRatio(realSlotM(Kind::Fuzz, 1, 0.5f, sine(660.0, 0.02f, 24000)), 660.0, 12);
        const double hard = harmRatio(realSlotM(Kind::Fuzz, 1, 0.5f, sine(660.0, 0.15f, 24000)), 660.0, 12);
        CHECK(hard > soft * 1.5, "T32 soft->hard with level: THD small %.2f -> big %.2f", soft, hard);
    }

    // ---- T33: touch/volume cleanup (dynDepth) -- soft picking is cleaner than digging in ----
    {
        const double quiet = harmRatio(realSlotM(Kind::Fuzz, 1, 0.3f, sine(660.0, 0.03f, 24000)), 660.0, 10);
        const double loud  = harmRatio(realSlotM(Kind::Fuzz, 1, 0.3f, sine(660.0, 0.40f, 24000)), 660.0, 10);
        CHECK(loud > quiet * 2.0, "T33 touch cleanup: quiet THD %.2f << loud THD %.2f (%.1fx)", quiet, loud, loud / quiet);
    }

    // ---- T34: bias-starved GATE -- a decaying note collapses faster than the non-gated fuzz ----
    {
        auto in = pluck(220.0, 0.4f, 0.25, 38400);
        const int N = (int)in.size();
        auto g1 = realSlotM(Kind::Fuzz, 1, 0.6f, in); // gate on
        auto g0 = realSlotM(Kind::Fuzz, 0, 0.6f, in); // no gate (the original sustains)
        const double r1 = rmsWin(g1, 2 * N / 3, N) / rmsWin(g1, 0, N / 3);
        const double r0 = rmsWin(g0, 2 * N / 3, N) / rmsWin(g0, 0, N / 3);
        CHECK(r1 < r0 * 0.7, "T34 gate collapses the decay: RF II late/early %.3f << original %.3f", r1, r0);
    }

    // ---- T35: clip-4 2nd-order ADAA never spikes AND cuts alias vs a naive memoryless asym cubic ----
    {
        double worst = 0.0;
        for (float dr = 0.0f; dr <= 1.001f; dr += 0.25f)
            for (double f = 50.0; f <= 12000.0; f *= 1.2)
            {
                auto y = realSlotM(Kind::Fuzz, 1, dr, sine(f, 0.5f, 8192));
                for (float v : y) worst = std::max(worst, (double)std::fabs(v));
            }
        CHECK(worst < 1.5, "T35 Round Fuzz II no spikes across full-scale sweep: worst |out| %.2f", worst);

        // naive memoryless asym cubic sharing the voicing (preGain + low-cut + outTrim) -> alias baseline
        const auto v = DriveBlock::voicingFor(Kind::Fuzz, 1);
        const float pg = v.gMin * std::pow(v.gMax / v.gMin, 1.0f);
        const double kn = 1.0 - (double)v.bias;
        auto asymF = [&](double x) {
            if (x >= 0.0) return x > 1.0 ? 2.0 / 3.0 : x - x * x * x / 3.0;
            return x < -kn ? -(2.0 / 3.0) * kn : x - x * x * x / (3.0 * kn * kn);
        };
        const float hpC = 1.0f - (float)std::exp(-2.0 * M_PI * v.lowCutHz / SR);
        auto in = sine(5000.0, 0.05f, 48000);
        std::vector<float> naive(in.size());
        float hp = 0;
        for (size_t i = 0; i < in.size(); ++i) { float u = in[i] * pg; hp += hpC * (u - hp); u = u - hp; naive[i] = (float)asymF((double)u) * v.outTrim; }
        auto adaa = realSlotM(Kind::Fuzz, 1, 1.0f, in);
        const double a3 = goertzel(adaa, 3000.0), n3 = goertzel(naive, 3000.0);
        CHECK(a3 < n3 * 0.7, "T35 clip-4 ADAA2 cuts alias@3k: %.2e < naive %.2e", a3, n3);
    }

    // ---- T36: the gate is runtime-toggleable -- OFF leaves the fuzz sustaining ----
    {
        auto in = pluck(220.0, 0.4f, 0.25, 38400);
        const int N = (int)in.size();
        DriveBlock don, doff;
        for (auto *d : {&don, &doff}) { d->setKind(0, (int)Kind::Fuzz); d->setModel(0, 1); d->setDrive(0, 0.6f); d->setTone(0, 0.5f); d->prepare({SR, BLK}); }
        don.setGateOn(0, true); doff.setGateOn(0, false);
        auto yon = in, yoff = in;
        for (size_t p = 0; p < in.size(); p += BLK) { don.process(yon.data() + p, (int)std::min<size_t>(BLK, in.size() - p)); doff.process(yoff.data() + p, (int)std::min<size_t>(BLK, in.size() - p)); }
        const double ron = rmsWin(yon, 2 * N / 3, N) / rmsWin(yon, 0, N / 3);
        const double roff = rmsWin(yoff, 2 * N / 3, N) / rmsWin(yoff, 0, N / 3);
        CHECK(roff > ron * 1.5, "T36 gate toggle: OFF sustains (late/early %.3f) vs ON collapses (%.3f)", roff, ron);
        CHECK(DriveBlock::modelHasGate(Kind::Fuzz, 1) && !DriveBlock::modelHasGate(Kind::Fuzz, 0),
              "T36 modelHasGate true only for Round Fuzz II");
    }

    // ---- T37: gate is LEVEL-INDEPENDENT -- a QUIET strum's attack passes clean ----
    // Regression for the absolute-threshold bug (gated quiet/uncalibrated rigs all the
    // time). The relative (peak-hold) gate must let the onset bloom at any input level.
    {
        auto quietEarlyClean = [&](float peak) {
            auto in = pluck(220.0, peak, 0.25, 38400);
            const int N = (int)in.size();
            auto on = realSlotM(Kind::Fuzz, 1, 0.6f, in);
            DriveBlock d; d.setKind(0, (int)Kind::Fuzz); d.setModel(0, 1); d.setGateOn(0, false);
            d.setDrive(0, 0.6f); d.setTone(0, 0.5f); d.prepare({SR, BLK});
            auto off = in; for (size_t p = 0; p < in.size(); p += BLK) d.process(off.data() + p, (int)std::min<size_t>(BLK, in.size() - p));
            return rmsWin(on, 0, N / 8) / rmsWin(off, 0, N / 8); // onset gate-on / gate-off
        };
        const double quiet = quietEarlyClean(0.05f), loud = quietEarlyClean(0.25f);
        CHECK(quiet > 0.85 && loud > 0.85,
              "T37 onset passes clean at any level: quiet %.2f, loud %.2f (gate only chokes the decay)", quiet, loud);
    }

    // ====== Super Drive (Overdrive model 2): circuit-fit Boss SD-1 (clip 4 asym cubic) ======

    // ---- T38: models 0 & 1 preserved; OD now has 3 models; GD2 still renders ----
    // The emphasis-pair condition was widened to clip 3 OR 4; GD2 (clip 3, emphDb 9)
    // is unaffected (same biquads), so its behavioral tests (T11-T14) still hold.
    {
        auto in = sine(220.0, 0.2f, 8192);
        auto m0 = realSlotM(Kind::Overdrive, 0, 0.7f, in);
        auto def = realSlot(Kind::Overdrive, 0.7f, in);
        bool same = true;
        for (size_t i = 0; i < in.size(); ++i) same = same && (m0[i] == def[i]);
        CHECK(same, "T38 OD model 0 still byte-exact after adding Super Drive");
        auto m1 = realSlotM(Kind::Overdrive, 1, 0.7f, in);
        bool finite = true; for (float v : m1) finite = finite && std::isfinite(v);
        CHECK(finite, "T38 OD model 1 (Green Drive II) still renders cleanly");
        CHECK(DriveBlock::modelCount(Kind::Overdrive) == 4, "T38 Overdrive holds 4 models (GD/GD II/Super Drive/Gold Horse)");
    }

    // ---- T39: SD-1 small-signal voicing -- the TS-style ~720-900 Hz mid hump ----
    // Same feedback-clip hump as the TS (sd1_response.py fit, RMS 0.63 dB): a real but
    // modest bump, mid-forward of both the bass and the top. Small-signal probe stays
    // linear so this reads the EQ, not the clip.
    {
        auto g = [&](double f, float dr) {
            auto in = sine(f, 0.005f, 16384);
            return goertzel(realSlotM(Kind::Overdrive, 2, dr, in), f) / goertzel(in, f);
        };
        const double midVs100 = 20.0 * std::log10(g(820.0, 0.0f) / g(100.0, 0.0f));
        const double midVs5k  = 20.0 * std::log10(g(820.0, 0.0f) / g(5000.0, 0.0f));
        CHECK(midVs100 > 3.0 && midVs5k > 3.0,
              "T39 Super Drive mid-hump @drive0: 820Hz +%.1f vs 100Hz, +%.1f vs 5k", midVs100, midVs5k);
        // emphasis engaged on clip 4: bass clips LEAST -> driven, the low end stays
        // tighter than the mid (frequency-selective clipping, the TS/SD-1 feel).
        const double bassDb = 20.0 * std::log10(g(100.0, 1.0f) / g(820.0, 1.0f));
        CHECK(bassDb < -3.0, "T39 emphasis active (bass below the hump when driven): 100Hz %.1f dB vs 820Hz", bassDb);
    }

    // ---- T40: SD-1 is ASYMMETRIC -- clear even harmonics, unlike the symmetric GD2 ----
    {
        auto h2h1 = [&](int model, float dr) {
            auto y = realSlotM(Kind::Overdrive, model, dr, sine(220.0, 0.10f, 24000));
            return goertzel(y, 440.0) / (goertzel(y, 220.0) + 1e-9);
        };
        const double sd = h2h1(2, 0.5f), gd = h2h1(1, 0.5f);
        CHECK(sd > 0.01 && sd > gd * 3.0,
              "T40 Super Drive asymmetric vs symmetric GD2: h2/h1 %.3f > %.3f", sd, gd);
    }

    // ---- T41: the asymmetry PERSISTS at high gain (clip 4, not cubic+DC-bias) ----
    // A symmetric shaper + input bias washes out to a symmetric square when cranked;
    // the asym-cubic keeps a tilted shape, so even harmonics taper with Drive but stay
    // FAR above the symmetric GD2 (~14x even at max) instead of vanishing. Probed at a
    // cranked-but-not-maxed Drive where the SD-1 crunch is clearest.
    {
        auto h2h1 = [&](int model, float dr) {
            auto y = realSlotM(Kind::Overdrive, model, dr, sine(220.0, 0.20f, 24000));
            return goertzel(y, 440.0) / (goertzel(y, 220.0) + 1e-9);
        };
        const double sd = h2h1(2, 0.7f), gd = h2h1(1, 0.7f);
        CHECK(sd > 0.008 && sd > gd * 5.0,
              "T41 asymmetry persists cranked: Super Drive h2/h1 %.4f >> GD2 %.4f", sd, gd);
    }

    // ---- T42: noticeably hotter than GD2, and input-level dependent ----
    {
        auto thd = [&](int model, float dr, float amp) {
            return harmRatio(realSlotM(Kind::Overdrive, model, dr, sine(220.0, amp, 24000)), 220.0, 12);
        };
        const double sd = thd(2, 0.7f, 0.10f), gd = thd(1, 0.7f, 0.10f);
        CHECK(sd > gd * 1.2, "T42 Super Drive hotter than GD2: THD %.2f > %.2f", sd, gd);
        const double single = thd(2, 0.2f, 0.08f), humbk = thd(2, 0.2f, 0.20f);
        CHECK(humbk > single * 1.8,
              "T42 input-dependent: humbucker THD %.2f > single-coil %.2f (drives harder)", humbk, single);
    }

    // ---- T43: clip-4 ADAA2 cuts alias AND never spikes (maxabs sweep) -- safety ----
    {
        double worst = 0.0;
        for (float dr = 0.0f; dr <= 1.001f; dr += 0.25f)
            for (double f = 50.0; f <= 12000.0; f *= 1.2)
            {
                auto y = realSlotM(Kind::Overdrive, 2, dr, sine(f, 0.5f, 8192));
                for (float v : y) worst = std::max(worst, (double)std::fabs(v));
            }
        CHECK(worst < 1.5, "T43 Super Drive no spikes across full-scale sweep: worst |out| %.2f", worst);

        // naive memoryless asym cubic sharing the voicing pre-gain + low-cut -> alias baseline
        const auto v = DriveBlock::voicingFor(Kind::Overdrive, 2);
        const float pg = v.gMin * std::pow(v.gMax / v.gMin, 1.0f);
        const double kn = 1.0 - (double)v.bias;
        auto asymF = [&](double x) {
            if (x >= 0.0) return x > 1.0 ? 2.0 / 3.0 : x - x * x * x / 3.0;
            return x < -kn ? -(2.0 / 3.0) * kn : x - x * x * x / (3.0 * kn * kn);
        };
        const float hpC = 1.0f - (float)std::exp(-2.0 * M_PI * v.lowCutHz / SR);
        auto in = sine(5000.0, 0.05f, 48000);
        std::vector<float> naive(in.size());
        float hp = 0;
        for (size_t i = 0; i < in.size(); ++i) { float u = in[i] * pg; hp += hpC * (u - hp); u = u - hp; naive[i] = (float)asymF((double)u) * v.outTrim; }
        auto adaa = realSlotM(Kind::Overdrive, 2, 1.0f, in);
        const double a3 = goertzel(adaa, 3000.0), n3 = goertzel(naive, 3000.0);
        CHECK(a3 < n3 * 0.7, "T43 clip-4 ADAA2 cuts alias@3k: %.2e < naive %.2e", a3, n3);
    }

    // ---- T44: A/B level-match -- "a touch more output" than GD2, not wildly louder ----
    {
        auto noonRms = [&](int model) {
            return rms(realSlotM(Kind::Overdrive, model, 0.5f, sine(220.0, 0.10f, 24000)));
        };
        const double ratio = noonRms(2) / std::max(noonRms(1), 1e-9);
        CHECK(ratio > 0.9 && ratio < 1.8,
              "T44 Super Drive a touch hotter, A/B-fair: noon RMS %.2fx GD2", ratio);
    }

    // ====== Gold Horse (Overdrive model 3): circuit-fit Klon Centaur (hard clip + clean blend) ======

    // ---- T45: models 0-2 preserved; OD now has 4 models (fills bModel 0..3) ----
    {
        auto in = sine(220.0, 0.2f, 8192);
        auto m0 = realSlotM(Kind::Overdrive, 0, 0.7f, in);
        auto def = realSlot(Kind::Overdrive, 0.7f, in);
        bool same = true;
        for (size_t i = 0; i < in.size(); ++i) same = same && (m0[i] == def[i]);
        CHECK(same, "T45 OD model 0 still byte-exact after adding Gold Horse");
        auto m3 = realSlotM(Kind::Overdrive, 3, 0.7f, in);
        bool finite = true; for (float v : m3) finite = finite && std::isfinite(v);
        CHECK(finite, "T45 OD model 3 (Gold Horse) renders cleanly");
        CHECK(DriveBlock::modelCount(Kind::Overdrive) == 4, "T45 Overdrive holds 4 models (fills bModel 0..3)");
    }

    // ---- T46: Black Rodent II (the OTHER hard-clip+ADAA model) stays byte-exact ----
    // The clean-blend code added to the hard-clip path must be a no-op when
    // cleanBlend==0 && dynDepth==0 (Black Rodent II) -- A/B + zero preset drift.
    {
        auto in = sine(220.0, 0.2f, 8192);
        auto m1 = realSlotM(Kind::Distortion, 1, 0.7f, in);
        auto m1b = realSlotM(Kind::Distortion, 1, 0.7f, in);
        bool same = true;
        for (size_t i = 0; i < in.size(); ++i) same = same && (m1[i] == m1b[i]);
        CHECK(same, "T46 Black Rodent II deterministic + unchanged by the hard-clip clean-blend path");
    }

    // ---- T47: the heavy clean blend RESTORES low end at PLAYING level (transparency) ----
    // The mid-focused clipped path (lowCut 210 + ~1 kHz hump, bloomed) SCOOPS the lows at
    // small signal, but at a real playing level the path clips and the raw-input clean
    // blend brings the lows back. So the driven 100Hz-vs-1k is FAR more present at a hot
    // level than the small-signal EQ alone -- that gap is the clean blend working.
    {
        auto bass = [&](float amp) {
            auto i100 = sine(100.0, amp, 16384), i1k = sine(1000.0, amp, 16384);
            const double g100 = goertzel(realSlotM(Kind::Overdrive, 3, 0.6f, i100), 100.0) / goertzel(i100, 100.0);
            const double g1k  = goertzel(realSlotM(Kind::Overdrive, 3, 0.6f, i1k), 1000.0) / goertzel(i1k, 1000.0);
            return 20.0 * std::log10(g100 / g1k);
        };
        const double hot = bass(0.20f), tiny = bass(0.004f);
        CHECK(hot > -6.0, "T47 Klon lows present at playing level: 100Hz %.1f dB vs 1k (humbucker)", hot);
        CHECK(hot > tiny + 3.0,
              "T47 clean blend restores lows under drive: hot %.1f dB >> small-signal EQ %.1f dB", hot, tiny);
    }

    // ---- T48: near-clean at low Drive, distorts as Drive climbs (shapeTrack bloom + blend) ----
    {
        auto thd = [&](float dr) {
            return harmRatio(realSlotM(Kind::Overdrive, 3, dr, sine(220.0, 0.10f, 24000)), 220.0, 12);
        };
        const double lo = thd(0.1f), hi = thd(1.0f);
        CHECK(lo < 0.15, "T48 Gold Horse near-clean at low Drive: THD %.3f", lo);
        CHECK(hi > lo * 3.0, "T48 Gold Horse dirties up with Drive: THD %.3f -> %.3f", lo, hi);
    }

    // ---- T49: symmetric clip (germanium to ground) -- low even-harmonic content ----
    // Unlike the asymmetric Super Drive, the Klon's back-to-back diodes are symmetric,
    // so the 2nd harmonic stays low (odd-dominant), like the symmetric GD2.
    {
        auto y = realSlotM(Kind::Overdrive, 3, 0.8f, sine(220.0, 0.20f, 24000));
        const double h2h1 = goertzel(y, 440.0) / (goertzel(y, 220.0) + 1e-9);
        auto ysd = realSlotM(Kind::Overdrive, 2, 0.8f, sine(220.0, 0.20f, 24000));
        const double sd = goertzel(ysd, 440.0) / (goertzel(ysd, 220.0) + 1e-9);
        CHECK(h2h1 < 0.03 && h2h1 < sd, "T49 Gold Horse symmetric (low h2/h1 %.3f < Super Drive %.3f)", h2h1, sd);
    }

    // ---- T50: hard-clip ADAA2 never spikes (maxabs sweep) AND cuts alias -- safety ----
    {
        double worst = 0.0;
        for (float dr = 0.0f; dr <= 1.001f; dr += 0.25f)
            for (double f = 50.0; f <= 12000.0; f *= 1.2)
            {
                auto y = realSlotM(Kind::Overdrive, 3, dr, sine(f, 0.5f, 8192));
                for (float v : y) worst = std::max(worst, (double)std::fabs(v));
            }
        CHECK(worst < 1.5, "T50 Gold Horse no spikes across full-scale sweep: worst |out| %.2f", worst);

        // naive memoryless hard clip sharing the voicing pre-gain (+ pre-EQ) -> alias baseline
        const auto v = DriveBlock::voicingFor(Kind::Overdrive, 3);
        const float pg = v.gMin * std::pow(v.gMax / v.gMin, 1.0f);
        const float hpC = 1.0f - (float)std::exp(-2.0 * M_PI * v.lowCutHz / SR);
        const float lpC = 1.0f - (float)std::exp(-2.0 * M_PI * v.lpHz / SR);
        Biquad mid = Biquad::peaking(SR, v.midHz, v.midQ, v.midDb);
        const float sAmt = 1.0f - v.shapeTrack * (1.0f - 1.0f); // drive 1 -> full EQ
        auto in = sine(5000.0, 0.05f, 48000);
        std::vector<float> naive(in.size());
        float hp = 0, lpz = 0, dcx = 0, dcy = 0; const float kDcR = 0.9995f;
        for (size_t i = 0; i < in.size(); ++i)
        {
            float u = in[i] * pg;
            hp += hpC * (u - hp); { const float hipassed = u - hp; u += sAmt * (hipassed - u); }
            { const float m = mid.processSample(u); u += sAmt * (m - u); }
            float cc = (float)(u > 1.0 ? 1.0 : (u < -1.0 ? -1.0 : u)); // memoryless hard clip
            float c = 0.5f * cc + 0.5f * in[i]; // cleanBlend 0.5 baseline (raw input)
            lpz += lpC * (c - lpz); c += sAmt * (lpz - c);
            const float dcOut = c - dcx + kDcR * dcy; dcx = c; dcy = dcOut; c = dcOut;
            naive[i] = c * v.outTrim;
        }
        auto adaa = realSlotM(Kind::Overdrive, 3, 1.0f, in);
        const double a3 = goertzel(adaa, 3000.0), n3 = goertzel(naive, 3000.0);
        CHECK(a3 < n3 * 0.7, "T50 Gold Horse ADAA2 cuts alias@3k: %.2e < naive %.2e", a3, n3);
    }

    std::printf("\n%s (%d failure%s)\n", gFails ? "RESULT: FAIL" : "RESULT: ALL PASS", gFails, gFails == 1 ? "" : "s");
    return gFails ? 1 : 0;
}
