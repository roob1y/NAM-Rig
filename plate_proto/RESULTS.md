# Plate FDN rebuild — RESULT (ear-confirmed winner, 2026-06-19)

Standalone prototype `fdnplate7.cpp` matches the real vintage plate (2.0s IR) on
every target and was ear-confirmed by Robbie ("this is a winner"). Built the
professional way: an accurate structural/physical model, with measurements used
as diagnostics — **no corrective output EQ**. See `IR_MATCHING_PLAYBOOK.md` for
the reusable method and `plate-hf-grain-rework` memory for the full worklog.

## Why the old approach failed
A single damping pole can make only ONE decay-curve shape, so the old plate was
both too short and too steeply tilted. And matching the EDR alone is not enough —
the EDR is per-bin normalised (decay timing only), so tonal balance, sub, and air
must be matched separately. Stacking EQ on an inaccurate model was the core trap.

## The model (fdnplate7.cpp defaults = the winning config)
- **32→64-line FWHT FDN**, SIZE 1.5 (modal density: smooth, dense, plate-like).
- **Per-line multi-band absorptive damping**, loss SCALED BY LINE LENGTH so all
  lines share one global T60(f); shape fit analytically (`design_damp.py`):
  `DC60 4.6822 | HS1 -0.2302@1562 | HS2 -0.2720@4834 | PK -0.1295@845 Q0.40`.
- **Driver model (input):** 2-pole LP @6k (bandwidth) + low-mid resonance
  `+5dB @220 Q0.5` (fills the native low-mid body).
- **Dense early stage:** 22 taps × 48ms, mix 0.30 (two-slope EDR front).
- **Plate low-cut @82Hz** (plates don't radiate deep sub).
- **HF-sheen air shelf +7dB @14k** (keeps top air → "open/clear").
- Output BODY/HC = 0 (no makeup EQ).

## Match vs real studio (all measured)
- **T60(f):** within ~0.06 s per band, 125Hz→11k.
- **EDR:** surface overlays (`edr_n64.png`); two-slope front present.
- **Integrated spectrum:** avg ~1 dB across 90Hz–9.5k, NO output EQ
  (`native_final.png`). Residual >1 dB only in the sub-200Hz modal ripple
  (inherent — different modes; do not chase with high-Q EQ).
- **Sub:** matched (was +6.4 dB hot → +0.1 after low-cut).
- **Air:** top octave lifted toward the studio (16k −9.7 → −4.6).

## Reproduce
```
g++ -std=c++17 -O2 fdnplate7.cpp -o fdnplate7
python3 -c "import numpy as np;a=np.zeros((384000,2),'<f4');a[10]=1;a.tofile('imp8.f32')"
./fdnplate7 imp8.f32 out.f32        # defaults = the winning config
python3 t60f.py out.f32             # T60(f) vs target + REF
python3 edr_plot.py out.f32         # EDR heatmap vs studio
python3 make_demos*.py              # loudness-matched guitar A/B
```
Use the **8 s** impulse (`imp8.f32`) — a 2 s impulse truncates the 4.4 s tail and
the T60 measurement falsely saturates.

## Demos
`plate_rebuild_demos_v5/` — `2_native-v2-air+lowcut` vs `0_reference_real-studio`,
loudness-matched, ours has no output EQ.

## Remaining (not blocking — for whenever)
- Fold this standalone prototype into `src/rig/ReverbBlock.h` (the JUCE plate).
- The mod-OFF + 3-tap output wins on the current Dattorro plate are independently
  bankable any time.
- The sub-200Hz modal ripple is inherent to any FDN vs one specific plate.
