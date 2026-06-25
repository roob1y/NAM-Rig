# Current `DriveBlock` — pre-rework baseline (June 2026)

What the engine does *today*, before the Option A overdrive rework. Source:
`src/rig/DriveBlock.h` + `tests/drive_test.cpp`.

## Structure

- **3-slot series rack**, shared pre-split (one board feeding both rigs).
- Categories (`Kind`): Off, Boost (2 models: Range '65, EP Boost), Overdrive
  (1: Green Drive), Distortion (1: Black Rodent), Fuzz (1: Round Fuzz).
- Per slot: Drive, Tone, Level, on/off footswitch, model, range switch
  (treble-boost only). Optional global **Auto Gain** (off by default).

## Per-slot signal path

```
drive gain -> pre low-cut (one-pole HP) -> mid/treble peak (RBJ biquad)
           -> waveshaper (1st-order ADAA, double) -> post low-pass
           -> DC blocker -> tone tilt -> level
```

`shapeTrack` lets the low-cut + mid bloom *with the Drive knob* (so the OD is
flat at Drive 0 and humps as you turn up). `midPost` puts the peak after the
clipper (level-stable tone stack) for OD/Dist, before it for the treble booster.

## The three base shapers

| type | shape | used by | antiderivative F1 (for ADAA) |
|------|-------|---------|------------------------------|
| 0 | `tanh` (soft) | Boost, Overdrive | `logcosh` |
| 1 | hard clip ±1 (symmetric) | Distortion | piecewise quadratic |
| 2 | hard clip, asymmetric rails | Fuzz | piecewise |

## Anti-aliasing — 1st-order ADAA

```
y = (F1(x1) − F1(x0)) / (x1 − x0)      (midpoint fallback for tiny dx)
```

Evaluated in **double** (float cancels and crackles at small signal). One sample
of state, zero latency, all-Off rack bit-exact.

## What the Overdrive (Green Drive) is today

A memoryless **`tanh`** waveshaper with Drive-tracked EQ:
`{clip 0, gMin 1.5, gMax 30, lowCut 560, mid 780Hz +6dB Q0.7, lp 1300,
bias 0.05, pivot 720, outTrim 1.10, shapeTrack 1, midPost 1}`.

### Gaps vs the real TS (what the rework fixes)

1. **Flat waveshaper, not feedback clipping.** Clipping is *not* frequency-
   selective — bass clips the same as mids. The real TS clips bass least.
2. **No clean blend.** Output is pure shaped signal; the TS keeps part of the
   clean input → we lose the dynamics/touch.
3. **`tanh` blocks cheap 2nd-order ADAA.** `tanh`'s 2nd antiderivative needs the
   dilogarithm. To go 2nd-order cheaply we switch the OD core to a **cubic
   soft-clip** (polynomial F1 *and* F2).
4. **1st-order ADAA only.** Still fizzes when cranked.
5. **No dynamics.** Response depends only on the Drive knob, not pick attack.

## Test coverage today (`tests/drive_test.cpp`)

T1 Off bit-exact · T2 OD mid-hump · T3 treble-boost brightness · T4 ADAA alias
cut (Fuzz) · T5 ADAA preserves low-freq · T6 Fuzz asymmetry > Dist · T7 tone
tilt · T8 OD@drive0 full-range · T9 OD@drive1 mid-hump bloom · T10 Dist bloom.

The rework keeps all of these and adds tests for 2nd-order alias reduction,
frequency-selective clipping, and the dynamics envelope (see
[option-a-design.md](option-a-design.md)).
