# Algorithmic IR Matching — Strategy & Playbook (v2, best-practice)

A reusable strategy for building an *algorithmic* reverb (FDN/plate/hall) that
matches a real impulse response. Distilled from the NAM Rig Plate-vs-vintage-plate
rebuild **and** upgraded with the rigorous techniques from the differentiable-FDN
literature (RIR2FDN, FLAMO, DecayFitNet, the Välimäki two-stage attenuation
filter, colorless/scattering matrices). The point of this doc is to **do it right
the first time** and bypass the hiccups we hit the hard way.

> v1→v2 change: where we originally hand-rolled a mechanism, the playbook now
> calls for the *rigorous* version and demotes the hand-rolled one to a labelled
> "first-pass" fallback. See `REVERB_ENGINE_V2_DESIGN.md`.

---

## 0. Core philosophy — build the model, don't EQ the symptom

Match the structure/physics first; treat every measurement as a **diagnostic**,
not as something to EQ over. Every dB a correction filter applies is a dB the
model got wrong — fix the structure. **Stacking fixes on an inaccurate model is
the #1 failure mode.** Ears are the final judge; metrics are diagnostics that must
*agree* with the ears, never replace them.

---

## 1. The FIVE independent match targets (EDR alone is NOT enough)

Each can be right while another is wrong, so match them **separately**:

1. **Decay vs frequency** — but as **early + late** decay per band (two-slope),
   not a single T60(f). See §3.
2. **EDR** — the time×frequency decay *surface*. NOTE: per-bin normalised →
   timing only, not level.
3. **Integrated magnitude spectrum** — tonal balance / the EQ curve. NOT
   constrained by the EDR; match separately. **Measure the band edges too**
   (sub <80 Hz, air >10 k).
4. **Onset & early character** — build-up envelope, echo density (NED), mixing
   time, early-field brightness.
5. **Modal density / colorlessness** — dense, smooth, *uncoloured* tail.

---

## 2. The model — best-practice components

- **Feedback matrix: scattering / colorless-optimized orthogonal**, NOT plain
  Hadamard. The colorless optimization (diff-fdn-colorless) and scattering
  matrices (Schlecht) remove the residual coloration that *is* the metallic grain,
  and give the smoothness at **lower line counts** (cheaper). *First-pass
  fallback:* FWHT/Hadamard at high N (N≥32) — works, but brute-forces smoothness
  with CPU.
- **Per-line decay filter: the Välimäki two-stage attenuation filter**, designed
  from the measured decay curve, **loss scaled by delay-line length** (so all
  lines share one global decay). *First-pass fallback:* broadband gain + a couple
  of shelves + a peak, fit numerically — approximate, misses corners.
- **Modal density:** enough that the field is smooth; with a good matrix this can
  be N=16–32 rather than 64.
- **Onset / early reflections:** plates are *instantly diffuse* (NED~1 by 20 ms) —
  no discrete early reflections; get the two-slope front from the early-decay
  split + dense diffusion / a velvet-noise early stage fit to the IR onset. A
  sparse bright early multitap is un-plate-like (harsh, kills bloom).
- **Physical device model (this is the edge the research lacks):** driver
  bandwidth roll-off, low-mid resonance, plate low-cut (sub limit), HF air shelf.
  This is what makes it *sound like the device*, not just measure like its tail.

---

## 3. Decay analysis — do it as multi-exponential + noise (NOT single-slope)

This is the biggest upgrade. A single Schroeder slope per band (−5…−35 dB) throws
away two things the ear cares about:
- the **two-slope "bloom & clear"** structure (early vs late decay differ, and the
  ratio *flips* across frequency — lows ring then clear, highs decay then linger);
- it lets the **noise floor** contaminate the late slope.

Fit each band's energy-decay curve as a **sum of exponentials + an explicit noise
term** (DecayFitNet, or a non-neural multi-exp / two-segment fit — see
`decayfit.py`). Output: **early T60(f), late T60(f), and the knee** per band — i.e.
the EDR surface *as parameters*, which then drives the attenuation filters
directly. This replaces the hand-tuned early-reflection fudge with a measurement.

*Worked example (vintage plate 2.0 s):* early/late = 125 Hz 4.56/1.77 s,
1 k 2.90/2.13 s, 8 k 1.71/3.47 s — structure the old single slope (≈1.89 s @8 k)
erased completely.

---

## 4. The measurement toolkit (in `plate_proto/`)

| tool | measures |
|------|----------|
| `decayfit.py` | **multi-slope** decay analysis (early/late T60 + knee per band) |
| `t60f.py` | 14-band single-slope T60(f) (first-pass / sanity) |
| `edrfit.py` / `edr_plot.py` | EDR numbers + heatmap surface |
| integrated octave spectrum (1/3-oct) + edges | tonal balance, sub, air |
| `profile.py` | centroid, RT60, NED, L/R corr |
| `design_damp.py` | (first-pass) analytic shelf-damping fit; replace with two-stage filter |
| `make_demos*.py` | loudness-matched guitar A/B vs IR convolution |

Build/measure offline (no JUCE) per `namrig-offline-build`. The differentiable
route (FLAMO/RIR2FDN, PyTorch) is the rigorous joint optimiser when needed.

---

## 5. The ordered workflow

1. **Profile** on all five targets, incl. the **multi-slope decay analysis** and
   the **band edges**. Use a **long impulse** (≥2× the longest tail; 8 s for a 4 s
   plate).
2. **Build the structural model**: scattering/colorless FDN + length-scaled
   two-stage attenuation filters fit to early/late decay + device model + early
   stage fit to the onset.
3. **Verify each target RAW** (no corrective EQ): decay, EDR surface, spectrum,
   onset, colorlessness.
4. **Read residuals as a map of structural deficiencies** and fix the *structure*
   (matrix, density, driver bandwidth, mode/injection distribution), not EQ.
5. **Joint-refine** the remaining residual against a combined **mel-EDR +
   echo-density + spectral** loss (derivative-free offline, or FLAMO). Reserve the
   tone-corrector GEQ for genuine broadband residual — never for modal ripple.
6. **A/B loudness-matched guitar demos** vs the IR convolution. Ears confirm.

---

## 6. The gotchas — bypass these (each cost us real time)

1. **Impulse length:** ≥8 s for a ~4 s tail; a 2 s impulse truncates → T60
   saturates (~3 s) and looks broken.
2. **EDR is per-bin normalised** → matches *timing*, not *level*. Match the
   integrated spectrum separately.
3. **Match THROUGHOUT, not averages** — the surface / per-band curves, not
   whole-IR summary stats.
4. **Decay loss must scale with delay-line length**, else longer lines ring longer
   → lumpy, inconsistent decay.
5. **One damping pole = one curve shape = always misses.** Use the two-stage
   attenuation filter fit to the measured decay.
6. **Decay is multi-slope** — fit early+late+noise, not one slope (§3). The
   two-slope front is a *measurement*, not a hand-tuned ER.
7. **Bloom is a TIME behaviour** (energy building up), not a level boost — never EQ
   it in.
8. **Don't chase the grain/flutter METRIC** — it mostly measures the inherent
   any-FDN-vs-real-IR gap. Fix grain structurally (colorless/scattering matrix),
   judge by ear.
9. **Don't chase sub-octave MODAL ripple with high-Q EQ** — it rings and makes the
   raw deviation worse. Match the smoothed (1/3-oct) curve; modes won't bin-match.
10. **Biggest native errors = missing physical structure, not a need for EQ**
    (driver bandwidth, colorless matrix, scaled decay). Fix the model.
11. **Plates are instantly diffuse** (NED~1 by 20 ms) — a sparse bright early
    multitap is un-plate-like (harsh + kills bloom).
12. **Loudness-match before any A/B**; compare against the IR *convolution* of the
    same dry source (a plate IR is 100 % wet — no direct path).
13. **Always measure the band EDGES** (sub <80 Hz, air >10 k). Plates roll off deep
    sub (low-cut) and keep gentle air to 16 k (a steep input LP kills it →
    sounds less "open/clear"). Measure sub as a broadband integral (25–70 Hz vs
    200–800 Hz), not modal-noisy single bins.
14. **Optimise jointly, not sequentially** — fixing one target by hand keeps moving
    another (darken→bloom shifts, air→mids shift). A combined loss finds the
    global compromise.
15. **Tooling:** the bash mount of the repo can be **stale** — file tools are the
    source of truth, compile from `/tmp`. Don't name a script `struct.py`. CRLF
    line endings.

---

## 7. Acceptance bar

- Integrated **smoothed (1/3-oct) spectrum within ~1 dB** across the band
  (sub-octave modal ripple exempt); **edges (sub, air) matched** too.
- **Early & late decay** per band within ~0.1 s; **EDR surface overlays**.
- **Colorless tail** (no metallic ring) — by ear and by tail spectral flatness.
- **Ears:** in a loudness-matched A/B vs the IR convolution, no obvious tell.

---

## 8. Reference implementations / tools (for the rigorous route)
- **RIR2FDN** (github gdalsanto/rir2fdn) — IR→FDN params: DecayFitNet decay
  analysis + two-stage attenuation filter + GEQ tone corrector + scattering FDN.
  *Late reverb only — it ignores early reflections (we add those).*
- **FLAMO** (github gdalsanto/flamo) — PyTorch differentiable FDN for joint fits.
- **diff-fdn-colorless** — colorless matrix/gain optimisation.
- **fdnToolbox** (Schlecht) — FDN reference (MATLAB).
- License diligence required before any commercial use of the above.

---

## Decay SHAPE is per-band, and it's convex (added 2026-06-20, from the Spring/studio spring work)

**Full-band EDC match is a TRAP — it hides per-band shape mismatches.** Matching the overall
energy decay to −15/−40/−60 dB at the right times can be a perfect average while every individual
frequency band decays *wrong*. ALWAYS verify with two diagnostics, not the full-band number:

1. **Difference EDR map** = `EDR(ours) − EDR(ref)` on a common time–freq grid, diverging colormap
   (red = ours too hot, blue = ours missing energy). Glaring gaps light up instantly.
2. **Per-band Schroeder decay overlays** = plot ref vs ours EDC for ~6 bands (lows→air) on the
   same axes. Compare the *shape* of each curve, not the endpoints.

**KEY CHARACTER TRUTH:** a real spring/plate (the studio spring) is **CONVEX in EVERY band** — a fast initial
drop, then a long, SHALLOW, low-level tail that lingers to the full decay length, at *all* frequencies
including the highs. The "linger everywhere at low level" is a core part of what the ear reads as the
real unit's character/"springiness". A single FDN decays **linearly** per band (one timescale) and
**structurally cannot sustain HF** over a long tail (an FDN's highs die early no matter the damping —
proven: 8 kHz dies ~3.8 s even full-bandwidth + integer-delay). So a single FDN can NEVER reproduce
the convex-everywhere shape.

**To get convex-everywhere you need parallel, NON-MIXING multi-timescale components across the whole
spectrum**: a fast diffuse body + a long diffuse bed + long-ringing low-level resonant fill that keeps
the MIDS and HIGHS alive to full length (not just the lows). Frequency-zone them (modal lows / diffuse
mids / HF rolloff) per the reference's own EDR zones. See [[reverb-spring-voicing]].
