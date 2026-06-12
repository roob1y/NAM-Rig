// delay_test — offline verification harness for DelayBlock. Exits nonzero on
// any FAIL.
//
// T1 first echo lands at the set time (sample accuracy after glide settle)
// T2 feedback ratio between consecutive echoes is exact (tone off)
// T3 sync resolves dotted/triplet divisions exactly (incl. vs currentTimeMs)
// T4 tone LPF darkens repeats cumulatively (measured on the SAME live coeffs)
// T5 ping-pong alternates channels
// T6 wow/flutter modulates echo timing (and 0 keeps it static)
// T7 mix law + mono aliasing safety
#include "rig/DelayBlock.h"
#include <cstdio>
#include <cmath>
#include <vector>

using nam_rig::DelayBlock;
using nam_rig::BlockContext;

static int gFails = 0;
#define CHECK(cond, ...) do { \
    const bool chkOk_ = (cond); \
    std::printf("%s: ", chkOk_ ? "PASS" : "FAIL"); std::printf(__VA_ARGS__); std::printf("\n"); \
    if (!chkOk_) ++gFails; } while (0)

static constexpr double SR = 48000.0;
static constexpr int BLK = 512;

static void run(DelayBlock &d, std::vector<float> &l, std::vector<float> &r)
{
    for (size_t p = 0; p < l.size(); p += BLK)
        d.process(l.data() + p, r.data() + p, (int)std::min<size_t>(BLK, l.size() - p));
}

// settle the time-glide smoother on silence before measuring
static void settle(DelayBlock &d, double seconds = 1.0)
{
    std::vector<float> l((size_t)(SR * seconds), 0.0f), r = l;
    run(d, l, r);
}

static size_t peakNear(const std::vector<float> &x, size_t center, size_t halfWin)
{
    size_t lo = center > halfWin ? center - halfWin : 0;
    size_t hi = std::min(x.size(), center + halfWin);
    size_t at = lo;
    double pk = -1;
    for (size_t i = lo; i < hi; ++i)
        if (std::abs(x[i]) > pk) { pk = std::abs(x[i]); at = i; }
    return at;
}

int main()
{
    // ---- T1: echo time ----
    {
        DelayBlock d;
        d.setTimeMs(250.0f);
        d.setFeedback(0.0f);
        d.setToneHz(20000.0f); // off
        d.setMix(1.0f);
        d.setModAmount(0.0f);
        d.prepare({SR, BLK});
        settle(d);
        std::vector<float> l((size_t)SR, 0.0f), r = l;
        l[0] = r[0] = 1.0f;
        run(d, l, r);
        const size_t expect = (size_t)(0.250 * SR);
        const size_t at = peakNear(l, expect, 200);
        CHECK(std::llabs((long long)at - (long long)expect) <= 1,
              "T1 echo at %zu (expect %zu +-1)", at, expect);
    }

    // ---- T2: feedback ratio ----
    {
        DelayBlock d;
        d.setTimeMs(100.0f);
        d.setFeedback(0.5f);
        d.setToneHz(20000.0f);
        d.setMix(1.0f);
        d.setModAmount(0.0f);
        d.prepare({SR, BLK});
        settle(d);
        std::vector<float> l((size_t)SR, 0.0f), r = l;
        l[0] = r[0] = 1.0f;
        run(d, l, r);
        const size_t t = (size_t)(0.100 * SR);
        const double e1 = std::abs(l[peakNear(l, t, 50)]);
        const double e2 = std::abs(l[peakNear(l, 2 * t, 50)]);
        const double e3 = std::abs(l[peakNear(l, 3 * t, 50)]);
        CHECK(std::abs(e2 / e1 - 0.5) < 0.01, "T2 echo2/echo1 = %.3f (want 0.500)", e2 / e1);
        CHECK(std::abs(e3 / e2 - 0.5) < 0.01, "T2 echo3/echo2 = %.3f (want 0.500)", e3 / e2);
    }

    // ---- T3: tempo sync (120 bpm) ----
    {
        DelayBlock d;
        d.setBpm(120.0);
        d.prepare({SR, BLK});
        struct { int idx; double ms; const char *name; } cases[] = {
            {6, 500.0, "1/4"}, {5, 750.0, "1/4."}, {7, 1000.0 / 3.0, "1/4T"},
            {9, 250.0, "1/8"}, {8, 375.0, "1/8."}, {3, 1000.0, "1/2"}};
        for (const auto &c : cases)
        {
            d.setSyncIndex(c.idx);
            CHECK(std::abs(d.currentTimeMs() - c.ms) < 0.01,
                  "T3 %s @120bpm = %.2f ms (want %.2f)", c.name, d.currentTimeMs(), c.ms);
        }
        // and the echo actually lands there for a dotted division
        d.setSyncIndex(8); // 1/8. = 375 ms
        d.setFeedback(0.0f);
        d.setToneHz(20000.0f);
        d.setMix(1.0f);
        d.setModAmount(0.0f);
        settle(d);
        std::vector<float> l((size_t)SR, 0.0f), r = l;
        l[0] = r[0] = 1.0f;
        run(d, l, r);
        const size_t expect = (size_t)(0.375 * SR);
        const size_t at = peakNear(l, expect, 200);
        CHECK(std::llabs((long long)at - (long long)expect) <= 1,
              "T3 dotted-eighth echo at %zu (expect %zu +-1)", at, expect);
    }

    // ---- T4: tone filter darkens repeats cumulatively ----
    {
        DelayBlock d;
        d.setTimeMs(100.0f);
        d.setFeedback(0.6f);
        d.setToneHz(2000.0f);
        d.setMix(1.0f);
        d.setModAmount(0.0f);
        d.prepare({SR, BLK});
        // analytic check on the live coefficients (single source of truth)
        const double g4k = d.toneFilter().magnitudeAt(SR, 4000.0);
        CHECK(g4k < 0.3, "T4 live tone LPF |H(4k)| = %.3f (< 0.3 for fc=2k)", g4k);
        settle(d);
        // 4 kHz burst: measure the 4 kHz BIN of each repeat (peak measurement
        // is polluted by the burst's broadband transient splatter, which the
        // 2 kHz LPF passes much more strongly than the 4 kHz carrier).
        std::vector<float> l((size_t)SR * 2, 0.0f), r;
        for (int i = 0; i < 480; ++i)
            l[(size_t)i] = (float)(0.5 * std::sin(2.0 * M_PI * 4000.0 * i / SR));
        r = l;
        run(d, l, r);
        const size_t t = (size_t)(0.100 * SR);
        auto bin4k = [&](size_t c)
        {
            double re = 0, im = 0;
            for (size_t i = c; i < c + 480 && i < l.size(); ++i)
            {
                const double ph = 2.0 * M_PI * 4000.0 * (double)(i - c) / SR;
                re += (double)l[i] * std::cos(ph);
                im -= (double)l[i] * std::sin(ph);
            }
            return std::sqrt(re * re + im * im);
        };
        const double e1 = bin4k(t), e2 = bin4k(2 * t);
        const double ratio = e2 / e1; // = feedback * |H(4k)| (one more loop trip)
        const double want = 0.6 * g4k;
        CHECK(std::abs(ratio - want) < 0.03,
              "T4 repeat 4k-bin ratio %.3f (want fb*|H| = %.3f)", ratio, want);
    }

    // ---- T5: ping-pong alternates ----
    {
        DelayBlock d;
        d.setTimeMs(100.0f);
        d.setFeedback(0.7f);
        d.setToneHz(20000.0f);
        d.setMix(1.0f);
        d.setModAmount(0.0f);
        d.setPingPong(true);
        d.setWidth(1.0f);
        d.prepare({SR, BLK});
        settle(d);
        std::vector<float> l((size_t)SR, 0.0f), r = l;
        l[0] = 1.0f; // impulse on LEFT only
        run(d, l, r);
        const size_t t = (size_t)(0.100 * SR);
        auto at = [&](const std::vector<float> &x, size_t c)
        { return std::abs(x[peakNear(x, c, 50)]); };
        // echo 1 left, echo 2 right, echo 3 left...
        CHECK(at(l, t) > 5.0 * at(r, t), "T5 echo1 L (%.3f) >> R (%.3f)", at(l, t), at(r, t));
        CHECK(at(r, 2 * t) > 5.0 * at(l, 2 * t), "T5 echo2 R (%.3f) >> L (%.3f)",
              at(r, 2 * t), at(l, 2 * t));
        CHECK(at(l, 3 * t) > 5.0 * at(r, 3 * t), "T5 echo3 L (%.3f) >> R (%.3f)",
              at(l, 3 * t), at(r, 3 * t));
    }

    // ---- T6: wow/flutter moves echoes; 0 keeps them put ----
    {
        auto echoAt = [&](float modAmt, double impulseAtSec) -> double
        {
            DelayBlock d;
            d.setTimeMs(500.0f);
            d.setFeedback(0.0f);
            d.setToneHz(20000.0f);
            d.setMix(1.0f);
            d.setModAmount(modAmt);
            d.prepare({SR, BLK});
            settle(d);
            std::vector<float> l((size_t)(SR * (impulseAtSec + 1.0)), 0.0f), r;
            l[(size_t)(SR * impulseAtSec)] = 1.0f;
            r = l;
            run(d, l, r);
            const size_t expect = (size_t)(SR * (impulseAtSec + 0.5));
            return ((double)peakNear(l, expect, 400) - (double)expect);
        };
        // static: same offset whenever the impulse goes in
        const double s1 = echoAt(0.0f, 0.10), s2 = echoAt(0.0f, 0.37);
        CHECK(std::abs(s1 - s2) <= 1.0, "T6 modAmt 0: echo offset static (%.0f vs %.0f)", s1, s2);
        // modulated: offsets differ across LFO phase (different insert times)
        const double m1 = echoAt(1.0f, 0.10), m2 = echoAt(1.0f, 0.37);
        CHECK(std::abs(m1 - m2) > 8.0,
              "T6 modAmt 1: echo offsets move (%.0f vs %.0f, |diff| > 8 smp)", m1, m2);
    }

    // ---- T7: mix law + mono aliasing ----
    {
        DelayBlock d;
        d.setTimeMs(80.0f);
        d.setFeedback(0.0f);
        d.setToneHz(20000.0f);
        d.setMix(0.0f);
        d.setModAmount(0.0f);
        d.prepare({SR, BLK});
        settle(d);
        std::vector<float> l((size_t)SR, 0.0f);
        for (size_t i = 0; i < l.size(); ++i)
            l[i] = (float)(0.3 * std::sin(2.0 * M_PI * 440.0 * (double)i / SR));
        auto ref = l;
        for (size_t p = 0; p < l.size(); p += BLK) // mono-aliased call
            d.process(l.data() + p, l.data() + p, (int)std::min<size_t>(BLK, l.size() - p));
        double err = 0;
        for (size_t i = 4800; i < l.size(); ++i)
            err = std::max(err, (double)std::abs(l[i] - ref[i]));
        CHECK(err < 1e-6, "T7 mix=0 mono-aliased dry error %.2e < 1e-6", err);
    }

    std::printf("\n%s (%d FAIL)\n", gFails == 0 ? "ALL PASS" : "FAILURES", gFails);
    return gFails == 0 ? 0 : 1;
}
