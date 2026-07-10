#pragma once
// PedalboardBlock — the unified, reorderable front-of-amp "pedalboard" POOL.
//
// It REPLACES the fixed env -> comp -> drive -> premod -> predelay sequence with a
// LOCKED front pair (Env + Comp) followed by a POOL of freely-orderable, lane-
// routable pedal slots. Every engine is OWNED here (pre-allocated in prepare(), so
// the audio thread never allocates) — this is the "real pedal pool" of the
// AmpliTube-style restructure (SCOPE_PEDALBOARD §10): multiples of any type.
//
//        (Locked pair, Trunk)      (free slots, Trunk = "Both")     Amp A lane
//   IN ->[ Env ][ Comp ] --> [p]->[p]->[p]--> ( SPLIT ) ==[p]==[p]==> vA -> Amp A
//                                                   \\====[p]=========> vB -> Amp B
//                                                            (Amp B lane)
//
//   - Env + Comp are a LOCKED front pair: always the first two, always on the Trunk
//     (feed both amps, pre-split), order swappable Env<->Comp only, not lane-routable.
//   - Each FREE slot i owns one of EVERY free engine type (a DrivePedalEngine, a
//     PreModBlock, a PreDelayBlock); its per-slot Type selects which is active
//     (Off = the slot is empty/passthrough). This per-position "all types" model maps
//     1:1 to the per-slot param-union (each slot carries the full drive+mod+delay
//     param set) — the MOD/DRIVE 3-slot-superset precedent, generalised to N slots.
//   - A free slot's Lane (Trunk/A/B) picks its segment; index order (0..kFreeSlots-1)
//     is the processing order within a segment.
//
// BIT-EXACT DEFAULT: place the free slots as [Drive,Drive,Drive,Mod,Delay,Off,Off,Off]
// all on the Trunk, Env-first, and process() runs Env,Comp then those in order on the
// shared bus and copies to vA/vB — numerically identical to today's legacy pre-split
// path (env -> comp -> drive(3-slot) -> premod -> predelay). Solo modes run only the
// active lane, so SoloA stays the byte-exact regression gate.
//
// Header-only DSP: all five engine headers are dependency-light (local headers + std,
// no JUCE), so the whole pool compiles in the offline harness tests/pedalboard_test.cpp
// with REAL engines.

#include "Blocks.h"
#include "EnvFilterBlock.h"
#include "CompBlock.h"
#include "DrivePedalEngine.h"
#include "PreModBlock.h"
#include "PreDelayBlock.h"
#include <cstring>

namespace nam_rig
{

class PedalboardBlock
{
public:
    enum Lane { Trunk = 0, LaneA = 1, LaneB = 2 };
    enum SlotType { TypeOff = 0, TypeDrive = 1, TypeMod = 2, TypeDelay = 3 };
    static constexpr int kFreeSlots = 8; // free (poolable) positions after the locked pair

    // ---- OWNED engines (public so the processor/RigChain set their params directly) ----
    EnvFilterBlock env;                 // locked front pair (Trunk, pre-split)
    CompBlock      comp;
    DrivePedalEngine drive[kFreeSlots]; // one of each type per free position; the
    PreModBlock      mod[kFreeSlots];   // slot's Type selects which is active
    PreDelayBlock    delay[kFreeSlots];

    // ---- configuration (message thread; cheap scalars) ----
    // Reset routing to the migrated DEFAULT that reproduces today's legacy chain:
    // Env-first, Comp on, free slots [Drive,Drive,Drive,Mod,Delay,Off...] all Trunk.
    void setDefaultRouting()
    {
        mEnvFirst = true;
        for (int i = 0; i < kFreeSlots; ++i) { mLane[i] = Trunk; mOn[i] = true; }
        mType[0] = TypeDrive; mType[1] = TypeDrive; mType[2] = TypeDrive;
        mType[3] = TypeMod;   mType[4] = TypeDelay;
        for (int i = 5; i < kFreeSlots; ++i) mType[i] = TypeOff;
    }
    // Clear routing to an empty board (every free slot Off). The owned engines keep
    // their state/params; only the routing map is cleared.
    void clearRouting()
    {
        mEnvFirst = true;
        for (int i = 0; i < kFreeSlots; ++i) { mType[i] = TypeOff; mLane[i] = Trunk; mOn[i] = true; }
    }

    void setEnvFirst(bool envFirst) { mEnvFirst = envFirst; } // locked-pair order swap
    void setSlotType(int i, int type) { if (valid(i)) mType[i] = clampType(type); }
    void setSlotLane(int i, int lane) { if (valid(i)) mLane[i] = clampLane(lane); }
    void setSlotOn(int i, bool on)    { if (valid(i)) mOn[i] = on; }

    int  slotType(int i) const { return valid(i) ? mType[i] : (int)TypeOff; }
    int  slotLane(int i) const { return valid(i) ? mLane[i] : (int)Trunk; }
    bool slotOn(int i)   const { return valid(i) ? mOn[i] : false; }
    bool envFirst()      const { return mEnvFirst; }

    // ---- lifecycle ----
    void prepare(const BlockContext &ctx)
    {
        env.prepare(ctx); comp.prepare(ctx);
        for (int i = 0; i < kFreeSlots; ++i) { drive[i].prepare(ctx); mod[i].prepare(ctx); delay[i].prepare(ctx); }
    }
    void reset()
    {
        env.reset(); comp.reset();
        for (int i = 0; i < kFreeSlots; ++i) { drive[i].reset(); mod[i].reset(); delay[i].reset(); }
    }

    // ---- processing ----
    // trunk : shared mono input bus (post-gate). Locked pair + Trunk slots run on it.
    // vA/vB : per-amp output buffers (caller-owned, sized >= n); filled for running lanes.
    // runA/runB : which amps are live (Solo gating). In Dual both are true.
    // heal  : heal(MonoBlock&, float*, int) after each engine (caller keeps NaN self-heal;
    //         a no-op lambda keeps the clean path byte-exact).
    template <class Heal>
    void process(float *trunk, float *vA, float *vB, int n,
                 bool runA, bool runB, Heal &&heal)
    {
        // 1) LOCKED front pair on the Trunk (pre-split), in the chosen order.
        if (mEnvFirst) { runLocked(env, trunk, n, heal); runLocked(comp, trunk, n, heal); }
        else           { runLocked(comp, trunk, n, heal); runLocked(env, trunk, n, heal); }

        // 2) Free Trunk slots (feed both amps), in index order, on the shared bus.
        for (int i = 0; i < kFreeSlots; ++i)
            if (mLane[i] == Trunk) runSlot(i, trunk, n, heal);

        // 3) Split: each live amp starts from the trunk result.
        if (runA) std::memcpy(vA, trunk, (size_t)n * sizeof(float));
        if (runB) std::memcpy(vB, trunk, (size_t)n * sizeof(float));

        // 4) Amp-A lane, in index order, on vA (only if Amp A is running).
        if (runA)
            for (int i = 0; i < kFreeSlots; ++i)
                if (mLane[i] == LaneA) runSlot(i, vA, n, heal);

        // 5) Amp-B lane, in index order, on vB (only if Amp B is running).
        if (runB)
            for (int i = 0; i < kFreeSlots; ++i)
                if (mLane[i] == LaneB) runSlot(i, vB, n, heal);
    }

    // Board PDC = locked pair + Trunk slots + the heavier of the two lane branches.
    // Bypass does not change latency (Blocks.h contract), so all PRESENT engines count.
    double latencySamples() const
    {
        double trunk = env.latencySamples() + comp.latencySamples();
        double a = 0.0, b = 0.0;
        for (int i = 0; i < kFreeSlots; ++i)
        {
            const double l = engineLatency(i);
            if (l == 0.0) continue;
            if (mLane[i] == LaneA) a += l;
            else if (mLane[i] == LaneB) b += l;
            else trunk += l;
        }
        return trunk + (a > b ? a : b);
    }

    // True if any ACTIVE free slot routes to a lane (so the pre-amp signal actually
    // splits). When false the board is pure-Trunk and vA==vB, so callers can keep the
    // mono fast paths (e.g. skip filling vB in Solo A).
    bool hasLaneRouting() const
    {
        for (int i = 0; i < kFreeSlots; ++i)
            if (mType[i] != TypeOff && mOn[i] && mLane[i] != Trunk) return true;
        return false;
    }

private:
    static bool valid(int i) { return i >= 0 && i < kFreeSlots; }
    static int clampLane(int l) { return l < Trunk ? Trunk : (l > LaneB ? LaneB : l); }
    static int clampType(int t) { return t < TypeOff ? TypeOff : (t > TypeDelay ? TypeDelay : t); }

    // The active engine for a free slot (nullptr when Off).
    MonoBlock *activeEngine(int i)
    {
        switch (mType[i])
        {
        case TypeDrive: return &drive[i];
        case TypeMod:   return &mod[i];
        case TypeDelay: return &delay[i];
        default:        return nullptr;
        }
    }
    double engineLatency(int i) const
    {
        switch (mType[i])
        {
        case TypeDrive: return drive[i].latencySamples();
        case TypeMod:   return mod[i].latencySamples();
        case TypeDelay: return delay[i].latencySamples();
        default:        return 0.0;
        }
    }

    template <class Heal>
    void runSlot(int i, float *buf, int n, Heal &heal)
    {
        if (mType[i] == TypeOff || !mOn[i]) return; // empty / footswitch off -> passthrough
        MonoBlock *e = activeEngine(i);
        if (e == nullptr || e->isBypassed()) return;
        e->process(buf, n);
        heal(*e, buf, n);
    }
    template <class Heal>
    void runLocked(MonoBlock &e, float *buf, int n, Heal &heal)
    {
        if (e.isBypassed()) return; // locked pair footswitch = the engine's own bypass
        e.process(buf, n);
        heal(e, buf, n);
    }

    bool mEnvFirst = true;
    int  mType[kFreeSlots] = { TypeOff, TypeOff, TypeOff, TypeOff, TypeOff, TypeOff, TypeOff, TypeOff };
    int  mLane[kFreeSlots] = { Trunk, Trunk, Trunk, Trunk, Trunk, Trunk, Trunk, Trunk };
    bool mOn[kFreeSlots]   = { true, true, true, true, true, true, true, true };
};

} // namespace nam_rig
