# NAM-Rig Reverb Rebuild — Handoff

Self-contained brief for any new assistant picking up the reverb work. Read this first, then
`docs/PLATE_PHASE2B.md` (active Plate task) and the memory note `reverb-engine-roadmap` if available.

## Goal
Give EACH reverb character its own best-fit, authentically-voiced DSP topology (not one shared engine),
voiced so the exposed controls are bounded to a sweet spot — "you can't dial a bad sound." Work ONE
character at a time: prototype the topology standalone in /tmp, MEASURE it, render WAVs for Robbie's
ears, get approval, THEN integrate, keep tests green, and have Robbie commit.

## Where things live
- Repo: `C:\Dev\NAM-Rig`, branch **`feature/reverb-characters`**.
- Main DSP (header-only): **`src/rig/ReverbBlock.h`** — all reverb engines + the mixer + the selector.
- Helpers: `src/rig/Blocks.h`, `src/rig/Lfo.h` (`FracDelayLine` 4-pt Hermite, `Lfo`).
- Tests: **`tests/reverb_test.cpp`** (T1–T21). Must stay green.
- JUCE-side UI/params (can't compile offline — review by hand): `src/PluginProcessor.cpp/.h`, `src/ui/Panels.h`.

## Architecture
`ReverbBlock` = character selector + one shared **GuardMixer**. 7 characters, 4 wet-only engines:
- **FdnReverb** (16-line Hadamard/FWHT FDN + 6-stage input diffuser) → Room, Hall, Ambience, Bloom
- **PlateReverb** → Plate
- **SpringReverb** (studio spring-voiced) → Spring
- **ShimmerReverb** (octave-up pitch shift in FDN) → Shimmer

Each engine's `process()` overwrites the buffer with WET only (reads dry as excitation). ReverbBlock owns
the dry copy and runs every character through GuardMixer: wet low-cut HPF, auto-makeup, ducking, M/S width,
Bloom swell, Freeze. Per-character voicing is exposed via static introspection (`sizeExposed`, `toneExposed`,
`predelayExposed`, `modExposed`, `tensionExposed`, ...) shared by UI + tests. Public engine API:
`setDecaySeconds/setDampHz/setPredelayMs/setModDepth/setFreeze` (+ engine-specific). Preserve it when editing.

## Character status
- **Hall** — 16-line FWHT FDN + dense diffuser; smooth (tail spectral flatness ~0.46, was ~0.04 metallic). DONE-ish. Open: push to 32 lines + nested allpasses.
- **Room** — shares FDN voiced small. Open: add a geometric sparse image-source (ISM) early-reflection front end for real "place".
- **Ambience** — FDN short/splashy. Spec ideal: Gerzon triple-allpass + Haas decorrelator.
- **Bloom** — FDN long + GuardMixer swell. FORK to decide: envelope-controlled FDN vs true reverse-delay cascade.
- **Spring** — DONE. studio spring-voiced: 4 parallel dispersion springs + in-loop diffusers + output allpass bank → smooth lush wash. Tension = spring tightness. (A separate "boingy" in-amp spring tank is a future idea.)
- **Shimmer** — DONE. Compact FDN + octave-up grain pitch-shifter in feedback; selectable interval.
- **Plate** — ACTIVE, see below.

## Plate (the active work)
1. Tried an **FDTD biharmonic plate mesh** (a real physical model). Physically accurate (dispersion, etc.)
   but only ~860 grid modes vs the thousands a real plate has → too sparse → metallic/thin. ABANDONED for
   Plate. Kept in git history at commit **`f9bf4a5`**; could return as a separate "physical plate" character.
2. **Reverted to the Dattorro figure-8 plate** (the proven, dense, smooth vintage plate-style diffusion topology).
   Restored from commit `05e45fc`, committed, sounds good. THIS is the current Plate engine.
3. **Phase 2b — vintage plate voicing, prototyped in /tmp, NOT yet in the live header.** Full code patch in
   `docs/PLATE_PHASE2B.md`: decay-calibration fix (knob now reads true seconds 0.5–5.5 s), a
   **Low-Cut** (input HPF = vintage plate "Input Filter") and a bounded **Tone** (±~4 dB high-shelf) control, hardwired
   vintage plate decay-vs-frequency (lows ring longest, ~1.5 s @10 kHz at damp ~4500).
4. **Real-IR profiling done.** Profiled 15 Greg Hopkins vintage plate IRs (mounted folder `vintage plate Plate IR's`)
   with an Abel–Huang NED + per-band-T60 harness. FINDING (corrected by Robbie's ears): Hopkins is a **dark**
   capture (centroid ~630–1150 Hz); the **studio** IRs (Robbie owns) are **bright**, ~like our voicing (~2250 Hz).
   vintage plate brightness varies hugely between captures — no single "correct" value. So: the **Tone knob should SPAN
   dark (~630 Hz) ↔ bright (~2250 Hz), default in the middle (~1200–1500 Hz)**. Do NOT blindly darken to Hopkins.
   Real NED→0.5 at ~1 ms (instant density), L/R corr ~0 — ours matches.
5. **Next steps:** set the Tone range to that span + a sensible mid default; render matched impulses; A/B
   against BOTH IR sets (Hopkins dark, studio bright); integrate the /tmp patch into the live header; wire the
   Low-Cut + Tone knobs JUCE-side; keep `reverb_test.cpp` green; have Robbie commit.

## Proposed Plate controls (UAD vintage plate-informed, all bounded — "no bad sounds")
Expose: **Decay** (0.5–5.5 s), **Pre-Delay** (0–250 ms log), **Low-Cut** (Off..~360 Hz), **Tone** (dark↔bright tilt).
Global: **Width**, **Mix**. Hardwire (no knob): the decay-vs-freq curve, damping, subtle modulation.

## CRITICAL workflow gotchas (these bit us repeatedly)
1. **The bash mount of `C:\Dev\NAM-Rig` is STALE / lagging** — it does NOT reliably reflect file-tool edits
   or the current checked-out branch. The **Read/Write/Edit file tools are the source of truth** for file
   contents. To compile offline, pull files from the git object store (`git show <ref>:path`) into /tmp —
   do NOT trust the working-tree copy the shell sees.
2. **Offline build recipe (no JUCE):** stage in `/tmp`; make a stub `stub/juce_audio_basics/juce_audio_basics.h`
   containing just `#pragma once`; get `Blocks.h`, `Lfo.h`, `ReverbBlock.h`, `reverb_test.cpp` (via `git show`);
   `g++ -std=c++17 -O2 -Wall -Wextra -I. -Istub reverb_test.cpp -o t && ./t`. JUCE-side files can't be built
   offline — review by hand.
3. **Line endings are CRLF** (git autocrlf). The Edit tool's **multi-line `\n` matches FAIL** — use SINGLE-LINE
   anchors, or apply changes to a `git show`-extracted LF copy in /tmp via python and full-file Write, or do a
   full-file Write. (Pattern that works: extract `<ref>:ReverbBlock.h`, python-apply the exact edits asserting
   each matches once, compile, run tests.)
4. **The sandbox CANNOT git-commit** (.git is read-only to bash, mount stale, lock files it can't unlink).
   **Hand Robbie exact git commands; he commits on Windows** (his autocrlf gives clean LF diffs). Commit by
   path (`git add docs/` or by filename), never `-A`/`.`; run `git diff --cached --check` first.
5. **Commit each feature the moment it's green.** Work was nearly lost twice to branch switches / stashes.
6. **Branch hygiene:** reverb work is ONLY on `feature/reverb-characters`. A branch switch can make work look
   "lost" — it's just on another branch. Check `git branch --show-current` + `git log --oneline -3`.

## Verification & tools
- `reverb_test.cpp` T1–T21: T2 = Plate predelay EXACT sample shift; T8 peak<8 + decaying; T9 mix=0 bit-exact;
  T10 wet tail; T11/T17 Plate L/R decorrelation; T21 Hall tail flatness>0.25. Keep ALL green.
- /tmp measurement harnesses (rebuildable from scratch): per-band T60 (RBJ bandpass + Schroeder backward
  integration), Abel–Huang Normalized Echo Density (NED=1 ⇒ fully diffuse), spectral centroid, L/R correlation.
- **IR profiler** `profile.cpp`: reads any WAV (PCM16/24/32 or float, mono/stereo), prints the same metrics —
  used to profile real IRs and compare to our model. Render our engine to WAV the same way to A/B.

## Reference data
- **vintage plate TS (solid-state)**: decay 0.5–5.5 s; ~5 s@500 Hz → ~1.5 s@10 kHz undamped, lows ring longest;
  80 Hz input filter; center transducer + 2 off-center pickups (true stereo); instant echo density; bright-but-smooth.
- **Real IRs**: `vintage plate Plate IR's` folder = 15 Greg Hopkins captures (bright/medium/dark × 1–5). **License CC-BY**
  (per oramics; confirm from Hopkins' own posting before release) — **credit Greg Hopkins**, use as private
  reference only (don't ship), and **keep "vintage plate" out of the product name** (trademark). studio bundle (Robbie owns)
  is brighter but has a commercial EULA — do NOT use it to voice/clone a competing product.

## Memory notes (if the same file-memory system is available)
`reverb-engine-roadmap` (per-character plan + status), `namrig-offline-build` (the /tmp recipe),
`commit-often` (commit discipline + the autocrlf/lock details), `nam-rig-ecosystem`, `nam-rig-code-course`.
