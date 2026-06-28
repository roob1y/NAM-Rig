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

    // ---- T8: dual L/R times (independent right division; Link mirrors L) ----
    {
        DelayBlock d;
        d.setBpm(120.0);
        d.prepare({SR, BLK});
        d.setSyncIndex(6);  // L = 1/4 = 500 ms
        d.setSyncIndexR(9); // R = 1/8 = 250 ms -> dual
        CHECK(std::abs(d.currentTimeMs() - 500.0) < 0.01, "T8 L = %.2f ms (want 500)", d.currentTimeMs());
        CHECK(std::abs(d.currentTimeMsR() - 250.0) < 0.01, "T8 R = %.2f ms (want 250)", d.currentTimeMsR());
        CHECK(d.dualTime(), "T8 unlinked R -> dualTime() true");
        d.setSyncIndexR(0); // Link mirrors L
        CHECK(std::abs(d.currentTimeMsR() - 500.0) < 0.01, "T8 Link R = %.2f (mirror L 500)", d.currentTimeMsR());
        CHECK(!d.dualTime(), "T8 Link -> dualTime() false");

        // Echoes land independently: L at 500 ms, R at 250 ms.
        d.setSyncIndex(6);
        d.setSyncIndexR(9);
        d.setFeedback(0.0f);
        d.setToneHz(20000.0f);
        d.setLowCutHz(20.0f);
        d.setWidth(1.0f);
        d.setMix(1.0f);
        d.setModAmount(0.0f);
        settle(d);
        std::vector<float> l((size_t)SR, 0.0f), r = l;
        l[0] = r[0] = 1.0f;
        run(d, l, r);
        const size_t expL = (size_t)(0.5 * SR), expR = (size_t)(0.25 * SR);
        const size_t atL = peakNear(l, expL, 300), atR = peakNear(r, expR, 300);
        CHECK(std::llabs((long long)atL - (long long)expL) <= 2, "T8 L echo at %zu (expect %zu)", atL, expL);
        CHECK(std::llabs((long long)atR - (long long)expR) <= 2, "T8 R echo at %zu (expect %zu)", atR, expR);
    }

    // ---- T9: feedback low-cut thins bass build-up in the repeats ----
    {
        auto tailEnergy = [&](float lowCutHz) {
            DelayBlock d;
            d.setTimeMs(120.0f);
            d.setFeedback(0.7f);
            d.setToneHz(20000.0f); // high-cut off
            d.setLowCutHz(lowCutHz);
            d.setWidth(1.0f);
            d.setMix(1.0f);
            d.setModAmount(0.0f);
            d.prepare({SR, BLK});
            settle(d);
            std::vector<float> l((size_t)SR, 0.0f), r = l;
            for (size_t i = 0; i < 480; ++i) // short 80 Hz burst at the start
                l[i] = r[i] = (float)(0.5 * std::sin(2.0 * M_PI * 80.0 * (double)i / SR));
            run(d, l, r);
            double e = 0; // energy left in the tail (where the repeats live)
            for (size_t i = 4800; i < l.size(); ++i) e += (double)l[i] * l[i];
            return e;
        };
        const double off = tailEnergy(20.0f);  // low-cut off
        const double on = tailEnergy(400.0f);   // low-cut at 400 Hz
        CHECK(on < off * 0.6, "T9 low-cut@400 thins 80Hz repeats (on %.3e < 0.6*off %.3e)", on, off * 0.6);
    }

    // ---- T10: Digital time mode — echo lands at the NEW time after a change ----
    {
        DelayBlock d;
        d.setBpm(120.0);
        d.setTimeMode(DelayBlock::TimeMode::Digital);
        d.setSyncIndex(6); // 1/4 = 500 ms
        d.setFeedback(0.0f);
        d.setToneHz(20000.0f);
        d.setLowCutHz(20.0f);
        d.setMix(1.0f);
        d.setModAmount(0.0f);
        d.prepare({SR, BLK});
        settle(d, 1.0);
        d.setSyncIndex(9); // -> 1/8 = 250 ms; the crossfade runs during the settle
        settle(d, 0.5);
        std::vector<float> l((size_t)SR, 0.0f), r = l;
        l[0] = r[0] = 1.0f;
        run(d, l, r);
        const size_t expect = (size_t)(0.25 * SR);
        const size_t at = peakNear(l, expect, 400);
        CHECK(std::llabs((long long)at - (long long)expect) <= 3,
              "T10 digital echo at new time %zu (expect %zu)", at, expect);
    }

    // ---- T11: Character::Clean is byte-for-byte the original (no-character) path ----
    {
        auto configure = [](DelayBlock &d) {
            d.setTimeMs(220.0f);
            d.setFeedback(0.7f);
            d.setToneHz(6000.0f);
            d.setLowCutHz(120.0f);
            d.setWidth(0.8f);
            d.setMix(0.5f);
            d.setModAmount(0.3f);
        };
        DelayBlock base, clean;
        configure(base);
        configure(clean);
        clean.setCharacter(DelayBlock::Character::Clean); // the ONLY difference
        base.prepare({SR, BLK});
        clean.prepare({SR, BLK});
        // identical stereo input (a burst + tone so the loop, tone, lowcut, width
        // and mod paths are all exercised)
        std::vector<float> bl((size_t)SR, 0.0f), br = bl, cl, cr;
        for (size_t i = 0; i < 1000; ++i)
            bl[i] = br[i] = (float)(0.4 * std::sin(2.0 * M_PI * 330.0 * (double)i / SR));
        cl = bl; cr = br;
        run(base, bl, br);
        run(clean, cl, cr);
        double err = 0;
        for (size_t i = 0; i < bl.size(); ++i)
            err = std::max(err, (double)std::max(std::abs(bl[i] - cl[i]), std::abs(br[i] - cr[i])));
        CHECK(err == 0.0, "T11 setCharacter(Clean) byte-for-byte vs default (max diff %.2e)", err);
    }

    // ---- T12: tape character darkens repeats more than the clean delay ----
    {
        auto tailHfRatio = [](bool tape) {
            DelayBlock d;
            d.setTimeMs(120.0f);
            d.setFeedback(0.7f);
            d.setToneHz(20000.0f); // tone knob OFF: isolate the character's own HF loss
            d.setLowCutHz(20.0f);
            d.setWidth(0.0f);      // mono wet -> simple
            d.setMix(1.0f);
            d.setModAmount(0.0f);
            if (tape) d.setCharacter(DelayBlock::Character::Tape);
            d.prepare({SR, BLK});
            settle(d);
            std::vector<float> l((size_t)SR, 0.0f), r = l;
            l[0] = r[0] = 1.0f;
            run(d, l, r);
            // ratio of HF energy (one-pole HP ~3 kHz) to total energy in the tail
            double hp = 0.0, prev = 0.0, eHi = 0.0, eAll = 0.0;
            const double k = 1.0 - std::exp(-2.0 * M_PI * 3000.0 / SR);
            for (size_t i = 4800; i < l.size(); ++i) {
                hp += k * ((double)l[i] - hp);
                const double hi = (double)l[i] - hp;
                eHi += hi * hi; eAll += (double)l[i] * (double)l[i]; prev = hp;
            }
            (void)prev;
            return eAll > 0 ? eHi / eAll : 0.0;
        };
        const double clean = tailHfRatio(false), tape = tailHfRatio(true);
        CHECK(tape < clean * 0.7, "T12 tape repeats darker (HF frac tape %.4f < 0.7*clean %.4f)",
              tape, clean * 0.7);
    }

    // ---- T13: tape saturation stays bounded + finite at hot input (alias/peak guard) ----
    {
        DelayBlock d;
        d.setTimeMs(60.0f);
        d.setFeedback(0.6f);
        d.setToneHz(20000.0f);
        d.setLowCutHz(20.0f);
        d.setWidth(1.0f);
        d.setMix(1.0f);
        d.setModAmount(0.0f);
        d.setCharacter(DelayBlock::Character::Tape);
        d.prepare({SR, BLK});
        settle(d);
        // very hot near-Nyquist-ish sweep into the loop -> the ADAA peak guard must hold
        std::vector<float> l((size_t)SR, 0.0f), r = l;
        for (size_t i = 0; i < l.size(); ++i)
            l[i] = r[i] = (float)(3.0 * std::sin(2.0 * M_PI * 5000.0 * (double)i / SR));
        run(d, l, r);
        double mx = 0; bool finite = true;
        for (size_t i = 0; i < l.size(); ++i) {
            if (!std::isfinite(l[i]) || !std::isfinite(r[i])) finite = false;
            mx = std::max(mx, (double)std::max(std::abs(l[i]), std::abs(r[i])));
        }
        CHECK(finite, "T13 tape hot-input output all finite (no NaN/Inf)");
        CHECK(mx < 8.0, "T13 tape hot-input bounded (maxabs %.3f < 8.0)", mx);
    }

    // ---- T14: tape self-oscillation at fb=1.0 sustains but stays bounded ----
    {
        // Feedback is normalised by the in-loop bump gain, so the tail still DECAYS
        // through most of the knob and only self-oscillates near the top (~1.0+).
        // Verify: at max feedback the tail sustains (vs a mid setting that decays),
        // and the saturation keeps it bounded + finite.
        auto tailMax = [&](float fbk) {
            DelayBlock d;
            d.setTimeMs(150.0f);
            d.setFeedback(fbk);
            d.setToneHz(20000.0f);
            d.setLowCutHz(20.0f);
            d.setWidth(1.0f);
            d.setMix(1.0f);
            d.setModAmount(0.0f);
            d.setCharacter(DelayBlock::Character::Tape);
            d.prepare({SR, BLK});
            settle(d, 0.2);
            std::vector<float> l((size_t)(SR * 8), 0.0f), r = l;
            l[0] = r[0] = 0.5f; // seed the loop
            run(d, l, r);
            double mx = 0, tail = 0; bool finite = true;
            for (size_t i = 0; i < l.size(); ++i) {
                if (!std::isfinite(l[i])) finite = false;
                mx = std::max(mx, (double)std::abs(l[i]));
            }
            for (size_t i = l.size() - (size_t)SR; i < l.size(); ++i) tail += (double)l[i] * l[i];
            struct R { double tail, mx; bool finite; }; return R{tail, mx, finite};
        };
        auto hi = tailMax(1.1f);  // top of knob -> self-oscillates
        auto mid = tailMax(0.5f); // mid -> decays to ~silence
        CHECK(hi.finite, "T14 self-osc finite at max feedback");
        CHECK(hi.mx < 8.0, "T14 self-osc bounded by saturation (maxabs %.3f < 8.0)", hi.mx);
        CHECK(hi.tail > 100.0 * mid.tail + 1e-6,
              "T14 max-fb sustains vs mid-fb decay (%.2e >> %.2e)", hi.tail, mid.tail);
    }

    // ---- T15: Space Tape head timing — head 1 = the target time, heads 2/3
    // ride the fixed kHeadRatio multiples (1.95x / 2.79x) in BOTH free and sync.
    // Guards against the old free-mode 69-177 ms remap and the sync leadingRatio
    // snap that made the echoes run fast. fb=0 -> one pass, so each active head
    // emits a single onset.
    {
        // generic onset picker: peaks of |x| above 0.2*max, >= 25 ms apart.
        auto onsets = [](const std::vector<float> &x) {
            std::vector<size_t> pk;
            double mx = 0; for (float v : x) mx = std::max(mx, (double)std::abs(v));
            if (mx < 1e-6) return pk;
            const double thr = 0.2 * mx;
            const size_t gap = (size_t)(0.025 * SR);
            for (size_t i = 1; i + 1 < x.size(); ++i)
            {
                if (std::abs(x[i]) > thr && std::abs(x[i]) >= std::abs(x[i-1])
                    && std::abs(x[i]) > std::abs(x[i+1]))
                {
                    if (pk.empty() || i - pk.back() > gap) pk.push_back(i);
                }
            }
            return pk;
        };
        auto headTaps = [&](bool sync) {
            DelayBlock d;
            d.setCharacter(DelayBlock::Character::SpaceTape);
            d.setHeadMode(10);          // mask 0b111 = all three heads (UI mode 11)
            d.setTimeMs(250.0f);        // free target / fallback
            d.setToneHz(8000.0f);
            d.setWidth(0.0f);
            d.setMix(1.0f);
            d.setModAmount(0.0f);
            d.setBpm(120.0);
            d.setSyncIndex(sync ? 6 : 0); // 6 = 1/4 = 500 ms @120
            d.setFeedback(0.0f);
            d.prepare({SR, BLK});
            settle(d);
            std::vector<float> l((size_t)(1.5 * SR), 0.0f), r = l;
            l[0] = r[0] = 1.0f;
            run(d, l, r);
            return onsets(l);
        };
        // FREE: head 1 = the Time knob (250 ms), NOT the old ~95 ms remap.
        {
            auto t = headTaps(false);
            CHECK(t.size() == 3, "T15 free: 3 head onsets (got %zu)", t.size());
            if (t.size() == 3)
            {
                const double h1 = t[0] / SR * 1000.0;
                const double r2 = (double)t[1] / t[0], r3 = (double)t[2] / t[0];
                CHECK(std::abs(h1 - 250.0) < 6.0, "T15 free head1 %.1f ms == knob 250", h1);
                CHECK(std::abs(r2 - 1.95) < 0.05, "T15 free head2 ratio %.3f ~ 1.95", r2);
                CHECK(std::abs(r3 - 2.79) < 0.05, "T15 free head3 ratio %.3f ~ 2.79", r3);
            }
        }
        // SYNC: head 1 lands on the host division (1/4 = 500 ms); heads follow.
        {
            auto t = headTaps(true);
            CHECK(t.size() == 3, "T15 sync: 3 head onsets (got %zu)", t.size());
            if (t.size() == 3)
            {
                const double h1 = t[0] / SR * 1000.0;
                const double r2 = (double)t[1] / t[0], r3 = (double)t[2] / t[0];
                CHECK(std::abs(h1 - 500.0) < 8.0, "T15 sync head1 %.1f ms on 1/4 div 500", h1);
                CHECK(std::abs(r2 - 1.95) < 0.05, "T15 sync head2 ratio %.3f ~ 1.95", r2);
                CHECK(std::abs(r3 - 2.79) < 0.05, "T15 sync head3 ratio %.3f ~ 2.79", r3);
            }
        }
    }

    std::printf("\n%s (%d FAIL)\n", gFails == 0 ? "ALL PASS" : "FAILURES", gFails);
    return gFails == 0 ? 0 : 1;
}
