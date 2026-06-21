# METRICS — what the battery measures and what a match looks like

All measured on the mid signal `M=(L+R)/2` unless noted, onset-aligned (first
sample > 1% of peak). Bands are octave-spaced; decay bands span 62 Hz–11 kHz.

| metric | definition | what a MATCH looks like | what it catches |
|---|---|---|---|
| **T30(f)** | Schroeder backward-integration, fit −5→−35 dB, ×(60/30), per band | per-band curve overlays the reference within ~±0.1–0.2 s | the decay-vs-frequency surface: the single most defining trait. Plates have a **low-mid bloom** (lows ring longest) + HF roll-off |
| **EDT(f)** | early decay, 0→−10 dB ×6, per band | overlays reference | the *front* of the decay (perceived reverb time). EDT≪T30 ⇒ non-exponential (fast early, long late) |
| **tonal balance** | octave power over a 3 s window, normalised at 1 kHz | within ~±1 dB across 125 Hz–8 kHz | spectral colour of the wash (bright/dark, HF extension) |
| **C80** | 10·log10(E[0–80 ms]/E[80 ms→]) per band | within ~±1–2 dB | clarity / early-to-late ratio. High C80 ⇒ sparse/thin tail (often HF dying too fast) |
| **echo density (NED)** | normalised echo density buildup, 20 ms frames | reaches ~1.0 by ~40–50 ms, like the reference | onset diffusion. Low NED early ⇒ metallic/grainy attack |
| **side/mid per band** | 10·log10(E_side/E_mid) per band, `S=(L−R)/2` | tracks the reference (≈0 dB ⇒ wide); also check broadband L/R correlation | stereo width per band. More negative ⇒ narrower/more correlated than reference |
| **modal depth** | std (dB) of the 2–6 kHz tail fine-spectrum (0.4–0.9 s window) | within ~±1 dB of the reference | **THE lush-vs-digital discriminator — EDR/RT60 are completely blind to it.** Low (~5–6) = flat/featureless = "digital/metallic"; high (~7–11) = peaky distinct modes = "lush". For an FDN: strong in-loop HF damping ⇒ sparse distinct modes ⇒ HIGH modal depth (so **warm = lusher**); a bright/undamped tail ⇒ dense overlapping modes ⇒ flat ⇒ digital. This is the metric that finally cracked the Hall "sounds fake/metallic" problem after EDR/RT60 said everything matched. |
| **centroid** | spectral centroid of first 3 s | within ~10–15% | overall brightness sanity check |

## Reading the verdict
- **Decay first.** If T30(f) doesn't overlay (in *shape*, after matching the 1 kHz
  point), nothing else matters yet — fix the decay surface (per-band feedback
  gains / in-loop damping / a low-mid bloom path).
- **Then tonal balance + C80** for colour and HF life.
- **Then modal depth — the "does it sound digital/metallic?" check.** If the decay
  and tonal balance match but it still sounds fake, this is almost always why:
  ours is too flat (low modal depth) vs the reference's peaky modal tail. The fix
  is usually *more in-loop HF damping* (warmer ⇒ sparser distinct modes ⇒ lusher),
  NOT corrective output EQ. Chase this BEFORE assuming the engine is wrong.
- **Then width.**
- **Echo density** is usually fine if the input diffuser is dense; check it only
  if the attack sounds grainy.

## Worked example — plate vs a vintage-plate reference (v3 engine)
The committed plate (length-scaled multiband-damping FDN, 32 lines) lands these
against the reference. Useful as a sanity target for any plate-family voicing:

| metric | reference | our v3 plate | verdict |
|---|---|---|---|
| T30 63 Hz → 8 kHz | ~4.9 → ~1.6 s (lows ring ~1.7× the mids) | ~5.0 → ~1.7 s | shape overlays ✓ |
| T30 @ 1 kHz | ~2.9 s | ~2.95 s | ✓ |
| modal depth | ~7.5 | ~7.1–7.4 | ✓ lush, not grainy |
| centroid | ~6.7 kHz | ~6.4 kHz | ✓ |
| tonal balance 63 Hz–8 kHz | — | within ~1 dB | ✓ |
| L/R correlation | ~0.0 | ~−0.06 | ✓ wide |
| C80 | ~−1 dB | ~−4 dB | ours is LUSHER — see below |

**The C80 gap is the one to understand, not "fix" blindly.** A real plate is
instantly dense (a two-slope onset that front-loads the first ~80 ms), so it
reads clearer (higher C80). An FDN's tail builds slightly, so it runs lusher
(lower C80). Closing it with a hot early tap drives L/R correlation anti-phase
(mono-incompatible), so past a point this is an EARS call, not a metric to chase.

**Why one damping pole can't do it (the structural lesson).** A single one-pole
per line gives ONE tilt shape — flat below the cutoff, a cliff above. It cannot
make the lows ring longest AND keep HF air AND match brightness independently:
darkening to hit the centroid shortens the mids. Reproducing the decay-vs-freq
curve needs a **length-scaled multiband** absorptive damping (a broadband gain +
a couple of high-shelves per line, the dB shape scaled by each line's loop length
so all lines share one T30(f), fit to the reference curve). See
`IR_MATCHING_PLAYBOOK.md`.

## Calibration reminder
A reference IR's filename decay label is usually the **unit's decay control**, not
measured RT60 — always measure. A "2.0 s"-labelled plate capture measured ~2.9 s
at 1 kHz (with the lows ringing ~5 s). Match the *measured* curve; note the knob
offset separately so the Decay control still reads true seconds.

## Output files
`reverb_battery.py` writes two PNGs per run:
- `<character>_battery_<label>.png` — 6 panels: T30(f), EDT(f), tonal balance, C80,
  side/mid, echo-density buildup (reference = black circles, ours = red squares).
- `<character>_edr_<label>.png` — Energy Decay Relief heatmaps side by side (the full
  time×frequency decay surface; the ground-truth picture of the tail).
