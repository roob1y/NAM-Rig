# Dynamic Cab — bass-cab Fs estimator upgrade (spec)

Fable-designed 2026-07-07 (review finding #1), for Opus implementation.
Read alongside `docs/cabdyn/PHYSICS_UPGRADE.md` §4 and `src/rig/LfResonance.h`.

## Problem

The per-cab Fs chain has a low-frequency blind spot that hits exactly the cabs
that need it most. The IrAnalysis response grid starts at **40 Hz**
(`kResFLo`), the LfResonance search window at **45 Hz**, and
`CabDynamicsBlock::retuneResonance` clamps to **45 Hz**. A bass cab tuned
~35–45 Hz (Aguilar 2x12 ~40, Acoustic 360 ~35, deep ported 4x10s) has its box
resonance at or below the grid edge, so the estimator's "low-side fall ≥1.5 dB
over the octave below the peak" check has almost no data below the peak and
fails → **invalid → 90 Hz fallback**. Consequences: excursion LP2 keyed more
than an octave above the true resonance (displacement model misses the
fundamental register entirely) and guitar modal centers on a bass cone
(`mScale` keys on `fsHz < 65`, and the 90 Hz fallback skips the rescale).

## Changes

### 1. `src/rig/IrAnalysis.h` — widen the shared grid

`kResFLo` **40.0 → 25.0**. Nothing else. The grid is shared with the UI
response well (x-axis gains 25–40 Hz — a JUCE-side eyeball item, out of scope
here) and with the estimator. Prominence and low-side fall are DIFFERENCES of
curve values, so the mean-centring shift from the wider span cancels out of
both. Resolution drops 26.2 → 24.1 pts/oct — immaterial. Short IRs have
limited intrinsic resolution at 25 Hz; the zero-padded FFT interpolates
smoothly, which is fine for finding a broad bump.

### 2. `src/rig/LfResonance.h` — search window, edge-aware low-fall, clamps

- Search window: `idxAt(45.0)` → **`idxAt(32.0)`** (upper 230 unchanged).
- Fs clamp: `[45, 200]` → **`[32, 200]`**.
- Low-side fall (step 3b) becomes **edge-aware**. Today it averages the octave
  below the peak; with the wider grid a 35 Hz peak still has only ~0.49 oct of
  data. Rule (window `[l0, pk-1]` computed as today, l0 clamped to 0):

  ```
  winOct = log2(fpk / freqAt(l0))          // octaves of data actually below peak
  if      winOct >= 0.5  -> require lowFall >= 1.5 dB   (unchanged for guitar cabs)
  else if winOct >= 0.25 -> require lowFall >= 1.0 dB   (edge-adjacent bass peaks)
  else                   -> invalid                      (cannot distinguish from a shelf)
  ```

  This keeps the mic-proximity-shelf rejection intact where a full window
  exists and only relaxes where the grid edge physically truncates the window.
- Update the header comment block (grid "40..8000" → "25..8000", window
  "45..230" → "32..230", clamp).

### 3. `src/rig/CabDynamicsBlock.h` — consume the deeper range

- `retuneResonance`: `std::max(45.0f, ...)` → **`std::max(32.0f, ...)`**.
  (Excursion LP2 at 32 Hz, Q 0.9 is unconditionally stable at any fs we run.)
- B3 shelf corner floor: `std::max(140.0f, std::min(260.0f, 2.2f * mFsEst))`
  → **floor 80.0f**. Guitar cabs are unaffected (2.2×69 = 152 > 140 > 80);
  bass cabs get a tracking corner (35 Hz cab → 80 Hz shelf) instead of a
  pinned 140 Hz that compressed well above their whole fundamental register.
  Update the adjacent comment (the "clamp(2.2*FsEst,140,260)" text).

### 4. `tests/cabdyn_test.cpp` — extend T15/T16

Base file: `outputs/verified/cabdyn_test.cpp` (T18 already present — do NOT
work from git HEAD, it predates T14d/T18).

- T15 synth grid: `FLO = 40.0f` → **25.0f** (mirror of change 1).
- T15a/b/c: keep, expectations unchanged (tolerances absorb the index shift —
  verify they still pass, don't loosen them).
- **T15d**: 40 Hz bump (bumpDb 8) → valid, fs within ±15%.
- **T15e**: 35 Hz bump (bumpDb 8) → valid, fs within ±15%.
- **T15f**: proximity shelf — monotone rise toward the low edge, e.g.
  `v += 6.0 * log2(150/f)` for `f < 150`, no interior bump, plus hash →
  **invalid** (guards the widened window against the shelf false-positive).
- **T16d**: `setSpeakerResonance(35, 6)` at max macros on 2 s noise → finite,
  peak < 4.0, `dbgFsEst() == 35 ± 0.5`.

## Verification (sandbox-specific — follow exactly)

The repo bash mount does NOT reflect this session's file-tool edits. Two-track
workflow:

1. Make the real edits with the **file tools** (Read/Edit) on
   `C:\Dev\NAM-Rig\...` — that is the real repository.
2. Build a `/tmp` mirror for compiling: `Biquad.h` and `IrAnalysis.h` +
   `LfResonance.h` via `git -C /sessions/happy-loving-hawking/mnt/NAM-Rig show
   HEAD:<path>` (unmodified this session); `CabDynamicsBlock.h` and
   `cabdyn_test.cpp` from `/sessions/happy-loving-hawking/mnt/outputs/verified/`
   (session-verified, newer than HEAD).
3. Re-apply YOUR edits to the mirror with a python3 replace script asserting
   each old-string matches exactly once (same edits as step 1, verbatim).
4. `g++ -std=c++17 -O2 -Wall -Wextra -Isrc tests/cabdyn_test.cpp -o t && ./t`
   — all checks green, zero warnings. (IrAnalysis.h is JUCE-side and is not in
   the test's include graph; its one-constant edit is verified by the test
   grid mirroring it.)
5. Report: exact edits, measured fs values for T15d/e, and the final
   pass count.

Out of scope: UI response-well tick labels (JUCE, separate task), Stage C Size
mapping, any constant not named above.
