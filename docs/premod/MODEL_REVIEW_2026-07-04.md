# Pre-amp modulation review — all 5 models, 2026-07-04

Critical pass over the mono front-of-amp modulation section, same treatment the
drive models got in `docs/drive/MODEL_REVIEW_2026-07-04.md`: code
(`src/rig/PreModBlock.h` + its wiring in `PluginProcessor.cpp` and `Panels.h`)
treated as **truth**, the handoff doc and tests cross-checked against it, then
each model checked against the **real pedal's circuit** (primary schematics,
re-verified 2026-07-04 — not taken from the handoff on faith). Nothing was
assumed correct. Findings are ranked per model; systemic rot is listed once,
first.

> **Note on memory:** the auto-memory said this section was a *"NEXT-CHAT
> objective … not started."* It is fully built and voiced. Code is truth; the
> memory was stale. (Same lesson the drive review recorded.)

Current inventory (verified, `PreModBlock::Type` enum PreModBlock.h:47 +
`ioFor()` :160 + the processor pin block PluginProcessor.cpp:1130-1148):
**Chorus 0** (Boss CE-2) · **Phaser 1** (MXR Phase 90) · **Flanger 2** (MXR
EVH117 = M117R) · **Tremolo 3** (Boss TR-2) · **Uni-Vibe 4** (Shin-ei). Type
range 0..4 (AudioParameterChoice, 5 entries) fits; the panel shows a per-type
control subset and pins the hidden knobs in the processor.

**Chain position (satisfies the "PRE-amp mod" premise):** the block lives in the
mono shared pre-section, default **After Drive** and before the A/B split
(`premodPos`, RigChain.h:258/270), switchable to **Before Drive**. So its output
is coloured by the amp's nonlinearity downstream — the whole point, and the thing
that distinguishes it from the post-cab stereo `ModBlock`. This interaction is
**structural** (guaranteed by RigChain), not exercised by any test — the unit
tests process the block in isolation. That's consistent with how every other
block is tested; noted, not a defect.

---

## Systemic: the scaffold-era rot (one root cause)

The section was built chorus-first: chorus voiced, the other four left as
transparent stubs, then all five filled in. As with the drives, the voicing
caught up but several **banners and header comments froze at the stub era**. None
of this affects sound; all of it misleads the next session.

**The stalest artifact — the test banner describes a plugin that no longer
exists** (`tests/premod_test.cpp:1-12`). The header docblock documents only
**T1–T7** and describes T4 as:

> "un-voiced types (Phaser/Flanger/Tremolo/Uni-Vibe) are exact passthrough
> (scaffold stubs -> transparent, never silent)"

That is the **opposite** of what the code's T4 now asserts (`premod_test.cpp:152`
checks every type is finite **and non-silent**, `maxAbs > 0.02`) and false about
the engine (all five are voiced). T8–T24 — 17 of the file's checks, including all
of the phaser/flanger/tremolo/uni-vibe and IoStage coverage — are **undocumented
in the banner**. This is exactly the drive review's "stale test banner" pattern.

**Duplicate / skipped test IDs** (same file). The suite's convention is
"one `T<n>` = one scenario, which may carry several `check()` lines" (T1 has 3,
T8 has 3, T11 has 3). Two blocks break it: the tremolo block labels its three
checks `T15, T15, T16` (a stray bump), the depth-0 block is `T17`, and **T18 is
skipped entirely**; the uni-vibe block is fine (`T19,T19 / T20 / T21`, shared-ID
like T1). Net effect: a reader counting coverage is misled, and it mirrors the
drive banner-count rot. The IDs are decorative (nothing indexes on them), so the
fix is cosmetic — but it should be contiguous. Keeping the IoStage checks at
**T22/T23/T24** (the handoff doc references those numbers) forces the tidy
renumber: tremolo → `T15 finite / T16 cut-only / T17 modulates / T18 depth-0`,
uni-vibe unchanged, IoStage unchanged.

**Stale "scaffold / voicing soon" header comments** (each wrong about the CURRENT
code):

| Where | Says | Truth |
|---|---|---|
| `Panels.h:6173-6177` (PremodPanel) | "Scaffold UI … Chorus is voiced; the other types are transparent stubs (a 'voicing soon' note shows) … Feedback … hidden until the phaser/flanger are voiced." | All five voiced; the panel now shows a per-type control subset (Rate always; Depth all but phaser; Mix uni-vibe only; Feedback flanger only; Wave tremolo only) and pins the rest in the processor. No "voicing soon" note exists. |
| `PluginProcessor.cpp:226` | "Scaffold: Chorus voiced; other types voiced later." | All five voiced. |
| `PreModBlock.h:30-33` STATE block | "all five are voiced …" | **Correct** — this one kept up. |

No wrong-index test bug (the drive **T38** kind) and **no tautological A/B check**
(the drive **T15** kind) exist here — the default type is chorus and no test
compares "model 0 vs the default" as if they were different. On the
correctness-of-tests axis the premod suite is in better shape than the drive
suite was; the only test rot is the banner and the numbering above.

---

## Where the real voicing actually lives: the processor pin table

The knob **defaults** in `PreModBlock.h` are *not* the shipped voice. The
fool-proof panel hides most knobs and the processor pins them per-type
(`PluginProcessor.cpp:1139-1146`). Anyone auditing `PreModBlock.h` alone will draw
wrong conclusions (e.g. "the phaser has no feedback" — the member default is 0,
but the shipped phaser runs 0.35). The shipped voice:

| Type | Rate | Depth | Mix | Feedback | Manual | Wave |
|---|---|---|---|---|---|---|
| Chorus | knob | knob | 0.5 (fixed 50/50) | 0 | — | — |
| Phaser | knob | **pinned 0.60** | 0.5 (DSP hardwires 50/50) | **pinned 0.35** | — | — |
| Flanger | knob | knob (Width) | pinned 1.0 → DSP halves → 50/50 | knob (Regen) | pinned 0.15 | — |
| Tremolo | knob | knob | — | 0 | — | knob |
| Uni-Vibe | knob | knob | knob (Chorus↔Vibrato) | 0 (stock) | — | — |

Two of these pins are the most interesting **sonic** findings below (phaser
feedback; flanger Manual). Recommend a one-line pointer comment in
`PreModBlock.h` (near `setFeedback`/`setDepth`) that "the shipped per-type values
are pinned in PluginProcessor.cpp" so the trap closes.

---

## Chorus 0 — Boss CE-2

Voicing (PreModBlock.h:49-63, `processSample` case kChorus:295): single BBD voice
(`kVoices 1`) at the ~9.5 ms delay centre (`kDelayMin/Max 8/11`), triangle LFO
`≤ kChorusMaxRateHz 3.5`, `kModDepthMs 1.4`, fixed 50/50 mix, BBD 3rd-order
polynomial colour (`x − x²/8 − x³/18`) via exact 1st-order ADAA, dark 6.6 kHz
2-pole reconstruction + 40 Hz subsonic trim. Io: buffered-ish, ~407 kΩ gentle
damp (−1 dB @ 2800). **Verified against the circuit:** MN3007 1024-stage BBD,
delay ~5–40 ms usable / ~9.6 ms centre; **Rate *and* Depth are both real CE-2
knobs**; wet/dry is fixed 50/50 (R21=R22=R24=47k); 3rd-order Sallen-Key
reconstruction ~6.6 kHz. The model is faithful.

1. **`kModDepthMs 1.4` overshoots the real max sweep (~±1.1 ms).** At Depth 1 the
   tap swings ±1.4 ms, wider/warblier than a real CE-2 at full Depth — the comment
   itself records the ±1.1 figure (:57). It's a knob, so a player can back off, but
   if authenticity to the pedal's *endpoint* is the standard, cap at ~1.1 (or note
   the overshoot as a deliberate "a bit more than stock" decision). **Low.**
2. Triangle LFO is a pure symmetric triangle; the real CE-2's LFO is a slightly
   rounded/asymmetric triangle (TL022 integrator). Inaudible at chorus depths —
   **cosmetic**, only worth a line in the doc.
3. Otherwise clean. The BBD polynomial being **level-independent** (a fixed colour,
   not a level-driven clipper) is the right call and is stated well in the header.

## Phaser 1 — MXR Phase 90

Voicing (PreModBlock.h:65-80, case kPhaser:314): 4 equal ZDF/TPT all-pass stages,
rest corner `kPhaserRestHz 141` (→ notches 58.4/340.4 Hz), swept **upward only**
`0.5+0.5·lfo` by `kPhaserOctaves 3.9 · Depth`, closed-form negative feedback
(`kPhaserFbMax 0.70`), hardwired 50/50 dry/phased. **Verified against the
circuit:** 4 stages, R 24k / C 47n, at-rest notches **58.5 & 340.8 Hz** (the
5.83× topology ratio comes out automatically), Speed-only (fixed depth), JFET
raises the corner from rest → upward sweep. The retune to the real notches is
**correct and is the good work in this model**. Two shipped-voice issues:

1. **As shipped the phaser is the *feedback* (block-logo / "Color") voice, not the
   Script the handoff describes.** `PluginProcessor.cpp:1143` pins phaser feedback
   to **0.35** (⇒ loop gain `0.70·0.35 ≈ 0.245`), while the member default and the
   handoff narrative ("Script Phase 90 … smooth … Block adds regen via R28") both
   say **zero**. So the default swirl has a resonant throb the classic script
   Phase 90 doesn't. This is a legitimate voice — but it's **undocumented and
   contradicts both the code default and the doc**. Decision needed: either pin it
   to **0** (ship the script) or keep 0.35 and **write down** "we ship the
   block-logo voice." Audible; **highest-value sonic item in this model.**
2. **The sweep span is a derived guess, not a measured endpoint.** Shipped span =
   `0.60 · 3.9 ≈ 2.34 octaves` upward from 141 Hz (upper notch travels ~340 Hz →
   ~1.7 kHz). The rest corner is verified; the *top* of the sweep isn't pinned to a
   measured Phase 90 value (Eichas DAFx-14 / Kiiski DAFx-16 have the JFET rDS
   range). Worth confirming the upper-notch endpoint against one of those and
   pinning `premodDepth` (phaser) to match. **Medium, ear.**
3. **"Phase 90 / Small Stone family" (:66) lumps two different circuits.** The
   retune is specifically the Phase 90 (4 equal JFET all-passes). The Small Stone
   is a 4-stage **OTA** phaser with a Color-switch feedback and a different
   response — it is *not* the same "equal-stage" animal. Drop the Small-Stone
   co-claim (or mark the model "Phase-90-primary; Small-Stone-ish only with
   feedback up"). **Doc.**

## Flanger 2 — MXR EVH117 (= M117R)

Voicing (PreModBlock.h:82-99, case kFlanger:362): one short swept tap, Manual base
delay (`kFlManualMin/Max 0.5/8`), `kFlSweepMs 6` upward, clamp `kFlMaxMs 12.8`,
tone-shaped regen (`kFlFbMax 0.78`, one-pole LP + tanh self-limit), brighter wet
ceiling `kFlWetLpHz 8200`, Mix spans dry→50/50. Io: buffered ~470k. **Verified
against the circuit:** EVH117 and M117R are the **same PCB/engine** — the EVH117's
only extra is a momentary preset button (fixed resistors for Eddie's setting), not
a separate algorithm; controls are **Manual / Width / Speed / Regen**;
Reticon SAD1024 (dual 512 = 1024 stages). The **12.8 ms** ceiling is coherent with
1024 stages at the ~40 kHz minimum clock (`1024/(2·40 k) = 12.8 ms`) — so the
figure holds up on the BBD math as well as the manual. Mapping (Manual/Width=Depth/
Speed=Rate/Regen=Feedback) is faithful.

1. **The Manual knob is pinned and hidden** (`0.15` → base ≈ 1.63 ms;
   `PluginProcessor.cpp:1139`, `Panels.h:6237`). On the real pedal Manual is a
   front-panel control and a *large* part of its range (thin jet ↔ deep chorussy
   sweep). The fool-proof UI trades that away for a fixed classic-jet base. Sensible
   default, but **document it as a deliberate deviation** (and note the
   "Unchained/EVH preset" idea from the handoff as the natural place to expose the
   long end). **Low–medium, doc + optional feature.**
2. The brighter wet ceiling (8200 vs the CE-2's 6600) so the upper comb notches
   stay audible is well reasoned; no change. **OK.**

## Tremolo 3 — Boss TR-2

Voicing (PreModBlock.h:101-108, case kTremolo:389): VCA amplitude modulation,
Wave morphs triangle→trapezoid (`kTremWaveK 8` + clamp), **cut-only** gain
(`[1−depth, 1]`, peak unity), de-click slew (`kTremSlewMs 1.5`), clean AM (no EQ).
Io: transparent 1 MΩ FET input. **Verified against the circuit:** **M5207L01**
linear VCA, **1 MΩ FET input**, clean AM with no EQ colour, and the cut-only law
that produces the TR-2's famous perceived **volume drop** as Depth rises — the
model reproduces all of it, including the drop. The real Wave knob morphs from a
(slightly off-centre) triangle toward a squarer pulse; the trapezoid morph is a
faithful, click-safe stand-in.

1. No structural gap. Only nuance: the trapezoid top could go a touch squarer than
   `kTremWaveK 8` allows if you want the choppiest TR-2 setting — **ear, optional.**
2. Cut-only means the effect gets quieter with Depth (authentic). If a player reads
   that as "broken," a **static, one-shot makeup** (not reactive — see
   [[no-dynamic-autolevel]]) keyed to Depth would restore level without pumping.
   Deliberately **omitted** to stay faithful; record the choice.

## Uni-Vibe 4 — Shin-ei

Voicing (PreModBlock.h:110-124, case kUniVibe:402): 4 **staggered** ZDF/TPT
all-pass stages, ratios `kUniMult {0.616, 0.042, 19.6, 1.97}`, geometric centre
430 Hz, sine LFO through an **asymmetric lamp** (`heat 12 ms / cool 110 ms`) + LDR
power-law (`kUniGamma 1.5`), subtle AM (`kUniAmDepth 0.08`), closed-form positive
feedback (`kUniFbMax 0.5`, stock 0), Mix = Chorus↔Vibrato. Io: **69 kΩ**
treble-suck load (−3 dB @ 2800, −0.5 dB level). **Verified against the circuit:**
staggered caps **15 n / 220 n / 470 p / 4.7 n** → uneven non-harmonic notches (the
"double beat" / Leslie Doppler); `kUniMult` **derives exactly** from `1/C`
normalised by their geometric mean (re-checked: 0.616/0.042/19.6/1.97 ✓); one lamp
+ 4 LDRs, heats fast / cools slow; **stock has no feedback** (a mod). This is the
most rigorously authentic model in the section.

1. **`kUniAmDepth 0.08` may be under-cooked.** The real 'vibe's amplitude throb
   (from the non-ideal shelved opto stages) is a defining part of the "vibe" vs a
   plain phaser; the DAFx-19 grey-box measured a more pronounced AM. 8 % is safe but
   possibly timid — **ear-test 0.10–0.15.** **Low–medium.**
2. The 69 kΩ front-end darkening is a real, audible part of the sound and is now
   modelled — good. No structural gap.

---

## Priorities

**P1 — correctness & anti-footgun (zero sonic risk) — APPLY NOW:** rewrite the
`premod_test.cpp` banner to cover T1–T24 and fix the T4 line; renumber the
tremolo checks (T15/T16/T17/T18) so IDs are contiguous with the IoStage checks
left at T22/T23/T24; correct the two "scaffold / voicing soon" header comments
(`Panels.h:6173-6177`, `PluginProcessor.cpp:226`); add the one-line pointer in
`PreModBlock.h` that the shipped per-type values are pinned in the processor.

**P2 — sound, ranked by expected audibility:** phaser feedback pin 0.35 vs the
script's 0 (decide + document) → phaser sweep-span endpoint verified/pinned →
Uni-Vibe AM depth 0.08→0.10–0.15 → CE-2 `kModDepthMs` 1.4 vs ±1.1 → tremolo
squarer top. All go through the established loop: offline `premod_test` first,
then Windows build + ear test.

**P3 — doc the deliberate deviations** so they survive fresh sessions: flanger
Manual pinned/hidden (and the Unchained-preset idea); tremolo cut-only volume
drop kept on purpose; "Phase 90 / Small Stone" is Phase-90-primary; CE-2 Depth
overshoot.

The P1 items are safe and should be applied in this pass. **Every P2/P3 change is
ear-gated and left for Robbie** — this review changes no voicing.
