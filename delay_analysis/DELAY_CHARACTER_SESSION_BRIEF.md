# Delay Characters — New-Session Brief (battery test + character validation)

Paste this (or point the new chat at this file) to kick off a focused session on the
NAM Rig **delay characters**. Goal of that session: **build a reusable metric "battery"
for the delay characters and use it (with graphs) to judge whether the Tape Echo and
Space Tape voicings actually match their measured references** — the same way the reverb
battery judges the reverb characters.

---

## 1. What NAM Rig's delay characters are

`src/rig/DelayBlock.h` — one delay engine with a `Character` selector:

- **Clean** (0) — transparent digital delay; byte-for-byte the original engine (regression-locked by `delay_test` T11).
- **Tape Echo** (1) — single-head tape echo, voiced to a **measured tape-echo reference**.
- **Space Tape** (2) — multi-head tape echo-style **multi-head** tape echo (3 playback heads at fixed ratios **1 : 1.9 : 2.76**), 12 head/mode combinations, auto-coupled to the rig's Spring reverb on the reverb modes.

Status (as of this brief): both characters implemented and **test-green** (`delay_test` 33/33 PASS). A Warp/Twist momentary-hold experiment was tried and **removed entirely** — don't resurrect it; it's out of scope. The delay-character DSP is otherwise considered done pending this validation pass.

Branch: `feature/delay-rework`.

---

## 2. The voicing spec (what "right" means)

`DelayVoicing` (the per-character parameter set) and the two voiced values, straight from `voicingFor()`:

| field | meaning | Tape Echo | Space Tape |
|---|---|---|---|
| satDrive | record-level soft-clip drive (bounds the loop) | 1.2 | 1.4 |
| headBumpHz / Db / Q | **in-loop** low-mid "head bump" bloom | 330 Hz / +4.0 / 0.6 | 300 Hz / +2.5 / 0.6 |
| gapLossHz | **in-loop** head-gap HF roll-off corner (caps High Cut) | 2100 Hz (**2-pole**) | 2000 Hz (**1-pole**, gentler) |
| outBassHz / Db | **output-once** bass shaping | 480 Hz / **−6** (thins) | 180 Hz / **+2.5** (boosts) |
| wowMul / flutterMul | wow + flutter depth (× base) | 0.05 / 0.055 | 0.05 / 0.055 |
| driftHz / driftDepthMs | slow transport wander | 0.5 Hz / 0.05 ms | 0.5 Hz / 0.05 ms |
| preampShelfHz / Db | **output-once** FET-preamp high-shelf | 1400 Hz / +4.0 | 2500 Hz / 0 (off) |
| fbCeiling | feedback ceiling | 1.10 | 1.10 |

**The defining difference:** Tape Echo = bigger bloom (+4 @330), 2-pole HF roll, and **thins** the lows at output (−6 @480) with a bright FET shelf. Space Tape = smaller bloom (+2.5 @300), gentle **1-pole** HF roll, and **boosts** the lows at output (+2.5 @180). Get these two timbres distinct and matching their references and the characters are right.

**Critical architecture lesson (already baked in — preserve it):** the tape EQ is split into a **PER-PASS (in-loop)** stage that compounds down the tail (head bump + gap-loss) and an **OUTPUT-ONCE** stage that shapes timbre without compounding (outBass + preamp shelf). A single-repeat fit alone gives the wrong tail — you must fit the in-loop transfer from a **multi-repeat** capture.

---

## 3. Reference captures — `delay_ref/`

Private A/B renders the user made by playing test signals through their **licensed** reference plugins. **Do NOT commit these to the repo** (see §7 IP rule) and do not name the source products anywhere.

| file | what it is | render settings used |
|---|---|---|
| `ref_impulse.wav` | Tape Echo, impulse → repeats | 350 ms, lowest feedback, 100% wet |
| `ref_levels.wav` | Tape Echo, input level sweep (for saturation) | level steps, 100% wet |
| `ref_sustain.wav` | Tape Echo, sustained tone (for wow/flutter) | 100% wet |
| `ref_tail.wav` | Tape Echo, multi-repeat tail (for per-pass transfer) | higher feedback, 100% wet |
| `se_impulse.wav` | Space Tape, impulse → repeats | 100% wet, single mode |
| `se_heads.wav` | Space Tape, all-heads impulse (head taps) | all-heads mode, 100% wet |
| `se_levels.wav` | Space Tape, input level sweep | 100% wet |
| `se_tail.wav` | Space Tape, multi-repeat tail | feedback ~default, 100% wet |

When the new chat needs fresh/cleaner captures, ask the user to render them with these settings (fully wet, noise/ducking off, tape-age centred, a single clean test signal).

---

## 4. Offline build — this WORKS now (verified 2026-06-27)

The engine compiles offline against a tiny JUCE stub. From the repo root in the sandbox:

```bash
mkdir -p /tmp/stub/juce_audio_basics && echo '#pragma once' > /tmp/stub/juce_audio_basics/juce_audio_basics.h
g++ -std=c++17 -O2 -I src -I /tmp/stub tests/delay_test.cpp -o /tmp/dtest && /tmp/dtest
```

A render harness can `#include "rig/DelayBlock.h"` directly. Minimal usage:
```cpp
DelayBlock d; BlockContext ctx; ctx.sampleRate=48000; ctx.maxBlockSize=512; d.prepare(ctx);
d.setCharacter((int)DelayBlock::Character::SpaceTape); // or Tape
d.setHeadMode(10);          // Space Tape head/mode index (0..11)
d.setTimeMs(350.0f); d.setFeedback(0.30f); d.setMix(1.0f);
// settle on silence, then feed an impulse / tone / level-sweep and capture d.process(L,R,n)
```
(`BlockContext` member is `maxBlockSize`, not `maxBlock`.) Output is **mono** (L==R) for both tape characters.

**Mount gotcha:** the bash mount of the repo can serve large files truncated and lags file-tool edits — the Read/Write/Edit tools are the source of truth. For offline builds, prefer pulling clean copies via `git show <ref>:path` into `/tmp` if anything looks short. (This pass it served the full 738-line `DelayBlock.h` fine, but verify.)

---

## 5. The metrics that actually decide a delay character

(Analogue of the reverb battery's "modal depth / T30(f) / C80" — these are the delay-specific ones.)

1. **Single-repeat magnitude response** (1/6-oct, 40 Hz–16 kHz, on ONE isolated echo). Shows the head bump (low-mid peak) and the gap-loss HF roll. *The tonal fingerprint.*
2. **Per-pass transfer = rep[n+1]/rep[n]** band-energy ratio (from the tail capture). This **cancels the output-once stage** and exposes the **in-loop** EQ — THE metric for whether the tail darkens/blooms correctly. *Most important and most often gotten wrong.*
3. **Head bump** fit: peak frequency + gain + Q (from #1 or #2).
4. **Gap-loss corner**: −3 dB HF frequency **and slope** (1-pole vs 2-pole — Space Tape is gentler than Tape).
5. **Output tilt**: low-shelf (bass cut vs boost) + preamp high-shelf — the output-once stage (single repeat minus per-pass).
6. **Saturation curve**: output peak vs input level (from the level sweep) — the soft-clip knee + the feedback self-oscillation asymptote.
7. **Wow/flutter**: instantaneous pitch of a sustained tone → depth (%) + rate, separating slow wow (~0.5–1 Hz) from flutter (~6 Hz). Target ~0.1 % peak (subtle/analogue, NOT warbly).
8. **Self-oscillation**: at max feedback, sustains and stays bounded by the saturation.
9. **Space Tape only:** head taps land at **1 : 1.9 : 2.76** (verified); per-mode head masks; free-mode head-1 range ~69–177 ms; sync snaps the **leading active head** to the host division; auto-spring on the reverb modes (5–12).

For each metric: print **ours / reference / Δ** and **graph ours vs reference** (overlaid), per character, the way `reverb_battery.py` does.

---

## 6. Build the battery like the reverb one

Template: **`reverb_analysis/reverb_battery.py`** (+ `README.md`, `METRICS.md`, `REVERB_CHARACTER_PLAYBOOK.md`). Mirror its shape:

- A **C++ render harness** (`delay_analysis/delay_render.cpp`) that drives the real `DelayBlock` and writes `.f32` for: impulse→repeats, level sweep, sustained tone, and a tail. One small CLI: `--char tape|space --mode N --test impulse|levels|sustain|tail --out x.f32`.
- A **Python analyzer** (`delay_analysis/delay_battery.py`) that loads `--ours x.f32` and `--ref delay_ref/….wav`, computes the §5 metrics, prints the ours/ref/Δ tables, and saves overlaid graphs to `outputs/`. Reuse `reverb_analysis/wavutil.py` for WAV reading.
- A **`DELAY_CHARACTER_PLAYBOOK.md`** capturing the §5 verdict-reading rules + the per-pass-vs-once lesson (analogue of `REVERB_CHARACTER_PLAYBOOK.md`).

Suggested graph sheet (one PNG per character): single-repeat spectrum (ours vs ref), per-pass transfer (ours vs ref), output tilt, saturation curve, wow/flutter pitch trace; plus a head-taps panel for Space Tape.

---

## 7. Hard rules (carry these into the new session)

- **IP / trademark scrub.** The references are captures from the user's **licensed** reference plugins. **Never** put the source product, manufacturer, or any vintage hardware brand/model trademark names in code, comments, docs, or UI strings. Use only generic descriptive terms: "a measured tape-echo reference", "a measured multi-head tape echo reference", "the single-head Tape", "the multi-head unit". The shipped character names are the generic **"Tape Echo"** and **"Space Tape"**. **Do not commit `delay_ref/`** to the repo.
- **Loudness-match before any A/B**, and compare like-for-like (same dry, same wet%, same time/feedback).
- **Ears are the final judge** — the user judges DSP by ear. Use the metrics to localise problems and to render A/B demos; relax brittle asserts rather than contort the engine to hit a number.
- **Don't touch the Clean path** (regression-locked) and don't reintroduce Warp/Twist.
- Sandbox **can't git-commit** (.git is read-only) — hand the user commands; they commit on Windows.

---

## 8. First moves for the new chat

1. Read `src/rig/DelayBlock.h` (`DelayVoicing`, `voicingFor`, `processSpaceTape`, the filter stages) and `reverb_analysis/reverb_battery.py`.
2. Build the render harness + battery (§6); confirm the offline build (§4).
3. Profile `delay_ref/` references and our matching renders; produce the graphs.
4. Report the ours/ref/Δ verdict per character and per metric; flag any gaps (e.g. per-pass HF too bright, bump too strong, wrong output tilt).
5. Only then propose voicing tweaks — by ear, with A/B demos.
