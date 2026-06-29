# Dallas Rangemaster → Range '65 II (worked example)

A third end-to-end run of the [building-drives-playbook](building-drives-playbook.md),
after the TS808 ([circuit-accuracy.md](circuit-accuracy.md)) and the ProCo RAT
([proco-rat.md](proco-rat.md)). The Rangemaster is the *simplest* of the three to
fit — and a clean demonstration that the playbook's "fit the circuit, the gain
range is input-level dependent" rule is what actually matters, not more filters.

Shipped as **Boost model 2 "Range '65 II"** (the original "Range '65" stand-in and
"EP Boost" are kept byte-for-byte). Source: `src/rig/DriveBlock.h` `boost[]`;
derivation: `docs/drive/rangemaster_response.py`; tests: `tests/drive_test.cpp`
T21–T25.

## The circuit (ElectroSmash analysis)

One PNP **common-emitter germanium** stage (Mullard **OC44**), positive-ground,
battery only. The whole thing:

| part | value | role |
|------|-------|------|
| C1 | 5 nF | **series input cap** — the entire audio-band voicing |
| R1 / R2 | 470 k / 68 k | base bias divider (Vb ≈ 7.8 V) |
| R3 | 3.9 k | emitter resistor (bias stability) |
| C3 | 47 µF | **bypasses R3** → fixes the midband gain |
| Rc = RV | 10 k | collector resistor *is* the Volume pot |
| C2 | 10 nF | output coupling cap |

**Voltage gain** (Volume maxed, C3 bypassing R3): `Gv = gm·Rc = 0.008 · 10k ≈ 80`
= **38 dB**. High for a booster — it drives hard.

**Frequency response** = three high-pass corners, only one in the audio band:

- **C1 into the ~12 kΩ input impedance** → `fc = 1/(2π·5n·12k) ≈ 2.65 kHz`. This is
  the treble boost: everything below 2.6 kHz rolls off at 6 dB/oct, flat above.
- C2 into ~1 MΩ load → 15.9 Hz (sub-audio). C3 over R3 → 0.8 Hz (sub-audio).

So the clean voicing is a **single 1st-order high-pass**, *flat above 2.6 kHz, no
resonant peak, no top roll* — it is a TREBLE booster, the output stays bright. The
"warmth" everyone hears is the **asymmetric germanium soft-clip** compressing the
boosted treble transients, *not* a low-pass. The deliberately off-centre bias
(Vb ≈ 8 V, not the 7 V centred max-headroom point) makes the clipping asymmetric →
even harmonics.

## The fit (`rangemaster_response.py`)

The target curve and our digital one-pole high-pass coincide to **RMS 0.01 dB** —
because our `lowCutHz` one-pole HP *is* the analog input-cap RC high-pass:

```
Rangemaster target (dB re 5 kHz):
   50    100    200    500   1000   2000   2650   5000   8000
 -33.4  -27.4  -21.4  -13.6  -8.0   -3.3   -1.9    0.0   +0.6
best-fit  lowCut=2650  (no peak, no LP)  RMS 0.01 dB
```

Input-cap **mod** = our Range switch (already in `applyRange`, left untouched so
models 0/1 stay byte-exact):

| C1 | corner | Range position |
|----|--------|----------------|
| 5 nF | 2653 Hz | Treble (stock) |
| 10 nF | 1326 Hz | Mid |
| 47 nF | 282 Hz | Full |

## The voicing (what actually changed vs the stand-in)

```
{"Range '65 II", clip 0 (tanh), gMin 4, gMax 80, lowCut 2653, no peak, no LP,
 bias 0.30, outTrim 0.50, shapeTrack 0 (static), midPost 0 (pre-clip HP), hasRange}
```

The corners were already right in the stand-in. The substance of the rework:

1. **gMax 20 → 80** — the real `Gv ≈ 80`. The stand-in was ~4× too low (the exact
   bug the early TS had): max Drive barely broke up. Now it drives like the real
   +38 dB booster. *(measured: at full Drive, model 2 THD 0.45 vs the stand-in's
   0.09 — 5× harder.)*
2. **bias 0.20 → 0.30** — models the Rangemaster's deliberately off-centre operating
   point. *(measured h2/h1 0.113 vs the original 0.045 — 2.5× the even-harmonic
   content; germanium warmth.)*
3. **High-pass is PRE-clip** (`midPost 0`, the input cap is before the gain) and
   **static** (`shapeTrack 0`, the cap network is fixed) → bass reaches the clipper
   attenuated, so bass clips least and the full-gain treble clips most. The
   booster's frequency-selective grind falls out of the topology for free.
4. **outTrim 0.50** level-matches the stand-in within ±17 % RMS across the Drive
   sweep (the two can't match tighter — different gain structures — but they A/B
   fairly at typical settings). Boost has no Tone/Level knob in the UI (the
   processor pins Tone to 0.5), so `outTrim` is the only thing setting its push.

## Verification (`drive_test.cpp` T21–T25)

- **T21** models 0 (Range '65) byte-for-byte unchanged; category now has 3 models.
- **T22** treble-boost high-pass (3 k ≫ 150 Hz, 9.6×) + Range switch moves the
  corner (300 Hz: Full 7× louder than Treble).
- **T23** germanium asymmetry: h2/h1 0.113 > the original's 0.045.
- **T24** the real gain range: drives 5× harder than the stand-in at full; and
  input-level dependent (humbucker drives harder than single-coil).
- **T25** no spikes across a full-scale sweep, all drives and all ranges.

Auto-gain (off by default) table `B2` measured against the same pink-noise
reference the existing B0/B1 tables use (`rms_in/rms_out`), so switching Boost
models with Auto Gain on stays level-consistent.

## Lessons reinforced

- **The gain range is the fix, not more filters.** The Rangemaster's whole EQ is
  one RC corner we already had right; making it *sound* right was the `Gv ≈ 80`
  gain (input-level dependent, fixed clip threshold) + the germanium asymmetry.
  Same lesson as the TS's `gMax 33 → 80`.
- **A treble booster stays bright.** No de-emphasis, no top roll — the output is
  meant to be trebly; the warmth is the soft-clip compression, derived separately
  from the clean small-signal curve.
- **Add, don't edit.** Shipped as model 2; the stand-in is preserved for A/B.

Sources: ElectroSmash, "Dallas Rangemaster Treble Booster Circuit Analysis".
