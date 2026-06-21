# reverb_analysis — the metric-battery toolkit (reusable across chats)

This folder is **self-contained**: a fresh session can run the full reverb
metric battery (the "does our algorithmic character actually match the modelled
reference?" workflow) with nothing else from the repo.

The guiding principle (see `IR_MATCHING_PLAYBOOK.md`): **tests aren't the goal —
sounding like the modelled character is.** Graphs decide whether it's a match;
ears decide whether it's *good*. Relax brittle asserts rather than contort the
engine. Don't over-chase metrics once the ear is happy.

## What's here
- `reverb_battery.py` — the battery. Profiles our render (`.f32` stereo) vs a
  reference IR (`.wav` stereo) across the full metric set and saves graphs.
- `platefdn_driver.cpp` — standalone, **no-JUCE** offline render of the committed
  `PlateFdn` engine (verbatim copy of the engine + `FracDelayLine` + helpers from
  `src/rig/ReverbBlock.h` / `Lfo.h`). Lets you render our plate without a DAW.
- `make_demos_template.py` — builds loudness-matched A/B guitar demos
  (reference-convolved vs our render), folder-per-version, identical filenames.
- `wavutil.py` — numpy-only float/int WAV read/write (no scipy/soundfile needed).
- `METRICS.md` — what each metric means and what a *match* looks like.
- `IR_MATCHING_PLAYBOOK.md` — the strategy (structure-first, the 5 targets, gotchas).
- `RESULTS_plate.md` — latest plate findings vs the reference set.
- `ir/` — **drop your local reference IRs here** (gitignored, per-session).

## IRs are local / per-session
Reference IRs are not committed (`.gitignore` keeps `ir/`). Each new sandbox
starts without them — re-add them to `ir/` (or re-upload) at the start of a
session. Keep a backup outside the repo.

## Quick start (offline, no JUCE — numpy + matplotlib only)
```bash
# 1. build the offline plate engine
g++ -std=c++17 -O2 platefdn_driver.cpp -o platefdn_driver

# 2. make an impulse (10s stereo f32, imp at sample 10) and render our IR
python3 - <<'PY'
import numpy as np; N=480000; x=np.zeros((N,2),'<f4'); x[10]=1.0; x.tofile('impulse.f32')
PY
FDN_T60=4.0 FDN_DAMP=6000 FDN_SIZE=1.2 FDN_PRE=0 ./platefdn_driver impulse.f32 wet.f32

# 3. run the battery (saves plate_battery_<label>.png + plate_edr_<label>.png)
python3 reverb_battery.py --ours wet.f32 --ref "ir/<your reference>.wav" --label 4.0 --out .
```
Driver env knobs: `FDN_T60` (decay s), `FDN_DAMP` (tone Hz), `FDN_SIZE` (0.8–1.6),
`FDN_PRE` (predelay ms), `FDN_NFRAMES` (extra tail frames).

## Adapting to other engines (Spring / Hall / Room / Shimmer)
The battery is engine-agnostic — it only needs an `.f32` render + a reference
`.wav`. To profile a different character, make an analogous offline driver:
copy that engine class + `FracDelayLine` + `reverb_detail` out of
`src/rig/ReverbBlock.h` into a `*_driver.cpp` (same pattern as the plate one),
render its impulse, then point `reverb_battery.py` at it.

## Calibration gotcha learned on the plate
A reference IR's filename decay label is often the **unit's decay control**, not
measured RT60. Always measure: a "2.0s"-labelled plate capture measured ~3.3s at
1 kHz with a 6–8s low-mid bloom. Match the *measured* decay-vs-frequency curve,
then note the knob offset separately.
