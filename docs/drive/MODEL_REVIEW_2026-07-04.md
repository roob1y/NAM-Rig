# Drive model review — all 8 models, 2026-07-04

Critical pass over every drive model: code (`src/rig/DriveBlock.h`) treated as
truth, docs and tests cross-checked against it, then each model checked against
the real pedal's circuit. Nothing was assumed correct. Findings are ranked per
model; systemic rot is listed once, first.

Current inventory (verified, `modelsFor()` DriveBlock.h:344-351):
**Boost 2** (Range '65, Plex Boost) · **Overdrive 4** (Green Drive, Super Drive,
Gold Horse, Breaker Drive) · **Distortion 1** (Black Rodent) · **Fuzz 2**
(Round Fuzz, Violet Ram). `bModel` range 0..3 (PluginProcessor.cpp:159) fits;
UI clamps per category (Panels.h:1594).

---

## Systemic: the index-rot epidemic (one root cause)

The v1 stand-in models were deleted and the "II" reworks slid down to fill the
holes — but most comments, doc headers, and test banners still use the OLD
indices/names. None of this affects sound; all of it misleads the next session.

**The one real test bug — T38 never tests what it claims**
(`tests/drive_test.cpp:694`): `auto m1 = realSlotM(Kind::Overdrive, 0, ...)` —
loads model **0** a second time. The CHECK "T38 OD model 1 (Green Drive II)
still renders cleanly" (696) therefore re-verifies model 0 and has never
exercised model 1 (Super Drive). Fix: index 0 → 1, rename the check.

**T15's A/B check is a tautology** (`drive_test.cpp:344-348`): compares
Distortion model 0 against the default model — which IS model 0. It cannot
fail; the legacy model it was written to A/B against is gone. Same pattern in
T11/T21/T30 (they at least still pin default==0 and count). Repurpose T15's
first CHECK to pin the shipped voicing (assert the actual field values) so a
future accidental edit trips it.

**Stale state descriptions (each wrong about the CURRENT code):**

| Where | Says | Truth |
|---|---|---|
| NEXT_SESSION_HANDOFF.md:222-228 | Boost 4 / OD 5 / Dist 2 / Fuzz 3, "Boost full at 0..3" | 2 / 4 / 1 / 2 |
| current-driveblock.md:9-10 | OD 1 model, Fuzz 1 model | 4 / 2 |
| proco-rat.md:1-6 | "model 1", legacy model 0 kept byte-exact | model 0; legacy gone |
| big-muff.md:172 | "Fuzz holds 3 / Distortion back to 2" | 2 / 1 |
| sd1.md:1 | "Overdrive model 2" | model 1 |
| klon.md:100,104 | "model 3" | model 2 |
| bluesbreaker.md:134+ | "model 4" | model 3 |
| rangemaster.md:92, ep3.md:84 | "3 models" / "4 models" | 2 |
| drive_test.cpp:131,288,339,682 | "Black Rodent II model 1", "Green Drive II model 1", "Super Drive model 2" | wrong names/indices |
| drive_test.cpp:289,341,428,496,684,798 | banner counts (3/2/3/4/3/…) | see CHECKs beside them |
| drive_test.cpp:808,1023 | "model 3 (Gold Horse)", "model 4 (Breaker)" | 2 / 3 (code indices are right) |
| DriveBlock.h:246,261,278 | "model 2/3/4" comments on od[1]/od[2]/od[3] | off by one |

NEXT_SESSION_HANDOFF.md is the highest-priority fix: it's the designated
read-first doc for fresh drive sessions, and its inventory table (247-266)
describes a world that no longer exists.

---

## Boost 0 — Range '65 (Dallas Rangemaster)

Voicing (DriveBlock.h:227): clip 0 tanh, gMin 4→gMax 80, HP 2653 (input cap,
Range switch remaps 2653/1326/282 via applyRange), bias 0.30, outTrim 0.50,
static. Io: loaded, −4 dB @ 2800, inHp 0 so the cap HP isn't double-counted
(ioFor:393) — good.

1. **Weakest anti-aliasing on the brightest model.** clip 0 runs the legacy
   1st-order-ADAA branch (DriveBlock.h:704-716). This model high-passes at
   2.65 kHz *before* clipping at up to gain 80 — its clipped energy lives
   almost entirely in the top octaves where fold-back is worst, yet it's the
   only distorting model family (with Plex) still on 1st-order. The RAT
   measurements in proco-rat.md showed 1st→2nd order worth ~10-14 dB. Fix:
   an ADAA2 tanh needs dilog, so either refit to the cubic (clip 3, ADAA2 free)
   or add a per-model 2× oversample. Rangemaster into a cranked amp is exactly
   the use case where this will be heard.
2. **No touch response.** clean/dyn = 0. A germanium booster driven hard
   compresses and cleans up with picking; the engine's dynDepth is sitting
   right there, unused. Cheap experiment: dynDepth ~0.3.
3. **Asymmetry shape.** bias 0.30 into tanh gives smooth even-harmonic warmth;
   the real cold-biased OC44 also *cuts off* on one side when slammed (closer
   to the Round Fuzz kn behaviour than to offset-tanh). Only matters at max
   boost into hot pickups; ear-test before bothering.

## Boost 1 — Plex Boost (EP-3 / Xotic EP Booster)

Voicing (DriveBlock.h:240): clip 0 tanh, gMin 1.3→gMax 6, HP 15, +4 dB @ 5k
Q 0.35 post, bias 0.10, outTrim 0.74. Io: buffered, transparent (~2.2 M) — right.

1. **Max gain ~15.6 dB vs the EP Booster's spec +20 dB.** gMax 6 leaves ~4.4 dB
   on the table. If that was an ear/headroom decision, doc it in ep3.md; if
   not, gMax ≈ 10 matches spec.
2. 1st-order ADAA, but at gain ≤6 with bias 0.10 it barely clips — genuinely low
   priority, unlike the Rangemaster.
3. The real EP Booster's internal DIP switches (bass boost / bright) are absent
   — the Range-switch mechanism already exists for exactly this kind of mod if
   you ever want it. Feature choice, not a flaw.

## OD 0 — Green Drive (TS808)

Voicing (DriveBlock.h:245): clip 3 cubic, 5→80, HP 220, +3.6 dB @ 820 Q 0.7
post-clip, LP 1900, emphasis 9 dB @ 700, clean 0.20 dyn 0.40, static. Io: 446k
gentle damp (ioFor:399). Emphasis pair ≈ the real 720 Hz feedback HP — the
right trick, and ADAA2 via the cubic path.

1. **Gain range 5..80 vs the real 12..118.** The code comment itself admits the
   approximation (DriveBlock.h:100). Real TS min gain is ~+21 dB *into the
   diodes* — a real TS808 at min Drive still has hair; yours is cleaner than
   the pedal. Max is ~3 dB shy. Worth one refit pass now that CalNorm exists
   (the range predates it).
2. **The structural unity path scales wrong.** In the non-inverting TS topology
   the dry signal passes at unity *always* — the clipped component rides on
   top, which is why a TS never fully saturates and why its clean component is
   constant in absolute level. cleanBlend 0.20 is a fixed *fraction of the
   mix*, so at high Drive your dry fraction stays 20 % where the real one
   shrinks in proportion, and at low Drive it's too small. A drive-dependent
   bEff (or summing clean at unity like the real op-amp does, post-trim) is
   more faithful than a constant.
3. Tone tilt ±9 dB treble-only (bass pinned in softPoly) — right shape, range a
   little narrow vs the real tone stack's extremes. Minor.

## OD 1 — Super Drive (Boss SD-1)

Voicing (DriveBlock.h:260): clip 4 asym (bias 0.35 → kn 0.65), 6→120, +5 dB @
900 Q 0.5 post, emphasis 10 dB @ 700, clean 0.15 dyn 0.40. Io: transparent 1 M ✓.

The strongest model in the review. Asym-cubic (persisting asymmetry) over
DC-bias-into-symmetric-clip was the correct call, the 2:1→0.35 softening is
reasoned in the comment (soft feedback knee), the fit doc (sd1.md, RMS 0.63 dB)
matches shipped values, and it has genuine test coverage (T39-T41). Remaining
items are rot only: DriveBlock.h:246 "model 2", sd1.md:1 "model 2", the T38 bug
above, and T38's check still calling model 1 "Green Drive II".

## OD 2 — Gold Horse (Klon Centaur)

Voicing (klon.md/DriveBlock.h:277): clip 1 hard + ADAA2, 2→70, band-pass
voicing @ 980, shapeTrack 1, **cleanBlend 0.50 fixed**, kCleanScale 3.5,
active treble shelf 18 dB @ 408 (proper bilinear 1st-order shelf — the flat
passband work is genuinely good). Io: buffered ✓.

1. **The clean/dirty mix should track Gain — that's the whole Klon trick.** The
   real Gain knob is a DUAL-GANGED pot that simultaneously raises dirty-path
   gain and rebalances the clean/dirty sum: near-min it's essentially a clean
   boost with a whisper of hair; at max the dirty path dominates but clean
   still fills underneath. The model holds a constant 50/50 (+envelope nudge)
   at every Drive — too dirty at low Gain, arguably too polite at max. Fix is
   cheap and per-block: bEff base interpolated with drive (≈0.85 → 0.30 across
   the sweep) before the envelope term. This is the single biggest sonic
   improvement available in the OD category.
2. kCleanScale fixed at 3.5 couples to #1 — if bEff becomes drive-dependent,
   revisit whether the clean leg should stay level-constant (it should: that's
   the real behaviour) while only the fraction moves.
3. Germanium threshold + 18 V charge-pump headroom are folded into the gain
   calibration rather than modelled — acceptable, but note the real unit's
   clipping onset is softer than an ideal hard clip at the extremes of the
   Gain pot (the diodes see a widely varying source impedance). Ear-test item.
4. Rot: DriveBlock.h:261 "model 3", klon.md:100/104 "model 3", T45's check text
   "model 3 (Gold Horse)" (code index 2 is correct), klon.md mentions
   "kCleanScale 5→3.5" history — code has only 3.5, fine, but the header
   comment should state the current value.

## OD 3 — Breaker Drive (Marshall Bluesbreaker)

Voicing (DriveBlock.h:300): clip 3, 3→48, HP 20, +4.7 dB @ 4k Q 0.68 pre
(midPost 0), LP 13000, emphasis 5 dB @ 700, clean 0.22 dyn 0.45. Io: fully
transparent, inHp 0 with the rationale documented (ioFor:404-406) ✓.

1. **Shipped values silently drift from the doc's derived fit** —
   bluesbreaker.md:87 derives lowCut 18 / lpHz ~16000, shipped is 20 / 13000.
   The lpHz gap (16k→13k) is an audible top-end choice and nothing records
   why. One sentence in the doc ("ear-darkened from the fit because …")
   prevents a future session from "fixing" it back.
2. The real BB's famously low output was deliberately not reproduced
   (outTrim 1.15 for usability) — good call, but same treatment: doc it as a
   decision so it isn't mistaken for an error.
3. Circuit fidelity is otherwise sound: symmetric soft feedback clip, gentlest
   gain range in the category, open lows — matches the pedal's character.
   Rot: DriveBlock.h:278 "model 4", bluesbreaker.md:134+ "model 4", T58 text
   "model 4" + variable `m4` (code index 3 correct).

## Distortion 0 — Black Rodent (ProCo RAT) — from the 2026-07-04 session review

> **ADDRESSED 2026-07-04 (UNCOMMITTED, needs Windows build + play-test):** items 1–3
> below are done. (1) T15 repurposed to pin the shipped voicing; proco-rat.md rewritten
> for the single model. (2) **Hump migration** implemented — per-block log-interpolated
> peak, Tight/Full toggle (default Tight ~1500→620 Hz, Full ~2200→330 Hz), new
> `midMigrate` field + `setMigrateFull`/`modelHasMigrate` + UI segmented control + tests
> T15b. (3) **LM308 slew** implemented — pre-clip rate limiter on the op-amp output with
> an anti-alias ceil, `slewMax 2.5` (sub-sample at 48k; measured via the ADAA path,
> test T15c). Item 4 (Filter 18k→32k, gMin, oversampling) left as noted. Details:
> `proco-rat.md`. drive_test all green offline.

1. **T15 tautology** (above) and proco-rat.md describing the deleted two-model
   state, including "A/B level-matched to model 0" against a model that's gone.
2. **The hump doesn't migrate.** The doc's own derivation shows the LM308 peak
   walking 2267→308 Hz as Distortion rises; shipped voicing pins midHz 935 /
   lpHz 4800 and shapeTrack only scales EQ *amount*. Cranked, it stays
   mid-935-bright where the real pedal goes dark and throaty. Fix:
   log-interpolate midHz/lpHz along the fitted Rdist curve per block (the Muff
   tone stack already recomputes per block — pattern exists; note the mid
   biquad currently only rebuilds on cfg change, DriveBlock.h:484).
3. **No LM308 slew-rate limiting** (~0.3 V/µs) — nonlinear, famous, and not
   reproducible by any linear filter. A simple rate limiter before the diode
   clip, threshold scaled to the CalNorm reference.
4. Minor: Filter open end 18 k vs real 32 k (inaudible); gMin 4 = +12 dB vs the
   real Gv≈1 (the doc's "near-clean at min" claim is optimistic); no test pins
   the Filter endpoints or the hump position at high drive; ADAA2 residual
   −36 dB @ 13 k worst case (oversampling would fix; conflicts with the
   lean-CPU wedge — a deliberate knob).

## Fuzz 0 — Round Fuzz (germanium Fuzz Face)

Voicing (DriveBlock.h:321): clip 4 asym (bias 0.45), 8→200, HP 50, gate 0.6
(relative-to-own-peak velcro — nicely designed), dyn 0.50, no tone (correct —
the real pedal has none). Io: strong loading shelf −5 dB @ 2700 ✓.

1. **Volume-cleanup is still the missing half of the pedal** — the code's own
   comment defers it (ioFor:415). Touch cleanup (dynDepth) exists, but the
   guitar-volume interaction is different physics: rolling the volume raises
   the source impedance the low-Z input sees → simultaneously *brighter* and
   cleaner. Since the rig is captured through a 1 MΩ DI it can't happen
   acoustically; the honest version is a "Cleanup" control (or wiring the
   existing input-trim) that crossfades the io shelf depth and drive together.
   Biggest authenticity item in the Fuzz category.
2. **Static bias.** bias 0.45 is fixed; a real FF's operating point *sags with
   signal* (that's where the splat/blocking on hard hits comes from — the gate
   approximates the decay end of it but not the attack end). A slow
   envelope→bias modulation (env you already compute) would get the
   hit-it-harder sputter. Medium effort, real payoff.
3. Cosmetic but trap-laying: the row's adaa2 field is 0, yet the clip-4 branch
   is hard-wired to `clipAsymCubicADAA2` (DriveBlock.h:651) — anyone reading
   the table concludes it's 1st-order. A comment on the field (or setting the
   flag redundantly) closes the trap.

## Fuzz 1 — Violet Ram (Big Muff Ram's Head)

Voicing (DriveBlock.h:342): 2-stage cubic cascade, per-stage Miller LPs
(1200/1780), post LP 1170, passive nodal tone stack recomputed per block,
midDb 0. Committed, play-tested, refit to the PluginDoctor trace — the most
verified model in the plugin.

1. **The model comment block describes the REPLACED tone implementation.**
   DriveBlock.h:332-337 still says the mid scoop is "a static post-clip notch
   … Tone = the engine see-saw tilt @ 1 kHz" — both were removed in the
   tone-stack rework (row has midDb 0; the passive stack at :559-588 is the
   implementation; T56 asserts passive). A future session reading only the
   comment would "restore" the notch. Highest-value five-minute fix in this
   file.
2. Known simplification worth recording in big-muff.md: the real Muff's *input
   booster* stage (stage 1 of 4) also clips at high Sustain; the model clips
   only the two diode stages. You judged 2 stages right by ear — write that
   down so it reads as a decision, not an omission.
3. Rot: big-muff.md:172 counts ("Fuzz holds 3 / Distortion back to 2").

---

## Priorities

**P1 — correctness & anti-footgun (an hour, zero sonic risk):** fix T38:694
(0→1 + check text); repurpose T15's tautological CHECK to pin the voicing;
rewrite the Violet Ram comment block; correct NEXT_SESSION_HANDOFF.md:222-266
and current-driveblock.md inventories; sweep the "model N" off-by-ones
(DriveBlock.h:246/261/278, klon.md, bluesbreaker.md, sd1.md, proco-rat.md,
test banners).

**P2 — sound, ranked by expected audibility:** Klon drive-tracked clean blend →
RAT hump migration + slew limit → Range '65 ADAA2/oversample → Fuzz Face
env-bias sag → TS gain-range refit → Fuzz Face cleanup control (bigger,
needs a UI decision).

**P3 — doc the deliberate deviations** so they survive fresh sessions: Breaker
lpHz 13k vs fit 16k and output level; Plex gMax 6 vs +20 dB spec; RAT gMin 4;
Muff 2-stage choice.

All P2 items should go through the established loop: offline drive_test
additions first, then Windows build + ear test. Nothing here is committed yet
— this review changed no code.
