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

## Fitting a voicing to the graphs (the method that worked for Tape)

Match the metrics by RENDERING candidate voicings through the real engine and
minimising band error — `delay_render` takes voicing overrides (`--hbDb --hbHz
--hbQ --gapHz --obDb --obHz --ppDb --ppHz --sat`, via `setTapeVoicingOverride`),
and `delay_fit_staged.py` coordinate-descends them. **Fit in dependency order**,
each stage pinned before the next:

1. **Saturation (satDrive)** — to the level-sweep compression. Pin it first; it
   couples into the bloom through loop level.
2. **Per-pass / in-loop** (bump Db/Hz/Q, gap-loss) — to the CLEAN-pair per-pass.
3. **Output-once** (outBass, preamp) — to the near-linear single repeat.

Hard-won measurement lessons (all now baked into the battery + fitter):
- **Per-pass: use clean (above-noise) early echo pairs only.** Averaging in the
  quiet later pairs corrupts the HF (the ratio goes positive). The reference's
  true bloom is bigger than the all-pairs average shows (+13 dB, not +10).
- **Fit gap-loss from the single-repeat HF, not the per-pass HF** — the tail HF
  above ~1.5 kHz is noise floor.
- **Render the single-repeat at LOW level** (`--impAmp ~0.12`, near-linear) to
  match a feedback-down quiet reference impulse. A unit impulse over-saturates
  its own bump and forces an impossibly deep output cut.
- **Keep the output bass-cut corner below 1 kHz** so it doesn't drag down the
  @1k normalisation point and make the low-mid read hot.
- **The loop couples everything:** a bigger in-loop bump re-hardens the
  saturation top (hotter loop into the clipper) and worsens the single-repeat
  250 Hz — so there's a balance, not a perfect simultaneous match.

## Tape Echo — fitted result (committed)

Fit to the measured tape-echo reference with the battery. Final voicing:
`sat 1.2, bump +9.5 dB @ 260 Hz Q0.50, gap-loss 2-pole 1.95 kHz, outBass
−8.5 dB @ 560 Hz (low-shelf), preamp +6 dB @ 2.2 kHz (PEAKING)`.

One engine change shipped with it: the **preamp went from a high-shelf to a
peaking band** — the reference's HF lift is a 2 kHz peak that falls again by
4 kHz, not a rising shelf. (`updateTapeFilters`, `Biquad::peaking` Q0.7;
SpaceTape leaves it off so only Tape is affected.) `delay_test` stays 33/33.

Match achieved: saturation top-to-bottom ✓, per-pass bloom centre dead-on,
output tilt within ~1.6 dB except a **~3 dB residual at 250 Hz** (single/tilt).
That residual is the structural floor of a peaking-bump cancelled by a low-shelf
cut (the reference's bump and cut are both broad and mirror each other); closing
it would need the in-loop bump and output cut to be matched arbitrary shapes —
not worth the topology for ~3 dB on one band. Ears are the final judge.

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
