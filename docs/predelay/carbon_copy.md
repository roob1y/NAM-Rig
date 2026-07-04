# MXR Carbon Copy (M169) — researched circuit (for PreDelayBlock kCarbonCopy voicing)

Deep-research pass 2026-07-03 (method per `docs/predelay/NEXT_CHARACTER_HANDOFF.md`). Unlike the
DD-7, **MXR/Dunlop has never published a Carbon Copy schematic** and is known to be secretive about
it. The only circuit-level source is community-traced schematics that live as **image files** on
freestompboxes.org / diystompboxes.com / a Japanese analysis blog (kanengomibako) — all behind
Cloudflare/JS bot-blocks and un-OCR-able through the research tools. So the load-bearing sources
here are: the **official Dunlop M169 + M292 manuals** (hard specs), **hands-on repair traces** that
read the real part markings (note.com/ebi_san_tech, PedalPCB repair thread, Keld Ampworks,
Stompbox Electronics), and the **Coolaudio/Belling datasheets** for the identified parts. Component
values that only exist on the schematic images (coupling caps, exact filter R/C, feedback-loop
components) are **flagged "could NOT verify"** below and were NOT guessed.

Everything below is cited; anything marked (inference) is derived from the stage-count/delay math or
topology, not measured, and anything marked (unverified) is not confirmed by a reachable source.

## Signal path (stage by stage)
guitar → **1 MΩ buffered input** → compressor (½ of an **SA571** compander) → anti-alias LP
(op-amp **Sallen-Key**) → **BBD delay line: 4× BL3208 (2048-stage each) = 8192 stages in two series
pairs**, clocked by a two-phase driver whose LFO-modulated clock does the pitch warble → reconstruction
LP (Sallen-Key) → expander (other ½ of the SA571) → **dry (clean analog through-path) + wet blend**
via the MIX pot → **1 kΩ buffered output**. Bypass = a "Millennium-style" FET true-bypass
(Q10–Q13), NOT in the audio path. Op-amps = **TL062** (low-power JFET dual).

## Facts that drive the DSP voicing (cited)
- **Input impedance 1 MΩ; output impedance 1 kΩ; delay 20–600 ms; max output +8 dBV; noise
  reduction 2:1; modulation speed 0.2–2.2 Hz; delay distortion <1% @ 1 kHz; 26 mA @ 9 V.** All from
  the official **Dunlop M169 manual** (Specifications). → https://www.jimdunlop.com/content/manuals/M169.pdf
  - 1 MΩ = the DI reference (`[[capture-interface-hiz-1meg]]`) → **NO input loading shelf**; the
    input is transparent buffered, like the DD-7. (`[[drive-io-stages]]`: buffered/≥1 MΩ = no shelf.)
- **BBD = 4× BL3208 (Shanghai Belling clone of the Panasonic MN3208 / Coolaudio V3208), 2048 stages
  each, two hidden under a pot → 8192 total stages.** Read off real boards in repair traces.
  → https://note.com/ebi_san_tech/n/neb731a6b49a8 ; https://forum.pedalpcb.com/threads/mxr-carbon-copy-repair.18072/
  ; https://www.freestompboxes.org/viewtopic.php?t=7365 ; BL3208/V3208 = 2048-stage datasheet
  → https://www.coolaudio.com/docs/COOLAUDIO_V3208_DATASHEETS.pdf
  - BBD physics: t = N/(2·fclk), **Nyquist = fclk/2 = N/(4·t)**. With N = 8192: at 600 ms the clock
    Nyquist ≈ 8192/(4·0.6) ≈ **3.4 kHz** (inference) — so the repeats band-limit and DARKEN as the
    delay lengthens, and at short times the Nyquist is far above the fixed reconstruction filter (so
    the fixed filter, not the clock, sets the darkness there). The GGG AD-3208 designer notes the
    "600 ms" figure for an 8192-stage line implies a steep, low post-filter (a marketing stretch),
    which matches the Carbon Copy's dark reputation. → https://generalguitargadgets.com/effects-projects/modulationecho/ad-3208/
- **Compander = SA571 (NE570/571 family), 2:1 noise reduction.** Confirmed from a real-board signal
  trace ("U7 = SA571D") and a repair thread ("the SA571 compander"). Compress-before / expand-after
  the BBD; mostly transparent noise reduction with a gentle program-dependent knee (NOT gross
  distortion). Reissue/modern boards may carry the Coolaudio **V571** equivalent. → https://note.com/ebi_san_tech/n/neb731a6b49a8
  ; https://forum.pedalpcb.com/threads/mxr-carbon-copy-repair.18072/page-2
  - NB: the **V3207 is a BBD** (MN3207 clone), NOT the compander — a common mix-up. The compander is
    the SA571/V571.
- **Reconstruction/anti-alias filters = op-amp Sallen-Key active low-pass, steep (~30–36 dB/oct,
  generic BBD-delay figure ≈ 3 kHz −3 dB).** → ElectroSmash BBD reference:
  https://www.electrosmash.com/mn3007-bucket-brigade-devices . The Carbon Copy is famously one of the
  DARKER analog delays; the fixed reconstruction filter dominates the darkness at all settings.
- **Self-oscillates** at high Regen ("past ~2 o'clock"); a healthy unit self-oscillates with Regen
  cranked (its absence is a documented fault symptom). Each repeat re-passes the reconstruction LP +
  compander → repeats progressively darken. → https://forum.pedalpcb.com/threads/mxr-carbon-copy-repair.18072/
- **Mix = clean analog dry through-path + BBD wet blend** ("fully CW = 100% wet, fully CCW = 100%
  dry"). The dry never enters the BBD. → M169 manual. Matches the block's `dry + mix·wet` law.
- **Modulation = LFO on the BBD clock (pitch warble on the repeats), 0.2–2.2 Hz**, set by two
  INTERNAL trimmers on the M169 (WIDTH = left, RATE = right); no external mod control on the M169.
  Deliberately SUBTLE; more audible at longer delays. The Deluxe (M292) exposes SPEED/WIDTH knobs +
  a MOD footswitch and widens the rate to 0.1–10 Hz. → M169 manual; https://toomuchgear.wordpress.com/2014/11/04/because-you-didnt-read-the-manual-mxr-carbon-copy/
  ; https://www.diystompboxes.com/smfforum/index.php?topic=118495.0 ; M292 manual
  https://www.jimdunlop.com/content/manuals/M292.pdf
- **No tone control on the M169** — the wet is fixed-dark. The later Carbon Copy **Bright (M269)** /
  **Deluxe** add a Bright switch specced (M292) at **+4.5 dB @ 1.5 kHz and −3 dB @ 200 Hz** on the
  delayed signal — i.e. a treble lift + gentle low-cut, NOT a filter bypass. → M292 manual.
- Op-amps = **TL062** (low-power JFET dual, chosen over the TL072 to avoid LFO ticking). Forum-sourced
  from the freestompboxes trace (medium confidence). The **TL022** candidate was NOT found. → https://www.freestompboxes.org/viewtopic.php?t=7365

## Could NOT verify (do NOT build hard numbers on these — schematic images are bot-blocked)
- **The exact reconstruction/anti-alias −3 dB corner of the M169.** The ~2.6 kHz figure floating
  around is unconfirmed; ~3 kHz is only a *generic* BBD-delay ballpark. Whether the input anti-alias
  corner differs from the output reconstruction corner is also unknown. → voicing uses ~2.6 kHz as an
  ear-tunable perceptual match (see below), FLAGGED, not asserted.
- Exact **filter order / pole count and R/C values** of the Sallen-Key stages (real slope is
  30–36 dB/oct; our in-loop model is a single 2-pole — a perceptual approximation).
- **Input/output coupling-cap values** and the exact input/output buffer devices (only the 1 MΩ /
  1 kΩ impedances are specced).
- **Clock-driver part number and exact fclk range** — conflicting community reports (a two-phase
  MN3102/V3102/BL3102 driver vs. a CD4047 astable); neither confirmed from a readable schematic. The
  20 ms minimum implies an ~205 kHz clock which is above the BBD datasheet's 100 kHz max, so the
  20 ms spec is itself suspect. The audibly-correct behavior (darkening toward ~3.4 kHz at 600 ms) is
  what the model reproduces.
- **Regen min/max gain** and any **dedicated in-loop tone filter** values (only "self-oscillates,
  repeats darken each pass" is confirmed).
- **Factory-default mod rate/depth** (only the 0.2–2.2 Hz trimmer range is published; depth in ms/%
  is nowhere specced — universally described as "subtle").
- Whether an **NE571 pre-/de-emphasis** HF network is fitted, and its values.

## → kCarbonCopy voicing decisions
- **bbd = true; maxTimeMs = 600; bbdStages = 8192** — all verified (4× BL3208 = 8192; 600 ms max).
  The in-loop LP tracks the clock Nyquist so the repeats darken with time (physics already in the block).
- **antiAliasHz ≈ 2600 Hz + bwQ 0.5 (still not circuit-verified, but better GROUNDED as of
  2026-07-04).** The real reconstruction filter is a steep multi-pole Sallen-Key with a ~3 kHz −3 dB
  corner; our in-loop filter is a single 2-pole (~12 dB/oct), so a lower 2600 corner matches the
  *perceived* darkness/energy in the guitar band, and the in-loop recirculation steepens it on
  sustained repeats. NEW grounding: the Memory Man's 1978 FACTORY calibration (see memory_man.md)
  shows a same-era 8192-stage BBD reconstruction is −3 dB at ~3.2–3.5 kHz **with a +3 dB presence
  peak at 2.5 kHz**. The Carbon Copy is universally described as **darker** and has **no** presence
  peak (no Bright switch on the M169 — that's the Deluxe M292), so modelling it as a **non-resonant**
  (bwQ 0.5) LP at a corner **below** the DMM's ~3.3 kHz is circuit-consistent. Exact corner still
  needs the controlled-probe (capture the real M169, null against ours).
- **satDrive 0.50 / satAsym 0.06 (gentle).** Stands in for the SA571 compander's program-dependent
  compression knee + a touch of BBD even-harmonic warmth — subtle, since companding is a noise-
  reduction scheme, not a distortion. Also bounds the self-oscillating loop.
- **loopHpHz 100** — a gentle in-loop low-cut. The M169 is warm (keeps lows), so this is only a
  practical floor to stop bass runaway in self-oscillation, not the Deluxe Bright's 200 Hz cut.
- **midDb 0, presDb 0** — no documented mid bump (that's the Memory Man) and no presence sheen (it's
  a dark pedal, no top lift).
- **modRateHz 1.2 (within the verified 0.2–2.2 Hz), modDepthMs 1.3 (a FIXED subtle depth), FIXED
  internal amount (kCarbonCopyMod 0.35).** The M169 has NO mod knob — its modulation is two internal
  trimmers (WIDTH/RATE), always on and subtle — so the Carbon Copy voice ignores the user Mod param
  and bakes in a fixed subtle warble (effective ~0.35·1.3 ms). NB a brief 2026-07-04 experiment made
  the depth delay-proportional; it was reverted (with the DMM) because a fixed ms gives a consistent,
  musical warble at every delay whereas proportional warbles wildly at long delays. (Robbie correction
  2026-07-03: "the actual pedal doesn't have a mod knob.")
- **glideMs 70** — analog BBD repitch: turning the Delay knob sweeps the clock → the repeats pitch-
  bend/swoop rather than snapping (tape/analog-delay feel).
- **fbCeiling 1.18** — self-oscillates past ~2 o'clock. Higher than the DD-7's 1.05 because the
  dark in-loop reconstruction LP + compander sat eat more loop gain per pass, so the ceiling has to
  overcome those lumped losses to reproduce the hardware's self-oscillation; the in-loop compander
  sat + loopLimit bound the swell so it sustains into a dark drone without running away. (Offline-
  tuned: 1.10/1.13 decayed, 1.16–1.20 sustained + stayed bounded; 1.18 chosen.)
- **IoStage = buffered, subsonic couplings only.** 1 MΩ in (= DI ref → NO loading shelf) / 1 kΩ out,
  both verified from the manual; coupling-cap values unverified so modeled as subsonic high-passes
  (like the DD-7). No output HF smoothing — the darkness is the in-loop reconstruction LP, not the
  output buffer.
- **Panel controls = Delay / Regen / Mix only.** The M169's real knobs are exactly those three
  (Regen = feedback). No tone control and NO mod knob (the mod is internal trimmers, baked in as a
  fixed warble). This mirrors the DD-7 panel showing only its real hardware knobs.
