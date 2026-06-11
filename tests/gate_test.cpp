// gate_test — offline verification harness for GateBlock (measurement-first,
// same culture as latency_test / chain_process). All thresholds printed and
// checked; exits nonzero on any FAIL.
//
// Last full pass: 2026-06-11 (sandbox gcc): T1 bit-exact open passthrough,
// T2 -80 dB floor, T3 PDC==lookahead + 100% transient kept (71.8% without
// lookahead — T3b shows why lookahead matters), T4 hold/release timing,
// T5 one opening on 2 s of threshold-level noise, T6 0.7 dB max step on
// decaying note.
#include "rig/GateBlock.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <random>

using nam_rig::GateBlock;
using nam_rig::BlockContext;

static int gFails = 0;
#define CHECK(cond, ...) do { \
    const bool chkOk_ = (cond); /* unique name: must not shadow caller's vars */ \
    std::printf("%s: ", chkOk_ ? "PASS" : "FAIL"); std::printf(__VA_ARGS__); std::printf("\n"); \
    if (!chkOk_) ++gFails; } while (0)

static constexpr double SR = 48000.0;
static constexpr int BLK = 512;

static void run(GateBlock &g, std::vector<float> &x)
{
    for (size_t p = 0; p < x.size(); p += BLK)
    {
        const int n = (int)std::min<size_t>(BLK, x.size() - p);
        g.process(x.data() + p, n);
    }
}

static std::vector<float> tone(double freq, double ampDb, int n, int startSilence = 0)
{
    std::vector<float> v((size_t)(n + startSilence), 0.0f);
    const float a = std::pow(10.0f, (float)ampDb / 20.0f);
    for (int i = 0; i < n; ++i)
        v[(size_t)(startSilence + i)] = a * (float)std::sin(2.0 * M_PI * freq * i / SR);
    return v;
}

int main()
{
    // ---- T1: bit-exact passthrough when open (lookahead 0) ----
    {
        GateBlock g; g.prepare({SR, BLK});
        auto x = tone(440.0, -20.0, 48000);
        auto ref = x;
        run(g, x);
        // after 20 ms of attack settling, output must equal input bitwise
        size_t from = 960;
        bool bitexact = true;
        for (size_t i = from; i < x.size(); ++i)
            if (x[i] != ref[i]) { bitexact = false; break; }
        CHECK(bitexact, "T1 open gate is bit-exact passthrough after settle");
        CHECK(g.latencySamples() == 0.0, "T1 PDC == 0 with lookahead off");
    }

    // ---- T2: closed gate attenuates to range floor ----
    {
        GateBlock g; g.setRangeDb(80.0f); g.prepare({SR, BLK});
        auto x = tone(440.0, -70.0, 48000); // 20 dB below threshold
        double peakIn = 0, peakOut = 0;
        for (auto v : x) peakIn = std::max(peakIn, (double)std::abs(v));
        run(g, x);
        for (auto v : x) peakOut = std::max(peakOut, (double)std::abs(v));
        const double attenDb = 20.0 * std::log10(peakOut / peakIn);
        CHECK(attenDb <= -79.0, "T2 closed attenuation %.1f dB (want <= -79)", attenDb);
    }

    // ---- T3: lookahead = PDC, transient preserved ----
    {
        GateBlock g; g.setLookaheadMs(2.0f); g.prepare({SR, BLK});
        const int look = (int)g.latencySamples();
        CHECK(look == 96, "T3 latencySamples == 96 (2 ms @ 48k), got %d", look);
        // burst after silence: first 1 ms of the burst must survive
        auto x = tone(2000.0, -10.0, 9600, 24000);
        auto ref = x;
        run(g, x);
        double eIn = 0, eOut = 0;
        for (int i = 0; i < 48; ++i) // first 1 ms of the burst
        {
            const float r = ref[(size_t)(24000 + i)];
            const float o = x[(size_t)(24000 + i + look)]; // compensate PDC
            eIn += (double)r * r; eOut += (double)o * o;
        }
        const double keepPct = 100.0 * eOut / eIn;
        CHECK(keepPct > 95.0, "T3 first-1ms transient energy kept: %.1f%% (want > 95%%)", keepPct);
    }

    // ---- T3b: same burst WITHOUT lookahead loses attack energy (why T3 matters) ----
    {
        GateBlock g; g.prepare({SR, BLK});
        auto x = tone(2000.0, -10.0, 9600, 24000);
        auto ref = x;
        run(g, x);
        double eIn = 0, eOut = 0;
        for (int i = 0; i < 24; ++i) // first 0.5 ms
        {
            const float r = ref[(size_t)(24000 + i)]; const float o = x[(size_t)(24000 + i)];
            eIn += (double)r * r; eOut += (double)o * o;
        }
        std::printf("INFO: T3b without lookahead, first-0.5ms energy kept: %.1f%%\n", 100.0 * eOut / eIn);
    }

    // ---- T4: hold + release timing ----
    {
        GateBlock g; g.setHoldMs(50.0f); g.setReleaseMs(100.0f); g.prepare({SR, BLK});
        // 0.5 s tone then a -70 dB probe tail (below close threshold) so the
        // applied gain stays observable after the note ends
        auto x = tone(440.0, -20.0, 24000);
        x.resize(72000, 0.0f);
        for (size_t i = 24000; i < x.size(); ++i)
            x[i] = 0.000316f * (float)std::sin(2.0 * M_PI * 440.0 * (double)i / SR); // -70 dB probe (below close thresh)
        run(g, x);
        auto envAt = [&](int center)
        {
            double e = 0; for (int i = -48; i < 48; ++i) { double v = x[(size_t)(center + i)]; e += v * v; }
            return std::sqrt(e / 96.0) / (0.000316 / std::sqrt(2.0));
        };
        const double gHold = envAt(24000 + (int)(0.040 * SR)); // 40 ms after end: inside hold
        const double gRel  = envAt(24000 + (int)(0.250 * SR)); // 250 ms after: deep into release
        CHECK(gHold > 0.9, "T4 still open during hold (40 ms): gain %.2f (want > 0.9)", gHold);
        CHECK(gRel < 0.05, "T4 released by 250 ms: gain %.3f (want < 0.05)", gRel);
    }

    // ---- T5: chatter — noise hovering at threshold must not toggle rapidly ----
    {
        GateBlock g; g.setHoldMs(20.0f); g.prepare({SR, BLK});
        std::mt19937 rng(11);
        std::normal_distribution<float> nd(0.0f, 1.0f);
        // noise with RMS right AT the open threshold (-50 dB)
        const float amp = std::pow(10.0f, -50.0f / 20.0f);
        std::vector<float> x(96000);
        for (auto &v : x) v = amp * nd(rng);
        run(g, x);
        // count "openings": output envelope transitions from quiet to loud
        int transitions = 0; bool loud = false;
        for (size_t i = 0; i + 96 <= x.size(); i += 96)
        {
            double e = 0; for (int k = 0; k < 96; ++k) { double v = x[i + k]; e += v * v; }
            const double db = 10.0 * std::log10(e / 96.0 + 1e-20);
            const bool nowLoud = db > -56.0;
            if (nowLoud && !loud) ++transitions;
            if (nowLoud != loud) loud = nowLoud;
        }
        CHECK(transitions <= 10, "T5 openings on threshold-level noise over 2 s: %d (want <= 10)", transitions);
    }

    // ---- T6: decaying note fades without steps ----
    {
        GateBlock g; g.setReleaseMs(150.0f); g.prepare({SR, BLK});
        // tone decaying from -20 dB through the threshold over 2 s
        std::vector<float> x(96000);
        for (size_t i = 0; i < x.size(); ++i)
        {
            const double t = (double)i / SR;
            const double amp = std::pow(10.0, (-20.0 - 30.0 * t / 2.0) / 20.0); // -20 -> -50 dB
            x[i] = (float)(amp * std::sin(2.0 * M_PI * 220.0 * t));
        }
        run(g, x);
        // envelope must fall without any jump > 6 dB between 10 ms frames
        double prevDb = 0; double maxStep = 0;
        for (size_t i = 0; i + 480 <= x.size(); i += 480)
        {
            double e = 0; for (int k = 0; k < 480; ++k) { double v = x[i + k]; e += v * v; }
            const double db = 10.0 * std::log10(e / 480.0 + 1e-20);
            if (i > 0 && prevDb - db > maxStep) maxStep = prevDb - db;
            prevDb = db;
        }
        CHECK(maxStep < 6.0, "T6 max 10ms-frame drop on decaying note: %.1f dB (want < 6)", maxStep);
    }

    // ---- T7: disabled gate = bit-exact passthrough, delay (PDC) still runs ----
    {
        GateBlock g; g.setLookaheadMs(2.0f); g.setEnabled(false); g.prepare({SR, BLK});
        const int look = (int)g.latencySamples();
        // quiet signal (below threshold!) must pass anyway, delayed by exactly 'look'
        auto x = tone(440.0, -70.0, 48000);
        auto ref = x;
        run(g, x);
        bool bitexact = true;
        for (size_t i = 4800; i + (size_t)look < x.size(); ++i)
            if (x[i + (size_t)look] != ref[i]) { bitexact = false; break; }
        CHECK(bitexact, "T7 disabled gate passes sub-threshold signal bit-exact, delayed by PDC");
        CHECK(g.latencySamples() == 96.0, "T7 PDC still reported when disabled (96), got %.0f", g.latencySamples());
    }

    std::printf("\n%s (%d failure%s)\n", gFails ? "RESULT: FAIL" : "RESULT: ALL PASS", gFails, gFails == 1 ? "" : "s");
    return gFails ? 1 : 0;
}
