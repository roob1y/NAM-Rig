# Fuzz Face → Round Fuzz II (worked example)

The fifth run of the [building-drives-playbook](building-drives-playbook.md), and
the first that needed **new engine DSP** rather than just a new voicing row. The
Fuzz Face's identity isn't an EQ — it's the asymmetric germanium clipping, the
touch/volume cleanup, and the bias-starved gate — so most of the work was in the
clipper, not the filter.

Shipped as **Fuzz model 1 "Round Fuzz II"** (model 0 "Round Fuzz" kept
byte-for-byte). Source: `src/rig/DriveBlock.h`; derivation:
`docs/drive/fuzz_face_response.py`; tests: `tests/drive_test.cpp` T30–T35.

## The circuit (ElectroSmash "Fuzz Face Analysis")

The classic Arbiter Fuzz Face: two **PNP germanium** transistors (AC128), a
2-stage feedback amplifier, round enclosure (hence "Round Fuzz").

| part | value | role |
|------|-------|------|
| C1 | 2.2 µF | input coupling → HP with the low input Z |
| C2 | 20 µF | Q2 emitter bypass (corner ~8 Hz) |
| C3 | 10 nF | output coupling → HP with the Volume pot |
| R1 | 33 k | Q1 collector | R2/R3 | 470 / 8.2 k | Q2 collector divider |
| R4 | 100 k | global feedback | Rfuzz | 1 k | Fuzz pot (Q2 emitter degen) |

Three signatures, none of them an EQ:

1. **Very low input impedance (~5 kΩ)** loads the pickups — this is why the
   Fuzz Face must go first in the chain and why it **cleans up when you roll the
   guitar volume / pick softly**.
2. **Asymmetric clipping** — Q1 is biased cold (VC ≈ −1.6 V, not the −4.5 V centre),
   so one semicycle swings further. Small signals soft-clip one side first; big
   signals (chords) hard-clip both → the famous **soft-to-hard touch response**.
3. **Bias starvation** — as a note decays (or the battery sags) the cold-biased
   stage can't sustain it, so the output **gates/splats** ("velcro" fuzz).

## The voicing (`fuzz_face_response.py`) — minimal, as expected

"It just removes some bass and keeps all the highs." Two cascaded high-passes
(C1/Zin ≈ 14 Hz, C3/Volume ≈ 31 Hz), **no low-pass** (bright, raspy):

```
Fuzz Face target (dB re 1 kHz):  30Hz -4.2  50 -1.8  100 -0.5  >=200 ~flat
best-fit one-pole low-cut = 38 Hz (no mid, no top roll)  RMS 0.10 dB
```

So the voicing row is just `lowCut ~50, no peak, no LP`. Everything else is the
clipper.

## The new engine DSP

**A new clip type 4 — asymmetric cubic.** A germanium fuzz clips asymmetrically at
*all* gains, but a symmetric shaper + DC bias only gives an amplitude-asymmetric
square (which is DC + odd harmonics after the DC blocker — no real asymmetry at
high gain). So clip 4 puts the **positive knee at 1** (rail +2/3, the plain cubic)
and the **negative knee at `kn = 1 − bias`** (rail −(2/3)·kn): the negative half
saturates sooner, so the asymmetry **persists** into hard clipping — soft for small
signals, a tilted square when cranked. It's polynomial, so it has exact closed-form
`F1`/`F2` → cheap **2nd-order ADAA** with the same peak-guard as the cubic/hard
clips (validated in isolation: kn=1 reduces *exactly* to the symmetric cubic;
derivatives F1'=f, F2'=F1 to 1e-10; worst output is exactly the +2/3 rail, no
spikes). Measured in-pipeline: ADAA2 cuts alias@3k by **−44 dB** vs a naive
memoryless asym cubic.

**Touch/volume cleanup** — the cubic path's envelope + clean-blend (`dynDepth`) now
serves clip 4 too: soft picking raises the clean blend (and the fixed clip
threshold means a quieter signal clips less), so soft playing cleans up — measured
4.3× quiet/loud harmonic spread.

**Bias-starved gate** — a new `gate` field (0..1, zero-fill = every existing model
unaffected). In the soft-poly path, an envelope-driven gate collapses the output as
the note decays past a threshold (squared knee = the abrupt splat). Measured: a
decaying pluck collapses to 0.38 of its early level vs 0.83 for the non-gated fuzz.

## The voicing row

```
{"Round Fuzz II", clip 4 (asym cubic), gMin 8, gMax 200, lowCut 50, bias 0.45
 (kn 0.55), dynDepth 0.50 (cleanup), gate 0.60, outTrim 0.65, no mid/LP, no range}
```

`outTrim 0.65` level-matches model 0 at the cranked settings where a fuzz lives.
Fuzz has no Tone in the UI (pinned 0.5). **Processor fix:** the Fuzz case was
hardcoded `setModel(s, 0)` (the documented "2nd model" gotcha) — now reads `bModel`.

## Verification (`drive_test.cpp` T30–T35)

- **T30** model 0 byte-for-byte; Fuzz now has 2 models.
- **T31** heavy asymmetric fuzz (THD 0.95, h2/h1 0.016), bright with sub-bass trim.
- **T32** soft-to-hard: THD 0.28 → 0.94 as input level rises.
- **T33** touch cleanup: quiet THD 0.20 ≪ loud 0.84 (4.3×).
- **T34** gate: decaying note collapses to 0.38 of early level vs 0.83 non-gated.
- **T35** no spikes across a full-scale sweep (worst 0.44); clip-4 ADAA2 cuts
  alias@3k −44 dB vs naive.

## Lessons

- **Asymmetry that survives high gain needs asymmetric RAILS, not a DC bias.** A
  biased odd shaper squares up symmetrically when cranked (the asymmetry becomes
  pure DC, removed by the blocker). Different per-side knees keep it.
- **Polynomial shapers are gold for ADAA.** Recasting the fuzz's soft half as a
  cubic (vs tanh) gave exact F1/F2 → 2nd-order ADAA with no dilogarithm, −44 dB
  alias. (This is exactly the recast the handoff anticipated.)
- **Validate a new shaper's antiderivatives in isolation first.** F1'=f, F2'=F1,
  the kn=1→cubic reduction, and a maxabs sweep — *before* wiring it into the rig,
  because one NaN bricks the whole chain.
- **The Fuzz Face is mostly odd-harmonic + touch + gate**, not an octave box; the
  modest even-harmonic content (h2/h1 ~0.016) is realistic, not a miss.

Sources: ElectroSmash "Fuzz Face Analysis".
