// pedalboard_test — offline verification of the PedalboardBlock POOL (the owning,
// reorderable front-of-amp board) using the REAL engines (all five engine headers
// are JUCE-free, so they compile in the sandbox). Proves composition/routing WITHOUT
// a DAW, comparing the board against manual sequential runs of the board's OWN engine
// instances (same code path -> bit-exact by construction, immune to /fp:fast FMA):
//
//   T1  legacy-chain regression: setLegacyChainRouting() (Env,Comp + Drive,Drive,
//       Drive,Mod,Delay all Trunk) == manual env->comp->d0->d1->d2->mod->delay
//       (BIT-EXACT, multi-block) + vA==vB.
//   T2  Env/Comp are ORDINARY poolable slots now (2026-07-11): Env-in-slot0/Comp-in-
//       slot1 vs the reverse changes the result, each matching its manual order — no
//       more locked front pair / frontOrder, order is just slot index like any pedal.
//   T3  routing truth table: a Trunk pedal feeds both, Lane A only vA, Lane B only vB.
//   T4  Solo gating leaves the inactive amp buffer untouched.
//   T5  multiple drives (a real POOL): 3 independent drive slots == manual series.
//   T6  reorder (swap two slots' Types) changes the result and matches manual.
//   T7  a slot set to Off is skipped (== the board without it).
//   T8  each engine advances EXACTLY once per buffer (state continuity).
//   T9  a fresh/empty board (clearRouting(), nothing placed) has latencySamples()==0;
//       the legacy-equivalent full board (setLegacyChainRouting()) also adds no PDC.
//   T10 STEREO Trunk SPLITTER (Dual) == manual mod.processStereo on split copies.
//   T11 STEREO slot in Solo collapses to the BIT-EXACT mono path.
//   T12 STEREO BRIDGE after a Lane-A mono pedal (stereo-in / stereo-out).
//   T13 STEREO OFF is unchanged; a Trunk stereo splitter reports lane routing.
//   T14 Env/Comp are LANE-ROUTABLE now (the old locked pair was Trunk-only): Env on
//       Lane A only, Comp on Lane B only, vA/vB each reflect only their own engine.
//   T15 singleton defensive guard: if routing is ever corrupted into TWO slots both
//       claiming TypeEnv, only the lower-index slot actually runs (process() must not
//       advance the shared engine's state twice in one block).
//   T16 removing Env (Type -> Off) makes it fully inert: latency drops to 0 and the
//       Trunk becomes pure passthrough again (nothing else was placed).
//   T17 STEREO Trunk SPLITTER (Env, Dual) == manual env.processStereo on split copies.
//       Unlike Mod's Spread, Env/Comp have no decorrelation mechanism, so identical
//       L/R input produces identical L/R output -- verified explicitly (not a bug).
//   T18 same shape as T17, for Comp.
//   T19 STEREO BRIDGE (Comp) after DIVERGENT Lane-A/Lane-B drives proves the stereo
//       lanes are UNLINKED (independent detector/GR per lane, no shared sidechain):
//       the board matches TWO SEPARATELY-run single-lane Comp references fed the same
//       divergent content -- a linked/stereo-bus detector could not reproduce this.
//   T20 STEREO Env/Comp slot in Solo collapses to the BIT-EXACT mono path (mirrors T11).
//   T21 STEREO OFF is unchanged for Env/Comp; turning it ON reports lane routing
//       (mirrors T13).
//
// NOTE on T3-T13's isolation: earlier versions of this file bypassed b.env/b.comp
// explicitly to isolate the free-slot pool, because the locked pair was ALWAYS in the
// signal path regardless of bypass state. Now that Env/Comp are ordinary poolable
// slots, clearRouting() alone leaves them unplaced (Type stays Off for every slot) —
// structurally absent from process(), not just bypassed — so no extra isolation call
// is needed.
//
// Build: added to CMakeLists.txt. Offline: g++ -std=c++17 -I src -I<stub> ...
#include "rig/PedalboardBlock.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>

using nam_rig::PedalboardBlock;
using nam_rig::DrivePedalEngine;
using nam_rig::CompBlock;
using nam_rig::EnvFilterBlock;
using nam_rig::PreModBlock;
using nam_rig::PreDelayBlock;
using nam_rig::BlockContext;
using nam_rig::MonoBlock;

static int gFail = 0;
static void check(bool ok, const char *what)
{
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++gFail;
}

static constexpr double SR = 48000.0;
static constexpr int N = 256;
static const BlockContext ctx{SR, N};

static auto noHeal = [](MonoBlock &, float *, int) {};

static bool bitEq(const float *a, const float *b, int n) { return std::memcmp(a, b, (size_t)n * sizeof(float)) == 0; }
static bool bitEqV(const std::vector<float> &a, const std::vector<float> &b) { return a.size() == b.size() && bitEq(a.data(), b.data(), (int)a.size()); }

// deterministic, decaying, harmonically rich block (engages env/gate/sag/mod paths)
static void fillBlock(float *x, int n, int blk)
{
    for (int i = 0; i < n; ++i)
    {
        const double t = (blk * n + i) / SR;
        float v = 0.55f * (float)std::sin(2.0 * 3.14159265358979 * 220.0 * t)
                + 0.30f * (float)std::sin(2.0 * 3.14159265358979 * 1500.0 * t)
                + 0.15f * (float)std::sin(2.0 * 3.14159265358979 * 70.0 * t);
        v *= 0.6f + 0.4f * (float)std::cos(2.0 * 3.14159265358979 * 3.0 * t); // slow amplitude wobble
        x[i] = v;
    }
}

// ---- non-trivial, non-commutative configs (applied to whichever instance) ----
static void cfgDrive(DrivePedalEngine &d, int kind, int model, float drv)
{
    d.setKind(kind); d.setModel(model); d.setDrive(drv); d.setTone(0.55f);
    d.setLevel(0.6f); d.setRange(0); d.setGateOn(true); d.setMigrateFull(false);
    d.setOn(true); d.setBypassed(false);
}
static void cfgComp(CompBlock &c)
{
    c.setMode(1); c.setSustain(0.7f); c.setAttackMs(15.0f); c.setReleaseMs(150.0f);
    c.setRatio(4.0f); c.setLevelDb(0.0f); c.setDryBlend(0.0f); c.setBypassed(false);
}
static void cfgEnv(EnvFilterBlock &e)
{
    e.setVoice(1); e.setMode(1); e.setSensitivity(0.6f); e.setRange(0.5f);
    e.setResonance(0.5f); e.setDepth(0.7f); e.setAttackMs(8.0f);
    e.setDirectionUp(true); e.setBoost(false); e.setMix(1.0f); e.setBypassed(false);
}
static void cfgMod(PreModBlock &m)
{
    m.setType(0); m.setRateHz(1.5f); m.setSyncIndex(0); m.setDepth(0.6f);
    m.setMix(0.5f); m.setFeedback(0.0f); m.setManual(0.15f); m.setWave(0.3f);
    m.setBypassed(false);
}
static void cfgDelay(PreDelayBlock &p)
{
    p.setModel(0); p.setDd7Mode(0); p.setTimeMs(120.0f); p.setSyncIndex(0);
    p.setFeedback(0.3f); p.setMix(0.35f); p.setMod(0.0f); p.setToneHz(20000.0f);
    p.setLevel(1.0f); p.setChorusVib(0); p.setBypassed(false);
}

int main()
{
    std::printf("pedalboard_test (owning pool, real engines)\n");
    const int K = 6; // blocks (state continuity)

    // ---- T1: legacy-chain regression == manual legacy order, bit-exact + vA==vB ----
    {
        PedalboardBlock b; b.prepare(ctx); b.setLegacyChainRouting();
        cfgEnv(b.env); cfgComp(b.comp);
        // setLegacyChainRouting() places slot0=Env, slot1=Comp, slot2..4=Drive,
        // slot5=Mod, slot6=Delay (Env/Comp now occupy real pool slots).
        cfgDrive(b.drive[2], 2, 0, 0.6f); cfgDrive(b.drive[3], 3, 0, 0.5f); cfgDrive(b.drive[4], 4, 1, 0.5f);
        cfgMod(b.mod[5]); cfgDelay(b.delay[6]);

        std::vector<float> boardA, boardB;
        std::vector<float> trunk(N), vA(N), vB(N);
        for (int k = 0; k < K; ++k)
        {
            fillBlock(trunk.data(), N, k);
            b.process(trunk.data(), vA.data(), vB.data(), N, true, true, noHeal);
            boardA.insert(boardA.end(), vA.begin(), vA.end());
            boardB.insert(boardB.end(), vB.begin(), vB.end());
        }
        // Reference = INDEPENDENT fresh engines configured identically (not the board's
        // own instances reset-and-reused: some engines' reset() != freshly-prepared state,
        // which would be a false negative). Same engine code path -> bit-exact.
        EnvFilterBlock e2; CompBlock c2; DrivePedalEngine d20, d21, d22; PreModBlock m2; PreDelayBlock p2;
        e2.prepare(ctx); c2.prepare(ctx); d20.prepare(ctx); d21.prepare(ctx); d22.prepare(ctx); m2.prepare(ctx); p2.prepare(ctx);
        cfgEnv(e2); cfgComp(c2); cfgDrive(d20, 2, 0, 0.6f); cfgDrive(d21, 3, 0, 0.5f); cfgDrive(d22, 4, 1, 0.5f); cfgMod(m2); cfgDelay(p2);
        std::vector<float> man;
        std::vector<float> buf(N);
        for (int k = 0; k < K; ++k)
        {
            fillBlock(buf.data(), N, k);
            e2.process(buf.data(), N); c2.process(buf.data(), N);
            d20.process(buf.data(), N); d21.process(buf.data(), N); d22.process(buf.data(), N);
            m2.process(buf.data(), N); p2.process(buf.data(), N);
            man.insert(man.end(), buf.begin(), buf.end());
        }
        check(bitEqV(boardA, man), "T1 legacy-chain board == manual env->comp->d0->d1->d2->mod->delay (bit-exact, multi-block)");
        check(bitEqV(boardA, boardB), "T1 pure-Trunk board: vA == vB");
    }

    // ---- T2: Env/Comp are ORDINARY poolable slots — order is just slot index ----
    {
        std::vector<float> ef, cf;
        // Env in slot0, Comp in slot1 (both Trunk)
        {
            PedalboardBlock b; b.prepare(ctx); b.clearRouting();
            b.setSlotType(0, PedalboardBlock::TypeEnv);  b.setSlotType(1, PedalboardBlock::TypeComp);
            cfgEnv(b.env); cfgComp(b.comp);
            std::vector<float> t(N), vA(N), vB(N);
            for (int k = 0; k < K; ++k) { fillBlock(t.data(), N, k); b.process(t.data(), vA.data(), vB.data(), N, true, true, noHeal); ef.insert(ef.end(), vA.begin(), vA.end()); }
        }
        // Comp in slot0, Env in slot1 (both Trunk) — the reverse order
        {
            PedalboardBlock b; b.prepare(ctx); b.clearRouting();
            b.setSlotType(0, PedalboardBlock::TypeComp); b.setSlotType(1, PedalboardBlock::TypeEnv);
            cfgEnv(b.env); cfgComp(b.comp);
            std::vector<float> t(N), vA(N), vB(N);
            for (int k = 0; k < K; ++k) { fillBlock(t.data(), N, k); b.process(t.data(), vA.data(), vB.data(), N, true, true, noHeal); cf.insert(cf.end(), vA.begin(), vA.end()); }
        }
        // manual env-first
        std::vector<float> manEF;
        {
            EnvFilterBlock e; CompBlock c; e.prepare(ctx); c.prepare(ctx); cfgEnv(e); cfgComp(c);
            std::vector<float> buf(N);
            for (int k = 0; k < K; ++k) { fillBlock(buf.data(), N, k); e.process(buf.data(), N); c.process(buf.data(), N); manEF.insert(manEF.end(), buf.begin(), buf.end()); }
        }
        check(bitEqV(ef, manEF), "T2 Env(slot0)->Comp(slot1) board == manual env->comp");
        check(!bitEqV(ef, cf), "T2 Env-first != Comp-first (order still matters, now via slot index)");
    }

    // ---- T3: routing truth table (Trunk both / A only / B only) ----
    {
        PedalboardBlock b; b.prepare(ctx); b.clearRouting();
        // slot0 Drive on Trunk, slot1 Mod on Lane A, slot2 Delay on Lane B
        b.setSlotType(0, PedalboardBlock::TypeDrive); b.setSlotLane(0, PedalboardBlock::Trunk);
        b.setSlotType(1, PedalboardBlock::TypeMod);   b.setSlotLane(1, PedalboardBlock::LaneA);
        b.setSlotType(2, PedalboardBlock::TypeDelay); b.setSlotLane(2, PedalboardBlock::LaneB);
        cfgDrive(b.drive[0], 2, 0, 0.6f); cfgMod(b.mod[1]); cfgDelay(b.delay[2]);

        std::vector<float> vA(N), vB(N), t(N);
        fillBlock(t.data(), N, 0);
        b.process(t.data(), vA.data(), vB.data(), N, true, true, noHeal);

        // manual: tr = drive(in); eA = mod(tr); eB = delay(tr)
        b.reset();
        std::vector<float> tr(N); fillBlock(tr.data(), N, 0);
        b.drive[0].process(tr.data(), N);
        std::vector<float> eA = tr, eB = tr;
        b.mod[1].process(eA.data(), N);
        b.delay[2].process(eB.data(), N);
        check(bitEq(vA.data(), eA.data(), N), "T3 vA == mod(drive(in)) (Lane A)");
        check(bitEq(vB.data(), eB.data(), N), "T3 vB == delay(drive(in)) (Lane B)");
        check(!bitEq(vA.data(), vB.data(), N), "T3 Lane A and Lane B diverge");
        check(b.hasLaneRouting(), "T3 hasLaneRouting() true");
    }

    // ---- T4: Solo gating leaves the inactive amp untouched ----
    {
        PedalboardBlock b; b.prepare(ctx); b.clearRouting();
        b.setSlotType(0, PedalboardBlock::TypeDrive); b.setSlotLane(0, PedalboardBlock::Trunk);
        b.setSlotType(1, PedalboardBlock::TypeMod);   b.setSlotLane(1, PedalboardBlock::LaneA);
        b.setSlotType(2, PedalboardBlock::TypeDelay); b.setSlotLane(2, PedalboardBlock::LaneB);
        cfgDrive(b.drive[0], 2, 0, 0.6f); cfgMod(b.mod[1]); cfgDelay(b.delay[2]);

        std::vector<float> t(N), vA(N, 111.0f), vB(N, 222.0f);
        fillBlock(t.data(), N, 0);
        b.process(t.data(), vA.data(), vB.data(), N, /*runA*/ true, /*runB*/ false, noHeal);
        bool vbKept = true; for (int i = 0; i < N; ++i) if (vB[i] != 222.0f) vbKept = false;
        check(vbKept, "T4 SoloA: vB untouched (sentinel intact)");

        b.reset();
        std::fill(vA.begin(), vA.end(), 111.0f); std::fill(vB.begin(), vB.end(), 222.0f);
        fillBlock(t.data(), N, 0);
        b.process(t.data(), vA.data(), vB.data(), N, /*runA*/ false, /*runB*/ true, noHeal);
        bool vaKept = true; for (int i = 0; i < N; ++i) if (vA[i] != 111.0f) vaKept = false;
        check(vaKept, "T4 SoloB: vA untouched (sentinel intact)");
    }

    // ---- T5: multiple drives (a real pool) == manual series ----
    {
        PedalboardBlock b; b.prepare(ctx); b.clearRouting();
        for (int i = 0; i < 3; ++i) { b.setSlotType(i, PedalboardBlock::TypeDrive); b.setSlotLane(i, PedalboardBlock::Trunk); }
        cfgDrive(b.drive[0], 2, 0, 0.55f); cfgDrive(b.drive[1], 4, 0, 0.5f); cfgDrive(b.drive[2], 3, 0, 0.6f);

        std::vector<float> boardA; std::vector<float> t(N), vA(N), vB(N);
        for (int k = 0; k < K; ++k) { fillBlock(t.data(), N, k); b.process(t.data(), vA.data(), vB.data(), N, true, true, noHeal); boardA.insert(boardA.end(), vA.begin(), vA.end()); }
        b.reset();
        std::vector<float> man, buf(N);
        for (int k = 0; k < K; ++k) { fillBlock(buf.data(), N, k); b.drive[0].process(buf.data(), N); b.drive[1].process(buf.data(), N); b.drive[2].process(buf.data(), N); man.insert(man.end(), buf.begin(), buf.end()); }
        check(bitEqV(boardA, man), "T5 three independent drive slots == manual d0->d1->d2 (pool state independent)");
    }

    // ---- T6: reorder (swap two slots' Types) changes result and matches manual ----
    {
        // config A: slot0 Drive, slot1 Mod ; config B: slot0 Mod, slot1 Drive
        std::vector<float> oAB, oBA;
        {
            PedalboardBlock b; b.prepare(ctx); b.clearRouting();
            b.setSlotType(0, PedalboardBlock::TypeDrive); b.setSlotType(1, PedalboardBlock::TypeMod);
            cfgDrive(b.drive[0], 2, 0, 0.6f); cfgMod(b.mod[1]);
            std::vector<float> t(N), vA(N), vB(N); fillBlock(t.data(), N, 0);
            b.process(t.data(), vA.data(), vB.data(), N, true, true, noHeal); oAB.assign(vA.begin(), vA.end());
            // manual d0 -> mod1
            b.reset(); std::vector<float> m(N); fillBlock(m.data(), N, 0); b.drive[0].process(m.data(), N); b.mod[1].process(m.data(), N);
            check(bitEq(oAB.data(), m.data(), N), "T6 [Drive,Mod] == manual drive->mod");
        }
        {
            PedalboardBlock b; b.prepare(ctx); b.clearRouting();
            b.setSlotType(0, PedalboardBlock::TypeMod); b.setSlotType(1, PedalboardBlock::TypeDrive);
            cfgMod(b.mod[0]); cfgDrive(b.drive[1], 2, 0, 0.6f);
            std::vector<float> t(N), vA(N), vB(N); fillBlock(t.data(), N, 0);
            b.process(t.data(), vA.data(), vB.data(), N, true, true, noHeal); oBA.assign(vA.begin(), vA.end());
        }
        check(!bitEqV(oAB, oBA), "T6 the two orders actually differ");
    }

    // ---- T7: a slot set Off is skipped (== board without it) ----
    {
        std::vector<float> withOff, without;
        {
            PedalboardBlock b; b.prepare(ctx); b.clearRouting();
            b.setSlotType(0, PedalboardBlock::TypeDrive); b.setSlotType(1, PedalboardBlock::TypeOff); b.setSlotType(2, PedalboardBlock::TypeDrive);
            cfgDrive(b.drive[0], 2, 0, 0.6f); cfgDrive(b.drive[1], 3, 0, 0.9f /*loud, but Off so ignored*/); cfgDrive(b.drive[2], 4, 0, 0.5f);
            std::vector<float> t(N), vA(N), vB(N); fillBlock(t.data(), N, 0);
            b.process(t.data(), vA.data(), vB.data(), N, true, true, noHeal); withOff.assign(vA.begin(), vA.end());
        }
        {
            PedalboardBlock b; b.prepare(ctx); b.clearRouting();
            b.setSlotType(0, PedalboardBlock::TypeDrive); b.setSlotType(2, PedalboardBlock::TypeDrive); // slot1 left Off
            cfgDrive(b.drive[0], 2, 0, 0.6f); cfgDrive(b.drive[2], 4, 0, 0.5f);
            std::vector<float> t(N), vA(N), vB(N); fillBlock(t.data(), N, 0);
            b.process(t.data(), vA.data(), vB.data(), N, true, true, noHeal); without.assign(vA.begin(), vA.end());
        }
        check(bitEqV(withOff, without), "T7 Off slot skipped (== board without it)");
    }

    // ---- T8: engine advanced exactly once per buffer ----
    {
        PedalboardBlock b; b.prepare(ctx); b.clearRouting();
        b.setSlotType(0, PedalboardBlock::TypeDelay); b.setSlotLane(0, PedalboardBlock::Trunk); // delay = obvious state
        cfgDelay(b.delay[0]);
        PreDelayBlock ref; ref.prepare(ctx); cfgDelay(ref);
        bool eq = true; std::vector<float> t(N), vA(N), vB(N), r(N);
        for (int k = 0; k < K; ++k)
        {
            fillBlock(t.data(), N, k); r.assign(t.begin(), t.end());
            b.process(t.data(), vA.data(), vB.data(), N, true, true, noHeal);
            ref.process(r.data(), N);
            if (!bitEq(vA.data(), r.data(), N)) eq = false;
        }
        check(eq, "T8 engine advanced exactly once per buffer (state continuity)");
    }

    // ---- T9: a fresh/empty board adds no PDC; neither does the legacy-chain board ----
    {
        PedalboardBlock b; b.prepare(ctx); b.clearRouting();
        check(b.latencySamples() == 0.0, "T9 fresh/empty board latency == 0 (nothing placed)");
    }
    {
        PedalboardBlock b; b.prepare(ctx); b.setLegacyChainRouting();
        cfgEnv(b.env); cfgComp(b.comp); cfgDrive(b.drive[2], 2, 0, 0.6f); cfgMod(b.mod[5]); cfgDelay(b.delay[6]);
        check(b.latencySamples() == 0.0, "T9 legacy-chain board latency == 0 (matches pre-pedalboard chain)");
    }

    // ---- T10: STEREO Trunk SPLITTER (Dual) == manual mod.processStereo on split copies ----
    {
        PedalboardBlock b; b.prepare(ctx); b.clearRouting();
        b.setSlotType(0, PedalboardBlock::TypeMod); b.setSlotLane(0, PedalboardBlock::Trunk);
        b.setSlotStereo(0, true);
        cfgMod(b.mod[0]); b.mod[0].setType(0 /*chorus*/); b.mod[0].setSpread(0.8f);

        std::vector<float> boardA, boardB, t(N), vA(N), vB(N);
        for (int k = 0; k < K; ++k)
        {
            fillBlock(t.data(), N, k);
            b.process(t.data(), vA.data(), vB.data(), N, true, true, noHeal);
            boardA.insert(boardA.end(), vA.begin(), vA.end());
            boardB.insert(boardB.end(), vB.begin(), vB.end());
        }
        // reference: a splitter seeds BOTH lanes from the same trunk, then processStereo.
        PreModBlock m; m.prepare(ctx); cfgMod(m); m.setType(0); m.setSpread(0.8f);
        std::vector<float> refA, refB, ta(N), tb(N);
        for (int k = 0; k < K; ++k)
        {
            fillBlock(ta.data(), N, k); tb = ta; // both lanes = the mono trunk
            if (k == 0) m.snapSpread();          // board snaps Spread on the first stereo block
            m.processStereo(ta.data(), tb.data(), N);
            refA.insert(refA.end(), ta.begin(), ta.end());
            refB.insert(refB.end(), tb.begin(), tb.end());
        }
        check(bitEqV(boardA, refA), "T10 splitter vA == manual processStereo L (bit-exact, multi-block)");
        check(bitEqV(boardB, refB), "T10 splitter vB == manual processStereo R (bit-exact, multi-block)");
        check(!bitEqV(boardA, boardB), "T10 splitter decorrelates Amp A vs Amp B (Spread>0)");
    }

    // ---- T11: STEREO slot in Solo collapses to the BIT-EXACT mono path ----
    {
        PedalboardBlock b; b.prepare(ctx); b.clearRouting();
        b.setSlotType(0, PedalboardBlock::TypeDelay); b.setSlotLane(0, PedalboardBlock::Trunk);
        b.setSlotStereo(0, true);
        cfgDelay(b.delay[0]);
        PreDelayBlock ref; ref.prepare(ctx); cfgDelay(ref);
        bool eq = true; std::vector<float> t(N), vA(N), vB(N, 222.0f), r(N);
        for (int k = 0; k < K; ++k)
        {
            fillBlock(t.data(), N, k); r.assign(t.begin(), t.end());
            b.process(t.data(), vA.data(), vB.data(), N, /*runA*/ true, /*runB*/ false, noHeal);
            ref.process(r.data(), N); // mono path
            if (!bitEq(vA.data(), r.data(), N)) eq = false;
        }
        check(eq, "T11 stereo slot in Solo A collapses to bit-exact mono");
    }

    // ---- T12: STEREO BRIDGE after a Lane-A mono pedal (stereo-in / stereo-out) ----
    {
        PedalboardBlock b; b.prepare(ctx); b.clearRouting();
        b.setSlotType(0, PedalboardBlock::TypeMod);   b.setSlotLane(0, PedalboardBlock::LaneA);
        b.setSlotType(1, PedalboardBlock::TypeDelay); b.setSlotLane(1, PedalboardBlock::LaneA); b.setSlotStereo(1, true);
        cfgMod(b.mod[0]); cfgDelay(b.delay[1]);

        std::vector<float> boardA, boardB, t(N), vA(N), vB(N);
        for (int k = 0; k < K; ++k)
        {
            fillBlock(t.data(), N, k);
            b.process(t.data(), vA.data(), vB.data(), N, true, true, noHeal);
            boardA.insert(boardA.end(), vA.begin(), vA.end());
            boardB.insert(boardB.end(), vB.begin(), vB.end());
        }
        // reference: split; A = mod(trunk), B = trunk; then delay.processStereo(A, B).
        PreModBlock m; m.prepare(ctx); cfgMod(m);
        PreDelayBlock d; d.prepare(ctx); cfgDelay(d);
        std::vector<float> refA, refB, ta(N), tb(N);
        for (int k = 0; k < K; ++k)
        {
            fillBlock(ta.data(), N, k); tb = ta; // split
            m.process(ta.data(), N);             // Lane-A mono mod on vA only
            d.processStereo(ta.data(), tb.data(), N); // bridge spans both lanes
            refA.insert(refA.end(), ta.begin(), ta.end());
            refB.insert(refB.end(), tb.begin(), tb.end());
        }
        check(bitEqV(boardA, refA), "T12 bridge vA == mod(A) then processStereo L (bit-exact)");
        check(bitEqV(boardB, refB), "T12 bridge vB == trunk then processStereo R (bit-exact)");
        check(b.hasLaneRouting(), "T12 hasLaneRouting() true with a stereo bridge");
    }

    // ---- T13: STEREO OFF is unchanged; a Trunk stereo splitter reports lane routing ----
    {
        PedalboardBlock b; b.prepare(ctx); b.clearRouting();
        b.setSlotType(0, PedalboardBlock::TypeMod); b.setSlotLane(0, PedalboardBlock::Trunk);
        cfgMod(b.mod[0]);
        std::vector<float> t(N), vA(N), vB(N);
        fillBlock(t.data(), N, 0);
        b.process(t.data(), vA.data(), vB.data(), N, true, true, noHeal);
        check(bitEq(vA.data(), vB.data(), N), "T13 Trunk Mod, stereo OFF: vA == vB (mono, unchanged)");
        check(!b.hasLaneRouting(), "T13 stereo OFF Trunk slot: no lane routing");
        b.setSlotStereo(0, true);
        check(b.hasLaneRouting(), "T13 stereo ON Trunk splitter: reports lane routing (vA!=vB)");
    }

    // ---- T14: Env/Comp are LANE-ROUTABLE now (the old locked pair was Trunk-only) ----
    {
        PedalboardBlock b; b.prepare(ctx); b.clearRouting();
        b.setSlotType(0, PedalboardBlock::TypeEnv);  b.setSlotLane(0, PedalboardBlock::LaneA);
        b.setSlotType(1, PedalboardBlock::TypeComp); b.setSlotLane(1, PedalboardBlock::LaneB);
        cfgEnv(b.env); cfgComp(b.comp);

        std::vector<float> t(N), vA(N), vB(N);
        fillBlock(t.data(), N, 0);
        b.process(t.data(), vA.data(), vB.data(), N, true, true, noHeal);

        // manual: independent fresh engines (see T1) so reset() semantics can't skew this.
        EnvFilterBlock e2; CompBlock c2; e2.prepare(ctx); c2.prepare(ctx); cfgEnv(e2); cfgComp(c2);
        std::vector<float> eA(N), eB(N);
        fillBlock(eA.data(), N, 0); fillBlock(eB.data(), N, 0);
        e2.process(eA.data(), N);
        c2.process(eB.data(), N);

        check(bitEq(vA.data(), eA.data(), N), "T14 vA == env(trunk) (Lane A)");
        check(bitEq(vB.data(), eB.data(), N), "T14 vB == comp(trunk) (Lane B)");
        check(!bitEq(vA.data(), vB.data(), N), "T14 Lane A and Lane B diverge");
        check(b.hasLaneRouting(), "T14 hasLaneRouting() true (Env/Comp can now route per-lane)");
    }

    // ---- T15: singleton guard — two slots both claiming TypeEnv, only the lower
    //      index actually runs (process() must not advance the shared engine twice) ----
    {
        PedalboardBlock b; b.prepare(ctx); b.clearRouting();
        // Pathological routing the UI should never produce (it enforces one-of-each),
        // but process() must defend against it: force TWO slots to TypeEnv directly.
        b.setSlotType(0, PedalboardBlock::TypeEnv); b.setSlotLane(0, PedalboardBlock::Trunk);
        b.setSlotType(1, PedalboardBlock::TypeEnv); b.setSlotLane(1, PedalboardBlock::Trunk);
        cfgEnv(b.env);

        EnvFilterBlock ref; ref.prepare(ctx); cfgEnv(ref);
        bool eq = true; std::vector<float> t(N), vA(N), vB(N), r(N);
        for (int k = 0; k < K; ++k)
        {
            fillBlock(t.data(), N, k); r.assign(t.begin(), t.end());
            b.process(t.data(), vA.data(), vB.data(), N, true, true, noHeal);
            ref.process(r.data(), N); // the shared engine should only ever advance ONCE per block
            if (!bitEq(vA.data(), r.data(), N)) eq = false;
        }
        check(eq, "T15 duplicate Env claim: shared engine advances exactly once (singleton guard holds)");
    }

    // ---- T16: removing Env (Type -> Off) makes it fully inert ----
    {
        PedalboardBlock b; b.prepare(ctx); b.clearRouting();
        b.setSlotType(0, PedalboardBlock::TypeEnv); b.setSlotLane(0, PedalboardBlock::Trunk);
        cfgEnv(b.env);
        check(b.latencySamples() == b.env.latencySamples(), "T16 Env placed: board latency == env's own latency");

        std::vector<float> t(N), vA(N), vB(N), inCopy(N);
        fillBlock(t.data(), N, 0);
        inCopy = t;
        b.process(t.data(), vA.data(), vB.data(), N, true, true, noHeal);
        check(!bitEq(vA.data(), inCopy.data(), N), "T16 Env placed: audibly alters the signal");

        b.setSlotType(0, PedalboardBlock::TypeOff); // remove it
        check(b.latencySamples() == 0.0, "T16 Env removed: board latency back to 0");

        std::vector<float> t2(N), vA2(N), vB2(N);
        fillBlock(t2.data(), N, 0);
        std::vector<float> inCopy2 = t2;
        b.process(t2.data(), vA2.data(), vB2.data(), N, true, true, noHeal);
        check(bitEq(vA2.data(), inCopy2.data(), N), "T16 Env removed: signal passes through unchanged (Trunk had nothing else)");
    }

    // ---- T17: STEREO Trunk SPLITTER (Env, Dual) == manual env.processStereo on split
    //      copies. Unlike Mod's Spread, Env has no decorrelation mechanism, so a
    //      splitter fed identical L/R content produces IDENTICAL L/R output -- that's
    //      the correct, expected behaviour (proven explicitly below), not a bug. ----
    {
        PedalboardBlock b; b.prepare(ctx); b.clearRouting();
        b.setSlotType(0, PedalboardBlock::TypeEnv); b.setSlotLane(0, PedalboardBlock::Trunk);
        b.setSlotStereo(0, true);
        cfgEnv(b.env);

        std::vector<float> boardA, boardB, t(N), vA(N), vB(N);
        for (int k = 0; k < K; ++k)
        {
            fillBlock(t.data(), N, k);
            b.process(t.data(), vA.data(), vB.data(), N, true, true, noHeal);
            boardA.insert(boardA.end(), vA.begin(), vA.end());
            boardB.insert(boardB.end(), vB.begin(), vB.end());
        }
        // reference: a splitter seeds BOTH lanes from the same trunk, then processStereo.
        EnvFilterBlock e; e.prepare(ctx); cfgEnv(e);
        std::vector<float> refA, refB, ta(N), tb(N);
        for (int k = 0; k < K; ++k)
        {
            fillBlock(ta.data(), N, k); tb = ta; // both lanes = the mono trunk
            e.processStereo(ta.data(), tb.data(), N);
            refA.insert(refA.end(), ta.begin(), ta.end());
            refB.insert(refB.end(), tb.begin(), tb.end());
        }
        check(bitEqV(boardA, refA), "T17 Env splitter vA == manual processStereo L (bit-exact, multi-block)");
        check(bitEqV(boardB, refB), "T17 Env splitter vB == manual processStereo R (bit-exact, multi-block)");
        check(bitEqV(boardA, boardB), "T17 Env splitter: identical L/R in -> identical L/R out (no decorrelation, unlike Mod)");
        check(b.hasLaneRouting(), "T17 hasLaneRouting() true with an Env stereo splitter");
    }

    // ---- T18: same shape as T17, for Comp ----
    {
        PedalboardBlock b; b.prepare(ctx); b.clearRouting();
        b.setSlotType(0, PedalboardBlock::TypeComp); b.setSlotLane(0, PedalboardBlock::Trunk);
        b.setSlotStereo(0, true);
        cfgComp(b.comp);

        std::vector<float> boardA, boardB, t(N), vA(N), vB(N);
        for (int k = 0; k < K; ++k)
        {
            fillBlock(t.data(), N, k);
            b.process(t.data(), vA.data(), vB.data(), N, true, true, noHeal);
            boardA.insert(boardA.end(), vA.begin(), vA.end());
            boardB.insert(boardB.end(), vB.begin(), vB.end());
        }
        CompBlock c; c.prepare(ctx); cfgComp(c);
        std::vector<float> refA, refB, ta(N), tb(N);
        for (int k = 0; k < K; ++k)
        {
            fillBlock(ta.data(), N, k); tb = ta;
            c.processStereo(ta.data(), tb.data(), N);
            refA.insert(refA.end(), ta.begin(), ta.end());
            refB.insert(refB.end(), tb.begin(), tb.end());
        }
        check(bitEqV(boardA, refA), "T18 Comp splitter vA == manual processStereo L (bit-exact, multi-block)");
        check(bitEqV(boardB, refB), "T18 Comp splitter vB == manual processStereo R (bit-exact, multi-block)");
        check(bitEqV(boardA, boardB), "T18 Comp splitter: identical L/R in -> identical L/R out (no decorrelation)");
        check(b.hasLaneRouting(), "T18 hasLaneRouting() true with a Comp stereo splitter");
    }

    // ---- T19: STEREO BRIDGE (Comp) after DIVERGENT Lane-A/Lane-B drives proves the
    //      stereo lanes are UNLINKED -- independent detector/GR per lane, no shared
    //      sidechain. If GR were linked (stereo-bus style), the board's output would
    //      NOT match two SEPARATELY-run single-lane references fed the same divergent
    //      content, because a linked detector reacts to BOTH channels' level. ----
    {
        PedalboardBlock b; b.prepare(ctx); b.clearRouting();
        b.setSlotType(0, PedalboardBlock::TypeDrive); b.setSlotLane(0, PedalboardBlock::LaneA);
        b.setSlotType(1, PedalboardBlock::TypeDrive); b.setSlotLane(1, PedalboardBlock::LaneB);
        b.setSlotType(2, PedalboardBlock::TypeComp);  b.setSlotLane(2, PedalboardBlock::LaneA); b.setSlotStereo(2, true);
        cfgDrive(b.drive[0], 2, 0, 0.9f  /*hot*/); cfgDrive(b.drive[1], 3, 0, 0.15f /*quiet, different kind*/);
        cfgComp(b.comp);

        std::vector<float> boardA, boardB, t(N), vA(N), vB(N);
        for (int k = 0; k < K; ++k)
        {
            fillBlock(t.data(), N, k);
            b.process(t.data(), vA.data(), vB.data(), N, true, true, noHeal);
            boardA.insert(boardA.end(), vA.begin(), vA.end());
            boardB.insert(boardB.end(), vB.begin(), vB.end());
        }
        // reference: split -> driveA(A) / driveB(B) -> TWO SEPARATE single-lane Comp
        // engines, one per lane, run independently (this IS "unlinked" by construction).
        DrivePedalEngine dA, dB; dA.prepare(ctx); dB.prepare(ctx);
        cfgDrive(dA, 2, 0, 0.9f); cfgDrive(dB, 3, 0, 0.15f);
        CompBlock cA, cB; cA.prepare(ctx); cB.prepare(ctx); cfgComp(cA); cfgComp(cB);
        std::vector<float> refA, refB, ta(N), tb(N);
        for (int k = 0; k < K; ++k)
        {
            fillBlock(ta.data(), N, k); tb = ta; // split
            dA.process(ta.data(), N); dB.process(tb.data(), N);
            cA.process(ta.data(), N); cB.process(tb.data(), N);
            refA.insert(refA.end(), ta.begin(), ta.end());
            refB.insert(refB.end(), tb.begin(), tb.end());
        }
        check(bitEqV(boardA, refA), "T19 Comp bridge vA == independent single-lane reference (bit-exact)");
        check(bitEqV(boardB, refB), "T19 Comp bridge vB == independent single-lane reference (bit-exact)");
        check(!bitEqV(boardA, boardB), "T19 divergent lane content -> divergent GR (unlinked, not averaged)");
    }

    // ---- T20: STEREO Env/Comp slot in Solo collapses to the BIT-EXACT mono path ----
    {
        PedalboardBlock b; b.prepare(ctx); b.clearRouting();
        b.setSlotType(0, PedalboardBlock::TypeEnv); b.setSlotLane(0, PedalboardBlock::Trunk);
        b.setSlotStereo(0, true);
        cfgEnv(b.env);
        EnvFilterBlock refE; refE.prepare(ctx); cfgEnv(refE);
        bool eqE = true; std::vector<float> t(N), vA(N), vB(N, 222.0f), r(N);
        for (int k = 0; k < K; ++k)
        {
            fillBlock(t.data(), N, k); r.assign(t.begin(), t.end());
            b.process(t.data(), vA.data(), vB.data(), N, /*runA*/ true, /*runB*/ false, noHeal);
            refE.process(r.data(), N);
            if (!bitEq(vA.data(), r.data(), N)) eqE = false;
        }
        check(eqE, "T20 stereo Env slot in Solo A collapses to bit-exact mono");
    }
    {
        PedalboardBlock b; b.prepare(ctx); b.clearRouting();
        b.setSlotType(0, PedalboardBlock::TypeComp); b.setSlotLane(0, PedalboardBlock::Trunk);
        b.setSlotStereo(0, true);
        cfgComp(b.comp);
        CompBlock refC; refC.prepare(ctx); cfgComp(refC);
        bool eqC = true; std::vector<float> t(N), vA(N), vB(N, 222.0f), r(N);
        for (int k = 0; k < K; ++k)
        {
            fillBlock(t.data(), N, k); r.assign(t.begin(), t.end());
            b.process(t.data(), vA.data(), vB.data(), N, /*runA*/ true, /*runB*/ false, noHeal);
            refC.process(r.data(), N);
            if (!bitEq(vA.data(), r.data(), N)) eqC = false;
        }
        check(eqC, "T20 stereo Comp slot in Solo A collapses to bit-exact mono");
    }

    // ---- T21: STEREO OFF is unchanged for Env/Comp; turning it ON reports lane routing ----
    {
        PedalboardBlock b; b.prepare(ctx); b.clearRouting();
        b.setSlotType(0, PedalboardBlock::TypeEnv); b.setSlotLane(0, PedalboardBlock::Trunk);
        cfgEnv(b.env);
        std::vector<float> t(N), vA(N), vB(N);
        fillBlock(t.data(), N, 0);
        b.process(t.data(), vA.data(), vB.data(), N, true, true, noHeal);
        check(bitEq(vA.data(), vB.data(), N), "T21 Trunk Env, stereo OFF: vA == vB (mono, unchanged)");
        check(!b.hasLaneRouting(), "T21 stereo OFF Trunk Env slot: no lane routing");
        b.setSlotStereo(0, true);
        check(b.hasLaneRouting(), "T21 stereo ON Trunk Env splitter: reports lane routing");
    }
    {
        PedalboardBlock b; b.prepare(ctx); b.clearRouting();
        b.setSlotType(0, PedalboardBlock::TypeComp); b.setSlotLane(0, PedalboardBlock::Trunk);
        cfgComp(b.comp);
        std::vector<float> t(N), vA(N), vB(N);
        fillBlock(t.data(), N, 0);
        b.process(t.data(), vA.data(), vB.data(), N, true, true, noHeal);
        check(bitEq(vA.data(), vB.data(), N), "T21 Trunk Comp, stereo OFF: vA == vB (mono, unchanged)");
        check(!b.hasLaneRouting(), "T21 stereo OFF Trunk Comp slot: no lane routing");
        b.setSlotStereo(0, true);
        check(b.hasLaneRouting(), "T21 stereo ON Trunk Comp splitter: reports lane routing");
    }

    std::printf("%s (%d failure%s)\n", gFail == 0 ? "ALL PASS" : "FAILURES", gFail, gFail == 1 ? "" : "s");
    return gFail == 0 ? 0 : 1;
}
