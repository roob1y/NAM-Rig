# Matching Green Drive II to the real TS808 — without a real pedal

We don't own a Tube Screamer, and we don't need one. Every component value in the
TS808 is published, so the circuit's exact small-signal frequency response is
*derivable*. We compute it from the schematic, then fit our voicing filters to it.
This is more repeatable than miking a physical unit (no room, no mic, no
component tolerance lottery).

## The ground-truth transfer function

The voicing lives in the clipping amplifier + tone stack (small-signal = diodes
not conducting, i.e. clean / min-drive). From the
[ElectroSmash BOM](https://www.electrosmash.com/tube-screamer-analysis):

```
Clipping amp (non-inverting):  A(f) = 1 + Z2/Z1
  Z1 = R4 + 1/(jωC3)              R4=4.7k   C3=0.047µF   -> HF-boost corner ~720 Hz
  Z2 = (R6 + Rdrive) ∥ 1/(jωC4)   R6=51k    C4=51pF      Rdrive = 0..500k
Input HPF (non-inv coupling):  Hin(f) = jωR5C2 / (1+jωR5C2)   R5=10k C2=1µF (~16 Hz)
Post-clip tone-stack pole:     Hlp(f) = 1 / (1 + jω/2π·fp)    fp = 1/(2π·R7·C5) ≈ 723 Hz
H(f) = Hin · A · Hlp
```

Run it (`docs/drive/ts808_response.py`) and the result is unambiguous — and it
barely moves with the Drive pot (small-signal), which is why our voicing is
**static** (`shapeTrack 0`):

```
TS808 small-signal, normalised to 200 Hz:
 f(Hz):   50   100   200   300   500   720  1000  1500  2000  3000  5000
 dB:    -8.2  -4.6  +0.0  +2.6  +4.9  +5.5  +5.1  +3.4  +1.5  -1.4  -5.6
```

So the real hump is **+5.5 dB over the 200 Hz body, peaking ~720 Hz**, with
gentle ~6 dB/oct skirts. Not a tall, narrow resonance.

## Fitting our filters to it

Our clean voicing is `lowCut (one-pole HP) · midPeak (RBJ) · topLP (one-pole)`.
A coordinate-descent fit of those five numbers to the curve above (matching the
exact digital filter responses at 48 kHz) gives **RMS 0.66 dB**:

| param | fit | shipped v2 |
|-------|-----|-----------|
| lowCutHz | 223 → **220** | 220 |
| midHz | 820 | 820 |
| midQ | 0.68 → **0.7** | 0.7 |
| midDb | 3.6 | 3.6 |
| lpHz | 1886 → **1900** | 1900 |

Our C++ output measured back against the target:

```
 f(Hz):   50   100   200   300   500   720  1000  1500  2000  3000  5000
 target:-8.2  -4.6  +0.0  +2.6  +4.9  +5.5  +5.1  +3.4  +1.5  -1.4  -5.6
 ours:  -10.0 -4.5  +0.0  +2.0  +4.3  +5.5  +5.1  +2.7  +0.8  -2.2  -6.8
```

Within ~1 dB across the band.

## What this corrected

Our **first** v2 pass was ~**+11.5 dB** of hump over 200 Hz — roughly *double*
the real circuit. Against AmpliTube's Overscream (which is voiced nearly flat in
the mids) ours looked "more TS" because it had a hump at all — but the schematic
shows the truth is in between: a real, but modest, +5.5 dB bump. Lesson: for a
voicing question with a known circuit, fit the circuit, don't trust the A/B.

## Caveats / next refinements

- Fit is to **tone at noon** (modelled as the passive 723 Hz pole). The active
  tone network shifts the high-side shelf; our treble-shelf Tone approximates it
  and tracks the right direction (tone up levels the hump). A full active-tone
  fit per tone position is a possible refinement.
- This is the **clean/voicing** response. The driven character (frequency-
  selective clipping = bass clipped least) is handled separately by the
  pre/de-emphasis pair — see [option-a-design.md](option-a-design.md).
- The same method works for the next models: derive SD-1 / Klon / Blues Breaker
  transfer functions from their BOMs and fit.
