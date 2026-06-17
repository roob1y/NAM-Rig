# Plate — Phase 2: voice the FDTD mesh to the vintage plate TS (Solid-State)

Status: **draft / not yet implemented.** Phase 1 (the FDTD thin-plate mesh) is committed
(`f9bf4a5`, branch `feature/reverb-characters`) and passes `reverb_test.cpp`. It sounds like an
accurate plate but **incredibly low-endy**. This doc is the plan to fix that and voice it to the
vintage plate TS. Do not touch the other characters.

---

## 1. Diagnosis (measured, not guessed)

The committed engine reads **raw plate displacement `u`** at the pickup nodes. Measured band balance
of the current impulse response (decay 3 s, damp 7 kHz):

| window | <80 | 80–200 | 200–500 | 500–2k | 2–6k | 6–14k | centroid |
|---|---|---|---|---|---|---|---|
| first 20 ms | 59% | 39% | 2% | 0% | 0% | 0% | — |
| full 1.5 s | 56% | 42% | 2% | 0% | 0% | 0% | 185 Hz |

Almost nothing above 500 Hz **even at the onset**, so this is *not* a decay-time imbalance and *not*
a grid/bandwidth cutoff (the mesh's modes actually run to ~16 kHz at μ=0.22, ~20 kHz at μ=0.24).

**Root cause:** a force-driven plate's *displacement* response rolls off ≈ −12 dB/oct at HF (each
mode is a 2nd-order resonator; displacement ∝ 1/ω²). A real vintage plate's **piezo contact pickups sense
bending strain / acceleration**, i.e. ∝ ∇²u or ∂²u/∂t², which adds ≈ +12 dB/oct and restores the
brightness. We modelled the steel but not the pickup.

### The fix, measured

Same IR, different readout transform at the pickup node:

| readout | centroid | balance |
|---|---|---|
| displacement `u` (current) | 242 Hz | all <200 Hz — the bug |
| velocity ∂u/∂t (+6 dB/oct) | 2.4 kHz | even, full-range |
| **curvature ∇²u (piezo strain)** | **2.6 kHz** | even; lows present, not dominant ✓ |
| acceleration ∂²u/∂t² (+12 dB/oct) | 7.1 kHz | too thin/harsh |

**Curvature (∇²u) is the authentic and best-balanced choice** and it's essentially free — `mLap` at
the pickup is already computed each step. Velocity is the cheap fallback. Final voicing is likely
curvature + a gentle tilt (below).

---

## 2. vintage plate TS target (the real unit)

The Solid-State stereo version (vintage plate TS / FB-TS, vintage plate 162 transistor amp, two off-centre contact
pickups, centre transducer, input compressor before the drive coil). Targets to hit:

- **Decay vs frequency is the signature.** Undamped ≈ 5 s @ 500 Hz, **rising toward the lows**
  (lows ring *longest*) and **declining to ≈ 1.5 s @ 10 kHz**; HF never exceeds ~1.25–1.5 s at any
  damper setting. The decay control sets the **midrange** (≈ 500 Hz–3.5 kHz), spec range ~1–4.5 s.
- **Level balance is bright / mid-forward** even though lows ring longer in *time* — the piezo
  pickups are bright and the plate/transducer sheds lows. (This is the curvature readout + tilt.)
- **Instant onset, instant echo density**, no build-up; a slight HF "sheen", not metallic.
- **Frequency-dependent stereo width** (key vintage plate trait, per Valhalla): the L/R onset time difference
  is <1 ms at HF (keeps transients sharp) and widens to >10 ms at LF (gives depth / "behind the
  speakers"). Our two-node pickup geometry already produces this via dispersion — verify and tune.

---

## 3. Change list (prioritised, mapped to `PlateReverb` in `src/rig/ReverbBlock.h`)

1. **Pickup readout → curvature (∇²u).** Replace `mU[pickL/R]` with the Laplacian at the pickup
   (use `mLap`/`mLap1`, already computed). Re-tune `kOutGain` (curvature magnitude differs). Keep
   the fractional-mod path (read curvature with bilinear interp). This is the primary fix.
2. **Output voicing EQ / tilt.** After the readout, a gentle low-shelf cut (~ −3 to −6 dB below
   ~250 Hz) + slight presence lift (~2–6 kHz) to land the centroid and match the bright-but-smooth
   vintage plate balance. Tune against the band table above. Cheap one-pole shelves.
3. **Decay-vs-frequency curve.** Calibrate so the *mid* (~500 Hz–3.5 kHz) equals the Decay knob,
   the lows ring a bit *longer* (the vintage plate "rise"), and 10 kHz lands ≈ 1.5 s regardless. σ₀ sets the
   baseline; σ₁ (the ∇²-derivative loss) already shortens HF — re-map `setDampHz`→σ₁ so 10 kHz hits
   the target, and consider a mild LF-favouring term for the low-end rise.
4. **σ₁ stability clamp (from review #2).** Enforce a stability-aware bound so the explicit σ₁ term
   can't destabilise: keep `μ_eff = (κk + 2σ₁k)/h² ≤ ~0.24` (clamp σ₁, or reduce effective μ).
   Needed once μ→0.24 and if the damp floor drops below 500 Hz.
5. **Freeze: σ₀→0 *and* σ₁→tiny floor (from review #3).** Currently freeze keeps σ₁, so a frozen
   tail darkens to an LF drone. Drop σ₁ to a small floor (not exactly 0 — pure lossless is only
   marginally stable and the Nyquist mode can accumulate). Re-verify a long (>30 s) freeze.
6. **μ 0.22 → 0.24.** Free HF headroom and less numerical dispersion; safe because mesh state is
   `double`. Pair with item 4.
7. **(Optional, authentic) input drive compressor.** The vintage plate 162 puts a compressor before the drive
   coil. A gentle soft-knee on the mesh input would add the real unit's drive behaviour.
8. **(Optional) frequency-dependent stereo trim.** If the natural L/R-vs-frequency delay needs
   shaping, nudge pickup spacing / add a tiny HF-only Haas trim. Verify before adding.

Boundaries stay **simply-supported** (current zero-border). Not free (no edge-flutter), and SS is
the standard plate choice; clamped (∂u/∂n=0) is only a voicing experiment, not a fix.

---

## 4. Verification / acceptance (extend the `/tmp` harness, then `reverb_test.cpp`)

- **Band balance:** centroid in the low-kHz; energy spread across bands with lows present but not
  dominant (target the curvature row above), at several Decay/Damp settings.
- **Decay vs frequency:** mid ≈ Decay knob; 10 kHz ≤ ~1.5 s; lows ≥ mid (the vintage plate rise).
- **Echo density** crosses 0.5 within 30–50 ms (review #4) — currently ~1.0 by 50 ms ✓.
- **Group delay** negative slope (HF arrives earlier) — currently 424 ms@300 Hz → 391 ms@9 kHz ✓.
- **Stereo:** L/R correlation < 0.6; widening L/R delay toward LF.
- **Stability:** 30 s noise + long freeze finite/bounded with the new σ₁ clamp and freeze floor.
- **Regression:** keep `reverb_test.cpp` green — re-check T2 (predelay shift stays exact with the
  curvature readout — still LTI, zero until arrival), T8 peak<8 (recalibrate `kOutGain`), T10/T11/T17.

---

## 5. CPU / deferred

Engine is ~790 M mul-add/s (38×26, heaviest character). The readout/EQ/stability changes add
negligible cost. If a full rig gets tight, drop `kNx`/`kNy` (density scales with node count). All of
the above is deferred to the next work session; nothing here is implemented yet.

---

### Sources
- Sean Costello, "The Physics and Psychophysics of Plates," Valhalla DSP —
  https://valhalladsp.com/2015/11/08/the-physics-and-psychophysics-of-plates/
- "vintage plate Plate Reverb" (specs, vintage plate TS / vintage plate 162, decay-vs-frequency, dimensions), Vintage
  Digital — https://www.vintagedigital.com.au/emt-140-plate-reverb/
