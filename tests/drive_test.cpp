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
    const float hpCoef = (v.lowCutHz > 0.0f)
        ? 1.0f - (float)std::exp(-2.0 * M_PI * v.lowCutHz / SR) : 0.0f;
    const bool useMid = (v.midDb != 0.0f);
    Biquad mid = useMid ? Biquad::peaking(SR, v.midHz, v.midQ, v.midDb) : Biquad::identity();
    auto clipF = [&](float x) {
        return v.clip == 0 ? std::tanh(x) : (x > 1.0f ? 1.0f : (x < -1.0f ? -1.0f : x));
    };
    const float dcSub = clipF(v.bias);
    std::vector<float> y(in.size());
    float hp = 0.0f;
    for (size_t i = 0; i < in.size(); ++i)
    {
        float u = in[i] * preGain;
        if (hpCoef > 0.0f) { hp += hpCoef * (u - hp); u -= hp; }
        if (useMid) u = mid.processSample(u);
        y[i] = (clipF(u + v.bias) - dcSub) * v.outTrim; // tone flat, level 0 dB
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

    // ---- T2: Overdrive has a midrange hump (the TS signature) ----
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
        // should sit clearly above Distortion (bias 0.12).
        auto h2overH1 = [&](const std::vector<float> &y) {
            return goertzel(y, 440.0) / (goertzel(y, 220.0) + 1e-9);
        };
        CHECK(h2overH1(fz) > 0.02 && h2overH1(fz) > h2overH1(ds),
              "T6 Fuzz more asymmetric than Dist (h2/h1 %.3f vs %.3f)", h2overH1(fz), h2overH1(ds));
    }

    // ---- T7: tone tilt up = brighter (odd 7th harmonic above the OD pivot) ----
    {
        auto in = sine(220.0, 0.1f, 16000);
        auto hi = in, lo = in;
        DriveBlock a, b;
        a.setKind(0, (int)Kind::Overdrive); a.setDrive(0, 0.7f); a.setTone(0, 0.9f); a.prepare({SR, BLK}); run(a, hi);
        b.setKind(0, (int)Kind::Overdrive); b.setDrive(0, 0.7f); b.setTone(0, 0.1f); b.prepare({SR, BLK}); run(b, lo);
        const double hiT = goertzel(hi, 1540.0), loT = goertzel(lo, 1540.0);
        CHECK(hiT > loT * 1.5, "T7 tone up = brighter (HF %.2e > %.2e)", hiT, loT);
    }

    std::printf("\n%s (%d failure%s)\n", gFails ? "RESULT: FAIL" : "RESULT: ALL PASS", gFails, gFails == 1 ? "" : "s");
    return gFails ? 1 : 0;
}
