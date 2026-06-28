# Violet Ram — EHX Big Muff Pi (Ram's Head '73)

Worked example of the [building-drives playbook](building-drives-playbook.md) for the
**first cascaded-clipping** drive in the rack. Added as **Fuzz model 2**
(`Violet Ram`), alongside Round Fuzz models 0/1. (The Muff is technically a diode
*distortion*, but it is marketed/perceived as a fuzz — Robbie's call to file it under
Fuzz.) Companion derivation script: [`big_muff_response.py`](big_muff_response.py).

> Era / character chosen by Robbie: **Ram's Head '73** (the violet-era, Gilmour
> Muff — smooth, bassy, scooped), **faithful 2-stage cascade**, **circuit-derived
> tone-stack scoop**, and a **moderate / controllable default with a high-gain
> "hot" ceiling built into the Drive (Sustain) range** rather than the always-
> pinned stock Muff.

---

## 1. Circuit — why the Muff is the "bigger change"

Sources: ElectroSmash *Big Muff Pi Analysis*, Coda Effects *Ram's Head clone (73)*,
Kit Rae's *Big Muff Page*.

The Muff is **four cascaded common-emitter stages**:

```
input booster -> CLIP stage 1 -> CLIP stage 2 -> passive tone stack -> recovery
```

Everything our single-shaper pedals are NOT:

- **TWO consecutive SOFT-clip stages.** Each transistor has a pair of back-to-back
  silicon diodes (1N914, ~±0.6 V) in its **collector→base FEEDBACK loop** — a soft,
  symmetric clip. Stage 1 softly clips + filters; stage 2 repeats and refines it
  into a denser, more-square wave. The two cascaded soft clips are what make the
  Muff's compressed, sustaining "wall" — a single shaper cannot reproduce it.
- **Heavy Miller-cap band-limiting around EACH clip** (470 pF feedback caps): the
  input booster rolls off ~1.2 kHz, clip stage 1 ~1.78 kHz, clip stage 2 ~1.17 kHz;
  the input caps high-pass at ~55/94 Hz. Net: everything below ~90 Hz and above
  ~1.2 kHz is attenuated 40–60 dB/dec — a **narrow, dark** band. Clipping a
  low-passed signal sounds smooth, not fizzy; this is the Muff's voice.
- **Passive tone-stack mid SCOOP.** A treble high-pass and a bass low-pass mixed by
  the Tone pot, with their crossover producing a **~1 kHz notch**. ElectroSmash's
  measured tone-noon curve: a 1 kHz notch **~6.5 dB below the shelves** (~7 dB
  overall insertion loss). Tone = a bass/treble **see-saw** across that notch.
- **Recovery stage**: flat ~+13 dB makeup for the tone-stack loss.

Ram's Head '73 specifics (vs the V3 ElectroSmash analyses): silicon clipping,
the tone-stack bass cap is **4.7 nF** (the "mids" mod swaps it to 10 nF), and the
voice is deliberately **bassy/smooth** (the violet-era, Gilmour sound).

---

## 2. Engine — a real 2-stage cascade (new, guarded, zero-fill)

The Muff needed new engine DSP. Two new `Voicing` fields (both default 0, so every
shipped model is **byte-exact** — verified across 324 configs vs HEAD):

| field | meaning |
|-------|---------|
| `muffStages` | `>1` runs the N-stage soft-clip cascade (2). `0` = single shaper. |
| `muffLpHz` | the Miller-cap low-pass applied **before each** cubic clip stage (the dark/smooth voice). |

Plus a private constant `kMuffStage2Gain` (the fixed inter-stage gain), a 2nd cubic
ADAA history pair, and three one-pole states (pre-clip LP, inter-stage HP, inter LP).

Per-sample cascade path (a new branch ahead of the single-shaper paths):

```
u = xin * preGain                  // Sustain (Drive) maps gMin..gMax (log)
u = highpass(u, lowCutHz=80)       // tighten lows pre-clip (shared pre-block)
s1 = lowpass(u, muffLpHz=1300)     // input-booster Miller LP
y1 = cubicSoftClip_ADAA2(s1)       // STAGE 1 (soft, 2nd-order ADAA)
s2 = y1 * kMuffStage2Gain          // fixed inter-stage gain
s2 = highpass(s2, 80) ; s2 = lowpass(s2, 1300)   // inter-stage HP + Miller LP
y2 = cubicSoftClip_ADAA2(s2)       // STAGE 2 (soft, 2nd-order ADAA)
c  = y2
// shared post: mid SCOOP (post-clip notch) -> top LP -> DC block -> Tone tilt -> Level
```

Each cubic stage runs **2nd-order ADAA** (peak-guarded), so the double clip never
spikes (worst |out| 0.56 across a full-scale freq/Drive sweep). The heavy pre-clip
LPs are themselves strong anti-aliasers (the harmonics are band-limited *before*
each clip), so the cascade is clean.

The clip is **type 3 (cubic soft)** — the symmetric soft knee of diodes-in-feedback.
`bias 0` (symmetric, back-to-back diodes). No clean blend, no emphasis (the Muff is
a wall, not a transparent feedback-clip OD).

---

## 3. Voicing — fit to the circuit

Derived + fit in `big_muff_response.py`:

- **Mid SCOOP** = a static post-clip RBJ notch fit to ElectroSmash's **measured**
  tone-noon response (the published curve is ground truth where it exists): `midHz
  1000, midQ 0.80, midDb −6.5` → matches the 1 kHz / −6.5 dB notch to **RMS 0.22 dB**.
  (A full nodal solve of the passive network is in the script as a cross-check — it
  confirms the scoop lives at ~0.7–1.3 kHz, but its exact interior depth is very
  sensitive to the poorly-specified source/load impedances, so we fit the measured
  curve.) `midPost 1` (post both clips, like the real tone stack), `shapeTrack 0`
  (static — the tone stack is a fixed network).
- **Band-limiting**: `lowCutHz 80` (the clip-stage HP 55/94 Hz), `muffLpHz 1300`
  (the Miller LPs, applied pre each clip), `lpHz 1600` (a gentle post LP — recovery
  is flat but we keep it dark). Measured small-signal voice (re 300 Hz): full lows
  (150 Hz −0.4 dB), scooped mids (1 kHz −9.9 dB), dark top (3 kHz −22 dB, 5 kHz
  −35 dB) — the narrow, bassy, dark Muff signature.
- **Tone** = the engine **see-saw tilt** at `pivotHz 1000` (the scoop centre): noon =
  flat (the scoop shows through), CCW = bass/dark, CW = bright/scooped-out. This is
  the real Muff Tone (a bass LP / treble HP blend), not the RAT "Filter" or a TS
  treble shelf. (The softPoly bass-fix is bypassed for the cascade so the full
  see-saw applies.)

---

## 4. Range / feel — moderate default, hot ceiling (Robbie's brief)

A stock Muff is *always* a saturated wall (the Sustain knob mostly changes sustain,
not steady-state tone). Robbie asked for **more usable range**: a controllable
default, the full hot/sustaining Muff reachable at max.

Tuned by measurement (`kMuffStage2Gain 2.0`, `gMin 3 / gMax 55`, `outTrim 0.80`):

- **Sustain (Drive) 0** — THD ~0.05 (SC): a controllable, lightly-broken-up floor.
- **noon** — THD ~0.18 (SC) / ~0.25 (HB): both stages clipping, a solid Muff.
- **max** — full saturation + **~4.6× louder/denser output** + maximum sustain:
  the high-gain "hot" ceiling, reached on the knob.

The inter-stage gain was lowered from a first pass (3.5) so **both** stages'
saturation progresses across the whole knob travel, instead of pinning by noon.
Output is **A/B level-matched** to Black Rodent II (noon RMS 0.446 vs 0.443).
Input-level dependent / calibration-referenced (a humbucker drives it clearly
harder than a single-coil: THD 0.28 vs 0.07 at the same low Sustain).

Auto-gain table `D2` measured (pink noise): compresses as both stages saturate.

---

## 5. UI

Fuzz's `bModel` is 0..3 (no widening — Fuzz was 2/4). The processor already reads
`bModel` for Fuzz, and the Type menu / model dropdown are generic, so the Violet Ram
appears automatically. The catch: the **Fuzz category had no Tone knob** (the Round
Fuzz models pin Tone to noon), but the Muff's Tone (the mid-scoop see-saw) is a
signature control. So:

- A new **`fTone`** param was added to the Fuzz block (Round Fuzz models ignore it —
  the processor pins their tone to 0.5; only the Muff, `bModel == 2`, reads `fTone`).
- Panels.h `configure()` case 4 (keyed on `model == 2`): the Muff shows **Sustain /
  Tone / Volume** with the Tone knob visible; the Round Fuzz models stay Fuzz / Volume
  (Tone hidden).

The `fTone` param is new (unreleased dev branch, so no preset drift).

---

## 6. Tests (drive_test.cpp T52–T57) + build

- **T52** existing Fuzz models byte-exact; Fuzz holds 3 / Distortion back to 2; scoop
  is a notch (midDb < 0); cascade enabled (muffStages 2).
- **T53** the Muff voice: full lows, ~10 dB mid scoop, ~22 dB-dark top.
- **T54** the cascade **compresses**: input ×8 → output ×1.14.
- **T55** dual-ADAA cascade **never spikes**: worst |out| 0.56, all finite.
- **T56** Tone see-saw (bass CCW, treble CW).
- **T57** input-level dependent (humbucker drives harder).

Offline build **green: 96 CHECKs, 0 failures**, and a full **324-config regression
byte-exact vs HEAD** (every shipped Boost/OD/Dist/Fuzz model — the new fields zero-fill
and the Muff just moved categories). UI/processor edits are JUCE (reviewed by hand —
captions/comments + the new `fTone` param + a model-keyed branch identical in shape to
the Klon's). **Pending commit on Windows + play-test.**

Model inventory now: Boost 4, Overdrive 4, Distortion 2 (Black Rodent / II), **Fuzz 3**
(Round Fuzz / II / **Violet Ram**). New reusable engine bits: the **N-stage soft-clip
cascade** (`muffStages` + `muffLpHz` + `kMuffStage2Gain`) for any multi-stage clipper.
