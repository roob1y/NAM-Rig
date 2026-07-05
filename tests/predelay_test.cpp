// predelay_test — offline verification harness for PreDelayBlock (the mono
// front-of-amp delay pedal). Exits nonzero on any FAIL. JUCE-free core, built
// with juce_audio_basics only like the other block tests.
//
// T1  first echo lands at the set free time (sample accuracy)
// T2  feedback produces a decaying train of echoes at the delay period
// T3  tempo sync resolves divisions exactly (currentTimeMs + echo position)
// T4  in-loop bandwidth hierarchy (DD-7 bright/fixed > Memory Man > Carbon Copy) + the
//     clock-cap never exceeds the fixed corner (8192-stage BBDs are fixed-filter dominated)
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
// T16 Memory Man revoice (2026-07-04): a PRESENCE peak at ~2.5 kHz (not a 650 Hz low-mid
//     boost), a FIXED-depth modulation (constant pitch swing at every delay), and bounded self-osc
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

    // ---- T4: in-loop bandwidth hierarchy + the clock-Nyquist cap ----
    // NOTE (2026-07-04 revoice): both BBD models have 8192 stages, so their clock-Nyquist
    // stays ABOVE their fixed reconstruction corner across their whole delay range -> the
    // FIXED filter dominates and time-darkening is negligible (the clock-tracking code is
    // real but only bites for lower-stage BBDs, which the shipped lineup no longer has).
    // So T4 asserts the accurate voicing HIERARCHY (DD-7 bright fixed > Memory Man ~3.2k
    // resonant > Carbon Copy ~2.6k dark) and that the clock cap never lets the corner
    // exceed the fixed antiAlias — NOT a "darkens with time" effect that doesn't exist here.
    {
        // Boss DD-7 (digital): fixed, full-band bandwidth regardless of time.
        PreDelayBlock dd;
        dd.setModel(PreDelayBlock::kDD7);
        dd.setMix(0.5f);
        dd.prepare({SR, BLK});
        dd.setTimeMs(120.0f);
        settle(dd, 0.2);
        const float ddC = dd.currentLoopLpHz();
        dd.setTimeMs(1500.0f);
        settle(dd, 0.2);
        const float ddC2 = dd.currentLoopLpHz();
        CHECK(std::abs(ddC - ddC2) < 1.0f, "T4 DD-7 bandwidth fixed (%.0f vs %.0f Hz)", ddC, ddC2);

        // Memory Man: fixed reconstruction corner (~3.2 kHz), never above its antiAlias, and
        // essentially constant across the range (fixed-filter dominated).
        PreDelayBlock mm;
        mm.setModel(PreDelayBlock::kMemoryMan);
        mm.setMix(0.5f);
        mm.prepare({SR, BLK});
        mm.setTimeMs(120.0f);
        settle(mm, 0.3);
        const float mmShort = mm.currentLoopLpHz();
        mm.setTimeMs(550.0f);
        settle(mm, 0.3);
        const float mmLong = mm.currentLoopLpHz();
        CHECK(mmShort <= mm.currentVoicing().antiAliasHz + 1.0f && mmLong <= mmShort + 1.0f,
              "T4 Memory Man corner capped by antiAlias, ~constant (%.0f short, %.0f long, aa %.0f)",
              mmShort, mmLong, mm.currentVoicing().antiAliasHz);

        // Carbon Copy is darker than the Memory Man, both far darker than the DD-7.
        PreDelayBlock cc;
        cc.setModel(PreDelayBlock::kCarbonCopy);
        cc.setMix(0.5f);
        cc.prepare({SR, BLK});
        cc.setTimeMs(400.0f);
        settle(cc, 0.3);
        CHECK(cc.currentLoopLpHz() < mmShort && mmShort < ddC,
              "T4 hierarchy DD-7 > Memory Man > Carbon Copy (%.0f > %.0f > %.0f Hz)",
              ddC, mmShort, cc.currentLoopLpHz());
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
        d.setFeedback(1.0f); // Carbon Copy ceiling 1.18 -> would run away without the in-loop compander
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
            d.setModel(PreDelayBlock::kMemoryMan);
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
    // The DD-7 self-oscillates ("Trick Sound"); its ceiling is 1.05, and it has NO
    // in-loop saturation (24-bit, no compander) — the only thing keeping it from
    // running away is the sub-unity in-loop LP + the loopLimit. Verify it sustains
    // without blowing up.
    {
        PreDelayBlock d;
        d.setModel(PreDelayBlock::kDD7);
        d.setTimeMs(200.0f);
        d.setFeedback(1.0f); // fb = 1.0 * ceiling 1.05
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
        osc.setFeedback(1.0f); // Carbon Copy ceiling 1.18
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

    // ---- T16: Memory Man revoice (docs/predelay/MODEL_REVIEW_2026-07-04.md) ----
    // Factory calibration (Howard Davis/EHX 1978): the delay path is flat below ~900 Hz,
    // peaks ~+3 dB at ~2.5 kHz, and rolls off above ~3.3 kHz. So the wet must be LOUDER at
    // 2.5 kHz than at 500 Hz (a PRESENCE peak) — the OPPOSITE of the old +4 dB @ 650 Hz
    // voicing, which would fail this. And the modulation depth is a FIXED ms, so the echo's
    // pitch/position swing is CONSTANT at every delay (delay-proportional depth made it a
    // multi-octave warble at long delays — Robbie ear-fix 2026-07-04).
    {
        // (a) presence peak: wet-only steady-state RMS at 2.5 kHz vs 500 Hz.
        auto wetRmsAt = [](double hz) {
            PreDelayBlock d;
            d.setModel(PreDelayBlock::kMemoryMan);
            d.setTimeMs(200.0f); d.setFeedback(0.0f); d.setMix(1.0f); d.setMod(0.0f);
            d.prepare({SR, BLK});
            std::vector<float> m((size_t)SR, 0.0f);
            for (size_t i = 0; i < m.size(); ++i) m[i] = 0.25f * std::sin(2.0 * 3.14159265 * hz * i / SR);
            run(d, m);
            double s = 0.0; int n = 0;
            for (size_t i = m.size() / 2; i < m.size(); ++i) { s += (double)m[i] * m[i]; ++n; }
            return std::sqrt(s / (double)std::max(1, n));
        };
        const double r500 = wetRmsAt(500.0), r2500 = wetRmsAt(2500.0);
        CHECK(r2500 > r500 * 1.15,
              "T16 Memory Man presence peak: 2.5kHz louder than 500Hz (r2500=%.3f > r500=%.3f)", r2500, r500);
        // pin the revoiced fields so an accidental edit trips
        PreDelayBlock v; v.setModel(PreDelayBlock::kMemoryMan); v.prepare({SR, BLK});
        const auto vc = v.currentVoicing();
        CHECK(vc.midDb == 0.0f && vc.bwQ > 1.0f && std::abs(vc.antiAliasHz - 3200.0f) < 1.0f
                  && std::abs(vc.modDepthMs - 2.5f) < 0.01f,
              "T16 Memory Man voicing pinned (mid %.0fdB, bwQ %.2f, aa %.0f, modMs %.1f)",
              vc.midDb, vc.bwQ, vc.antiAliasHz, vc.modDepthMs);

        // (b) FIXED-depth modulation: the MAX echo displacement (swept over the LFO cycle so we
        // catch the peak, not a chance zero-crossing) is CONSTANT across delay times -> a musical,
        // predictable chorus/vibrato. Delay-proportional depth would blow up ~5x from 100->500 ms
        // (an unusable warble at long delays); fixed ms keeps the two within a hair.
        auto maxEchoDev = [](float timeMs) {
            const size_t nominal = (size_t)(timeMs * 0.001 * SR);
            double mx = 0.0;
            for (double ph = 0.0; ph < 1.25; ph += 0.1) { // sweep >1 LFO period (chorus ~0.85 Hz)
                PreDelayBlock d;
                d.setModel(PreDelayBlock::kMemoryMan);
                d.setTimeMs(timeMs); d.setFeedback(0.0f); d.setMix(1.0f); d.setMod(1.0f);
                d.prepare({SR, BLK});
                settle(d, ph);
                std::vector<float> m((size_t)SR, 0.0f); m[0] = 1.0f;
                run(d, m);
                const size_t at = peakNear(m, nominal, 3000);
                mx = std::max(mx, (double)std::llabs((long long)at - (long long)nominal));
            }
            return mx;
        };
        const double dev100 = maxEchoDev(100.0f), dev500 = maxEchoDev(500.0f);
        CHECK(dev100 > 50.0 && std::abs(dev500 - dev100) < 0.3 * dev100,
              "T16 modulation depth is fixed/constant across delays (maxdev100=%.0f maxdev500=%.0f)",
              dev100, dev500);

        // (c) the resonant reconstruction LP must keep self-oscillation bounded.
        PreDelayBlock osc;
        osc.setModel(PreDelayBlock::kMemoryMan);
        osc.setTimeMs(200.0f); osc.setFeedback(1.0f); osc.setMix(1.0f); osc.setMod(0.4f);
        osc.prepare({SR, BLK});
        std::vector<float> m((size_t)(SR * 4.0), 0.0f);
        for (int k = 0; k < 300; ++k) m[(size_t)k] = 0.4f;
        run(osc, m);
        double pk = 0.0; bool finite = true;
        for (size_t i = m.size() * 3 / 4; i < m.size(); ++i)
        { pk = std::max(pk, std::abs((double)m[i])); if (!std::isfinite(m[i])) finite = false; }
        CHECK(finite && pk > 0.1 && pk < 4.0,
              "T16 Memory Man self-osc sustains + bounded (tail peak %.2f)", pk);
    }

    // ---- T17-T18: stereo PING-PONG (mono-in / stereo-out; repeats bounce Amp A/L <-> Amp B/R) ----
    {
        auto ppRun = [](PreDelayBlock &d, std::vector<float> &L, std::vector<float> &R, int blk) {
            for (size_t p = 0; p < L.size(); p += (size_t)blk)
                d.processStereo(L.data() + p, R.data() + p,
                                (int)std::min<size_t>((size_t)blk, L.size() - p));
        };

        // T17: band-limited 1 kHz "note" burst, DD-7 300 ms fb 0.5. TRUE alternating ping-pong:
        // taps keep the MONO spacing (300/600/900/1200 ms) but alternate L, R, L, R (NOT doubled
        // — no in-between taps). Equal-level pairs: tap1 L@300 == tap2 R@600; tap3 L@900 == tap4
        // R@1200 a step quieter (fb=0.5 per pair). Each tap hard-panned (opposite side ~silent).
        PreDelayBlock d;
        d.setModel(PreDelayBlock::kDD7); d.setDd7Mode(PreDelayBlock::kMode800);
        d.setTimeMs(300.0f); d.setFeedback(0.5f); d.setMix(1.0f);
        d.prepare({SR, BLK});
        const size_t imp = (size_t)(0.05 * SR);
        std::vector<float> L((size_t)SR * 2, 0.0f), R((size_t)SR * 2, 0.0f);
        for (int k = 0; k < (int)(0.008 * SR); ++k)
        { const float v = 0.5f * (float)std::sin(2.0 * 3.14159265358979323846 * 1000.0 * k / SR);
          L[imp + (size_t)k] = v; R[imp + (size_t)k] = v; }
        ppRun(d, L, R, BLK);
        auto tapPk = [&](std::vector<float> &v, double sec) {
            const size_t c = imp + (size_t)(sec * SR); double m = 0.0;
            for (size_t i = c - (size_t)(0.006 * SR); i < c + (size_t)(0.012 * SR); ++i)
                m = std::max(m, (double)std::abs(v[i]));
            return m; };
        const double L1 = tapPk(L, 0.300), R1 = tapPk(R, 0.300); // tap 1 (LEFT) + R silent there
        const double R2 = tapPk(R, 0.600), L2 = tapPk(L, 0.600); // tap 2 (RIGHT) + L silent there
        const double L3 = tapPk(L, 0.900), R4 = tapPk(R, 1.200); // tap 3 (LEFT) & tap 4 (RIGHT)
        CHECK(L1 > 0.20 && R1 < 0.05, "T17 tap 1 hard-LEFT (L=%.3f R=%.3f)", L1, R1);
        CHECK(R2 > 0.20 && L2 < 0.05, "T17 tap 2 hard-RIGHT (R=%.3f L=%.3f)", R2, L2);
        CHECK(std::abs(L1 - R2) < 0.05 * L1, "T17 tap 1 & 2 EQUAL volume (L1=%.3f R2=%.3f)", L1, R2);
        CHECK(L3 < 0.9 * L1 && std::abs(L3 - R4) < 0.06 * L3,
              "T17 tap 3 & 4 equal + a step quieter (L3=%.3f R4=%.3f L1=%.3f)", L3, R4, L1);

        // T18: block-split == one-shot in stereo (lane + LFO state continuous across blocks) —
        // run the same input in 512- and 37-sample chunks and compare.
        auto runFresh = [](int blk, std::vector<float> &Lv, std::vector<float> &Rv) {
            PreDelayBlock e;
            e.setModel(PreDelayBlock::kMemoryMan);
            e.setTimeMs(180.0f); e.setFeedback(0.5f); e.setMix(0.6f); e.setMod(1.0f);
            e.prepare({SR, BLK});
            for (size_t p = 0; p < Lv.size(); p += (size_t)blk)
                e.processStereo(Lv.data() + p, Rv.data() + p,
                                (int)std::min<size_t>((size_t)blk, Lv.size() - p));
        };
        std::vector<float> bl((size_t)SR, 0.0f), br((size_t)SR, 0.0f);
        std::vector<float> ml((size_t)SR, 0.0f), mr((size_t)SR, 0.0f);
        for (int k = 0; k < 200; ++k)
        { float v = 0.25f; bl[(size_t)k] = v; br[(size_t)k] = v; ml[(size_t)k] = v; mr[(size_t)k] = v; }
        runFresh(BLK, bl, br);
        runFresh(37, ml, mr);
        double md = 0.0;
        for (size_t i = 0; i < bl.size(); ++i)
        { md = std::max(md, (double)std::abs(bl[i] - ml[i])); md = std::max(md, (double)std::abs(br[i] - mr[i])); }
        CHECK(md < 1.0e-5, "T18 ping-pong block-split == one-shot (max diff %.2e)", md);
    }

    std::printf("\n%s (%d failures)\n", gFails == 0 ? "ALL PASS" : "FAILURES", gFails);
    return gFails > 0 ? 1 : 0;
}
