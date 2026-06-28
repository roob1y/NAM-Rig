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

## Space Tape — fitted result

Fit to the measured multi-head reference (echo-only mode 1, **head 1 = 250 ms = a
1/8 @120 SYNC capture** — above the free-mode 69–177 ms cap, so the harnesses gained
`--sync N --bpm B` to drive the engine in sync; `--sync 9 --bpm 120` reproduces it).
Validated dry: our wet lands within **7 samples** + a few % of the wet capture, so
`delay_ref/se_*.wav` are the true dry inputs and the level is pinned. Final voicing:
`sat 2.0, asym 0.06, bump +1.0 dB @ 300 Hz Q0.6, gap-loss 1-pole 2.2 kHz,
outBass +6 dB @ 240 Hz (LOW-SHELF boost, outBassQ 0), preamp off,
loopHpHz 50 (an IN-LOOP 1-pole high-pass — the character's own sub-bass shed)`.

The loop is a BAND-PASS, not just a low-pass: the reference SHEDS sub-bass down the
tail (40 Hz −12.7→−37 dB across the train; reliable repeat-by-repeat octband) the same
way it sheds highs. Without an in-loop high-pass ours instead BUILDS sub-bass (lows are
uncut so they dominate the tail → boom). `loopHpHz 50` (1-pole = −4 dB/pass @40 Hz)
matches the reference's −3.4 dB/pass shed. This is a CHARACTER trait, separate from the
user Low Cut utility. MEASUREMENT TRAP: a low-corner HP rings on transients, so a
CLICK-driven tail render makes it look like it BUILDS bass — verify in-loop LF with a
STEADY tone through the loop, never an impulse (the per-pass/tilt/single-repeat LF panels
are click-contaminated; the impulse capture's lows disagree with the tail's by ~24 dB).
Likewise the saturation-growth panel is inflated by echo overlap in the ref capture (the
harmonics are the trustworthy sat metric), and the per-pass HF "rise" is the noise floor.

Distinct from Tape exactly as the table predicts: a low-end **boost** (not Tape's
peaking cut), a **small** in-loop bloom (+2.3 vs Tape's +14), a **brighter 1-pole**
gap-loss. Match: single-repeat within ~1 dB 80 Hz–4 kHz (slightly bright at 6 k = the
1-pole's gentleness, on-character); per-pass bloom +2.4/+2.3 @ 250/330 vs ref +2.2/+2.3.

Harmonics — the key dimension (Space Tape shipped with `satAsym 0`, odd-only, margin
−82 dB). The reference is **richer/more driven** than Tape's (near-balanced, even-odd
margin only **−3 dB**, NOT Tape's strongly-even +22): `sat 2.0` sets the odd **H3 −31
= ref**, `asym 0.06` (the cosh even-gen) sets **H2 −35 = ref**, margin **−4 ≈ −3**.
Cross-checked by the real-dry levels null (H2/H3 exact). H4/H5/H6 sit below the
reference's flatter even series — the cubic+cosh topology limit (fixed `kEvenShape`;
same class as Tape's accepted H5 gap); the dominant H2/H3 + the even/odd balance match.
`delay_test` stays 34/34 (Clean byte-exact, loop bounded). Ears are the final judge.

## The saturation HARMONIC null — the dimension the battery used to miss (2026-06-27)

The Tape voicing matched every magnitude panel **and** the level-domain saturation
curve, yet the repeats sounded different ("more tapey"). Reason: those metrics only
measure **how much** the saturation compresses (a level-domain curve) and the tonal
**magnitude** — neither sees the **harmonic series**, i.e. whether the distortion is
**even (2nd, warm, asymmetric tape)** or **odd (3rd, hollow, a symmetric clipper)**.

How it was found — **null tests** (`delay_analysis/null_probe.py`), driving the real
engine with the EXACT dry inputs that produced the wet captures (`delay_ref/` ->
`delay_references/`, validated: our wet output lands within **3 samples** and **3 %**
of the reference, so the dry files are the true inputs and the operating LEVEL is
pinned — no guessing the sweep amplitudes):
- **impulse** null (single echo, near-linear): residual was **magnitude-dominated,
  NOT phase** (forcing the magnitude to match collapsed the residual; group delay was
  negligible). So it is not a filter-phase problem.
- **levels** null at the **calibrated level** (the reference sweep is a **1 kHz** tone
  topping at the true operating ceiling **0.30**, NOT 0.80 — the old `delay_render`
  assumed hot 0.80 steps): the reference is **even-dominant** (H2 grows −69→−54 dB with
  level, H3 far below at −75) — and **essentially LINEAR** (compression ratios
  1.00/1.00/1.00/0.999: it adds 2nd-harmonic warmth **without compressing**). Our old
  cubic was **odd-only** (H3 −31 dB, H2 at the noise floor) and over-compressing
  (satDrive 1.2 fit to imagined 0.80 inputs). Harmonic-vector distance to the
  reference dropped **~335×** after the fix.
- **sustain** null: looked like 10× excess wow/flutter, but that was a **measurement
  artifact** — the dry reference tone itself isn't a pure sine (it reads ~0.4 % on the
  same estimator; a synthetic sine reads 0). Driven with a **pure** carrier (the
  battery's `sustain`), ours and the reference both read ~0.05 %. **wow/flutter was
  left unchanged.** Always drive a clean carrier for the wow/flutter metric.

The engine fix (`tapeSat`): keep the odd ADAA cubic only for **loop-bounding** at a
**gentle** drive (so it is near-linear at the operating level, matching the reference's
linearity and killing the 3rd harmonic), and add an **even-harmonic generator** for the
2nd-harmonic warmth. A bare square gives only a lone 2nd — but the reference has a
smooth **even SERIES** (H2 −54, H4 −94, H6 −124, each ~−18 dB on the previous), so the
generator is a **clamped `cosh`** of the (pre-drive) sample: `satAsym·(cosh(kEvenShape·
clamp(x))−1)`, whose x²+x⁴+… expansion makes H2 plus a tapering H4/H6. `kEvenShape`
(=2.0) sets the H4/H2 spread; clamped so it can't run away in the loop, band-limited by
the in-loop gap-loss LP so it doesn't alias, DC-blocked so nothing accumulates. `satAsym`
sets the even amount independently of the bounding drive. Fitted result: **satDrive 1.2
→ 0.10, satAsym 0 → 0.003** — H2/H3/H4 within ~2 dB of the reference, even/odd margin
+20 dB ≈ the reference's +22. (H5 stays at the floor: the pure cubic makes no 5th
harmonic, but at −108 dB it is inaudible; a richer odd path could add it.) `delay_test`
stays green (T13/T14 bound the loop; T11 Clean byte-exact — Clean never calls `tapeSat`).

Two measurement lessons baked in here:
- **The levels render must be SINGLE-PASS (fb 0).** Feedback recirculation compounds the
  saturation and inflates the harmonics, so the battery's harmonic read no longer matches
  the (near-single-pass) reference. `delay_render` levels now renders at fb 0.
- **The per-pass HF transfer is noise-floor-limited.** Averaging the quiet later repeats
  drives the HF ratio toward 0 dB → a FALSE plateau that makes the reference look like its
  HF "carries on" while ours rolls off. The TRUE in-loop roll (loud early repeats only) is
  steep for both and they agree within ~3 dB. `per_pass_transfer` now gates each band by a
  per-pair noise floor (loud repeats only for HF, more pairs for LF). Don't read the raw
  averaged per-pass HF as a real transfer.

The battery prints **SAT HARMONICS** (even/odd + margin) and a harmonic bar panel, so this
dimension is regression-visible.

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
