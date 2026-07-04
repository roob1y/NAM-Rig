# PreMod — circuit-authenticity pass (5 real pedals)

**Date:** 2026-07-04 · **Branch:** Plugin-Assortment-Fixes · **Status:** UNCOMMITTED, offline-tested, **needs Windows build + Robbie play-test**

Robbie's ask: make the pre-amp (front-of-amp, mono) modulation section authentic to real
pedals **from the circuitry — input/output stages, impedance, etc.** Targets:

| Slot | Real pedal | Core status | New this pass |
|---|---|---|---|
| Chorus | **Boss CE-2** | already voiced, matches | + input-stage loading (407k) |
| Phaser | **MXR Phase 90** | retuned to real notches | + input stage; **rest-corner sweep fix** |
| Flanger | **MXR EVH117** (= M117R) | already M117-family = correct | + input stage; max delay 12.8ms; brighter wet |
| Tremolo | **Boss TR-2** | already voiced, matches | + transparent 1M FET input |
| Uni-Vibe | **Shin-ei** | staggered stages already exact | + **69k treble-suck load**; slower lamp cool |

Only files touched: `src/rig/PreModBlock.h`, `tests/premod_test.cpp`. All core DSP is
JUCE-free; **premod_test 34 checks (T1–T24) PASS offline.**

---

## The headline change: authentic input/output stages (IoStage)

Before this pass the `PreModBlock` had **no** impedance/coupling model — unlike the drives,
which load the guitar via `src/rig/IoStage.h`. Now each pedal carries an `IoStage` (member
`mIo`), configured per-type from a `ioFor(Type)` anchor table, called `processIn` before the
effect and `processOut` after — exactly mirroring `DriveBlock`.

Same premise as the drives ([[capture-interface-hiz-1meg]]): the guitar is DI'd at ~1 MΩ, so
we model only the **delta** each pedal's real input impedance adds. A low Zin damps the
pickup's resonant peak (~2.7–3 kHz) and drops a little level; a buffered/high-Z input is just
its DC-blocking coupling high-passes.

Verified input impedances (research 2026-07-04, primary schematics):

| Pedal | Zin | IoStage anchors `{inHp, shelfHz, shelfDb, inLvl, inLp, outHp, outLvl}` | Character |
|---|---|---|---|
| CE-2 | **~407 kΩ** (R2 470k) | `{8, 2800, -1.0, -0.2, 0, 15, 0}` | gentle damp (like TS) |
| Phase 90 | **~470 kΩ**, C5 10n | `{33, 3000, -1.0, -0.2, 0, 22, 0}` | gentle damp, ~33Hz coupling |
| EVH117 | **470 kΩ** (spec), 1k out | `{8, 3000, -1.0, -0.2, 0, 8, 0}` | gentle damp |
| TR-2 | **1 MΩ** FET input | `{6, 0, 0, 0, 0, 7, 0}` | transparent |
| Uni-Vibe | **69 kΩ** (22k + 47k) | `{45, 2800, -3.0, -0.5, 0, 12, 0}` | **strong treble-suck** |

The Uni-Vibe's 69 kΩ is the big one — geofex: *"low enough to cause significant treble loss
to single-coil pickups."* That murky, dark front-end is a real part of the sound, now modelled.

---

## Per-pedal circuit findings & what changed

### Boss CE-2 chorus — core already correct
Source: ElectroSmash CE-2 analysis; Electric Druid BBD study; Anasounds teardown.
- MN3007, 1024 stages, delay centre **~9.6 ms** (8.5–10.75 ms) — current `kDelayMin/Max 8/11`
  (mean 9.5 ms) ✓. Triangle LFO 0.3–**3.5 Hz** ✓. **Fixed 50/50** mix (R21=R22=R24=47k) ✓.
  3rd-order Sallen-Key reconstruction **~6.6 kHz** ✓. Pre/de-emphasis is for BBD hiss (net-flat
  on signal), so the audible top is the 6.6 kHz LP — the current dark-wet model is faithful.
- **Change:** only the input loading (407k → −1 dB damp). Core untouched.

### MXR Phase 90 — retuned to the real notches (the one core change to listen to)
Source: ElectroSmash Phase 90; Cytomic; Eichas DAFx-14; Kiiski DAFx-16.
- 4 **equal** all-pass stages (R 24k, C **47n**), at-rest notches **58.5 Hz + 340.8 Hz**, which
  the JFET sweeps **upward** (it can only lower rDS → raise the corner). Fixed depth (Speed-only).
  50/50 fixed (R8=R16=150k). Block adds regen via R28=24k.
- **Was:** swept symmetrically about a 500 Hz centre ±2.2 oct — a real mismatch.
- **Now:** `kPhaserRestHz = 141` (⇒ notches 58.4/340.4 Hz at rest, verified), swept **upward**
  `kPhaserOctaves = 3.9` (unipolar `0.5+0.5·lfo`). The two-notch ratio is topology-fixed at
  5.83× and comes out right automatically.
- ⚠️ **This is the change most likely to want your ear.** You approved the old phaser
  ("sounds great!"); this makes the swirl sit lower/more authentic. If it regresses, revert by
  setting `kPhaserRestHz` back to a centre model — it's a 2-line change.

### MXR EVH117 flanger — it *is* the M117R
Source: Dunlop M117R/EVH117 manuals; falseelectronics teardown; geofex SAD1024.
- **Key fact:** EVH117 and M117R are the **same PCB and same flanger engine**. The only
  difference is the momentary **"EVH/Unchained" button**, which swaps four pots for fixed
  resistors (a hardware preset: high regen, deep width, slow sweep). Not a separate algorithm.
- Buffered ~470k in, 1k out. **Max delay 12.8 ms** (both manuals), rate **0.1–10 Hz**, triangle
  LFO, regen loop around the delay, ~50/50 fixed, fast-clock BBD (brighter than a chorus).
- **Changes:** renamed to EVH117; `kFlMaxMs 14 → 12.8`; **flanger gets its own brighter wet
  ceiling** `kFlWetLpHz = 8200` (vs the CE-2's dark 6600) so the comb's upper notches — the
  "jet" — stay audible. Regen/width/rate ranges already matched.
- **Optional future:** an "Unchained" preset button that snaps Rate/Depth/Regen to the EVH
  values. Not built (the foolproof panel already exposes Rate/Depth/Regen). Say the word.

### Boss TR-2 tremolo — core already correct
Source: hobby-hour TR-2; experimentalistsanonymous schematic; tremolo-project scope.
- **M5207L01 linear VCA**, clean AM, **no EQ colour**. WAVE morphs triangle→slewed trapezoid
  (fixed RC slew, rate-independent = anti-tick) ✓. **Cut-only** gain (peak unity, dips by depth
  → the famous perceived volume drop) ✓. Rate ~1–11 Hz. **1 MΩ FET input** (transparent).
- **Change:** only the transparent input/output coupling. Core untouched. (Note: research
  confirms the buffer is a **JFET** 2SK118Y, not a bipolar — matches our transparent 1M model.)

### Shin-ei Uni-Vibe — staggering already exact, lamp made lazier
Source: geofex "Technology of the Univibe"; Darabundit et al. DAFx-19 grey-box.
- 4 **staggered** all-pass caps **15n / 220n / 470p / 4.7n** → uneven non-harmonic notches (the
  "double beat"). Our `kUniMult{0.616, 0.042, 19.6, 1.97}` derives **exactly** from those caps
  (verified: `1/C` normalised by geometric mean). One lamp + 4 LDRs, **heats fast / cools slow**
  (asymmetric throb). Rounded sine LFO 0–7.6 Hz. Chorus = dry+wet (2×100k); Vibrato = wet only.
  **Stock has NO feedback** (a mod only) — our default is 0 ✓. Real AM throb from the non-ideal
  shelved stages — approximated by `kUniAmDepth`.
- **Changes:** lamp `heat 8→12 ms`, `cool 55→110 ms` (DAFx says fall is *several ×* the rise —
  more lopsided, more authentic throb); rate cap `8→7.6 Hz`.

---

## Tests (offline, `tests/premod_test.cpp`) — 34 checks, ALL PASS
- Existing T1–T21 still pass; **T2** and **T17** updated: "transparent" now means *settles to
  the IoStage-processed dry* (the input stage always colours, like the real pedal), and **T15**
  cut-only is checked against what enters the VCA (post input-stage).
- New **T22** Uni-Vibe 69k loads down the 3 kHz vs 200 kHz probe; **T23** TR-2 1M input is flat;
  **T24** anchor classification (Uni-Vibe loaded / TR-2 buffered).

Offline recipe (no JUCE): `git show HEAD` the headers into `/tmp` + `IoStage.h` + `Biquad.h`,
stub `juce_audio_basics`, `g++ -std=c++17 -O2 -I rig -I stub premod_test.cpp -o t && ./t`.

## To finish (Windows)
1. Build + run `build-clang\premod_test_artefacts\Release\premod_test.exe` → expect ALL PASS.
2. Build the plugin, **play-test all 5** (esp. the **Phase 90** — the retuned sweep — and the
   **Uni-Vibe** — the new 69k murk; also confirm the **EVH117** flanger reads bright/jetty).
3. If all good, **commit** (small, per [[commit-often]]). Nothing else was touched.
