# Controlled-Probe Voicing Method (delay characters)

How to voice a delay CHARACTER (Tape Echo, Space Tape, BBD, …) against a measured
reference **accurately**. This is the method that nailed Space Tape after the
click-based captures kept disagreeing with themselves. Use it for any character.

## Why (the problem it fixes)

Capturing a reference by feeding it a **click/impulse** and analysing the echo is
unreliable:
- A click's broadband transient **contaminates the low end** (the first echo reads
  +10 dB of bass that isn't really there). Our own impulse and tail captures
  agreed with each other but the *reference's* impulse vs tail disagreed ~24 dB in
  the lows and ~9 dB at 4 kHz — they were taken at different feedback/tone.
- A low-corner filter (an in-loop high-pass) **rings on the click**, so a per-pass
  measurement shows bass *building* when steady tones show it shedding.
- Feedback captures overlap echoes, inflating the **saturation** growth curve.
- The battery's per-pass HF sits at the **capture noise floor** and reads a false
  "HF carries on" plateau.

The fix: **drive the reference with controlled, known dry signals — mostly steady
tones — and null our engine against the IDENTICAL dry.** Steady tones give a clean
per-frequency transfer (no click smear); a known dry gives an exact null; one fixed
reference setting gives one consistent target instead of contradictory captures.

## The dry probe signals (in `delay_references/dry_probes/`)

Reusable for any single-line character; regenerate with
`python3 delay_analysis/gen_dry_probes.py` (the committed source of truth -- the
WAVs themselves are kept out of git) if the operating level differs.
- **`dry_main.wav`** (run at LOW feedback): 5 s pure 440 Hz carrier (wow/flutter) →
  3 s 1 kHz at the operating ceiling ~0.30 (harmonic series) → stepped steady tones
  30 Hz–8 kHz, 0.45 s each at 0.12 (single-repeat EQ) → a 7-step level sweep to 0.30
  (saturation) → a 1 ms click (impulse/phase). One capture yields EQ + saturation +
  harmonics + wow/flutter + impulse.
- **`dry_perpass.wav`** (run at HIGH feedback): short 0.2 s bursts at 0.05, octave-
  spaced 40 Hz–6 kHz, 2.5 s gaps. SHORT + QUIET so nothing recirculates into a clip
  at high feedback; the decaying train gives the per-pass (in-loop) EQ.
- **`dry_click.wav`** (multi-head only): one click through the all-heads mode → the
  head tap ratios.

## Capture protocol (what the user does)

Run each probe through the reference at **100 % wet**, ONE fixed setting:
flat tone (Bass/Treble centred), utility filters OFF, no reverb/ducking/noise,
tape age centred, input nominal (loudest part not clipping). Keep tone/repeat-rate
identical across all takes; only the input signal and feedback change.
- `dry_main.wav` at **lowest feedback** (single clean echo) → `cap_low.wav`
- `dry_perpass.wav` at **high feedback, just below self-oscillation** → `cap_high.wav`
- (multi-head) `dry_click.wav`, all heads, low feedback → `cap_taps.wav`

Report the **repeat-rate (delay time in ms)** and which feedback was used. The
absolute feedback number need not map to ours — match the measured decay instead.
Put the `cap_*.wav` in `delay_references/<character>/` (NOT committed).

## Extraction (per response)

Align the capture to the dry by the delay (the click locates it; cap = dry delayed).
- **Single-repeat EQ** (the reliable tonal fingerprint): for each stepped tone,
  `20·log10(echo_rms / dry_rms)`, normalised @1 kHz. This is the clean per-pass-free
  EQ. NEVER read it from a click FFT (LF-noisy, HP-rings).
- **Saturation**: level-sweep echo RMS vs input step → the growth/compression curve
  (the +dB rise, mild compression). The reference's makeup gain is just level —
  divide it out.
- **Harmonics** (the saturation CHARACTER): H2..H6 of the 1 kHz@0.30 echo, rel f0.
  A real asymmetric tape transfer is a smooth FULL series (even ≈ odd, pairs ~18 dB
  apart). Match the series shape, not just H2/H3.
- **Wow/flutter**: instantaneous pitch of the pure-440 echo (bandpass + Hilbert),
  99th-pctile % deviation. Use the PURE carrier — an impure tone inflates it.
- **Per-pass** (in-loop EQ): from `cap_high`, the decay rate (dB per repeat) per
  burst frequency, OR the relative buildup. Splits in-loop (compounds) vs output-
  once (single − per-pass). Low frequencies are noisy here — cross-check with the
  steady-state buildup / the single-repeat.
- **Head taps**: envelope peaks of `cap_taps` → the ratios.

## Fitting

1. **EQ → filters.** Fit the single-repeat EQ with biquads (numerically, then in the
   engine via overrides). A tape echo is a BAND-PASS: in-loop high-pass × gap-loss
   low-pass (± a head-bump peak). Decide in-loop vs output-once from the per-pass
   (in-loop compounds → the tail sheds/blooms). Note the engine's fractional read
   adds a little HF loss — voice the gap-loss corner a bit high to compensate.
2. **Harmonics → saturation function.** If the reference series is a smooth full
   even+odd cascade, use the **asymmetric tanh** (`sat::tanhADAA1`, satDrive = g,
   satAsym = bias b) — the odd-cubic+cosh can't make a 5th harmonic. If it's even-
   dominant (like the single-head Tape was), the cubic + cosh-even may fit. Fit g/b
   (or drive/asym) to the H2..H6 series at the operating level; re-check the growth.
3. **Wow/flutter, drift, head ratios, fb ceiling** to the measured values.
4. Re-fit saturation after the EQ (level into the saturator shifts with the EQ).

## Verifying ours (offline)

Drive **the same dry probe** through `delay_null` (`--sync`/`--bpm` to hit the
reference's delay time, `--loCut`/`--loopHp`/`--sat`/`--asym`/`--gapHz`/… overrides
to audition) and compare per-frequency, NOT a time-domain null (the delay times may
differ). Steady tones through `delay_null --fb 0` give the clean single-pass EQ —
the trustworthy metric. `delay_test` must stay green (Clean byte-exact, loop
bounded). Build offline from `git show HEAD:src/rig/DelayBlock.h` (the mount
truncates large files; file tools are truth; the outputs mount doesn't truncate).

## Hard rules

Generic names only (ships as "Tape Echo" / "Space Tape"); never a hardware brand/
model. Don't commit `delay_ref/`, `delay_references/`, or `dry_probes/`. Don't touch
the Clean path (regression-locked). Ears + the null residual are the final judge —
the user A/Bs by playing guitar through the JUCE plugin; the metrics localise, they
don't certify.
