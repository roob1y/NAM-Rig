# Space Tape — voicing brief (next chat)

Continuing NAM Rig delay-character work, branch `feature/delay-rework`, `C:\Dev\NAM-Rig`.
The single-head **Tape Echo** is voiced and committed using a harmonic-null method.
Now voice the multi-head **Space Tape** character the same way, matching its reference
captures. (Shipped names are generic: **Tape Echo** / **Space Tape** — never use any
hardware brand/model name anywhere; refer only to "a measured multi-head tape echo
reference".)

## Read first
- `delay_analysis/DELAY_CHARACTER_PLAYBOOK.md` — the method + the metric pitfalls.
- `voicingFor(SpaceTape)`, `processSpaceTape`, `tapeSat`, `updateTapeFilters` in
  `src/rig/DelayBlock.h`.
- The memory note `delay-characters-tape` (full worklog).

## What Tape Echo taught us (apply it)
The Tape voicing matched every magnitude panel **and** the level-domain saturation
curve yet the repeats sounded different. The missing dimension was the saturation's
**harmonic SERIES (even vs odd)** — invisible to the sat curve and the magnitude
spectra. Real tape is **even/2nd-harmonic dominant** (warm, asymmetric record transfer);
a symmetric clipper is odd/3rd-dominant. Found by **null tests** driving the real engine
with the EXACT dry inputs that produced the wet captures.

Engine pieces already in place (shared, used by Space Tape too):
- `tapeSat` = gentle odd ADAA cubic (bounds the loop) **+** `satAsym` clamped-`cosh`
  EVEN-harmonic generator (DC-blocked, band-limited). `satAsym` sets the even amount
  independent of `satDrive`. **Space Tape currently has `satAsym = 0` (odd-only) — it
  almost certainly needs even harmonics, like Tape did.**
- `outBassQ` voicing field: `>0` = PEAKING output cut (Tape, to cancel a big in-loop
  bloom on the single pass); `0` = low-shelf (Space Tape's full low-end BOOST).

## Method (proven on Tape Echo)
1. **Offline build:** compile from `git show HEAD:src/rig/DelayBlock.h` into `/tmp` —
   the `C:\Dev\NAM-Rig` FUSE mount **truncates large files** on bash read and lags
   file-tool edits; the Read/Write/Edit tools are the source of truth; the `outputs/`
   mount does NOT truncate. `g++ -std=c++17 -O2 -I src -I /tmp/stub tests/delay_test.cpp`.
2. **Pin the operating level:** drive the real engine (`delay_null.cpp`) with the dry
   input `delay_ref/se_impulse.wav` and confirm the wet lands within a few samples + a
   few % of `delay_references/space_tape/impulse.wav`. If so, `delay_ref/se_*.wav` ARE
   the true inputs and the level is pinned (don't guess amplitudes).
3. **Null tests** (`null_probe.py`, adapt for `--char space`): impulse = magnitude vs
   phase; **levels = HARMONIC even/odd** (the missing dimension); tail = per-pass;
   heads = the 1 : 1.9 : 2.76 tap spacing.
4. **Saturation:** fit `satDrive` (gentle — keep it near-linear at the operating level if
   the reference is linear there) + `satAsym` (the cosh even amount) to the reference's
   harmonic series via the battery `SAT HARMONICS` metric. Even-dominant = warm.
5. **Magnitude:** in-loop head bump + gap-loss (from the per-pass), output-once outBass +
   preamp (from the single repeat). Space Tape is voiced ~OPPOSITE to Tape in the bass:
   low-shelf **BOOST** (`outBassQ = 0`), **smaller** bloom, **brighter/gentler 1-pole**
   gap-loss. Check whether its bloom is big enough to need a peaking cancel (probably not).
6. **Re-fit the saturation after the magnitude** — the in-loop bump lifts the level into
   the saturator, so they couple (every magnitude change shifts the harmonics).

## Battery metric pitfalls (already fixed for Tape — do NOT reintroduce)
- Render `levels` at **fb 0** (single pass): feedback recirculation compounds the
  saturation and inflates the harmonics vs the near-single-pass reference.
- The per-pass HF is **noise-floor-limited**: `per_pass_transfer` is gated per band to
  loud repeats only; the old all-pairs average flattens HF into a FALSE plateau.
- Output tilt = single − **normalised** per-pass (the raw per-pass carries the per-repeat
  feedback-decay offset → a bogus ~19 dB gap).
- Render the impulse/single-repeat at the **reference dry level** (~0.5), not full-scale.
- Wow/flutter needs a **pure** carrier — the dry reference tone isn't a pure sine and
  inflates the apparent depth ~10×.

## Tooling (all in `delay_analysis/`)
`delay_render.cpp` (`--char space`, overrides incl. `--asym --obQ --wowM --flutM`),
`delay_null.cpp` (process an arbitrary dry input through the real engine), `null_probe.py`
(the null harness), `delay_battery.py` (`--char space`; SAT HARMONICS + gated per-pass +
corrected tilt + zoomed wow/flutter), `delay_fit_staged.py`.

## Reference captures
`delay_references/space_tape/` = impulse, levels, tail, heads (wet). `delay_ref/se_*.wav`
= the dry inputs. **Neither folder is committed — keep it that way.**

## Hard rules
- No hardware brand/model names anywhere — generic terms only; ships as **Space Tape**.
- Don't commit `delay_ref/` or `delay_references/`.
- Don't touch the **Clean** path (byte-exact, regression-locked by `delay_test` T11);
  keep `delay_test` 33/33.
- The **null residual + ears** are the judge — Robbie A/Bs by playing his own guitar
  through the JUCE plugin on Windows (not offline demos). The sandbox can't git-commit;
  hand over commit commands.

## Goal
Space Tape matches its reference on the **SAT HARMONICS** (even-dominant warmth), the
single-repeat tone, the per-pass bloom, and the head taps (1 : 1.9 : 2.76) — and stays
**distinct** from Tape Echo: brighter, gentler 1-pole HF roll, low-end **boost** not cut,
smaller bloom. Then defaults / UI polish if needed (note: ping-pong + dual are already
Clean-only; Space Tape is mono multi-head with auto-spring on reverb modes 5–11/12).
