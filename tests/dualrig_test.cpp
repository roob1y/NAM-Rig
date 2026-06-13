// dualrig_test — verification for the dual-rig RigChain core (src/rig/RigChain.h).
// Exits nonzero on any FAIL.
//
// T1 SoloA REGRESSION: SoloA stereo output is BIT-EXACT to the rig-A blocks run
//    in the old serial order (gate->comp->amp->eq->cab, mono, duplicated L/R) —
//    proves the dual split/mix machinery doesn't disturb the shipping path.
// T2 mixer: voices are independent and Dual mixes them with per-rig level + pan
//    (hard pan, centered pan, mono fold) exactly per the pan law.
// T3 pan law endpoints/centre.
// T4 latency invariant: with one shared AA factor the two voices are equal, so
//    SoloA == SoloB == Dual PDC == the manual block sum.

#include "rig/RigChain.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using nam_rig::RigChain;

static int gFails = 0;
#define CHECK(cond, ...)                                                        \
    do                                                                          \
    {                                                                           \
        const bool chkOk_ = (cond);                                             \
        std::printf("%s: ", chkOk_ ? "PASS" : "FAIL");                          \
        std::printf(__VA_ARGS__);                                               \
        std::printf("\n");                                                      \
        if (!chkOk_)                                                            \
            ++gFails;                                                           \
    } while (0)

static bool approxArr(const float *a, const float *b, int n, float tol = 1.0e-6f)
{
    for (int i = 0; i < n; ++i)
        if (std::fabs(a[i] - b[i]) > tol)
            return false;
    return true;
}

static void makeInput(std::vector<float> &x, int n)
{
    x.resize((size_t)n);
    for (int i = 0; i < n; ++i)
        x[(size_t)i] = 0.25f * std::sin(2.0f * 3.14159265f * 220.0f * (float)i / 48000.0f)
                       + 0.02f * (float)((i % 7) - 3);
}

int main(int argc, char **argv)
{
    const char *model = argc > 1 ? argv[1] : "with_meta.nam";
    const int n = 256;
    std::vector<float> x;
    makeInput(x, n);

    // ===================== T1: SoloA bit-exact regression =====================
    {
        RigChain chain;
        const auto info = chain.amp.engine().loadModel(model);
        CHECK(info.ok, "T1 model loads (%s)", info.ok ? "ok" : info.error.c_str());
        chain.prepare(48000.0, n);
        chain.amp.setRequestedFactor(2);
        // Rig A active = amp -> eq(+6 dB @ 1k); cab + everything else out of the way.
        chain.gate.setBypassed(true);
        chain.comp.setBypassed(true);
        chain.eq.setBandGainDb(4, 6.0f);
        chain.cab.setBypassed(true);
        chain.mod.setBypassed(true);
        chain.delay.setBypassed(true);
        chain.reverb.setBypassed(true);
        chain.setMode(RigChain::SoloA);

        // New path: SoloA on a stereo buffer.
        juce::AudioBuffer<float> buf(2, n);
        std::memcpy(buf.getWritePointer(0), x.data(), (size_t)n * sizeof(float));
        buf.clear(1, 0, n);
        chain.process(buf);

        // Reference: a second identical chain, blocks driven in the OLD serial
        // order on a mono buffer (gate/comp bypassed, amp, eq, cab bypassed),
        // then duplicated L/R. Independent of the dual code.
        RigChain ref;
        ref.amp.engine().loadModel(model);
        ref.prepare(48000.0, n);
        ref.amp.setRequestedFactor(2);
        ref.eq.setBandGainDb(4, 6.0f);
        std::vector<float> m = x;
        ref.amp.process(m.data(), n); // gate/comp bypassed -> skipped
        ref.eq.process(m.data(), n);
        // cab bypassed -> skipped; mod/delay/reverb bypassed -> skipped.

        CHECK(std::memcmp(buf.getReadPointer(0), m.data(), (size_t)n * sizeof(float)) == 0,
              "T1 SoloA L == old serial chain (bit-exact)");
        CHECK(std::memcmp(buf.getReadPointer(1), m.data(), (size_t)n * sizeof(float)) == 0,
              "T1 SoloA R == old serial chain (bit-exact)");
    }

    // ===================== T2: mixer / mode / pan =====================
    {
        RigChain chain;
        chain.prepare(48000.0, n);
        // No amps: voices are pure passthrough so the mixer math is exact.
        // Make voiceA != voiceB: eqA +6 @ 1k, eqB bypassed.
        chain.gate.setBypassed(true);
        chain.comp.setBypassed(true);
        chain.amp.setBypassed(true);
        chain.ampB.setBypassed(true);
        chain.eq.setBandGainDb(4, 6.0f);
        chain.eqB.setBypassed(true);
        chain.cab.setBypassed(true);
        chain.cabB.setBypassed(true);
        chain.mod.setBypassed(true);
        chain.delay.setBypassed(true);
        chain.reverb.setBypassed(true);

        auto run = [&](int mode, float lA, float lB, float pA, float pB,
                       int channels, std::vector<float> &outL, std::vector<float> &outR)
        {
            chain.reset();
            chain.setMode(mode);
            chain.setLevelA(lA);
            chain.setLevelB(lB);
            chain.setPanA(pA);
            chain.setPanB(pB);
            juce::AudioBuffer<float> buf(channels, n);
            std::memcpy(buf.getWritePointer(0), x.data(), (size_t)n * sizeof(float));
            if (channels > 1)
                buf.clear(1, 0, n);
            chain.process(buf);
            outL.assign(buf.getReadPointer(0), buf.getReadPointer(0) + n);
            if (channels > 1)
                outR.assign(buf.getReadPointer(1), buf.getReadPointer(1) + n);
        };

        std::vector<float> vA, vAr, vB, vBr, dummy;
        run(RigChain::SoloA, 1.0f, 1.0f, 0, 0, 2, vA, vAr);   // voiceA in L (==R)
        run(RigChain::SoloB, 1.0f, 1.0f, 0, 0, 2, vB, vBr);   // voiceB in L (==R)
        CHECK(approxArr(vA.data(), vAr.data(), n, 0.0f), "T2 SoloA L==R");
        CHECK(approxArr(vB.data(), vBr.data(), n, 0.0f), "T2 SoloB L==R");
        // voiceB is the raw input (eqB bypassed); voiceA is EQ'd -> they differ.
        bool differ = false;
        for (int i = 0; i < n; ++i)
            if (std::fabs(vA[(size_t)i] - vB[(size_t)i]) > 1e-4f) { differ = true; break; }
        CHECK(differ, "T2 voiceA != voiceB (independent voices)");

        // Dual, hard pans: A hard-left @0.8, B hard-right @0.5.
        std::vector<float> dL, dR;
        run(RigChain::Dual, 0.8f, 0.5f, -1.0f, 1.0f, 2, dL, dR);
        std::vector<float> expL(n), expR(n);
        for (int i = 0; i < n; ++i)
        {
            expL[(size_t)i] = vA[(size_t)i] * 0.8f; // B panned fully right -> ~0 in L
            expR[(size_t)i] = vB[(size_t)i] * 0.5f; // A panned fully left  -> ~0 in R
        }
        CHECK(approxArr(dL.data(), expL.data(), n, 2e-5f), "T2 Dual hard-pan L = voiceA*0.8");
        CHECK(approxArr(dR.data(), expR.data(), n, 2e-5f), "T2 Dual hard-pan R = voiceB*0.5");

        // Dual, both centered: equal-power -3 dB on each rig into both sides.
        run(RigChain::Dual, 0.8f, 0.5f, 0.0f, 0.0f, 2, dL, dR);
        const float c = std::cos(3.14159265f * 0.25f); // ~0.70711
        for (int i = 0; i < n; ++i)
            expL[(size_t)i] = vA[(size_t)i] * 0.8f * c + vB[(size_t)i] * 0.5f * c;
        CHECK(approxArr(dL.data(), expL.data(), n, 2e-5f), "T2 Dual centre L mix");
        CHECK(approxArr(dR.data(), expL.data(), n, 2e-5f), "T2 Dual centre R==L mix");

        // Mono fold (1 channel): both rigs summed at level, pan ignored.
        std::vector<float> mL;
        run(RigChain::Dual, 0.8f, 0.5f, -1.0f, 1.0f, 1, mL, dummy);
        for (int i = 0; i < n; ++i)
            expL[(size_t)i] = vA[(size_t)i] * 0.8f + vB[(size_t)i] * 0.5f;
        CHECK(approxArr(mL.data(), expL.data(), n, 2e-5f), "T2 mono fold = A*0.8 + B*0.5");
    }

    // ===================== T3: pan law =====================
    {
        float gL, gR;
        RigChain::panGains(-1.0f, gL, gR);
        CHECK(std::fabs(gL - 1.0f) < 1e-6f && std::fabs(gR) < 1e-6f, "T3 pan -1 -> (1,0)");
        RigChain::panGains(1.0f, gL, gR);
        CHECK(std::fabs(gL) < 1e-6f && std::fabs(gR - 1.0f) < 1e-6f, "T3 pan +1 -> (0,1)");
        RigChain::panGains(0.0f, gL, gR);
        const float c = std::cos(3.14159265f * 0.25f);
        CHECK(std::fabs(gL - c) < 1e-6f && std::fabs(gR - c) < 1e-6f, "T3 pan 0 -> equal-power");
    }

    // ===================== T4: latency invariant (shared AA) =====================
    {
        RigChain chain;
        chain.prepare(48000.0, n);
        chain.amp.setRequestedFactor(2);
        chain.ampB.setRequestedFactor(2); // shared AA -> equal voice latency

        const double manual =
            chain.gate.latencySamples() + chain.comp.latencySamples() +
            chain.amp.latencySamples() + chain.eq.latencySamples() + chain.cab.latencySamples() +
            chain.mod.latencySamples() + chain.delay.latencySamples() + chain.reverb.latencySamples();

        chain.setMode(RigChain::SoloA);
        const double lSoloA = chain.latencySamples();
        chain.setMode(RigChain::SoloB);
        const double lSoloB = chain.latencySamples();
        chain.setMode(RigChain::Dual);
        const double lDual = chain.latencySamples();

        CHECK(std::fabs(lSoloA - manual) < 1e-9, "T4 SoloA PDC == block sum (%.3f)", lSoloA);
        CHECK(std::fabs(lSoloA - lSoloB) < 1e-9, "T4 SoloA PDC == SoloB PDC");
        CHECK(std::fabs(lSoloA - lDual) < 1e-9, "T4 Dual PDC == Solo PDC (shared AA)");
    }

    // ===================== T5: phase-align measurement =====================
    {
        // White-ish noise has a sharp autocorrelation peak -> unambiguous lag.
        const int m = 512;
        std::vector<float> a((size_t)m);
        uint32_t seed = 22222u;
        for (int i = 0; i < m; ++i)
        {
            seed = seed * 1103515245u + 12345u;
            a[(size_t)i] = (float)((int)((seed >> 16) & 0x7fff) - 16384) / 16384.0f;
        }

        // B = A delayed by 9 samples (B later) -> lag ~ +9, not inverted.
        std::vector<float> bShift((size_t)m, 0.0f);
        for (int i = 9; i < m; ++i)
            bShift[(size_t)i] = a[(size_t)(i - 9)];
        auto r1 = nam_rig::PhaseAlign::measure(a.data(), bShift.data(), m, 64);
        CHECK(std::fabs(r1.lagSamples - 9.0) < 0.25 && !r1.invert,
              "T5 lag +9 detected (got %.2f, inv=%d)", r1.lagSamples, (int)r1.invert);

        // Inverted copy -> same lag, polarity flagged.
        std::vector<float> bInv((size_t)m, 0.0f);
        for (int i = 9; i < m; ++i)
            bInv[(size_t)i] = -a[(size_t)(i - 9)];
        auto r2 = nam_rig::PhaseAlign::measure(a.data(), bInv.data(), m, 64);
        CHECK(std::fabs(r2.lagSamples - 9.0) < 0.25 && r2.invert,
              "T5 inverted -> lag +9 + invert (got %.2f, inv=%d)", r2.lagSamples, (int)r2.invert);

        // Fractional delay of 6.4 samples via the same Hermite line the chain uses.
        nam_rig::FracDelayLine fdl;
        fdl.prepare(64);
        std::vector<float> bFrac((size_t)m);
        for (int i = 0; i < m; ++i)
        {
            fdl.write(a[(size_t)i]);
            bFrac[(size_t)i] = fdl.readFrac(6.4);
        }
        auto r3 = nam_rig::PhaseAlign::measure(a.data(), bFrac.data(), m, 64);
        CHECK(std::fabs(r3.lagSamples - 6.4) < 0.2 && !r3.invert,
              "T5 fractional lag ~6.4 (got %.2f)", r3.lagSamples);
    }

    // ===================== T6: chain align + polarity integration =====================
    {
        RigChain chain;
        chain.prepare(48000.0, n);
        // All blocks bypassed -> each voice is the raw input; isolates align+pol.
        chain.gate.setBypassed(true);
        chain.comp.setBypassed(true);
        chain.amp.setBypassed(true);
        chain.ampB.setBypassed(true);
        chain.eq.setBypassed(true);
        chain.eqB.setBypassed(true);
        chain.cab.setBypassed(true);
        chain.cabB.setBypassed(true);
        chain.mod.setBypassed(true);
        chain.delay.setBypassed(true);
        chain.reverb.setBypassed(true);

        auto runMono1 = [&](std::vector<float> &out)
        {
            juce::AudioBuffer<float> buf(2, n);
            std::memcpy(buf.getWritePointer(0), x.data(), (size_t)n * sizeof(float));
            buf.clear(1, 0, n);
            chain.process(buf);
            out.assign(buf.getReadPointer(0), buf.getReadPointer(0) + n);
        };

        // Polarity flip on rig A (Solo A): output = -input, exactly.
        chain.reset();
        chain.setMode(RigChain::SoloA);
        chain.setPolarityA(true);
        chain.setAlignA(0.0);
        std::vector<float> oInv;
        runMono1(oInv);
        bool polOk = true;
        for (int i = 0; i < n; ++i)
            if (oInv[(size_t)i] != -x[(size_t)i]) { polOk = false; break; }
        CHECK(polOk, "T6 polarity flip -> -input (bit-exact)");

        // Integer align of 10 on rig A (Solo A, polarity back to +): out[i]==in[i-10].
        chain.reset();
        chain.setPolarityA(false);
        chain.setAlignA(10.0);
        std::vector<float> oDel;
        runMono1(oDel);
        bool delOk = true;
        for (int i = 10; i < n; ++i)
            if (std::fabs(oDel[(size_t)i] - x[(size_t)(i - 10)]) > 1e-6f) { delOk = false; break; }
        CHECK(delOk, "T6 align +10 -> input delayed 10 samples");

        // PDC reflects the align delay (Solo A): +10 over the no-align case.
        chain.setAlignA(0.0);
        const double pdc0 = chain.latencySamples();
        chain.setAlignA(10.0);
        const double pdc10 = chain.latencySamples();
        CHECK(std::fabs((pdc10 - pdc0) - 10.0) < 1e-9, "T6 align delay folds into PDC (+10)");
    }

    // ===================== T7: auto-align probe recovers a cab offset =========
    // Same model in both rigs; Rig A cab passthrough, Rig B cab = an IR whose
    // body is delayed ~11 samples. measureAlignment() should report B later ~11.
    {
        const char *irPath = argc > 2 ? argv[2] : "delay11.wav";
        RigChain chain;
        chain.amp.engine().loadModel(model);
        chain.ampB.engine().loadModel(model);
        chain.prepare(48000.0, 256);
        chain.amp.setRequestedFactor(1);
        chain.ampB.setRequestedFactor(1);
        const bool irOk = chain.cabB.loadIr(juce::File(juce::String(irPath)));
        CHECK(irOk, "T7 Rig B delay IR loads (%s)", irPath);
        const auto r = chain.measureAlignment();
        CHECK(std::fabs(r.lagSamples - 11.0) < 1.5 && !r.invert,
              "T7 auto-align recovers cab offset ~11 (got %.2f, inv=%d)",
              r.lagSamples, (int)r.invert);
    }

    std::printf("\n%s (%d failure%s)\n", gFails == 0 ? "ALL PASS" : "FAILURES",
                gFails, gFails == 1 ? "" : "s");
    return gFails == 0 ? 0 : 1;
}
