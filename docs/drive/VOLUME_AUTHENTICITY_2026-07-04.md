# Drive Volume/Level authenticity — 2026-07-04

Robbie's observation: the drive pedals' Volume/Level knob doesn't feel authentic —
at noon they're quite loud, and he suspected unity gain sat in the wrong place on
the knob. This pass measured our old behaviour, researched the real pedals'
output pots, and reworked the Level control into an authentic per-model Volume pot.
Method as always: measure ours → research the real circuit → static per-model fix →
offline test → Robbie's ear (no reactive gain-riding, see `no-dynamic-autolevel`).

## The bug (measured)

The old Level knob was a symmetric **±12 dB trim in dB, centred at noon (0 dB)**,
multiplied by a per-model `outTrim` that had been tuned to loudness-*match* the
models to each other (and to the old stand-ins). Result, measured at Drive noon
with a 220 Hz sine at a humbucker-ish 0.2 peak (output RMS vs input RMS):

| model | old noon gain | reachable at knob min (−12 dB) |
|---|---|---|
| Green Drive (TS808) | **+12.9 dB** | +0.9 dB (can't reach unity) |
| Super Drive (SD-1) | **+13.3 dB** | still hot |
| Gold Horse (Klon) | **+12.6 dB** | still hot |
| Breaker Drive (Bluesbreaker) | **+12.8 dB** | still hot |
| Black Rodent (RAT) | **+10.0 dB** | −0.0 dB |
| Round Fuzz (Fuzz Face) | **+7.2 dB** | −4.8 dB |
| Violet Ram (Big Muff) | **+9.9 dB** | −2.1 dB |

So every drive was **+7 to +13 dB hot at noon**, and the four overdrives were so
hot that *even at the Level knob's minimum they stayed above unity* — you could
never get them to bypass level, let alone below it. Robbie's ear was right. The
root cause was structural: the knob was a small dB trim sitting on a hot,
loudness-matched makeup, so it never modelled where unity actually sits on a real
Volume pot.

Boost models (Range '65, Plex Boost) have **no Level knob** and were left alone —
the real Dallas Rangemaster and Echoplex EP-3 have no output/level pot (their one
knob *is* the gain). This is authentic and unchanged.

## The real pedals (researched)

From ElectroSmash / GGG / Aion BOMs and circuit analyses (pot value + taper +
output-stage behaviour), plus player consensus on where unity sits:

| Pedal | Level pot | Boost on tap | Unity ~clock | Character |
|---|---|---|---|---|
| TS808 Tube Screamer | 100 kΩ **audio** | modest | ~10–11:00 | not a hot pedal |
| Boss SD-1 | 100 kΩ **linear** | modest | ~10–11:00 | TS-class output |
| Klon Centaur (Output) | 10 kΩ linear (network-shaped) | **large** | ~9–10:00 | famously loud clean boost |
| Marshall Bluesbreaker | 100 kΩ **linear** | **very low** | near max (~2–3:00) | notoriously low output |
| ProCo RAT (Volume) | 100 kΩ **audio** | **large** | ~9:00 | gets loud fast |
| Fuzz Face (Volume) | 500 kΩ **audio** | **~unity max** | near max (~2–3:00) | deliberately ≈ unity out |
| Big Muff (Ram's Head) | 100 kΩ **log** | **very large** | ~9:00 or lower | painfully loud past noon |
| Dallas Rangemaster | *no level pot* (10 k "Set" = the gain) | — | — | bright hot boost, no makeup |
| Echoplex EP-3 / EP Booster | *no level pot* (one boost knob) | +3…+20 dB | min only | always boosting |

The takeaway: real drive Volume pots are **attenuators from a hot internal clipped
signal down to silence**, so unity sits *below* the top of the sweep and the amount
of boost on tap varies hugely — Bluesbreaker/Fuzz Face barely reach unity even wide
open, while Klon/RAT/Muff are very loud. Taper splits **audio** (TS, RAT, Fuzz Face,
Muff) vs **linear** (SD-1, Bluesbreaker).

## The fix

The Level/Volume control is now an authentic **per-model Volume pot**, static, in
`DriveBlock` (`volFor()` + `volPot()`, wired into `process()`; the param is now a
0..1 knob shown 0..10 pedal-style). Two parts per model:

* a **unity makeup** (`unityTrimDb`) that cancels the model's internal drive gain,
  so the knob reads directly as output-vs-input dB (measured from the internal
  gain; drive-dependence is *preserved*, not cancelled — see below);
* a **pot curve** from silence (knob 0) through `noonDb` (knob 0.5) up to `maxDb`
  (full CW), with a `taper` exponent on the lower half (**1 = linear pot, ~2 =
  audio/log pot** — more sweep spent fading to silence, so the usable range sits
  higher, like a real audio Volume).

Boost keeps its fixed voicing makeup (no volume knob). Nothing in the drive *tone*
or *clipping* changed — the Volume pot is a pure output-stage level map.

### Result (measured, Drive noon, same 0.2 peak sine)

| model | taper | our unity clock | noon gain | max (full CW) | min |
|---|---|---|---|---|---|
| Green Drive (TS808) | audio | **11:27** | **+2.0 dB** | +9.0 | silence |
| Super Drive (SD-1) | linear | **11:00** | **+1.9 dB** | +8.9 | silence |
| Gold Horse (Klon) | linear-ish | **10:11** | **+5.1 dB** | +18.1 | silence |
| Breaker Drive (BB) | linear | **2:58** | **−3.0 dB** | +2.0 | silence |
| Black Rodent (RAT) | audio | **10:46** | **+4.9 dB** | +15.9 | silence |
| Round Fuzz (Fuzz Face) | audio | **2:23** | **−1.9 dB** | +2.1 | silence |
| Violet Ram (Big Muff) | log | **10:44** | **+5.1 dB** | +18.1 | silence |

Every knob now reaches silence at minimum, unity is reachable, and the per-model
character matches the hardware: TS/SD modest with unity ~11:00; Klon/RAT/Muff loud
with unity below noon and big boost on tap; Bluesbreaker/Fuzz Face quiet with unity
up near max (you run them wide open). Noon dropped from +7…+13 dB to −3…+5 dB.

**Drive → output is preserved** (authentic, not normalised away): Green Drive at
the noon knob reads +1.3 / +2.0 / +2.6 dB at Drive 0.2 / 0.5 / 0.8 — more drive,
more output, exactly as a real pedal.

## What changed (code)

* `oLevel` / `dLevel` / `fLevel` params: `±12 dB` → **`0..1` Volume pot**, default
  0.5 (noon). The UI already shows these as a 0..10 rotation knob (unchanged).
* `DriveBlock.h`: `setLevelDb` → `setLevel(0..1)`; new `VolCurve` / `volFor(Kind,
  model)` per-model pot table + `volPot()` curve; `levelLin` now = unity-makeup ×
  pot curve (Boost keeps `outTrim`). No tone/clip changes.
* `PluginProcessor.cpp`: param defs + bindings updated to the 0..1 knob.
* `tests/drive_test.cpp`: helpers drive the 0.5 (noon) knob; the naive alias
  mirrors are now **level-matched to the engine's Volume pot** (`lvlOf()`) so the
  ADAA/alias tests stay honest; new **T71** unity-point suite (silence-at-min,
  unity reachable, per-model authentic noon window, loudness ordering, Drive→
  output, Boost-has-no-knob). Full suite: **190 checks, 0 failures** offline.

## Flagged / unverified (no guessing)

* **Preset/automation impact — intended.** Changing the Level param range from
  `±12 dB` to `0..1` means saved presets / DAW automation that stored the old dB
  value will load out of range and clamp (a stored `0.0 dB` → knob 0 = silence).
  This is the deliberate cost of the full pot rework (Robbie's call). **Old
  sessions need their drive Level knobs reset.**
* **SD-1 Level taper.** 100 kΩ value is solid; **linear** is the widely-cited stock
  taper (vs the TS's audio) but I couldn't pull an official Boss parts number
  confirming the taper letter. High-confidence, not primary-sourced.
* **Loud-pedal unity clock.** Research puts Klon/RAT/Muff unity ~9:00; ours lands
  ~10:10–10:45. That's a **deliberate balance** — placing unity at 9:00 would need
  noon hotter than +5 dB, which reintroduces the "noon too loud" complaint. They
  still have big boost on tap (+16…+18 dB at max). Easy to push hotter per Robbie's
  ear (raise `noonDb`/`maxDb` in `volFor`).
* **Unity calibration used a single 220 Hz tone.** The internal-gain measurement
  (`unityTrimDb`) is broadband-approximate; real guitar loudness may differ a dB or
  two per model. Ear-test is the gate; `noonDb` per model in `volFor` is the one
  number to nudge.
* **Rangemaster / EP-3 have no level pot** — correctly left with no Volume knob.
  (Range '65's −5.5 dB reading at 220 Hz is the treble-booster bass cut, not a
  level issue.)

## Verify / build

Offline (sandbox): the reworked engine + tests build and pass —
`g++ -std=c++17 -O2 drive_test.cpp -o drive_test && ./drive_test` → **RESULT: ALL
PASS (190 checks)**. Robbie builds the authoritative MSVC exe
(`build-clang\drive_test_artefacts\Release\drive_test.exe`) and play-tests the plugin
before the single scoped commit. The knob to tune by ear is `noonDb` (and `maxDb`)
per model in `DriveBlock::volFor()`.
