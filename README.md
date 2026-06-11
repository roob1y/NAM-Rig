# NAM Rig

Full-rig plugin (JUCE VST3) built around the verified NAM AA engine from
[NAM-AA-Plugin](../NAM-AA-Plugin). Fixed serial chain of blocks:

```
[mono]   gate -> comp/drive -> AMP (NAM AA) -> EQ -> CAB (IR loader)
[stereo]                                                -> modulation -> delay -> reverb
```

## Scoping decisions (June 2026)

- **Fixed serial chain** — no free graph, no reordering (v1).
- **First-party blocks only** — no plugin hosting.
- **Mono through the cab, stereo after** — amp is mono; mod/delay/reverb stereo.
- **Single-file rig presets** (planned).
- **Skeleton + amp + cab first**; remaining blocks land incrementally,
  each with an offline verification harness before shipping.

## Architecture

- `src/rig/Blocks.h` — `MonoBlock` / `StereoBlock` interfaces
  (prepare / process / latency-report) + passthrough stubs.
- `src/rig/AmpBlock.h` — wraps `nam_aa::AaEngine` (the SAME code the NAM-AA
  plugin runs: 1x–32x dilation-scaled model set, SRC layer, oversamplers,
  routing/latency). Engine internals are invisible to neighbor blocks.
  Includes NAM-AA's post-model DC blocker for sonic parity.
- `src/rig/CabBlock.h` — mono cab IR loader (uniform-partitioned convolution,
  zero added latency).
- `src/rig/RigChain.h` — chain host; total PDC = sum of block latencies.
  Shared verbatim with the offline harness.
- `tests/rig_chain_process.cpp` — offline full-rig processor (console).

## Dependencies

Same clones as NAM-AA-Plugin: `C:/Dev/JUCE`, `C:/Dev/NeuralAmpModelerCore`,
and `C:/Dev/NAM-AA-Plugin/core` (the `nam_aa_core` shared library — added via
`add_subdirectory`). Build like the NAM-AA plugin (MSVC/clang-cl, `/arch:AVX2 /fp:fast`).

## Verification status (2026-06-11, sandbox gcc)

- All sources compile clean against full JUCE headers.
- Chain-host gate: `rig_chain_process` (amp engaged, all stubs passthrough,
  no IR) is **bit-identical** to NAM-AA's `chain_process` + DC blocker at
  48k/4x and 44.1k/4x on a synthetic A2 model. The host adds nothing to the
  signal path.
- PDC reporting flows from `AaEngine::latencySamples` (the formula verified to
  ±0.5 samples across 72 configs in NAM-AA's latency_test).

## Next

1. Real cab IR verification (load IR in harness, measure convolution + PDC).
2. Gate, comp/drive, EQ blocks (DAW-rate DSP, harness-first).
3. Stereo section blocks; single-file rig presets; block UI.
