// pitch_test — offline verification for the WHAMMY pitch-bend (PitchBlock.h). Build:
//   g++ -std=c++17 -O2 -I src -I plate_proto/stub tests/pitch_test.cpp -o /tmp/pt && /tmp/pt
//
// Proves (no JUCE): the treadle sweeps pitch up/down to the mode interval; the heel is
// bit-exact dry; Chords (poly) shifts a chord cleanly; Classic reports 0 latency and
// Chords reports the STFT latency; nothing blows up.
#include "rig/PitchBlock.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <random>

using nam_rig::PitchBlock;

static int gFails = 0;
#define CHECK(cond, ...) do { \
    const bool chkOk_ = (cond); \
    std::printf("%s: ", chkOk_ ? "PASS" : "FAIL"); std::printf(__VA_ARGS__); std::printf("\n"); \
    if (!chkOk_) ++gFails; } while (0)

static constexpr double SR = 48000.0;
static constexpr int BLK = 512;

static void run(PitchBlock &b, std::vector<float> &x)
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

static std::vector<float> chord(std::vector<double> fs, double amp, int n)
{
    std::vector<float> v((size_t)n, 0.0f);
    for (int i = 0; i < n; ++i)
    {
        double s = 0.0;
        for (double f : fs) s += std::sin(2.0 * M_PI * f * (double)i / SR);
        v[(size_t)i] = (float)(amp * s / (double)fs.size());
    }
    return v;
}

static double binPow(const std::vector<float> &x, double f)
{
    const size_t start = x.size() / 2;
    double re = 0.0, im = 0.0; int c = 0;
    for (size_t i = start; i < x.size(); ++i)
    {
        const double a = 2.0 * M_PI * f * (double)i / SR;
        re += x[i] * std::cos(a); im += x[i] * std::sin(a); ++c;
    }
    return (re * re + im * im) / ((double)c * (double)c);
}

int main()
{
    std::printf("=== pitch_test (Whammy) SR=%.0f ===\n", SR);

    { // W1: treadle toe (mode +1 oct) shifts to 2f; heel (0) ~ unison; a down mode drops
      // the pitch; all finite.
        auto renderW = [](int mode, float treadle){
            PitchBlock b; b.prepare({SR, BLK});
            b.setWhammyMode(mode); b.setWhammy(treadle);
            std::vector<float> x = tone(220.0, 0.3, (int)(SR * 1.2));
            run(b, x);
            return x;
        };
        std::vector<float> toe = renderW(1, 1.0f);   // +1 oct full
        std::vector<float> heel = renderW(1, 0.0f);  // unison
        CHECK(binPow(toe, 440.0) > binPow(toe, 220.0) * 4.0, "W1 Whammy toe -> +1 oct (2f)");
        CHECK(binPow(heel, 220.0) > binPow(heel, 440.0), "W1 Whammy heel ~ unison (f)");
        std::vector<float> dn = renderW(3, 1.0f);    // -1 oct
        bool fin = true; float pk = 0; for (float s2 : dn) { if (!std::isfinite(s2)) fin = false; pk = std::max(pk, std::abs(s2)); }
        CHECK(fin && pk < 20.0f && binPow(dn, 110.0) > binPow(dn, 220.0), "W1 Whammy -1 oct (f/2), finite");
    }

    { // W2: Chords (poly) shifts a two-note chord cleanly (+1 oct); Classic reports 0
      // latency, Chords reports the STFT latency.
        PitchBlock b; b.prepare({SR, BLK});
        b.setWhammyPoly(true); b.setWhammyMode(1); b.setWhammy(1.0f);
        std::vector<float> x = chord({200.0, 300.0}, 0.4, (int)(SR * 1.5));
        run(b, x);
        CHECK(binPow(x, 400.0) > binPow(x, 200.0) * 3.0 && binPow(x, 600.0) > binPow(x, 300.0) * 3.0,
              "W2 Whammy Chords shifts both notes +1 oct");
        PitchBlock cl; cl.prepare({SR, BLK}); cl.setBypassed(false);
        PitchBlock ch; ch.prepare({SR, BLK}); ch.setBypassed(false); ch.setWhammyPoly(true);
        CHECK(cl.latencySamples() == 0.0 && ch.latencySamples() > 0.0,
              "W2 Classic latency %.0f == 0, Chords %.0f > 0", cl.latencySamples(), ch.latencySamples());
    }

    { // W3: heel (treadle 0) is BIT-EXACT dry (Classic) -> no shifter warble at rest.
        PitchBlock b; b.prepare({SR, BLK});
        b.setWhammy(0.0f); b.setWhammyMode(1);
        std::mt19937 rng(21); std::uniform_real_distribution<float> sg(-0.7f, 0.7f);
        std::vector<float> in(4096), wk(4096); for (size_t i = 0; i < in.size(); ++i) { in[i] = sg(rng); wk[i] = in[i]; }
        run(b, wk);
        bool exact = true; for (size_t i = 0; i < in.size(); ++i) if (wk[i] != in[i]) { exact = false; break; }
        CHECK(exact, "W3 Whammy heel = bit-exact dry (no warble at rest)");
    }

    std::printf("=== %s (%d fail) ===\n", gFails ? "FAILURES" : "ALL PASS", gFails);
    return gFails ? 1 : 0;
}
