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

- **EP Boost II** (Boost model 3, Echoplex EP-3 / Xotic EP Booster): the pure EP-3
  JFET common-source stage measures ~FLAT across audio (fit `ep3_response.py`) — its
  character is clean headroom + very high input Z + JFET 2nd-harmonic, FULL-RANGE
  (opposite of the Rangemaster). Voiced as the Xotic EP Booster: full bass
  (`lowCut 15`) + a gentle broad presence shelf (low-Q peak ~5 kHz, +4 dB) + a small
  JFET `bias 0.10` for warmth. High headroom — mostly clean (THD ~1–5% single-coil
  across the sweep), a little hair only when cranked. `gMin 1.3/gMax 6`,
  `outTrim 0.74` A/B-matches the stand-in. Tests T26–T29; worked example in
  [`ep3.md`](ep3.md). **UNCOMMITTED** — offline build green (drive_test 44 CHECKs),
  pending commit on Windows + play-test. Voiced to the **Xotic** flavor by choice
  (Robbie's call); the pure-EP-3 flat alternative is noted in the doc if he wants it.

- **Round Fuzz II** (Fuzz model 1, germanium Fuzz Face): the first rework that
  needed new engine DSP. Voicing is just a bass trim (one-pole low-cut ~50 Hz,
  bright, no top roll, fit `fuzz_face_response.py`). NEW **clip type 4 = asymmetric
  cubic**: positive knee at 1 (rail +2/3), negative knee at `kn = 1−bias` (rail
  −(2/3)kn) → asymmetry that PERSISTS at high gain (soft small-signal → tilted
  square cranked); polynomial → exact F1/F2 → **2nd-order ADAA** (−44 dB alias vs
  naive, peak-guarded, validated in isolation: kn=1 == the cubic exactly). NEW
  **`gate` field** (0..1, zero-fill so every existing model is unaffected): envelope
  gate that collapses the output on decay (the bias-starved splat). Touch/volume
  **cleanup** via the cubic path's `dynDepth` (now serves clip 4). `gMin 8/gMax 200`,
  `bias 0.45`, `dynDepth 0.50`, `gate 0.60`, `outTrim 0.65`. **Processor fix:** the
  Fuzz case was hardcoded `setModel(s,0)` (the 2nd-model gotcha) → now reads
  `bModel`. Tests T30–T35; worked example in [`fuzz-face.md`](fuzz-face.md).
  **UNCOMMITTED** — offline build green (drive_test all pass, 0 failures), pending
  commit on Windows + play-test. Voiced germanium FF + all three behaviors (cleanup,
  gate, ADAA) per Robbie's call. The gate is **RELATIVE to the note's peak**
  (peak-hold instant-attack/~500ms-decay, gate on env/peak) so it's input-level
  independent — the attack blooms clean at any level, only the decay tail chokes
  (the first absolute-threshold version gated quiet rigs constantly; fixed +
  T37 regression). `gate` depth 0.6, ratio knee 0.20..0.55, `dynDepth` 0.5 are
  ear-tunable taste params. The gate is a **UI toggle** (default ON):
  per-slot `fGate` bool param → `DriveBlock::setGateOn()`; the Fuzz panel shows an
  Off/Gate `SegmentedControl` when `modelHasGate()` is true (Round Fuzz II only),
  placed in the glyph area. Test T36 covers the toggle. The Panels.h/processor
  param edits are UNTESTED in a build (JUCE can't compile offline) — Robbie builds.

- **Super Drive** (Overdrive model 2, Boss SD-1) — the first *new* model (not a
  rework). The SD-1 is OD-1/TS lineage, so the small-signal voicing fits ~the
  TS808 (fit `sd1_response.py`, RMS 0.63 dB: same ~+5 dB hump @ 720–900 Hz,
  slightly fuller bass / a hair brighter — the SD-1's "more open" reputation,
  confirmed by the circuit, not guessed). The IDENTITY is **asymmetric clipping**
  (3 diodes, 2+1 → a ~2:1 threshold ratio): built on **clip type 4 (asym cubic)**
  so the even-harmonic crunch PERSISTS at gain (a symmetric clip + DC bias would
  just square up symmetrically). Real ratio 2:1 = `bias 0.50`, softened to
  **`bias 0.35` (kn 0.65)** for the diodes' soft feedback-loop knee — clearly
  asymmetric (h2/h1 ~0.04–0.075 low/mid drive, still ~14× the symmetric GD2 even
  cranked), milder than the fuzz. **Noticeably hotter** than GD2 (`gMin 6/gMax 120`)
  + a touch more output (`outTrim 1.25`, noon RMS 1.07× GD2). Same feedback-clip
  feel as GD2 (pre/de-emphasis `10@700`, clean blend 0.15, dynamics 0.40), static
  voicing, mid post-clip. **Engine change:** the pre/de-emphasis pair is now enabled
  for clip 3 **OR 4** when `emphDb > 0` — Round Fuzz II has `emphDb 0` so it is
  unaffected (byte-exact); no other shipped model changes. Auto-gain `O2` table
  measured (near-flat ~0.40, like the fuzz's `F1` — the asym clipper compresses).
  Tests T38–T44 (incl. the maxabs no-spike sweep + alias cut). Worked example in
  [`sd1.md`](sd1.md). **UNCOMMITTED** — offline build green (drive_test **68 CHECKs,
  0 failures**), pending commit on Windows + play-test. UI just works (the Overdrive
  case already reads `bModel`; menu is generic; `bModel` 0..3 covers model 2).

- **Gold Horse** (Overdrive model 3, Klon Centaur) — the first model whose identity
  is the **parallel clean sum**, not the clipper. NOT a TS: the op-amp gain stage is
  a ~1 kHz **band-pass** (fit `klon_response.py`, RMS 0.14 dB) clipped by a
  **symmetric** germanium hard clip (clip 1 + 2nd-order ADAA, like Black Rodent II),
  then summed with a big parallel **clean** path = the "transparent overdrive".
  **Engine change:** the clean-blend path (previously soft-poly only) was added to
  the **hard-clip branch**, guarded so Black Rodent II (`cleanBlend 0`) stays
  byte-exact (regression T46). The clean is the **RAW input** (full-range, restores
  the lows the mid-focused clip drops), at input level (no crackle), scaled by the
  new constant **`kCleanScale 3.5`** to the clip's ±1 level so `cleanBlend 0.50` is
  genuinely heavy (without it the ×preGain clipped path swamps the clean). `dynDepth
  0.30` (touch). **`shapeTrack 1`** = the hump blooms with Drive → near-clean boost
  at low Drive (THD ~0.001), dirties up cranked (the Klon reputation). `gMin 2/gMax
  70`, bright/open `lpHz 4700`, `outTrim 0.95` (≈ GD2 at noon; the boost lives on the
  Level knob, kept safe so the clean sum doesn't overshoot — worst |out| 1.39 < 1.5).
  Auto-gain `O3` measured. Tests T45–T50. Worked example in [`klon.md`](klon.md).
  **UNCOMMITTED** — offline build green (drive_test **79 CHECKs, 0 failures**),
  pending commit on Windows + play-test. UI just works (Overdrive reads `bModel`;
  menu generic; model 3 fills the last `bModel` 0..3 slot — no param widening).
  **PluginDoctor-verified** vs the Nembrini "NA Clon Minotaur": ours matches the
  real Klon's broad ~1 kHz band-pass more closely than the commercial clone (which
  is darker/narrower up top) — Robbie's A/B, gain+treble at noon. **Per-model control
  labels:** the Gold Horse panel shows **Gain / Treble / Output** (the Klon's own
  control names) while the TS-family OD models keep Drive/Tone/Level — captions only,
  params (oDrive/oTone/oLevel) unchanged (Panels.h `configure()` case 2, keyed on
  `model == 3`). **Treble = the real Klon active high-shelf** (DONE): new voicing
  field `trebleShelfDb` (0 = legacy tilt → all other models byte-exact). Implemented
  as a **proper 1st-order high-shelf** (zero fixed at `pivotHz`, **pole at
  pivotHz·G**, bilinear, per-block coeffs, one state pair `shX1/shY1`) so the LF
  passband stays FLAT at full boost (measured +0.26 dB @ 100 Hz vs the derived
  circuit's +0.25 dB). An earlier low/high-*blend* shelf leaked ~+6 dB into the lows
  and was replaced. Asymmetric +18/−8 dB (cut = 0.44× boost), noon = flat, and a
  *boost* shelf so Treble-up raises level (+11.7 dB broadband, authentic, vs a tilt's
  ~neutral). Gold Horse = `trebleShelfDb 18 @ pivot 408`. Test T51. Tone audit of the other drives: GD II (TS) + Super Drive (SD-1) already use a
  proper treble shelf (the cubic/asym-cubic `softPoly` path forces bass-fixed); Black
  Rodent II = the RAT "Filter" LP (its real tone); Boost/Fuzz have no tone control;
  the v1 legacy stand-ins (Green Drive 0, Black Rodent 0) keep the symmetric tilt by
  design (byte-exact A/B refs). So tone is now circuit-modelled on every shipped "II"/
  new model.

Current model inventory: Boost (4: Range '65, EP Boost, Range '65 II, EP Boost II),
Overdrive (**4**: Green Drive, Green Drive II, **Super Drive**, **Gold Horse**),
Distortion (**3**: Black Rodent, Black Rodent II, **Violet Ram**), Fuzz (2: Round
Fuzz, Round Fuzz II).
**NB: `bModel` 0..3 is now FULL for BOTH Boost AND Overdrive.** Distortion has room
for 1 more (3/4); Fuzz has room for 2. Adding a 5th model to Boost or Overdrive (or any beyond 0..3)
needs the `bModel` param range widened (PluginProcessor.cpp `bModel` AudioParameterInt
0,3 → 0,4 + the UI menu IDs). New engine bits available for reuse: **clip type 4
(asym cubic + 2nd-order ADAA)** — with optional pre/de-emphasis — the **`gate`**
field, and now a **hard-clip clean blend** (raw-input, `kCleanScale`) for parallel
clean/dirty sums.

---

## 5. What's OPEN — the actual next work

Pick one and run the §1 playbook workflow on it.

**Reworks** (existing simple stand-ins → give them the GD2/Black Rodent II
treatment):

All four stand-in reworks are now **DONE**:

| Stand-in | Reworked as | Model |
|----------|-------------|-------|
| Range '65 (Boost) | **Range '65 II** (Dallas Rangemaster) | Boost model 2 |
| EP Boost (Boost) | **EP Boost II** (Echoplex EP-3 / Xotic EP Booster) | Boost model 3 |
| Black Rodent (Distortion) | **Black Rodent II** (ProCo RAT) | Distortion model 1 |
| Round Fuzz (Fuzz) | **Round Fuzz II** (germanium Fuzz Face) | Fuzz model 1 |

(Green Drive → Green Drive II was the original worked example.) Boost's `bModel`
slots (0..3) are now full. Remaining drive work is **new models** (§ below), not
reworks.

**New models** to add alongside (add, don't replace):

| Pedal | Topology delta | Engine knobs |
|-------|---------------|--------------|
| ~~**SD-1**~~ | **DONE — Super Drive** (Overdrive model 2, [sd1.md](sd1.md)) | clip 4 asym cubic (bias 0.35), TS-fit EQ, gMin 6/gMax 120, outTrim 1.25; emphasis now enabled on clip 4 |
| ~~**Klon**~~ | **DONE — Gold Horse** (Overdrive model 3, [klon.md](klon.md)) | clip 1 hard + 2nd-order ADAA + heavy raw-input clean blend (new hard-clip blend path, `kCleanScale`), ~1 kHz band-pass, shapeTrack bloom; fills bModel 0..3 |
| **Blues Breaker** | softer, symmetric, open low end | cubic clip, gentler emphasis, less bass-cut. → **needs `bModel` widened** (Overdrive is now FULL at 0..3) OR put it in another category |
| ~~**Big Muff**~~ | **DONE — Violet Ram** (Distortion model 2, [big-muff.md](big-muff.md)) | new 2-stage soft-clip CASCADE (`muffStages` + `muffLpHz` + `kMuffStage2Gain`), cubic ×2, circuit-fit −6.5 dB scoop, moderate-default/hot-ceiling range, see-saw Tone. Distortion now 3/4 |

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

---

## 8. Big Muff — ✅ DONE (Violet Ram, 2026-06-28)

**Built** as **Distortion model 2 "Violet Ram"** (Ram's Head '73): a real 2-stage
soft-clip CASCADE (new `muffStages`/`muffLpHz` fields + `kMuffStage2Gain`, both
zero-fill → 324-config regression byte-exact vs HEAD), circuit-fit −6.5 dB scoop
(ElectroSmash measured), see-saw Tone, moderate-default/hot-ceiling Sustain range.
drive_test 96 CHECKs, 0 failures (T52–T57). Worked example: [big-muff.md](big-muff.md).
**UNCOMMITTED** — offline build green, pending commit on Windows + play-test. The
prep notes below are retained for reference / the era variants.

**Original goal:** add a Big Muff as **Distortion model 2** (Distortion has room: 2/4, so
`bModel` 0..3 fits — no param widening). Disguised name TBD (scheme: pick something
evocative-but-not-the-brand, e.g. "Green Mountain" / "Civil War" / "Triangle Fuzz").
Ask Robbie up front (he likes to choose): **clip character, how scooped the mids,
gain/output, and which Big Muff era** (Triangle / Ram's Head / Green Russian / NYC —
they differ mostly in the tone-scoop depth and the clipping diodes).

**Why it's the "bigger change":** the Muff is **two cascaded clipping stages**
(each a transistor gain stage with **symmetric soft clipping**, diodes in feedback),
then the famous **passive mid-SCOOP tone stack**, then a recovery stage. Our engine
is a **single shaper per slot** — so the Muff needs either:
  - (a) a real **2-stage cascade** in the process loop (new path: clip → inter-stage
    EQ → clip), the faithful route; or
  - (b) a **single hotter soft-clip** approximation (clip 3 cubic, high gain, lots of
    sustain) + the scoop. Cheaper, less authentic. **Ask Robbie which.**
The **mid scoop** is a **negative `midDb`** (the RBJ peak does notches fine) — the
Muff Tone knob sweeps a blend of a low-pass and a high-pass with a midrange notch;
model the notch with negative `midDb` and the Tone as a tilt/shelf, or fit the real
passive network (ElectroSmash has the exact tone-stack response).

**Reusable bits now available** (all guarded/zero-fill → byte-exact for shipped
models): clip 1 hard + 2nd-order ADAA, clip 3 cubic, clip 4 asym-cubic; the
**hard-clip clean blend** (raw-input × `kCleanScale`); the **`trebleShelfDb`** active
shelf; the **`gate`** field; pre/de-emphasis on clip 3/4. The Muff likely wants
**cubic soft clip ×2** (smooth/saturated) + **negative midDb scoop** + a low-ish
`lpHz` (Muffs are dark) + high gain/sustain.

**Research:** ElectroSmash "Big Muff Pi Analysis" (full schematic + the tone-stack
math + the two clipping stages); Coda Effects "Big Muff mods and tweaks"; Kit Rae's
Big Muff history (the era/version differences). Derive the tone-stack response and
the clipping-stage EQ, fit as usual (`*_response.py`).

### Hard-won mechanics from the SD-1/Klon session (read these — they save hours)

- **The sandbox bash mount TRUNCATES file reads** (DriveBlock.h/drive_test.cpp come
  back cut off near the tail). **Build offline from `git show HEAD:<path>`** (reads
  the object store, full) **into `/tmp`**, re-apply the session's edits there with a
  small Python `str.replace` script (assert each replace hits exactly once), then
  `g++ -std=c++17 -O2 -I/tmp/... `. JUCE stub: a 1-line `juce::jlimit` header. The
  **Read/Write/Edit file tools are the source of truth** (NOT truncated) — edit the
  real repo files with those; only bash reads are truncated.
- **Sandbox git is unreliable here:** the truncated mount makes `git diff`/`status`
  show **phantom modifications** (e.g. `ep3_response.py`, `sd1_response.py` appear
  "modified" = a chopped last line) and the index can get a stuck `index.lock`
  ("Operation not permitted") + spurious staged deletions of `reverb_test.cpp`/
  `rig_chain_process.cpp`. **Commit on Windows**, add files **by explicit name**,
  ignore the phantoms, and `del .git\index.lock` if it complains.
- **Set test thresholds to MEASURED reality**, not a priori guesses — probe the new
  model in a tiny C++ harness first, read the numbers, then write the CHECK bounds
  (with a little cross-compiler margin; Robbie builds on MSVC).
- **Auto-gain tables (`O`/`O2`/`O3`/...) are measured** with a pink-noise probe
  (`rms_in/rms_out` per drive point); hard clippers come out near-flat.
- **Per-model control labels** live in `Panels.h` `configure()` (keyed on `model`);
  **per-model voicing** is just a new zero-filled `Voicing` field + a guarded branch.
