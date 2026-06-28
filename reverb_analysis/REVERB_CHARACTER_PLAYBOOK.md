# Reverb-character production playbook
Hard-won workflow + pitfalls from the Plate vs vintage-plate marathon (2026-06-21).
Goal: match/voice a character to a reference IR in hours, not days. Read this BEFORE starting.

## THE CORE PRINCIPLE (the one reusable rule — applies to every character)
**Let coherent recursive structures (the FDN) carry the BODY and GEOMETRY. Let stochastic
structures (velvet / sparse late-field / derived convolution) carry TEXTURE and EXTREME LATE/HF
energy.** Forcing the recursive field to do texture/top-octave leaves coherent fingerprints
(modal bands, a centroid that won't fall). Validated end-to-end on Plate (architecture-validated,
not just hypothesized): dark recursive body + velvet top + convolved onset = centroid trajectory and
band-coherence both land on the reference. Apply the same split to halls, chambers, ambient, shimmer.
Once the architecture is right, remaining work is integration + gain-structure tuning + QA — NOT a
search for a missing mechanism. (Diagnostic that tells you which stage you're in: EDR residuals go
from *structured* errors [wrong mechanism] to *spectral-balance* errors [just tuning].)

## The workflow (in order — do NOT skip step 2)

1. **Build the fast offline loop first.** Compile the LIVE engine standalone via the juce stub
   (`reverb_analysis/render_character.cpp`, `g++ -I../src -Istub`), render an IR with an impulse,
   measure with the battery. ~3 s/iteration, no plugin build, no DAW. For onset work use a SHORT
   impulse (`impulse_short.f32`, 0.3 s) — ~30× faster renders.

2. **LOCALIZE before you tune. Use the SPLICE TEST.** This is the single most important lesson.
   Before tuning anything, find *where in time* the problem is: take the reference IR and your IR,
   swap their halves at ~100 ms (level-matched + short crossfade), convolve a DI, loudness-match,
   and A/B. (`splice_test.py`.) Ears tell you in 30 s whether it's the ONSET or the TAIL.
   **On Plate we burned most of the effort tuning the tail; the problem was the onset the whole time.**
   Don't tune the half that's already fine.

3. **Auto-fit, don't hand-tune-and-listen.** Once localized, fit the parameter(s) to the reference
   with a machine-in-the-loop optimizer scoring a *perceptual* error (per-ERB time-energy map,
   `onset_fit.py` pattern), so YOU listen ONCE at the end, not to 50 tweaks. Hand-tuning + ear-A/B
   per change = "we'll be here centuries."

4. **Ear-gate at decisions only — and do NOT reflexively ask for one.** Use the full battery +
   auto-fit to converge. An A/B listen belongs at exactly two moments: (a) final sign-off just before
   porting into `ReverbBlock.h`, and (b) a genuine perceptual tradeoff the metrics can't settle
   (e.g. "long top = shimmer or hiss?"). It is NOT a closer for every step. Objective results —
   localization, decomposition, auto-fits, across-knob validation — are settled by the numbers:
   report them and move on. Do not end an analytical result with "want me to render a demo for your
   ears?"; when an ear test is actually warranted, render it and state the ONE narrow question as
   optional, not as a gate. (Robbie's explicit correction, 2026-06-22.)

## Measurement — what actually tracks perception

- **The energy/decay battery LIES about perception when the onset is wrong.** T30, EDT, C80,
  octave spectrum, side/mid, RT60, EDR — these are all time-AVERAGED. Two reverbs can match every
  one of them and sound "worlds apart." We matched all of them and it still sounded muffled.
- **What the muffle/clarity actually lives in (measure THESE):** onset time-frequency map
  (per-ERB energy over the first ~120 ms), brightness-over-time (running spectral centroid —
  a real plate is bright-at-attack then darkens), crest factor (dense vs spiky), echo-density
  buildup time (instant-diffuse vs builds-over-10 ms), and the low-mid **bloom** timing
  (energy peaking ~70 ms in). `full_measure.py` computes ~28 measures across auditory (Zwicker
  sharpness, ERB bands), modulation spectrum, dispersion/group-delay, IACC, brightness-over-time,
  coloration. **Rank by |ours − ref| to surface the discriminators automatically.**
- **The reference IR is the ground truth — fit to IT** (its measured TF surface), not to abstract
  target numbers. Matching a number you wrote down ≠ matching the plate.

## DSP pitfalls (don't repeat these)

- **Don't densify by SMEARING.** A velvet/allpass *pre-diffuser* on the input densifies but delays
  the transient → muffled mush. Density must come from *adding clean copies* (early reflections) or
  *more modes*, never from spreading one copy.
- **The coherent-bright-dense transient is the wall.** A real plate onset is bright AND dense AND
  non-spiky at once. Random-phase synthesis → dense but dark/soft; minimum-phase → bright but a
  spike. EULA-safe re-synthesis from magnitude can't fully reproduce it. Accept the residual or use
  a derived convolution kernel.
- **An FDN can't sustain a low-level, rising high-frequency shelf.** Whatever you do (partial
  coupling, HF-retentive lines, reservoirs), forcing the top to ring either brightens the whole
  field or destabilizes. If the reference has a long top-octave shimmer, use an ADDITIVE field
  (velvet / sparse late-field), not the FDN.
- **Hybrid-convolution is the pragmatic answer** when the algorithm can't make a feature: a fixed,
  *derived* (re-synthesized, EULA-safe — not the capture) early-reflection kernel convolved in front
  of the algorithmic tail. Early reflections are ~decay-knob-independent → one kernel works across the
  range.

## PORTING pitfalls (the plugin, not the proto)

- **Level calibration is NOT optional.** A kernel/convolver's raw amplitude has no relation to the
  engine's internal output scale. We shipped gain=1.0 and it was ~5× (+14 dB) too hot → a loud early
  burst that dropped "off a cliff" when the kernel ended. ALWAYS measure the engine's native level
  for that region and scale to match (`rms(engine region)/rms(kernel region)`).
- **Continuity at any early→tail handoff.** An added early block that ends abruptly = a volume cliff.
  Match the early's END level to the tail at the crossover, and taper the last ~30 ms.
- **You cannot compile JUCE in the analysis sandbox.** Plugin code (convolver, BinaryData, real-time
  levels) goes out UNVERIFIED → a build-and-listen loop with the user. Calibrate everything you can
  OFFLINE first; tell the user it's untested; expect 1–2 build iterations on level/continuity.
- **`ReverbBlock.h` is CRLF and the dev mount truncates large header writes.** Edit at BYTE level,
  and after EVERY write verify: CRLF count == line count, zero LF-only lines, brace balance 0, tail
  intact. Repair truncation by splicing the tail from a clean snapshot.
- **`g++ ... | head && echo $?` reports HEAD's exit, not the compiler's** — a failed build looks
  green and you measure a stale binary. Build with `g++ ...; echo $?` (no pipe) and confirm 0.
- **Guard plugin-only code** (`#if __has_include(<juce_dsp/juce_dsp.h>) && __has_include("BinaryData.h")`)
  so the offline tools and `reverb_test` (which compile the same header without JUCE) still build.
- **Don't embed large IR data in a header** (14k floats = guaranteed truncation + bad practice).
  Ship kernels as `juce_add_binary_data` BinaryData resources.

## Time-box / strategy
- The GTM priority is shipping a few great characters + presets, not perfect IR matching
  ([[namrig-gtm-and-shipped-reverb-set]]). Localize fast, fix the dominant perceptual gap, accept
  small residuals, move on. Plate ate days; with this playbook it should have been an afternoon.

## Voicing stage — residual → lever map (when architecture is right, "same class, different specimen")
Once EDR residuals are spectral-balance not structured, the remaining differences map to KNOBS, not
mechanisms. Plate residuals seen 2026-06-21 and their levers:
- **"continuous statistical grain; modes don't announce themselves" / residual 2–5 kHz horizontal
  structures** = MODAL DENSITY. N32 FDN has too few modes so individual ones stand out. Lever:
  raise line count (N32→N64) and/or denser input diffusion → more modes → no single line. This is the
  one remaining *core* change; everything else is tuning. (Or: push the stochastic/velvet field DOWN to
  cover the mid so the whole tail is statistically uniform and the FDN carries even less.)
- **"separation between body and top a bit too clean"** = the dark-body crossover is too sharp. Lever:
  soften DARKG / overlap the velvet band lower so body↔top blend continuously (the real plate has no seam).
- **"different 6–10 kHz distribution"** = velvet sub-band corners/levels. Lever: velvet band centers/gains.
- **"different noise-floor texture"** = velvet statistics. Lever: velvet density + L/R decorrelation +
  decay so its envelope-distribution (Rayleigh CV ~0.52) and modulation spectrum match the reference.
Diagnostic for "modes announcing themselves": modal-pk/kHz + the 2–5k peak/median band metric +
envelope Rayleigh CV. When those match, the grain matches.

Tools (all in `reverb_analysis/`): `render_character.cpp`, `full_measure.py`, `splice_test.py`,
`onset_fit.py`, `synth_onset.py`/`synth_v3.py` (kernel re-synth), `reverb_battery.py`, `lateslope.py`.

## Refined HF architecture (the sharpest lesson — Robbie's synthesis, 2026-06-21)
Verified end-to-end on Plate. The cleanest mental model for matching a reference's HF:

- **Supply late HF STATISTICALLY, not RECURSIVELY.** A recursive field (FDN) generating its own
  HF lifetime leaves COHERENT fingerprints: horizontal modal bands (esp. 2–5 kHz) and a centroid
  that stalls (won't fall) because a discrete HF population persists. A real plate's late HF is a
  smooth stochastic shimmer with no bands. So: **darken the FDN body so the recursive field carries
  only the (dark) mid-body and produces NO top, and add the entire top octave as a low-level
  stochastic field (velvet / sparse late-field / convolution).**
- **Diagnostic that nails it: centroid-over-time.** Reference falls (e.g. 7.5k→4k by 500 ms);
  a recursive-HF build stalls (~4.8–5 k). Stalling = a persistent coherent HF population. Pushing the
  FDN to make the top ring just raises the stall and adds bands. Darker-body + low stochastic velvet
  makes it fall again (measured: 4913→4277 toward ref 3745) — direction proven; full match needs more
  FDN HF-damping headroom than a Tone knob exposes (a core damping re-voice).
- **You can't fix coherent fingerprints by MOVING them.** Predelaying the recursive field to hide its
  early modes doesn't work (lines ring wherever you put them; tested, got worse). Density/coherence is
  the lever, not timing.
- **Summary of the whole Plate journey (keep this):** Shipped failed = lacked the late HF population.
  Accepted proved the population is necessary but generated it too coherently (recursive → bands +
  centroid stall). The win is supplying that population STATISTICALLY (stochastic velvet / convolution)
  rather than recursively — same energy, no fingerprints. Build future characters this way from the
  start: FDN = dark body only; onset = convolved/derived early kernel; top/late HF = low-level velvet.

## When the IR is NOT the whole story — controlled probes for NONLINEAR / TIME-VARYING reverbs
Everything above matches the **linear** fingerprint, and an IR is the *complete* description of a
linear, time-invariant system — for a clean Room/Hall/Plate (modelled linearly) it's all you need,
and a swept (ESS) IR already avoids the click/LF problems. But an IR is captured at ONE level and
assumes time-invariance, so it is **blind to** two dimensions that define some characters:
- **Nonlinearity / level-dependence** — a **Spring** saturates, drips and chirps; a driven plate
  edges up. The tail behaves differently when you hit it hard. The IR (one level) can't see it, and
  the linear battery (EDR/C80/centroid) is blind to it.
- **Time-variance / modulation** — modulated/chorused tails, spring flutter. An IR smears these into
  a static decay.

Fix: complement the IR with the **controlled-probe method used for the delay characters** (full
recipe in `delay_analysis/CONTROLLED_PROBE_METHOD.md`). Drive the reference reverb with KNOWN dry
probes and compare against your engine driven by the IDENTICAL dry (the null idea):
- a **multi-level tone-burst sweep** → how the harmonics / compression grow with input level (the
  nonlinear character — spring overdrive, drip onset). Match the harmonic SERIES, not just amount.
- a **sustained pure carrier** → the tail's pitch / amplitude modulation (chorus, flutter) via the
  wow/flutter estimator (bandpass + Hilbert; use a pure tone or it inflates).
Keep the IR + this handbook as the primary tool for the linear body/onset/tail; add these two probes
ONLY for characters where nonlinearity or modulation matters — **Spring first** (the most nonlinear),
then anything you'd push into drive. Generic names only; ears + the residual are the final judge.
