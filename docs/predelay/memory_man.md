# EHX Deluxe Memory Man — researched circuit (for PreDelayBlock kMemoryMan voicing)

Deep-research pass 2026-07-03 (method per `docs/predelay/NEXT_CHARACTER_HANDOFF.md`). Target = the
**classic vintage EH7850 5-knob Deluxe Memory Man (MN3005 BBD)** and its XO reissue; the modern
Nano DMM is noted where its official spec sheet fills a gap. Like the Carbon Copy, EHX never
published a schematic, and the circulated traced schematics (freestompboxes t=1208,
experimentalistsanonymous EH7850 PDF, David Morrin's per-board pages) are image-only / bot-blocked,
so component-level R/C values could not be OCR'd. Load-bearing sources: **David Morrin's DMM circuit
archive** (BBD, CD4047, NE compander, and a swept-sine scope trace of the delay path), the
**official EHX manuals** (vintage + Nano), the **MN3005/MN3008 datasheets**, ElectroSmash's BBD
reference, and hands-on mod/repair writeups (diystompboxes mmmod, Analogman XO mod). Anything only
on the schematic images is **flagged "could NOT verify."**

Everything below is cited; (inference) = derived from stage-count/delay math or topology,
(unverified) = not confirmed by a reachable source.

## Signal path (stage by stage)
guitar → **~100 kΩ INVERTING input stage** (JRC4558D, loads the source — the "dark dry" gotcha) →
compressor (½ of an **NE570/571**) → anti-alias LP (op-amp active filter) → **BBD: 2× MN3005
(4096 stages each) in SERIES = 8192 stages**, clocked by a **CD4047** whose timing cap is pulled by
a varicap diode fed from the LFO (this is the pitch modulation) → reconstruction LP (cascaded
active, ~24 dB/oct) → expander (other ½ of the NE570/571) → **BLEND crossfade of clean analog dry
vs wet** → LEVEL gain block → **~300 Ω buffered output** (Nano spec). Modulation = an op-amp
**triangle LFO** (Depth = sweep width, Rate/Chorus-Vibrato = speed). No tone control.

## Facts that drive the DSP voicing (cited)
- **BBD = 2× Panasonic MN3005 (4096 stages each) in series = 8192 total stages; max delay ≈ 550 ms
  (min ≈ 30 ms).** A single MN3005 only reaches ~205 ms, so the series pair is what gets to ~550 ms.
  Confirmed by a period EH7850 board listing ("MN3005 × 2 BBD's on board") + the MN3005 datasheet +
  Morrin. Modern reissue uses **4× MN3008 (2048 each) = 8192** — same total. → David Morrin DMM
  archive https://sites.google.com/site/davidmorrinoldsite/home/trouble/troubleeffects/electro-harmonix-memory-man
  ; Reverb EH7850 listing https://reverb.com/item/43838958-1979-electro-harmonix-deluxe-memory-man-eh7850-mn3005-x-2-bbd-s-on-board-w-original-box
  ; MN3005 datasheet https://xvive.com/audio/wp-content/uploads/sites/2/2021/11/Xvive_MN3005_BBD_DATASHEET.pdf
  ; EHX 550 ms https://www.ehx.com/products/deluxe-memory-man/
  - **This corrects the old placeholder (which assumed ONE 4096-stage chip → ~1.6 kHz @ 550 ms).**
    With 8192 stages, Nyquist = stages/(4·t) ≈ **3.7 kHz at 550 ms** — much brighter than a single
    chip, so the DMM's darkness is dominated by the FIXED reconstruction filter, not the clock; the
    clock-tracked darkening with time is real but SUBTLE (inference from the corrected stage count).
- **Clock = CD4047** (CMOS astable, ÷2 → two-phase BBD clocks), NOT an MN3101; the DELAY pot sets
  the RC, and the LFO injects into it via a varicap diode. → Morrin CD4047 page. Exact fclk range
  NOT published (derived only).
- **Compander = NE570/571** (2:1 noise reduction; compress-before / expand-after the BBD). Some
  revs use 571 vs 570. → Morrin NE-compander page; Premier Guitar "Behind the Bucket Brigade"
  https://www.premierguitar.com/articles/25035-behind-the-bucket-brigade
- **Voice = a measured BAND-PASS: heavily attenuated highs, LITTLE bass cut, a STRONG MID BOOST.**
  This is from David Morrin sweeping a sine (55 Hz–5.5 kHz) through the real delay path on a scope
  — his verbatim finding. The reconstruction filtering is **cascaded multipole active (Sallen-Key
  style), ~24 dB/oct** (4-pole; from an EH7850 clone builder). → Morrin archive; atomiumamps DMM
  clone https://atomiumamps.tumblr.com/post/185732560726/ ; ElectroSmash BBD reference
  https://www.electrosmash.com/mn3007-bucket-brigade-devices
- **NO tone control.** Controls = LEVEL, BLEND, FEEDBACK, DELAY, DEPTH + a CHORUS/VIBRATO switch.
  So the dark/mid-forward voice is FIXED — the player can't brighten the repeats. → official EHX
  vintage manual https://www.ehx.com/wp-content/uploads/2020/11/deluxe-memory-man.pdf
- **BLEND is a TRUE CROSSFADE: 100% dry (CCW) → equal (centre) → 100% wet (CW).** This is how the
  DMM does chorus vs vibrato: **BLEND max (100% wet) = you hear only the pitch-modulated delay =
  vibrato; BLEND mid (wet + dry) = the modulated wet beats against dry = chorus.** → official EHX
  Nano manual https://www.ehx.com/wp-content/uploads/2021/09/nano-deluxe-memory-man-manual.pdf
- **Modulation = a TRIANGLE op-amp LFO modulating the BBD clock (Doppler pitch warble on the
  repeats), DEEP/lush** — genuinely wider than the Carbon Copy's subtle warble. DEPTH = how far the
  delay time is allowed to deviate; fully CCW = mod off. Named rates: **Chorus ≈ 1 Hz, Vibrato ≈
  4 Hz** (the vintage Chorus/Vibrato switch swaps a cap to change the LFO speed range; it does NOT
  change waveform). → Morrin EH7850 page; diystompboxes LFO thread
  https://www.diystompboxes.com/smfforum/index.php?topic=120825.0 ; Nano manual
- **Input impedance ≈ 100 kΩ, INVERTING stage — it LOADS the source** ("dark dry tone" gotcha; the
  Analogman XO mod adds a trimpot to raise it). This is the vintage/XO trait; the modern **Nano is
  1 MΩ** (redesigned "to preserve your guitar's tone"). → diystompboxes mmmod
  https://www.diystompboxes.com/pedals/mmmod.html ; Analogman XO mod
  https://www.analogman.com/manuals/dmmxotweeks.doc ; Nano manual (1 MΩ in / 300 Ω out)
- **Self-oscillates** at high Feedback ("runaway oscillation will occur"); an internal 100 kΩ F.B.
  trim caps the knob. Each repeat re-passes the wet-line filtering → repeats darken cumulatively.
  → EHX vintage manual
- Op-amps = **JRC4558D** in the audio path (input + elsewhere). → mmmod

## Could NOT verify (do NOT build hard numbers on these — schematic images are bot-blocked)
- **Exact reconstruction/anti-alias −3 dB corner(s)** of the DMM (no published kHz; the real filter
  is ~24 dB/oct multipole, ours is a single 2-pole approximation) → voicing uses ~3.8 kHz, FLAGGED.
- **Center frequency + dB height of the mid bump** — the scope trace shows it but doesn't quantify
  it → voicing uses ~650 Hz / +4 dB, FLAGGED (ear-tunable).
- **The low-cut corner** ("little bass cut" is qualitative only) → voicing uses a gentle 80 Hz HP.
- **Exact CD4047 clock fclk range**, input/output coupling-cap values, and the vintage input-Z
  precise figure (~100 kΩ is reported secondhand from the freestompboxes analysis) and vintage
  output-Z (only the Nano's 300 Ω is official).
- **LFO Rate continuous endpoints in Hz** (only the ~1 Hz chorus / ~4 Hz vibrato points are cited)
  and the **modulation depth in ms/cents** (nowhere specced; "deep/lush" is qualitative).

## → kMemoryMan voicing decisions
- **bbd = true; maxTimeMs = 550; bbdStages = 8192** — corrected to the real 2× MN3005 series pair.
- **antiAliasHz ≈ 3800 Hz (FLAGGED).** The fixed reconstruction filter dominates the darkness; set
  just above the max-delay clock-Nyquist (~3.7 kHz) so a subtle authentic time-darkening remains.
  2-pole approximation of the real ~24 dB/oct multipole → ear-tunable / controlled-probe "measure
  it" item. Brighter than the Carbon Copy's 2600 (the DMM is more present/hi-fi, mid-forward).
- **midHz 650 / midDb +4 / midQ 0.80 (magnitude FLAGGED)** — the documented "strong mid boost";
  with the 80 Hz low-cut and the ~3.8 kHz LP this composites into the measured band-pass.
- **loopHpHz 80** — the gentle "little bass cut" (lows largely preserved).
- **satDrive 0.45 / satAsym 0.06** — the NE570/571 compander knee + a touch of BBD warmth (subtle).
- **modRateHz 1.6, modDepthMs 3.0, TRIANGLE LFO** — deep/lush modulation (deeper than the Carbon
  Copy's 1.3 ms), triangle waveform per the real LFO; Depth knob (user Mod) scales it. Chorus vs
  vibrato emerges from the Blend position (like the Nano), so no separate switch is modeled.
- **glideMs 80** — analog BBD repitch swoop on a Delay-knob change.
- **fbCeiling 1.06** — self-oscillates readily (the brighter LP retains loop gain), a DMM feature;
  bounded by the in-loop compander sat + loopLimit. (Offline: sustained + bounded at ~1.6 tail.)
- **Mix law = TRUE CROSSFADE** (not the DD-7/Carbon Copy dry+wet): `(1−mix)·dry + mix·wet`, so
  full-wet = vibrato (dry removed), mid = chorus. The feedback write (delay input = dry + fb·wet)
  is unchanged — only the output blend differs.
- **IoStage = LOADED (gentle).** ~100 kΩ inverting input → a gentle high-shelf cut (−1.5 dB from
  ~3 kHz, −0.5 dB level) representing the loading (mostly relevant with a high-Z guitar; subtle
  here since the predelay sits after the buffered drive). ~300 Ω buffered output; subsonic couplings.
- **Panel controls = Delay / Feedback / Blend / Depth, no Tone.** The DMM's real knobs (LEVEL is the
  global output). Chorus/Vibrato is set by the Blend position, not a separate control.
