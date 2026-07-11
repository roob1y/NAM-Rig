#pragma once
// PedalboardBlock — the unified, reorderable front-of-amp "pedalboard" POOL.
//
// It REPLACES the fixed env -> comp -> drive -> premod -> predelay sequence with a
// single POOL of freely-orderable, lane-routable pedal slots. Every engine is OWNED
// here (pre-allocated in prepare(), so the audio thread never allocates) — this is the
// "real pedal pool" of the AmpliTube-style restructure (SCOPE_PEDALBOARD §10).
//
//        (free slots, Trunk = "Both")     Amp A lane
//   IN -> [p]->[p]->[p]--> ( SPLIT ) ==[p]==[p]==> vA -> Amp A
//                                \\====[p]=========> vB -> Amp B
//                                         (Amp B lane)
//
//   - Each FREE slot i owns one of EVERY free engine type (a DrivePedalEngine, a
//     PreModBlock, a PreDelayBlock); its per-slot Type selects which is active
//     (Off = the slot is empty/passthrough). This per-position "all types" model maps
//     1:1 to the per-slot param-union (each slot carries the full drive+mod+delay
//     param set) — the MOD/DRIVE 3-slot-superset precedent, generalised to N slots.
//   - A free slot's Lane (Trunk/A/B) picks its segment; index order (0..kFreeSlots-1)
//     is the processing order within a segment.
//   - Env and Comp are POOLABLE too, same as Drive/Mod/Delay — but each is a SINGLETON
//     (one physical EnvFilterBlock/CompBlock instance, `env`/`comp` below, shared by
//     whichever slot claims TypeEnv/TypeComp). They're addable/removable/reorderable/
//     lane-routable exactly like any other pedal; a fresh board starts with neither
//     placed (empty), matching every other slot type's Off default. process() defends
//     against two slots claiming the same singleton type (see the `live()` guard) —
//     that should never happen (the UI enforces one-of-each), but the guard keeps the
//     shared engine's internal state from being advanced twice in one block if it did.
//   - Neither Env nor Comp support the Stereo span (stereoAt() only allows Mod/Delay).
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
    enum SlotType { TypeOff = 0, TypeDrive = 1, TypeMod = 2, TypeDelay = 3, TypeEnv = 4, TypeComp = 5 };
    static constexpr int kFreeSlots = 8; // poolable positions (Env/Comp now compete for these too)

    // ---- OWNED engines (public so the processor/RigChain set their params directly) ----
    EnvFilterBlock env;                 // SINGLETON pool engines: claimed by at most one
    CompBlock      comp;                // slot each (TypeEnv / TypeComp), see class comment.
    DrivePedalEngine drive[kFreeSlots]; // one of each type per free position; the
    PreModBlock      mod[kFreeSlots];   // slot's Type selects which is active
    PreDelayBlock    delay[kFreeSlots];

    // ---- configuration (message thread; cheap scalars) ----
    // Historical regression fixture: reproduces the pre-pedalboard fixed chain
    // (env -> comp -> drive x3 -> mod -> delay, all Trunk) using today's poolable
    // slots, for bit-exact comparison against the old hardcoded path. NOT the runtime
    // default — a fresh board starts empty via clearRouting() (every slot Off,
    // including Env/Comp) and the user adds only what they want.
    void setLegacyChainRouting()
    {
        for (int i = 0; i < kFreeSlots; ++i) { mLane[i] = Trunk; mOn[i] = true; mStereo[i] = false; }
        mType[0] = TypeEnv;   mType[1] = TypeComp;
        mType[2] = TypeDrive; mType[3] = TypeDrive; mType[4] = TypeDrive;
        mType[5] = TypeMod;   mType[6] = TypeDelay;
        mType[7] = TypeOff;
    }
    // Clear routing to an empty board (every free slot Off, including Env/Comp). The
    // owned engines keep their state/params; only the routing map is cleared.
    void clearRouting()
    {
        for (int i = 0; i < kFreeSlots; ++i) { mType[i] = TypeOff; mLane[i] = Trunk; mOn[i] = true; mStereo[i] = false; }
    }

    void setSlotType(int i, int type) { if (valid(i)) mType[i] = clampType(type); }
    void setSlotLane(int i, int lane) { if (valid(i)) mLane[i] = clampLane(lane); }
    void setSlotOn(int i, bool on)    { if (valid(i)) mOn[i] = on; }
    // STEREO: a Mod/Delay slot that SPANS both amps. It runs AFTER the split with the
    // engine's stereo path (processStereo): L feeds Amp A, R feeds Amp B. On the Trunk it
    // seeds both lanes from the (mono) trunk -> a SPLITTER (mono in, decorrelated A/B out);
    // after lane pedals it processes an existing A/B image -> a BRIDGE. Ignored on Drive/Off.
    // Engages only when BOTH amps run (Dual); Solo collapses to the mono path (bit-exact).
    void setSlotStereo(int i, bool on) { if (valid(i)) mStereo[i] = on; }

    int  slotType(int i) const { return valid(i) ? mType[i] : (int)TypeOff; }
    int  slotLane(int i) const { return valid(i) ? mLane[i] : (int)Trunk; }
    bool slotOn(int i)   const { return valid(i) ? mOn[i] : false; }
    bool slotStereo(int i) const { return valid(i) ? mStereo[i] : false; }
    // Which slot (if any) currently holds the Env/Comp singleton — -1 if unplaced.
    int envSlot() const { for (int i = 0; i < kFreeSlots; ++i) if (mType[i] == TypeEnv) return i; return -1; }
    int compSlot() const { for (int i = 0; i < kFreeSlots; ++i) if (mType[i] == TypeComp) return i; return -1; }

    // ---- lifecycle ----
    void prepare(const BlockContext &ctx)
    {
        env.prepare(ctx); comp.prepare(ctx);
        for (int i = 0; i < kFreeSlots; ++i) { drive[i].prepare(ctx); mod[i].prepare(ctx); delay[i].prepare(ctx); }
    }
    void reset()
    {
        env.reset(); comp.reset();
        for (int i = 0; i < kFreeSlots; ++i) { drive[i].reset(); mod[i].reset(); delay[i].reset(); mWasStereo[i] = false; }
    }

    // ---- processing ----
    // trunk : shared mono input bus (post-gate). Trunk slots run on it.
    // vA/vB : per-amp output buffers (caller-owned, sized >= n); filled for running lanes.
    // runA/runB : which amps are live (Solo gating). In Dual both are true.
    // heal  : heal(MonoBlock&, float*, int) after each engine (caller keeps NaN self-heal;
    //         a no-op lambda keeps the clean path byte-exact).
    template <class Heal>
    void process(float *trunk, float *vA, float *vB, int n,
                 bool runA, bool runB, Heal &&heal)
    {
        // Env/Comp singleton guard: at most one slot's claim on each type actually runs
        // per block (see class comment) — cheap, stack-local, no effect in the normal
        // one-of-each case.
        bool envClaimed = false, compClaimed = false;
        auto live = [&](int i) {
            if (mType[i] == TypeEnv)  { if (envClaimed)  return false; envClaimed  = true; }
            if (mType[i] == TypeComp) { if (compClaimed) return false; compClaimed = true; }
            return true;
        };

        // 1) Free MONO Trunk slots (feed both amps), in index order, on the shared bus.
        //    STEREO Trunk slots are NOT pre-split -> they defer to the post-split pass below
        //    as splitters (they need the two lane buffers to write L/R).
        for (int i = 0; i < kFreeSlots; ++i)
            if (mLane[i] == Trunk && !stereoAt(i) && live(i)) runSlot(i, trunk, n, heal);

        // 2) Split: each live amp starts from the trunk result.
        if (runA) std::memcpy(vA, trunk, (size_t)n * sizeof(float));
        if (runB) std::memcpy(vB, trunk, (size_t)n * sizeof(float));

        // 3) Post-split pass, ONE index-ordered walk (was two lane loops; unifying is
        //    bit-exact for independent A/B content and lets a STEREO slot be invoked once
        //    with BOTH buffers at its position in the flow):
        //      - Lane-A mono slot  -> vA         - Lane-B mono slot -> vB
        //      - STEREO slot (Dual) -> processStereo(vA, vB)  (spans both amps)
        //      - STEREO slot (Solo) -> mono path on the single live amp (bit-exact)
        for (int i = 0; i < kFreeSlots; ++i)
        {
            if (mType[i] == TypeOff || !mOn[i]) { mWasStereo[i] = false; continue; }
            if (stereoAt(i) && runA && runB)
            {
                if (live(i)) runSlotStereo(i, vA, vB, n, heal); // mWasStereo updated inside
                continue;
            }
            // Mono placement. A stereo slot in Solo collapses to the live amp's lane.
            const int lane = stereoAt(i) ? (runA ? LaneA : LaneB) : mLane[i];
            if (lane == LaneA) { if (runA && live(i)) runSlot(i, vA, n, heal); }
            else if (lane == LaneB) { if (runB && live(i)) runSlot(i, vB, n, heal); }
            // lane == Trunk (mono) already ran pre-split -> nothing here.
            mWasStereo[i] = false;
        }
    }

    // Board PDC = Trunk slots + the heavier of the two lane branches. Bypass does not
    // change latency (Blocks.h contract), so all PLACED engines count regardless of
    // footswitch/bypass state — but an unplaced Env/Comp (no slot claims it) counts 0,
    // same as any other Off slot.
    double latencySamples() const
    {
        double trunk = 0.0, a = 0.0, b = 0.0;
        bool envSeen = false, compSeen = false; // singleton guard, mirrors process()
        for (int i = 0; i < kFreeSlots; ++i)
        {
            if (mType[i] == TypeEnv)  { if (envSeen)  continue; envSeen  = true; }
            if (mType[i] == TypeComp) { if (compSeen) continue; compSeen = true; }
            const double l = engineLatency(i);
            if (l == 0.0) continue;
            if (stereoAt(i)) { a += l; b += l; } // spans both amps -> counts on each lane
            else if (mLane[i] == LaneA) a += l;
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
            if (mType[i] != TypeOff && mOn[i] && (mLane[i] != Trunk || stereoAt(i))) return true;
        return false;
    }

private:
    static bool valid(int i) { return i >= 0 && i < kFreeSlots; }
    static int clampLane(int l) { return l < Trunk ? Trunk : (l > LaneB ? LaneB : l); }
    static int clampType(int t) { return t < TypeOff ? TypeOff : (t > TypeComp ? TypeComp : t); }

    // The active engine for a free slot (nullptr when Off). TypeEnv/TypeComp return the
    // shared singleton — callers must not invoke this for more than one slot per type
    // in the same block (process()/latencySamples() both guard against that).
    MonoBlock *activeEngine(int i)
    {
        switch (mType[i])
        {
        case TypeDrive: return &drive[i];
        case TypeMod:   return &mod[i];
        case TypeDelay: return &delay[i];
        case TypeEnv:   return &env;
        case TypeComp:  return &comp;
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
        case TypeEnv:   return env.latencySamples();
        case TypeComp:  return comp.latencySamples();
        default:        return 0.0;
        }
    }

    // Only Mod/Delay have a stereo path; Drive/Off ignore the stereo flag.
    bool stereoAt(int i) const { return mStereo[i] && (mType[i] == TypeMod || mType[i] == TypeDelay); }

    template <class Heal>
    void runSlot(int i, float *buf, int n, Heal &heal)
    {
        if (mType[i] == TypeOff || !mOn[i]) return; // empty / footswitch off -> passthrough
        MonoBlock *e = activeEngine(i);
        if (e == nullptr || e->isBypassed()) return;
        e->process(buf, n);
        heal(*e, buf, n);
    }
    // Stereo span: run the slot's engine as L=Amp A / R=Amp B. The engines read independent
    // L/R input, so this serves BOTH the splitter (vA==vB seeded from trunk) and the bridge
    // (vA!=vB from upstream lane pedals). Delay self-reseeds its R lane on the mono->stereo
    // edge; Mod snaps its Spread so the first stereo block honours the width exactly.
    template <class Heal>
    void runSlotStereo(int i, float *vA, float *vB, int n, Heal &heal)
    {
        if (mType[i] == TypeMod)
        {
            if (mod[i].isBypassed()) { mWasStereo[i] = false; return; }
            if (!mWasStereo[i]) mod[i].snapSpread();
            mod[i].processStereo(vA, vB, n);
            heal(mod[i], vA, n); heal(mod[i], vB, n);
            mWasStereo[i] = true;
        }
        else if (mType[i] == TypeDelay)
        {
            if (delay[i].isBypassed()) { mWasStereo[i] = false; return; }
            delay[i].processStereo(vA, vB, n);
            heal(delay[i], vA, n); heal(delay[i], vB, n);
            mWasStereo[i] = true;
        }
    }
    int  mType[kFreeSlots] = { TypeOff, TypeOff, TypeOff, TypeOff, TypeOff, TypeOff, TypeOff, TypeOff };
    int  mLane[kFreeSlots] = { Trunk, Trunk, Trunk, Trunk, Trunk, Trunk, Trunk, Trunk };
    bool mOn[kFreeSlots]   = { true, true, true, true, true, true, true, true };
    bool mStereo[kFreeSlots] = { false, false, false, false, false, false, false, false };
    // Per-slot "was stereo last block" edge tracker (NOT routing state): drives the one-shot
    // reseed/Spread-snap when a slot first engages its stereo path. Mutated during process().
    bool mWasStereo[kFreeSlots] = { false, false, false, false, false, false, false, false };
};

} // namespace nam_rig
