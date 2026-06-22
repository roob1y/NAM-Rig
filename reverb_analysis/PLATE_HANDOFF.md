# Plate reverb — full handoff for a fresh chat (2026-06-21)

> ## ⭐ LATEST STATE (resume here) — updated end of 2026-06-21 marathon session
> Big multi-part advance beyond the original Residual A/B below. **Read
> `reverb_analysis/plate_locked_geometry.txt` top-to-bottom first** — it is the full chronological
> worklog of everything since. Quick map:
> - **Residual B (EDT plateau): SOLVED** via a modal-pocket geometry rebuild (uneven line lengths) +
>   voicing re-fit, validated across all 10 knobs.
> - **The top-end / "shimmer" gap (was Residual A): MECHANISM SOLVED, shaping left.** Proven via a
>   two-slope Schroeder test that it's a *missing long-lived HF energy reservoir* (the ref HF decay is
>   two-slope, late slope 3.3-3.8× shallower; ours was single-exponential), NOT a coupling/diffusion
>   problem. Built the **synthesis**: partial FDN coupling (α=0.25, for *texture* — modal 6.70→7.06) +
>   a **field-fed HF reservoir** (for the *sustain* — bends the late slope −28→−11). These are TWO
>   separable mechanisms for two separate problems.
> - **TASK #5 (multi-band reservoir R1/R2/R3): BUILT + TUNED 2026-06-21 pt2. WIN on the low end; the top
>   shelf re-hit the known wall.** Built `struct HfBand` = 3 parallel field-fed reservoirs, env-gated by
>   `HFM=1` (HFM off byte-reproduces SYNTHESIS). Each: 4th-order Butterworth INPUT high-pass (confines the
>   band) + a per-line Butterworth LOOP high-pass at a LOW corner (the key fix — per-pass it spares the top
>   ~1.0 but discriminates lows, compounding over the tail to kill leak ~50dB; input/output filtering can't,
>   because the loop gain is +54dB at resonance). **WIN:** kills the BACKWARDS low platform — 3.5k late-slope
>   −9.2 → **−20.5 (ref −20.7, matched)**, anchor preserved/better (EDT plateau intact, T30err 0.077→0.067,
>   cent 4648, modal 6.9, stable). **WALL (re-confirmed):** still can't sustain the rising 8-12k shimmer at low
>   level — R3 is feed-starved (dark field) so 8k always out-rings 11k (gradient HUMPS at 5-8k, doesn't rise to
>   the top); forcing 11k to −9.6 needs a loud bright wash (cent 6917/T30 0.72). Anchor-safe optimum = flat
>   −19..−21 across 8-12k. SAME wall as STEP 4c/coupling/two-slope → the **velvet-noise** late-field remains the
>   proven mechanism for the 8-12k shelf. Full worklog + the env-truncation gotchas in `plate_locked_geometry.txt`
>   (TASK #5 section). New tools: `lateslope.py`, `opt_multiband.py`. Snapshot: `plate_proto_ReverbBlock_MULTIBAND.h`.
> - **VELVET HF FIELD: BUILT real-time in the proto 2026-06-21 pt2 (struct VelvetBand, env VLV, stacks on HFM).**
>   The residual tests first PROVED the 6-12k tail is real at the anchor (+17-23dB over floor) and FULLY DIFFUSE
>   (envelope CV 0.523 = Rayleigh, no beating) -> velvet is the physically-correct (max-entropy) model, not a hack;
>   a high-Q localized bank would be observationally identical. Real-time block: 3 sub-band PURE-GAIN FDNs (decay
>   set exactly by t60, no top-damping), fed from the BRIGHT pre-driver input (no feed-starvation), 4th-order
>   Butterworth-confined, decorrelated, low level. RESULT @anchor: gradient now RISES & matches ref within ~1.6dB
>   (8k -15.6/-14.0, 11k -12.0/-12.7), tail is Rayleigh (CV 0.522-0.528), cent +115, stable; VLV-off == multi-band.
>   Velvet t60 SCALES with the knob + a shared HF-tail taper (v_dt) shrinks the added tail at short decay (the
>   cross-knob check exposed that the WHOLE HF-tail family was anchor-only); cent now tracks ref at every knob 0.5-4.5.
>   At long knobs the ref top is NOISE-FLOOR-limited -> ours correctly stays a real shimmer (doesn't chase the floor).
>   Snapshot plate_proto_ReverbBlock_HYBRID.h; figs edr_hybrid.png, residual_test.png. Tools velvet_proof.py,
>   test_residual.py, lateslope.py, opt_multiband.py, edr_compare.py, edr_hybrid.py.
> - **NEXT (follow-up pass):** (a) optional few-ms velvet predelay to remove a ~0.1s low-mid EDT-onset shift + soften
>   the T30 knee (0.067->0.100). (b) LIVE PORT: VelvetBand + VLV into the CRLF src/rig/ReverbBlock.h at BYTE level,
>   keep reverb_test green, re-verify anchor. (c) EAR A/B: bare vs +multi-band (HFM) vs +velvet (VLV) vs ref-convolution,
>   with HFM/VLV/HFMLV/VLVLV/VDT as ears knobs. (d) minor: knobs 0.7-1.0 ~5dB long at 5k -> sharpen the taper.
>
> **How to resume (proto clears between sessions):** 1) Robbie re-drops the 10 ref IRs into
> `reverb_analysis/ir/` (§1a). 2) Recreate the proto from the CURRENT-BEST snapshot:
> `mkdir -p plate_proto/inc/rig plate_proto/stub/juce_audio_basics`;
> `cp reverb_analysis/plate_proto_ReverbBlock_SYNTHESIS.h plate_proto/inc/rig/ReverbBlock.h`;
> `cp src/rig/{Blocks,Lfo,Biquad}.h plate_proto/inc/rig/`;
> `echo '#pragma once' > plate_proto/stub/juce_audio_basics/juce_audio_basics.h`; build render_proto per
> the PLATE_MODAL_POCKET_PROTO.md recipe. (Verified: this snapshot rebuilds & reproduces the synthesis
> byte-exact.) 3 snapshots: `_SNAPSHOT.h` (α=1 banked-safe), `_ALPHA025_REFIT.h` (texture),
> `_SYNTHESIS.h` (texture+field-fed reservoir); `_MULTIBAND.h` (SYNTHESIS + the 3-band reservoir, HFM-gated =
> newest). Env knobs: COUPLING, HFT_LV/FB/HP/LP/FIELD/RCOUP/FBLO/FBHI, BASSMONO, ER1/2/3, and (multi-band)
> HFM + R{1,2,3}{HP,FB,LV,MS,LHP,SH} + HFMLP/HFMPRE/HFMLV. BUILD RULE: `g++ ...; echo $?` (no pipe) and verify
> exit 0 + `wc -l`/tail before measuring — the mount truncates header writes (cost real time this session).
> **The shipped `src/rig/ReverbBlock.h` is UNTOUCHED all session** (byte-matches `outputs/plate_voicing.diff`).
> Nothing committed — the whole top-end exploration lives only in the proto + snapshots. Validation across
> 10 knobs + **Robbie's ear A/B** (metallic check at α=0.25; reservoir naturalness) + the live-header port
> all remain before this ships.

---

Everything a new session needs to continue the Plate-vs-vintage-plate work **without
hiccups**. Read this top to bottom before touching anything. Companion detail lives in
`RESULTS_plate.md` (chronological worklog), `METRICS.md`, `IR_MATCHING_PLAYBOOK.md`.

---

## 0. TL;DR — current state
The Plate (`PlateFdn` in `src/rig/ReverbBlock.h`) is **matched to the user's vintage-plate
reference across the full Decay knob** on every battery metric, and the damping was rebuilt
to make it versatile at long settings. It is **NOT committed** — the sandbox can't commit;
Robbie reviews `outputs/plate_voicing.diff` + commits himself. Original pristine header backed
up at `outputs/ReverbBlock.h.orig`; pre-damping-rebuild state at `outputs/ReverbBlock.h.preDampRebuild`.

What's done: all-rounder voicing (T30, C80, tonal, side/mid, EDT-low) + decay-scaling (bloom,
tone) + multiband damping rebuild (long-decay low gradient). Two known residuals remain (see §7).

---

## 1. The goal & the reference
- Match our algorithmic Plate to the user's reference plate IR **by the metric battery**
  (graphs decide the match; ears decide if it's good). Target = a faithful, versatile plate
  across the whole Decay range.
- Reference = a real hardware plate captured at **10 decay-knob settings**: files in
  `reverb_analysis/ir/vintage-plate-{0.5,0.7,1.0,1.5,2.0,2.5,3.0,3.5,4.0,4.5}s.wav` (stereo).
- **BRAND RULE (hard):** never write the real brand/product name anywhere — files, code,
  graphs, docs. Call it the **"vintage plate"** reference. The original uploads carried a brand
  name; they were staged under neutral names. Keep it that way.
- The filename number is the unit's **knob**, NOT seconds. Measured T60@1k per knob (set
  `RV_T60` to this so the 1 kHz decay lines up):
  `0.5->0.81, 0.7->1.34, 1.0->1.87, 1.5->2.45, 2.0->2.93, 2.5->3.16, 3.0->3.42, 3.5->3.59, 4.0->3.78, 4.5->3.90`
- **The versatile target / anchor = the 1.5 knob (2.45 s @1k)** — the most usable guitar plate.
  Everything is anchored here; preserving it is sacred (see §5).

## 1a. The reference IRs are PER-SESSION
`ir/` is gitignored. A fresh sandbox starts WITHOUT them — Robbie must re-drop the 10 wavs into
`reverb_analysis/ir/` (or re-upload) at the start of the new chat. Without them you can't measure.

---

## 2. Environment gotchas (these cost real time — know them upfront)
1. **Sandbox = WSL2.** If the Linux shell won't start ("VM service failed"), WSL isn't installed/
   running on the user's Windows box. Fix: `wsl --install` (needs reboot), then fully quit+reopen
   the Claude desktop app. Services to check in `services.msc`: `vmcompute` (Hyper-V Host Compute),
   `LxssManager`/`WSLService`. This is the user's machine — guide them, don't try to fix from here.
2. **PyPI is BLOCKED in the sandbox** (proxy 403). NO `pip install`. **numpy + matplotlib only.**
   The whole toolkit is numpy-only by necessity. `validate_battery.py` is a pure-numpy ISO-3382/
   Lundeby cross-check of the battery (already proven the battery's core numbers are trustworthy;
   band edges 62Hz/11kHz are noisy in the reference itself — don't over-chase them).
3. **CRLF — THE #1 gotcha.** `src/rig/ReverbBlock.h` is **CRLF**. Editing it with python TEXT mode
   (or the Edit tool's text path) flattens CRLF->LF and produces a WHOLE-FILE spurious diff that
   looks catastrophic. **Always edit at BYTE level:** `d=open(p,'rb').read(); d=d.replace(b'...\r\n...', b'...')；open(p,'wb').write(d)`.
   After every edit verify `grep -c $'\r' file` == `wc -l file`. (The Edit tool also fails after a
   bash `cp` restore with "file not read" — re-Read or use byte-level python.)
4. **Sandbox CANNOT git commit** (stale-mount). Make the change, save the diff, hand it to Robbie.
5. **The mount can truncate a file mid-write** occasionally (lost the tail of the header once).
   After big edits, check `wc -l` and `tail` the file. Keep the backups.
6. **Long render loops can time out** (45s bash cap). Render in small batches.

---

## 3. The workflow / tooling (all in `reverb_analysis/`)
- **Renderer:** `render_character.cpp` compiles the LIVE `ReverbBlock` (real engine, no drift) via
  a tiny JUCE stub. Build once:
  `mkdir -p stub/juce_audio_basics && echo '#pragma once' > stub/juce_audio_basics/juce_audio_basics.h`
  `g++ -std=c++17 -O2 -I../src -Istub render_character.cpp -o render_character`
  Run: `RV_T60=<sec> ./render_character plate impulse.f32 out.f32` (100% wet IR; impulse.f32 =
  10s stereo, impulse @ sample 10 — regen: `python3 -c "import numpy as np;N=480000;x=np.zeros((N,2),'<f4');x[10]=1;x.tofile('impulse.f32')"`).
  Env knobs: `RV_T60` (decay s), `RV_DAMP` (tone Hz, default 6000), `RV_SIZE`, `RV_PRE`.
- **Battery:** `python3 reverb_battery.py --ours out.f32 --ref ir/vintage-plate-1.5s.wav --label X --out <dir>`
  prints the metrics + saves a 6-panel PNG (T30, EDT, tonal, C80, side/mid, echo density) and an
  EDR PNG. Helpers: `import reverb_battery as rb; rb.per_band_decay(mono,8.0,-5,-35)` -> T60 per
  `rb.FC` band; `rb.read_wav(path)`; `rb.centroid`, `rb.c80`, `rb.modal_depth`, `rb.onset`.
  Our render: `np.fromfile(p,dtype='<f4').reshape(-1,2)`; mono=(L+R)/2.
- **Demos:** `make_demos_template.py` builds loudness-matched A/B (reference-convolved vs our
  render). DI = a **synthesized Karplus-Strong guitar** (`dry_guitar.f32`), NOT a real DI file.
- **The metrics that decide it** (see METRICS.md): T30(f) decay is THE defining trait; **modal
  depth** is THE metallic-vs-lush discriminator (NOT one of the 6 graph panels — draw it
  separately when showing graphs; >6 = lush, ~5 = metallic; ours 6.7-6.8 = lush). Echo-density
  GRAPH always looks jagged = per-frame estimator noise (scalar matches 1.01).

---

## 4. Engine architecture (`PlateFdn`) — what each lever does
32-line FWHT FDN, length-scaled multiband absorptive damping, renders WET only. Signal path:
predelay -> input low-mid EQ (`mLmEq`) -> 2-pole driver (`mDrvK`, Tone) -> 6 diffuser allpasses
-> FDN lines (read) -> output sum + EARLY TAP -> presence bell -> 2k dip -> air trim -> low-cut
-> side widener/narrower -> out. Damping is in the FEEDBACK loop.

**Two independent halves — keep them mentally separate:**
- **EARLY-PATH + OUTPUT voicing** (sets C80, side/mid, tonal colour, attack): `mEarly`,
  `mEarlyLowGain`, `mEarlyLpK` (freq split ~420Hz), `mHighCorr`, `mHfEarly/mHfLp` (HF early
  emphasis), `mEReso1/2/3` (early-path C80 resonance shaper: 250 boost / 500 dip / 125 cut),
  `mSideLpA/B`+`mMidWidth` (side widener 460-1150), `mSide2Hp/Lp`+`mNarrow2k` (biquad 2k side
  narrower), `mDip2kL/R` (2k tonal notch), `mAirL/R` (>10kHz air trim, centroid match), `mPresL/R`
  (presence bell 3300/0.5/+4.2), `mLmEq`, `mLcK` (low-cut 88Hz). **These are the banked match and
  are fragile/over-fit to THIS capture. Do NOT touch them for decay/damping work.**
- **DAMPING** (sets T60(f) decay across freq): broadband `bbEff` + high-shelves `mHs1`(355Hz,
  `g1Eff`), `mHs2`(3600Hz, `g2Eff`), plus the NEW `mLo1` (200Hz, long-decay only). Decay-dependent.
  **This is the half you change for decay-range work.**

---

## 5. The anchor-preservation principle (do not break this)
At `RV_T60=2.45` the engine MUST reproduce the committed all-rounder: **T30 mean 0.05s, spec err
0.77dB, modal 6.8, centroid ~4540, side/mid <0.15dB, L/R corr ~0**. Verify after ANY change:
`RV_T60=2.45 ./render_character plate impulse.f32 a.f32 && python3 reverb_battery.py --ours a.f32 --ref ir/vintage-plate-1.5s.wav --label a --out /tmp`.
All decay-dependent terms are written so their EXTRA contribution is **exactly 0 at T60=2.45**
(e.g. `dd = max(0, T60-2.45)`; Rspl cubic passes exactly through the anchor). That's why the early/
output voicing (which depends on the tail) stays intact across all the decay-scaling work.

---

## 6. The decay-scaling + damping rebuild (the current model, in updateGeometry)
Reference behaviour to reproduce: as the knob goes up, the **low-mid bloom deepens** (125/1k decay
ratio 1.03 short -> 2.6 long) and the **tone darkens** (centroid 6242 -> 3405). Our old engine was
FLAT (ratio ~1.4, centroid ~4800). The model:
- `kD1k = kDampBb + kDampG1*kA1k` (kA1k=0.70) = the 1 kHz damping, **held constant so the knob reads
  true seconds**. `bbEff = kD1k/Rspl`; `g1Eff=(kD1k-bbEff)/kA1k` (keeps 1k pinned).
- `Rspl(T60)` cubic — EXACT through the anchor (2.45->1.362) — scales the broadband/shelf split so
  bloom deepens with decay. Coeffs: `(0.12412, -0.635407, 1.20743, 0.392912)`, clamp [1.0,3.05].
- Tone: `g2Eff = kDampG2 * (T60/2.45)^1.7` (top darkens with decay).
- **Centroid air offset fix:** a global output high-shelf `mAirL/R` (10kHz, -1.7dB) drops the whole
  centroid curve onto the reference (the anchor itself was ~+250Hz bright >8kHz). Anchor-safe.
- **NEW multiband damping (the rebuild, the agent's work, ~68-line diff on top):** two all-negative
  (loss-only, feedback stays <1) decay-activated terms, zero at the anchor:
  (1) `rB` reduces broadband loss for the lows at long settings so they ring 10-13s:
      `bbEff = kD1k/(Rspl*(1+rB))`, `rB = 0.213*dd^2 + 0.819*max(0,dd-1)^2`, `dd=max(0,T60-2.45)`.
      g1Eff recomputed from new bbEff so 1k stays pinned.
  (2) `mLo1` = per-line absorptive high-shelf @200Hz, gain `-(-8.13e-6*dd + 4.149e-5*dd^2)` dB,
      length-scaled, steepens the 125->710Hz slope so mids don't over-bloom when rB lifts the lows.
  Fit render-in-the-loop at knobs 3.0/3.5/4.0/4.5, smoothed vs T60.

### Why this was hard (so the new chat doesn't repeat the dead ends)
The reference's long-setting low end is 6 adjacent bands at 6 very different decays (62Hz~12s,
125~11s, 180~9s ... 1k pinned 3.8s) all sharing ONE feedback path. **Single broadband + one low
shelf CANNOT make that gradient.** Dead ends already tried & failed: pushing the bloom cubic alone
(over-rings the 62 sub + drifts 1k); a low shelf (reaches 1k, kills the pin); a peaking absorption
band (skirts hit 125 and 1k). The ONLY thing that worked: **add genuine low-band DOF (the rB
broadband-split + the mLo1 200Hz shelf) and FIT them render-in-the-loop**, keeping every added term
loss-only (stability) and zero-at-anchor (preserve the match). Do NOT hand-tune one band at a time.

---

## 7. Current results & the TWO remaining residuals
T30 mean error across the knob (per_band_decay win=8.0):
- 0.5-3.0 knob: **0.05-0.20s** (every usable plate decay — dialed, anchor 0.05).
- 3.5/4.0/4.5: all-band 0.44/0.58/0.55, **but 180Hz-8k (audible) only 0.22/0.23/0.20**.
- Bloom tracks the reference 0.5-3.0; tone (centroid) within ~±3.5% across the whole knob.

**Residual A — extreme-long 11kHz air band. INVESTIGATED 2026-06-21 → FOLDED INTO B.** At 3.5-4.5
the reference's 11k ring jumps to 6-7s (ours flat ~1.65); it's a real sustained ring (-2.7dB vs 1k
at 4.5), not noise. The handoff's `mAirL/R` idea CANNOT work: an output shelf changes a band's LEVEL,
not its DECAY. Tried the correct (damping) path instead: a per-line decay-activated POSITIVE high-
shelf `mHs3` (zero at anchor, loss-bounded), fit render-in-loop, corners 9-16k. STRUCTURAL FAIL on
two walls: (1) any broadband top lift wrecks the matched centroid (A=2e-4 → centroid 3349→3946 for
only +0.06s at 11k); (2) pushing harder destabilizes (A≥1e-3 → 1k balloons to 15s, runaway via FWHT
cross-mixing). ROOT CAUSE = identical to Residual B: the ref's 6.6s 11k-ring-under-a-dark-centroid is
a SPARSE MODAL trait, unreachable by a broadband shared-feedback FDN + EQ damping. mHs3 fully
REVERTED (engine byte-matches the saved diff). The only fix is the B rebuild. A stays LOW priority
(barely audible 11k at a 4.5 wash). Full worklog in RESULTS_plate.md.
**Residual B — flat low-mid EDT plateau (250-710Hz). PROTOTYPE BREAKTHROUGH 2026-06-21.** Make the
FDN's own modes uneven (detune the 32 line lengths + lighten diffusion). Built an ISOLATED proto
(forked headers in `plate_proto/`, baseline byte-identical to live — never prototype on the shipped
header). exp2 (lighter dispersion kDispG 0.62→0.45 / kDispG2 0.55→0.40 + irregular ±6% kLineMs jitter
= modal pockets) **CLOSED the plateau** at the anchor: EDT 250-710 went flat-2.40 → 2.42/2.54/2.52/
2.44 ≈ ref 2.48/2.59/2.52/2.47, while T30err 0.058 / modal 6.8 / centroid 4495 all held, and with
NONE of the parallel-resonator's failure modes (no t=0 spike, no tonal bumps). COST (the "re-opens the
match" warning, now concrete): detuning moved the OVER-FIT voicing — C80-low −0.77→−2.11, side/mid
d 0.15→0.7dB — all re-fittable. Residual A (11k long ring) is NOT incidentally fixed by this jitter
and needs targeted short-line detune / a dedicated high-mode line. FULL recipe, winning params, and
the remaining multi-session rebuild plan (optimize jitter → re-fit voicing → validate all 10 knobs →
ear A/B → port to live CRLF) are in **`reverb_analysis/PLATE_MODAL_POCKET_PROTO.md`**. `plate_proto/`
is gitignored (clears) — that doc + `reverb_analysis/measure_proto.py` are the tracked continuation.

---

## 8. What the new chat should do (suggested order)
1. Get the sandbox up (WSL, §2), have Robbie re-drop the 10 reference IRs into `ir/` (§1a).
2. Rebuild `render_character` (§3), regen `impulse.f32`.
3. **Verify the current state reproduces this handoff** before changing anything: anchor 2.45
   (T30 0.05/spec 0.77), and the across-knob T30 table (§7). If not, restore from
   `outputs/ReverbBlock.h.preDampRebuild` (pre-rebuild) or `.orig` (pristine) and re-check.
4. Then pick up the open items: finalize/А-B the damping rebuild by ear, optionally tackle
   Residual A (11k air at long), and/or schedule Residual B (FDN modal-pocket rebuild).
5. Always: byte-level CRLF edits, verify CRLF count, never touch the early/output voicing for
   decay work, re-check the anchor after every change, save the diff for Robbie to commit.

## 9. Key files
- Engine: `src/rig/ReverbBlock.h` (class `PlateFdn`). CRLF. UNCOMMITTED.
- Diff for Robbie to commit: `outputs/plate_voicing.diff`.
- Backups: `outputs/ReverbBlock.h.orig` (pristine), `outputs/ReverbBlock.h.preDampRebuild` (pre-rebuild).
- Toolkit: `reverb_analysis/` — `render_character.cpp`, `reverb_battery.py`, `validate_battery.py`,
  `make_demos_template.py`, `wavutil.py`, `METRICS.md`, `IR_MATCHING_PLAYBOOK.md`, `RESULTS_plate.md`
  (full chronological worklog of every pass — read it for the blow-by-blow).
