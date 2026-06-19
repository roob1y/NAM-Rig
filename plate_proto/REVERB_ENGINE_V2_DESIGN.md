# Reverb Engine v2 — rigorous redesign

The v1 engine (`fdnplate7.cpp`) matches the studio and is ear-confirmed, but several
of its parts are **hand-rolled approximations** of techniques the literature does
more rigorously. This is the clean redesign: replace the "kinda good" mechanisms
with the best-practice ones, and **keep** the genuine edge the research lacks
(early reflections / onset, the physical device model, by-ear validation).

Goal: same-or-better match, **cheaper** runtime, and a **fit pipeline that is
analytic instead of eyeballed** — so it can become the repeatable Reverb Capture.

---

## 1. Keep / replace, component by component

| Component | v1 (what we did) | v2 (do it right) | Why it's better |
|---|---|---|---|
| **Feedback matrix** | FWHT / Hadamard | **Scattering / colorless-optimized** orthogonal matrix (Schlecht; diff-fdn-colorless) | removes residual spectral coloration (= the grain) at the source |
| **Modal density** | brute-force **N=64** | aim **N=16–32** with a better matrix, matched smoothness | big CPU saving in the plugin |
| **Decay analysis** | single-slope T60(f), −5…−35 dB | **multi-exponential + noise** per band (DecayFitNet-style); **early + late** decay times | captures the two-slope "bloom & clear"; strips noise-floor contamination |
| **Damping filter** | broadband gain + 2 shelves + 1 peak, random-searched | **Välimäki two-stage attenuation filter** (GEQ-accurate), length-scaled | accurate across the *whole* curve incl. corners; designed not searched |
| **Two-slope / bloom** | hand-tuned ER multitap | **driven by the early/late decay split** from the analysis | analytic, not eyeballed (this was our hardest hand-fight) |
| **Tone match** | hand-tuned GEQ + driver | **GEQ tone-corrector designed from the measured spectral residual** | one-shot, not iterated by ear |
| **Optimization** | sequential, per-target, by hand | **joint** optimization vs a combined **mel-EDR + echo-density + spectral** loss (derivative-free offline, or FLAMO/PyTorch) | global compromise — kills the whack-a-mole where fixing one target moved another |
| **Early reflections / onset** | hand-tuned 22-tap stage | **fit the early part of the IR** (velvet/sparse FIR) + NED/mixing-time target | *our edge* — research ignores ER; make it analytic too |
| **Physical device model** | driver bandwidth + low-mid resonance + sub low-cut + air shelf | **KEEP** | *our edge* — this is what makes it "sound like the plate," not just measure like its tail |
| **Validation** | loudness-matched by-ear A/B vs IR convolution | **KEEP** | final arbiter; the metrics only ever earned trust by agreeing with it |

---

## 2. v2 signal flow

```
input
  → DRIVER MODEL        : bandwidth roll-off + low-mid resonance   (device model — our edge)
  → EARLY/LATE SPLIT    : early stage fit to the IR's first ~50–80 ms (velvet/sparse FIR)
  → FDN
       feedback matrix  : SCATTERING / colorless-optimized orthogonal
       per-line filters : TWO-STAGE ATTENUATION (Välimäki), length-scaled,
                          fit to the EARLY+LATE decay times per band
  → OUTPUT
       low-cut (plate low-frequency limit) + air shelf (HF sheen)
       GEQ tone-corrector designed from the integrated spectral residual
  → wet
```

The two-slope EDR is no longer a hand-tuned ER on top of a single-decay tank — it
comes from (a) the early/late attenuation per band and (b) an early stage fit to
the real onset. The ER stage stops being a fudge and becomes a measured fit.

---

## 3. The capture / fit pipeline (analytic, in order)

1. **Decay analysis** — multi-exp + noise per band → early/late T60(f) + knee
   (this IS the EDR surface, as parameters). *Validated:* `decayfit.py` on the
   studio shows early/late split flipping across frequency (lows 4.56→1.77 s,
   highs 1.71→3.47 s) — structure the old single slope erased.
2. **Two-stage attenuation filters** designed from the decay curve, length-scaled.
3. **Scattering / colorless matrix** constructed (and optionally optimized).
4. **Early stage** fit to the IR onset (velvet/sparse FIR) + match NED/mixing time.
5. **Tone-corrector GEQ** computed from the integrated spectral residual (one shot).
6. **Joint refine** against mel-EDR + echo-density + spectral loss
   (derivative-free offline now; FLAMO/PyTorch for the differentiable version).
7. **Device model** (driver/sub/air) + **by-ear A/B** vs the IR convolution.

---

## 4. What's buildable offline NOW vs the ML route

**Offline now (our C++/numpy stack, no PyTorch):**
- Multi-exponential decay fit (✓ prototyped, `decayfit.py`).
- Välimäki two-stage attenuation filter (closed-form design).
- Scattering matrix construction; colorless check via tail spectral flatness.
- GEQ-from-residual (we already do this).
- Derivative-free joint refine over the small param set (extend our random search).
- Device model + by-ear harness (done).

**ML route (later, FLAMO/PyTorch):**
- Full differentiable joint optimization (gradient descent on all params).
- Neural DecayFitNet estimator — substitute our non-neural multi-exp fit for now.

So ~90% of the rigor is reachable offline without ML; the differentiable joint
fit is an upgrade, not a prerequisite.

---

## 5. Build order

1. **Decay analysis** — done & validated (`decayfit.py`).
2. **Two-stage attenuation filter** — implement + verify it hits the early/late
   curve more accurately than our shelf fit.
3. **Scattering matrix experiment** — does it match v1 smoothness at N=16–32?
   (CPU win + colorless tail.)
4. **Wire the v2 engine**; fit to the studio via the pipeline above; **auto-grade**
   against the playbook bars (T60 ±0.1 s, spectrum ≤1 dB smoothed, EDR overlay,
   sub/air matched).
5. **By-ear A/B** vs v1 and the studio — v2 ships only if it's at least as good.

v1 (`fdnplate7`) stays the working baseline until v2 beats it by ear. No mistakes
baked in: every replaced component must prove itself against v1 + the studio first.

See `IR_MATCHING_PLAYBOOK.md` (overhauled) for the method, and
`REVERB_CAPTURE_DESIGN.md` for how this engine becomes the capture feature.
