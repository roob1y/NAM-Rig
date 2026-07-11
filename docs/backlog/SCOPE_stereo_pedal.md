# SCOPE — Stereo pedals on the pedalboard

**Status:** design / not started · **Date:** 2026-07-11 · **Area:** `PedalboardBlock`, `RigChain`, `PedalboardPanel`

## Goal

Let any modulation or delay pedal in the pedalboard be switched to **stereo** with a
per-pedal STEREO toggle. A stereo pedal spans amp A and amp B vertically in the UI and
feeds both amps. Two behaviours are supported (both requested):

- **Splitter** — mono in, the pedal *is* the A/B split. Its L output feeds Amp A, its R
  output feeds Amp B. This is what "placed on the trunk, decorrelates into both amps" means.
- **Bridge** — the A/B split already happened upstream; the pedal sits after it and
  processes lane A and lane B together as one true stereo pair (e.g. a stereo chorus or
  ping-pong delay across both amps). This is the "placed after a pedal on Amp A, spans down
  to Amp B" case.

Drives stay mono. **Mono is preserved bit-exact** whenever STEREO is off, or the rig is in
Solo / single-amp mode.

---

## Current state (verified against code)

- `PedalboardBlock` (`src/rig/PedalboardBlock.h:46`) is **entirely mono**. `process(float* trunk,
  float* vA, float* vB, int n, runA, runB, heal)` runs the locked Env/Comp pair + Trunk slots on
  the mono `trunk`, then **`memcpy`s `trunk` into `vA` and `vB`** (the split, lines 119–121), then
  processes LaneA slots on `vA` and LaneB slots on `vB` in two separate loops (lines 123–131).
- Every pedal is a `MonoBlock` — `activeEngine(i)->process(float*, int)`. The pool only ever
  hands a pedal **one** mono buffer (`runSlot`, line 188).
- Routing state per slot: `mType[8]`, `mLane[8]` (`Lane { Trunk=0, LaneA=1, LaneB=2 }`, line 49),
  `mOn[8]`, plus `mEnvFirst`. Mirrored by the `pbS{i}*` param union (36 suffixes,
  `PedalboardPanel.h:273`).
- The signal only becomes true 2-channel stereo at `mix()`, **after the cabs** (`RigChain`).
  The whole front-of-amp section, including both amp lanes, is mono.

**The key asset:** `PreModBlock::processStereo(L, R, n)` and `PreDelayBlock::processStereo(L, R, n)`
already exist — **mono-in / stereo-out**, with mono-collapse and R-lane reseed already handled.
The legacy (non-board) branch of `RigChain` already calls them after the split, gated on Dual:

```cpp
// RigChain.h ~388–442 — the precedent to reuse
const bool stereoActive = mPremodStereo && (mMode == Dual)
                          && !mPremodPreDrive && !premod.isBypassed();
...
if (stereoActive) { premod.processStereo(vA, vB, numSamples); ... }   // L->Amp A, R->Amp B
if (predStereoActive) { predelay.processStereo(vA, vB, numSamples); ... } // ping-pong
```

The pedalboard pool simply never calls `processStereo`. **The engines can already do most of
what we need; the missing pieces are a routing state, the `process()` wiring, params, and UI.**

---

## Routing model

Add a per-slot boolean **Stereo** (new `pbS{i}Stereo` param) on top of the existing
`Lane` placement. The pedal's *mode* is derived from where it sits — no separate mode control:

| Placement | STEREO off | STEREO on |
|---|---|---|
| **Trunk** | mono, feeds both amps identically (today) | **Splitter**: mono `trunk` in → decorrelated `vA`/`vB` out. Becomes the split producer. |
| **Lane A / Lane B** | mono, that lane only (today) | **Bridge**: reads `vA` & `vB`, processes as a stereo pair, writes both. Spans both amps. |

Only `TypeMod` and `TypeDelay` slots expose the toggle; `TypeDrive`/`TypeOff` ignore it.

### Processing rework

Two changes to `PedalboardBlock::process()`:

1. **Unify the two lane loops into one index-ordered pass.** Today LaneA fully processes,
   then LaneB. Replace with a single loop over `i` that dispatches on `mLane[i]`: LaneA→`vA`,
   LaneB→`vB`, and a **Span/bridge** slot→`(vA, vB)` together. For pure A/B content this is
   **bit-exact** (the two lanes are independent buffers; cross-lane order is irrelevant) and it
   gives bridge pedals a deterministic position in the flow.

2. **Splitter boundary.** The split (`memcpy`, step 3) stays where it is *unless* a Trunk slot
   is a STEREO splitter, in which case that slot produces the split via `processStereo(trunk→vA, vB)`
   instead of the plain copy. A splitter is the trunk→lane boundary: Trunk slots before it are
   mono; slots after it must be Lane/Span. (Enforced in the UI + `commitArrangement`.)

### Mono / Solo collapse (bit-exact guarantee)

Stereo engages **only** when `mMode == Dual` and both amps run (`runA && runB`) — identical to
the existing `mPremodStereo && Dual` gate. In Solo or single-amp, a "stereo" pedal falls back to
its mono `process()` on the single active buffer, so those paths stay byte-for-byte as today.
STEREO off is likewise a pure passthrough of current behaviour.

---

## DSP + param hooks

- **New param:** `pbS{i}Stereo` (bool, default `false`). Grow the union `kU` 36→37 and add
  `"Stereo"` to the suffix array (`PedalboardPanel.h:273`). Wire in `PluginProcessor.cpp`
  (~1468–1507) via a new `bd.setSlotStereo(i, on)`.
- **PedalboardBlock:** add `bool mStereo[8]`, `setSlotStereo/slotStereo`, and the `process()`
  rework above. Reuse `PreModBlock::processStereo` / `PreDelayBlock::processStereo` for the
  **splitter** directly (mono-in/stereo-out is exactly right).
- **Bridge needs a true stereo-in method.** `processStereo` today is mono-in/stereo-out. For a
  bridge (stereo-in/stereo-out) either (a) sum `vA+vB`→mono then stereo-out as an interim (loses
  per-lane distinctness), or (b) add a real `processStereoInPlace(L, R, n)` to `PreModBlock`/
  `PreDelayBlock`, modelled on the true-stereo `ModBlock`/`DelayBlock` (`StereoBlock`s) already in
  the post-cab section. (b) is the correct end state; (a) is an acceptable Stage-2 stand-in.
- **`latencySamples()`** (line 136): a splitter counts on the trunk; a bridge/span pedal counts on
  *both* lanes (so it lands in the `max(a, b)` term). Update the accumulation.
- **`hasLaneRouting()`** (line 154): a stereo pedal also causes the pre-amp to split → return true
  when any active slot is stereo, so Solo-A fast paths still fill `vB` correctly.
- PDC "bypass doesn't change latency" contract is preserved.

---

## UI — vertical span

Two surfaces, both already partly ready:

- **CHAIN strip** (`PedalboardPanel.h:1367`) already has vertical lane bands: `laneAY()` (upper
  quarter), `trunkY()` (centre), `laneBY()` (lower three-quarter). A stereo node's rect spans from
  the A band to the B band, centred on the split — this reads correctly as "spans both amps" with
  almost no new geometry. Add the STEREO state to `drawNode`/`layoutNodes` and a right-click
  "Stereo: on/off" item next to the existing Route menu.
- **DECK** (`PedalboardPanel.h:989`) is a single fixed-height row (`layoutCells`, ~1271: one common
  `yTop`, `cellH = 366 * 0.85`). To span both amps here, render a stereo slot as a **double-height
  enclosure** anchored at the lane-A row and extending down over the lane-B position, with two
  output jacks/cables (top→Amp A, bottom→Amp B). Least-disruptive path: give stereo cells a taller
  `bounds` and a two-jack face; the row order is unchanged. Bigger alternative (parked): restructure
  the post-split DECK into stacked A-over-B rows so spanning is inherent.
- **Per-pedal toggle:** a STEREO switch on the `StompPedal`/`DrivePedal` face (mod & delay only)
  plus the context-menu item. Toggling on expands the pedal to the spanning render.

---

## Staged build plan

Each stage is independently buildable, testable, and commit-sized (per your commit-small
workflow). Every stage ends with a Windows build + the relevant `*_test` and, for UI stages,
`uishot`, plus an ear check in Dual mode.

- **Stage 0 — param + data model, no behaviour change.** Add `pbS{i}Stereo`, `mStereo[]`, setters,
  processor wiring. Default false. **Gate:** `pedalboard_test` still 16/16, bit-exact.
- **Stage 1 — Splitter DSP.** Unify the lane loops (bit-exact for A/B), add the Trunk-splitter
  boundary calling `processStereo(trunk→vA,vB)`. **Gate:** new `pedalboard_test` cases; Solo/mono
  bit-exact; Dual splitter decorrelates L/R.
- **Stage 2 — Bridge DSP.** Add the Span dispatch in the unified loop; interim mono-derive, then
  the true `processStereoInPlace` engine method. **Gate:** tests + ear (stereo chorus across amps).
- **Stage 3 — CHAIN UI.** Spanning node + STEREO toggle + right-click. **Gate:** `uishot`.
- **Stage 4 — DECK UI.** Double-height spanning enclosure + two output jacks + face toggle.
  **Gate:** `uishot`, viewport-host check.
- **Stage 5 — polish + preset migration.** Silkscreen/jack art; confirm old presets recall
  bit-exact (missing `Stereo` suffix → default false everywhere).

---

## Risks & open questions

- **Cross-lane ordering.** The unified loop is bit-exact for independent A/B content, but a bridge
  reads both lanes *at its index position* — verify this matches intent when lanes have different
  pre-bridge chains. (Recommend: a bridge acts on whatever `vA`/`vB` hold at that index.)
- **Bridge true-stereo engine work.** Front engines only do mono-in/stereo-out today; the interim
  mono-derive collapses per-lane character. Budget the `processStereoInPlace` method (Stage 2b).
- **Splitter position constraint.** A splitter must be the trunk→lane boundary; needs UI guardrails
  (can't place a Trunk slot after a splitter) and `commitArrangement` support.
- **Preset back-compat.** New suffix defaults false → old presets bit-exact. Confirm the repack in
  `commitArrangement` carries the new `Stereo` field.
- **PDC.** A stereo pedal counts in both lanes / the trunk depending on mode — get
  `latencySamples()` right or the amps misalign.
- **Board is ALWAYS-ON** and interacts with Solo gating — stereo must no-op cleanly in Solo.
- **Baseline is in flux.** The pedalboard DSP pool and the 2026-07-10 UI redesign are currently
  **UNCOMMITTED and UI-unverified (need a Windows build)**. Land/verify those first, or branch from
  them, before starting Stage 0 — otherwise this stacks unverified work on unverified work.

---

## One-line summary

The engines already do mono-in/stereo-out (`processStereo`); the work is a per-slot `Stereo` flag,
a unified post-split loop in `PedalboardBlock::process()` that dispatches Trunk-splitter and Span-
bridge slots, a true stereo-in engine method for bridges, and a spanning render in the CHAIN and
DECK — all gated to engage only in Dual so mono/Solo stay bit-exact.
