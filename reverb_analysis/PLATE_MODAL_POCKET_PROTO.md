# Residual B — FDN modal-pocket rebuild: prototype recipe + results (2026-06-21)

This is the live continuation point for Residual B (and, since they share a root cause,
Residual A). Read `PLATE_HANDOFF.md` §7 first. `plate_proto/` is **gitignored → clears every
session**, so the reproducible recipe + the winning parameters live HERE (tracked).

## Why this exists
Residuals A (extreme-long 11k ring) and B (flat low-mid EDT plateau @ 250-710Hz) are the SAME
problem: the reference is a real plate with UNEVEN modal pockets; our 32-line FDN has smooth,
monotonic line spacing + heavy dispersion → a smooth tail that can neither (B) hold a sustained
low-mid early decay nor (A) sustain a sparse high mode. Confirmed this session: A is unreachable
by EQ/damping (a positive top-shelf wrecks centroid + destabilizes — see RESULTS_plate.md). The
only fix is to give the FDN genuine modal-pocket DOF = detune line lengths + lighten diffusion.
The earlier parallel-resonator add-on FAILED (t=0 spike + tonal bumps); THIS approach does not.

## Reproduce the isolated proto (NEVER edit the shipped header to prototype)
```
cd plate_proto                                  # recreate if cleared
mkdir -p inc/rig stub/juce_audio_basics
cp ../src/rig/ReverbBlock.h ../src/rig/Blocks.h ../src/rig/Lfo.h ../src/rig/Biquad.h inc/rig/
echo '#pragma once' > stub/juce_audio_basics/juce_audio_basics.h
cp ../reverb_analysis/impulse.f32 .
g++ -std=c++17 -O2 -Iinc -Iinc/rig -Istub ../reverb_analysis/render_character.cpp -o render_proto
# baseline render MUST equal the live anchor (byte-identical) before any edit.
python3 measure_proto.py baseline   # helper script below
```
`measure_proto.py` prints EDT(250-710) + T30err + centroid + modal + L/R corr vs the 1.5 anchor.
(Recreate it from the snippet in RESULTS_plate.md / this session if cleared.)

## STEP 1 DONE — GEOMETRY LOCKED (2026-06-21). seed 837575, mag 0.06, dispG=dispG2=0.40
Replaced the first arbitrary exp2 jitter with a proper search: `search_geom.py` randomly samples
line-length jitter (BASE×(1+mag·rng(seed).uniform(-1,1,32))) + dispersion, scoring EDT-plateau
closure against firm T30/centroid/modal/corr constraints (runtime overrides via env `PLINES`/
`PDISPG`/`PDISPG2` so no recompile — ~2.4s/candidate; log in `search_results.csv`, 65 candidates).
Phase-2 deep-eval of the top 5 (full C80/side-mid battery + 11k@4.5) → **LOCKED seed 837575**
(best balance: T30err 0.054 = shipped baseline, modal 6.93 lushest, lowest side/mid disruption).
The locked line set + dispersion is in **`reverb_analysis/plate_locked_geometry.txt`** (tracked) and
is now the proto's no-env default. Tools persisted: `search_geom.py`, `phase2.py`, `finalists.py`.
```
// dispG = 0.40, dispG2 = 0.40
kLineMs = {6.16, 7.53, 9.0, 10.47, 12.16, 13.32, 14.71, 17.02, 18.7, 20.69, 22.45, 24.48, 26.47,
           27.91, 28.63, 30.19, 32.19, 36.45, 35.57, 38.02, 40.61, 43.36, 45.47, 46.48, 51.68,
           50.44, 51.93, 57.03, 55.21, 60.92, 65.38, 68.49};
```
LOCKED results at anchor: EDT 250-710 = **2.51/2.56/2.56/2.50** (ref 2.48/2.59/2.52/2.47 — plateau
CLOSED, was flat 2.40), T30err 0.054, centroid 4529 (ref 4560), modal 6.93 (lush), corr −0.03,
stable (peak 0.033). C80-low barely moved from shipped (−0.8/+0.6 vs shipped −0.8/+0.1); side/mid
drifted only ~0.2dB → small voicing re-fit ahead. Residual A unchanged (11k@4.5 ~1.65) — needs the
dedicated high-mode line, NOT this jitter. NEXT = step 2 (re-fit C80-low + side/mid to locked modes).

### (historical) first probe exp2 — seed 7, ±6%, dispG 0.45/0.40 — proved the plateau is closable
```
kLineMs = {6.6, 8.28, 9.5, 10.25, 11.81, 14.31, 14.3, 17.45, 19.16, 20.02, 21.28, 22.97,
           24.56, 26.92, 29.02, 31.0, 34.64, 35.82, 37.14, 40.76, 39.12, 40.86, 45.3, 44.24,
           46.27, 51.29, 53.18, 58.49, 58.9, 60.5, 62.78, 63.32};
```

### Results at the 1.5 anchor (vs shipped baseline → exp2 → reference)
- EDT 250/355/500/710: baseline FLAT 2.40/2.39/2.39/2.41 → exp2 **2.42/2.54/2.52/2.44** →
  ref 2.48/2.59/2.52/2.47. **The plateau is essentially filled** — this is the headline win.
- T30err(allband) 0.053 → 0.058 (held). Centroid 4539 → 4495 (ref 4560, fine). Modal 6.8 (held,
  still lush). L/R corr −0.03 (fine). No t=0 spike, no narrowband tonal bumps.

### The cost (this is the "re-opens the whole match" the handoff warned about)
Detuning the lines moved the voicing that was OVER-FIT to the OLD line lengths:
- C80 125Hz: −0.77 (≈ref −1.06) → **−2.11** (regressed ~1.0dB). C80 250 also off ~0.5dB.
- side/mid: was d<0.15dB all bands → now d up to **−0.71dB (125)**, **+0.48dB (8k)** — the side
  widener + 2k narrower were tuned to the old modes.
- NED@40ms 0.97 → 0.91 (lighter dispersion = slightly fewer early reflections).
These are RE-FITTABLE (re-tune mEReso1/2/3, mEarly*, the side widener/narrower to the new lines).

### Residual A is NOT incidentally fixed
11k T30 at long knobs stayed flat ~1.67 (ref 2.40/4.30/5.95/6.60 @ 3.0/3.5/4.0/4.5). The ±6%
jitter doesn't create the sparse LONG-RINGING high mode the ref has. A needs either stronger/
targeted detuning of the SHORT lines (which set the high modes) or a dedicated sparse high-mode
line with light damping. Still the harder half.

## Progress (the real rebuild)
1. ✅ DONE — OPTIMIZE/LOCK geometry: search_geom.py (65 candidates) + phase2 → seed 837575
   (mag 0.06, dispG=dispG2=0.40). See the LOCKED block above + `plate_locked_geometry.txt`.
2. ✅ DONE — RE-FIT voicing: mEReso1 6.8→5.5, mEReso2 −3.8→−5.2 → C80 maxdev 1.03→0.70, tonal
   unchanged, side/mid ≤0.25 (one band 125 +0.43), T30err 0.047, plateau still closed. Env hooks
   ER1/ER2/ER3 left in the proto for future tuning.
3. ✅ DONE — VALIDATE across all 10 knobs: decay-scaling/damping STILL HOLDS with the new modes;
   T30(all) per knob within noise of shipped (3.5/4.0 slightly better), centroid tracks ref
   everywhere. (Numbers in `plate_locked_geometry.txt`.)
4. ⏳ NOW — EAR A/B ([[reverb-voicing-ab-method]]): loudness-matched demos built at
   `plate_modalpocket_demos/{1_anchor-knob1.5, 2_long-knob3.0}/` — each has 0_reference-convolved,
   1_ours-shipped, 2_ours-modalpocket (guitar_mix_38pct-wet.wav + guitar_wet-only.wav). Robbie's
   ears decide whether the modal-pocket Plate is GO before porting.
5. ⏳ TODO (after ears) — PORT to live `src/rig/ReverbBlock.h`: byte-level CRLF edits (kLineMs +
   kDispG/G2 + mEReso1/2 gains), verify CR==LINES, re-verify the 2.45 anchor, save the diff.
   Residual A (11k high-mode line) is a SEPARATE later round — geometry jitter does not fix it.

STATUS: steps 1-3 COMPLETE in the proto — EDT plateau CLOSED, full anchor battery back to banked
quality, all 10 knobs validated. Awaiting ear A/B (step 4). Shipped engine UNTOUCHED (byte-identical
to the saved plate_voicing.diff this session). Reproduce proto: recipe at top; geometry+voicing in
`plate_locked_geometry.txt`; the proto's no-env default already = locked geometry + re-fit.
