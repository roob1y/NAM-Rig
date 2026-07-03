// env_filter_test — offline verification for Svf.h + EnvFilterBlock.h.
// Build: g++ -std=c++17 -O2 -I src -I plate_proto/stub tests/env_filter_test.cpp -o /tmp/eft && /tmp/eft
#include "rig/Svf.h"
#include "rig/EnvFilterBlock.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <random>

using nam_rig::Svf;
using nam_rig::EnvFilterBlock;
using nam_rig::BlockContext;

static int gFails = 0;
#define CHECK(cond, ...) do { \
    const bool chkOk_ = (cond); \
    std::printf("%s: ", chkOk_ ? "PASS" : "FAIL"); std::printf(__VA_ARGS__); std::printf("\n"); \
    if (!chkOk_) ++gFails; } while (0)

static constexpr double SR = 48000.0;
static constexpr int BLK = 512;

enum Tap { LP, BP, HP, NOTCH };
static double svfToneRms(float cutoff, float Q, double freq, Tap tap)
{
    Svf f; f.prepare(SR); f.setCoeffs(cutoff, Q);
    const int n = 24000;
    double sum = 0.0; int cnt = 0;
    for (int i = 0; i < n; ++i)
    {
        const float x = (float)std::sin(2.0 * M_PI * freq * i / SR);
        f.setCoeffs(cutoff, Q);
        Svf::Out o = f.tick(x);
        const float y = (tap == LP) ? o.lp : (tap == BP) ? o.bp : (tap == HP) ? o.hp : o.notch;
        if (i > n / 2) { sum += (double)y * y; ++cnt; }
    }
    return std::sqrt(sum / std::max(1, cnt));
}

static void run(EnvFilterBlock &b, std::vector<float> &x)
{
    for (size_t p = 0; p < x.size(); p += BLK)
    {
        const int n = (int)std::min<size_t>(BLK, x.size() - p);
        b.process(x.data() + p, n);
    }
}

static std::vector<float> tone(double freq, double amp, int n)
{
    std::vector<float> v((size_t)n);
    for (int i = 0; i < n; ++i) v[(size_t)i] = (float)(amp * std::sin(2.0 * M_PI * freq * i / SR));
    return v;
}

int main()
{
    std::printf("=== env_filter_test (SR=%.0f) ===\n", SR);

    { // T1: SVF tap correctness
        const float fc = 1000.0f, Q = 3.0f;
        double lpLow = svfToneRms(fc, Q, 200.0, LP), lpHigh = svfToneRms(fc, Q, 5000.0, LP);
        double hpLow = svfToneRms(fc, Q, 200.0, HP), hpHigh = svfToneRms(fc, Q, 5000.0, HP);
        double bpAt = svfToneRms(fc, Q, 1000.0, BP), bpOff = svfToneRms(fc, Q, 5000.0, BP);
        double nAt = svfToneRms(fc, Q, 1000.0, NOTCH), nOff = svfToneRms(fc, Q, 200.0, NOTCH);
        CHECK(lpLow > lpHigh * 4.0, "T1 LP low(%.3f) >> high(%.3f)", lpLow, lpHigh);
        CHECK(hpHigh > hpLow * 4.0, "T1 HP high(%.3f) >> low(%.3f)", hpHigh, hpLow);
        CHECK(bpAt > bpOff * 3.0, "T1 BP at(%.3f) >> off(%.3f)", bpAt, bpOff);
        CHECK(nAt < nOff * 0.5, "T1 Notch at(%.3f) << off(%.3f)", nAt, nOff);
    }
    { // T2: SVF stability at max Q under a full-scale per-sample cutoff sweep
        Svf f; f.prepare(SR);
        std::mt19937 rng(1);
        std::uniform_real_distribution<float> cut(20.0f, 20000.0f), sig(-1.0f, 1.0f);
        bool finite = true; float peak = 0.0f;
        for (int i = 0; i < 200000; ++i)
        {
            f.setCoeffs(cut(rng), 60.0f);
            Svf::Out o = f.tick(sig(rng));
            if (!std::isfinite(o.lp) || !std::isfinite(o.bp) || !std::isfinite(o.hp)) finite = false;
            peak = std::max(peak, std::abs(o.bp));
        }
        CHECK(finite, "T2 SVF finite at max Q under random per-sample sweep");
        CHECK(peak < 1.0e3f, "T2 SVF bounded under sweep (peak bp=%.2f)", peak);
    }
    { // T3: envelope -> cutoff tracking, Up
        EnvFilterBlock b; b.prepare({SR, BLK});
        b.setVoice(EnvFilterBlock::kFX25); b.setDirectionUp(true); b.setMix(1.0f);
        std::vector<float> sil((size_t)(SR * 0.2), 0.0f); run(b, sil);
        const float rest = b.currentCutoffHz();
        std::vector<float> loud = tone(440.0, 0.3, (int)(SR * 0.2)); run(b, loud);
        const float open = b.currentCutoffHz();
        std::vector<float> sil2((size_t)(SR * 0.6), 0.0f); run(b, sil2);
        const float closed = b.currentCutoffHz();
        CHECK(open > rest * 3.0f, "T3 Up loud opens (rest=%.0f -> %.0f Hz)", rest, open);
        CHECK(closed < open * 0.5f, "T3 Up release closes (%.0f -> %.0f Hz)", open, closed);
    }
    { // T4: Down direction inverts
        EnvFilterBlock b; b.prepare({SR, BLK});
        b.setVoice(EnvFilterBlock::kFX25); b.setDirectionUp(false); b.setMix(1.0f);
        std::vector<float> sil((size_t)(SR * 0.2), 0.0f); run(b, sil);
        const float rest = b.currentCutoffHz();
        std::vector<float> loud = tone(440.0, 0.3, (int)(SR * 0.2)); run(b, loud);
        const float open = b.currentCutoffHz();
        CHECK(open < rest * 0.5f, "T4 Down loud darkens (rest=%.0f -> %.0f Hz)", rest, open);
    }
    { // T5: mix=0 bit-exact passthrough (FX25 Blend fully CCW)
        EnvFilterBlock b; b.prepare({SR, BLK});
        b.setMix(0.0f); b.setResonance(1.0f);
        std::mt19937 rng(7);
        std::uniform_real_distribution<float> sig(-0.8f, 0.8f);
        std::vector<float> in((size_t)4096), work((size_t)4096);
        for (size_t i = 0; i < in.size(); ++i) { in[i] = sig(rng); work[i] = in[i]; }
        run(b, work);
        bool exact = true;
        for (size_t i = 0; i < in.size(); ++i) if (work[i] != in[i]) { exact = false; break; }
        CHECK(exact, "T5 mix=0 bit-exact passthrough");
    }
    { // T6: no non-finite over random input, both voices, high Q; incl Q-Tron Boost
        for (int v = 0; v <= 1; ++v)
        {
            EnvFilterBlock b; b.prepare({SR, BLK});
            b.setVoice(v); b.setResonance(1.0f); b.setDepth(1.0f); b.setSensitivity(1.0f); b.setMix(1.0f);
            if (v == 1) b.setBoost(true);
            std::mt19937 rng(42 + v);
            std::uniform_real_distribution<float> sig(-1.0f, 1.0f);
            std::vector<float> x((size_t)(SR * 1.0));
            for (auto &s : x) s = sig(rng);
            run(b, x);
            bool finite = true; float peak = 0.0f;
            for (float s : x) { if (!std::isfinite(s)) finite = false; peak = std::max(peak, std::abs(s)); }
            CHECK(finite, "T6 voice %d finite on random input", v);
            CHECK(peak < 50.0f, "T6 voice %d bounded (peak=%.2f)", v, peak);
        }
    }
    { // T7: input-Z loading — Q-Tron (300k) sheds more top end than FX25 (500k)
        auto hfRms = [](int voice) {
            EnvFilterBlock b; b.prepare({SR, BLK});
            b.setVoice(voice); b.setMode(EnvFilterBlock::kHP); b.setResonance(0.0f);
            b.setSensitivity(0.0f); b.setDepth(0.0f); b.setRange(0.0f); b.setMix(1.0f);
            std::vector<float> x = tone(7000.0, 0.5, (int)(SR * 1.0));
            run(b, x);
            double s = 0.0; int c = 0;
            for (size_t i = x.size() / 2; i < x.size(); ++i) { s += (double)x[i] * x[i]; ++c; }
            return std::sqrt(s / std::max(1, c));
        };
        const double fx = hfRms(EnvFilterBlock::kFX25), qt = hfRms(EnvFilterBlock::kQTron);
        CHECK(qt < fx, "T7 input-Z loading: Q-Tron 7kHz(%.4f) < FX25(%.4f)", qt, fx);
    }
    { // T8: input/output coupling high-passes kill DC
        EnvFilterBlock b; b.prepare({SR, BLK});
        b.setVoice(EnvFilterBlock::kFX25); b.setMix(1.0f);
        std::vector<float> d((size_t)(SR * 1.0), 0.5f);
        run(b, d);
        double m = 0.0; int c = 0;
        for (size_t i = d.size() / 2; i < d.size(); ++i) { m += d[i]; ++c; }
        m /= std::max(1, c);
        CHECK(std::abs(m) < 0.01, "T8 coupling kills DC (0.5 -> mean %.5f)", m);
    }
    { // T9: Q-Tron MIX mode blends BP with dry (passes more low tone than pure BP)
        auto rms = [](int mode) {
            EnvFilterBlock b; b.prepare({SR, BLK});
            b.setVoice(EnvFilterBlock::kQTron); b.setMode(mode); b.setResonance(0.3f);
            b.setSensitivity(0.3f); b.setRange(0.5f); b.setMix(1.0f);
            std::vector<float> x = tone(300.0, 0.3, (int)(SR * 0.5));
            run(b, x);
            double s = 0.0; int c = 0;
            for (size_t i = x.size() / 2; i < x.size(); ++i) { s += (double)x[i] * x[i]; ++c; }
            return std::sqrt(s / std::max(1, c));
        };
        const double bp = rms(EnvFilterBlock::kBP), mx = rms(EnvFilterBlock::kMix);
        CHECK(mx > bp, "T9 MIX>BP low-tone (mix=%.4f > bp=%.4f)", mx, bp);
    }
    { // T10: ADAA reduces the Q-Tron Boost soft-clip aliasing (2nd < 1st < naive)
        auto aliasE = [](int order) {
            EnvFilterBlock b; b.prepare({SR, BLK});
            b.setVoice(EnvFilterBlock::kQTron); b.setBoost(true); b.setAdaaOrder(order);
            b.setMode(EnvFilterBlock::kHP); b.setResonance(0.0f); b.setSensitivity(0.0f);
            b.setDepth(0.0f); b.setRange(0.4f); b.setMix(1.0f);
            std::vector<float> x = tone(5000.0, 0.6, (int)(SR * 1.0));
            run(b, x);
            auto dft = [&](double f) {
                double re = 0, im = 0; int c = 0;
                for (size_t i = x.size() / 2; i < x.size(); ++i)
                { const double a = 2 * M_PI * f * i / SR; re += x[i] * std::cos(a); im += x[i] * std::sin(a); ++c; }
                return std::sqrt(re * re + im * im) / std::max(1, c);
            };
            double e = 0; for (double f : {3000.0, 7000.0, 13000.0, 17000.0, 23000.0}) e += dft(f);
            return e;
        };
        const double n0 = aliasE(0), n1 = aliasE(1), n2 = aliasE(2);
        CHECK(n1 < n0 && n2 < n1, "T10 ADAA cuts Boost aliasing (naive %.4f > 1st %.4f > 2nd %.4f)", n0, n1, n2);
    }

    std::printf("=== %s (%d fail) ===\n", gFails ? "FAILURES" : "ALL PASS", gFails);
    return gFails ? 1 : 0;
}
