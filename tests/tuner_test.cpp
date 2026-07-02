// tuner_test — offline verification for the Tuner pitch detector (measurement
// first). Exits nonzero on any FAIL. No JUCE: Tuner.h is pure std C++.
#include "rig/Tuner.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <string>

using nam_rig::Tuner;

static int gFails = 0;
#define CHECK(cond, ...) do { \
    const bool ok_ = (cond); \
    std::printf("%s: ", ok_ ? "PASS" : "FAIL"); std::printf(__VA_ARGS__); std::printf("\n"); \
    if (!ok_) ++gFails; } while (0)

static constexpr double SR = 48000.0;
static constexpr int BLK = 512;

// error in cents between measured and expected frequency
static double cents(double measured, double expected)
{
    return 1200.0 * std::log2(measured / expected);
}

// Feed a generated tone through the tuner in BLK-sized blocks and return the
// final published frequency. `gen(i)` produces sample i.
template <class Gen>
static double detect(Tuner &t, int n, Gen gen)
{
    std::vector<float> x((size_t)n);
    for (int i = 0; i < n; ++i) x[(size_t)i] = (float)gen(i);
    for (int p = 0; p < n; p += BLK)
        t.push(x.data() + p, std::min(BLK, n - p));
    return t.frequency();
}

int main()
{
    // ---- T1: standard EADGBE tuning, pure sines, within 5 cents ----
    struct Note { const char *name; double hz; };
    const Note guitar[] = {
        {"E2", 82.41}, {"A2", 110.00}, {"D3", 146.83},
        {"G3", 196.00}, {"B3", 246.94}, {"E4", 329.63}};
    {
        bool ok = true; double worst = 0;
        for (const auto &nt : guitar)
        {
            Tuner t; t.prepare(SR);
            const double f = detect(t, 48000, [&](int i) {
                return 0.5 * std::sin(2.0 * M_PI * nt.hz * i / SR);
            });
            const double e = std::abs(cents(f, nt.hz));
            if (e > worst) worst = e;
            if (e > 5.0) { ok = false; std::printf("   %s: %.2f Hz (%.1f cents)\n", nt.name, f, cents(f, nt.hz)); }
        }
        CHECK(ok, "T1 standard tuning detected within 5 cents (worst %.2f)", worst);
    }

    // ---- T2: bass low B (~31 Hz) and a high note (~1175 Hz) still resolve ----
    {
        Tuner tb; tb.prepare(SR);
        const double fb = detect(tb, 96000, [&](int i){ return 0.5*std::sin(2.0*M_PI*30.87*i/SR); });
        Tuner th; th.prepare(SR);
        const double fh = detect(th, 48000, [&](int i){ return 0.5*std::sin(2.0*M_PI*1174.66*i/SR); });
        CHECK(std::abs(cents(fb, 30.87)) < 10.0 && std::abs(cents(fh, 1174.66)) < 10.0,
              "T2 extremes: B0 %.2f Hz, D6 %.2f Hz (want within 10 cents)", fb, fh);
    }

    // ---- T3: harmonic-rich sawtooth -> reports the FUNDAMENTAL, not an octave ----
    {
        Tuner t; t.prepare(SR);
        const double f0 = 110.0; // A2
        const double f = detect(t, 48000, [&](int i) {
            double s = 0.0; // band-limited-ish sawtooth: sum of harmonics
            for (int k = 1; k <= 12; ++k) s += std::sin(2.0 * M_PI * f0 * k * i / SR) / k;
            return 0.3 * s;
        });
        CHECK(std::abs(cents(f, f0)) < 15.0,
              "T3 harmonic-rich saw reads fundamental %.2f Hz (want ~110, <15 cents)", f);
    }

    // ---- T4: a detuned string reads its true (offset) pitch ----
    {
        Tuner t; t.prepare(SR);
        const double target = 110.0 * std::pow(2.0, 30.0 / 1200.0); // A2 + 30 cents
        const double f = detect(t, 48000, [&](int i){ return 0.5*std::sin(2.0*M_PI*target*i/SR); });
        CHECK(std::abs(cents(f, target)) < 5.0,
              "T4 detuned +30c reads %.1f cents off nominal A2 (want ~+30)", cents(f, 110.0));
    }

    // ---- T5: silence -> no pitch, low clarity ----
    {
        Tuner t; t.prepare(SR);
        const double f = detect(t, 24000, [&](int){ return 0.0; });
        CHECK(f == 0.0f && t.clarity() < 0.5f, "T5 silence -> freq 0 (%.1f), clarity %.2f", f, t.clarity());
    }

    // ---- T6: clean tone yields high clarity ----
    {
        Tuner t; t.prepare(SR);
        detect(t, 48000, [&](int i){ return 0.5*std::sin(2.0*M_PI*196.0*i/SR); });
        CHECK(t.clarity() > 0.9f, "T6 clean tone clarity %.3f (want > 0.9)", t.clarity());
    }

    std::printf("\n%s (%d failure%s)\n", gFails ? "RESULT: FAIL" : "RESULT: ALL PASS", gFails, gFails == 1 ? "" : "s");
    return gFails ? 1 : 0;
}
