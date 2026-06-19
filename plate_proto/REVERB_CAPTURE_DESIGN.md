# Reverb Capture — feature design sketch

Turn the one-off manual IR match (our Plate-vs-studio work) into a **repeatable
feature**: point at any reverb IR → out comes a small, fully-tweakable algorithmic
preset that matches it and runs cheap (algorithmic, not convolution).

This is the Neural-Amp-Modeler idea applied to reverb — which NAM itself
*cannot* do (it falls over on time-based effects) — but with a crucial twist:
the capture is a handful of **parameters you can keep tweaking**, not a frozen
black box.

---

## 1. The product shape (the NAM-style split)

| | NAM (amps) | Reverb Capture (us) |
|---|---|---|
| Offline | train neural model from amp recording | fit FDN+filter params from an IR |
| Artifact | `.nam` weights (black box) | small **preset** (~30–50 numbers, tweakable) |
| Runtime | cheap neural inference | cheap **algorithmic FDN** (our `fdnplate`) |
| Tweakable? | no | **yes** — decay, size, tone, pre-delay, mix |

The asset is the **capture format = a parameter set**, not the IR. It's tiny,
tweakable, CPU-light, and (unlike shipping the IR itself) far more defensible to
distribute — see Legal below.

**Two halves:**
- **Offline Capture tool** — IR in → fit → preset out. (Python; not real-time.)
- **In-plugin runtime** — ONE universal algorithmic engine (our `fdnplate7`:
  dense FDN + length-scaled multiband damping + driver model + ER + low-cut +
  air) that any preset drives. Already built and ear-validated.

---

## 2. What the research gives us (and what it doesn't)

The academic state of the art already does most of the *late-reverb* fit, open
source:

- **RIR2FDN** (DAFx24, Dal Santo et al.) — *IR → FDN parameters, perceptually
  matched.* Pipeline = **DecayFitNet** (estimate per-band decay from the energy
  decay curve) → **two-stage attenuation filter** (Välimäki — the in-loop
  frequency-dependent decay, == our length-scaled damping) → **graphic-EQ tone
  corrector** (out-of-loop spectral match, == our GEQ/driver tone stage) →
  **colorless-optimized scattering FDN**. This is *our exact decomposition*,
  arrived at independently.
- **FLAMO** (ICASSP25) — PyTorch library for differentiable FDNs; the engine to
  gradient-descent-fit params to a target.
- **diff-fdn-colorless / Sony diffvox / fdnToolbox** — supporting differentiable
  FDN optimizers and an FDN reference toolbox.

**What they DON'T do — our edge:**
1. **RIR2FDN explicitly ignores early reflections / onset** ("should be used to
   synthesize only the late reverberation part"). Our pipeline matches the
   **two-slope EDR front + onset**, which is a big part of perceived character.
2. **No physical device model.** We model the **plate driver bandwidth, low-mid
   resonance, sub roll-off, HF air** — the things that fixed "harsh / no bloom /
   not open / too much sub" by ear. That's the difference between "technically
   matched late tail" and "sounds like the plate."
3. **No real-time, tweakable, CPU-cheap C++ runtime** aimed at a guitar plugin.
   RIR2FDN renders offline in MATLAB; FLAMO is research PyTorch.
4. **No ear-in-the-loop product workflow.**

So: the math is published (good — it's a recipe, not a research risk); the
**product** (capture → tiny tweakable preset → cheap on-brand runtime, with ER +
physical voicing + curated captures) is the open space.

---

## 3. Two build tracks

### Track 1 — MVP, deterministic, ships on OUR toolkit (no ML)
Automate the heuristic pipeline we just proved by hand, end to end:

1. **Profile** the IR: onset/pre-delay, 14-band **T60(f)**, **EDR** surface,
   integrated 1/3-oct **spectrum**, **sub** (25–70 Hz), **air** (>10 k), NED.
2. **Fit** params from the profile (each step already exists as a script):
   - damping ← analytic fit to T60(f) (`design_damp.py`)
   - DC decay/size ← T60 at DC; ER ← EDR two-slope front
   - driver bandwidth + low-mid ← spectrum; low-cut ← sub; air shelf ← top octave
3. **Emit** a preset (the param schema below).
4. **Auto-grade**: render the preset, measure residual vs the IR on all targets,
   report pass/fail against the playbook bars (T60 ±0.1 s, spectrum ≤1 dB
   smoothed, EDR overlay, sub/air matched).
5. Optional **ear A/B** via the existing loudness-matched harness.

Pros: buildable in our current C++/Python stack, deterministic, fast, no PyTorch.
Cons: heuristic; assumes a plate/room-class topology; may need per-class presets
(plate vs room vs hall) for the fitter's starting guesses.

### Track 2 — General, gradient-descent (FLAMO)
For arbitrary IRs (halls, chambers, springs) where heuristics struggle:
- Build the fit in **FLAMO**, constraining the differentiable FDN topology to
  **match our runtime engine** (so fitted params render identically in-plugin).
- Loss = **mel-EDR** (decay surface) + **echo density** + **spectral** terms —
  i.e. our five targets expressed as a differentiable loss.
- Add our **ER/onset** and **physical-model** params to the optimized set.

Pros: general, jointly optimal, less hand-tuning. Cons: PyTorch infra; must
constrain topology to the cheap runtime; per-capture optimization time; **license
diligence required** (see below).

**Recommendation:** ship **Track 1** first (plate + room), reuse everything we
built; run **Track 2** as parallel R&D for generality. The runtime engine and the
capture format are shared, so Track-1 work is not throwaway.

---

## 4. The capture format (the contract)
A small, versioned param schema = the only thing that crosses from offline fitter
to in-plugin engine. Roughly:
```
size/density (N, SIZE) · DC decay (T60) · damping curve (shelf/peak set)
· ER (count, spread, mix) · pre-delay · driver (bandwidth, low-mid)
· low-cut · air shelf · width · [user macros: decay, tone, size, mix]
```
Lock this early — it's the stable interface, and the thing users tweak + share.

---

## 5. Productization (JUCE)
- **UX:** drag an IR (or record a sweep → deconvolve) → "Capture" → a tweakable
  preset loads into the rig's reverb block.
- **Factory captures:** ship curated captures of plates/rooms/springs we own or
  have rights to (or record ourselves).
- **Community captures:** users capture + share *presets* — the Tone3000/NAM
  community-library analogy, but for reverb, and tweakable.

---

## 6. Legal / IP (flag — NOT legal advice)
- A **parameter set fitted from an IR is not the IR**, and is generally more
  defensible to distribute than the sample itself — but this needs **real legal
  review**, and source EULAs must be respected (e.g. the studio IRs we used:
  do not ship).
- Cleanest moat: a capture ecosystem built on **self-recorded / owned / licensed**
  reverbs — ship our own factory captures, let users capture their own gear/spaces.
- **Open-source diligence required before shipping:** RIR2FDN's main repo has
  **no license** (MATLAB) → treat as all-rights-reserved, not usable as-is
  commercially; its submodules and FLAMO have their own licenses to check. The
  *method* is published and reimplementable; the *specific code* may not be
  redistributable. Verify before depending on any of it.

---

## 7. Risks / unknowns
- **Topology transfer:** the offline fitter MUST target our exact runtime engine
  or params won't render the same. (Constrain FLAMO's FDN to our structure.)
- **Class generality:** plates are tractable; springs (dispersive), long halls,
  and gated/odd IRs need Track 2 and possibly extra structure.
- **Capture UX:** sweep recording + deconvolution + denoising for users without a
  clean IR.
- **Match-vs-tweakability:** the more we expose tweak macros, the looser the exact
  match — by design; that's the selling point.

---

## 8. Next concrete steps
1. License diligence on FLAMO + RIR2FDN submodules (gates Track 2).
2. Build the **Track-1 orchestrator** (profile → params → auto-grade) by wiring
   the scripts we already have; test on 3–4 IRs (plate, room, hall, spring).
3. **Lock the capture param schema** (Section 4) — the offline↔runtime contract.
4. Prototype a **Track-2 FLAMO fit** constrained to our topology on one IR;
   compare to Track-1.
5. Decide MVP scope (likely: plate + room capture, factory + user-capture).

See `IR_MATCHING_PLAYBOOK.md` for the matching method and acceptance bars this
feature automates.
