#pragma once
// PedalboardBlock — the unified, reorderable front-of-amp "pedalboard".
//
// It REPLACES the fixed env -> comp -> drive -> premod -> predelay sequence with an
// ordered board of pedal MODULES, each assigned to a routing LANE:
//
//        (Trunk = "Both")                       Amp A lane
//   IN ->[p]->[p]->[p]--> ( SPLIT ) ==[p]==[p]==> vA -> Amp A
//                              \\====[p]=========> vB -> Amp B
//                                       (Amp B lane)
//
//   - Trunk pedals feed BOTH amps (processed once on the shared bus, pre-split).
//   - Lane-A / Lane-B pedals feed only that amp (processed post-split on vA / vB).
//   - Position order (0..kMaxSlots-1) is the processing order within each segment.
//
// The board does NOT own its engines — it holds non-owning MonoBlock* pointers into
// the engines that already live on RigChain (envfilter, comp, drive, premod,
// predelay). It only orchestrates ORDER + LANE, so it cannot change any voicing.
// Each engine is still advanced exactly ONCE per buffer (in the segment it sits on),
// so no block state is duplicated and the model is naturally alias-free.
//
// BIT-EXACT DEFAULT: with every module placed on the Trunk in the current order
// (env, comp, drive, premod, predelay), process() runs them in that order on the
// shared bus and copies the result to vA and vB — numerically identical to today's
// single-bus pre-split path (RigChain's legacy branch). Solo modes run only the
// active lane, so SoloA stays the byte-exact regression gate.
//
// Header-only DSP (compiles in the offline harness, tests/pedalboard_test.cpp) —
// the only dependency is the MonoBlock interface in Blocks.h.

#include "Blocks.h"
#include <cstring>

namespace nam_rig
{

class PedalboardBlock
{
public:
    enum Lane { Trunk = 0, LaneA = 1, LaneB = 2 };
    static constexpr int kMaxSlots = 8;

    struct Slot
    {
        MonoBlock *engine = nullptr; // NOT owned (lives on RigChain)
        int lane = Trunk;
    };

    // ---- configuration (message thread; cheap scalars) ----
    void clear()
    {
        for (auto &s : mSlot) { s.engine = nullptr; s.lane = Trunk; }
    }
    // Place an engine at board position `pos` (0 = first). A null engine or an
    // out-of-range position clears that slot. Positions with no engine are skipped.
    void set(int pos, MonoBlock *engine, int lane)
    {
        if (pos < 0 || pos >= kMaxSlots) return;
        mSlot[pos].engine = engine;
        mSlot[pos].lane   = clampLane(lane);
    }
    void setLane(int pos, int lane)
    {
        if (pos < 0 || pos >= kMaxSlots) return;
        mSlot[pos].lane = clampLane(lane);
    }
    const Slot &slot(int pos) const { return mSlot[pos < 0 ? 0 : (pos >= kMaxSlots ? kMaxSlots - 1 : pos)]; }

    // ---- processing ----
    // trunk : shared mono input bus (post-gate). Trunk pedals run in place on it.
    // vA/vB : per-amp output buffers (caller-owned, sized >= n). On return they hold
    //         the signal ready for the amp split (only filled for running lanes).
    // runA/runB : which amps are live (Solo gating). In Dual both are true.
    // heal  : called after each engine as heal(MonoBlock&, float* buf, int n) so the
    //         caller keeps its NaN/Inf self-heal semantics (template -> no alloc, and
    //         a no-op lambda makes it byte-exact for the clean path).
    //
    // A bypassed engine is skipped (matches RigChain's per-block !isBypassed() guard).
    template <class Heal>
    void process(float *trunk, float *vA, float *vB, int n,
                 bool runA, bool runB, Heal &&heal) const
    {
        // 1) Trunk segment (feeds both amps) — in order, on the shared bus.
        for (int p = 0; p < kMaxSlots; ++p)
        {
            const Slot &s = mSlot[p];
            if (s.engine && s.lane == Trunk && !s.engine->isBypassed())
            {
                s.engine->process(trunk, n);
                heal(*s.engine, trunk, n);
            }
        }

        // 2) Split: each live amp starts from the trunk result.
        if (runA) std::memcpy(vA, trunk, (size_t)n * sizeof(float));
        if (runB) std::memcpy(vB, trunk, (size_t)n * sizeof(float));

        // 3) Amp-A lane — in order, on vA (only if Amp A is running).
        if (runA)
            for (int p = 0; p < kMaxSlots; ++p)
            {
                const Slot &s = mSlot[p];
                if (s.engine && s.lane == LaneA && !s.engine->isBypassed())
                {
                    s.engine->process(vA, n);
                    heal(*s.engine, vA, n);
                }
            }

        // 4) Amp-B lane — in order, on vB (only if Amp B is running).
        if (runB)
            for (int p = 0; p < kMaxSlots; ++p)
            {
                const Slot &s = mSlot[p];
                if (s.engine && s.lane == LaneB && !s.engine->isBypassed())
                {
                    s.engine->process(vB, n);
                    heal(*s.engine, vB, n);
                }
            }
    }

    // Board PDC = trunk pedals + the heavier of the two lane branches. Bypass does
    // not change latency (Blocks.h contract), so all PRESENT engines count. All of
    // today's front pedals report 0, so the default board reports 0 (unchanged).
    double latencySamples() const
    {
        double trunk = 0.0, a = 0.0, b = 0.0;
        for (const auto &s : mSlot)
        {
            if (!s.engine) continue;
            const double l = s.engine->latencySamples();
            if (s.lane == LaneA) a += l;
            else if (s.lane == LaneB) b += l;
            else trunk += l;
        }
        return trunk + (a > b ? a : b);
    }

    // True if any lane pedal exists (i.e. routing actually splits pre-amp signal).
    // When false the board is pure-Trunk and vA==vB, so callers can keep the mono
    // fast paths (e.g. skip filling vB in Solo A).
    bool hasLaneRouting() const
    {
        for (const auto &s : mSlot)
            if (s.engine && s.lane != Trunk) return true;
        return false;
    }

private:
    static int clampLane(int l) { return l < Trunk ? Trunk : (l > LaneB ? LaneB : l); }
    Slot mSlot[kMaxSlots];
};

} // namespace nam_rig
