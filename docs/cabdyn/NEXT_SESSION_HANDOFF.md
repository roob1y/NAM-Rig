# Dynamic Cab — NEXT SESSION HANDOFF

## 2026-07-07 session — review fixes landed; bass-Fs next

A full review pass, then two of its findings SHIPPED (in the working tree —
commit first if not yet committed):

1. **Thermal motor-floor engage** (`CabDynamicsBlock.h` control block):
   voice-coil heating is motor physics, not wear, so engage now keys on
   `clamp01(kThermMotor(0.6)·max(Age,Thump) + kThermWear(0.4)·Age)`. Age=1 is
   bit-identical to the old dose; fresh-cab presets (Recto/Aguilar) now keep
   ~60% of the sag they physically have. Guarded by **T14d** (0.736 dB, fresh
   cab @ 6 s). Both constants [EAR].
2. **Speaker-drive calibration trim** `setSpeakerDriveDb(±24 dB)`: scales ONLY
   the detector sidechain (excursion LP2 drive, pre/post push envelopes,
   thermal x² integrator) — never the audio path, IM ring, or B1
   band/describing-function math. Fixes the §12 portability caveat (knees are
   calibrated to ONE rig's ~ −14 dBFS internal level). 0 dB is bit-exact vs
   never-set (**T18c**, memcmp); −18 dB kills dispPush on the T10a tone
   (**T18a**), +12 dB restores it on a −12 dB tone (**T18b**). NOT yet exposed
   as a parameter — JUCE plumbing below.
3. Stale IM-ring comment fixed (kDopUs 6.4 µs → ring is 8 @ 48k, not 16).

**Verified 45/45, zero warnings** (offline g++; byte-equivalent /tmp
reconstruction — a fresh session should re-run the suite from its mount as
step 0 to confirm from literal repo bytes).

**Queue, in order (specs ready):**

- **Bass-Fs upgrade — `docs/cabdyn/BASS_FS_UPGRADE.md`** (full spec, review
  finding #1: cabs tuned 35–45 Hz fall back to 90 Hz). An Opus agent was
  launched and died at the session limit BEFORE editing anything — no partial
  state, start clean from the spec.
- **Multi-rate tests**: suite is 48k-only. Re-run T1/T2b/T11a/T13 invariants at
  44.1k and 96k. While there: Stage C prime delays lose primality when scaled
  by fs/48000 and rounded — snap to nearest prime after scaling (prepare and
  dbgSetEncDelays).
- **JUCE plumbing** (hand-review only, cannot compile offline): APVTS floats
  `cabDynSpkrDrive`/`rigBcabDynSpkrDrive` (−18..+18 dB, default 0) pushed to
  `setSpeakerDriveDb` next to the other cabDyn params (PluginProcessor.cpp
  ~1248/1267); an fsEst readout in CabPanel via `CabBlock::lfResonance()`
  ("Fs 52 Hz", or "Fs —" when invalid/no IR) so the 90 Hz fallback is finally
  visible; response-well x-axis eyeball after kResFLo 40→25; fix the stale
  "kDriveEnv 0.9" claim in the older section below (code is 1.2 + kDriveBase
  0.5 per §12).
- Robbie's items, unchanged: local Windows build; EAR_NOTES listen (all [EAR]
  constants still unheard). Product call pending: gate dyncab when the cab
  block is bypassed / no IR loaded?

Sandbox note: mid-session file-tool edits are NOT visible to the repo bash
mount (fresh-session mounts DO include prior sessions' edits). Verify offline
builds from `git show` bases + re-applied patches, or commit first.

## PHYSICS UPGRADE shipped (2026-07-06) — needs local build + ear-test

The block moved from broadband-envelope-driven to **cone-displacement-driven**
per `docs/cabdyn/PHYSICS_UPGRADE.md` (spec written before the code; read it
first). All roadmap items landed:

1. **Excursion model** — RBJ LP2 at the box resonance on the pre-conv input →
   `xd/xs/dispPush`; A1 bloom, B3 compression and B1 drive now key on
   DISPLACEMENT (a loud 2 kHz bend no longer fattens the 90 Hz peak — T10/T17).
2. **LF→HF intermodulation** — Bl(x) AM + Doppler FM as a sidebands-only
   two-tap delta on the shared HP800 band. Exactly zero when the cone isn't
   moving (T12), sidebands −21.6 dBc on a slammed two-tone (T11), alias-clean at
   base rate (T13, −75 dBc off-grid).
3. **Per-cab Fs** — `src/rig/LfResonance.h` estimates the captured LF resonance
   from the existing IR analysis curve; plumbed CabBlock→RigChain→CabDynamics
   via atomics (`setSpeakerResonance`). Invalid/failed IR falls back to 90 Hz.
4. **Thermal voice-coil sag** — multi-second series compression (τ 3.5 s,
   ≤1.5 dB, Age-scaled, reduction only; T14 measured 1.23 dB @ 6 s, recovers).
5. **Modal exciter shaping** — the harmonics-only breakup delta is filtered
   through two bending-wave peaks + fizz shelf (can't re-comb — no fundamental
   in the delta; T9/T16 confirm).

**Verification: 40/40 checks pass** (T1–T17, zero warnings, offline g++ from the
exact repo bytes). Bit-exact bypass, click-free disengage, determinism, all
ceilings held. Stage B breakup drive is `1 + Age·(kDriveBase 0.5 + kDriveEnv 1.2·push
+ kDriveDisp 1.0·dispPush)` per §12 (a static wear floor was added and the envelope
term left at 1.2 — an earlier note claiming kDriveEnv was rebalanced to 0.9 is stale).

**Not yet done:** local Windows build (JUCE-side edits — CabBlock/RigChain/
PluginProcessor/IrAnalysis — could not be compiled offline; they are small and
hand-reviewed). Stage C untouched this pass.

**Data-anchoring pass (same day):** the main [EAR] guesses were replaced with
measured values from the published Klippel analysis of the Celestion G12H(55)
Greenback (Voice Coil Feb 2015) — see PHYSICS_UPGRADE.md **§11**: `kAMsym 0.22`
confirmed (Bl 82 % @ 2.0 mm), `kDopUs` 10.2→**6.4 µs** (2.2 mm slam excursion),
`kA1FsShift` 0.06→**0.15** (Cms 75 % @ 2.3 mm), and the Bl asymmetry split into
`kAMasymBase 0.10` (deliberate coil-out offset, present on FRESH cones — 1.6 mm
measured) + `kAMasymAge 0.10` (wear, still [EAR]). Le(x) omission validated
(0.04 mH measured swing). 40/40 tests re-pass.

**Still [EAR] (dose/taste, not physics):** `kAMasymAge`, `kXCal` + `kDispLo/Hi`
(gain-staging convention — calibrate against the real pre-cab level, not by
ear), modal centers/gains, `kThermDb` dose, Stage C voicing. For judging by ear,
use the delta-null trick: render a loop Dyn-on and Dyn-off, invert one, sum —
the residual is exactly what the block adds, solo'd and plainly audible.

---

# Previous handoff (pre-physics-upgrade, still-relevant context)

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

> **Measured in Plugin Doctor (this session, flat baseline, delta):** with Thump
> up, the enclosure's low resonance sits at **~35 Hz (Size 0) and moves to ~14 Hz
> (Size max)** — correct direction (bigger box = lower), but **too subsonic to be
> musical**. Action for next session: shorten the Stage C delay lines so the box
> resonance lands ~**60–120 Hz**, and add a subsonic high-pass on the enclosure
> output so sub-20 Hz doesn't build up. Also confirmed: the A1 79 Hz thump peak is
> real but small under a multitone probe (envelope only ~1/5 driven) — it reaches
> the full +3 dB only under a sustained loud note, so ear-test with real playing,
> not analyzer sweeps.

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
