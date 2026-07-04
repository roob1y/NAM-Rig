# Boss DD-7 — verified circuit (for PreDelayBlock kDD7 voicing)

Source of record: **Roland/Boss DD-7 SERVICE NOTES, May 2008 (doc #17058564E0)** — full parts
list + analog & digital circuit diagrams. Mirrors: synfo.nl/servicemanuals/Boss/DD-7_SERVICE_NOTES.pdf,
manualmachine.com/boss/dd7, archive.org boss_DD-7_SERVICE_NOTES, elektrotanya. Plus the official
Boss spec page (boss.info/global/products/dd-7/specifications) and owner's manual
(static.roland.com/assets/media/pdf/DD-7_e01_W.pdf). Deep-research pass 2026-07-03.

Everything below is from the service-notes schematic/parts list unless marked (inference) or (review).

## Signal path (stage by stage)
guitar → **JFET input buffer (Q1/Q2 = 2SK880-GR)**, 1 MΩ in, 10 µF coupling (C1/C2/C5/C6),
clamp diodes 1SS362FV → op-amp conditioning (IC1 NJM2115V, IC3/4/5 NJM4558M) → **AK4552VTP
24-bit stereo codec ADC** → **µPD800402 custom Roland DSP** (delay + Analog/Modulate/Reverse,
delay memory in Samsung K4S641632K 64 Mbit SDRAM) → codec DAC → reconstruction op-amp (IC5) →
**mix: analog dry (unity) + scaled wet** at IC3 → **1 kΩ output (R85/R88), 10 µF coupling (C75/C76)**.

## Facts that drive the DSP voicing
- **Input impedance 1 MΩ** (spec + service notes). = the DI reference (1 MΩ) → NO loading shelf; input is transparent buffered. Input coupling 10 µF into 1 MΩ → HP ≈ 0.016 Hz (inaudible).
- **Output impedance 1 kΩ** (R85/R88 series). Output coupling 10 µF into ≥10 kΩ load → HP ≈ 1.6 Hz (inaudible). Buffered bypass.
- **NO compander.** The DD-7 dropped the DD-2/DD-3 NE570 compress/expand + 7 kHz pre/de-emphasis. De-emphasis/HPF are inside the AK4552 (digital). → repeats are NOT companded/compressed; the "warmth" is NOT a compander effect.
- **24-bit AK4552 codec, full-range.** Repeats are essentially full audio bandwidth and **do NOT degrade per pass** (SDRAM addressing at a fixed clock, not a BBD clock; Boss: "won't degrade when it repeats"). Bandwidth is constant vs delay time. Contrast the 12-bit DD-2/DD-3 which get lo-fi per repeat.
- **Standard digital mode has no in-loop tone filter.** So repeats stay clean/bright; the only intentional darkening is the separate **Analog mode** (models the DM-2 = progressive LPF) — a different mode, not the standard voice.
- **Feedback self-oscillates** at high F.BACK ("Trick Sound", manual: oscillation increases volume). No documented in-loop EQ.
- **Mix = dry-always-unity + wet-added** (E.LEVEL scales only the wet; dry is a fixed analog through-path). NOT a crossfade. This is the authentic delay-pedal blend and now the block's mix law.
- Character (review): "clear without being brittle… defined warmth" (Premier Guitar) — i.e. clean/full-band but not harsh; that comes from the clean 24-bit path + analog JFET-in/op-amp-out buffers, not from any HF roll-off.
- Modes: max 6.4 s; Hold 40 s; Modulate = static gentle chorus ("just enough warble", borrowed from DD-20); Analog = DM-2 LPF model; Reverse.
- I/O nominal −20 dBu, load ≥10 kΩ, 55 mA @ 9 V.

## Could NOT verify (do not build on these)
- Exact anti-alias/reconstruction RC corner frequencies (schematic has the caps — 47 pF/470 pF etc. — but no stated corners; AK4552 sets the real band).
- Exact DD-7 sample rate (AK4552 supports ≤96 kHz; a guitar delay would run ~32–48 kHz → ~16–24 kHz Nyquist — inference, not documented).
- Modulate LFO rate/depth numbers (only "static/fixed" is documented).

## → kDD7 voicing decisions
bbd=false (fixed full bandwidth, no repitch). antiAliasHz ≈ 19 kHz (effectively full-band, non-degrading — NOT the old 15 k that darkened repeats). bwQ 0.5 (gentle, non-resonant — the digital converter LP has no presence peak). satDrive 0 (no compander, clean). fbCeiling 1.05 (sustains → self-oscillation-capable; the ~19 k in-loop LP loses a hair per pass + the loopLimit backstop so it won't run away numerically). Mix law = dry + wet (dry unity). IoStage = transparent buffered (subsonic coupling HPs only; 1 MΩ = DI ref, no shelf; no HF smoothing). Mod knob defaults 0 → DD-7 standard mode is clean; dialing Mod emulates the Modulate mode's gentle chorus.
