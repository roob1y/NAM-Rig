# Breaker Drive — the Marshall Bluesbreaker (Overdrive model 4)

A worked example of the [playbook](building-drives-playbook.md), built end-to-end the
same way as [Green Drive II](circuit-accuracy.md) (TS808) and [Super Drive](sd1.md)
(SD-1). The Bluesbreaker is the **soft, open, symmetric** end of the overdrive family —
the deliberate opposite of a Tube Screamer — and the ancestor of the Analogman King of
Tone, the Paul Cochrane Timmy and the JHS Morning Glory. Companion derivation script:
[`bluesbreaker_response.py`](bluesbreaker_response.py).

> Robbie's brief: build the **original early-'90s Marshall pedal** (not the KoT/Timmy
> descendants), as a **softer, symmetric, open-low-end OD** — gentler than a TS, less
> bass cut, mild emphasis, soft diode clipping. Filed in **Overdrive** (its real
> category), which needed the shared `bModel` param widened 0..3 → 0..4. Disguised
> name **"Breaker Drive."**

---

## 1. The circuit (research)

Sources: AionFX *Cerulean Overdrive* analysis + full BOM (the gold-standard trace),
GM Arts / Stomp Box Schematics, tonegeek's *Bluesbreaker mods*.

The Bluesbreaker (Marshall, 1991–~1996) is a **TL072 dual-op-amp feedback-clip
overdrive** — it shares the op-amp-feedback diode clipping idea with the TS, but the
voicing is its own. Three blocks:

```
adjustable gain stage & filter (IC1A + IC1B)  ->  soft clip  ->  passive tone -> volume
```

1. **IC1A — non-inverting boost + filter.** Gain `= 1 + Zf1/Zg1`. The Drive pot
   (100 kB) is wired as a **voltage divider shared between the two stages** (the classic
   Marshall Guv'nor/Shredmaster gain control): adding resistance to IC1A's feedback
   *removes* it from IC1B's input, so both stages gain together. The ground leg has an
   **R2 (3k3) + C3 path that lifts mids/highs only** — a gentle HF emphasis shelf.
2. **IC1B — inverting soft-clip stage.** Gain `= Zf2/Zin2`, with **four 1N914 diodes
   (two in series each way) in the feedback loop**. Two-in-series = a **high ~1.2 V
   threshold**, plus a **6k8 series resistor that softens** the knee. AionFX: by its
   nature feedback clipping is "harder than diodes-to-ground, but here it's offset by
   the high threshold + the 6k8" → the result is **soft and warm**, only "grainy" at
   high settings. **Symmetric** (back-to-back, equal both ways).
3. **Passive tone + volume.** The Tone control simply "rolls off the highs as it's
   turned down" — a **treble rolloff, bass fixed**. Volume is a standard output pot.

Stock "standard"/Korean-V2 BOM (the common version; the King of Tone uses the earlier
27k/33k for R2/R3 = lower gain):

```
R1 1M  R2 3k3  R3 4k7  R4 10k  R5 220k  R6/R8 6k8  R7 1k  R9 1M  R10/R11 47k
C1 10n  C2 47p  C3 10n  C4 10n  C5 220n  C6/C7 10n  C8 100n
IC1 TL072   D1-4 1N914   Drive 100kB   Tone 25kB   Volume 100kA
```

Sources:
[AionFX — Cerulean / Bluesbreaker analysis + BOM](https://aionfx.com/project/cerulean-amp-overdrive/),
[AionFX legacy build doc (PDF)](https://aionfx.com/app/files/docs/cerulean_legacy_documentation_v1.pdf),
[GM Arts — Marshall Blues Breaker](https://stompboxschematics.com/circuits/marshall-blues-breaker-project-by-gm-arts/),
[tonegeek — Bluesbreaker mods](http://www.tonegeek.com/musicgear/pedals/marshall-bluesbreaker-mods.php).

## 2. The clean voicing (derive + fit)

`bluesbreaker_response.py` derives the small-signal (diodes open) transfer function —
input HPF (C1/R1), the IC1A gain + HF-emphasis shelf (the shared-pot divider modelled),
the IC1B inverting gain + the 47 pF feedback roll — and fits our `lowCut · midPeak ·
topLP` to it (normalised at 200 Hz). The voice is the **opposite of a TS**:

```
Bluesbreaker small-signal (dB re 200 Hz):
 f(Hz):    50   100   200   300   500   720  1000  1500  2000  3000  5000  7000
 min  :  -0.4  -0.1  +0.0  +0.0  +0.0  +0.0  +0.0  -0.0  -0.0  -0.1  -0.4  -0.8
 noon :  -0.4  -0.1  +0.0  +0.1  +0.2  +0.4  +0.7  +1.4  +2.0  +3.3  +4.6  +5.1
 (TS808 for contrast: 50Hz -8.2, 720Hz +5.5 hump -- a bass cut + a MID hump)
```

Two defining traits, both confirmed by the circuit (not guessed):

- **Wide-open low end.** The input HPF is `C1 10n / R1 1M ≈ 16 Hz` — essentially no
  bass cut (the TS cuts ~8 dB at 50 Hz, the SD-1 ~7.6). This is the "open, full,
  amp-like low end" the BB is known for.
- **A gentle bright presence shelf**, not a mid hump: a high-shelf rising to ~+5 dB
  toward 4–7 kHz (the IC1A R2/C3 emphasis). It **blooms with the Drive pot** in the
  circuit, but saturates fast — it is ~flat only at the very bottom of the pot and
  essentially full from noon up. So we ship it **static** (`shapeTrack 0`): a fixed
  shelf matches the usable range far better than a slow linear bloom, and the "clean
  till you push it" feel comes from the low `gMin` + soft clip, not the EQ.

Best fit (RMS **0.35 dB**, 40 Hz–6 kHz): `lowCut 18, midHz 4000, midQ 0.68, midDb 4.7,
lpHz ~16000`. A single RBJ bell can't perfectly track a continuously-rising shelf, so
the top octave is approximated (the bell gently rolls 5–7 kHz where the real pedal's
47 pF feedback cap + tone control also trim it). Shipped voicing: `lowCut 20, midHz
4000, midDb 4.7, midQ 0.68, lpHz 13000` (the ~15 kHz C2 corner, with a hair of
anti-alias headroom).

## 3. The clip (symmetric soft)

Clip **type 3 — the cubic soft clip** (`x − x³/3`, the Green Drive II core), `bias 0`
(**symmetric**, the back-to-back diodes). The high real threshold + 6k8 series R make
the Bluesbreaker a *soft* clipper despite the feedback topology, so the smooth cubic
knee is the right shape — gentler than the SD-1's asymmetric cubic and far gentler than
a hard clip. Polynomial → exact F1/F2 → **2nd-order ADAA** (automatic for clip 3),
peak-guarded. Measured: **30.8 dB alias reduction at 3 kHz** vs a naive memoryless cubic
sharing the same pre-gain (T64).

The **pre/de-emphasis** pair is set gentle — `emphDb 5 @ 700 Hz` (vs the TS's 9 / SD-1's
10) — the "mild emphasis" of the brief. It cancels exactly in the linear path (so it
doesn't touch the small-signal fit) and only bites under drive: bass clips least, the
top stays smooth. A **slightly larger clean blend (0.22)** + **touch (`dynDepth 0.45`)**
preserve the BB's famously good guitar timbre/dynamics ("retention of guitar timbre and
dynamics is good" — GM Arts).

## 4. Range, output, tone, feel (calibrated by measurement)

- **The gentlest OD in the rack.** `gMin 3 / gMax 48` — lower and softer than GD2
  (5/80) and SD-1 (6/120). Measured noon THD (humbucker level): **Breaker 0.35 < GD2
  0.38 < SD-1 0.56** (T61). It **breaks up late** — nearly clean at low Drive
  (THD 0.012) and dirties up the dial (T63), the real pedal's "not much overdrive
  before 3 o'clock."
- **Input-level dependent** (fixed clip threshold, calibration-referenced): a humbucker
  drives it clearly harder than a single-coil at the same knob (THD 0.10 vs 0.01, T63).
- **`outTrim 1.15`** A/B level-matches Green Drive II at noon (RMS ratio **0.96**, within
  the playbook's ~5 %).
- **Tone = TS-style treble shelf, bass fixed** (the soft-poly path forces `bassG = 1`):
  the BB's passive treble rolloff. `pivotHz 1200`. Measured: 3 kHz moves +13.6 dB across
  the sweep while 100 Hz stays fixed (±0.2 dB) (T59).
- **`shapeTrack 0`, `midPost 0`** (the presence shelf sits PRE-clip, like the real IC1A
  gain stage — it both colours the tone and lets the boosted highs clip a touch = the
  BB's mild grain when pushed). Static voicing → the small-signal response is
  drive-independent (T59).

End-to-end check: a small-signal Goertzel sweep of the **actual engine** (not the fitted
pieces) lands on the circuit target to **RMS 0.71 dB** over 40 Hz–6 kHz, with the open
lows (−0.7 dB @ 50 Hz) and the presence rise (+3.4 dB @ 3 kHz) where the schematic says.

## 5. Voicing row (DriveBlock.h `od[]`, model 4)

```
//  clip gMin  gMax  lowCut  midHz midDb midQ  lpHz   bias  pivot  outTrim shp post emphDb emphHz clean dyn  toneF adaa2
{ 3, 3.0f, 48.0f, 20.0f, 4000.0f, 4.7f, 0.68f, 13000.0f, 0.00f, 1200.0f, 1.15f, 0.0f, 0.0f, 5.0f, 700.0f, 0.22f, 0.45f, 0.0f, 0.0f }
```

## 6. UI / param wiring

Overdrive's `bModel` was **full at 0..3** (Green Drive / GD II / Super Drive / Gold
Horse), so Breaker Drive needed the **shared `bModel` `AudioParameterInt` widened
`0,3` → `0,4`** in `PluginProcessor.cpp` (it is the per-slot model index across *all*
drive categories; the model menu clamps to each category's `modelCount`, so the smaller
categories ignore the extra index). Everything else is generic and required **no** menu
changes: the Type menu / model dropdown are built from `modelCount`/`modelName`
(Panels.h), the processor's Overdrive case already reads `bModel`, and the per-model
caption branch keys on `model == 3` (Gold Horse → Gain/Treble/Output), so Breaker Drive
(model 4) correctly falls through to the standard **Drive / Tone / Level**.

## 7. Tests + build

`drive_test.cpp` **T58–T64** (and the `modelCount(Overdrive) == 5` bumps in T11/T38/T45):

- **T58** OD model 0 byte-exact after adding Breaker; model 4 finite; symmetric cubic.
- **T59** the voice: open lows (−0.7 dB @ 50 vs GD2 −10), presence shelf (+3.4 dB @ 3k),
  static across Drive, treble-shelf Tone (3k moves, 100 Hz fixed).
- **T60** symmetric (h2/h1 0.0009) vs the asymmetric SD-1 (0.021).
- **T61** the gentlest OD: noon THD 0.35 (BB) < 0.38 (GD2) < 0.56 (SD-1).
- **T62** no spikes — maxabs 0.84 at Tone noon (suite bound); bounded+finite (1.63) even
  at Tone fully-CW (the bright +9 dB treble shelf = legitimate gain on a hot signal, not
  a crackle spike — the brightest OD runs hotter there by design).
- **T63** input-dependent (humbucker drives harder) + breaks up late (clean till pushed).
- **T64** the cubic's 2nd-order ADAA cuts alias@3k by 30.8 dB vs a naive cubic.

Offline build **green: 114 CHECKs, 0 failures**, plus a **byte-exact regression** (FNV
hash over every shipped Boost/OD/Dist/Fuzz model × drive × tone × range, HEAD vs the new
build — identical, so the new model is purely additive and no preset/automation drifts).
UI/processor edits are JUCE (reviewed by hand — only the one-line `bModel` range widen).
**Pending commit on Windows + play-test.**

Model inventory now: Boost 4, **Overdrive 5** (Green Drive / II / Super Drive / Gold
Horse / **Breaker Drive**), Distortion 2, Fuzz 3. `bModel` is now `0..4`; the next 5th
model in any category fits without further widening.
