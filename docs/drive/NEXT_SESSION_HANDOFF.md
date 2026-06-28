# Drive improvements — next-session handoff

Everything a fresh chat needs to start improving the drive pedals cold. Written
2026-06-28. Companion to the existing drive docs (read those too); this file is
the "where we are, what to do next" layer on top.

---

## 0. Start here (orientation order)

1. This file (state + plan).
2. [`building-drives-playbook.md`](building-drives-playbook.md) — THE method. The
   workflow for any pedal (new or rework): research circuit → derive clean-voicing
   curve from the BOM → pick clip shape → fit filters → calibrate range/taper/tone
   → add as a **new** model → wire UI → measurement tests → commit small.
3. [`option-a-design.md`](option-a-design.md) — the DSP design (cubic soft-clip,
   ADAA math, pre/de-emphasis, clean blend, dynamics).
4. [`circuit-accuracy.md`](circuit-accuracy.md) — how we fit a voicing to a
   schematic (the TS808 worked example, RMS 0.66 dB).
5. [`proco-rat.md`](proco-rat.md) — a full second worked example end-to-end
   (RAT → Black Rodent II).

---

## 1. Git / repo state

- **Work on branch `Drives-Improvement`.** That's the drives workspace.
- As of this handoff the merge chain was: delay rework → `main` (via PR #5,
  `main` = `558efa8`), and `main` → `Drives-Improvement` (clean fast-forward,
  `Drives-Improvement` is an ancestor of `main`). Confirm with
  `git log --oneline -3` that `Drives-Improvement` has the "Merge pull request #5"
  delay commit; if not, run `git merge main` while on the branch (it fast-forwards).
- Remote is `origin` = https://github.com/roob1y/NAM-Rig.git. **Push/commit from
  Windows**, not from the agent sandbox — the Linux mount has FUSE quirks and a
  recurring stuck `.git/index.lock` ("Operation not permitted" to delete from
  Linux). If git complains about a lock, delete `C:\Dev\NAM-Rig\.git\index.lock`
  on Windows first. The agent CAN read the repo and edit source/doc files fine;
  it just shouldn't drive commits/merges.
- Outstanding loose ends (optional cleanup, not blockers): a couple of duplicate
  doc commits exist on `feature/delay-rework` (same "add Space Tape/Tape Echo
  session docs" message twice, plus two gitignore commits). Those latest doc +
  `.gitignore` commits live only on `feature/delay-rework`, not in `main`. Not
  needed to start drive work.
- `.gitignore` now ignores regenerable scratch (`delay_analysis/demos/`,
  `delay_analysis/outputs/`, `Guitar plugin design exploration/`, `.claude/`) but
  keeps the `.md` design/research docs tracked. Same convention as
  `reverb_analysis/`: track code + docs, ignore renders/wavs.

---

## 2. Where the code lives

- **Engine:** `src/rig/DriveBlock.h` (header-only). 3-slot series drive rack,
  shared pre-split feeding both rigs.
- **Tests:** `tests/drive_test.cpp` — measurement-first, currently T1–T20 (49
  CHECKs). Build offline (DSP-only; JUCE can't compile in the sandbox — review
  `PluginProcessor.cpp` / `PluginEditor.cpp` / `Panels.h` by hand). Offline build
  recipe: assemble in `/tmp` local FS, see memory `namrig-offline-build`.
- **UI/params:** `src/PluginProcessor.cpp` (`applyDrive()` reads per-slot params),
  `src/ui/Panels.h` (pedalboard UI, type/model menus). The type menu + per-slot
  `bModel` param are **generic** — a category with >1 model auto-shows a submenu,
  BUT the processor must actually read `bModel` for that category (the Overdrive
  case was hardcoded to model 0 until we fixed it; check this for any category
  getting its 2nd model). `bModel` range 0..3.
- **Research/derivation scripts:** `docs/drive/ts808_response.py`,
  `docs/drive/proco_rat_response.py` — copy these to derive the next pedal's
  small-signal transfer function from its BOM and coordinate-descent fit our
  digital filters to it.

---

## 3. The engine model (the `Voicing` struct)

Categories (`Kind`): Off, Boost, Overdrive, Distortion, Fuzz. Per slot: Drive,
Tone, Level, on/off footswitch, model select, range switch (treble-boost only),
optional global Auto Gain (off by default).

Signal path per slot:
`drive gain → pre low-cut → mid/treble peak → waveshaper (ADAA) → post LP → DC
blocker → tone tilt → level`.

`Voicing` fields (in `DriveBlock.h`; all the clip-3 "extras" default 0 = plain
shaper, fully backward-compatible):

| field | meaning |
|-------|---------|
| `clip` | 0 soft tanh · 1 hard sym · 2 hard ASYM rails (fuzz) · 3 cubic soft (modern OD) |
| `gMin`,`gMax` | pre-gain range (linear), **log-mapped** from the Drive knob (already an audio-taper pot — don't warp it) |
| `lowCut`,`midHz`,`midDb`,`midQ`,`lpHz` | the voicing EQ (fit to the circuit) |
| `bias` | asymmetry (even harmonics): input bias for clip 0/1/3, negative-rail for clip 2 |
| `pivot`,`outTrim`,`shapeTrack`,`midPost` | tone pivot; level match; EQ blooms-with-Drive (1) vs static (0); mid peak post-clip (1) vs pre (0) |
| `emphDb`,`emphHz` | **pre/de-emphasis** high-shelf (clip 3): frequency-selective clipping — bass clipped least (the TS/RAT "feel") |
| `cleanBlend` | 0..1 clean signal summed back after the clip (transparency; TS-ish small, Klon-ish large). **Blend at INPUT level (`u/preGain`), not the gained signal** — that was the crackle |
| `dynDepth` | 0..1 envelope → clean-blend (touch: soft picking cleans up) |
| `toneFilterHz` | >0: Tone = a sweepable post-clip low-pass (the RAT "Filter") |
| `adaa2` | >0: run this clip through **2nd-order ADAA** (needs polynomial F2 + peak guard); 0 = 1st-order |

Models defined in `modelsFor()` — the comment header above the table lists the
column order. **Add models, never edit shipped ones** (old presets/tests must
stay byte-for-byte; there are regression tests asserting model 0 == legacy).

---

## 4. What's DONE (two full circuit-fit reworks)

- **Green Drive II** (Overdrive model 1, TS808): cubic soft-clip, 2nd-order ADAA,
  pre/de-emphasis, clean blend, envelope dynamics; voicing fit to the schematic
  (RMS 0.66 dB), `gMin 5 / gMax 80` (~real TS 12..118). Tests T11–T14.
- **Black Rodent II** (Distortion model 1, ProCo RAT): LM308 gain-stage EQ fit
  pre-clip (blooms with Drive — bass clips least), hard clip on 2nd-order ADAA
  (+~12 dB alias cut vs 1st-order), "Filter" sweepable LP tone, `gMin 4/gMax 150`.
  Tests T15–T20.
- **Range '65 II** (Boost model 2, Dallas Rangemaster): the whole audio-band
  voicing is the 5nF input cap into the ~12k input Z = a 1st-order high-pass at
  ~2.65 kHz, flat above (fit to RMS 0.01 dB, `rangemaster_response.py`). It's a
  TREBLE booster — stays bright, no top roll. Real `Gv = gm·Rc ≈ 80` (38 dB) →
  `gMin 4/gMax 80` (the stand-in's gMax 20 was ~4× too low, the early-TS bug).
  Soft germanium tanh clip with off-centre `bias 0.30` → asymmetric even-harmonic
  warmth (h2/h1 0.113 vs the stand-in's 0.045). HP is pre-clip + static so bass
  clips least. `outTrim 0.50` A/B-matches the stand-in. Tests T21–T25; full
  worked example in [`rangemaster.md`](rangemaster.md). **UNCOMMITTED** — offline
  build green (drive_test 42 CHECKs all pass), pending commit on Windows + play-test.

Current model inventory: Boost (3: Range '65, EP Boost, Range '65 II), Overdrive
(2: Green Drive, Green Drive II), Distortion (2: Black Rodent, Black Rodent II),
Fuzz (1: Round Fuzz).

---

## 5. What's OPEN — the actual next work

Pick one and run the §1 playbook workflow on it.

**Reworks** (existing simple stand-ins → give them the GD2/Black Rodent II
treatment):

| Model | Real circuit to fit | Notes |
|-------|--------------------|-------|
| **Round Fuzz** (Fuzz) | Fuzz Face / Tone Bender (germanium, bias-starved, asym) | model sag + input-impedance interaction; 2nd-order ADAA needs a polynomial recast of the tanh half |
| **EP Boost** (Boost) | EP-3 (Echoplex preamp) | input-cap voicing there; circuit-fit the exact corners + FET stage. (Range '65 → **DONE** as Range '65 II, model 2.) |

**New models** to add alongside (add, don't replace):

| Pedal | Topology delta | Engine knobs |
|-------|---------------|--------------|
| **SD-1** | TS with **asymmetric** diodes | start from Green Drive II + `bias`; refit if EQ differs; +gain/+louder |
| **Klon** | **hard** clip + heavy parallel **clean blend** | clip 1 + large `cleanBlend`, LP clean feed for low end; very transparent/dynamic |
| **Blues Breaker** | softer, symmetric, open low end | cubic clip, gentler emphasis, less bass-cut |
| **Big Muff** | cascaded clipping + scooped mids | needs a 2-stage path + negative `midDb` — bigger change |

---

## 6. Hard-won gotchas (don't relearn these the painful way)

- **Fit the circuit, don't trust ear-A/B or another plugin.** We voiced the TS
  hump by ear vs AmpliTube and got it ~2× too tall (+11.5 vs the real +5.5 dB).
  Components are published; derive the transfer function and fit.
- **ADAA order is per-shaper.** 1st-order is the safe default (single guarded
  divided-difference, no catastrophic cases). Reach for 2nd-order only on hotter/
  sharper clippers that still fizz — and only on **polynomial** shapers (needs a
  closed-form F2; tanh needs the dilogarithm, which is why the OD core is cubic).
  Verify it actually wins in the FULL pipeline per shaper.
- **2nd-order ADAA divides by `x[n]-x[n-2]` → ÷0 spikes at signal peaks.** Guard
  it: when `|x-x[n-2]| < TOL`, fall back to 1st-order over the step
  `(F1(x)-F1(x1))/(x-x1)` (F1, not the F2 path). ~1 sample of group delay.
- **Clean blend at INPUT level (`u/preGain`), not the gained signal** — blending
  the 33×-amplified signal back, modulated by the envelope at note pitch, spiked
  to ~10. This was the main crackle, independent of anti-aliasing.
- **Verify with a `maxabs` sweep, not just Goertzel/THD** — a frequency bin
  averages over spikes and hides them.
- **NaN hygiene:** flush non-finite state, never emit a non-finite sample — one
  bad sample latches into the amp/cab filters and bricks the rig until reset.
- **Don't warp the Drive taper** — `gMin·(gMax/gMin)^drive` already is a
  log/audio-taper pot. **Range is input-level dependent** (fixed clip threshold)
  — voice it for the app's calibration reference (`CalNorm kReferenceDbu`) and
  verify the THD-vs-knob sweep for BOTH single-coil (~0.08) and humbucker (~0.20);
  the humbucker should clearly drive harder.
- **Static voicing (`shapeTrack 0`) = the real fixed tone stack** and enables the
  "Drive off, Tone past noon" mid-shaper trick. Bloom (`shapeTrack 1`) is a
  stylisation. Separate the clean voicing (small-signal EQ fit) from the driven
  character (pre/de-emphasis) — tune them independently.

---

## 7. First moves for the next session

1. `git checkout Drives-Improvement`; confirm it has the delay merge (§1).
2. Read the playbook + the relevant worked example (TS for an OD/boost, RAT for a
   distortion).
3. Decide the target pedal (§5) — ask the user which one if unstated.
4. Research its schematic/BOM, write findings into a new `docs/drive/<pedal>.md`,
   copy a `*_response.py` script, derive + fit the curve.
5. Add it as a new model in `modelsFor()`, wire `bModel` if it's a category's 2nd
   model, write measurement tests (maxabs + the suite), build offline, all green,
   then commit small by filename on Windows.
