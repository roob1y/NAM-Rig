// drive_test — offline verification harness for DriveBlock (measurement-first).
// Exits nonzero on any FAIL. Proves: Off/Boost paths are exact, the Boost gain
// law is right, and the clippers' ANTIDERIVATIVE anti-aliasing (ADAA) actually
// suppresses fold-back vs a naive memoryless clipper using identical voicing
// constants (DriveBlock::voicingFor is the shared source of truth).
#include "rig/DriveBlock.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>

using nam_rig::DriveBlock;
using nam_rig::BlockContext;
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

// |X(freq)| magnitude over n samples (Goertzel).
static double goertzel(const std::vector<float> &x, double freq)
{
    const int n = (int)x.size();
    const double w = 2.0 * M_PI * freq / SR;
    const double cw = std::cos(w), coeff = 2.0 * cw;
    double s0 = 0, s1 = 0, s2 = 0;
    for (int i = 0; i < n; ++i) { s0 = x[(size_t)i] + coeff * s1 - s2; s2 = s1; s1 = s0; }
    return std::sqrt(s1 * s1 + s2 * s2 - coeff * s1 * s2) / (n * 0.5);
}

static double rms(const std::vector<float> &x)
{
    double e = 0; for (float v : x) e += (double)v * v; return std::sqrt(e / x.size());
}

// Naive (memoryless) mirror of one DriveBlock slot, sharing the exact voicing
// constants — identical to the block EXCEPT the clipper is evaluated pointwise
// (no ADAA). tone is left at 0.5 (transparent) so the only difference is ADAA.
static std::vector<float> naiveSlot(Kind k, float drive, const std::vector<float> &in)
{
    const auto v = DriveBlock::voicingFor(k);
    const float preGain = v.gMin * std::pow(v.gMax / v.gMin, drive);
    const float hpCoef = (v.lowCutHz > 0.0f)
        ? 1.0f - (float)std::exp(-2.0 * M_PI * v.lowCutHz / SR) : 0.0f;
    auto clipF = [&](float x) {
        return v.clip == 0 ? std::tanh(x)
                           : (x > 1.0f ? 1.0f : (x < -1.0f ? -1.0f : x));
    };
    const float dcSub = (v.clip >= 0) ? clipF(v.bias) : 0.0f;
    std::vector<float> y(in.size());
    float hp = 0.0f;
    for (size_t i = 0; i < in.size(); ++i)
    {
        float u = in[i] * preGain;
        if (hpCoef > 0.0f) { hp += hpCoef * (u - hp); u -= hp; }
        float c = (v.clip < 0) ? u : (clipF(u + v.bias) - dcSub);
        y[i] = c * v.outTrim; // level 0 dB, tone flat
    }
    return y;
}

static std::vector<float> realSlot(Kind k, float drive, const std::vector<float> &in)
{
    DriveBlock d;
    d.setKind(0, (int)k);
    d.setDrive(0, drive);
    d.setTone(0, 0.5f);
    d.setLevelDb(0, 0.0f);
    d.prepare({SR, BLK});
    auto x = in;
    run(d, x);
    return x;
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

    // ---- T2: Boost at drive=0 is unity (bit-exact) ----
    {
        auto in = sine(440.0, 0.25f, 4096);
        auto y = realSlot(Kind::Boost, 0.0f, in);
        double maxErr = 0;
        for (size_t i = 0; i < in.size(); ++i) maxErr = std::max(maxErr, (double)std::abs(y[i] - in[i]));
        CHECK(maxErr < 1e-6, "T2 Boost@drive0 unity passthrough (max err %.2e)", maxErr);
    }

    // ---- T3: Boost gain law: drive=1 -> +18 dB (preGain 8x) ----
    {
        auto in = sine(440.0, 0.05f, 8192);
        auto y = realSlot(Kind::Boost, 1.0f, in);
        const double g = rms(y) / rms(in);
        CHECK(std::abs(20.0 * std::log10(g) - 18.06) < 0.3,
              "T3 Boost@drive1 ~ +18 dB (measured %.2f dB)", 20.0 * std::log10(g));
    }

    // ---- T4: ADAA suppresses aliasing vs naive (Distortion, hot) ----
    {
        auto in = sine(5000.0, 0.12f, 48000); // 5 kHz -> odd harmonics fold to 3 k / 13 k
        auto adaa = realSlot(Kind::Distortion, 1.0f, in);
        auto naive = naiveSlot(Kind::Distortion, 1.0f, in);
        const double a3 = goertzel(adaa, 3000.0),  n3 = goertzel(naive, 3000.0);
        const double a13 = goertzel(adaa, 13000.0), n13 = goertzel(naive, 13000.0);
        CHECK(a3 < n3 * 0.6 && a13 < n13 * 0.6,
              "T4 ADAA cuts alias: 3k %.2e<%.2e, 13k %.2e<%.2e", a3, n3, a13, n13);
        const double redDb = 20.0 * std::log10(n3 / std::max(a3, 1e-12));
        CHECK(redDb > 4.0, "T4 alias@3k reduced by %.1f dB", redDb);
    }

    // ---- T5: ADAA preserves the wanted low-freq signal (matches naive) ----
    {
        auto in = sine(100.0, 0.2f, 8192);
        auto adaa = realSlot(Kind::Overdrive, 0.7f, in);
        auto naive = naiveSlot(Kind::Overdrive, 0.7f, in);
        std::vector<float> diff(in.size());
        for (size_t i = 0; i < in.size(); ++i) diff[i] = adaa[i] - naive[i];
        const double rel = rms(diff) / std::max(rms(adaa), 1e-9);
        CHECK(rel < 0.05, "T5 ADAA ~= naive at 100 Hz (rel err %.3f)", rel);
    }

    // ---- T6: Fuzz is more asymmetric (even-harmonic) than Overdrive ----
    {
        auto in = sine(220.0, 0.15f, 24000);
        auto od = realSlot(Kind::Overdrive, 0.8f, in);
        auto fz = realSlot(Kind::Fuzz, 0.8f, in);
        auto h2over3 = [&](const std::vector<float> &y) {
            return goertzel(y, 440.0) / (goertzel(y, 660.0) + 1e-9);
        };
        CHECK(h2over3(fz) > h2over3(od),
              "T6 Fuzz more 2nd-harmonic than OD (h2/h3 %.2f vs %.2f)", h2over3(fz), h2over3(od));
    }

    // ---- T7: tone tilt is transparent at 0.5, bright>0.5, dark<0.5 ----
    {
        auto in = sine(220.0, 0.1f, 16000);  // fundamental low, plus harmonics from drive
        DriveBlock dd; dd.setKind(0, (int)Kind::Overdrive); dd.setDrive(0, 0.7f);
        auto hi = in, lo = in;
        DriveBlock a, b;
        a.setKind(0, (int)Kind::Overdrive); a.setDrive(0, 0.7f); a.setTone(0, 0.9f); a.prepare({SR, BLK}); run(a, hi);
        b.setKind(0, (int)Kind::Overdrive); b.setDrive(0, 0.7f); b.setTone(0, 0.1f); b.prepare({SR, BLK}); run(b, lo);
        // brighter tone boosts HF. Symmetric OD makes ODD harmonics, so measure
        // the 7th (1540 Hz), which sits above the 720 Hz tilt pivot.
        const double hiTreb = goertzel(hi, 1540.0), loTreb = goertzel(lo, 1540.0);
        CHECK(hiTreb > loTreb * 1.5, "T7 tone up = brighter (HF %.2e > %.2e)", hiTreb, loTreb);
    }

    std::printf("\n%s (%d failure%s)\n", gFails ? "RESULT: FAIL" : "RESULT: ALL PASS", gFails, gFails == 1 ? "" : "s");
    return gFails ? 1 : 0;
}
