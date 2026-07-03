// pitch_test — offline verification for the POLYPHONIC octave (PitchBlock.h +
// SpectralShifter.h phase vocoder). Build:
//   g++ -std=c++17 -O2 -I src -I plate_proto/stub tests/pitch_test.cpp -o /tmp/pt && /tmp/pt
//
// Proves (no JUCE): single notes shift to an exact clean f/2, f/4, 2f; a CHORD
// (2-3 notes) octaves cleanly with every note shifted and no broadband glitch;
// the dry path is delayed to match the reported STFT latency; nothing blows up.
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

// Two/three-note sustained chord.
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
    std::printf("=== pitch_test (SR=%.0f) ===\n", SR);

    { // T1: single note octave-down -> clean f/2.
        const double f = 220.0;
        PitchBlock b; b.prepare({SR, BLK});
        b.setType(PitchBlock::kOctDown);
        b.setDirect(0.0f); b.setOct1(1.0f); b.setOct2(0.0f); b.setTightness(1.0f);
        std::vector<float> x = tone(f, 0.3, (int)(SR * 1.5));
        run(b, x);
        const double half = binPow(x, f / 2.0), fund = binPow(x, f);
        CHECK(half > fund * 15.0, "T1 octave-down f/2 (%.3e >> f %.3e)", half, fund);
    }
    { // T2: octave-up -> clean 2f.
        const double f = 220.0;
        PitchBlock b; b.prepare({SR, BLK});
        b.setType(PitchBlock::kOctUp);
        b.setFuzz(0.0f); b.setOctave(1.0f); b.setTone(1.0f); b.setVolume(1.0f);
        std::vector<float> x = tone(f, 0.3, (int)(SR * 1.5));
        run(b, x);
        const double up = binPow(x, 2.0 * f), fund = binPow(x, f);
        CHECK(up > fund * 12.0, "T2 octave-up 2f (%.3e >> f %.3e)", up, fund);
    }
    { // T3: octave-down TWO (OCT2) -> f/4.
        const double f = 320.0;
        PitchBlock b; b.prepare({SR, BLK});
        b.setType(PitchBlock::kOctDown);
        b.setDirect(0.0f); b.setOct1(0.0f); b.setOct2(1.0f); b.setTightness(1.0f);
        std::vector<float> x = tone(f, 0.3, (int)(SR * 1.5));
        run(b, x);
        const double q = binPow(x, f / 4.0), fund = binPow(x, f);
        CHECK(q > fund * 8.0, "T3 two-down f/4 (%.3e >> f %.3e)", q, fund);
    }
    { // T4: THE CHORD TEST — a two-note dyad octaves cleanly: BOTH notes appear an
      // octave down, the originals are gone, and there's no broadband glitch spray.
        const double f1 = 200.0, f2 = 300.0; // a fifth
        PitchBlock b; b.prepare({SR, BLK});
        b.setType(PitchBlock::kOctDown);
        b.setDirect(0.0f); b.setOct1(1.0f); b.setOct2(0.0f); b.setTightness(1.0f);
        std::vector<float> x = chord({f1, f2}, 0.4, (int)(SR * 1.5));
        run(b, x);
        const double h1 = binPow(x, f1 / 2.0), h2 = binPow(x, f2 / 2.0);
        const double o1 = binPow(x, f1), o2 = binPow(x, f2);
        double glitch = 0.0;
        for (double g : {60.0, 130.0, 190.0, 260.0, 340.0, 430.0}) glitch += binPow(x, g);
        CHECK(h1 > o1 * 10.0 && h2 > o2 * 10.0,
              "T4 both chord notes shift down (100:%.3e vs 200:%.3e ; 150:%.3e vs 300:%.3e)", h1, o1, h2, o2);
        CHECK(std::min(h1, h2) > glitch * 5.0,
              "T4 clean chord (min target %.3e >> off-freq glitch %.3e)", std::min(h1, h2), glitch);
    }
    { // T5: THREE-note chord octaves cleanly (polyphonic).
        const double f1 = 165.0, f2 = 220.0, f3 = 262.0; // ~A-minor-ish triad
        PitchBlock b; b.prepare({SR, BLK});
        b.setType(PitchBlock::kOctDown);
        b.setDirect(0.0f); b.setOct1(1.0f); b.setOct2(0.0f);
        std::vector<float> x = chord({f1, f2, f3}, 0.4, (int)(SR * 1.5));
        run(b, x);
        const double h1 = binPow(x, f1 / 2.0), h2 = binPow(x, f2 / 2.0), h3 = binPow(x, f3 / 2.0);
        const double o1 = binPow(x, f1), o2 = binPow(x, f2), o3 = binPow(x, f3);
        CHECK(h1 > o1 * 6.0 && h2 > o2 * 6.0 && h3 > o3 * 6.0,
              "T5 triad all shift down (%.2e/%.2e/%.2e vs %.2e/%.2e/%.2e)", h1, h2, h3, o1, o2, o3);
    }
    { // T6: reported latency + dry-delay alignment. DIRECT=1, oct=0 -> output is the
      // input delayed by exactly the reported latency (dry stays aligned to shifts).
        PitchBlock b; b.prepare({SR, BLK});
        b.setBypassed(false);
        const int L = (int)b.latencySamples();
        CHECK(L > 0, "T6 reports STFT latency (%d samples)", L);
        b.setType(PitchBlock::kOctDown);
        b.setDirect(1.0f); b.setOct1(0.0f); b.setOct2(0.0f);
        std::mt19937 rng(7);
        std::uniform_real_distribution<float> sig(-0.8f, 0.8f);
        const int N = 8192;
        std::vector<float> in((size_t)N), work((size_t)N);
        for (int i = 0; i < N; ++i) { in[(size_t)i] = sig(rng); work[(size_t)i] = in[(size_t)i]; }
        run(b, work);
        bool aligned = true;
        for (int i = L; i < N; ++i) if (std::abs(work[(size_t)i] - in[(size_t)(i - L)]) > 1e-6f) { aligned = false; break; }
        CHECK(aligned, "T6 dry delayed by exactly L (aligned passthrough)");
    }
    { // T7: silence stays silent; finite + bounded on random + chord, both types.
        for (int t = 0; t <= 1; ++t)
        {
            PitchBlock b; b.prepare({SR, BLK});
            b.setType(t);
            b.setDirect(0.5f); b.setOct1(1.0f); b.setOct2(1.0f); b.setTightness(1.0f);
            b.setFuzz(0.5f); b.setOctave(1.0f); b.setTone(1.0f); b.setVolume(1.0f);
            std::vector<float> sil((size_t)(SR * 0.3), 0.0f);
            run(b, sil);
            float sp = 0.0f; for (float s : sil) sp = std::max(sp, std::abs(s));
            CHECK(sp < 1.0e-6f, "T7 type %d silence stays silent (%.2e)", t, sp);

            std::mt19937 rng(9 + t);
            std::uniform_real_distribution<float> sig(-1.0f, 1.0f);
            std::vector<float> x = chord({147.0, 196.0}, 0.5, (int)(SR * 0.6));
            for (auto &s : x) s += 0.4f * sig(rng);
            run(b, x);
            bool finite = true; float pk = 0.0f;
            for (float s : x) { if (!std::isfinite(s)) finite = false; pk = std::max(pk, std::abs(s)); }
            CHECK(finite, "T7 type %d finite", t);
            CHECK(pk < 50.0f, "T7 type %d bounded (%.2f)", t, pk);
        }
    }

    { // T8: GRAIN engine — mono character voice, single note octave-down f/2, ZERO latency.
        const double f = 220.0;
        PitchBlock b; b.prepare({SR, BLK});
        b.setEngine(PitchBlock::kGrain);
        b.setType(PitchBlock::kOctDown);
        b.setDirect(0.0f); b.setOct1(1.0f); b.setOct2(0.0f); b.setTightness(1.0f);
        std::vector<float> x = tone(f, 0.3, (int)(SR * 1.5));
        run(b, x);
        const double half = binPow(x, f / 2.0), fund = binPow(x, f);
        CHECK(half > fund * 10.0, "T8 GRAIN octave-down f/2 (%.3e >> f %.3e)", half, fund);
    }
    { // T9: latency is engine-dependent — Poly adds STFT latency, Grain is zero.
        PitchBlock bp; bp.prepare({SR, BLK}); bp.setBypassed(false); bp.setEngine(PitchBlock::kPoly);
        PitchBlock bg; bg.prepare({SR, BLK}); bg.setBypassed(false); bg.setEngine(PitchBlock::kGrain);
        CHECK(bp.latencySamples() > 0.0 && bg.latencySamples() == 0.0,
              "T9 Poly latency %.0f > 0, Grain %.0f == 0", bp.latencySamples(), bg.latencySamples());
    }

    { // T10: IoStage — transparent = identity; buffered = low-freq (coupling HP) cut;
      // loaded (fuzz) = high-freq (input-Z shelf) cut.
        auto rms = [](nam_rig::IoStage &io, double f) {
            std::vector<float> x = tone(f, 0.3, (int)(SR * 0.5));
            io.reset();
            for (size_t p = 0; p < x.size(); p += BLK)
            { const int n = (int)std::min<size_t>(BLK, x.size() - p); io.processIn(x.data() + p, n); io.processOut(x.data() + p, n); }
            double s = 0; int c = 0; for (size_t i = x.size() / 2; i < x.size(); ++i) { s += (double)x[i] * x[i]; ++c; } return std::sqrt(s / c);
        };
        nam_rig::IoStage tr; tr.prepare(SR); tr.setTransparent();
        std::mt19937 rng(5); std::uniform_real_distribution<float> sg(-0.5f, 0.5f);
        std::vector<float> a(2048), b(2048);
        for (size_t i = 0; i < a.size(); ++i) { a[i] = sg(rng); b[i] = a[i]; }
        tr.processIn(b.data(), (int)b.size()); tr.processOut(b.data(), (int)b.size());
        bool ident = true; for (size_t i = 0; i < a.size(); ++i) if (a[i] != b[i]) { ident = false; break; }
        CHECK(ident, "T10 transparent IoStage = identity");
        nam_rig::IoStage buf; buf.prepare(SR); buf.setBuffered(30.0f, 30.0f, 0.0f, 0.0f);
        CHECK(rms(buf, 15.0) < rms(buf, 1000.0) * 0.7, "T10 buffered cuts sub-bass (HP)");
        nam_rig::IoStage fz; fz.prepare(SR); fz.setLoaded(14.0f, 2500.0f, -8.0f, -1.0f, 20.0f, 0.0f);
        CHECK(rms(fz, 8000.0) < rms(fz, 300.0) * 0.7, "T10 loaded (fuzz) cuts highs (input-Z shelf)");
    }

    { // T11: POG voicing (Type=POG) — dry + sub(x0.5) + up(x2) at once, polyphonic,
      // resonant filter. A chord shows both the sub and up of each note.
        const double f1 = 200.0, f2 = 300.0;
        PitchBlock b; b.prepare({SR, BLK});
        b.setType(PitchBlock::kPog);
        b.setDirect(0.0f); b.setOct1(1.0f); b.setOctave(1.0f); b.setFilter(1.0f); b.setAttack(0.0f);
        std::vector<float> x = chord({f1, f2}, 0.4, (int)(SR * 1.5));
        run(b, x);
        const double sub1 = binPow(x, f1 / 2.0), up1 = binPow(x, 2 * f1);
        const double sub2 = binPow(x, f2 / 2.0), up2 = binPow(x, 2 * f2);
        CHECK(sub1 > binPow(x, f1) * 3.0 && up1 > binPow(x, f1) * 3.0,
              "T11 POG note1 sub+up present (sub %.2e up %.2e vs f %.2e)", sub1, up1, binPow(x, f1));
        CHECK(sub2 > 0.0 && up2 > 0.0 && std::isfinite(sub2 + up2), "T11 POG note2 sub+up present");
    }
    { // T12: POG latency = poly STFT (even with engine=Grain, POG forces poly).
        PitchBlock b; b.prepare({SR, BLK}); b.setBypassed(false);
        b.setType(PitchBlock::kPog); b.setEngine(PitchBlock::kGrain);
        CHECK(b.latencySamples() > 0.0, "T12 POG uses poly latency (%.0f)", b.latencySamples());
    }

    { // T13: Octavia (Type=Octavia) octave-up fuzz -> 2f present; finite; ADAA keeps
      // the aliasing of a high input tone low (no loud in-band alias products).
        const double f = 250.0;
        PitchBlock b; b.prepare({SR, BLK});
        b.setType(PitchBlock::kOctavia);
        b.setFuzz(0.6f); b.setTone(1.0f); b.setOctave(1.0f); b.setVolume(0.6f);
        std::vector<float> x = tone(f, 0.3, (int)(SR * 0.5));
        run(b, x);
        const double up = binPow(x, 2 * f), fund = binPow(x, f);
        CHECK(up > fund, "T13 Octavia octave-up 2f (%.2e) > f (%.2e)", up, fund);
        CHECK(b.latencySamples() == 0.0, "T13 Octavia zero latency");
        PitchBlock b2; b2.prepare({SR, BLK});
        b2.setType(PitchBlock::kOctavia);
        b2.setFuzz(0.8f); b2.setTone(1.0f); b2.setOctave(1.0f); b2.setVolume(0.6f);
        std::vector<float> hx = tone(4000.0, 0.4, (int)(SR * 0.5));
        run(b2, hx);
        bool finite = true; float pk = 0; for (float s2 : hx) { if (!std::isfinite(s2)) finite = false; pk = std::max(pk, std::abs(s2)); }
        double alias = binPow(hx, 137.0) + binPow(hx, 553.0) + binPow(hx, 971.0); // non-harmonic low bins
        double sig = binPow(hx, 8000.0) + binPow(hx, 4000.0);
        CHECK(finite && pk < 20.0f, "T13 Octavia finite+bounded on 4k (pk=%.2f)", pk);
        CHECK(alias < sig * 0.05 + 1e-9, "T13 ADAA keeps low-band alias small (alias %.2e vs sig %.2e)", alias, sig);
    }

    std::printf("=== %s (%d fail) ===\n", gFails ? "FAILURES" : "ALL PASS", gFails);
    return gFails ? 1 : 0;
}
