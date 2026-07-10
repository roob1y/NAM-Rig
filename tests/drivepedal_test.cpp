// drivepedal_test — proves the single-pedal DrivePedalEngine extraction is
// byte-for-byte identical to the old monolithic 3-slot DriveBlock rack.
//
// The rack now IS three DrivePedalEngine instances run in series (slot 0 -> 1 ->
// 2). This suite pins that equivalence so the pedalboard POOL (which allocates
// DrivePedalEngine directly) can trust a board slot == a rack slot:
//   T1  DriveBlock(3 configured slots) == 3 DrivePedalEngine in series, bit-exact
//   T2  one active mid-slot            == a single DrivePedalEngine, bit-exact
//   T3  all-Off rack / Off engine      == input passthrough, bit-exact
//   T4  slot order is non-commutative  AND matches the engine ordering, bit-exact
//   T5  odd/ragged block sizes         == same (per-chunk series order preserved)
// Bit-compare is exact because BOTH paths run the identical code (no re-inlined
// arithmetic, so /fp:fast FMA contraction can't cause a spurious mismatch).
// Exits nonzero on any FAIL.
#include "rig/DriveBlock.h"
#include "rig/DrivePedalEngine.h"
#include <cstdio>
#include <cmath>
#include <cstring>
#include <vector>
#include <algorithm>

using nam_rig::DriveBlock;
using nam_rig::DrivePedalEngine;
using Kind = DriveBlock::Kind;

static int gFails = 0;
#define CHECK(cond, ...) do { \
    const bool ok_ = (cond); \
    std::printf("%s: ", ok_ ? "PASS" : "FAIL"); std::printf(__VA_ARGS__); std::printf("\n"); \
    if (!ok_) ++gFails; } while (0)

static constexpr double SR = 48000.0;
static constexpr int BLK = 512;

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

struct Cfg { Kind kind; int model; float drive, tone, level; int range; bool gateOn; bool migrateFull; };

// three distinct, STATEFUL, non-commutative pedals (order + state are observable)
static const Cfg c0{Kind::Overdrive, 0, 0.62f, 0.40f, 0.60f, 0, true,  false}; // Green Drive (TS808)
static const Cfg c1{Kind::Distortion,0, 0.71f, 0.55f, 0.50f, 0, true,  true }; // Black Rodent (RAT, Full migrate)
static const Cfg c2{Kind::Fuzz,      1, 0.50f, 0.60f, 0.70f, 0, true,  false}; // Violet Ram (Big Muff)

static void cfgSlot(DriveBlock &d, int s, const Cfg &c)
{
    d.setKind(s, (int)c.kind); d.setModel(s, c.model); d.setDrive(s, c.drive);
    d.setTone(s, c.tone); d.setLevel(s, c.level); d.setRange(s, c.range);
    d.setGateOn(s, c.gateOn); d.setMigrateFull(s, c.migrateFull); d.setOn(s, true);
}
static void cfgEng(DrivePedalEngine &e, const Cfg &c)
{
    e.setKind((int)c.kind); e.setModel(c.model); e.setDrive(c.drive);
    e.setTone(c.tone); e.setLevel(c.level); e.setRange(c.range);
    e.setGateOn(c.gateOn); e.setMigrateFull(c.migrateFull); e.setOn(true);
}

static std::vector<float> makeSig(int n)
{
    std::vector<float> x((size_t)n);
    for (int i = 0; i < n; ++i)
    {
        const double t = i / SR;
        float v = 0.60f * (float)std::sin(2.0 * M_PI * 220.0 * t)
                + 0.30f * (float)std::sin(2.0 * M_PI * 1500.0 * t)
                + 0.15f * (float)std::sin(2.0 * M_PI * 60.0 * t);
        v *= (float)std::exp(-t * 1.5); // decaying so envelope/gate/sag paths engage
        x[(size_t)i] = v;
    }
    return x;
}

static bool bitEq(const std::vector<float> &a, const std::vector<float> &b)
{
    return a.size() == b.size()
        && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}

template <class F>
static void runChunks(std::vector<float> &x, int blk, F f)
{
    for (size_t p = 0; p < x.size(); p += (size_t)blk)
        f(x.data() + p, (int)std::min<size_t>((size_t)blk, x.size() - p));
}

int main()
{
    const auto sig = makeSig(4096);

    // ---- T1: 3-slot rack == 3 engines in series (per-chunk order matched) ----
    {
        DriveBlock d; cfgSlot(d, 0, c0); cfgSlot(d, 1, c1); cfgSlot(d, 2, c2);
        d.prepare({SR, BLK});
        DrivePedalEngine e0, e1, e2; cfgEng(e0, c0); cfgEng(e1, c1); cfgEng(e2, c2);
        e0.prepare({SR, BLK}); e1.prepare({SR, BLK}); e2.prepare({SR, BLK});

        auto xb = sig; runChunks(xb, BLK, [&](float *m, int n){ d.process(m, n); });
        auto xe = sig; runChunks(xe, BLK, [&](float *m, int n){ e0.process(m, n); e1.process(m, n); e2.process(m, n); });
        CHECK(bitEq(xb, xe), "T1 DriveBlock(3 slots) == 3 DrivePedalEngine in series, byte-exact");
        // and it actually did something (not a trivial passthrough match)
        CHECK(!bitEq(xb, sig), "T1 the driven rack changed the signal (non-trivial)");
    }

    // ---- T2: one active mid-slot == a single engine ----
    {
        DriveBlock d; cfgSlot(d, 1, c1); d.prepare({SR, BLK}); // slots 0,2 default Off = passthrough
        DrivePedalEngine e; cfgEng(e, c1); e.prepare({SR, BLK});
        auto xb = sig; runChunks(xb, BLK, [&](float *m, int n){ d.process(m, n); });
        auto xe = sig; runChunks(xe, BLK, [&](float *m, int n){ e.process(m, n); });
        CHECK(bitEq(xb, xe), "T2 one active mid-slot == single DrivePedalEngine, byte-exact");
    }

    // ---- T3: all-Off rack / Off engine == passthrough ----
    {
        DriveBlock d; d.prepare({SR, BLK});
        auto xb = sig; runChunks(xb, BLK, [&](float *m, int n){ d.process(m, n); });
        CHECK(bitEq(xb, sig), "T3 all-Off DriveBlock == input passthrough, byte-exact");
        DrivePedalEngine e; e.prepare({SR, BLK});
        auto xe = sig; runChunks(xe, BLK, [&](float *m, int n){ e.process(m, n); });
        CHECK(bitEq(xe, sig), "T3b Off DrivePedalEngine == input passthrough, byte-exact");
    }

    // ---- T4: order is non-commutative AND matches the engine ordering ----
    {
        DriveBlock dab; cfgSlot(dab, 0, c0); cfgSlot(dab, 1, c1); dab.setOn(2, false); dab.prepare({SR, BLK});
        DriveBlock dba; cfgSlot(dba, 0, c1); cfgSlot(dba, 1, c0); dba.setOn(2, false); dba.prepare({SR, BLK});
        auto xab = sig; runChunks(xab, BLK, [&](float *m, int n){ dab.process(m, n); });
        auto xba = sig; runChunks(xba, BLK, [&](float *m, int n){ dba.process(m, n); });
        CHECK(!bitEq(xab, xba), "T4 slot order changes the result (pedals are non-commutative)");

        DrivePedalEngine a0, a1; cfgEng(a0, c0); cfgEng(a1, c1); a0.prepare({SR, BLK}); a1.prepare({SR, BLK});
        auto xe = sig; runChunks(xe, BLK, [&](float *m, int n){ a0.process(m, n); a1.process(m, n); });
        CHECK(bitEq(xab, xe), "T4b rack order [c0,c1] == engines run c0 then c1, byte-exact");
    }

    // ---- T5: ragged/odd block sizes preserve the per-chunk series order ----
    {
        const int ODD = 37;
        DriveBlock d; cfgSlot(d, 0, c0); cfgSlot(d, 1, c1); cfgSlot(d, 2, c2); d.prepare({SR, BLK});
        DrivePedalEngine e0, e1, e2; cfgEng(e0, c0); cfgEng(e1, c1); cfgEng(e2, c2);
        e0.prepare({SR, BLK}); e1.prepare({SR, BLK}); e2.prepare({SR, BLK});
        auto xb = sig; runChunks(xb, ODD, [&](float *m, int n){ d.process(m, n); });
        auto xe = sig; runChunks(xe, ODD, [&](float *m, int n){ e0.process(m, n); e1.process(m, n); e2.process(m, n); });
        CHECK(bitEq(xb, xe), "T5 ragged 37-sample blocks: rack == engines in series, byte-exact");
    }

    std::printf("\nRESULT: %s (%d failures)\n", gFails == 0 ? "ALL PASS" : "FAILURES", gFails);
    return gFails == 0 ? 0 : 1;
}
