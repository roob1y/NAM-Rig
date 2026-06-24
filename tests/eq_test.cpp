// eq_test — offline verification for EqBlock (8-band graphic) and the
// post-cab CutFilters. The harness measures the processed impulse response
// and compares against Biquad::magnitudeAt evaluated on the SAME live
// coefficients — plus absolute checks (band gain at center, -3 dB points,
// 12 dB/oct slopes) that catch wrong coefficient formulas.
// Exits nonzero on any FAIL.
#include "rig/EqBlock.h"
#include "rig/Biquad.h"
#include <cstdio>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <vector>

using nam_rig::EqBlock;
using nam_rig::Biquad;
using nam_rig::CutFilters;
using nam_rig::BlockContext;

static int gFails = 0;
#define CHECK(cond, ...) do { \
    const bool chkOk_ = (cond); /* unique name: must not shadow caller's vars */ \
    std::printf("%s: ", chkOk_ ? "PASS" : "FAIL"); std::printf(__VA_ARGS__); std::printf("\n"); \
    if (!chkOk_) ++gFails; } while (0)

static constexpr double SR = 48000.0;
static constexpr int BLK = 512;
static constexpr int IRLEN = 32768;

// |H(f)| measured from a processed impulse response (single-frequency DFT).
static double measuredMagDb(const std::vector<float> &h, double f)
{
    double re = 0, im = 0;
    for (size_t n = 0; n < h.size(); ++n)
    {
        const double w = 2.0 * M_PI * f * (double)n / SR;
        re += (double)h[n] * std::cos(w);
        im -= (double)h[n] * std::sin(w);
    }
    return 20.0 * std::log10(std::sqrt(re * re + im * im) + 1e-30);
}

static std::vector<float> impulseThrough(EqBlock &eq)
{
    std::vector<float> h(IRLEN, 0.0f);
    h[0] = 1.0f;
    for (size_t p = 0; p < h.size(); p += BLK)
        eq.process(h.data() + p, (int)std::min<size_t>(BLK, h.size() - p));
    return h;
}

int main()
{
    // ---- T1: all flat = bit-exact passthrough ----
    {
        EqBlock eq; eq.prepare({SR, BLK});
        std::vector<float> x(4800);
        for (size_t i = 0; i < x.size(); ++i)
            x[i] = (float)std::sin(2.0 * M_PI * 1234.0 * (double)i / SR) * 0.5f;
        auto ref = x;
        for (size_t p = 0; p < x.size(); p += BLK)
            eq.process(x.data() + p, (int)std::min<size_t>(BLK, x.size() - p));
        bool bitexact = true;
        for (size_t i = 0; i < x.size(); ++i)
            if (x[i] != ref[i]) { bitexact = false; break; }
        CHECK(bitexact, "T1 all-flat EQ is bit-exact passthrough");
        CHECK(eq.latencySamples() == 0.0, "T1 PDC == 0");
    }

    // ---- T2/T3: each band, boost and cut, exact gain at center ----
    for (const float gain : {12.0f, -12.0f})
    {
        bool ok = true;
        double worst = 0;
        for (int b = 0; b < EqBlock::kNumBands; ++b)
        {
            EqBlock eq; eq.prepare({SR, BLK});
            eq.setBandGainDb(b, gain);
            auto h = impulseThrough(eq);
            const double got = measuredMagDb(h, EqBlock::kBandHz[(size_t)b]);
            const double err = std::abs(got - (double)gain);
            if (err > worst) worst = err;
            if (err > 0.3) ok = false;
        }
        CHECK(ok, "T2 band centers hit %+.0f dB exactly (worst err %.2f dB, want < 0.3)", gain, worst);
    }

    // ---- T4: cascade (all bands set) — measured == analytic product ----
    {
        EqBlock eq; eq.prepare({SR, BLK});
        const float gains[EqBlock::kNumBands] = {6.0f, -4.0f, 8.0f, -12.0f, 3.0f, -6.0f, 12.0f, -2.0f};
        for (int b = 0; b < EqBlock::kNumBands; ++b)
            eq.setBandGainDb(b, gains[b]);
        auto h = impulseThrough(eq);

        bool ok = true;
        double worst = 0;
        for (double f = 50.0; f < 12000.0; f *= 1.31)
        {
            double analyticDb = 0.0;
            for (int b = 0; b < EqBlock::kNumBands; ++b)
                analyticDb += 20.0 * std::log10(eq.filter(b).magnitudeAt(SR, f));
            const double err = std::abs(measuredMagDb(h, f) - analyticDb);
            if (err > worst) worst = err;
            if (err > 0.2) ok = false;
        }
        CHECK(ok, "T4 cascade matches analytic response (worst err %.3f dB, want < 0.2)", worst);
    }

    // ---- T5: post-cab cuts — -3 dB points and 12 dB/oct slopes ----
    {
        CutFilters cuts;
        cuts.prepare(SR);
        cuts.update(100.0, 20000.0); // HPF 100 Hz only
        CHECK(cuts.engaged(), "T5 HPF engages above 20 Hz");
        const double hp3 = 20.0 * std::log10(cuts.hpf().magnitudeAt(SR, 100.0));
        const double hpOct = 20.0 * std::log10(cuts.hpf().magnitudeAt(SR, 50.0));
        CHECK(std::abs(hp3 + 3.01) < 0.3, "T5 HPF -3 dB at cutoff (got %.2f dB)", hp3);
        CHECK(std::abs(hpOct + 12.3) < 0.7, "T5 HPF ~-12.3 dB one octave below (got %.2f dB)", hpOct);

        cuts.update(20.0, 5000.0); // LPF 5 kHz only
        const double lp3 = 20.0 * std::log10(cuts.lpf().magnitudeAt(SR, 5000.0));
        const double lpOct = 20.0 * std::log10(cuts.lpf().magnitudeAt(SR, 10000.0));
        CHECK(std::abs(lp3 + 3.01) < 0.3, "T5 LPF -3 dB at cutoff (got %.2f dB)", lp3);
        // One octave above 5 kHz is 10 kHz — a sizeable fraction of Nyquist at
        // 48 k, where bilinear warping makes the digital response fall FASTER
        // than the analog -12.3 dB (the RBJ LPF has a zero at Nyquist).
        // Bound distinguishes filter order: 1st order ~-7, 2nd ~-12..-17 here,
        // 4th ~-25. Measured -14.33 dB is correct 2nd-order behavior.
        CHECK(lpOct < -11.0 && lpOct > -18.0,
              "T5 LPF 2nd-order slope one octave above, warping allowed (got %.2f dB, want -11..-18)", lpOct);

        cuts.update(20.0, 20000.0); // both at extremes
        CHECK(!cuts.engaged(), "T5 cuts disengage at knob extremes (bit-exact path)");

        // and the filters measurably filter: impulse through HPF, DC must die
        cuts.update(100.0, 20000.0);
        std::vector<float> h(IRLEN, 0.0f); h[0] = 1.0f;
        for (size_t p = 0; p < h.size(); p += BLK)
            cuts.process(h.data() + p, (int)std::min<size_t>(BLK, h.size() - p));
        double dc = 0; for (auto v : h) dc += v;
        CHECK(std::abs(dc) < 1.0e-3, "T5 HPF kills DC in the measured impulse (sum %.2e)", dc);
    }

    // ---- T6: instant curve-based auto-gain holds output == input level ----
    // Drive a signal with EQUAL ENERGY PER OCTAVE (the makeup's design spectrum:
    // log-spaced equal-amplitude tones) and confirm the level is preserved.
    {
        const float gains[EqBlock::kNumBands] = {6, 4, -3, -2, 2, 5, 8, 4};

        // Multitone: 120 equal-amplitude tones, log-spaced 50 Hz..10 kHz.
        const int K = 120;
        const double fLo = 50.0, fHi = 10000.0;
        std::vector<double> tone(K);
        for (int k = 0; k < K; ++k)
            tone[k] = fLo * std::pow(fHi / fLo, (double)k / (double)(K - 1));
        const int N = (int)(SR * 2.0);
        std::vector<float> base(N, 0.0f);
        for (int i = 0; i < N; ++i)
        {
            double x = 0.0;
            for (int k = 0; k < K; ++k)
                x += std::sin(2.0 * M_PI * tone[k] * (double)i / SR + 0.7 * k);
            base[i] = (float)(x * 0.01);
        }

        const int skip = 4800;        // let the 15 ms gain smoother settle
        auto rmsTail = [&](const std::vector<float> &v) {
            double sq = 0; for (int i = skip; i < N; ++i) sq += (double)v[i] * v[i];
            return std::sqrt(sq / (N - skip));
        };
        const double inRms = rmsTail(base);

        EqBlock eqOn; eqOn.prepare({SR, BLK}); eqOn.setAutoGain(true);
        for (int b = 0; b < EqBlock::kNumBands; ++b) eqOn.setBandGainDb(b, gains[b]);
        std::vector<float> on = base;
        for (int p = 0; p < N; p += BLK) eqOn.process(on.data() + p, std::min(BLK, N - p));
        const double onDb = 20.0 * std::log10(rmsTail(on) / inRms);

        EqBlock eqOff; eqOff.prepare({SR, BLK}); // auto-gain off by default
        for (int b = 0; b < EqBlock::kNumBands; ++b) eqOff.setBandGainDb(b, gains[b]);
        std::vector<float> off = base;
        for (int p = 0; p < N; p += BLK) eqOff.process(off.data() + p, std::min(BLK, N - p));
        const double offDb = 20.0 * std::log10(rmsTail(off) / inRms);

        std::printf("[T6] makeup=%.3f  onDb=%.3f  offDb=%.3f\n", eqOn.makeupGain(), onDb, offDb);
        CHECK(std::abs(onDb) < 0.5, "T6 instant auto-gain holds output == input level (%.2f dB off, want < 0.5)", onDb);
        CHECK(std::abs(offDb) > 0.5, "T6 without auto-gain the shape shifts level (%.2f dB), so makeup is doing work", offDb);

        // T6b: the makeup is INSTANT — correct from the very first block, no
        // settling needed beyond the click-smoother. Check the first 256 samples
        // after the smoother lead are already within 1 dB.
        const int a = 1200, bgn = a, end = a + 4096;
        double sqi = 0, sqo = 0;
        for (int i = bgn; i < end; ++i) { sqi += (double)base[i] * base[i]; sqo += (double)on[i] * on[i]; }
        const double earlyDb = 20.0 * std::log10(std::sqrt(sqo) / std::sqrt(sqi));
        CHECK(std::abs(earlyDb) < 1.0, "T6b makeup is instant (%.2f dB off just 25 ms in, want < 1.0)", earlyDb);
    }

    std::printf("\n%s (%d failure%s)\n", gFails ? "RESULT: FAIL" : "RESULT: ALL PASS", gFails, gFails == 1 ? "" : "s");
    return gFails ? 1 : 0;
}
