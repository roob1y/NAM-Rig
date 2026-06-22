# Plate onset fix — implementation spec (hybrid-convolution early field)

## What this is
The "muffled / worlds-apart" problem in the Plate was localized (splice test) to the **onset
(first ~120–150 ms)**, not the tail. The fix = convolve the input with a fixed, derived
**early-reflection kernel** that reproduces the reference plate's onset character, and feed the
existing algorithmic FDN as the **tail**. Ear-confirmed "a lot closer"; onset-TF error vs the
reference 5.47 (shipped) → 2.90 (this), tail/tonal balance preserved.

## Assets (this folder)
- `plate_early_kernel.f32` / `.wav` — **the kernel**: 48 kHz, stereo, ~150 ms (7,200 samples/ch),
  32-bit float interleaved (.f32). This is a derived model (re-synthesized from the reference
  onset's per-ERB time-energy envelope, random phase) — NOT the reference capture. EULA-safe.
- `plate_accepted_IR.wav/.f32` — full accepted IR (kernel + tail) for reference/QA.
- `demo_0_reference.wav`, `demo_1_accepted_plate.wav` — the A/B.

## Signal flow to implement (per channel, wet path)
```
input → [ predelay ] → split:
        ├─ EARLY: partitioned convolution with plate_early_kernel  ──┐
        └─ TAIL : existing PlateFdn (algorithmic)                    ├─ sum → wet out
                                                                     ┘
```
- The kernel already contains the early level/decorrelation; sum the convolver output directly
  with the FDN output (unity). Trim the FDN's own early/direct so the onset isn't doubled
  (in the proto this was `mEarly` scaled down; tune the FDN early-tap to ~0 since the kernel
  now supplies the early field).
- Stereo: kernel L and R are independently decorrelated — convolve each channel with its own
  kernel column (it's a true stereo IR, 2 columns).

## Convolver
- Use a **uniform-partitioned FFT convolution** (block = the host buffer or 128 samples;
  ~56 partitions for 150 ms). JUCE has `dsp::Convolution` (zero-latency / partitioned) — simplest:
  load `plate_early_kernel.wav` into a `dsp::Convolution` in zero-latency mode, run it on the wet
  send, sum with the FDN tail. That's the whole onset fix.
- Kernel ships as a **binary resource** (BinaryData), NOT a header array.
- Latency: zero-latency partitioned mode → no added latency. (A plain partitioned mode adds
  one block; fine for a reverb but zero-latency is cleaner.)

## Knob behaviour
- The early field is **decay-knob-independent** (early reflections barely change with RT60), so the
  SAME kernel is valid across the whole Decay range. No per-knob kernels needed.
- Size knob: if you want the early field to scale with Size, resample the kernel ±; optional, not required.

## Tail
- Unchanged / already approved. The shipped v3 multiband-damping FDN is the tail. (The proto's
  extra reservoir/velvet/coupling were exploration and are NOT required for this onset win — the
  splice showed the shipped tail is already fine. Ship kernel + current tail.)

## QA / acceptance
- Convolve `dry_guitar` (or any DI) with `plate_accepted_IR.wav` and A/B vs `demo_1_accepted_plate.wav`
  — should match. Then in-plugin: kernel-convolver + FDN tail should match the accepted IR.
- Re-run `reverb_analysis/full_measure.py` on a rendered plugin IR if you want the metric table.

## Why not embed the kernel in ReverbBlock.h
14,400 floats as a constexpr array = a huge text write into the CRLF header; the dev-sandbox mount
truncates large header writes (hit repeatedly this session). Ship it as BinaryData instead.
