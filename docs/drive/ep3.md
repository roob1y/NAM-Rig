# Echoplex EP-3 preamp → EP Boost II (worked example)

The fourth end-to-end run of the [building-drives-playbook](building-drives-playbook.md),
after the TS808, ProCo RAT and [Rangemaster](rangemaster.md). The EP-3 is the
useful counter-example: the circuit fit comes out **flat**, which is itself the
finding — the EP-3's character is headroom + input impedance + JFET harmonics, not
an EQ. We voice it as the **Xotic EP Booster** (the recognizable "EP Boost" pedal),
which adds the gentle presence shaping on top.

Shipped as **Boost model 3 "EP Boost II"** (models 0–2 kept byte-for-byte). Source:
`src/rig/DriveBlock.h` `boost[]`; derivation: `docs/drive/ep3_response.py`; tests:
`tests/drive_test.cpp` T26–T29.

## The circuit (AionFX tracing + masterplex/effectslayouts BOM)

A single **JFET common-source** stage (original Maestro EP-3 used a **TIS58**;
clones use **2N5457 / BF245 / J201**, sorted by IDSS because FET spread is huge).

| part | value | role |
|------|-------|------|
| Cin | 22 nF | series into the gate (the "gain cap" people swap) |
| Rgate | ~2.2 M | gate bias → very high input Z (no tone-suck) |
| Cshunt | 220 pF | gate-to-ground, a whisker of HF roll (>70 kHz) |
| Rd | 22 k | drain resistor |
| Rs | 3.3 k | source resistor, **unbypassed** |
| Cout | 100 nF | output coupling → 500 k Volume |

Runs on an internal **22 V** (charge-pumped) for headroom. **Gain** (source
unbypassed): `Av = Rd/(Rs + 1/gm) ≈ 22k/3.8k ≈ 5.8`, loaded by the volume pot →
**~+11 dB**. A fixed, high-headroom **clean** boost.

There are three well-known takes (all AionFX-traced): the **Secret Preamp / Axion**
(pure part-for-part EP-3, no second stage), the **ClinchFX EP-Pre / Ares** (EP-3 +
output buffer), and the **Xotic EP Booster / Ephemeris** (EP-3 + extra tone-shaping,
the most "usable"/recognizable). "EP Boost" as a pedal means the Xotic.

## The fit (`ep3_response.py`) — the flat finding

The **pure EP-3** small-signal response is flat to ±0.1 dB across the audio band:

```
Pure EP-3 (dB re 200 Hz):
   50    100    400    800   1500   3000   5000   8000  12000
  -0.0   -0.0   +0.0   +0.0   -0.0   -0.0   -0.0   -0.1   -0.1
```

Cin/Rgate HPF sits at ~3 Hz, the source is unbypassed (no treble shelf), the 220 pF
roll is >70 kHz (it sees the guitar's ~10 kΩ source impedance, **not** the 2.2 M —
a real trap: charge it through Rgate and you'd wrongly predict a 330 Hz rolloff).
So the EP-3's "magic" is **not** an EQ: it's clean headroom, the very high input Z
(keeps the guitar's own top that a lossy chain would lose), and a subtle JFET
2nd-harmonic. It is **full-range** — the exact opposite of the Rangemaster's
treble-only high-pass.

We voice EP Boost II to the **Xotic EP Booster**: full bass + a gentle broad
**presence high-shelf**, fit by our low-Q pre-shaper peak to **RMS 0.35 dB**:

```
Xotic target:  +0.2 (400) +0.9 (800) +2.0 (1500) +3.6 (5000) +3.9 (12k)
ours (fit):    lowCut 15, midHz 5000, midQ 0.35, midDb 4.1
```

## The voicing

```
{"EP Boost II", clip 0 (tanh), gMin 1.3, gMax 6, lowCut 15, midHz 5000, midDb 4.0,
 midQ 0.35, bias 0.10, outTrim 0.74, shapeTrack 0, midPost 1, no LP, no range}
```

- **Full-range** (`lowCut 15`) + a **gentle broad presence rise** (low-Q peak,
  ~+3.3 dB measured at 5 kHz) — the Xotic shelf, not a narrow bell. The opposite of
  the Rangemaster's high-pass.
- **High headroom, mostly clean.** `gMin 1.3 / gMax 6` with a soft tanh clip:
  measured THD 0.8 %→5 % (single-coil) and 2 %→18 % (humbucker) across the Drive
  sweep — clean through most of it, a little JFET hair only when cranked.
- **JFET even-harmonic warmth** from a small off-centre `bias 0.10` (h2/h1 ≈ 0.033
  when pushed — gentle, far below the Rangemaster's 0.113).
- `outTrim 0.74` level-matches the original EP Boost stand-in within ±15 % across
  the sweep. Boost has no Tone/Level knob in the UI (Tone pinned to 0.5), so
  `outTrim` sets the level and the presence shelf is fixed (`shapeTrack 0`).

## Verification (`drive_test.cpp` T26–T29)

- **T26** model 1 (EP Boost) preserved; Boost now has 4 models.
- **T27** full-range + presence: 80 Hz ~unchanged vs 200 Hz, +3 dB by 5 kHz, and it
  passes 4.5× more bass than the Rangemaster's high-pass.
- **T28** stays clean at noon (THD 0.018, less than half the Rangemaster's) with
  mild JFET even-harmonic warmth.
- **T29** no spikes across a full-scale sweep.

## Lessons reinforced

- **A flat fit is a result, not a failure.** When the circuit-derived curve is flat,
  the honest answer is "the voicing isn't an EQ" — here it's headroom + input Z +
  JFET harmonics. Don't manufacture a hump that isn't in the circuit.
- **Watch which resistance a cap actually sees.** The 220 pF charges through the
  ~10 kΩ guitar source, not the 2.2 M gate bias — a 20× error in the corner if you
  grab the wrong node.
- **"EP Boost" the pedal ≠ the bare EP-3.** The recognizable sound is the Xotic's
  gentle presence shaping over the flat EP-3 stage; we voice to that, and note the
  pure-EP-3 flat alternative for anyone who wants it.

Sources: AionFX (Axion JFET Preamp / Ares / Ephemeris tracing journals);
effectslayouts & low-poly-studio "MasterPlex" build docs/BOM.
