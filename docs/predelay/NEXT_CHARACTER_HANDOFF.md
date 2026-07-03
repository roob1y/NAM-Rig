# Pre-Amp Delay Pedal — Handoff for the NEXT character

**READ THIS FIRST before voicing the next delay pedal.** It captures the method,
architecture, standards, and hard-won gotchas from building the Boss DD-7 (the first
of four). The bar here is the drive-pedal bar: **circuit-fit from the REAL schematic,
no guessing, flag what can't be verified, verify offline, judge by ear.**

Related memory (loaded every session via MEMORY.md): `[[pre-amp-delay-pedal]]` (the full
worklog), `[[robbie]]`, `[[drives-build-worklog]]`, `[[drive-io-stages]]`,
`[[capture-interface-hiz-1meg]]`, `[[namrig-offline-build]]`, `[[commit-often]]`.
Per-pedal circuit doc: `docs/predelay/dd7.md` (the DD-7 verified circuit).

---

## 1. What this is

A NEW **mono, front-of-amp delay pedal** — `src/rig/PreDelayBlock.h` — that sits in the
shared pre section (after the drive rack, before the A/B split), switchable pre/post
drive. It is DISTINCT from the existing post-cab **stereo** `DelayBlock`. It hosts four
per-model voicings, chosen by Robbie:

- **0 Boss DD-7** — clean digital. **DONE + circuit-verified.**
- **1 MXR Carbon Copy** — dark analog BBD. **NEXT.** (placeholder voicing, must be replaced)
- **2 EHX Deluxe Memory Man** — lush analog BBD. placeholder.
- **3 Korg SDD-3000** — bright colored early digital. placeholder.

**The Carbon Copy / Memory Man / SDD-3000 voicings currently in the code are educated
placeholders written BEFORE the circuit-research standard. Do NOT trust them. Each pedal
gets its own deep-research pass and a fresh, circuit-grounded voicing.**

---

## 2. THE METHOD (do it in this order — this is what "trained" the DD-7 result)

1. **DEEP-RESEARCH THE REAL CIRCUIT FIRST.** Use the `deep-research` skill (the harness)
   pointed at the pedal's **actual service notes / traced schematic** — not reviews, not
   "typical BBD" assumptions. Demand: real part numbers + component values, cited, with an
   explicit "could NOT verify" list. For the DD-7 the load-bearing source was the Roland
   Service Notes PDF (synfo.nl / manualmachine / elektrotanya mirrors) + the official Boss
   spec page. Prioritize primary sources; forum image scans are often login-gated.
   - Extract, stage by stage: **input impedance + buffer topology + coupling caps**; the
     **BBD/converter** (part, stage count, clock → bandwidth; or bit depth/sample rate);
     **companding** IC (NE570/571? or none); **anti-alias + reconstruction filter corners**;
     the **feedback** path + self-oscillation; the **output buffer + impedance + coupling**;
     the **dry/wet mix topology**; any **modulation** (trimmer rate/width); **max delay**.
2. **TRANSLATE the verified facts into the `Voicing` struct + `IoStage` anchors** (below).
   Where the schematic gives a value, use it. Where it doesn't (flagged "could not verify"),
   use the documented/measurable behavior and SAY SO — never fabricate a number.
3. **WRITE a per-pedal circuit doc** `docs/predelay/<pedal>.md` (like `dd7.md`): the
   stage-by-stage facts, each cited, plus a "could NOT verify" list and the voicing decisions.
4. **OFFLINE-VERIFY** with `tests/predelay_test.cpp` (add/adjust checks for the new voice).
5. **HAND TO ROBBIE for a Windows build + PLAY-TEST.** He judges by ear. The corners/sat/mod
   are ear-tunable starting points — expect to iterate. Offer the **controlled-probe measure
   path** (capture his real pedal, fit to it) for anything the schematic can't pin down.

**NEVER GUESS. Robbie will call it out. If you can't verify it, flag it and offer to measure.**

---

## 3. Architecture (`src/rig/PreDelayBlock.h`)

`class PreDelayBlock : public MonoBlock`. JUCE-free core (Blocks/Lfo/Biquad/Saturation/IoStage).

### The per-model `Voicing` struct (edit `voicingFor(Model)` for the new pedal)
```
bool  bbd;          // analog BBD: bandwidth tracks the clock (darkens with time) + repitch glide
float maxTimeMs;    // the pedal's REAL max delay
float bbdStages;    // total BBD stages -> Nyquist(time) = stages/(4*tSec); ignored if !bbd
float antiAliasHz;  // fixed reconstruction/anti-alias LP ceiling (IN-LOOP, recirculates)
float loopHpHz;     // in-loop low-cut (bass build control); 0 = off
float midHz,midDb,midQ;   // in-loop mid bump (Memory Man's documented mid boost); 0 dB = off
float satDrive,satAsym;   // companding/preamp soft-clip knee (cubic ADAA, in-loop); 0 = clean
float presHz,presDb;      // OUTPUT-ONCE presence sheen (digital top); 0 dB = off
float modRateHz,modDepthMs; // built-in modulation (user Mod knob scales depth)
float glideMs;      // time-change feel: analog = slow pitch-bend swoop; digital = quick
float fbCeiling;    // feedback ceiling; >1 => self-oscillates (loopLimit bounds it)
```
Key physics already implemented and reusable:
- **BBD bandwidth tracks the clock**: `updateBandwidth()` sets the in-loop LP to
  `Nyquist = stages/(4*tSec)` capped by `antiAliasHz` — so analog pedals DARKEN as the
  delay lengthens, digital stay fixed. (DD-7 verified: fixed 19 kHz; Memory Man 1583 Hz @550ms.)
- **In-loop stages recirculate** (HP -> LP -> mid -> user Tone -> compander sat) so repeats
  compound (the authentic "each repeat gets darker/warmer").
- **`loopSat`** = cubic 2nd-order ADAA + cosh even-harmonic + DC block = the BBD compander
  knee / preamp grit. `loopLimit` = a transparent-below-±1.5 headroom limiter so max feedback
  SWELLS to a bounded self-oscillation instead of running away.
- **Mix law = `dry + mix*wet`** (dry stays at unity, wet added on top). This is the authentic
  delay-pedal blend (verified on the DD-7: dry is a fixed analog through-path). NOT a crossfade.
- **`IoStage` per model** (`applyIo()`): the real input/output stage — impedance loading,
  coupling HPs, buffer. DD-7 = transparent buffered (1 MΩ = the DI reference so NO loading
  shelf; subsonic couplings; no HF smoothing). The other models' I/O is `setTransparent()`
  **pending their circuit research** — set the real anchors when you have the schematic.
  See `[[drive-io-stages]]`: buffered/≥1 MΩ = no shelf; low-Z fuzz-class = a loading high-shelf cut.
- **DD-7 MODE** (`Dd7Mode` enum, `setDd7Mode`) is DD-7-SPECIFIC (its 8-position rotary:
  4 time ranges + Hold/Modulate/Analog/Reverse). Other pedals ignore mMode. If the next pedal
  has its own control set (e.g. Carbon Copy's Mod footswitch), model it per-pedal like this.

### Where the wiring lives (touch these for a new pedal only if it needs new params/controls)
- `src/rig/RigChain.h` — the block is a member `predelay`, in `allMonoBlocks()`, processed
  after premod (switchable pre/post drive). Usually NO change per pedal.
- `src/PluginProcessor.cpp` — params `predelay*` in `createParameterLayout()`, bound in
  `updateChainParameters()`. Add per-pedal params only if the pedal needs a new control.
- `src/ui/Panels.h` — `PreDelayPanel` is **model-aware**: `refresh()` shows/relabels the knobs
  per model (DD-7 = D.TIME/E.LEVEL/F.BACK + the MODE **rotary knob**). Give the new pedal its
  AUTHENTIC knob legends + control set here (e.g. Carbon Copy = Delay / Mix / Regen, no Tone).
- `tests/predelay_test.cpp` (+ CMake `foreach`) — add checks for the new voice.

---

## 4. DD-7 result (the reference for "done right") — see `docs/predelay/dd7.md`

Verified from the 2008 Roland service notes: 2SK880 JFET input buffer, **1 MΩ in**; **AK4552
24-bit codec** + µPD800402 DSP + SDRAM; **NO compander** (dropped the DD-2/DD-3 NE570); NJM4558
output buffer, **1 kΩ out**; buffered bypass; **dry analog-unity + wet added**; repeats
**full-band, non-degrading**; **self-oscillates**. Three corrections vs the first guess:
antiAlias 15k→19k (was wrongly darkening), crossfade→dry+wet mix, fbCeiling→1.05 (self-osc).
Behaviors fixed by ear feedback: TIME re-clocks (glide 45 ms = pitch-bend on sweep); FEEDBACK
self-oscillation swell (loopLimit + ceiling); MODE knob (all 8, rotary); REVERSE (grain player,
reads backward at 2× rate — needs a 2× ring). `predelay_test` = T1–T12, **all pass offline.**

---

## 5. Hard-won GOTCHAS (these cost real time — don't relearn them)

- **THE SANDBOX MOUNT TRUNCATES/CORRUPTS reads of files you've edited** (both the src mount
  AND the outputs mount degrade after edits — null bytes, cut-off tails). The Read/Write/Edit
  FILE TOOLS are the source of truth. **To build offline: write the full source into `/tmp`
  via a bash heredoc with a QUOTED delimiter** (`cat > f <<'EOF' ... EOF`) — bypasses the mount
  entirely. Pull committed deps via `git show HEAD:src/rig/X.h`. NEVER `cp` from the src mount
  and trust it. See `[[namrig-offline-build]]`.
- **Offline build recipe** (no JUCE): stub `stub/juce_audio_basics/juce_audio_basics.h` =
  `#pragma once`; deps Blocks.h/Lfo.h/Biquad.h/Saturation.h/IoStage.h; then
  `g++ -std=c++17 -O2 -Wall -Wextra -I. -Istub predelay_test.cpp -o t && ./t`.
- **JUCE-side files (PluginProcessor, src/ui/*) can't be built offline** — review by hand, then
  Robbie builds on Windows.
- **Adding a STRIP block = reindex EVERY hardcoded tile index in `BlockStrip.h`**: `slots[]`,
  `setManualLed`, the mode-switch, `paint()` branch/merge/chevrons, AND `updateLeds()`. Missing
  `updateLeds()` clobbered the PREDLY/EQ bypass LEDs on a timer (the "offset bypass" bug). Also
  the editor `mPanels` array + size. (Not needed per-pedal — only when adding a whole block.)
- **Commit on Windows, not the sandbox** — sandbox git shows phantom diffs from truncation.
- **Robbie judges by EAR.** Ship circuit-grounded starting points; expect ear-driven iteration.
  He hates guessing and wants controls authentic to the hardware.

---

## 6. NEXT: MXR Carbon Copy (M169) — what its research must nail

Do a `deep-research` pass on the **real M169 schematic** (freestompboxes/diystompboxes traced
schematic, the MXR M169 manual, ElectroSmash-style analysis). Get, cited:
- **BBD**: which chip(s) and how many stages (V3205SD / BL3208 / MN3xxx?) → sets max delay
  (~600 ms) and the clock→Nyquist behavior. Confirm the "two internal trimmers" = mod width + rate.
- **Companding**: which compander IC (NE570/571?) and its emphasis network.
- **Reconstruction / anti-alias filters**: the actual corner(s) that make it DARK (the Carbon
  Copy's signature is a low fixed LP — get the real corner, don't assume ~2.6 kHz).
- **Input/output impedance + buffer topology** (for `IoStage`): is the input buffered/high-Z or
  loading? output Z? coupling caps?
- **Feedback (Regen)** range + whether/how it self-oscillates; any in-loop tone shaping.
- **The Mod switch**: rate/depth of the chorus warble (from the trimmers/LFO).
Then write `docs/predelay/carbon_copy.md`, replace the `kCarbonCopy` `Voicing` + `applyIo()`
anchors + the panel's authentic knob legends (Delay / Mix / Regen; Mod switch), add a test
check, and hand to Robbie for the play-test. Then Memory Man, then SDD-3000, same way.
