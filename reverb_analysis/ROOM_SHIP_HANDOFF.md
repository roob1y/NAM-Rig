# Room character — SHIPPED 2026-06-30 (honest rebuild). Handoff.

## What shipped
`SmallRoomFdn` in `src/rig/ReverbBlock.h` was replaced with the honest rebuild: a dual-instance
Dattorro/Griesinger allpass tank (true per-channel stereo: L->coreL take L, R->coreR take R) voiced
to the brand-free **wood small-room reference** (`reverb_references/room_wood_small_ref.wav`).

Verified OFFLINE (JUCE-free path): compiles via the juce stub, `tests/reverb_test.cpp` **ALL PASS**
(incl. the new-engine T23: Decay knob shortens correctly, wide/decorrelated). Plugin build + play-test
+ commit are on Robbie (sandbox can't JUCE-build/commit).

## The objective bar it was judged on (use this for every future character)
NOT the waveform null % (it saturates: the reference's own L vs R = ~79% audible, so <~79% is
impossible for any reverb). The trustworthy metric = **audible-range per-band time-energy SURFACE
distance** vs anchors: FLOOR = MSE(ref-L, ref-R) = 2.5 (same unit), SCALE = MSE(wood, plain) = 58
(different room). **Shipped Room = 3.1** (full plugin chain) / 2.6 (raw engine) -> as close to the
reference as its own two channels. See REVERB_CHARACTER_PLAYBOOK.md mandatory step 5.

## Engine design (genuine levers only — NO compensation hacks)
- input bandwidth (Tone) + high-SHELF: darkens onset top without blunting the transient.
- EARLYSEND: taps the tank's OWN 6-stage input diffusion to the output = dense early field /
  definition (NOT a separate ER FIR — the old bolt-on was removed). ESLP darkens it.
- figure-8 tank: body + bloom. PLAIN in-loop HF damping (the decay-scaled-damping hack is GONE).
- pre-delayed full-band late-reverb FDN (sustain AFTER the attack = lush not washed) with an in-loop
  LOW-cut so lows decay faster than mids (matches the reference's per-band decay).
- per-core input-diffuser jitter via `seedphase` decorrelates L/R (width corr ~0, native).

## Baked voicing constants (in SmallRoomFdn)
kSizeBase 0.30, kDamp 0.45, kShelfG 0.60 @ kShelfHz 4500, kEarlySend 0.78 @ kEsLpHz 12000,
kLateG 0.28, late T60 law = Decay*(1 + kLateRatio*Decay) with kLateRatio 2.5 (lush at long Decay,
short when short — passes T23), kLateDampHz 12000, kLateHpHz 600, kLateLoopHpHz 150, kLatePreMs 80.
NB the late-FDN feedback uses the RAW line-ms {53,67,79,97} (not size-scaled) — this reproduces the
fitted effective late decay (~0.3x nominal). Don't "fix" that; it's the fit.

## Param defaults (PluginProcessor.cpp, Room-specific)
Decay default = rangeDefault({0.2,3.0}) = 1.04s (~the voiced 1.13). Tone range widened {800,9000};
Tone default 0.85 (-> ~8 kHz -> bw 0.65, the voiced brightness). Mod default 0.0 (modulation washes
the room — keep it off by default; it's still a knob). Size 1.0. Predelay 0 (engine voiced ~2.7ms).

## Files
Proto: `reverb_analysis/room3_v2.cpp` (env-driven). Fitters: `room_fitfinal.py` (surface distance),
`room_fitenv.py`. Battery: `room_battery.py` (now has `spatial_full` mid/side+IACC and `null_nmr`).
Demos: `room_demos/43_final` (reference / ours_final / ours_final_tiny_mod). Backup of the pre-port
header: `outputs/ReverbBlock.h.precommit_backup` (also /tmp/room_rebuild/).

## If reopened
Residual: the full plugin chain reads 3.1 vs the 2.6 raw engine — the gap is the standard wet
low-cut(90Hz)/width every character gets; it's small and not a bug. Onset is on the bright side
(cen1 ~8.7k vs ref 7.2k) but the surface + ears accept it. Re-run the FULL battery + surface metric
(not scalars) before any further change.
