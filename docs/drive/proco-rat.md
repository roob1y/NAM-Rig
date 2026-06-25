# Black Rodent II — the ProCo RAT, circuit-fit

The Distortion category's reworked model (model 1), done the same way Green Drive
became Green Drive II: derive the circuit, fit the voicing, pick the clip + ADAA,
calibrate the feel, test. Model 0 ("Black Rodent", the original simple hard-clip
stand-in) is kept **byte-for-byte** for A/B. Source: the
[ElectroSmash ProCo RAT analysis](https://www.electrosmash.com/proco-rat).

## How the RAT differs from the Tube Screamer (and why it matters)

| | Tube Screamer (Green Drive II) | ProCo RAT (Black Rodent II) |
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

We fit at **Rdist ≈ 12 k** (peak ~960 Hz = the canonical ElectroSmash "~1 kHz hump").
Baking in the extreme max-gain 300 Hz collapse would make a muddy voicing; the
recognizable RAT is the mid-forward honk, and `shapeTrack` + the Filter handle the
"darker when cranked" behaviour. Fit RMS **0.03 dB**:

```
 f(Hz):    50   100   300   500   700  1000  1500  2000  3000  5000
 target:-20.6 -17.0 -10.6  -5.7  -1.8   0.0  -4.3  -7.8 -12.0 -16.9   (rel 1 kHz)
 ours:  -20.6 -17.0 -10.6  -5.7  -1.8   0.0  -4.3  -7.8 -12.1 -16.9
```

Shipped voicing (model 1): `lowCutHz 62, midHz 935, midDb 17.0, midQ 0.50,
lpHz 4800`, `midPost 0`, `shapeTrack 1`, hard clip (`clip 1`) on 2nd-order ADAA.

## Clip shape + ADAA order — measured, not assumed

Hard clip (the RAT diodes) is the harshest shaper and fizzes the most. The playbook
warns that on a *bare* hard clip 2nd-order ADAA measured no better than 1st-order,
so we measured it **in the full RAT pipeline**. 2nd-order clearly wins here (the
mid-hump + high gain make the difference real), with no spike penalty:

| metric (model 1, 5 kHz hot, Drive max) | naive | 1st-order ADAA | **2nd-order ADAA** |
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
A/B level-matched to model 0 within ~7 % RMS across the usable upper-half of the
Drive sweep (`outTrim 0.47`).

## Tests (`tests/drive_test.cpp`)

T15 model 0 byte-for-byte == legacy (A/B preserved) · T16 ADAA2 cuts alias ≥12 dB ·
T17 maxabs no-spike sweep · T18 Filter darker CW · T19 mid-forward voicing that
tightens the bass with Drive · T20 humbucker drives harder than single-coil. All
green alongside the unchanged T1–T14.
