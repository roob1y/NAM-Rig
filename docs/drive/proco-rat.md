# Black Rodent — the ProCo RAT, circuit-fit

The **sole Distortion model** (model 0). Derived the circuit, fit the voicing, picked
the clip + ADAA, calibrated the feel, tested. The original simple hard-clip stand-in was
retired, so this circuit-fit RAT **is** model 0 now (no legacy A/B target survives — the
old "model 0 kept byte-for-byte" note below no longer applies). Source: the
[ElectroSmash ProCo RAT analysis](https://www.electrosmash.com/proco-rat).

> **2026-07-04 — migrating hump + LM308 slew (UNCOMMITTED, needs Windows build + play-test).**
> Two RAT behaviours the first fixed voicing missed are now modelled: the mid hump
> **migrates down** with Drive (the LM308 GBW collapse), with a **Tight/Full** toggle for
> how far it slides; and the LM308 **slew limit** rounds the hard-clip edges. New sections
> "Hump migration" and "LM308 slew" below; everything above them is the original fit.

## How the RAT differs from the Tube Screamer (and why it matters)

| | Tube Screamer (Green Drive II) | ProCo RAT (Black Rodent) |
|---|---|---|
| Op-amp | JRC4558, modest gain | **LM308**, huge gain (Gv up to **2305 / 67 dB**) |
| Clipping | soft, diodes **in the feedback** loop | **hard, silicon diodes to GROUND** after the gain stage |
| Freq-selective clip | feedback HPF (modelled by pre/de-emphasis) | the **gain stage's own EQ** is the pre-clip shape |
| Tone | treble shelf, bass fixed, brighter CW | **"Filter" low-pass, darker CW** (opposite) |
| Clip shape / ADAA | cubic soft, 2nd-order ADAA | hard clip, **2nd-order ADAA** (verified, see below) |

Because the RAT clips *after* the gain stage, the gain stage's frequency response
**is** the pre-clip EQ — whatever it emphasises is what the diodes hard-clip. So
there's no separate emphasis pair (the TS trick); the mid-hump + bass-cut are fit
directly as the pre-clip EQ, set PRE-clip (`midPost 0`) and bloomed with Drive
(`shapeTrack 1`), so bass is clipped LEAST and mids grind hardest.

## The gain stage (derived → `proco_rat_response.py`)

```
Gv(f) = 1 + Zf/Zg ,  with the LM308 finite GBW rolling off the top:
  Zf = Rdist || 1/(jwC4)        Rdist 0..100k, C4 100pF  (feedback LP, ~16k @ max)
  Zg = (R4+1/jwC5) || (R5+1/jwC6)  R4 47/C5 2.2u -> 1539 Hz ; R5 560/C6 4.7u -> 60 Hz
  Aol = GBW/jw (GBW ~1 MHz) -> Gcl = Gv*Aol/(Aol+Gv)   ("the op-amp collapsing")
Gv = 1 + Rdist/(R4||R5) = 1 (min) .. 2305 (max, 67 dB)
```

The hump **migrates with the Distortion pot** as the GBW collapses the bandwidth:

```
Rdist    2k -> peak 2267 Hz      10k -> 1020 Hz      30k -> 583 Hz     100k -> 308 Hz
```

The nominal fit anchor is **Rdist ≈ 12 k** (peak ~960 Hz = the canonical ElectroSmash
"~1 kHz hump"), kept as `midHz 935` for the small-signal shape below. **The peak now
MIGRATES with Drive** (see "Hump migration"), so this table is the mid-Drive slice; the
first version pinned the peak here and used `shapeTrack` + the Filter alone for the
"darker when cranked" feel. Fit RMS **0.03 dB** at the anchor:

```
 f(Hz):    50   100   300   500   700  1000  1500  2000  3000  5000
 target:-20.6 -17.0 -10.6  -5.7  -1.8   0.0  -4.3  -7.8 -12.0 -16.9   (rel 1 kHz)
 ours:  -20.6 -17.0 -10.6  -5.7  -1.8   0.0  -4.3  -7.8 -12.1 -16.9
```

Shipped voicing (model 0): `lowCutHz 62, midHz 935, midDb 17.0, midQ 0.50,
lpHz 4800`, `midPost 0`, `shapeTrack 1`, hard clip (`clip 1`) on 2nd-order ADAA,
plus the two authenticity extras `midMigrate 1` and `slewMax 2.5` (below). `midHz 935`
is now the nominal noon anchor; the migration overrides the peak frequency per block.

## Clip shape + ADAA order — measured, not assumed

Hard clip (the RAT diodes) is the harshest shaper and fizzes the most. The playbook
warns that on a *bare* hard clip 2nd-order ADAA measured no better than 1st-order,
so we measured it **in the full RAT pipeline**. 2nd-order clearly wins here (the
mid-hump + high gain make the difference real), with no spike penalty:

| metric (model 0, 5 kHz hot, Drive max) | naive | 1st-order ADAA | **2nd-order ADAA** |
|---|---|---|---|
| alias @ 3 kHz | 0 dB | −31.1 dB | **−41.5 dB** |
| alias @ 13 kHz | 0 dB | −22.5 dB | **−36.1 dB** |
| worst \|out\| over full-scale sweep, all drives | — | 0.55 | **0.55** (no spikes) |

So we keep 2nd-order, with the **same peak guard** as the cubic (the `x[n]==x[n-2]`
alternation at signal peaks falls back to 1st-order ADAA over the step — verified
by the maxabs sweep, T17).

## Tone — the "Filter"

The RAT tone is a passive one-pole low-pass that gets **darker clockwise** — the
opposite of the TS. `fc = 1/(2π(Rtone+1.5k)·3.3nF)` sweeps 32 kHz (CCW/bright) down
to 475 Hz (CW/dark). Modelled as a swept one-pole LP (`toneFilterHz 475`), output =
the low-passed signal (no high path). The Distortion tone knob is already labelled
"Filter" in the UI.

## Gain range + feel (calibration-referenced)

`gMin 4.0 → gMax 150` — much hotter than the TS's `5..80`. With the +17 dB pre-clip
mid, the effective mid gain at max Drive (~150·7 ≈ 1050) matches the LM308 stage's
GBW-limited peak gain (~960). The clip threshold is fixed, so distortion tracks the
actual input LEVEL: a humbucker (≈0.20) drives the diodes clearly harder than a
single-coil (≈0.08), exactly like the real pedal (T20). Voiced for the app's
calibration reference (`CalNorm kReferenceDbu`). Min Drive is near-clean — faithful
to the RAT (Gv ≈ 1 at min), so we don't force grit there as the TS rework did.
Output trim `outTrim 0.47` (the level match to the retired stand-in is historical — the
stand-in is gone; the trim is kept so presets/levels don't jump).

## Hump migration (2026-07-04)

The migration table above is a real circuit behaviour, not a curiosity: as the Distortion
pot raises Rdist, the LM308's finite GBW collapses the closed-loop bandwidth, so the peak
walks **2267 → 308 Hz** across the sweep. The first version pinned it at 935 Hz to avoid a
muddy cranked tone; it now migrates, log-interpolated by the Drive knob (circuit-consistent:
`log(preGain)` is linear in Drive, and the peak ∝ `preGain^-½`, so `log(peak)` is linear in
Drive too — a plain geometric interpolation between two endpoints).

A per-model **Tight/Full toggle** (`setMigrateFull`, UI `Tight`/`Full` segmented control,
default **Tight**) picks the endpoints:

```
Tight (default):  peak 1500 Hz (Drive 0) -> 620 Hz (max)   geo-mean ~965 Hz ≈ the old 935 anchor
Full  (authentic):peak 2200 Hz (Drive 0) -> 330 Hz (max)   spans the real 2267 -> ~308 collapse
```

Tight keeps a mid honk cranked (tasteful, no mud); Full collapses dark and throaty (the
authentic max-gain behaviour). Endpoints (`kRatHump{Tight,Full}{Hi,Lo}`) + the whole idea
are **ear-tunable** — Robbie play-tests. Engine: the mid peaking biquad is re-derived per
block via `Biquad::copyCoeffsFrom` (state-preserving → click-free), gated on `midMigrate>0`
so every other model keeps its config-time fixed peak, byte-exact. Note `shapeTrack 1` still
gates the hump's DEPTH by Drive, so at low Drive the (high-frequency) peak is barely engaged
and the audible effect is mostly WHERE the honk lands when cranked. **lpHz left fixed** (the
slew handles the rest of the cranked HF loss) — migrating it too is a further option.

## LM308 slew (2026-07-04)

The LM308 is a slow op-amp (~0.3–0.5 V/µs); its slew limit rounds the hard-clip edges = the
RAT's "aggressive but not buzzy" grind + large-signal HF loss when cranked. Modelled as a
rate limiter on the **op-amp output** (`u`, pre-diode): the output can't move faster than
`slewMax` clip-normalized units/sample (48 kHz-referenced, SR-scaled), and a ceil
(`kRatSlewCeil 1.5`) bounds its swing into the diodes so the slew ramps don't alias past what
the ADAA fixes. It's a **follower** of the amplified target (no feedback → can't wind up).

Placement matters: slewing the *clipped* ±1 node does nothing at 48 kHz (the ADAA-smeared
edge already fits in ~1 sample), and the effect is genuinely **sub-sample** here — a pointwise
clip can't see it; only the ADAA (which integrates over the sample) reveals it. Measured (max
Drive, hot 1 kHz through the ADAA path): top octave −~21 %, fundamental unchanged (<0.3 %),
aliasing controlled, worst |out| 0.65. `slewMax 2.5` is the shipped default — the effect
plateaus by ~3, so it sits engaged-but-not-maxed with headroom both ways. **Ear-tunable.**

## Tests (`tests/drive_test.cpp`)

T15 pins the shipped voicing (clip/ADAA, hump, gain range, Filter, `midMigrate`, `slewMax` —
replaces the old tautological "== legacy" A/B, the legacy model being gone) · **T15b** the
hump migrates down with Drive + Full darker than Tight · **T15c** the slew rounds the top
octave (via the ADAA mirror, since it's sub-sample) + preserves the fundamental + the mirror
tracks the engine · T16 ADAA2 cuts alias (probed at Drive 0.6, where the slew hasn't already
band-limited the 5 kHz) · T17 maxabs no-spike sweep · T18 Filter darker CW · T19 mid-forward
voicing at the *migrated* peak, tightens the bass with Drive · T20 humbucker drives harder.
All green (offline drive_test, all CHECKs pass) alongside the unchanged T1–T14.
