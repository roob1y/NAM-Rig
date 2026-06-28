# Gold Horse — the Klon Centaur (Overdrive model 3)

A worked example of the [playbook](building-drives-playbook.md). The Klon is the
first model whose identity is **not** the clipper but the **parallel clean sum** —
so the engine work was adding a clean-blend path to the hard clip, not a new shaper.

## 1. The circuit (research)

The Klon Centaur (Bill Finnegan + MIT, 1994) set out to "improve the Tube
Screamer's transient response and the midrange-bass… a big open sound with a hint
of tube clipping" — the original **transparent overdrive**. It is *not* a TS. From
[ElectroSmash](https://www.electrosmash.com/klon-centaur-analysis) and
[Coda Effects](https://www.coda-effects.com/p/klon-centaur-circuit-analysis.html):

- **Gain stage (TL072):** a non-inverting amp whose response is a **band-pass that
  peaks ~1 kHz** (40 dB max) and rolls off below and above — a broad mid hump
  (cf. TS 723 Hz, RAT 1 kHz).
- **Clipping:** two germanium **1N34A** diodes **back-to-back to ground**
  (VF ≈ 0.35) — **symmetric**, hard clip. But most distortion at low/medium gain
  is the **op-amp itself** clipping into its rails; the diodes only bite at the
  highest gain ("soft knee first, more aggressive at the end").
- **The signature — parallel clean sum:** a **dual-gang gain pot** blends the
  clipped path with **clean feedforward** paths (one a low-passed bass feed
  ~106 Hz that restores low end; one gain-balanced). All three are mixed in a
  **high-headroom (27 V) summing stage** → "a signal that has dynamics and not just
  pure distortion." This is the transparency.
- **Tone:** an active treble shelf, corner ~408 Hz (bass fixed, treble boost/cut).
- **Output:** lots of it — the Klon doubles as a clean boost.

## 2. The clean voicing (derive + fit)

`docs/drive/klon_response.py` models the clipped-path gain stage as the published
~1 kHz band-pass (the exact caps are gooped/"tricky values", so we fit the
*response*, not guessed components) and fits our `lowCut·midPeak·topLP` to it:

```
Klon clipped-path band-pass (dB re 1 kHz):
 f(Hz):   50   100   200   400   700  1000  1500  2000  3000  5000
 dB:   -15.6  -9.9  -4.8  -1.4  -0.2  +0.0  -0.3  -0.8  -2.2  -4.9
```

Best fit (RMS **0.14 dB**): `lowCut 210, midHz 980, midQ 0.3, midDb 3.2, lpHz 4700`
(bright/open top — the 27 V headroom feel). The clipped path is mid-focused, so it
*scoops* the lows on its own — which is exactly what the clean sum is for. The
script's transparency check confirms it: blending 50 % flat clean lifts the 50 Hz
response from −15 dB (clipped alone) to **−4.7 dB** (open, full low end).

## 3. The clip + the clean sum (the actual work)

Clip **type 1 (hard, symmetric)** on **2nd-order ADAA** — same clean hard-clip path
as Black Rodent II (the germanium diodes are a symmetric hard clip to ground).

The identity is the **heavy parallel clean blend**, which the engine only had on the
soft-poly clips (3/4). Added to the **hard-clip path** (guarded so Black Rodent II,
`cleanBlend 0`, stays byte-exact — regression T46):

- The clean is the **RAW input** (`xin`), full-range and flat, so it restores the
  lows the mid-focused clip drops — the Klon "big open" low end. It is at **input
  level** (never the gained signal → the crackle pitfall), then scaled by
  **`kCleanScale 5→3.5`** so it sits at the clipped path's ±1 level (otherwise the
  ×preGain clipped path swamps it and the "heavy" blend is inaudible). `cleanBlend
  0.50` + `dynDepth 0.30` (touch). Tuned so worst-case |out| stays < 1.5 (T50).
- **`shapeTrack 1`**: the mid hump + bass-cut **bloom with Drive**, so at low Drive
  the pre-EQ is ~flat and the path barely clips → a **near-clean boost** (THD ~0.001
  at Drive 0.1); it distorts more as Drive climbs (the Klon reputation). T48.

Measured character (humbucker): THD climbs 0.001 → 0.02 → 0.20 → 0.52 across the
Drive sweep (cleaner than both Green Drive II and Super Drive at low/mid Drive — the
transparent feel); lows stay present at playing levels (100 Hz within ~0.4 dB of
1 kHz, T47); symmetric so even harmonics stay low (h2/h1 ~0, T49, unlike the asym
Super Drive).

## 4. Range, output, tone, feel

- **Faithful Klon** (Robbie's call): `gMin 2` (genuinely clean at minimum — clean
  boost), `gMax 70` (~the real 40 dB), `outTrim 0.95` (≈ Green Drive II at noon;
  the Klon's famous *boost* comes from the per-slot **Level** knob, kept safe so the
  parallel clean sum doesn't overshoot the amp). Calibration-referenced; humbucker
  drives harder than single-coil (fixed clip threshold).
- **Tone (Treble):** the **real Klon active treble shelf** — a **proper 1st-order
  high-shelf**, zero fixed at `pivotHz 408`, **pole at 408·G** (rides up with the
  knob, bilinear-discretised, recomputed per block). LF passband stays **flat** even
  at full boost (measured **+0.26 dB @ 100 Hz**, matching the derived circuit's
  +0.25 dB — the first low/high-*blend* attempt leaked ~+6 dB and was replaced).
  Treble swings asymmetrically **+18 dB (full CW) to −8 dB (full CCW)**
  (`trebleShelfDb 18`, cut = 0.44×), flat at noon. Circuit: Gvmax = (RV2+R23)/R21 =
  +18.24 dB, Gvmin = −8 dB, fc = 1/(2π·R22·C14) = 408 Hz. Because it's a *boost*
  shelf (passband fixed), turning Treble up genuinely **raises level** (+11.7 dB
  broadband CCW→CW) — the authentic Klon behaviour, unlike a level-neutral tilt.
  (New voicing field `trebleShelfDb`; 0 = the legacy tilt → every other model
  byte-exact. Panel labels this knob **Treble**.)

Engine note: two small additions, both zero-fill/guarded so all other models stay
byte-exact — (1) the hard-clip branch does an optional clean blend
(`cleanBlend>0 || dynDepth>0`, raw input × `kCleanScale`), and (2) the
`trebleShelfDb` active high-shelf tone. Tunable taste knobs by ear:
`cleanBlend`/`kCleanScale` (transparency), `gMax` (gain), `outTrim` (level),
`lowCut`/`midDb` (body vs hump), `trebleShelfDb`/`pivotHz` (treble shelf).

## 5. Voicing row (DriveBlock.h `od[]`, model 3)

```
//  clip gMin gMax  lowCut midHz midDb midQ  lpHz   bias  pivot  outTrim shp post emphDb emphHz clean dyn  toneF adaa2 gate trebleShelfDb
{ 1, 2.0f, 70.0f, 210.0f, 980.0f, 3.2f, 0.3f, 4700.0f, 0.00f, 408.0f, 0.95f, 1.0f, 0.0f, 0.0f, 700.0f, 0.50f, 0.30f, 0.0f, 1.0f, 0.0f, 18.0f }
```
