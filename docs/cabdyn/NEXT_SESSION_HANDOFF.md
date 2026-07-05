# Dynamic Cab — NEXT SESSION HANDOFF

Status as of this session. Read alongside `docs/cabdyn/DESIGN.md` (the physics /
math) and the code: `src/rig/CabDynamicsBlock.h`, `tests/cabdyn_test.cpp`.

---

## What is built (and proven)

`CabDynamicsBlock` — a standalone, header-only, JUCE-free DSP class that wraps the
existing IR convolution as a **delta**: two process stages (`processPre` before
`CabBlock`, `processPost` after) that add only level-dependent / time-variant
behaviour a static IR cannot contain.

- **Stage A — Reactive Impedance Delta (pre-conv):** envelope-driven 90 Hz Fs
  resonance peak (≤ +3 dB) + 2 kHz Le inductance shelf droop (≤ −3 dB). Flat at
  rest; deviates only under drive + Age.
- **Stage B — Band-Limited Cone Breakup (pre-conv):** 0.8–3.8 kHz midband isolated
  by a crossover, driven through a `tanh` soft clipper as a **parallel delta** at
  **2× oversampling** (own 65-tap polyphase halfband, no library) + low-band cone
  power compression (≤ −2 dB, gain reduction only).
- **Stage C — Enclosure Air/Damping Delta (post-conv):** two parallel 3-allpass
  diffusion networks (prime delays < 10 ms, in-loop LP damping), crossfaded by
  Cab Size, mixed in at low level (≤ 0.12).
- **3 macros**, floats 0..1, all default 0: **Age/Drive**, **Thump**, **Cab Size**.

### Verification — all green (offline g++, no JUCE)

`tests/cabdyn_test.cpp`, 22 numbered checks, **ALL PASS**:

```
g++ -std=c++17 -O2 -Wall -Wextra -Isrc tests/cabdyn_test.cpp -o cabdyn_test && ./cabdyn_test
```

- **T1** bit-exact bypass — all macros 0 ⇒ output byte-identical to input (pre & post).
- **T2a** halfband 1 kHz passband 0.000 dB; 30 kHz decimator stopband −96 dB.
- **T2b** hard-driven breakup worst inharmonic/alias **−104 dB** (bound −55).
- **T3** stable at max settings (noise finite, peak < 1.0); enclosure impulse decays.
- **T4** Cab-Size sweep artifact-free (max sample step 0.014, no zipper).
- **T5** deterministic. **T6** envelope-driven (loud blooms A1 +3 dB, quiet 0.0 dB).
- **T7** all modulation ceilings held. **T8** disengage click-free, returns bit-exact.
- **T9** Stage B comb-free — level-dependent fundamental ripple 0.30 dB, no bumps > +0.2 dB.

*(The sandbox repo mount is a stale snapshot — the block was compiled/verified in
`/tmp` from the exact file bytes written here. Recompile locally to re-confirm.)*

---

## Wired IN (this session) — needs a local build to confirm

The block is now integrated per lane. **These are JUCE-side edits that could NOT be
compiled in the offline sandbox — build locally once to confirm before relying on
it.** The standalone DSP core + `cabdyn_test` still pass (22/22), unchanged.

**`src/rig/RigChain.h`** — `#include "CabDynamicsBlock.h"`; members `cabDyn` /
`cabDynB` next to `cab` / `cabB`; `prepare()`/`reset()` call them explicitly (they
aren't `MonoBlock`s, so not in `allMonoBlocks()`); the per-voice loop wraps the
convolver:

```cpp
cabDyn.processPre(vA, numSamples);                         // impedance + cone breakup
if (!cab.isBypassed()) { cab.process(vA, n); heal(cab, vA, n); }   // static IR
cabDyn.processPost(vA, numSamples);                        // enclosure air
healDyn(cabDyn, vA, numSamples);                           // NaN self-heal for the wrapper
```

`latencySamples() == 0`, so PDC is unchanged (the wrapper is deliberately left out
of the LA/LB latency sums — revisit only if it ever gains latency). All macros at
0 = bit-exact bypass, so **SoloA stays byte-exact**.

**`src/PluginProcessor.cpp`** — 6 APVTS floats (0..1, default 0):
`cabDynAge / cabDynThump / cabDynSize` and `rigBcabDyn*`, pushed each block to
`mChain.cabDyn` / `mChain.cabDynB` via `setAgeDrive / setThump / setCabSize`.

**`src/ui/Panels.h`** — three `LabeledKnob`s (Age / Thump / Size) added to each
`CabPanel` lane, in a 58 px row along the bottom (the IR response well takes the
space above). ⚠️ **Eyeball this layout** — blind edit; if the cab lane is short the
response well may get cramped. Tunable: the `58` / `6` px in `CabPanel::resized()`.

### Build-check checklist (local)
1. Compile the plugin — the three files above are the only changes.
2. Confirm the 6 new params appear; **all default 0 ⇒ output unchanged** (SoloA
   still bit-exact) — the fastest proof that bypass survived integration.
3. Open the Cab panel: three knobs per lane, wired and labelled.
4. Then ear-test (Stage C first — see risk ranking below).

---

## Voicings that are GUESSES pending your ear-test  [EAR]

I cannot hear any of this. These are physically-motivated numbers, not confirmed
tones. Most likely to need tuning, in order:

1. **Stage C enclosure (highest risk of sounding artificial).** The whole
   "diffuse air" idea is the least certain. Guesses: wet ceiling `kEncMax = 0.12`,
   feedback `g` 0.50/0.62, damping 6 k/3 k, prime delays {113,179,251} and
   {211,331,461} @48k. It may sound like reverb rather than "cab air" — if so,
   drop `kEncMax` hard (0.04–0.06), shorten delays, raise damping. **Gate this on
   your ears before trusting it at all.** Consider auditioning Stage C alone.
2. **Stage B breakup band + drive.** Band 800–3800 Hz and `drive = 1 + …(≤3.2×)`
   are guesses for "cone cry." Could be too fizzy or too subtle. The `−55 dB`
   alias headroom leaves room to push drive or widen the band if it's too polite.
3. **Stage A depths.** +3 dB @ 90 Hz "thump bloom" and −3 dB @ 2 kHz "thermal
   droop" ceilings are plausible speaker numbers but unconfirmed by ear. The
   90 Hz centre assumes a generic 12"; a specific 3Sigma cab's `Fs` may differ.
4. **Envelope knee.** `kEnvLo/Hi = 0.03/0.50` (≈ −30/−6 dBFS) sets how hard you
   have to play before it moves. Set for a hot amp-out level; may need shifting
   for your actual gain staging into the cab.

Everything above is a named constant at the bottom of the header — easy to sweep.

---

## Open questions for next session

- **Where in the level structure does this sit?** The envelope knee assumes a
  roughly unity, hot amp-output feeding the cab. Confirm the actual signal level
  at the pre-cab point so `kEnvLo/Hi` are calibrated to real playing dynamics.
- **Per-cab `Fs`?** Stage A's 90 Hz is generic. Worth reading each 3Sigma IR's
  low-resonance from `IrAnalysis` and setting A1's centre from it, so the "thump"
  lands on the actual box tuning rather than a fixed 90 Hz?
- **Is Stage C worth keeping?** It's the most speculative stage. If it doesn't
  earn its place on your ears, cut it — the block is still meaningful with just A
  (dynamic impedance) + B (dynamic breakup), which are the two clearest "IR can't
  do this" wins.
- **A/B vs each other?** Two lanes share the same voicing constants now. Do you
  want per-lane character (e.g. Cab A tight, Cab B big) baked in, or leave it all
  to the Cab Size macro per instance?
- **CMake/CTest target?** Say the word and I'll add `cabdyn_test` next to the
  other block tests.

---

## Design decisions worth remembering

- **File location:** the brief said `src/dsp/`, but every DSP block in this repo
  lives in `src/rig/` and includes siblings as `"Biquad.h"`. I matched the repo
  convention — `src/rig/CabDynamicsBlock.h` — so the include paths and test style
  line up with `PreDelayBlock`, `DriveBlock`, etc. Flag if you'd rather it move.
- **Bit-exact by construction, not by luck:** every stage is `x + macro·delta(x)`
  and is skipped entirely when its macro is 0 (early return). That's why T1 is
  byte-identical and why the crossover/allpass phase shifts can't leak a static
  comb into the dry path.
- **No loudness compensation anywhere** (per your repeated rejection of auto-level
  — see the Drive Auto Gain memory). The only gain move is cone power
  *compression* (level drops when pushed, never restored).
- **Stage B is a harmonics-ONLY exciter (phase-comb fix).** A naïve band-limited
  `nl(band) − band` delta combs against the dry path near the crossover corners
  under hard drive (a compressive nonlinearity's fundamental gain `G(A) < 1`
  leaves a phase-shifted residual fundamental — measured ±3/−6 dB ripple). Fixed
  by cancelling that fundamental with the **tanh describing function**:
  `δ = nl(band) − G(A)·band` (257-point `g(β)` lookup + a band-envelope follower).
  Ripple → 0.30 dB, guarded by **T9**. Consequence: Stage B no longer compresses
  the mid *fundamental* (it's a clean exciter); the intended low-band power
  compression now lives in a separate **series low-shelf** (B3), which can't comb.
  Credit: this was flagged by an external review pass before it shipped.
- **Oversampling, not ADAA, for Stage B** — the brief mandated explicit halfband/
  polyphase code. The rest of the rig uses ADAA (`Saturation.h`); this block is
  the exception, by request. If you'd prefer ADAA here later to match the house
  style and drop the 65-tap FIR, it's a clean swap (the delta formulation stays).
