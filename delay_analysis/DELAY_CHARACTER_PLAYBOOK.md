# Delay Character Playbook — reading the battery verdict

How to judge whether **Tape Echo** and **Space Tape** match their measured references,
using `delay_render.cpp` (renders the real `DelayBlock`) + `delay_battery.py`
(ours-vs-reference tables + overlaid graphs). Analogue of
`reverb_analysis/REVERB_CHARACTER_PLAYBOOK.md`. Ears are the final judge; the
metrics only localise problems and render A/B candidates.

## The one architecture lesson (preserve it)
The tape EQ is split into two stages and they must be measured separately:
- **PER-PASS (in-loop):** head bump + gap-loss. Re-applied every repeat, so it
  **compounds down the tail**. This is what darkens/blooms the echoes.
- **OUTPUT-ONCE:** outBass low-shelf + preamp high-shelf. Applied once at output,
  **does not recirculate**, so it colours timbre without compounding.

A single-repeat fit ALONE gives the wrong tail, because it folds both stages
together. You must fit the in-loop transfer from a **multi-repeat tail capture**.
The battery does exactly this: it derives the once-stage as `single-repeat − per-pass`.

## The metrics, and how to read each
1. **Single-repeat spectrum** (one isolated echo, 1/6-oct, norm @1 kHz) — the tonal
   fingerprint. Shows the *net* low-mid (bump + once-stage bass shelf together) and
   the HF roll. Match the SHAPE after the @1k anchor.
2. **Per-pass transfer** `rep[n+1]/rep[n]`, normalised @1k — THE in-loop metric, the
   one most often gotten wrong. The bump should sit as a positive low-mid hump; the
   gap-loss as the HF droop. The raw (un-normalised) level is the per-pass decay
   (≈ feedback in dB); the panel title prints it.
3. **Output tilt** = single − per-pass — the once stage. Tape should read **bass
   CUT** (−6 @480) + a **bright HF lift** (+4 @1.4k); Space should read **bass
   BOOST** (+2.5 @180) and a flat top (preamp off).
4. **Gap-loss slope:** Tape ≈ **−12 dB/oct (2-pole)**, Space ≈ **−6 dB/oct (1-pole,
   brighter/gentler)**. Read the slope across the octave above the −3 dB corner, not
   just the corner Hz (the corner estimate is coarse; the slope is the discriminator).
5. **Saturation:** output burst growth vs input step (the level sweep). Look for the
   knee — where doubling the input stops doubling the output. Tape ≈ near-linear with
   a soft top; the self-oscillation asymptote lives near the feedback ceiling.
6. **Wow/flutter:** instantaneous pitch of a sustained tone → depth % + rate, slow
   wow (~0.5–1 Hz) vs flutter (~6–7 Hz). Target ~0.1 % peak (subtle/analogue, NOT
   warbly). **Render the sustain test at feedback 0** so there is ONE clean delayed
   tap — overlapping repeats beat against each other and massively inflate the
   apparent depth (a fb>0 sustain reads ~9 % that is pure artefact, not real wow).
7. **Space Tape head taps** (from the all-heads render) land at **1 : 1.9 : 2.76**.
   The battery prints the tap times + ratios and overlays the target.

## The two voicings should come out DISTINCT
| trait | Tape Echo | Space Tape |
|---|---|---|
| in-loop bump (per-pass) | +4 dB @ 330 (Q0.6) | +2.5 dB @ 300 |
| gap-loss | 2-pole ~2.1 k (−12 dB/oct) | 1-pole ~2.0 k (−6 dB/oct) |
| output bass (tilt) | **cut** −6 @ 480 | **boost** +2.5 @ 180 |
| preamp shelf (tilt) | +4 @ 1.4 k | off |
If the single-repeat + per-pass + tilt panels don't separate these two, the
characters are wrong before you even reach the reference.

## Capture requirements (what makes a reference usable)
The battery is only as good as the `delay_ref/` captures. A usable set is rendered
through the reference plugin at **100 % wet**, noise/ducking OFF, tape-age centred,
one clean test signal, and crucially **with audible repeats** (feedback up enough
that the echo train decays over the capture, not a lone impulse). Required:
- `*_impulse` — a single click → a visible, decaying **train of repeats** (low fb).
- `*_tail` — **higher feedback**, many repeats (this is what the per-pass fit needs).
- `*_levels` — the input-level sweep; Tape and Space captures must **differ** (if the
  two characters produce byte-identical level files, the capture is the dry signal).
- `*_sustain` — a **long continuous** tone (several seconds), low feedback, so the
  single delayed tap carries the transport wow/flutter.
- `se_heads` — all-heads mode, single impulse, to read the 1 : 1.9 : 2.76 taps.

The battery auto-flags a dead capture: `has_echo_train()` prints
`NO ECHO TRAIN — unusable` when nothing survives ~150 ms past the onset.

## IP rule
References are captures of the user's licensed reference plugins. Never name the
source product/manufacturer or any vintage hardware brand/model in code, comments,
docs, or UI. Generic terms only: "a measured tape-echo reference", "a measured
multi-head tape echo reference". The shipped names are **Tape Echo** / **Space Tape**.
Do not commit `delay_ref/`.
