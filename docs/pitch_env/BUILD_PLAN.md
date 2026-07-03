# Pitch effects + Envelope filter — build plan (LOCKED 2026-07-03)

Two new effect families for NAM Rig: **pitch effects** (octaver, detune, harmonizer,
whammy-style bend) and **envelope filter** (auto-wah). Grounded in `docs/pitch_env/RESEARCH.md`
(circuit + DSP research + verification). Matches the house style: JUCE-free core DSP, voiced
like real pedals, per-effect fixed sweet spots, each block behind an offline `tests/*_test.cpp`,
default-bypassed for byte-exact preset regression.

**Target genres: 2000s radio/alt rock + indie/acoustic.** Reference pedals chosen to serve both
ends — gnarly (Whammy, Octavia, OC-2, FX25) and clean/textural (POG, MicroPitch detune).

## Locked decisions (2026-07-03)

- **Envelope filter placement: FIXED, before the compressor** — it tracks the *raw* guitar
  dynamics, which is what the real pedal senses. No switch (a squashed signal into an auto-wah
  is the inauthentic version).
- **Pitch = ONE block with a Type toggle**, built in phases; the user switches engine.
- **Whammy: static Interval knob first**; expression-pedal / MIDI-CC / LFO sources added later.
- **Synth effects = OUT OF SCOPE here**, parked for a future family (see nam-rig backlog).

## Chosen reference pedals (voice against these, controlled-probe / A-B method)

| Block | Reference(s) | Genre role |
|---|---|---|
| Envelope filter | **DOD FX25** (primary) + **EHX Q-Tron** (2nd voice) | Frusciante/RHCP clean-funk auto-wah |
| Sub-octave | **Boss OC-2** | Muse riff-thickening (rock) + clean acoustic octave-down |
| Octave-up | **Tycobrahe/Roger Mayer Octavia** | garage/stoner octave-fuzz (White Stripes, QOTSA) |
| Detune | **Eventide MicroPitch** | indie/acoustic lush doubling + widening |
| Whammy | **DigiTech Whammy** | 2000s leads/dive-bombs (Morello, Bellamy, White, Greenwood) |
| Poly octave | **EHX Micro POG** | indie/acoustic clean octaves, organ, faux-12-string |
| Harmony | (optional, later) Boss PS-6 / EHX Pitch Fork | indie-folk twin lines |

---

## 1. Chain position

Current chain (`src/rig/RigChain.h`):

```
[mono pre]  gate -> comp -> drive(3-slot) -> premod ->
   split -> [A: amp->eq->cab] [B: ampB->eqB->cabB] -> mix ->
[stereo post]  mod -> delay -> reverb
```

Both new blocks are **dynamic / pre-amp** effects → mono pre section, and both track raw
dynamics so they go **before the compressor**:

```
gate -> pitch -> envfilter -> comp -> drive(3-slot) -> premod -> split -> ...
```

Pitch first (front of chain, raw attack for the trackers/dividers), then envfilter (raw
dynamics), then comp. Two new `MonoBlock`s added to `allMonoBlocks()` and the `process()` order,
each reporting `latencySamples()`. Default-bypassed → SoloA stays bit-exact.

---

## 2. Envelope filter — `EnvFilterBlock` (PHASE 1, build now)

Every serious analog auto-wah is the same core — envelope follower → two-integrator
state-variable filter — and the *character is the control element* (RESEARCH.md §A0–A4). Build
one engine; get the voicings from modeling the control element.

### Engine

```
in -> [envelope follower] --exp map--> cutoff
in ---------------------------------> [resonant SVF @ cutoff, LP/BP/HP/notch] -> wet
                                        wet/dry blend -> out
```

- **`src/rig/Svf.h`** — a **TPT/ZDF (Zavalishin) state-variable filter**, JUCE-free, unit-tested.
  LP/BP/HP taps + HP+LP = notch. Not `Biquad.h`: RBJ coeffs zipper/destabilise under per-sample
  cutoff sweeps; the TPT SVF stays tuning-accurate and stable at high Q + fast modulation, and
  cutoff/Q are orthogonal.
- **Envelope follower** — rectify → one-pole attack/release (separate up/down coeffs). Factor a
  small shared `EnvFollower` (CompBlock uses the same idea). FX25 = full-wave; Q-Tron = half-wave.
- **Cutoff map** — exponential, `fc = fc_min * 2^(k*env)` (map env → log2(fc), not linear Hz).
  Up = louder→brighter; Down = invert.
- **Control-element lag** — a *separate* one-pole lag on the control voltage models the
  photocell/CMOS smoothing = the pedal's smoothness. (FX25 = OTA, near-zero lag → smooth the
  detector instead; Q-Tron = photocell → real lag.)

### Voicings to ship (2, both real distinct circuits)

| Voice | Reference | Control-element model | Feel |
|---|---|---|---|
| **Clean/snappy** (default) | DOD FX25 | OTA: exp map, ~no lag (ripple-on-you → smooth detector, keep fast attack), full-wave detect | tight, fast, clean, low-noise |
| **Vocal/smooth** | EHX Q-Tron | photocell: exp map + slow lag (tens of ms), half-wave detect, Q → self-oscillation | smooth, wide, vocal, can scream |

Q-Tron adds the **notch** mode (HP+LP). Controls: Sensitivity, Range (sweep centre), Resonance
(Q), Attack, Depth, Mode (LP/BP/HP/notch), Direction (Up/Down), Dry/Wet. Ranges: cutoff
~100 Hz–3 kHz, Q gentle→self-osc, attack ~5–20 ms, release ~100–300 ms.

**Test `tests/env_filter_test.cpp`:** follower attack/release constants; cutoff tracks a step
monotonically; **SVF stable at max Q under a full-scale cutoff sweep** (no NaN — must never trip
RigChain self-heal); LP/BP/HP/notch tap correctness; bypass = passthrough.

---

## 3. Pitch — `PitchBlock` (one block, Type toggle, phased)

Type enum selects the engine; only that type's controls show (as ModBlock/PreModBlock do).

### Phase 2 — analog dividers (no tracking, O(1), zero latency) — RESEARCH.md §B0–B3
- **Sub-octave (OC-2):** detection LPF (~800 Hz–1 kHz) → Schmitt comparator w/ hysteresis →
  `state ^= 1` per rising edge = OCT1, cascade = OCT2. For OC-2 smoothness, multiply the dry
  (or half-wave-rectified) audio by the toggle ±1 (sign-flip alternate cycles), not the bare
  square. Output LPF + envelope-follow from dry. Mix DIRECT/OCT1/OCT2. Covers rock riffs +
  clean acoustic octave-down.
- **Octave-up (Octavia):** fuzz first (asymmetric germanium-ish clip) → `abs()`/asymmetric
  full-wave rectify → AC-couple → tone LPF. Ring-mod-like on chords by design.
- Shared: note-confidence gate (mute quiet/ambiguous → no motorboating), hysteresis "tightness"
  knob, honest low-note warble.

### Phase 3 — granular delay-line (no tracking, low latency) — RESEARCH.md §C1
One crossfaded variable-delay engine → **Detune** (indie/acoustic thickening) + **Whammy** (rock).
Puckette: `f = (β − 1)·fs/w` (β = 2^(semi/12)); two taps a half-period apart, half-sine window,
4-point cubic interp, `w` ≈ 40–60 ms. Detune = tiny β (warble negligible); Whammy = swept β
(knob first). Controls: Interval, Fine (cents), Grain, Mix; Whammy sweeps β from the knob.

### Phase 4 — poly octave (POG-style) — RESEARCH.md §B6, §C3
Clean glitch-free poly octaves for indie/acoustic (organ, faux-12-string, octave-down under
acoustic). Fixed-integer ratios (×2/×4/×0.5/×0.25) → exploit for a clean octave-locked OLA or a
peak-locked phase vocoder (N=2048, 75% overlap). Per-voice mix + Attack onset envelope +
resonant LPF are the character controls. Higher latency/CPU than phases 2–3.

### Phase 5 — PSOLA/WSOLA harmony (optional, needs tracker) — RESEARCH.md §C2, §D
Clean mono lead/twin-line harmony, formant-preserved, gated on tracker confidence.

### Pitch tracking — reuse the Tuner (for phases 4/5)
`src/rig/Tuner.h` already implements **MPM/NSDF** (octave-up-robust — right for distorted guitar,
free clarity value, no prefilter, ~2-period window). Factor `src/rig/PitchTracker.h` with an RT
incremental mode (per-hop 256–512, seed lag ±1 semitone from last estimate, clarity-gate).
Shared by tuner + harmony. ~20–25 ms latency floor at low E (physics).

**Test `tests/pitch_test.cpp`:** divider = exact f/2 & f/4 on steady tones; granular hits β
within tolerance on a sine sweep & stays stable; poly/PSOLA hit target pitch; all engines
bypass = passthrough, never non-finite.

---

## 4. Wiring checklist (per block)

1. Header in `src/rig/`, `: public MonoBlock`, JUCE-free core (+ `Svf.h`, `PitchTracker.h`).
2. Member in `RigChain`; insert into `allMonoBlocks()` + `process()` order; report
   `latencySamples()` (0 env filter + dividers; ~w granular; ~1 period PSOLA; FFT+hop poly).
3. APVTS params in `PluginProcessor` (like drive/premod); `FactoryPresets.h`.
4. UI panel in `src/ui/Panels.h` + strip tile (per-type control visibility); reuse dropdown LnF.
5. `tests/*_test.cpp` offline harness; passes before ship.
6. Default = bypassed (SoloA regression gate).

## 5. Build order (LOCKED)

1. **`EnvFilterBlock`** (FX25 + Q-Tron) — Phase 1, now.
2. **`PitchBlock` dividers** (OC-2 + Octavia) — Phase 2.
3. **`PitchBlock` granular** (Detune + Whammy, knob) — Phase 3.
4. **`PitchBlock` poly** (POG-style) — Phase 4.
5. **`PitchTracker.h` + PSOLA harmony** — Phase 5 (optional).

Model: **Opus** for the pitch DSP. Voice via the controlled-probe / A-B method (drives/reverbs).

*Full detail + sources + verification: `docs/pitch_env/RESEARCH.md`.*
