# Reverb Engine v2 — build results (session 2026-06-19)

Progress on `REVERB_ENGINE_V2_DESIGN.md` build order. v1 (`fdnplate7.cpp`) stays
the ear-confirmed baseline; every replaced component was tested against v1 + the
vintage plate before any adoption call. Headline: **the rigorous methods work and
are now built offline, but on the *smooth* studio curve they do not beat the
already-good v1 — their real value is generalization (Reverb Capture) and the CPU
headroom to afford the accurate filter.**

## 1. Decay analysis — `decayfit.py` (rebuilt + validated)
The tool was missing from the repo (only its results survived in the docs); rebuilt
as a two-segment (early/late) piecewise-linear fit to the Schroeder energy-decay
curve with a scanned knee + per-band noise-floor truncation (the non-neural stand-in
for DecayFitNet's multi-exp+noise fit).

- **Early T60(f) reproduces the documented values** within ~0.1 s every band
  (125 Hz 4.53 vs 4.56, 1 k 2.92 vs 2.90, 8 k 1.64 vs 1.71).
- **The early/late flip is reproduced** (early falls 4.5→1.5 s with frequency, late
  rises) — "lows ring then clear, highs decay then linger."
- The *absolute late* magnitudes are milder than the doc's DecayFitNet numbers and
  are method/noise-sensitive (the playbook already flags the high-freq late tail as
  partly recording noise floor). Robust, ear-relevant part is solid; treat late as
  qualitative. Curve saved to `decay_targets.txt`.

## 2. Two-stage attenuation filter — `geq_twostage.py` / `geq_export.py` (built)
Accurate cascade GEQ (Välimäki–Liski) with an interaction-matrix gain solve,
structured Jot/Schlecht-style as a **broadband gain floor + 16-band GEQ shaping the
residual loss** (the floor guarantees attenuation at every frequency incl. DC/Nyquist
→ stable; total loop loss stays ≤ −0.78 dB across 0–Nyquist). Length-scaled per line.

- **Predicted T60(f) accuracy on the studio:** v2 mean 0.006 s / max 0.029 s vs v1's
  3-biquad random search 0.016 s / 0.124 s — ~3× better mean, ~4× better max, and
  **designed analytically, not eyeballed.**
- **BUT the *rendered* T60(f) is identical to v1** (both within measurement noise of
  the studio; both share the same ~0.2 s 8 k overshoot from the ER/air/measurement, not
  the damping filter). So on this smooth target v2 is *not* an audible upgrade.
- **The real win is generalization.** On a deliberately non-monotonic target (mid
  resonance bump + dip — the arbitrary-IR case Reverb Capture must handle), v2 GEQ
  hits mean 0.025 / max 0.098 s while v1's fixed 3-biquad structure cannot follow it
  (mean 0.082 / max 0.207 s). See `v2_filter_accuracy.png`.
- Wired into `fdnplate8.cpp` (= fdnplate7 with the GEQ replacing the 3-biquad
  cascade); renders stable, matches the studio.

## 3. Scattering / colorless matrix experiment — `fdnplate9.cpp` / `flat.py`
Matrix-configurable FDN (delays spread across the full prime range at any N; optional
full orthogonal matrix vs FWHT). Colorlessness measured as the dB std of the late
tail's fine spectral structure relative to its smoothed envelope (isolates modal
coloration from tonal balance). Lower = denser/smoother. v1 (N=64 FWHT) = **6.01 dB**.

| N | FWHT | best random-ortho | Householder |
|---|------|-------------------|-------------|
| 16 | 7.18 | 7.00 | 10.07 |
| 24 | — | 6.39 | — |
| 32 | 6.28 | **6.16** | 8.02 |
| 64 | 6.01 | 5.83 | 6.20 |

- **At equal N, a random-orthogonal matrix ≈ FWHT** (Householder is worse). Just
  swapping the matrix type is *not* a free win — colorlessness is driven by N
  (density).
- **N=32 nearly matches the ear-confirmed N=64** (6.16 vs 6.01, +0.15 dB) → a plausible
  ~2× saving on lines/biquads, which is what makes the heavier 16-biquad GEQ
  affordable. **N=16 is clearly grainier (7.0, +1 dB)** and would need the actual
  gradient-based *colorless optimization* (diff-fdn-colorless / FLAMO, the PyTorch
  route) — not reachable with a hand-built matrix offline.
- The metric is a proxy; **ears decide** (per the playbook's grain-metric warning).

## 4. By-ear A/B set — `plate_v2_demos/` (`make_demos_v2.py`)
Loudness-matched guitar (synthetic DI) through: `0` studio convolution, `1` v1 N64,
`2` v2 N64 GEQ, `3` v2 N32 GEQ (FWHT), `4` v2 N32 GEQ (random-ortho), `5` v2 N16 GEQ.
`guitar_mix_38pct-wet.wav` + `guitar_wet-only.wav` in each. Awaiting Robbie's verdict:
(a) is v2-N64 indistinguishable from v1? (b) is N32 indistinguishable from N64? (c) is
N16 audibly grainier?

## 5. Reverb Capture Track-1 pipeline — `capture.py` / `grade.py` / `fdnplate10.cpp` (built)
The deterministic orchestrator from `REVERB_CAPTURE_DESIGN.md`: **IR in → engine
params out, analytically**, then auto-graded vs the IR. `fdnplate10.cpp` = fdnplate8
with the 16 GEQ command gains loadable from a file (`FDN_GUNIT`) so a capture drives
the whole engine (DC60, damping, driver bandwidth, low-cut, air, tone-corrector all
parameterised). Pipeline: profile → measure T60(f) on 16 bands → two-stage GEQ damping
design → driver/low-cut/air from the spectrum edges → render → tone-corrector GEQ for
the broadband residual → auto-grade. Adaptive impulse/window length (avoids the
truncation gotcha on long tails).

**Validation (fully automatic, no hand-tuning), graded vs each studio IR:**

| IR | T60(f) mean / max | spectrum 200-8k mean (after tone) |
|----|-------------------|-----------------------------------|
| 0.5 s | 0.024 / 0.124 s | 1.70 dB |
| 1.0 s | 0.042 / 0.110 s | 0.76 dB |
| 2.0 s | 0.087 / 0.105 s | 0.81 dB |
| 4.5 s | **~1.0 s (fails)** | 3.4 dB |

- **Works for moderate plates (0.5–2.0 s):** reproduces the hand-tuned match within the
  playbook bars (T60 ≈0.1 s, spectrum ≈0.8 dB after a 4–5-band tone-corrector). The
  capture is fully automatic.
### 5a. Steep-tilt under-damping — diagnosed & largely FIXED (cuts-only damping)
The 4.5 s capture (T60(f) **13→2 s, a 6:1 tilt**) first rendered with the mids
**under-damped ~1.3 s** (1 kHz 5.30 s vs target 3.93). Root-caused by elimination —
it is **not** group delay (1–2 samples), **not** the ER stage, **not** per-line
length-scaling (independent per-line design predicts identically), **not** measurement
leakage (early-decay EDT is off too), and the broadband floor alone is **exact**
(flat-target render = dead-flat 4.00 s every band). The cause: the unconstrained
least-squares GEQ uses **boosts** at some bands to sculpt the steep tilt, and a
**boosted band sustains and contaminates the decay** in the render. Since the GEQ sits
on the longest-T60 floor it should only ever **cut** — switching to a **cuts-only**
projected-gradient fit (auto-enabled when tilt ≥ 4:1) brings the mids to **±0.3 s**
(1 kHz 4.23 vs 3.93). Gentle/moderate IRs keep the unconstrained fit (which is slightly
better for them), so **0.5–2.0 s are unchanged** (0.029 / 0.042 / 0.105 s).
**Residual:** on the 6:1 tilt the broad Q=0.7 cut skirts now slightly **over-cut the
lows** (62 Hz 10 s vs 13.5 s) — a single broad-band-GEQ FDN can't perfectly realise a
6:1 tilt.

### 5b. The real resolution — cap the captured decay at the reverb's max range (Robbie)
The "6:1 tilt" was largely an artefact of **what we were targeting**. The 4.5 s studio's
low band genuinely rings ~10–13 s to −35 dB (SNR 100 dB+, not noise) — a deep sub-bass
tail — but Robbie's call: **the IR sets the CEILING of our reverb's decay range
(~4.5 s); we should not chase the sub tail beyond it.** So `capture.py` now (a) cleans
the measured T60(f) to monotonic-non-increasing above 355 Hz (kills HF noise-floor
bumps like a spurious 11 k reading) and (b) **caps the per-band target at a max decay**
(`CAP_MAXT60` env; auto-default 1.5×T60(1 kHz) to catch runaways without biting normal
IRs). With the cap at 4.5 s the 4.5 s render **tops out at 4.55 s** with a clean gentle
4.5→2.5 s tilt and the **mids within ~0.27 s** — the 5.30 s mid runaway is gone, and the
moderate IRs (0.5–2.0 s, cap doesn't bite) are unchanged. The max-decay is really a
**capture parameter** = the top of the Decay knob's range for that character. *(Note:
the auto-grader compares to the RAW IR's 13 s low tail, so it still shows a large
"error" there by design — grade the capped target / by ear instead.)*

## Recommendation
- **Adopt the two-stage GEQ filter for the Reverb Capture pipeline** (analytic +
  generalizes to any IR). For the studio plate specifically, v1's filter is already
  fine — no urgency to swap the shipped plate.
- **N=32 is the CPU sweet spot** that lets us afford the accurate per-line GEQ; don't
  drop to N=16 without the differentiable colorless-matrix route.
- **Next build steps:** joint optimization (mel-EDR + echo-density + spectral loss,
  derivative-free) and the early-stage velvet/sparse-FIR fit to the onset — then the
  full FLAMO/PyTorch differentiable fit for the colorless matrix + N=16 question.
