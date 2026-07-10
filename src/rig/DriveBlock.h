#pragma once
// DriveBlock — a 3-slot SERIES rack of drive pedals (shared pre, before the
// A/B split: one board feeding both rigs, the common real two-amp live rig).
//
// As of the pedalboard extraction (Stage A, SCOPE_PEDALBOARD §10) this is a
// THIN WRAPPER over three `DrivePedalEngine` instances (rig/DrivePedalEngine.h).
// Each slot is one single-pedal engine; the rack runs them in series (slot 0,
// then 1, then 2), which is byte-for-byte identical to the old monolithic
// 3-slot loop — every slot always processed the whole buffer before the next,
// so nothing about the numerics changes. All the voicing tables, model catalog,
// per-model I/O + Volume-pot curves and the DSP itself live in DrivePedalEngine;
// this class just re-exposes the same static API (Kind/modelsFor/voicingFor/
// ioFor/volFor/... ) and slot-indexed setters the rest of the plugin + the
// offline drive_test already call, so callers are untouched.
//
// The pedalboard POOL will allocate DrivePedalEngine directly (each drive pedal
// its own freely-routable board slot); this wrapper preserves the classic
// fixed-rack behaviour for the current front-of-amp chain.

#include "Blocks.h"
#include "DrivePedalEngine.h"

namespace nam_rig
{

class DriveBlock : public MonoBlock
{
public:
    const char *name() const override { return "Drive"; }

    static constexpr int kSlots = 3;

    // ---- re-exported types (single source of truth = DrivePedalEngine) ----
    using Engine    = DrivePedalEngine;
    using Kind      = Engine::Kind;
    using Voicing   = Engine::Voicing;
    using Model     = Engine::Model;
    using IoAnchors = Engine::IoAnchors;
    using VolCurve  = Engine::VolCurve;

    // ---- slot-indexed setters (delegate to the per-slot engines) ----
    void setKind(int slot, int k)            { at(slot).setKind(k); }
    void setDrive(int slot, float v)         { at(slot).setDrive(v); }
    void setTone(int slot, float v)          { at(slot).setTone(v); }
    void setLevel(int slot, float v)         { at(slot).setLevel(v); }        // Volume/Level KNOB 0..1
    void setRange(int slot, int r)           { at(slot).setRange(r); }        // treble-boost cap switch
    void setOn(int slot, bool on)            { at(slot).setOn(on); }          // footswitch (default on)
    void setModel(int slot, int m)           { at(slot).setModel(m); }        // model within the category
    void setGateOn(int slot, bool on)        { at(slot).setGateOn(on); }      // fuzz bias-starved gate enable
    void setMigrateFull(int slot, bool full) { at(slot).setMigrateFull(full); } // RAT hump range: Tight/Full

    bool anyActive() const
    {
        for (int s = 0; s < kSlots; ++s)
            if (mPedal[s].active())
                return true;
        return false;
    }

    // Direct access to a single-pedal engine (for the pedalboard router).
    Engine &pedal(int slot) { return at(slot); }
    const Engine &pedal(int slot) const { return mPedal[juce::jlimit(0, kSlots - 1, slot)]; }

    // ---- static catalog / voicing API (forward to DrivePedalEngine) ----
    static const Model *modelsFor(Kind cat, int &count) { return Engine::modelsFor(cat, count); }
    static int modelCount(Kind c)                       { return Engine::modelCount(c); }
    static const char *modelName(Kind c, int m)         { return Engine::modelName(c, m); }
    static const char *modelSub(Kind c, int m)          { return Engine::modelSub(c, m); }
    static bool modelHasRange(Kind c, int m)            { return Engine::modelHasRange(c, m); }
    static bool modelHasGate(Kind c, int m)             { return Engine::modelHasGate(c, m); }
    static bool modelHasMigrate(Kind c, int m)          { return Engine::modelHasMigrate(c, m); }
    static Voicing voicingFor(Kind c, int m)            { return Engine::voicingFor(c, m); }
    static Voicing voicingFor(Kind c)                   { return Engine::voicingFor(c); }
    static IoAnchors ioFor(Kind c, int model)           { return Engine::ioFor(c, model); }
    static void applyIo(IoStage &io, const IoAnchors &a) { Engine::applyIo(io, a); }
    static VolCurve volFor(Kind c, int model)           { return Engine::volFor(c, model); }
    static float volPot(float k, const VolCurve &vc)    { return Engine::volPot(k, vc); }
    static void applyRange(Voicing &v, int rng)         { Engine::applyRange(v, rng); }

    // ---- MonoBlock lifecycle (per-slot, in series) ----
    void prepare(const BlockContext &ctx) override
    {
        for (auto &p : mPedal) p.prepare(ctx);
    }

    void reset() override
    {
        for (auto &p : mPedal) p.reset();
    }

    // Series rack: run each pedal over the whole buffer in turn. Identical to the
    // old single loop (slot 0 -> 1 -> 2), so the no-drive path stays bit-exact.
    void process(float *mono, int numSamples) override
    {
        for (auto &p : mPedal) p.process(mono, numSamples);
    }

private:
    Engine &at(int slot) { return mPedal[juce::jlimit(0, kSlots - 1, slot)]; }

    Engine mPedal[kSlots];
};

} // namespace nam_rig
