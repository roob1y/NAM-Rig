# Building drives — playbook

How to make any `DriveBlock` voicing *accurate* and good-feeling — whether you're
**adding a new model** (SD-1, Klon, Blues Breaker, Big Muff, …) or **reworking an
existing one** (the current Boost / Distortion / Fuzz, the same way Green Drive
became Green Drive II). The method is the same for all of them; the Tube Screamer
is just the first worked example. Written so a future session can pick up cold
without re-deriving anything.

> The existing Boost/Distortion/Fuzz are *robust as-is* but not yet
> circuit-fit/reworked. When you rework one, treat it like a fresh pedal: run the
> whole workflow below (circuit fit → clip shape → ADAA → feel → tests). Their
> current simplicity (1st-order ADAA, no clean blend) is why they don't crackle
> today — a rework that adds those features must add the matching guards.

Read this first, then the companions:
[option-a-design.md](option-a-design.md) (the DSP design),
[circuit-accuracy.md](circuit-accuracy.md) (the fit method),
[tube-screamer-circuit.md](tube-screamer-circuit.md) /
[overdrive-family.md](overdrive-family.md) (circuit references).
Worked reworks: [proco-rat.md](proco-rat.md) (RAT → Black Rodent II) is a full
second example of this playbook end-to-end.

---

## 0. The one rule that matters most

**For a voicing question with a known circuit, fit the circuit — don't trust the
ear-A/B or another plugin.** We voiced the TS hump by ear/A/B against AmpliTube
Overscream and got it ~2× too tall (+11.5 dB vs the real +5.5 dB). The schematic
settled it in minutes (RMS 0.66 dB). Components are published for almost every
classic pedal; derive the transfer function and fit. See §4.

A reference plugin (Overscream, etc.) tells you *a* voicing, not *the* voicing —
it may be simplified or voiced to taste. The circuit is ground truth.

---

## 1. Workflow for any drive (new model OR reworking an existing one)

1. **Research the circuit.** Pull the schematic + BOM (ElectroSmash, GeoFex,
   AionFX, Electric Druid are reliable). Identify: the gain stage topology
   (feedback-clip vs clip-to-ground), the diode arrangement (symmetric /
   asymmetric / hard / soft / LED / germanium), the EQ (where the bass-cut,
   mid-hump, treble-roll live, and whether they're pre- or post-clip), the pot
   tapers, and any clean blend. Write findings into a `docs/drive/<pedal>.md`.
2. **Derive the clean voicing curve** from the schematic (small-signal, diodes
   open) — see §4. This is the target frequency response.
3. **Pick the clip shape** (§3) and the topology toolkit pieces it needs (§2).
4. **Fit the voicing filters** to the derived curve (§4) → `midHz/midDb/midQ`,
   `lowCutHz`, `lpHz`.
5. **Set the drive range + taper** (§5) and the **tone topology** (§6).
6. **Add it as a `Model`** in `modelsFor()` (keep existing models byte-for-byte
   so old presets/tests don't move — add, don't edit).
7. **Wire the UI** if the category gains its first 2nd model: the type menu +
   `bModel` param are generic (see §8), but check `PluginProcessor.cpp` reads
   `bModel` for that category (the Overdrive case did NOT until we fixed it).
8. **Write measurement tests** (§7) and build offline (§9). All green before commit.
9. **Commit small, by filename** (see `GIT_SAFETY.md` / the commit-often rule).

---

## 2. The toolkit — ways to make a drive better / more authentic

Each is a `Voicing` field or a process-path feature. Reach for the ones the real
circuit needs; leave the rest at 0 (a voicing with everything 0 is a plain
shaper, fully backward-compatible).

| Technique | What it buys | How |
|-----------|-------------|-----|
| **Circuit-fit EQ** | the voicing is *right*, not guessed | derive curve, fit `midHz/midDb/midQ`, `lowCutHz`, `lpHz` (§4) |
| **Feedback-clip emulation** (pre/de-emphasis) | frequency-selective clipping — bass clipped least, smooth top, no fizz; the TS/SD-1/Klon "feel" | `emphDb`/`emphHz`: high-shelf +emphDb into the clipper, −emphDb after. Transparent small-signal; only bites under drive |
| **Clean blend** | transparency + dynamics; never "pure fizz" | `cleanBlend`: sum a little un-clipped signal back after the clip (TS "secret" / Klon feed-forward). Small (~0.2) = TS-ish; large = Klon-ish |
| **Envelope dynamics** | touch sensitivity — soft picking cleans up, digging in bites | `dynDepth`: input follower nudges the clean blend. Saturates fast (loud → 0 clean); ~0.3–0.5 is plenty |
| **2nd-order ADAA** | much lower aliasing on the cubic clip (~36 dB vs naive) | use clip type 3 (cubic); ADAA2 is automatic in the process loop |
| **Drive taper = log** | the knob *sweep* feels like the pot | the engine's `gMin·(gMax/gMin)^drive` IS a log/audio taper. Don't add a warp (we tried; it was wrong) |
| **Static vs blooming voicing** | shaper-at-drive-off vs scoop-that-blooms | `shapeTrack`: 0 = EQ always on (real fixed tone stack, works as an always-on shaper); 1 = EQ blooms with the Drive knob |
| **Tone topology** | the Tone knob feels right | treble-shelf (bass fixed, TS) via the `clip==3` path; or the engine default symmetric tilt. See §6 |
| **Asymmetry** | even-harmonic "crunch" (SD-1, fuzz) | `bias` (input bias for clip 0/1/3; negative-rail for clip 2) |

---

## 3. Clip shapes (`Voicing.clip`)

| clip | shape | antialias | use for |
|------|-------|-----------|---------|
| 0 | `tanh` (soft) | 1st-order ADAA (F1 = logcosh) | treble booster, legacy OD |
| 1 | hard clip ±1 (sym) | 1st-order ADAA | distortion (RAT) |
| 2 | hard, ASYM rails | 1st-order ADAA | germanium fuzz |
| 3 | cubic soft `x − x³/3` | **2nd-order ADAA** (poly F1+F2) | modern OD (TS-style) — lowest alias, soft knee |

Adding a new shape: implement `f`, its antiderivative `F1` (for 1st-order ADAA),
and — if you want 2nd-order — `F2`. Polynomials are gold (cheap exact F1/F2;
`tanh` needs the dilogarithm for F2, which is why the cubic exists). The cubic
math + ADAA formula is in [option-a-design.md](option-a-design.md).

### ADAA order — when to use which (hard-won)

Decide ADAA order **per shaper, when you build/rework it** — not globally.

- **1st-order ADAA is the safe default.** Single guarded division
  `(F1(x)-F1(x0))/(x-x0)` with a midpoint fallback for tiny `dx`. No catastrophic
  cases. The current Boost/Distortion/Fuzz use it and are glitch-free (verified:
  worst output < 1.0 across a full harsh sweep). Always start here.
- **Reach for 2nd-order ADAA when a rework needs it** — typically a hotter or
  sharper clipper (a reworked Distortion/Fuzz, a higher-gain OD) where 1st-order
  still fizzes. It can cut aliasing a lot (the cubic OD measured ~50 dB vs naive
  in the full pipeline). But it is **not free**:
  - It divides by `x[n]-x[n-2]`, which is 0 at signal peaks → ÷0 spikes. **Must**
    have the peak guard (§Crackle pitfalls).
  - ~1 sample of group delay.
  - Only **polynomial** shapers can do it cheaply (need a closed-form `F2`).
    `tanh` and the asym-fuzz's tanh half need the dilogarithm → swap them for a
    polynomial shape first (that's exactly why the OD's `tanh` became the cubic).
  - **Verify it actually wins in the FULL pipeline**, per shaper. On a bare hard
    clip / tanh, 2nd-order measured *no better* (sometimes worse) than 1st-order —
    so confirm a real reduction before keeping it, don't assume.
- **The crackle was Overdrive-specific because only the OD had the two triggers**
  (a clean blend and 2nd-order ADAA). The others don't crackle *today* simply
  because they're still simple. When you rework one to add a clean blend or
  2nd-order ADAA, bring the matching guards with it.

---

## 4. Deriving + fitting a voicing from the schematic (the core skill)

1. Write the **small-signal transfer function** `H(f)` of the gain stage + EQ +
   tone-stack pole, diodes open (clean). For a non-inverting feedback-clip stage:
   `A(f) = 1 + Z2(f)/Z1(f)`, times input HPF, times post LP. See
   `ts808_response.py` for a worked TS808 example — copy it for the next pedal.
2. **Normalise** to a body frequency (we used 200 Hz) and tabulate dB.
3. **Fit** our digital `lowCut·midPeak·topLP` to it (coordinate descent in the
   same script; the digital filter responses are replicated at 48 kHz so the fit
   targets exactly what the C++ runs). Target RMS < ~1 dB.
4. **Verify in C++**: a small-signal Goertzel sweep of the real `DriveBlock`
   should land on the target (we hit ~1 dB across 50 Hz–5 kHz).

Notes:
- Most voicing is **drive-independent** (small-signal hump barely moved across
  the TS Drive pot) → fit at min drive and use `shapeTrack 0`.
- The **driven** character (frequency-selective clipping) is NOT in the
  small-signal fit — that's the pre/de-emphasis, tuned separately.
- Tone is usually fit at **noon**; the active tone network shifts the high
  shelf — approximate with the treble-shelf and accept it tracks the right
  direction. A per-tone-position fit is a future refinement.

---

## 5. Drive range + taper (the sweep feel)

- **Taper:** leave it. `preGain = gMin·(gMax/gMin)^drive` is exponential =
  equal dB per rotation = a log/audio-taper pot, which is what real drive pots
  are. We front-loaded it once with a curve exponent and it felt wrong — reverted.
- **Range:** pick `gMin`/`gMax` so the *clipping* sweep matches the pedal:
  - `gMin` sets how dirty the **minimum** is. Real pedals (TS) already break up a
    little at min — set `gMin` so there's audible grit at Drive 0 (we used 3.0 →
    ~1–2 % THD on mids). Too clean a minimum feels wrong.
  - `gMax` sets the top. A TS is moderate (we used 33); a distortion is much hotter.
  - Measure THD-vs-knob (§7) and aim for the pedal's character: TS = dirty by
    noon then compresses; high-gain = keeps climbing.
- **Range must match the real circuit, and it's INPUT-LEVEL dependent.** The clip
  threshold is fixed, so distortion tracks the absolute input level — a real TS's
  effective gain is ~12..118, and `gMin 3 / gMax 33` (4× too low) left max Drive
  barely breaking up below a hot DI. Use ~`gMin 5 / gMax 80`. Keeping the
  input-dependence is *correct* (hot pickups drive harder, like the real pedal);
  voice the range for the app's **calibration reference** (`CalNorm
  kReferenceDbu`) so a calibrated guitar is level-accurate. Verify the THD-vs-knob
  sweep at realistic levels for BOTH a single-coil (~0.08) and a humbucker
  (~0.20) — the humbucker should clearly drive harder.
- **Gain vs touch dynamics trade-off:** higher gain = more drive but LESS cleanup
  at mid-Drive (everything clips). Touch response lives at low-to-mid Drive; test
  it there. Keep `gMin` modest so soft picking still cleans up.
- **A/B fairness:** when shipping an old + new model side by side, level-match
  them (`outTrim`) so the comparison is about tone, not loudness (within ~5 % RMS
  across the sweep).

---

## 6. Tone topologies

- **Treble-shelf (TS, SD-1):** bass/low-mids fixed, Tone moves treble only. In
  the engine: force `bassG = 1` (the `clip==3` path does this) so the tilt
  becomes a high-shelf; set `pivotHz` ~1.2 kHz. Measured TS-correct: 100 Hz <0.3
  dB across the sweep, 5 kHz ±~8 dB.
- **Symmetric tilt (engine default):** bass rises as treble falls (see-saw). Not
  TS-like; fine for some voicings.
- **Tone↔hump interaction:** real TS tone *levels the hump* as it goes up (adds
  treble back, filling the hump's high side). Our treble-shelf does this relative
  to the treble; a more literal model (shelf corner overlapping the hump) is a
  possible refinement if a pedal needs it.

---

## 7. Verification — measurement-first tests (`tests/drive_test.cpp`)

Every claim gets a measured test. Patterns already in the harness (Goertzel for
single-bin magnitude, `harmRatio` for THD-ish, `realSlotM` to pick a model):

- **Voicing fit:** small-signal sweep lands on the circuit curve (or assert the
  hump height/center: 780 vs 100/3k).
- **Alias:** hot 5 kHz sine, fold-back at 3 k/13 k far below a naive memoryless
  mirror sharing the same voicing.
- **Dynamics:** loud-vs-quiet harmonic spread, larger for the dynamic model than
  a static one; test at the edge-of-breakup mid (660 Hz, low drive) where it's
  clearest.
- **Tone:** treble moves, bass fixed (or the tilt direction).
- **Drive sweep:** THD-vs-knob has the right shape / minimum grit.
- **Regression:** existing models stay **byte-for-byte** (T11 pattern) — add
  models, never edit shipped ones.

---

## 8. UI / param wiring

The type-pick menu (`Panels.h`) and the per-slot `bModel` param are **generic**:
a category with `modelCount > 1` automatically shows a model submenu/dropdown
built from `modelName()`. So adding a 2nd model to a category *just works* in the
UI — **except** the processor must actually read `bModel` for that category.
`PluginProcessor.cpp` `applyDrive()` had Overdrive hardcoded to model 0; we
changed it to `setModel(s, (int)g("bModel"))` (mirror the Boost case). Check this
for any category getting its 2nd model. `bModel` range is 0..3 (fine for now).

JUCE code can't be compiled offline — review the processor/editor by hand.

---

## 9. Building + committing

- Offline DSP build recipe (stale repo mount + FUSE quirks, JUCE stub needs
  `juce::jlimit`): see memory `namrig-offline-build`. TL;DR — assemble in `/tmp`
  local FS from the pristine mount + injected blocks via `python3`, don't
  round-trip big writes through the `outputs` mount (it truncated a 300-line
  write once).
- Commit small, **by filename**, after each verified change (memory
  `commit-often` / `GIT_SAFETY.md`). Prefer committing on Windows (autocrlf).

---

## 10. Lessons learned (this build)

- **Circuit beats ear-A/B.** The over-humped TS (+11.5 vs +5.5 dB) *looked*
  right next to a flat-voiced competitor. Deriving from the schematic fixed it.
- **The drive pot is the log map.** Don't warp the taper to "front-load" — the
  exponential gain map already is the log/audio pot.
- **Minimum should do something.** Real pedals break up a little at min; raise
  `gMin` until Drive 0 has audible grit.
- **Static voicing = shaper.** `shapeTrack 0` matches the real fixed tone stack
  and enables the "Drive off, Tone past noon" mid-shaper trick. Bloom
  (`shapeTrack 1`) is a stylisation, not the circuit.
- **Feel = taper + range + voicing**, not just the clip math. A perfect clipper
  with the wrong sweep still feels wrong.
- **Add, don't edit.** Ship new pedals as new models; keep old ones byte-exact
  for A/B and zero preset drift.
- **Separate clean voicing from driven character.** Small-signal EQ (fit to
  circuit) and frequency-selective clipping (emphasis) are tuned independently.

### Crackle pitfalls (these caused a real, audible crackle)

- **Blend clean at INPUT level, not the gained signal.** `clean = u` (post-gain)
  summed back the 33×-amplified signal; the envelope ripples at the note pitch,
  modulating that huge chunk in/out → spikes to ~10. Fix: `clean = u / preGain`
  (input level — which is where the real TS's surviving clean actually sits).
  This was the *main* crackle, and it's independent of the anti-aliasing.
- **2nd-order ADAA divides by `x[n] − x[n-2]`, which hits zero at signal peaks**
  (where `x[n] == x[n-2]` by symmetry while `x[n-1]` is the peak) → a divide-by-
  zero spike of thousands. Guard it: when `|x − x[n-2]| < TOL`, fall back to the
  **1st-order ADAA over the step** `(F1(x) − F1(x1))/(x − x1)` — note F1, *not*
  `cubD` (which uses F2 and returns the wrong scale). With this guard + the clean
  fix, 2nd-order ADAA is robust (worst |out| 0.9 across 50 Hz–23.5 kHz, full
  scale, all drives). Verify any new shaper with a `maxabs` sweep, not just a
  Goertzel/THD test — a Goertzel bin averages over spikes and hides them.
- **NaN hygiene:** flush non-finite state (`flushD`) and never emit a non-finite
  output sample — one bad sample latched into a filter state bricks the whole
  rig until reset.

---

## 11. Targets (same method — new models AND reworks)

**Rework the existing drives** (currently simple stand-ins; give them the GD2
treatment — circuit-fit voicing, real shaper, ADAA, calibrated feel):

| Existing model | Real circuit to fit | Likely upgrades |
|----------------|---------------------|-----------------|
| ~~**Black Rodent** (Distortion) → RAT~~ | **DONE — Black Rodent II** ([proco-rat.md](proco-rat.md)) | circuit-fit EQ (pre-clip mid-hump bloomed with Drive), hard clip on **2nd-order ADAA** (measured +10 dB vs 1st-order in the full pipeline — F2 + peak guard), "Filter" LP tone (darker CW), gMin 4/gMax 150 calibrated |
| **Round Fuzz** (Fuzz) | Fuzz Face / Tone Bender (germanium, bias-starved, asym) | bias/sag modelling, input-impedance interaction; 2nd-order needs a polynomial recast of the tanh half |
| **Range '65 / EP Boost** (Boost) | Rangemaster / EP-3 | input-cap voicing already there; circuit-fit the exact corners |

**New models** to add alongside:

| Pedal | Topology delta | Engine knobs |
|-------|---------------|--------------|
| **SD-1** | TS with **asymmetric** diodes | start from Green Drive II, add `bias` (asym), refit if EQ differs; +gain/+louder |
| **Klon** | **hard** clip + heavy parallel **clean blend** | clip 1 (hard) + large `cleanBlend`, LP clean feed for low end; very transparent/dynamic |
| ~~**Blues Breaker**~~ | **DONE — Breaker Drive** (Overdrive model 4, [bluesbreaker.md](bluesbreaker.md)) | symmetric cubic, open lows (lowCut 20), gentle presence shelf (static), mild emphasis, soft range; widened `bModel` 0..4 |
| **Big Muff** | cascaded clipping stages, scooped mids | needs a 2-stage path + a mid *scoop* (negative midDb) — bigger change |

For each (new or rework): research → derive curve → fit → pick clip shape +
**ADAA order (§3)** → taper/tone/range (calibrated) → crackle-guard if it gains a
clean blend or 2nd-order ADAA → test (maxabs + suite) → commit.
