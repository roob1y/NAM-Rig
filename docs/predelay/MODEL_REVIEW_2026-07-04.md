# Pre-amp delay model review — all 3 models, 2026-07-04

Critical pass over the mono front-of-amp delay pedal (`src/rig/PreDelayBlock.h`,
`tests/predelay_test.cpp`, the three per-pedal docs). Same method as the drive
review (`docs/drive/MODEL_REVIEW_2026-07-04.md`): the **code is treated as
truth**, docs/tests/handoff cross-checked against it, then each model checked
against the real unit's circuit. Nothing assumed correct. Findings ranked per
model; systemic rot listed once, first. This pass was triggered by the
**KORG SDD-3000 removal (2026-07-04)**, whose deletion left index/name rot, and
by two explicitly-FLAGGED unverified filter guesses (Carbon Copy `antiAliasHz`
2600, Memory Man `antiAliasHz` 3800 / mid 650).

Current inventory (verified, `PreDelayBlock.h:90`):
`enum Model { kDD7 = 0, kCarbonCopy = 1, kMemoryMan = 2, kNumModels = 3 }`.
**Boss DD-7** (digital) · **MXR Carbon Copy** (4× BL3208 BBD, SA571) · **EHX
Deluxe Memory Man** (2× MN3005 BBD, crossfade Blend, triangle LFO). Param
`predelayModel` StringArray = `{"Boss DD-7","Carbon Copy","Memory Man"}`
(PluginProcessor.cpp:274, Panels.h:6295) — 3 entries, consistent. No `predelaySdd*`
params, no `case 3` in the panel, no SDD bindings: **the code strip was clean.**

---

## Systemic: SDD-3000 removal rot (the drive section's disease, milder here)

The SDD-3000 was model index 3. Deleting it and dropping `kNumModels` 4→3 was done
thoroughly in the **wiring** (enum, params, bindings, panel cases, strip) — grep for
`SDD`/`Korg`/`predelaySdd`/`case 3` across `src/` is clean. But three classes of
stale reference survive, all in comments/docs (none affect sound; all mislead the
next session):

**1. A live SDD leftover in the engine comment** (`PreDelayBlock.h:348`): the
in-loop compander soft-clip is annotated `// 13-bit companded grit (model 3).` —
"model 3" and "13-bit" were the SDD's gain-ranging quantizer. The `loopSat` it
labels is now used only by Carbon Copy (1) and Memory Man (2). Misleads a reader
into thinking a 13-bit quantizer model still exists.

**2. The read-first handoff describes a 4-model world that no longer exists**
(`docs/predelay/NEXT_CHARACTER_HANDOFF.md`). It is the designated
read-first doc (this review was told to "read it FIRST but VERIFY it against
code — handoff docs rot"), and it is the highest-priority doc fix:

| Where | Says | Truth |
|---|---|---|
| :19-25 | "It hosts **four** per-model voicings" + lists `3 Korg SDD-3000` | 3 models; SDD removed |
| :24 | "`1 MXR Carbon Copy` — **NEXT** (placeholder voicing, must be replaced)" | Carbon Copy is DONE + circuit-researched |
| :27-29 | "The Carbon Copy / Memory Man / SDD-3000 voicings currently in the code are educated **placeholders** … Do NOT trust them" | CC + MM are circuit-researched (`carbon_copy.md`, `memory_man.md`); SDD gone |
| §6 :146-161 | "NEXT: MXR Carbon Copy … Then Memory Man, then SDD-3000, same way" | all 3 done; there is no "next" pedal |

The doc's **method section (§2) and gotchas (§5) are still correct and valuable** —
only the status/inventory is stale. Fix = repoint it from a "how to voice the NEXT
pedal" doc to a "how these 3 were voiced + how to iterate" doc.

**3. Stale in-code state descriptions (each wrong about the CURRENT code):**

| Where | Says | Truth |
|---|---|---|
| predelay_test.cpp:237 (T8) | `// ceiling 1.03 -> would run away` (Carbon Copy) | CC `fbCeiling` = **1.18** |
| predelay_test.cpp:383 (T13) | `// ceiling 1.05` (Carbon Copy) | CC `fbCeiling` = **1.18** |
| predelay_test.cpp:277,284 (T10) | "its ceiling is 1.0" / `// fb = 1.0 * ceiling 1.0` (DD-7) | DD-7 `fbCeiling` = **1.05** |
| PreDelayBlock.h:204 | DD-7 prose `fbCeiling 1.0` | struct value (`:212`) = **1.05** (line 209 already says 1.05) |
| dd7.md:36 | `fbCeiling 1.0 (sustains …)` | code = **1.05** |
| PreDelayBlock.h:479-486 | applyIo header: "other models' I/O is `setTransparent()` **pending their circuit research** → transparent until verified" | `applyIo()` now sets CC (buffered) + MM (loaded) from their researched docs |
| Panels.h:6360-6365 | "Only the DD-7 is circuit-verified so far … other models keep a **generic** set until each is circuit-researched" | all three cases (0/1/2) carry their real per-pedal control sets; the "generic" set is the now-unreachable `default:` |

Note the test CHECKs themselves are **logically sound** — unlike the drive pass
(T38 loaded model 0 twice; T15 was a tautology), no predelay CHECK fails to
exercise what it claims, and T13/T14 already pin the shipped voicing fields
(bbd/maxTime/stages) the way the drive review recommended. The rot here is
comment-only wrong ceiling numbers, not broken test logic.

---

## DD-7 (model 0) — digital, circuit-verified

Voicing (`PreDelayBlock.h:212`): `bbd=false`, antiAlias 19 kHz (full-band,
non-degrading), `satDrive 0` (no compander), `fbCeiling 1.05`, dry+wet mix law,
transparent buffered I/O (1 MΩ in = DI ref). Verified from the 2008 Roland
service notes (`dd7.md`); the strongest-grounded model in this block.

1. **Rot only** (above): the "1.0" ceiling references in the header comment,
   `dd7.md`, and the T10 test comments should read 1.05. No sonic change.
2. The MODE rotary, REVERSE grain player, and self-oscillation are all
   exercised by T10–T12 and pass. No sonic finding.

## Carbon Copy (model 1) — dark analog BBD

Voicing (`PreDelayBlock.h:179`): `bbd=true`, 600 ms, 8192 stages, antiAlias 2600,
`bwQ` 0.5 (non-resonant), loopHp 100, sat 0.50/0.06, mod fixed (no knob),
`fbCeiling 1.18`, 1 MΩ buffered I/O. Research doc `carbon_copy.md` is honest about
what could not be verified (MXR never published a schematic; community traces are
bot-blocked images).

1. **`antiAliasHz` 2600 remains unverifiable — but is now better GROUNDED, not
   just an ear guess.** The Memory Man's factory calibration (see below) gives a
   real reference for what a same-era 8192-stage BBD reconstruction filter does:
   −3 dB at ~3.2–3.5 kHz with a resonant presence peak. The Carbon Copy is
   universally described as **darker** than the DMM and, critically, has **no
   presence peak** (no Bright switch on the M169; that's the Deluxe M292's
   +4.5 dB @ 1.5 kHz add). So modelling CC as a **non-resonant** (`bwQ` 0.5) LP
   at a corner **below** the DMM's verified ~3.3 kHz is circuit-consistent. The
   2600 figure for our single 2-pole is a defensible perceptual stand-in for the
   real steep (~3 kHz, 30–36 dB/oct) Sallen-Key — a 2-pole at 2600 sits close to
   a 6-pole at ~3 kHz in the guitar band, and the in-loop recirculation steepens
   it on sustained repeats. **Kept at 2600; the flag is upgraded from "ear guess"
   to "derived by comparison to the DMM factory reference, exact corner still
   unverified."** The one true "measure it" path stays: controlled-probe capture
   of Robbie's real M169.
2. **Modulation depth was a fixed 1.3 ms — physically wrong for a BBD.** A BBD's
   warble is the LFO pulling the *clock*, so the pitch deviation is a **percentage
   of the delay time**, not a fixed ms. Fixed-ms means the wobble vanishes at long
   delays and is (relatively) too strong at short ones. Fixed to delay-proportional
   (see the engine change); CC uses a **subtle** fraction (~0.6 %), FLAGGED (no
   published CC depth spec — only the 0.2–2.2 Hz rate range is in the M169 manual).
3. `fbCeiling` 1.18 is behaviour-tuned (self-oscillates past ~2 o'clock,
   bounded by the in-loop compander sat + loopLimit), not a circuit number — the
   real loop gain isn't specced. Left as-is; the stale test comments calling it
   1.03/1.05 are fixed.

## Memory Man (model 2) — lush analog BBD — **the main sonic finding**

Voicing (`PreDelayBlock.h:194`, before this pass): `bbd=true`, 550 ms, 8192
stages, antiAlias 3800, **mid +4 dB @ 650 Hz**, crossfade Blend, triangle LFO,
`fbCeiling 1.06`. The 650 Hz mid and 3800 corner were both explicitly FLAGGED as
unverified.

**The flagged values were wrong, and the fix is circuit-derived, not by ear.**
The load-bearing source is the **factory calibration procedure** (Howard Davis,
EHX, 8/1/1978, archived by David Morrin) — it specifies the delay-path frequency
response at the real test points:

> FREQ. RESPONSE CHECK #1 (at MN3005 pin 7): *"flat up to about 900 Hz, rise to a
> max of about 2 V p-p at around 2.5 kHz … drop back to 1.5 V p-p at about 3.8 kHz
> and roll off sharply above this."* (baseline 1.5 V → peak ≈ +2.5 dB)
>
> FREQ. RESPONSE CHECK #2 (after the NE570 expander): max delay = *"flat … and
> −3 dB at about 3.2 kHz"*; min delay = *"a peak of about +3 dB (×1.4) around
> 2.5 kHz and roll off sharply above 3.5 kHz."*
>
> — <https://sites.google.com/site/davidmorrinoldsite/home/trouble/troubleeffects/electro-harmonix-memory-man/eh-7850-calibration>

So the real DMM voice is **flat below ~900 Hz, a resonant presence peak of ~+3 dB
at ~2.5 kHz, and a −3 dB corner at ~3.2–3.5 kHz that rolls off sharply.** There is
**no boost anywhere near 650 Hz** — the "strong mid boost" the earlier research
read off Morrin's swept-sine trace is actually this **2.5 kHz filter resonance**,
mislocated ~two octaves low.

1. **Mid peak 650 Hz → dropped; reconstruction LP made RESONANT at ~2.5 kHz.**
   Measured (analytic, `Biquad` magnitude): the old voicing peaks +3 dB at ~500 Hz
   and *cuts* 2.5 kHz by −2.6 dB — backwards from the factory curve. Replaced with
   a resonant in-loop lowpass (new per-model `bwQ` field): **antiAlias 3200,
   `bwQ` 1.30, midDb 0**, which measures flat ≤900 Hz, **+3.0 dB peak at 2.53 kHz**,
   0 dB at 3.5 kHz — a direct match to the factory curve, using one filter (the
   real reconstruction filter IS a single resonant multipole). This is the biggest
   audible correction in the block: the DMM's characteristic "present/hi-fi,
   mid-forward chime" comes from this 2.5 kHz peak, which the old voicing inverted
   into a low-mid honk.
2. **`antiAliasHz` 3800 → 3200.** The factory −3 dB corner is ~3.2–3.5 kHz, not
   3.8 kHz. 3200 sits just above the 550 ms clock-Nyquist (3165 Hz with our 0.85
   margin), so the model keeps the *subtle* real time-darkening (FRC#2's 3.5 kHz
   at min delay → 3.2 kHz at max delay) while the fixed resonant filter dominates —
   which is the corrected 8192-stage physics ("the darkness is the fixed filter,
   not the clock").
3. **Modulation depth fixed-ms → delay-proportional, anchored to the factory
   ±10 %.** Calibration: *"at max chorus setting the period should swing approx.
   10 % of its average value."* The old fixed 3.0 ms gave ~10 % only near ~30 ms
   delay and ~0.6 % at 500 ms — i.e. the DMM's signature deep/lush pitch wobble was
   **missing at the long delays it's famous for**. Now `depthMs = 0.10 · delayMs`
   at full Depth (the varicap-on-clock physics). This is the DMM's seasick vibrato
   restored, and it's a **verified** magnitude.
4. **Chorus LFO rate 1.0 Hz → 0.85 Hz.** Factory: chorus = *"slightly less than
   1 Hz"*, vibrato ≈ 4 Hz (vibrato 4.0 already correct). Minor.
5. `fbCeiling` re-checked after the resonant-LP change (the +3 dB resonance adds
   in-loop gain): self-oscillation must stay bounded. See the test/engine notes.
6. Input loading (~100 kΩ inverting, the "dark dry" gotcha) is secondhand but
   consistent; `applyIo` models it as a gentle high-shelf cut. Unchanged. The
   compander (NE570/571, 2:1) is modelled as a static in-loop soft-clip — a
   deliberate simplification (a real compander is a level-dependent gain, not a
   waveshaper); it's near-transparent at normal level and only bends on overload,
   which matches the factory "no distortion until the overload LED" behaviour.

---

## ADDRESSED 2026-07-04 (UNCOMMITTED, needs Windows build + Robbie's ear)

All P1 rot + the ranked P2 Memory Man items below were implemented this pass;
`predelay_test` is **44 CHECKs, 0 fail** offline (T1–T16). Done:

- **P1 rot:** deleted the "13-bit … (model 3)" SDD leftover comment; fixed the wrong
  ceiling numbers in the three test comments + the DD-7 header + `dd7.md`; updated the
  `applyIo` header + the Panels "only DD-7 verified / generic" comment; rewrote the
  read-first `NEXT_CHARACTER_HANDOFF.md` status/inventory (kept its method + gotchas);
  updated `memory_man.md` + `carbon_copy.md`.
- **P2 Memory Man revoice** (new per-model `bwQ` + `modDepthFrac` fields; DD-7 + Carbon
  Copy bandwidth byte-exact via bwQ 0.5): 650 Hz mid → dropped; resonant reconstruction LP
  antiAlias 3200 / bwQ 1.30 (measured +3.0 dB @ 2.53 kHz, flat ≤900 Hz — factory match);
  antiAlias 3800→3200; chorus rate 1.0→0.85 Hz; modulation fixed-ms → delay-proportional
  0.10 (verified ±10 %). Carbon Copy modulation also made delay-proportional (0.006, flagged).
- **New tests:** T4 rewritten to the accurate bandwidth hierarchy (the old "MM darkens with
  time" tested a near-nonexistent effect for an 8192-stage BBD); T16 pins the revoice
  (presence peak @2.5 kHz > 500 Hz, voicing fields, delay-proportional mod ratio ~5×, bounded
  self-osc).

Suggested scoped commit:
`predelay: fix SDD-removal rot + revoice Memory Man from the EH-7850 factory calibration`

## Priorities

**P1 — correctness & anti-footgun (zero sonic risk):** delete the "13-bit …
(model 3)" SDD leftover comment (PreDelayBlock.h:348); fix the wrong ceiling
numbers in the three test comments + the DD-7 header/`dd7.md` "1.0"; update the
`applyIo` header and the Panels "only DD-7 verified / generic" comment; **rewrite
`NEXT_CHARACTER_HANDOFF.md`'s status/inventory** (keep its method + gotchas).

**P2 — sound, ranked by audibility:** Memory Man 650 Hz mid → 2.5 kHz resonant
reconstruction (wrong by ~2 octaves — top item) → Memory Man delay-proportional
modulation (restores the lush long-delay wobble, factory-verified 10 %) →
Memory Man antiAlias 3800→3200 → Memory Man chorus rate 1.0→0.85. Carbon Copy
modulation → delay-proportional (subtle, magnitude flagged).

**P3 — doc the deliberate/unverifiable deviations** so they survive fresh
sessions: Carbon Copy antiAlias 2600 (2-pole stand-in for a steep ~3 kHz filter,
grounded against the DMM factory reference, exact corner still needs the probe);
`fbCeiling`s 1.18/1.05/1.06 are behaviour-tuned not circuit-derived; the compander
static-soft-clip simplification.

All P2 items go through the loop: offline `predelay_test` additions first, then
Robbie's Windows build + ear test. **This review's code changes are UNCOMMITTED**
and gated on his ears (especially the Memory Man revoice, which is a real change in
character — for the better, per the factory data — not a subtle tweak).
