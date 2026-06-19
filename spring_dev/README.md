# Spring reverb dev — sandbox harness (persisted for continuation)

The dispersive-FDN spring prototype + measurement tools. Rebuild offline (no JUCE) per
the `namrig-offline-build` memory: g++ -std=c++17 -O2 -I. <file>.cpp, with a stub
`stub/juce_audio_basics/juce_audio_basics.h` containing just `#pragma once`.

## Files
- `fdn_proto.cpp` — THE evolved spring engine prototype. Built on FdnReverb (16-line FWHT,
  proven smooth) + input STRETCHED-allpass dispersion (APk, env K) = spring chirp + dense
  short early diffuser (EARLY) + direct early tap (EARLYTAP, EARLYHP) + band-limit. Many env
  knobs: DISP APA K EARLY EARLYG EARLYTAP EARLYHP SIZE FDAMP DIFF PRE DARK HICUT MOD SCOOP
  SCOOPHZ PRES SWELL IMPLEN. Reads INWAV (process a file) or renders an impulse.
- `profile.cpp` — IR profiler (per-band RT60, NED, centroid, L/R corr).
- `wavio.h` — WAV read/write (float32/PCM).
- `bloomfit.py` — bloom-fingerprint measurement + param-sweep scaffold (START POINT for the
  CMA-ES / black-box optimizer experiment).
- `analyze.py` / `bloom.py` / `cmp2.py` / `cmp3.py` — spectral / decay / bloom analysis figures.
- `dry.wav` — the test-guitar phrase (Karplus-Strong). Browser tuner uses a seeded JS copy.
- `src/` — the engine headers pulled from git HEAD (FdnReverb etc.) for offline compile.

## Best-tuned recipe so far (Robbie, in the browser tuner @44.1k):
decay=3 K=6 APA=0.75 tone/FDAMP=6500 EARLYTAP=0.35 EARLY=3 SIZE=1.0  (HICUT=tone*0.83)
At 48k bump FDAMP->~8000 to match brightness (engine is sample-rate dependent; the chirp
stretch K is in SAMPLES — should be made time-based when baked in).

## The open problem: BLOOM
studio spring low-mids bloom (peak ~85ms); our FDN low-mids peak ~376ms and a swell can't pull a peak
earlier than the signal's natural peak. Hand-tuning size made it noisy. NEXT: black-box
optimizer (CMA-ES / differential evolution) over the engine params + structural freedom
(delay lengths, feedback matrix, an early-reflection stage) against a perceptual loss that
INCLUDES the per-band energy-decay/bloom envelope. See refs in the memory.
