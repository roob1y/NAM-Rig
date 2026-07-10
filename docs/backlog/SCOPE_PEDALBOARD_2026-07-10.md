# SCOPE — PEDALBOARD (unified reorderable front-of-amp section)

Status: **PROPOSAL / awaiting greenlight** (2026-07-10). Big multi-stage feature.
Supersedes the "per-pedal drive routing" mini-feature (folded in here) and provides
the natural home for the new **Boost** pedal.

## 1. The idea

Today the mono front-of-amp section is a **fixed chain of separate blocks**:

```
gate -> env -> comp -> drive(3-slot rack) -> premod -> predelay --> [A/B split] --> amps
```

Each block is its own tile + panel, order is hardcoded, and routing to Amp A/B/Both
is a single rack-wide `driveSend` selector bolted on.

**Replace the ENV + COMP + DRIVE + PREMOD + PREDELAY blocks with ONE `PEDALBOARD`
block**: a reorderable board of pedal *modules* you drag in from a categorised
palette, laid out as a horizontal signal-flow graph — exactly the AmpliTube-style
view in Robbie's reference image (source -> DI -> splitter -> two amp lanes ->
mix -> out), **but for the pedals**.

The visual model *is* the routing model:

```
             (shared trunk = "Both")            Amp A lane
  IN --> [pedal]--[pedal]--[pedal] --> ( SPLIT ) ==[pedal]==[pedal]==> AMP A
                                              \\== [pedal] ===========> AMP B
                                                     (Amp B lane)
```

- A pedal on the **trunk** feeds **both** amps (default).
- A pedal on the **A lane** or **B lane** feeds only that amp.
- Left-to-right position within a segment = its processing order.
- Drag a pedal between trunk / A-lane / B-lane to change routing; drag within a
  segment to reorder.

Cables coloured per lane (amber trunk / amber-A / teal-B, matching the Mix panel
rig colours and the reference image's dual amber/orange cabling).

**GATE stays a separate pre-block** (it has lookahead latency + special
enable-without-bypass handling, and Robbie's list was ENV/COMP/DRIVE/PREMOD/PREDELAY).
Everything after the cab (mod/delay/reverb) is untouched.

## 2. Why this is the right shape (and bit-exact by construction)

The hard DSP constraint: every pedal engine holds internal state (filters, delay
lines, LFOs) and can be advanced **once per buffer**. The trunk+lanes model respects
that — each pedal is processed exactly once, in the segment it lives on — so it needs
**no duplicated block state** and no divergence bookkeeping. Processing:

```
trunk = input (post-gate)
for each pedal on TRUNK in order:  pedal.process(trunk)
vA = trunk;  vB = trunk                      # the split
for each pedal on A-LANE in order: pedal.process(vA)
for each pedal on B-LANE in order: pedal.process(vB)
# vA -> Amp A voice, vB -> Amp B voice (existing per-rig amp/eq/cab path)
```

**Default board = today's chain, bit-exact.** The migrated default places the
existing pedals on the trunk in the current order (env, comp, drive×N, premod,
predelay), all "Both". Processing once on the trunk then copying to vA/vB is
numerically identical to today's single-bus pre-split path. In Solo modes only the
relevant lane runs, so SoloA stays the byte-exact regression gate it is now.

This also cleanly subsumes the existing `*Pos` (Pre/Post-drive) switches and the
`premodStereo` / `predelayStereo` dual-amp modes: "stereo front-mod / ping-pong"
becomes "this pedal is on a lane" (or a per-pedal stereo option), expressed visually.

## 3. Module model (DSP)

Every existing engine already implements the uniform `MonoBlock` interface
(`prepare/reset/process/latencySamples/setBypassed`, `Blocks.h:32`). So:

- Define a **`Pedal`** = a MonoBlock engine + `{ PedalType type; Lane route; bool on; }`
  where `Lane ∈ {Trunk, A, B}`.
- **`PedalboardBlock`** owns an ordered `std::vector<Slot>` (fixed capacity, all
  engines pre-allocated in `prepare()` so the audio thread never allocates). Each
  slot carries one engine of each supported type (tagged by `type`) OR — leaner —
  a small pre-constructed pool. Processing walks the order three times filtered by
  lane, as in §2.
- **Module categories / types** (the palette), reusing the existing engines verbatim
  (zero voicing change in Stage 1):
  - **Dynamics**: Compressor (`CompBlock`)
  - **Filter**: Envelope Filter / auto-wah (`EnvFilterBlock`)
  - **Drive**: Boost / Overdrive / Distortion / Fuzz (`DriveBlock`, refactored to a
    *single-pedal* engine — see below)
  - **Modulation**: Chorus/Phaser/Flanger/Tremolo/Uni-Vibe (`PreModBlock`)
  - **Delay**: DD-7 / Carbon Copy / Memory Man (`PreDelayBlock`)
  - **Boost** (new, §6): clean level/solo boost
- **Drive refactor**: today `DriveBlock` is a 3-slot internal rack. In the board,
  each drive pedal is its own slot, so we need a **single-pedal drive engine**
  (extract the per-`Slot` processing already in `DriveBlock.h` into a standalone
  `Pedal`). The rack's 3 slots migrate to 3 board slots. All voicings/params/tests
  carry over unchanged; this is a mechanical extraction, not a re-voice.

Capacity: propose **8 slots** (covers 3 drives + comp + env + 2 mod/delay + boost
with headroom). Open decision (§8).

## 4. Params & preset migration (must stay bit-exact)

Preset facts (verified): `.namrig` is versioned JSON that snapshots **by paramID
name**, applies **defaults-first then overrides present keys** (`PresetManager.h:101`),
so **adding new params with identity defaults keeps every old preset and all 5
factory presets loading unchanged** (factory presets only ever set `comp*`, never
drv/premod/predelay/env — confirmed). Automation indices are stable as long as new
params are **appended last** (the established convention).

Strategy:

- **Keep every existing param ID and default** for the five engines (drv1*/drv2*/
  drv3*, comp*, envfilter*, premod*, predelay*). The board reads the same param
  values; nothing about the engines' params changes. This is what preserves
  bit-exactness and old presets.
- **Add, appended-last, per-slot board metadata**: for each of the N board slots a
  `pbSlotK` group — `pbSlotKType` (which engine/off), `pbSlotKRoute` (Trunk/A/B,
  default Trunk), and the **order** as a single permutation-style encoding (mirror
  the proven `modChainOrder` pattern: one value that saves/automates cleanly; default
  = identity = current order). Engine knob params stay where they are.
- **Deprecate but DON'T remove** `driveSend`, `premodPos`, `predelayPos`,
  `envfilterPos`, `premodStereo`, `predelayStereo` in Stage 1 (leaving the registered
  params in place keeps automation indices and old presets intact). Their *effect* is
  reproduced by slot routing/order; map old values -> board defaults on load so a v2
  preset that set e.g. `driveSend=Amp B` still routes correctly. Full removal is a
  later preset-version bump (v3) with a migration hook, not now.

Net: **default state and every existing preset are byte-identical; new capability is
purely additive.**

## 5. UI (Stage 2) — the node-chain board

Reference = Robbie's AmpliTube-style image: horizontal nodes, splitter, two amp
lanes, coloured cables. Build it in the existing design language (tokens from
`design_handoff_nam_rig_ui`: accent `#ffb13d`, panel `#1d2027`, Archivo/JetBrains
Mono, 7px pills; rig colours amber A / teal B).

Reusable infrastructure that already exists:
- **`ChainRack`** (`Panels.h:3864`) — self-contained mouse drag-to-reorder chip
  stack committing to a param. Generalise from the 3-slot permutation to an N-slot
  free order + lane assignment.
- **`DrivePickerOverlay`** (`Panels.h:1250`) — canvas-parented categorised cascade
  menu (category -> two-line model list). This *is* the "categorised module palette";
  extend its categories from drive-only to all pedal types.
- **DnD**: AmpBrowser/IrBrowser already use `DragAndDropContainer` +
  `getDragSourceDescription` payloads + `DragAndDropTarget::itemDropped`
  (`AmpBrowser.h:83`, `SortableFileList.h:146`). Use the same pattern to drag a
  module from the palette onto a board lane.

The board replaces DRIVE/ENV/COMP/PREMOD/PREDLY tiles in `BlockStrip` with a single
**PEDALBOARD** tile whose panel is the graph. `BlockStrip`'s hardcoded `slots[]` +
the editor's parallel `mPanels[]` (index-locked, `BlockStrip.h:216`) shrink by four
tiles — the branch/LED index wiring (`mTiles[6/8/10]`) must be updated in lockstep.

Per-pedal editing: click a node to open its existing panel (Drive/Comp/etc. panels
are reused as the node's detail view).

## 6. Boost pedal (feature #2, folded in)

A dedicated **clean Boost** module, following the existing block-On pattern
(drives a bypass LED, automatable):

- `boostOn` (bool, default false — inert until used), `boostDb` (0..+12 dB, default 0),
  `boostPos` (choice **Front / End**, default Front) using the same Position-choice
  pattern as `premodPos`/`predelayPos`.
- **Front** = a Boost *module on the trunk* (or a chosen lane) upstream of the amp —
  pushes the NAM capture into more breakup. Must interact correctly with the amp
  calibration: place it **after** the global input-cal / before `inTrim`, so it adds
  drive on top of the calibrated reference (same tap logic the drive pedals rely on),
  not fighting the −24 LUFS normalize (which sits post-cab at `mOutTrim`). Net effect:
  more breakup, output level held by normalize/Level as designed (fits
  `no-dynamic-autolevel` — this is a static, user-set boost, not gain-riding).
- **End** = a post-chain clean gain lift on the final stereo bus (after reverb), for
  cutting through a mix — a simple scalar, PDC-neutral.
- 0 dB / off = bit-exact no-op.

Boost can ship **before** the full board (it's small and independent) or as the first
new module type — see staging.

## 7. Staged build plan

- **Stage 0** — this doc + decisions (§8). *(here)*
- **Stage 1 — DSP core, headless + tested.** `PedalboardBlock` container; extract
  single-pedal drive engine; port comp/env/premod/predelay as modules; wire params so
  the **default board reproduces the current chain bit-exact**; keep old param IDs.
  New `tests/pedalboard_test.cpp` proving: (a) default order all-Both == current
  pre-split chain byte-for-byte, (b) reorder correctness, (c) Trunk/A/B routing truth
  table (SoloA/B/Dual), (d) latency sum unchanged. No UI yet (temporary: existing
  panels keep driving the same param IDs). Ship-safe: plugin still builds & sounds
  identical.
- **Stage 2 — UI.** The node-chain board (palette + drag-add + drag-reorder + lane
  routing + cables + per-node panels). Restyle to the reference image + tokens.
  Update `BlockStrip`/editor index wiring.
- **Stage 3 — Boost + polish.** Boost module (Front/End), retire the deprecated
  `*Pos`/`driveSend`/`*Stereo` params behind a preset v3 migration, factory presets
  showcasing routing (e.g. "drive into Amp A only, clean Amp B").

Recommended: **do Stage 1 first** (the risky part is the DSP/preset invariants; get
them proven offline before any UI). Boost (§6, Front/End) can be slotted into Stage 1
as the first new module if Robbie wants an early win.

## 8. Open decisions

1. **Slot capacity** — 8 proposed. Enough? (More = more params appended.)
2. **Which blocks join the board** — confirmed ENV/COMP/DRIVE/PREMOD/PREDELAY; GATE
   stays separate. Should the **post-cab MOD/DELAY** ever be draggable too, or is the
   board strictly the mono front-of-amp section? (Recommend: front-only for v1.)
3. **Order encoding** — single permutation-style value per the `modChainOrder`
   precedent, vs per-slot position params. (Recommend: single encoded value for clean
   automation, as the existing code comment advises.)
4. **Boost timing** — ship Boost inside Stage 1 (early), or defer to Stage 3?
5. **Per-pedal stereo** — keep the current premod stereo-spread / predelay ping-pong
   as a per-pedal option on the board, or drop them in favour of pure lane routing?
6. **Greenlight to start Stage 1 now?**

## 9. Risks

- **Bit-exact regression**: the whole value prop rests on default == today. Mitigated
  by the offline `pedalboard_test` byte-compare gate before anything else.
- **Drive extraction**: turning the 3-slot rack into single-pedal instances must not
  perturb voicings (shared biquad/IoStage state per slot). Mechanical, but drive_test
  must stay green.
- **UI index wiring**: `BlockStrip.slots[]` and editor `mPanels[]` are index-locked;
  collapsing 5 tiles into 1 touches branch/LED/selection wiring — do it carefully.
- **Scope**: this is multi-session. Stage boundaries are ship-safe checkpoints.
