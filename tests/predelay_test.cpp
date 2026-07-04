// predelay_test — offline verification harness for PreDelayBlock (the mono
// front-of-amp delay pedal). Exits nonzero on any FAIL. JUCE-free core, built
// with juce_audio_basics only like the other block tests.
//
// T1  first echo lands at the set free time (sample accuracy)
// T2  feedback produces a decaying train of echoes at the delay period
// T3  tempo sync resolves divisions exactly (currentTimeMs + echo position)
// T4  BBD bandwidth tracks time (analog darkens as delay lengthens); digital is fixed
// T5  per-model max-time clamp (Carbon Copy/Memory Man can't reach 2000 ms)
// T6  modulation moves the echo timing (and Mod 0 leaves it static)
// T7  mix law: Mix 0 = dry, Mix 1 = wet only; output always finite
// T8  self-oscillation is bounded (analog feedback at max stays finite/limited)
// T9  determinism: same input + params -> identical output
// T10 DD-7 max feedback self-oscillation sustains + bounded
// T11 all 8 DD-7 MODE positions run clean; T12 REVERSE produces output
// T13 Carbon Copy circuit-grounded voicing (600 ms / 8192 stages / dark / self-osc)
// T14 Memory Man circuit-grounded voicing (550 ms / 8192 stages / crossfade Blend)
// T15 Memory Man master Level (Volume) + Chorus/Vibrato switch (LFO speed range)
// T16 SDD-3000 controls (HIGH filter, crossfade Balance, input preamp, feedback INV)
#include "rig/PreDelayBlock.h"
#include <cstdio>
#include <cmath>
#include <vector>

using nam_rig::PreDelayBlock;
using nam_rig::BlockContext;

static int gFails = 0;
#define CHECK(cond, ...) do { \
    const bool chkOk_ = (cond); \
    std::printf("%s: ", chkOk_ ? "PASS" : "FAIL"); std::printf(__VA_ARGS__); std::printf("\n"); \
    if (!chkOk_) ++gFails; } while (0)

static constexpr double SR = 48000.0;
static constexpr int BLK = 512;

static void run(PreDelayBlock &d, std::vector<float> &m)
{
    for (size_t p = 0; p < m.size(); p += BLK)
        d.process(m.data() + p, (int)std::min<size_t>(BLK, m.size() - p));
}

static void settle(PreDelayBlock &d, double seconds = 0.5)
{
    std::vector<float> m((size_t)(SR * seconds), 0.0f);
    run(d, m);
}

static size_t peakNear(const std::vector<float> &x, size_t center, size_t halfWin)
{
    const size_t lo = center > halfWin ? center - halfWin : 0;
    const size_t hi = std::min(x.size(), center + halfWin);
    size_t at = lo;
    double pk = -1.0;
    for (size_t i = lo; i < hi; ++i)
        if (std::abs((double)x[i]) > pk) { pk = std::abs((double)x[i]); at = i; }
    return at;
}

static double peakAmpNear(const std::vector<float> &x, size_t center, size_t halfWin)
{
    return std::abs((double)x[peakNear(x, center, halfWin)]);
}

int main()
{
    // ---- T1: first echo lands at the set time (Boss DD-7, clean digital) ----
    {
        PreDelayBlock d;
        d.setModel(PreDelayBlock::kDD7);
        d.setTimeMs(250.0f);
        d.setFeedback(0.0f);
        d.setMix(1.0f);
        d.setMod(0.0f);
        d.setToneHz(20000.0f);
        d.prepare({SR, BLK});
        std::vector<float> m((size_t)SR, 0.0f);
        m[0] = 1.0f;
        run(d, m);
        const size_t expect = (size_t)(0.250 * SR);
        const size_t at = peakNear(m, expect, 300);
        CHECK(std::llabs((long long)at - (long long)expect) <= 2,
              "T1 echo at %zu (expect %zu +-2)", at, expect);
    }

    // ---- T2: feedback -> decaying echo train at the delay period ----
    {
        PreDelayBlock d;
        d.setModel(PreDelayBlock::kDD7);
        d.setTimeMs(200.0f);
        d.setFeedback(0.5f);
        d.setMix(1.0f);
        d.setMod(0.0f);
        d.setToneHz(20000.0f);
        d.prepare({SR, BLK});
        std::vector<float> m((size_t)SR, 0.0f);
        m[0] = 1.0f;
        run(d, m);
        const size_t T = (size_t)(0.200 * SR);
        const double e1 = peakAmpNear(m, T, 300);
        const double e2 = peakAmpNear(m, 2 * T, 300);
        const double e3 = peakAmpNear(m, 3 * T, 300);
        CHECK(e1 > 0.2 && e2 > 0.02, "T2 echoes present (e1=%.3f e2=%.3f)", e1, e2);
        CHECK(e2 < e1 && e3 < e2, "T2 echo train decays (e1=%.3f e2=%.3f e3=%.3f)", e1, e2, e3);
        const double ratio = e2 / e1;
        // ~feedback (0.5); the bright in-loop LP retains a little more per pass, so allow up to ~0.65.
        CHECK(ratio > 0.30 && ratio < 0.65, "T2 decay ratio ~feedback (%.3f)", ratio);
    }

    // ---- T3: tempo sync resolves divisions exactly ----
    {
        PreDelayBlock d;
        d.setModel(PreDelayBlock::kDD7);
        d.setBpm(120.0);
        d.setSyncIndex(3); // 1/4 note = 500 ms at 120 bpm
        d.setFeedback(0.0f);
        d.setMix(1.0f);
        d.setMod(0.0f);
        d.prepare({SR, BLK});
        CHECK(std::abs(d.currentTimeMs() - 500.0f) < 0.5f,
              "T3 sync 1/4 @120 = %.1f ms (expect 500)", d.currentTimeMs());
        std::vector<float> m((size_t)SR, 0.0f);
        m[0] = 1.0f;
        run(d, m);
        const size_t expect = (size_t)(0.500 * SR);
        const size_t at = peakNear(m, expect, 400);
        CHECK(std::llabs((long long)at - (long long)expect) <= 2, "T3 echo at %zu (expect %zu)", at, expect);
    }

    // ---- T4: BBD bandwidth tracks delay time; digital is fixed ----
    {
        // Memory Man (analog BBD): darker at long delay than short delay.
        PreDelayBlock mm;
        mm.setModel(PreDelayBlock::kMemoryMan);
        mm.setMix(0.5f);
        mm.prepare({SR, BLK});
        mm.setTimeMs(120.0f);
        settle(mm, 0.3);
        const float shortCorner = mm.currentLoopLpHz();
        mm.setTimeMs(550.0f);
        settle(mm, 0.3);
        const float longCorner = mm.currentLoopLpHz();
        CHECK(longCorner < shortCorner,
              "T4 Memory Man darkens with time (%.0f Hz @550ms < %.0f Hz @120ms)", longCorner, shortCorner);

        // Boss DD-7 (digital): fixed bandwidth regardless of time.
        PreDelayBlock dd;
        dd.setModel(PreDelayBlock::kDD7);
        dd.setMix(0.5f);
        dd.prepare({SR, BLK});
        dd.setTimeMs(120.0f);
        settle(dd, 0.2);
        const float c1 = dd.currentLoopLpHz();
        dd.setTimeMs(1500.0f);
        settle(dd, 0.2);
        const float c2 = dd.currentLoopLpHz();
        CHECK(std::abs(c1 - c2) < 1.0f, "T4 DD-7 bandwidth fixed (%.0f vs %.0f Hz)", c1, c2);

        // Carbon Copy is darker than DD-7 at the same setting.
        PreDelayBlock cc;
        cc.setModel(PreDelayBlock::kCarbonCopy);
        cc.setMix(0.5f);
        cc.prepare({SR, BLK});
        cc.setTimeMs(400.0f);
        settle(cc, 0.3);
        CHECK(cc.currentLoopLpHz() < c1,
              "T4 Carbon Copy darker than DD-7 (%.0f < %.0f Hz)", cc.currentLoopLpHz(), c1);
    }

    // ---- T5: per-model max-time clamp ----
    {
        PreDelayBlock cc;
        cc.setModel(PreDelayBlock::kCarbonCopy);
        cc.setTimeMs(2000.0f); // way past the pedal's 600 ms max
        cc.prepare({SR, BLK});
        CHECK(cc.currentTimeMs() <= 600.5f, "T5 Carbon Copy clamps to 600 ms (%.0f)", cc.currentTimeMs());

        PreDelayBlock dd;
        dd.setModel(PreDelayBlock::kDD7);
        dd.setTimeMs(2000.0f);
        dd.prepare({SR, BLK});
        CHECK(dd.currentTimeMs() > 1900.0f, "T5 DD-7 allows long delay (%.0f)", dd.currentTimeMs());
    }

    // ---- T6: modulation moves echo timing; Mod 0 is static ----
    {
        auto echoPos = [](float modAmt) {
            PreDelayBlock d;
            d.setModel(PreDelayBlock::kMemoryMan);
            d.setTimeMs(300.0f);
            d.setFeedback(0.0f);
            d.setMix(1.0f);
            d.setMod(modAmt);
            d.prepare({SR, BLK});
            // let the LFO advance to a nonzero phase so the sweep is engaged
            settle(d, 0.2);
            std::vector<float> m((size_t)SR, 0.0f);
            m[0] = 1.0f;
            run(d, m);
            return peakNear(m, (size_t)(0.300 * SR), 600);
        };
        const size_t staticPos = echoPos(0.0f);
        const size_t modPos = echoPos(1.0f);
        CHECK(modPos != staticPos, "T6 mod shifts echo timing (mod=%zu static=%zu)", modPos, staticPos);
    }

    // ---- T7: mix law -> Mix 0 adds NO wet/echo (dry-through-the-analog-IO only) ----
    // The Carbon Copy I/O is now circuit-grounded as BUFFERED analog coupling (1 MΩ in /
    // 1 kΩ out, subsonic coupling caps), so the dry is no longer bit-identical to the raw
    // input (a tiny inaudible subsonic phase shift). So we test the real mix-law intent --
    // "at Mix 0 there is no echo" -- via an impulse, not bit-equality of the dry.
    {
        PreDelayBlock d;
        d.setModel(PreDelayBlock::kCarbonCopy);
        d.setTimeMs(180.0f);
        d.setFeedback(0.5f);
        d.setMod(0.3f);
        d.setMix(0.0f); // before prepare so the mix smoother seeds at 0 (no ramp)
        d.prepare({SR, BLK});
        std::vector<float> m((size_t)SR, 0.0f);
        m[0] = 1.0f; // impulse
        run(d, m);
        const size_t T = (size_t)(0.180 * SR);
        const double dryPk = peakAmpNear(m, 0, 64);    // the dry impulse passes through
        const double echoPk = peakAmpNear(m, T, 400);  // there must be no echo at Mix 0
        bool finite = true;
        for (float v : m) if (!std::isfinite(v)) finite = false;
        CHECK(dryPk > 0.5, "T7 Mix 0 passes dry (impulse peak %.3f)", dryPk);
        CHECK(echoPk < 1.0e-4, "T7 Mix 0 adds no echo (delay-time peak %.2e)", echoPk);
        CHECK(finite, "T7 output finite");
    }

    // ---- T8: self-oscillation is bounded (analog max feedback) ----
    {
        PreDelayBlock d;
        d.setModel(PreDelayBlock::kCarbonCopy);
        d.setTimeMs(150.0f);
        d.setFeedback(1.0f); // ceiling 1.03 -> would run away without the in-loop compander
        d.setMix(1.0f);
        d.setMod(0.5f);
        d.prepare({SR, BLK});
        std::vector<float> m((size_t)(SR * 4.0), 0.0f);
        for (int k = 0; k < 200; ++k) m[(size_t)k] = 0.5f; // a burst to excite the loop
        run(d, m);
        double pk = 0.0;
        bool finite = true;
        for (size_t i = m.size() / 2; i < m.size(); ++i) // tail
        {
            pk = std::max(pk, std::abs((double)m[i]));
            if (!std::isfinite(m[i])) finite = false;
        }
        CHECK(finite, "T8 self-osc output finite");
        CHECK(pk < 4.0, "T8 self-osc bounded (tail peak %.2f)", pk);
    }

    // ---- T9: determinism ----
    {
        auto render = []() {
            PreDelayBlock d;
            d.setModel(PreDelayBlock::kSDD3000);
            d.setTimeMs(260.0f);
            d.setFeedback(0.45f);
            d.setMix(0.4f);
            d.setMod(0.3f);
            d.prepare({SR, BLK});
            std::vector<float> m((size_t)SR, 0.0f);
            for (size_t i = 0; i < m.size(); ++i) m[i] = 0.3f * std::sin(2.0 * 3.14159265 * 330.0 * i / SR);
            run(d, m);
            return m;
        };
        const std::vector<float> a = render(), b = render();
        bool same = a.size() == b.size();
        for (size_t i = 0; same && i < a.size(); ++i) same = (a[i] == b[i]);
        CHECK(same, "T9 deterministic (bit-identical re-render)");
    }

    // ---- T10: DD-7 at MAX feedback stays bounded ----
    // The DD-7 self-oscillates ("Trick Sound") so its ceiling is 1.0, and it has NO
    // in-loop saturation (24-bit, no compander) — the only thing keeping it from
    // running away is the sub-unity in-loop LP. Verify it sustains without blowing up.
    {
        PreDelayBlock d;
        d.setModel(PreDelayBlock::kDD7);
        d.setTimeMs(200.0f);
        d.setFeedback(1.0f); // fb = 1.0 * ceiling 1.0
        d.setMix(1.0f);
        d.setMod(0.0f);
        d.prepare({SR, BLK});
        std::vector<float> m((size_t)(SR * 5.0), 0.0f);
        for (int k = 0; k < 400; ++k) m[(size_t)k] = 0.4f; // a short burst to excite the loop
        run(d, m);
        double pk = 0.0;
        bool finite = true;
        for (size_t i = m.size() * 3 / 4; i < m.size(); ++i) // far tail
        {
            pk = std::max(pk, std::abs((double)m[i]));
            if (!std::isfinite(m[i])) finite = false;
        }
        CHECK(finite, "T10 DD-7 max-feedback output finite");
        CHECK(pk > 0.3, "T10 DD-7 self-oscillation SUSTAINS (tail peak %.2f)", pk); // builds/holds, doesn't die
        CHECK(pk < 8.0, "T10 DD-7 self-oscillation bounded (tail peak %.2f)", pk);
    }

    // ---- T11: all 8 DD-7 MODE positions run clean (finite + bounded) and the time
    // ranges clamp (50 ms mode holds <=50 ms, 200 ms mode <=200 ms) ----
    {
        static const char *kModeNames[8] = {"50ms", "200ms", "800ms", "3200ms",
                                             "Hold", "Modulate", "Analog", "Reverse"};
        for (int mode = 0; mode < 8; ++mode)
        {
            PreDelayBlock d;
            d.setModel(PreDelayBlock::kDD7);
            d.setDd7Mode(mode);
            d.setTimeMs(300.0f);
            d.setFeedback(0.5f);
            d.setMix(0.6f);
            d.setMod(0.3f);
            d.prepare({SR, BLK});
            std::vector<float> m((size_t)(SR * 3.0), 0.0f);
            for (int k = 0; k < 2400; ++k) m[(size_t)k] = 0.3f * std::sin(2.0 * 3.14159265 * 220.0 * k / SR);
            run(d, m);
            double pk = 0.0;
            bool finite = true;
            for (size_t i = m.size() / 3; i < m.size(); ++i)
            {
                pk = std::max(pk, std::abs((double)m[i]));
                if (!std::isfinite(m[i])) finite = false;
            }
            bool timeOk = true;
            if (mode == 0 && d.currentTimeMs() > 50.5f) timeOk = false;
            if (mode == 1 && d.currentTimeMs() > 200.5f) timeOk = false;
            CHECK(finite && pk < 6.0 && timeOk,
                  "T11 MODE %s (finite=%d peak=%.2f time=%.0fms)", kModeNames[mode], (int)finite, pk, d.currentTimeMs());
        }
    }

    // ---- T12: REVERSE actually produces output (regression guard: the reverse read
    // must sweep the buffer, not freeze on one sample). Wet-only RMS must be substantial,
    // comparable to a forward delay. ----
    {
        auto wetRms = [](int mode) {
            PreDelayBlock d;
            d.setModel(PreDelayBlock::kDD7);
            d.setDd7Mode(mode);
            d.setTimeMs(400.0f);
            d.setFeedback(0.0f);
            d.setMix(1.0f);
            d.setMod(0.0f);
            d.prepare({SR, BLK});
            std::vector<float> m((size_t)(SR * 2.0), 0.0f);
            for (size_t i = 0; i < m.size(); ++i) m[i] = 0.3f * std::sin(2.0 * 3.14159265 * 330.0 * i / SR);
            run(d, m);
            double s = 0.0; int n = 0;
            for (size_t i = (size_t)SR; i < m.size(); ++i)
            {
                const double dry = 0.3 * std::sin(2.0 * 3.14159265 * 330.0 * i / SR);
                const double wet = (double)m[i] - dry; // out = dry + wet -> isolate wet
                s += wet * wet; ++n;
            }
            return std::sqrt(s / (double)std::max(1, n));
        };
        const double rev = wetRms(PreDelayBlock::kReverse);
        CHECK(rev > 0.05, "T12 REVERSE produces output (wet RMS %.3f > 0.05)", rev);
    }

    // ---- T13: Carbon Copy circuit-grounded voicing (docs/predelay/carbon_copy.md) ----
    // Verified specs it must honour: max delay 600 ms, dark BBD (in-loop corner well below
    // the DD-7's), and self-oscillation past ~2 o'clock Regen that SUSTAINS but stays bounded.
    {
        PreDelayBlock cc;
        cc.setModel(PreDelayBlock::kCarbonCopy);
        cc.prepare({SR, BLK});
        const auto v = cc.currentVoicing();
        CHECK(v.bbd && std::abs(v.maxTimeMs - 600.0f) < 0.5f && std::abs(v.bbdStages - 8192.0f) < 0.5f,
              "T13 Carbon Copy = BBD, 600 ms, 8192 stages (%.0f ms, %.0f stages)", v.maxTimeMs, v.bbdStages);
        // dark in-loop bandwidth at a mid setting (below ~3.2 kHz)
        cc.setTimeMs(400.0f);
        settle(cc, 0.3);
        CHECK(cc.currentLoopLpHz() < 3200.0f, "T13 Carbon Copy is dark (in-loop %.0f Hz < 3200)", cc.currentLoopLpHz());
        // self-oscillation: max Regen sustains and stays bounded
        PreDelayBlock osc;
        osc.setModel(PreDelayBlock::kCarbonCopy);
        osc.setTimeMs(180.0f);
        osc.setFeedback(1.0f); // ceiling 1.05
        osc.setMix(1.0f);
        osc.setMod(0.3f);
        osc.prepare({SR, BLK});
        std::vector<float> m((size_t)(SR * 4.0), 0.0f);
        for (int k = 0; k < 300; ++k) m[(size_t)k] = 0.4f; // a burst to excite the loop
        run(osc, m);
        double pk = 0.0; bool finite = true;
        for (size_t i = m.size() * 3 / 4; i < m.size(); ++i) // far tail
        {
            pk = std::max(pk, std::abs((double)m[i]));
            if (!std::isfinite(m[i])) finite = false;
        }
        CHECK(finite, "T13 Carbon Copy self-osc finite");
        CHECK(pk > 0.25, "T13 Carbon Copy self-osc SUSTAINS (tail peak %.2f)", pk);
        CHECK(pk < 4.0, "T13 Carbon Copy self-osc bounded (tail peak %.2f)", pk);
    }

    // ---- T14: Memory Man circuit-grounded voicing (docs/predelay/memory_man.md) ----
    // 2× MN3005 = 8192 stages, 550 ms; and the BLEND is a true CROSSFADE (full wet removes
    // the dry -> vibrato), unlike the DD-7/Carbon Copy dry+wet law.
    {
        PreDelayBlock mm;
        mm.setModel(PreDelayBlock::kMemoryMan);
        mm.prepare({SR, BLK});
        const auto v = mm.currentVoicing();
        CHECK(v.bbd && std::abs(v.maxTimeMs - 550.0f) < 0.5f && std::abs(v.bbdStages - 8192.0f) < 0.5f,
              "T14 Memory Man = BBD, 550 ms, 8192 stages (%.0f ms, %.0f stages)", v.maxTimeMs, v.bbdStages);
        // crossfade: at Mix 1 the dry impulse is removed (wet-only vibrato path)
        PreDelayBlock xf;
        xf.setModel(PreDelayBlock::kMemoryMan);
        xf.setTimeMs(250.0f);
        xf.setFeedback(0.0f);
        xf.setMod(0.0f);
        xf.setMix(1.0f); // full wet -> crossfade removes dry
        xf.prepare({SR, BLK});
        std::vector<float> m((size_t)SR, 0.0f);
        m[0] = 1.0f;
        run(xf, m);
        const size_t T = (size_t)(0.250 * SR);
        const double dryPk = peakAmpNear(m, 0, 64);    // dry impulse should be gone at full wet
        const double echoPk = peakAmpNear(m, T, 400);  // the echo is present
        CHECK(dryPk < 0.05, "T14 Blend crossfade removes dry at full wet (t0 peak %.3f)", dryPk);
        CHECK(echoPk > 0.1, "T14 wet echo present at full wet (echo peak %.3f)", echoPk);
    }

    // ---- T15: Memory Man master Level (Volume) + Chorus/Vibrato switch (LFO speed) ----
    {
        auto rms = [](float level){
            PreDelayBlock d;
            d.setModel(PreDelayBlock::kMemoryMan);
            d.setTimeMs(200.0f); d.setFeedback(0.3f); d.setMix(0.6f); d.setMod(0.0f);
            d.setLevel(level);
            d.prepare({SR, BLK});
            std::vector<float> m((size_t)SR, 0.0f);
            for (size_t i=0;i<m.size();++i) m[i]=0.25f*std::sin(2.0*3.14159265*196.0*i/SR);
            run(d, m);
            double s2=0; for(size_t i=SR/4;i<m.size();++i) s2+=(double)m[i]*m[i];
            return std::sqrt(s2/(double)(m.size()-SR/4));
        };
        const double full = rms(1.0f), half = rms(0.4f);
        CHECK(full > 1e-4 && std::abs(half/full - 0.4) < 0.05,
              "T15 Level scales output (0.4/1.0 ratio %.3f)", half/full);

        // Chorus (slow) vs Vibrato (fast) = different LFO rate => different modulated echo.
        auto renderCV = [](int cv){
            PreDelayBlock d;
            d.setModel(PreDelayBlock::kMemoryMan);
            d.setTimeMs(300.0f); d.setFeedback(0.0f); d.setMix(1.0f); d.setMod(1.0f);
            d.setChorusVib(cv);
            d.prepare({SR, BLK});
            settle(d, 0.4);
            std::vector<float> m((size_t)SR, 0.0f); m[0]=1.0f;
            run(d, m);
            return peakNear(m, (size_t)(0.300*SR), 800);
        };
        CHECK(renderCV(0) != renderCV(1),
              "T15 Chorus vs Vibrato differ (chorus=%zu vibrato=%zu)", renderCV(0), renderCV(1));
    }

    // ---- T16: Korg SDD-3000 controls (docs/predelay/sdd3000.md) ----
    {
        // HIGH filter (2 kHz) lowers the in-loop LP corner vs Flat.
        PreDelayBlock a; a.setModel(PreDelayBlock::kSDD3000); a.setSddHiCut(0); a.prepare({SR, BLK});
        a.setTimeMs(300.0f); settle(a, 0.2); const float flat = a.currentLoopLpHz();
        PreDelayBlock b; b.setModel(PreDelayBlock::kSDD3000); b.setSddHiCut(3); b.prepare({SR, BLK});
        b.setTimeMs(300.0f); settle(b, 0.2);
        CHECK(b.currentLoopLpHz() < flat && b.currentLoopLpHz() < 2500.0f,
              "T16 HIGH filter darkens repeats (%.0f Hz < %.0f Hz)", b.currentLoopLpHz(), flat);

        // Level Balance is a crossfade: full wet removes the dry.
        PreDelayBlock xf; xf.setModel(PreDelayBlock::kSDD3000);
        xf.setTimeMs(250.0f); xf.setFeedback(0.0f); xf.setMod(0.0f); xf.setMix(1.0f);
        xf.prepare({SR, BLK});
        std::vector<float> m((size_t)SR, 0.0f); m[0] = 1.0f; run(xf, m);
        CHECK(peakAmpNear(m, 0, 64) < 0.05, "T16 SDD Balance crossfade removes dry at full wet");
        CHECK(peakAmpNear(m, (size_t)(0.250 * SR), 400) > 0.1, "T16 SDD wet echo present");

        // Input preamp drive + Attenuator change the tone (Mix 0 -> output is the preamp'd dry).
        auto preampRms = [](float in, int at){
            PreDelayBlock p; p.setModel(PreDelayBlock::kSDD3000);
            p.setTimeMs(200.0f); p.setFeedback(0.0f); p.setMix(0.0f);
            p.setSddInput(in); p.setSddAtten(at); p.prepare({SR, BLK});
            std::vector<float> s((size_t)SR, 0.0f);
            for (size_t i=0;i<s.size();++i) s[i]=0.5f*std::sin(2.0*3.14159265*220.0*i/SR);
            run(p, s);
            double e=0; for(size_t i=SR/4;i<s.size();++i) e+=(double)s[i]*s[i];
            return std::sqrt(e/(double)(s.size()-SR/4));
        };
        CHECK(std::abs(preampRms(1.0f,0) - preampRms(0.0f,2)) > 1e-3, "T16 preamp Input/Atten change the tone");

        // Feedback INV changes the repeat train (2nd echo polarity flips).
        auto echo2 = [](bool inv){
            PreDelayBlock p; p.setModel(PreDelayBlock::kSDD3000);
            p.setTimeMs(150.0f); p.setFeedback(0.6f); p.setMix(1.0f); p.setMod(0.0f);
            p.setSddInvert(inv); p.prepare({SR, BLK});
            std::vector<float> s((size_t)SR, 0.0f); s[0]=1.0f; run(p, s);
            return (double)s[(size_t)(0.300 * SR)];
        };
        CHECK(echo2(true) != echo2(false), "T16 feedback INV changes the repeats");
    }

    // T17 SDD converter authenticity: preamp silicon soft-clip compresses; the 13-bit
    // gain-ranging converter is self-dithering (noise floor tracks signal, not fixed).
    {
        // (a) preamp soft-clips a hot signal (bounded, not linear gain).
        auto peakOut = [](int at, float in, float amp){
            PreDelayBlock p; p.setModel(PreDelayBlock::kSDD3000);
            p.setTimeMs(100.0f); p.setFeedback(0.0f); p.setMix(0.0f); // Mix 0 -> output = preamp'd dry
            p.setSddAtten(at); p.setSddInput(in); p.prepare({SR, BLK});
            std::vector<float> m((size_t)SR, 0.0f);
            for (size_t i=0;i<m.size();++i) m[i]=amp*std::sin(2.0*3.14159265*220.0*i/SR);
            run(p, m); double pk=0; for (size_t i=SR/4;i<m.size();++i) pk=std::max(pk,std::abs((double)m[i]));
            return pk;
        };
        const double hot = peakOut(0, 1.0f, 0.6f); // -30 dB, full Input, loud in
        CHECK(hot < 1.05 && hot > 0.5, "T17 preamp soft-clips (bounded peak %.3f)", hot);

        // (b) gain-ranging self-dithers: quant-noise/signal similar for loud vs quiet
        //     (a FIXED quantizer would give ~50x worse ratio for the quiet signal).
        auto noiseRatio = [](float amp){
            PreDelayBlock p; p.setModel(PreDelayBlock::kSDD3000);
            p.setTimeMs(120.0f); p.setFeedback(0.0f); p.setMix(1.0f);
            p.setSddAtten(2); p.setSddInput(0.0f); p.setSddHiCut(0); p.prepare({SR, BLK}); // clean-ish preamp
            std::vector<float> m((size_t)SR, 0.0f);
            for (size_t i=0;i<m.size();++i) m[i]=amp*std::sin(2.0*3.14159265*150.0*i/SR);
            run(p, m);
            double y=0, resid=0, sig=0; int n=0;
            for (size_t i=SR/4;i<m.size();++i){ y+=0.2*((double)m[i]-y); const double r=(double)m[i]-y;
                resid+=r*r; sig+=(double)m[i]*(double)m[i]; ++n; }
            return std::sqrt(resid/std::max(1,n)) / (std::sqrt(sig/std::max(1,n)) + 1e-20);
        };
        const double rLoud = noiseRatio(0.2f), rQuiet = noiseRatio(0.004f);
        CHECK(rQuiet < rLoud*5.0 + 0.02, "T17 gain-ranging self-dithers (loud %.4f quiet %.4f)", rLoud, rQuiet);
    }

    std::printf("\n%s (%d failures)\n", gFails == 0 ? "ALL PASS" : "FAILURES", gFails);
    return gFails > 0 ? 1 : 0;
}
