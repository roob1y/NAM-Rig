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

## RESOLVED 2026-07-04 — the FACTORY calibration quantifies the delay-path response
The earlier "could not verify the corner / the mid" flags are **CLOSED** by the official
EH-7850 calibration procedure (Howard Davis/EHX, 8/1/1978, archived by David Morrin), which
specifies the delay-path frequency response at the real test points:
- FREQ. RESPONSE CHECK #1 (at MN3005 pin 7): *"flat up to about 900 Hz, rise to a max of about
  2 V p-p at around 2.5 kHz … drop back to 1.5 V p-p at about 3.8 kHz and roll off sharply above
  this."* (baseline 1.5 V → the peak is ≈ +2.5 dB)
- FREQ. RESPONSE CHECK #2 (after the NE570 expander): max delay = *"flat … and −3 dB at about
  3.2 kHz"*; min delay = *"a peak of about +3 dB (×1.4) around 2.5 kHz and roll off sharply above
  3.5 kHz."*
  → <https://sites.google.com/site/davidmorrinoldsite/home/trouble/troubleeffects/electro-harmonix-memory-man/eh-7850-calibration>

So the real voice is **flat below ~900 Hz, a RESONANT presence peak of ~+3 dB at ~2.5 kHz, and a
−3 dB corner at ~3.2–3.5 kHz.** There is NO boost near 650 Hz — the earlier "strong mid boost"
read off Morrin's swept-sine was this **2.5 kHz filter resonance**, mislocated ~2 octaves low.
Also verified here: modulation = *"~10 % of the period"* swing at max Chorus; Chorus rate
*"slightly less than 1 Hz"*, Vibrato *"approx 4 Hz"*; runaway self-oscillation at max Feedback.

## Could NOT verify (schematic images bot-blocked)
- Exact **CD4047 clock fclk range**, input/output coupling-cap values, and the vintage input-Z
  precise figure (~100 kΩ secondhand from the freestompboxes analysis) and vintage output-Z
  (only the Nano's 300 Ω is official). The low-cut corner ("little bass cut" is qualitative) →
  voicing uses a gentle 80 Hz HP.
- **LFO Rate continuous endpoints in Hz** (only the ~0.85 Hz chorus / ~4 Hz vibrato points are
  cited — now factory-confirmed).

## → kMemoryMan voicing decisions
- **bbd = true; maxTimeMs = 550; bbdStages = 8192** — corrected to the real 2× MN3005 series pair.
- **antiAliasHz 3200 + bwQ 1.30 (REVOICED 2026-07-04 from the factory calibration; was a FLAGGED
  3800).** The reconstruction filter is modelled as one RESONANT 2-pole LP: measured (analytic
  `Biquad` magnitude) it is flat ≤900 Hz, **+3.0 dB at ~2.53 kHz**, 0 dB at ~3.5 kHz — a direct
  match to the factory curve. 3200 sits just above the 550 ms clock-Nyquist (3165 Hz) so the fixed
  filter dominates with a whisper of the real time-darkening. Brighter/peakier than the Carbon
  Copy's dark, non-resonant 2600 (that peak is why the DMM reads present/hi-fi).
- **midHz 0 / midDb 0 (the separate mid bump is DROPPED).** The old "+4 dB @ 650 Hz" was a
  mislocation of the 2.5 kHz filter resonance ~2 octaves low; the resonant LP (bwQ) IS the peak.
- **loopHpHz 80** — the gentle "little bass cut" (lows largely preserved).
- **satDrive 0.45 / satAsym 0.06** — the NE570/571 compander knee + a touch of BBD warmth (subtle).
- **modDepthFrac 0.10 (delay-PROPORTIONAL, factory-verified ±10%), TRIANGLE LFO.** BBD clock
  modulation is a percentage of the delay period (varicap on the clock), so the pitch swing scales
  with time — restoring the DMM's lush long-delay wobble (a fixed ms wrongly vanished at long
  delays). Depth knob (user Mod) scales it. Chorus rate ~0.85 Hz / Vibrato ~4 Hz (factory), set by
  the Chorus/Vibrato switch; whether you *hear* chorus vs vibrato also depends on the Blend position.
- **glideMs 80** — analog BBD repitch swoop on a Delay-knob change.
- **fbCeiling 1.06** — self-oscillates readily, a DMM feature; the resonant LP adds loop gain but
  the in-loop compander sat + loopLimit keep it bounded (offline: sustained + bounded at ~1.6 tail).
- **Mix law = TRUE CROSSFADE** (not the DD-7/Carbon Copy dry+wet): `(1−mix)·dry + mix·wet`, so
  full-wet = vibrato (dry removed), mid = chorus. The feedback write (delay input = dry + fb·wet)
  is unchanged — only the output blend differs.
- **IoStage = LOADED (gentle).** ~100 kΩ inverting input → a gentle high-shelf cut (−1.5 dB from
  ~3 kHz, −0.5 dB level) representing the loading (mostly relevant with a high-Z guitar; subtle
  here since the predelay sits after the buffered drive). ~300 Ω buffered output; subsonic couplings.
- **Panel = the DMM's real FIVE knobs + a switch: Delay / Feedback / Blend / Depth / Level, plus a
  CHORUS/VIBRATO switch.** LEVEL = master output Volume (new `predelayLevel` param, unity at 1,
  applied only for the DMM). The CHORUS/VIBRATO switch (new `predelayChorusVib` param) selects the
  LFO speed range like the EH7850's toggle — **Chorus ≈ 1 Hz (slow), Vibrato ≈ 4 Hz (fast)** — it
  does NOT change the waveform. (Robbie correction 2026-07-03: "the Memory Man should have 5 knobs
  and a switch.") No tone control. Whether you perceive chorus vs vibrato still also depends on the
  Blend position (full-wet crossfade = the pitch wobble heard directly = vibrato).
