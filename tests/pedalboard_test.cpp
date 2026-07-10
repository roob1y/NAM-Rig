// pedalboard_test — offline verification of PedalboardBlock (the unified reorderable
// front-of-amp board). Proves the container's composition/routing WITHOUT any DAW:
//
//   T1  default all-Trunk order == manual sequential composition (BIT-EXACT) — the
//       invariant that makes the migrated default board identical to today's chain.
//   T2  reordering positions reorders the composition (order is honoured).
//   T3  routing truth table: Trunk feeds both amps; Lane A only vA; Lane B only vB.
//   T4  Solo gating: SoloA leaves vB untouched, SoloB leaves vA untouched.
//   T5  bypassed engine is skipped (== the same board with that slot empty).
//   T6  each engine advances EXACTLY once per buffer (stateful, order-sensitive).
//   T7  latency = trunk sum + max(laneA sum, laneB sum); hasLaneRouting().
//
// Engines are deterministic STATEFUL, NON-COMMUTATIVE stubs (affine + one-pole), so
// both "processed in the right order" and "processed exactly once" are observable:
// a wrong order or a double-advance changes the bytes.
//
// Build: added to the console-app foreach in CMakeLists.txt. Offline (sandbox):
//   g++ -std=c++17 -I<juce-stub> tests/pedalboard_test.cpp -o pedalboard_test

#include "rig/PedalboardBlock.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using nam_rig::MonoBlock;
using nam_rig::BlockContext;
using nam_rig::PedalboardBlock;

static int gFail = 0;
static void check(bool ok, const char *what)
{
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++gFail;
}

// A stateful, non-commutative stub pedal: y = onepole( x*mul + add ). Order and
// single-processing both change the output, so the container logic is really tested.
struct StubPedal : MonoBlock
{
    float mul, add, pole, z = 0.0f;
    const char *nm;
    StubPedal(const char *n, float m, float a, float p) : mul(m), add(a), pole(p), nm(n) {}
    const char *name() const override { return nm; }
    void prepare(const BlockContext &) override { z = 0.0f; }
    void reset() override { z = 0.0f; }
    void process(float *x, int n) override
    {
        for (int i = 0; i < n; ++i)
        {
            const float in = x[i] * mul + add;
            z = pole * z + (1.0f - pole) * in;
            x[i] = z;
        }
    }
    double latencySamples() const override { return lat; }
    double lat = 0.0;
};

static bool bitEqual(const float *a, const float *b, int n)
{
    return std::memcmp(a, b, (size_t)n * sizeof(float)) == 0;
}

// no-op heal (keeps the clean path byte-exact, like RigChain's healthy case)
static auto noHeal = [](MonoBlock &, float *, int) {};

static void fillRamp(float *x, int n, float base)
{
    for (int i = 0; i < n; ++i) x[i] = base + 0.013f * (float)i - 0.0007f * (float)(i * i % 37);
}

int main()
{
    const int N = 256;
    const BlockContext ctx{48000.0, N};

    std::printf("pedalboard_test\n");

    // ---- T1: default all-Trunk order == manual sequential (bit-exact) ----
    {
        StubPedal env("env", 0.9f, 0.02f, 0.10f), comp("comp", 1.3f, -0.01f, 0.30f),
            drive("drive", 2.0f, 0.05f, 0.02f), premod("premod", 1.1f, 0.0f, 0.55f),
            predelay("predelay", 0.95f, 0.03f, 0.40f);
        MonoBlock *seq[5] = {&env, &comp, &drive, &premod, &predelay};
        for (auto *b : seq) b->prepare(ctx);

        PedalboardBlock board;
        board.clear();
        for (int i = 0; i < 5; ++i) board.set(i, seq[i], PedalboardBlock::Trunk);

        // Run several blocks so state continuity is exercised, comparing board vA
        // against a manual sequential run over identical engines with identical state.
        StubPedal env2("env", 0.9f, 0.02f, 0.10f), comp2("comp", 1.3f, -0.01f, 0.30f),
            drive2("drive", 2.0f, 0.05f, 0.02f), premod2("premod", 1.1f, 0.0f, 0.55f),
            predelay2("predelay", 0.95f, 0.03f, 0.40f);
        MonoBlock *seq2[5] = {&env2, &comp2, &drive2, &premod2, &predelay2};
        for (auto *b : seq2) b->prepare(ctx);

        bool allEq = true, abEq = true;
        std::vector<float> trunk(N), vA(N), vB(N), man(N);
        for (int blk = 0; blk < 4; ++blk)
        {
            fillRamp(trunk.data(), N, 0.1f * (float)(blk + 1));
            man = trunk;
            board.process(trunk.data(), vA.data(), vB.data(), N, true, true, noHeal);
            for (auto *b : seq2) b->process(man.data(), N);
            if (!bitEqual(vA.data(), man.data(), N)) allEq = false;
            if (!bitEqual(vA.data(), vB.data(), N)) abEq = false; // both amps identical on pure-Trunk
        }
        check(allEq, "T1 default all-Trunk board == manual sequential (bit-exact, multi-block)");
        check(abEq, "T1 pure-Trunk board: vA == vB (both amps get same signal)");
    }

    // ---- T2: reordering positions reorders the composition ----
    {
        StubPedal a("a", 2.0f, 0.1f, 0.0f), b("b", 0.5f, -0.2f, 0.0f); // pole 0 => pure affine, order matters
        a.prepare(ctx); b.prepare(ctx);
        PedalboardBlock board;

        std::vector<float> in(N), vA(N), vB(N), oAB(N), oBA(N);
        fillRamp(in.data(), N, 0.3f);

        board.clear(); board.set(0, &a, PedalboardBlock::Trunk); board.set(1, &b, PedalboardBlock::Trunk);
        { auto t = in; board.process(t.data(), vA.data(), vB.data(), N, true, true, noHeal); oAB = vA; }
        a.reset(); b.reset();
        board.clear(); board.set(0, &b, PedalboardBlock::Trunk); board.set(1, &a, PedalboardBlock::Trunk);
        { auto t = in; board.process(t.data(), vA.data(), vB.data(), N, true, true, noHeal); oBA = vA; }

        // Expected outputs are built by running the SAME engine code in each order —
        // NOT a re-inlined arithmetic formula. A hand-inlined affine expression rounds
        // differently from the engine's own process() under fast-math (/fp:fast FMA
        // contraction), so it would spuriously fail bit-compare on MSVC/clang-cl while
        // being numerically equal. Running the real engines makes the compare exact by
        // construction (same code path), which is what we actually want to verify.
        std::vector<float> mAB(N), mBA(N);
        {
            StubPedal a2("a", 2.0f, 0.1f, 0.0f), b2("b", 0.5f, -0.2f, 0.0f);
            a2.prepare(ctx); b2.prepare(ctx);
            mAB = in; a2.process(mAB.data(), N); b2.process(mAB.data(), N); // order a -> b
        }
        {
            StubPedal a3("a", 2.0f, 0.1f, 0.0f), b3("b", 0.5f, -0.2f, 0.0f);
            a3.prepare(ctx); b3.prepare(ctx);
            mBA = in; b3.process(mBA.data(), N); a3.process(mBA.data(), N); // order b -> a
        }
        check(bitEqual(oAB.data(), mAB.data(), N), "T2 order a->b == run a then b");
        check(bitEqual(oBA.data(), mBA.data(), N), "T2 order b->a == run b then a");
        check(!bitEqual(oAB.data(), oBA.data(), N), "T2 the two orders actually differ");
    }

    // ---- T3: routing truth table (Trunk both / A only vA / B only vB) ----
    {
        StubPedal t("t", 1.7f, 0.05f, 0.2f), pa("pa", 0.6f, -0.1f, 0.3f), pb("pb", 1.4f, 0.2f, 0.1f);
        t.prepare(ctx); pa.prepare(ctx); pb.prepare(ctx);
        PedalboardBlock board;
        board.clear();
        board.set(0, &t, PedalboardBlock::Trunk);
        board.set(1, &pa, PedalboardBlock::LaneA);
        board.set(2, &pb, PedalboardBlock::LaneB);

        std::vector<float> in(N), vA(N), vB(N);
        fillRamp(in.data(), N, 0.25f);
        auto buf = in;
        board.process(buf.data(), vA.data(), vB.data(), N, true, true, noHeal);

        // Expected: tr = t(in); vA = pa(tr); vB = pb(tr).
        StubPedal t2("t", 1.7f, 0.05f, 0.2f), pa2("pa", 0.6f, -0.1f, 0.3f), pb2("pb", 1.4f, 0.2f, 0.1f);
        t2.prepare(ctx); pa2.prepare(ctx); pb2.prepare(ctx);
        std::vector<float> tr = in, eA, eB;
        t2.process(tr.data(), N);
        eA = tr; pa2.process(eA.data(), N);
        eB = tr; pb2.process(eB.data(), N);
        check(bitEqual(vA.data(), eA.data(), N), "T3 vA == pa(trunk(in))");
        check(bitEqual(vB.data(), eB.data(), N), "T3 vB == pb(trunk(in))");
        check(!bitEqual(vA.data(), vB.data(), N), "T3 lane A and lane B diverge as routed");
        check(board.hasLaneRouting(), "T3 hasLaneRouting() true with lane pedals");
    }

    // ---- T4: Solo gating leaves the inactive amp buffer untouched ----
    {
        StubPedal t("t", 1.2f, 0.0f, 0.2f), pa("pa", 0.7f, 0.0f, 0.2f), pb("pb", 1.5f, 0.0f, 0.2f);
        t.prepare(ctx); pa.prepare(ctx); pb.prepare(ctx);
        PedalboardBlock board;
        board.clear();
        board.set(0, &t, PedalboardBlock::Trunk);
        board.set(1, &pa, PedalboardBlock::LaneA);
        board.set(2, &pb, PedalboardBlock::LaneB);

        std::vector<float> in(N), vA(N, 111.0f), vB(N, 222.0f);
        fillRamp(in.data(), N, 0.2f);
        auto buf = in;
        board.process(buf.data(), vA.data(), vB.data(), N, /*runA*/ true, /*runB*/ false, noHeal);
        bool vbUntouched = true;
        for (int i = 0; i < N; ++i) if (vB[i] != 222.0f) vbUntouched = false;
        check(vbUntouched, "T4 SoloA: vB left untouched (sentinel intact)");

        t.reset(); pa.reset(); pb.reset();
        std::fill(vA.begin(), vA.end(), 111.0f);
        std::fill(vB.begin(), vB.end(), 222.0f);
        buf = in;
        board.process(buf.data(), vA.data(), vB.data(), N, /*runA*/ false, /*runB*/ true, noHeal);
        bool vaUntouched = true;
        for (int i = 0; i < N; ++i) if (vA[i] != 111.0f) vaUntouched = false;
        check(vaUntouched, "T4 SoloB: vA left untouched (sentinel intact)");
    }

    // ---- T5: bypassed engine skipped == same board with the slot empty ----
    {
        StubPedal a("a", 1.3f, 0.1f, 0.2f), byp("byp", 3.0f, 0.5f, 0.4f), c("c", 0.8f, -0.05f, 0.1f);
        a.prepare(ctx); byp.prepare(ctx); c.prepare(ctx);
        std::vector<float> in(N), vA(N), vB(N), withByp, without;
        fillRamp(in.data(), N, 0.15f);

        PedalboardBlock board;
        board.clear();
        board.set(0, &a, PedalboardBlock::Trunk);
        board.set(1, &byp, PedalboardBlock::Trunk);
        board.set(2, &c, PedalboardBlock::Trunk);
        byp.setBypassed(true);
        { auto t = in; board.process(t.data(), vA.data(), vB.data(), N, true, true, noHeal); withByp = vA; }

        StubPedal a2("a", 1.3f, 0.1f, 0.2f), c2("c", 0.8f, -0.05f, 0.1f);
        a2.prepare(ctx); c2.prepare(ctx);
        PedalboardBlock board2;
        board2.clear();
        board2.set(0, &a2, PedalboardBlock::Trunk);
        board2.set(2, &c2, PedalboardBlock::Trunk); // slot 1 empty (skipped)
        { auto t = in; board2.process(t.data(), vA.data(), vB.data(), N, true, true, noHeal); without = vA; }
        check(bitEqual(withByp.data(), without.data(), N), "T5 bypassed slot == empty slot");
    }

    // ---- T6: each engine advances exactly once (double-advance would drift) ----
    {
        // A pure-Trunk single stateful pedal over many blocks must equal a lone engine
        // fed the identical stream. If the board advanced it twice, the one-pole state
        // would diverge immediately.
        StubPedal p("p", 1.0f, 0.0f, 0.7f), ref("p", 1.0f, 0.0f, 0.7f);
        p.prepare(ctx); ref.prepare(ctx);
        PedalboardBlock board; board.clear(); board.set(0, &p, PedalboardBlock::Trunk);
        bool eq = true;
        std::vector<float> vA(N), vB(N);
        for (int blk = 0; blk < 8; ++blk)
        {
            std::vector<float> in(N), r(N);
            fillRamp(in.data(), N, 0.05f * (float)(blk + 1));
            r = in;
            board.process(in.data(), vA.data(), vB.data(), N, true, true, noHeal);
            ref.process(r.data(), N);
            if (!bitEqual(vA.data(), r.data(), N)) eq = false;
        }
        check(eq, "T6 engine advanced exactly once per buffer (state continuity)");
    }

    // ---- T7: latency sum + hasLaneRouting ----
    {
        StubPedal t("t", 1, 0, 0), pa("pa", 1, 0, 0), pb("pb", 1, 0, 0), pa2("pa2", 1, 0, 0);
        t.lat = 4.0; pa.lat = 5.0; pb.lat = 7.0; pa2.lat = 3.0;
        PedalboardBlock board; board.clear();
        board.set(0, &t, PedalboardBlock::Trunk);   // trunk 4
        board.set(1, &pa, PedalboardBlock::LaneA);  // A: 5
        board.set(2, &pa2, PedalboardBlock::LaneA); // A: +3 = 8
        board.set(3, &pb, PedalboardBlock::LaneB);  // B: 7
        // expected = trunk(4) + max(A 8, B 7) = 12
        check(board.latencySamples() == 12.0, "T7 latency = trunk + max(laneA, laneB)");

        PedalboardBlock mono; mono.clear();
        mono.set(0, &t, PedalboardBlock::Trunk);
        check(!mono.hasLaneRouting(), "T7 hasLaneRouting() false for pure-Trunk board");
    }

    std::printf("%s (%d failure%s)\n", gFail == 0 ? "ALL PASS" : "FAILURES", gFail, gFail == 1 ? "" : "s");
    return gFail == 0 ? 0 : 1;
}
