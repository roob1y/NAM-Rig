# Pitch + Envelope — session handoff (2026-07-03)

Pick-up notes for a fresh chat. The full running state also lives in auto-memory
(`pitch-and-envelope-effects.md`, loaded each session); this is the human-readable
version. Companion docs: `RESEARCH.md` (circuit + DSP research, cited) and
`BUILD_PLAN.md` (locked plan + phases).

## Where we are

**Phase 1 — Envelope filter: DONE, built on Windows, play-tested, ready to commit.**
Two voices, each faithful to a real unit's control set:

- **FX25 = DOD FX25B** — knobs SENSITIVITY, RANGE, BLEND. Fixed band-pass, Up sweep,
  fixed Q; full-wave OTA detector (fast), 500 kΩ input Z.
- **Q-Tron = EHX Q-Tron+** — knobs GAIN, PEAK + segmented toggle switches MODE
  [LP·BP·HP·MIX], DRIVE [Up·Down], RANGE [Hi·Lo], BOOST [Normal·Boost],
  RESPONSE [Fast·Slow]. Half-wave photocell detector, self-osc-capable, 300 kΩ Z.
  MIX = BP blended with dry. BOOST = preamp into the filter (2nd-order-ADAA cubic
  soft-clip, baked in) + level lift. RESPONSE = attack time.

Under the hood: one TPT/ZDF state-variable filter (`Svf.h`), envelope follower with
exponential cutoff map + modeled control-element lag, and a per-voice **pedal
front-end** — input-impedance loading (high-shelf cut = delta from Robbie's 1 MΩ
interface), input/output coupling high-passes, input gain staging. Sits BEFORE the
compressor so it tracks the calibrated (not raw) dynamics. Default-bypassed → SoloA
stays bit-exact. Verified by `tests/env_filter_test.cpp` (18 checks, all pass offline).

## What's next (not started)

Pitch effects — one `PitchBlock : MonoBlock` with a Type toggle, built in phases
(see BUILD_PLAN.md §3). Target genres: 2000s rock + indie/acoustic.

1. **Phase 2 — analog dividers**: OC-2 sub-octave + Octavia octave-up. Comparator +
   flip-flop / full-wave rectify. O(1), zero latency, no pitch tracking. *Start here.*
2. **Phase 3 — granular**: detune + whammy (Puckette `f=(β−1)fs/w`). No tracking.
3. **Phase 4 — poly octave (POG-style)**: fixed-integer FFT/phase-vocoder.
4. **Phase 5 — PSOLA harmony**: needs a real-time pitch tracker → factor
   `PitchTracker.h` from the existing MPM/NSDF in `Tuner.h` (octave-up-robust).

Reference pedals locked: OC-2, Octavia, MicroPitch detune, DigiTech Whammy (knob
first), Micro POG. Use Opus for the pitch DSP; voice by the controlled-probe/A-B
method (as the drives/reverbs were). **Synth effects = separate future family, out
of scope.**

## Files changed this session (the commit scope)

New: `src/rig/Svf.h`, `src/rig/EnvFilterBlock.h`, `tests/env_filter_test.cpp`,
`docs/pitch_env/{RESEARCH,BUILD_PLAN,HANDOFF}.md`.
Modified: `src/rig/Saturation.h` (added `cubicADAA1`), `src/rig/RigChain.h`,
`src/PluginProcessor.cpp`, `src/PluginProcessor.h`, `src/PluginEditor.cpp`,
`src/PluginEditor.h`, `src/ui/Panels.h`, `src/ui/BlockStrip.h`, `CMakeLists.txt`.

Strip/editor indices shifted +1 when ENV was inserted at tile 1 (AMP A now 5, etc.);
`BlockStrip kCols=12`, `mPanels` size 14.

## Dev gotchas (read before coding next session)

- **Offline build** (no JUCE, sandbox g++): `g++ -std=c++17 -O2 -I src -I plate_proto/stub tests/<x>_test.cpp -o /tmp/t && /tmp/t`.
  Blocks must stay JUCE-free in the audio core (use `std::max`, not `juce::jmax`).
- **No `M_PI` in headers included by the plugin target** — MSVC/clang-cl doesn't define
  it there (only test targets set `_USE_MATH_DEFINES`). Use a local `constexpr float kPi`.
- **Stale sandbox mount**: after file-tool edits, `bash`/g++ can see a stale or
  truncated copy. Trust the Grep/Read tools (real files); when you need an offline
  compile, **rewrite the whole file via a `cat > … << 'EOF'` heredoc** to sync the
  mount. Do NOT string-patch files on the mount (a `python str.replace` once hit the
  CHECK macro's first `\n    std::printf`).
- **Sandbox can't build JUCE or commit** — the plugin + UI compile only on Robbie's
  Windows toolchain; he commits. Keep DSP offline-testable so it's verified before he builds.
