# Plate FDN rebuild — prototype toolkit (handoff)

Goal: an algorithmic plate whose ENERGY DECAY RELIEF (time x frequency decay surface)
matches the real vintage plate, judged by ear + EDR (NOT summary stats — that was the
trap). See memory `plate-hf-grain-rework` for the full diagnosis.

## Files
- `fdnplate.cpp`  — standalone 32-line FWHT FDN plate prototype (env-tunable). Starting point.
- `wavutil.py`    — float WAV read/write (numpy only).
- `guitar.py`     — regenerates the synthetic dry guitar DI (`dry.f32`/`dry.wav`). Robbie's test source.
- `profile.py`    — IR profiler (centroid, per-band RT60, NED, corr).
- `edrfit.py`     — per-band t@-20/-40 dB decay times (the EDR decay-surface numbers).
- `edr_plot.py`   — side-by-side EDR heatmap (ours vs studio).

## Setup (offline, no JUCE)
1. Re-upload the vintage plate IRs (0.5s..4.5s) — uploads are per-session. Put them in `ir/`.
2. `pip` has no network; numpy + matplotlib are preinstalled. No scipy/soundfile.
3. Build: `g++ -std=c++17 -O2 fdnplate.cpp -o fdnplate`
4. Make dry: `python3 guitar.py`  ; impulse: a 96000-frame stereo f32 with imp[10]=1.
5. Render: `FDN_N=32 FDN_T60LO=3.4 FDN_DAMPF=14000 ... ./fdnplate dry.f32 out.f32`
6. Analyse: `python3 edrfit.py out.f32` and `python3 edr_plot.py out.f32`.

## TARGET — studio decay-vs-frequency T60(f) (the "2.0s" IR):
125:4.41 180:4.17 250:3.97 355:3.57 500:3.34 710:3.09 1k:2.93 1.4k:2.67
2k:2.38 2.8k:2.27 4k:2.06 5.6k:1.80 8k:1.66 11k:1.63  (seconds; smooth monotonic ~2.7:1)

## THE THREE THINGS TO BUILD (the professional recipe)
1. Higher modal density (32+ lines already smooth; keep/raise) so EDR is continuous.
2. MULTI-BAND absorptive damping filter in feedback, FIT to the T60(f) curve above
   (one damping pole = one fixed curve shape = always misses; need shelf+pole(s)).
3. Dense early-reflection stage feeding the tank to shape the FRONT of the EDR.
Iterate against the EDR surface (objective), THEN ear-confirm vs loudness-matched studio.

## Confirmed wins already (independent of rebuild): mod OFF + 3-tap output on the
## CURRENT Dattorro plate — bank anytime into src/rig/ReverbBlock.h.
