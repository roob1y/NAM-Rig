# Pitch + Envelope effects — circuit & DSP research

Deep research (2026-07-03) to inform circuit-accurate emulations, in the same spirit as
the drive worklogs. Organised by effect, each with a **DSP modeling implications**
subsection. Sources are inline as [Title](URL) and collected at the end. Where a value or
topology is disputed or unverified it says so explicitly — treat reverse-engineered
schematics as approximate, and closed-source DSP products (Whammy, POG, Eventide) as
"documented behaviour + informed inference," not confirmed algorithms.

Companion to `docs/pitch_env/BUILD_PLAN.md` (chain positions, block wiring, build order).

---

## 0. TL;DR for the build

**Envelope filter.** Every serious analog auto-wah is the *same* core — an envelope
follower driving a **two-integrator state-variable filter** — and the pedal's personality
lives in the **control element**, not the filter:

- **Mu-Tron III / Q-Tron** = photocell (LDR/vactrol) control → slow, smooth, **low-ripple**,
  can self-oscillate. The LDR lag is a *separate* low-pass on the control voltage on top of
  the detector RC — model it or you won't get the smoothness.
- **DOD FX25** = OTA control (LM13600) → fast, clean, **ripple-on-you** (no LDR smoothing).
- **Boss TW-1 / AW-2** = **photocoupler (FC1) + inductor (transformer L1) bandpass** with plain
  op-amps (RC3403A) — *corrected 2026-07-03: NOT an OTA/BA662, see A4/A5*. Photocoupler control
  → smoother than the FX25, but a single inductor-based bandpass, not an op-amp SVF.
- **MXR M-120** = CMOS duty-ratio switched-resistor → very low ripple, **limited Q** (won't
  self-oscillate), synthy.

Build a TPT/ZDF SVF (LP/BP/HP taps + HP+LP notch), drive cutoff with an **exponential** map
of a half/full-wave-rectified envelope, keep Q independent, and model the control-element lag.

**Pitch.** Two archetypes:

1. **Analog dividers (OC-2, Blue Box, Octavia)** — NOT pitch shifters. Comparator + boolean
   flip-flop division (down) or full-wave rectification (up). O(1)/sample, **zero added
   latency**, cheap, monophonic, warble on chords *by design*. This is the cheap, high-value,
   lean-CPU win and fits NAM Rig's identity.
2. **Digital shifters (Whammy, POG, Eventide, OC-5 poly)** — a time-domain 2-tap crossfade
   shifter (vintage, glitchy, mono, low latency) or a phase vocoder (clean, poly, higher
   latency/CPU). The **granular delay-line shifter** covers whammy/octave/detune cheaply and
   is what most pedals actually do; **PSOLA/WSOLA** is the clean mono-lead harmoniser;
   **phase vocoder** only when chords must track.

**Pitch tracking.** NAM Rig already has **MPM/NSDF** in `Tuner.h` — it's the right choice
(octave-up-robust, free clarity/confidence value, works on distorted tone). Factor a shared
RT `PitchTracker` from it. Budget ~20–25 ms unavoidable latency at low E, and gate the effect
on clarity.

---

# PART A — ENVELOPE FILTER / AUTO-WAH

## A0. General envelope-follower + VCF principles

**Signal split + sensitivity.** Every ECF buffers the input and splits it: one copy to the
filter (audio path), one to the envelope detector (side-chain), with a sensitivity/gain
control between. The Mu-Tron couples them (one Gain control feeds *both* filter and
detector), so raising gain for S/N also hits the detector harder — an authentic interaction
worth modeling. ECFs are very level-sensitive: too hot and the filter parks at the top of
the sweep, too weak and it barely moves ([GEO — Technology of Auto-Wahs](http://www.geofex.com/article_folders/ecftech/ecftech.htm)).

**Rectification.** A *precision* rectifier puts the diodes inside an op-amp feedback loop so
the ~0.6 V drop is divided out and sub-diode-drop signals rectify accurately. Full-wave
doubles the ripple frequency (easier smoothing, lower ripple); **the Mu-Tron III actually
uses a half-wave precision rectifier** (cheaper, more ripple character). A naive `abs(x)` in
DSP behaves like an *ideal* full-wave precision rectifier — for the Mu-Tron use half-wave
with slight asymmetry ([GEO](http://www.geofex.com/article_folders/ecftech/ecftech.htm), [ESP AN001 precision rectifiers](http://sound-au.com/appnotes/an001.htm)).

**Attack/release.** Rectified signal charges a smoothing cap: charge-path R = attack,
discharge-path R = release. Mu-Tron: 330 Ω attack, 47 kΩ→−9 V decay of a 4.7 µF cap. Typical
commercial ranges: **attack ≤ ~50 ms (often 5–30 ms), release ≤ ~500 ms (often 100–400 ms)**
([GEO](http://www.geofex.com/article_folders/ecftech/ecftech.htm)).

**Ripple is the key gotcha.** If the envelope isn't smooth, audio-rate fluctuation modulates
cutoff → "gargle" instead of a clean "bwow." Ripple shows mostly during *decay* and on *low
notes*. A fast control element (OTA/transistor) exposes it; a slow one (LDR) hides it because
the cell physically can't slew that fast — so model the **LDR lag as a separate one-pole
low-pass on the control voltage**, not just the detector RC ([GEO](http://www.geofex.com/article_folders/ecftech/ecftech.htm)).

**Exponential cutoff map.** Pitch perception is logarithmic and the analog control elements
(OTA transconductance ∝ bias current; LDR resistance vs LED current is power-law) give a
roughly **V/octave** law. In DSP: `fc = fc_min * 2^(k*env)`, i.e. map envelope → `log2(fc)`
linearly, not envelope → linear Hz.

**Why an SVF (not an RBJ biquad).** Nearly every analog ECF uses a two-integrator
state-variable/biquad topology because: (1) cutoff and Q are near-orthogonal — sweep `fc` by
scaling the two integrator gains while Q is a separate feedback term, so a fast sweep doesn't
disturb resonance; (2) no zipper — state variables *are* the LP/BP/HP outputs and evolve
continuously, whereas recomputing RBJ `b0..a2` from `cos/sin(2π fc)` every sample is expensive
and injects discontinuities when `fc` moves fast; (3) simultaneous LP/BP/HP taps = the mode
switch for free; (4) graceful behaviour up to self-oscillation. Use a **TPT/ZDF
(Zavalishin) SVF** — the digital form that stays tuning-accurate and stable at high Q and fast
modulation.

**Q near self-oscillation.** As Peak/Q rises the BP gain and ring time grow steeply; just
below oscillation you get the vocal "quack," past it the SVF self-oscillates into a sine at
`fc`. Decide whether to allow beyond-oscillation as a design choice.

## A1. Mu-Tron III (Musitronics, 1972) — the archetype

Mike Biegel's design, the first commercial envelope filter. Controls: **Gain** (sensitivity),
**Peak** (Q), **Range** (Hi/Lo), **Mode** (LP/BP/HP), **Drive** (Up/Down). Values from the
GEO teardown + Aion "Lumitron" clone BOM ([GEO](http://www.geofex.com/article_folders/ecftech/ecftech.htm), [Aion Lumitron](https://aionfx.com/project/lumitron-resonant-filter/)).

**Correction to a common myth: the Mu-Tron III does NOT use a CA3080/OTA.** Its control
element is a **dual optocoupler** (LED + two matched LDRs) — originally a Hamamatsu part
marked "805A" (service manual P873-13); modern clones use two **VTL5C3** vactrols. CA3080/
LM13700 belong to *other* pedals in the family.

- **Input (A1):** inverting stage, 120 kΩ + 1 MΩ Gain pot, ~0.1× to ~×40, feeding *both*
  filter and detector.
- **Filter (A2–A4):** classic 3-op-amp **state-variable** (summer/HP + two integrators),
  simultaneous LP/BP/HP; HP+LP gives a notch (a Q-Tron addition). Cutoff control: two matched
  220 kΩ resistors with the two LDRs in **parallel** — brighter LED → lower LDR R → both
  integrator corners rise together. Two elements swept at once = why a single-element foot-wah
  can't do it and why the SVF is essential. Integrator caps ≈ 1 nF + 1.8 nF pairs; **Range**
  switch adds parallel C to shift the band down. **No initial-frequency knob** (only Hi/Lo).
  Peak = 150 kΩ audio pot, can reach self-oscillation.
- **Detector (A5, A6):** **precision half-wave** rectifier → 330 Ω into 4.7 µF (attack),
  discharge through 47 kΩ to −9 V (release ≈ ~220 ms nominal, plus LDR lag). A6 inverter =
  the Up/Down (Drive) switch (louder→brighter vs louder→darker).

**DSP modeling implications — Mu-Tron III.** TPT SVF with LP/BP/HP + HP+LP notch. Model the
**LED→LDR** path as (a) an exponential-ish static conductance map and (b) a **separate one-pole
lag (tens of ms)** on top of the detector RC — this is the smoothness. Half-wave precision
rectify (not `abs`), two selectable rectifier gains for Up/Down depth. The single Gain must
scale both filter drive and detector drive. Typical: cutoff sweep ~100 Hz–2–3 kHz (Lo/Hi
range), Q gentle→self-osc, attack ~5–20 ms, release ~100–300 ms effective. Gotchas: no
start-frequency knob; very level-sensitive; notch is a Q-Tron feature.

## A2. EHX Q-Tron / Q-Tron+ (Biegel for EHX, 1990s)

Biegel's own updated Mu-Tron: same photocell-SVF core (VTL5C3 vactrols), same detector.
**Control complement (verified 2026-07-03 from the EHX manual):** knobs **GAIN** + **PEAK**;
switches **MODE [LP | BP | HP | MIX]** (the 4th position is **MIX = band-pass blended with the
dry**, *NOT a notch* — correction to an earlier note here), **DRIVE [Up | Down]** (sweep
direction), **RANGE [Hi | Lo]**, **BOOST [Normal | Boost]** (engages the internal preamp →
Gain also sets volume + drives the filter for the "chewy" clip). The **Q-Tron+** adds a
**RESPONSE [Fast | Slow]** attack switch and an **effects loop (send/return)** — the loop
derives the envelope from the *pre-loop* clean signal so drives/comps in it don't kill the
dynamics (the "compressor before ECF flattens the sweep" fix) ([EHX Q-Tron manual](https://www.ehx.com/wp-content/uploads/2021/07/q-tron-manual.pdf), [GEO](http://www.geofex.com/article_folders/ecftech/ecftech.htm)).

**DSP implications — Q-Tron.** Start from the Mu-Tron model; implement **MIX = 0.5·BP + 0.5·dry**
(not a notch), a **Boost** preamp (drive the filter + level lift), and **Response** as the
attack time. The FX loop is meaningless in-plugin (omit). Params ≈ Mu-Tron.

## A3. DOD FX25 and MXR M-120 — two opposite "simple" pedals

Do **not** assume either uses a CEM/SSM chip — neither original does.

**DOD FX25 / FX25B — OTA state-variable.** Essentially **National's LM13600 datasheet Fig. 14
voltage-controlled SVF** (dual OTA), plus 4558-class op-amps, JRC4560 trigger, TL072 mix on
the FX25B (schematic labels TL072 but production units often show JRC4560 — documented
discrepancy). Basic diode+cap follower drives the OTA bias current, so **cutoff ∝ bias current
(exponential-ish, fast)**. Reported clean, low noise, low sweep noise ([mirosol FX25B](https://mirosol.kapsi.fi/2014/06/dod-fx25b-envelope-filter/)).
*DSP:* TPT SVF, exp map, ripple management matters (fast OTA, no LDR lag); attack ~10–30 ms,
release ~100–300 ms, cutoff ~150 Hz–2 kHz, moderate fixed Q; single (up) sweep; add Blend.

**MXR Envelope Filter M-120 (1978) — CMOS switched-resistor.** The surprise: **2× CD4069 hex
inverters + CD4066 quad analog switch**, controls originally just **Threshold** + **Attack**.
The filter is an SVF built from **4069 inverters as integrators**; the control element is a
**4066 switch chopped by an ultrasonic clock whose duty ratio the envelope varies**, so the
switch's *average* on-resistance changes with picking → switched-resistor VCF. An extra
post-detector lag stage makes it famously **low-ripple**. CMOS inverters have limited gain so
**Q maxes lower** and it won't self-oscillate → synthy, mid-forward ([diystompboxes M-120 (Mark Hammer)](https://www.diystompboxes.com/smfforum/index.php?topic=20811.0), [Effects Freak M-120](https://effectsfreak.com/effect/m-120-envelope-filter/)).
Note the modern **M82 Bass Envelope Filter is a different newer analog design** — don't conflate.
*DSP:* SVF with an exp/duty map of a heavily post-smoothed envelope; signatures = very low
ripple + limited Q. Attack is a front-panel control, release fixed-ish, cutoff ~200 Hz–2 kHz,
sweeps up. Don't over-resonate.

## A4. Boss TW-1 / AW-2 / AW-3

**TW-1 → AW-2 (analog):** the **AW-2 is basically a TW-1 with an LFO mixed into the
envelope-detector output** (confirmed) — same core filter, LFO added, TW-1's Up/Down switch
dropped, Rate/Depth pots added. **Control element = a photocoupler/opto-isolator (FC1) driving
a band-pass filter built around an inductor (transformer L1), with RC3403A quad op-amps —
NOT an OTA, NOT a BA662** (*corrected 2026-07-03 after verification; the earlier "BA662"
attribution was wrong*). BA662 appears in Boss only in the CS-2 compressor and VB-2 vibrato.
Bandpass-only (no LP/HP mode switch) ([AW-2 Auto Wah Mods / Will Pirkle](http://autowahmods.blogspot.com/2009/08/schematics.html), [Effects Database TW-1](https://www.effectsdatabase.com/model/boss/compact/tw1), [AMSynths BA662 usage list](https://amsynths.co.uk/2018/01/07/all-about-the-ba662-chip/)).
*DSP:* a **photocoupler-controlled bandpass** (so, like the Mu-Tron, model a control-element
lag → smoother than the FX25), but a single resonant band-pass rather than a full LP/BP/HP SVF;
an inductor/gyrator-style resonance if you want the exact flavour. For AW-2 add a **summable
LFO** into the control CV (mix env + LFO, rate/depth). Practically, a TPT SVF bandpass tap with
a modeled photocoupler lag is a faithful-enough model.

**AW-3 (digital):** it's a **DSP pedal** (8-bit CPU + DSP + NJM2100 analog I/O), with three wah
voicings plus a **Humanizer (vowel/formant)** mode and tempo/rate-synced LFO ([Boss AW-3](https://www.boss.info/us/products/aw-3/)).
Useful as a *feature template*: multiple filter voicings, a **formant/Humanizer** mode (model
as 2–3 parallel resonant band-passes interpolating between vowel targets under env/LFO
control), tempo-synced LFO, pick-strength sensitivity.

## A5. Envelope-filter disputes / verification results (updated 2026-07-03)

- **Mu-Tron integrator/Range caps — PARTIALLY CONFIRMED.** Topology confirmed by two
  independent clone lineages: two integrators tuned by **220k + LDR** with fixed low-nF caps,
  and a **Range switch adding ~2.2 nF in parallel per integrator** to drop the sweep band (Lo).
  The **2.2 nF Range cap is corroborated** by both Aion (Lumitron) and R.G. Keen (Neutron). The
  **exact per-integrator cap differs between respected clones**: Aion = **1 nF ∥ 1.8 nF (~2.8 nF)**;
  Keen's Neutron = **1.8 nF alone**. The original factory schematic is **image-only (raster
  scan), not machine-readable**, so the 1 nF companion cap couldn't be confirmed against the
  original. → Build target: **1.8 nF integrator + 2.2 nF Range parallel is the safe, corroborated
  value**; treat the extra 1 nF (→2.8 nF) as an optional Aion-specific tweak. It's a voicing
  detail to set by ear anyway, not a hard constraint ([Aion Lumitron PDF](https://aionfx.com/app/files/docs/lumitron_documentation.pdf), [Keen Neutron PDF](http://www.geofex.com/PCB_layouts/Layouts/neutronpub.pdf), [original factory scan (image-only)](https://el34world.com/charts/Schematics/Files/Effects/Mu_tron_iii_om_sch.pdf)).
- **Boss TW-1/AW-2 "BA662 OTA" — CONTRADICTED.** It is a **photocoupler (FC1) + inductor
  (transformer L1) bandpass with RC3403A op-amps**, no OTA (see A4). The "AW-2 = TW-1 + LFO"
  relationship is **confirmed**. Caveat: primary service schematics are image-only/gated, so the
  RC3403A/FC1/L1 identifications come from secondary teardown sources, not first-hand OCR of the
  parts list ([Will Pirkle AW-2 mods](http://autowahmods.blogspot.com/2009/08/schematics.html), [AMSynths BA662](https://amsynths.co.uk/2018/01/07/all-about-the-ba662-chip/)).
- Mu-Tron "805A" optocoupler isn't in any Hamamatsu catalog (original LDR curve unspecified;
  clones use VTL5C3); FX25 op-amp labelling (TL072 vs JRC4560) differs schematic-vs-unit;
  "MXR = CEM3320" is **false** for the M-120 (it's CMOS 4069/4066).

---

# PART B — PITCH PEDALS (hardware)

## B0. The core analog trick (down vs up)

Every analog **octave-DOWN** pedal (OC-2, Blue Box, U-Boat) does the same thing, nothing like
a pitch shifter: (1) low-pass the input hard to get a near-sine, (2) square it with a
comparator/Schmitt at the fundamental, (3) **divide with D flip-flops** — a CD4013 toggling
once per cycle = ÷2 = one octave down, cascade → ÷4 = two down. Exact, integer, phase-locked
→ perfectly in tune, **near-zero latency**, no pitch estimation. (4) make it musical. It only
works on **monophonic** notes because the comparator needs one dominant frequency; chords/
harmonic-rich/low notes make it miscount → **warble/glitch/dropout**, worst on low notes. This
is physical, reproduce it ([ValveWizard U-Boat](https://www.valvewizard.co.uk/uboat.html), [toshi.life OC-2](https://toshi.life.coocan.jp/review/en_diy_analog_octaver.html)).

**Octave-UP (Octavia)** is different: **full-wave rectification = frequency doubling** of a
*distorted* signal, ring-mod-like.

## B1. Boss OC-2 Octave (1982) — the key one

Fully analog, DIRECT + OCT1 (−1) + OCT2 (−2). Schematics are all reverse-engineered (differ
slightly; original ICs ROHM BA634, 2SC732/2SK30A obsolete — substituted in every clone).

Chain: input buffer → split (DIRECT clean + detection path) → **aggressive LPF (~≤1 kHz)** to
make it look like a sine → **peak-hold + squaring** (the OC-2's tracking secret: stable square
even as the note decays) → **flip-flop dividers** (CD4013 ÷2 = OCT1; a second FF, orig BA634 /
DIY CD4027, ÷4 = OCT2). **The clever bit:** the OC-2 doesn't output the raw square — it
**half-wave rectifies the original tone and flips the op-amp gain sign every other cycle**
(driven by the divider), stitching the *real audio waveform* into a sub-octave that carries
some timbre (smoother than the Blue Box). Then **output LPF** tames the switching hash and an
**envelope follower** makes the octave track the dry note's dynamics and gate on silence.
Three level pots sum DIRECT/OCT1/OCT2 ([toshi.life](https://toshi.life.coocan.jp/review/en_diy_analog_octaver.html), [Champion Leccy](https://championleccy.com/2018/01/01/project-2-the-rotund-robot-part-2-analysing-schematics/)).
Near-zero latency: worst case one half-cycle of the note. (Which op-amp is env-follower vs
tracking filter is disputed among DIY analysts.)

**DSP modeling implications — OC-2.** Don't pitch-shift; model the flow: (1) detection LPF
~800 Hz–1 kHz on a copy; (2) Schmitt comparator w/ hysteresis or period tracker w/ debounce;
(3) boolean `state ^= 1` per rising edge for OCT1, cascade for OCT2; (4) for OC-2 smoothness,
multiply the **dry (or half-wave-rectified) audio** by the toggle-derived ±1 (sign-flip
alternate cycles) rather than outputting the bare square; (5) output LPF + envelope follower
from the dry; (6) mix. Gotchas to reproduce: chord warble (free if you model the comparator
honestly — add a "tightness"/hysteresis knob to tame), **gate on quiet/ambiguous input** (else
motorboating), low-note jitter. O(1)/sample, no delay line — cheap and truly zero-latency.

## B2. MXR Blue Box — two-down + fuzz

The raw cousin: same LPF→high-gain **Schmitt** front end, but the square is **kept as the fuzz
voice** and cascaded ÷4 = two octaves down (shorting CD4013 pins 1&3 → one octave, a known
mod). Output is the **raw divider square** (synthetic/organ-like, not re-synthesised like the
OC-2). Only **Output** + **Blend** (fuzz↔octave), with a 56 kΩ resistor bleeding a little fuzz
onto the octave so you can never get pure octave. Sputters on ambiguous input — that chaos is
the charm ([tagboardeffects Blue Box](https://tagboardeffects.blogspot.com/2013/04/mxr-blue-box.html)).
*DSP:* same front end but keep the square (soft-clip it as fuzz), two toggles = ÷4 with a
1/2-octave switch, model the 56k bleed, crossfade the two squares, keep the glitch (optional
stability knob). Trivial CPU, zero latency.

## B3. Analog octave-UP — Tycobrahe / Roger Mayer Octavia

Different mechanism. **Fuzz FIRST** (heavily distorting germanium/silicon stage), then a
**transformer phase-splitter** feeds **two germanium diodes (1N34A) as a full-wave rectifier**
→ frequency doubling → screaming, unstable octave-up. Full-wave rectifying ≈ multiplying the
signal by a square at its own frequency ≈ self-squaring → sum/difference tones (`f+f=2f`,
`f−f=0`), i.e. **ring-mod-like**: strong on clean sustained single notes, inharmonic chaos on
chords. Pre-distortion is essential (flattens amplitude, adds the harmonics that make the
doubled component thick). Octave on/off = disconnect one rectifier diode. Best around the 12th
fret/neck pickup, muddy low down ([Fuzz Central Octavia](https://fuzzcentral.ssguitar.com/octavia.php), [Aion Octahedron](https://aionfx.com/project/octahedron-octave-fuzz/)).
*DSP:* fuzz (asymmetric germanium-ish clip, mis-biased 9 V flavour) → `abs()`/**asymmetric**
full-wave rectify (unequal half gains for the sputter) → AC-couple (remove the `f−f` DC) →
tone LPF; optional transformer colour (saturating HP/LP + mild hysteresis); octave-defeat =
blend rectified vs pure-fuzz. Level/pitch-dependent prominence. Zero latency, cheap.

## B4. Boss OC-3 / OC-5 (polyphonic, digital)

OC-3 (2003) = first compact **polyphonic** octave pedal; DSP, three modes (OC-2 emulation,
Drive, Poly). OC-5 (2020) = analog/digital hybrid, adds an **upper octave** in poly, refined
**Range**, faster tracking. Poly mode is **not** frequency division (a single FF can't handle
multiple frequencies) — it's **DSP pitch tracking + transpose**, which is why it tolerates
chords but has **audible latency** (must observe ≥1 cycle) the analog OC-2 doesn't. The
signature **Range ("Lowest") control** limits which notes get octaved — at the extreme only
the single lowest note in a chord is octaved, the rest pass dry (a movable low-frequency
crossover on the octave path) ([GuitarPedalX OC-5](https://www.guitarpedalx.com/news/gpx-blog/bosss-3rd-generation-oc-5-octave-introduces-state-of-the-art-tracking-and-new-upper-octave-while-expanding-on-the-very-best-of-the-oc-2-and-oc-3), [toshi.life](https://toshi.life.coocan.jp/review/en_diy_analog_octaver.html)).
*DSP:* reuse the analog-divider model for OC-2/Drive modes; Poly needs polyphonic pitch
detect + per-voice octave (or band-split → per-band divider gated by Range). Honesty:
**poly mode must feel laggier than the OC-2 mode** — that difference is part of the character.

## B5. DigiTech Whammy (1989) — first mass-market digital shifter

Pitch detection **licensed from IVL Technologies**. The IVL interview is the best primary
source: IVL determines pitch in **~2 cycles** with a hybrid tracker, and shifts with a refined
**"chop and loop"** = **time-domain pitch-synchronous splicing** with pitch-informed splice
points — they *deliberately avoided* frequency-domain resynthesis for cost. Hence great on
single notes, falls apart (warble) on chords ([SOS — IVL/Speckeen interview](https://www.soundonsound.com/people/fred-speckeen-ivl-tech-pitch-craft), [Wikipedia Whammy](https://en.wikipedia.org/wiki/DigiTech_Whammy)).

The **expression pedal** continuously interpolates a **pitch ratio** between two mode
endpoints: Whammy modes shift the whole signal (up 4th/5th/1/2 oct, down to −3 oct, dive-bomb);
Harmony modes keep the dry note and sweep only the *added* voice between two intervals (moving
dyad); Detune modes repurpose the treadle as wet/dry chorus blend. Whammy 5 adds a
**classic/chords** switch — Classic keeps the vintage mono artifacts, Chords is a heavier
(likely phase-vocoder) poly algorithm with **more latency** ([SOS Whammy 5 review](https://www.soundonsound.com/reviews/digitech-whammy-5th-gen)).
*DSP:* time-domain pitch-synchronous crossfade/splice (SOLA/PSOLA) driven by a mono tracker;
expression pedal = a **log-domain (cents) ratio interpolation**, slewed. **Preserve** the
glitch/comb dip, warble, phasing on big ratios, chord mistracking wobble — a "too clean"
Whammy is wrong. Couple glitchiness to tracker mis-latch/confidence.

## B6. EHX POG / POG2 / Micro POG (Dave Cockerell)

Clean **polyphonic** octaves (dry, ±1, ±2 oct, each mixable) — tracks single notes,
arpeggios, full chords glitch-free. Only ever shifts by **fixed harmonic intervals** (exact
octaves), which is far easier to do cleanly than arbitrary ratios; community inference is it's
**FFT/frequency-domain** on a DSP56364 (EHX never published it). Near-zero perceived latency.
POG2 adds upper-octave **detune**, an **Attack** control (fade voices in for swells/organ), and
a resonant **LPF** — some perceived "latency" is actually the Attack envelope. ([EHX POG2](https://www.ehx.com/products/pog2/), [KVR — how POG/HOG work](https://www.kvraudio.com/forum/viewtopic.php?t=208046)).
*DSP:* fixed-integer-ratio poly shift (exploit ×2/×4/×0.5/×0.25 for cheap clean octave-locked
OLA or harmonic-aligned resampling; or a phase vocoder). Fully polyphonic — don't gate on one
pitch. Character is the **per-voice mix, upper-octave detune, Attack onset envelope, resonant
LPF**, not shift artifacts. If it glitches on a chord it's not a POG.

## B7. Eventide H910 / H949 Harmonizer (1975/77) — the origin

Best-documented case (Valhalla writeups + Eventide patent). **H910 = a 2-tap delay-line
shifter**: delay written at normal rate, **read faster/slower** to change pitch, **two read
taps crossfaded** (H910 = simple triangle crossfade). The glitch is physics: the read tap
eventually catches the write pointer and must **jump**; a raw jump = a pop, so you fade one tap
down and the other up. **Crossfade dilemma:** long crossfade → constant delay offset → comb
"metallic" colour; short crossfade → phase-mismatch **cancellation dip = audible glitch**. That
tradeoff *is* the vintage voice. **H949** adds the ALG-3 de-glitcher (patent US 4,464,784):
**autocorrelation** finds where the two taps share real phase similarity and aligns the splice
→ clean on **near-periodic (mono)** signals, still glitches on chords/drums. **MicroPitch** =
small dual-voice detune (thickening) ([Valhalla H910](https://valhalladsp.com/2010/05/07/early-pitch-shifting-the-eventide-h910-harmonizer/), [Valhalla H949/de-glitching](https://valhalladsp.com/2010/05/07/pitch-shifting-the-h949-and-de-glitching/)).
*DSP:* model literally — circular buffer, two read taps at the shift ratio, crossfade window
(the "authenticity tone knob"): H910 triangle+longish → comb+glitch; H949 add an
autocorrelation splice search. Let it glitch on chords/drums (drive the amplitude dip from
autocorrelation confidence). Preserve comb colour, MicroPitch detune shimmer, and
feedback-through-shifter **shimmer/descending** behaviour.

## B8. "Clean poly" vs "vintage glitchy" — unifying picture

Vintage glitchy (H910/H949, Whammy Classic, OC-2) = **time-domain splice/crossfade shifters**
on a **monophonic** period estimate — magic on single notes, characterful break on chords
(glitch = failed splice on a low-autocorrelation signal). Clean modern poly (POG, OC-5 poly,
Whammy Chords) = **frequency-domain** or **fixed-integer-ratio** engines — glitch-free chords
at the cost of latency and a subtly synthetic/phasey timbre. **Latency floor = ≥1 waveform
cycle (~12.5 ms at low E)**; rises with transposition amount, more audible on clean tone —
which is why analog dividers *feel* faster than any digital octaver.

---

# PART C — PITCH-SHIFTING ALGORITHMS (DSP)

Core identity: **pitch shift = time-stretch by β, then resample by 1/β** (β = 2^(semitones/12)).

## C1. Time-domain granular / crossfaded variable delay-line — cheapest, lowest latency

Write to a circular buffer; read with a **delay time that ramps** (sawtooth) → Doppler shift.
Puckette's closed form: with sawtooth freq `f` sweeping a **window `w`** samples,

```
β = 1 + w·f / fs        →   to hit interval β:   f = (β − 1)·fs / w
```

Negative `f` (descending ramp) shifts **up**. Sustain a fixed β with a wrapping sawtooth and
**crossfade two taps a half-period apart**, each **half-sine windowed** (sin²+cos²=1 → constant
power *for uncorrelated taps*). Use **4-point cubic** fractional interpolation ([Puckette §7](https://msp.ucsd.edu/techniques/v0.11/book-html/node115.html), [Dattorro Effect Design Pt.2](https://ccrma.stanford.edu/~dattorro/EffectDesignPart2.pdf)).

Pseudocode:
```cpp
phase += f / fs;  if (phase >= 1.0) phase -= 1.0;
double ph2 = fmod(phase + 0.5, 1.0);
double d1 = D0 + phase*w,  d2 = D0 + ph2*w;
double g1 = sin(M_PI*phase), g2 = sin(M_PI*ph2);
out = g1*readCubic(buf, d1) + g2*readCubic(buf, d2);
```

**Artifacts:** warble/AM at rate `f` (the half-sine power law fails for *correlated*/tonal
input → beating/tremolo — the dominant artifact on clean guitar); transient flam/smear at
grain seams. Trade grows with transposition (bigger β forces `f` up). **Window `w`** ≈ 30–100 ms:
small → less latency/echo but more warble; large → smoother pitch but audible double-echo and
smear. **Latency ~ D0 + w** (few ms to ~50 ms). **CPU: lowest** — O(1)/sample, no FFT, no
detection; works on mono *or* poly (harmonic-agnostic). This is what whammy/octave/detune
pedals and grain-delays actually use.

## C2. SOLA / WSOLA / PSOLA — time-domain overlap-add (gold standard for mono)

**WSOLA** ([Verhelst & Roelands 1993](https://www.isca-archive.org/eurospeech_1993/roelands93_eurospeech.html)): rigid synthesis frame positions, but slide the *analysis* frame within a tolerance
window to the offset that **maximises waveform cross-correlation** with the naturally-continuing
output, then overlap-add. No explicit pitch marks needed; online, time-varying rates; robust.
Limits: transients shorter than a frame get *repeated*; only the largest harmonic source is
tracked → degrades on polyphony (better than PSOLA there, worse than a phase vocoder).

**PSOLA** ([Aalto PSOLA](https://speechprocessingbook.aalto.fi/Representations/Pitch-Synchoronous_Overlap-Add_PSOLA.html)): needs **f0 + per-period pitch marks**. Window a **2-period Hann grain** at each
mark, then **overlap-add at a new spacing T0/β** (closer → higher). Because each grain is a
windowed copy of one period, the **formant/timbre is preserved automatically** — only the
repetition rate moves (no chipmunk from the shift). Latency ≈ **one pitch period + window**
(~12–25 ms at low E). Best natural quality for **monophonic** leads at moderate ratios; a
single mislocated mark = a glitch; poly = no. CPU low (pitch detection dominates).

## C3. Frequency-domain STFT phase vocoder — for polyphony

STFT (Hann, **75% overlap / hop = N/4**) → per-bin **instantaneous frequency via phase
unwrapping** across frames → either time-stretch-then-resample or direct spectral peak-shift →
ISTFT ([Bernsee — pitch shift via FFT](http://blogs.zynaptiq.com/bernsee/pitch-shifting-using-the-ft/), [Laroche & Dolson 1999](https://www.ee.columbia.edu/~dpwe/papers/LaroD99-pvoc.pdf)).

```
Δφ_true = (φ[k,m] − φ[k,m−1]) − k·ω_bin·R          // R = hop
f_true[k] = k·ω_bin + princarg(Δφ_true)/R          // instantaneous freq
```

**Phasiness** (smeared/reverberant) comes from lost vertical phase coherence; **transients
pre-smear** across the window (worst shifting up). Cure = **phase-locking**: Laroche & Dolson's
**identity/scaled phase locking** — find spectral **peaks**, lock each peak's "region of
influence" to the peak's phase. Their **peak-shifting** variant rotates each region's phase by
`e^(jΔω·R)` — no arctan/unwrap, **cost independent of β**, and copying a peak to several targets
in one pass gives near-free **multi-voice harmony**. Integer-bin shift → 50% overlap OK;
fractional-bin → 75% overlap (sidebands −51 dB vs −21 dB). **Latency: highest** (FFT+hop, tens
of ms; N=2048@44.1k ≈ ~46 ms window). **CPU: highest** (2 FFTs/hop + per-bin math). Handles
arbitrary/**polyphonic** material. Reference: [smbPitchShift.cpp](http://downloads.dspdimension.com/smbPitchShift.cpp).

## C4. Comparison

| Property | Granular delay-line | WSOLA | PSOLA | Phase vocoder (peak-locked) |
|---|---|---|---|---|
| Domain | Time (Doppler) | Time (corr OLA) | Time (pitch-sync OLA) | Frequency (STFT) |
| Mono/poly | Both | Mono-best | **Mono only** | **Poly** |
| Needs pitch detect | No | No | **Yes (marks)** | No (peak-pick) |
| Transients | Smear/flam | Repeats short ones | Good | Pre-smear (phase-lock helps) |
| Formants | Shift (chipmunk) | Preserved | **Preserved** | Shift unless corrected |
| Latency | **Lowest** | ~frame | ~1 period+win | **Highest** |
| CPU | **Lowest** | Low–mod | Low | **Highest** |
| Use | Whammy/octave/detune | mono stretch | mono transpose/harmony | poly harmoniser |

Bernsee's rule: (P)SOLA for speed + small mono shifts; phase vocoder for large shifts +
polyphony, accepting transient smear ([Bernsee overview](http://blogs.zynaptiq.com/bernsee/time-pitch-overview/)).

## C5. Formant preservation (avoid "chipmunk")

Formants are fixed body/tract resonances that shouldn't move with pitch; any **resampling**
drags them → thin/"Mickey Mouse." Fix = source-filter decouple: estimate the **spectral
envelope**, shift only the fine structure, **re-impose the original envelope** (amplitude-only,
cheap in a phase vocoder). Envelope estimation: **cepstral liftering** (cheap, popular) or
LPC/true-envelope (better, more CPU). **PSOLA gets it for free.** For guitar it matters less
than for voice but keeps octave-down from getting "boxy" and octave-up from thinning — a
worthwhile *option*, not a necessity ([Bernsee overview §4](http://blogs.zynaptiq.com/bernsee/time-pitch-overview/), [DAFX Ch.9](https://www.dafx.de/DAFX_Book_Page/chapter9.html)).

## C6. Algorithm recommendations for the plugin

| Use case | Method | Latency | CPU |
|---|---|---|---|
| Sub-octave (−12) | analog divider (B1) or granular | ~0 / 5–20 ms | very low |
| Detune-thicken (±5–20 cents) | granular 2-tap (warble negligible at tiny β) | 5–15 ms | very low |
| Octave-up (+12) | Octavia rectify (B3) / PSOLA mono / PV poly | ~0 / 12–30 / 30–50 ms | low/low/high |
| Whammy sweep (continuous) | **granular** (only method that glides β cheaply, low latency) | 5–20 ms | very low |
| Clean lead harmony (mono, diatonic) | **PSOLA/WSOLA** formant-preserved (PV if poly) | 12–40 ms | low–mod |

Start with the **granular 2-tap** (covers whammy/octave/detune, works on chords, minimal code
— use `f=(β−1)fs/w`, w≈40–60 ms, cubic interp, half-sine crossfade). Add a **PSOLA/WSOLA** path
for clean mono harmony gated on the tracker's confidence. Reserve the **peak-locked phase
vocoder** for a poly-harmoniser mode (N=2048, 75% overlap, identity phase-locking + transient
detection). Optional cepstral formant toggle on the octave voices. Fundamental tension:
**latency vs artifact** — live guitar wants <~15 ms, pushing toward granular/PSOLA.

---

# PART D — PITCH DETECTION / TRACKING

## D0. Why guitar is octave-error-prone

The electric guitar's **fundamental is often weaker than its 2nd/3rd harmonics** (pickups act
as a differentiator), and **distortion multiplies partials** — so a detector sees many
near-equal candidate periods and a strong 2nd harmonic *is* genuinely periodic at half the
true period. The whole game is **which peak/dip you pick** ([Cycfi — pitch detection](https://www.cycfi.com/2017/10/fast-and-efficient-pitch-detection/)).

## D1. Autocorrelation — fragile on guitar
ACF peaks at period multiples, but fixed-window ACF is amplitude-sensitive (peaks grow with
lag → **too-low/sub-octave** error), and on guitar the strong 2nd-harmonic peak rivals the
fundamental → **octave-up** error. ~5% gross error, mostly too-high ([YIN paper](http://audition.ens.fr/adc/pdf/2002_JASA_YIN.pdf)).

## D2. AMDF / difference function
`AMDF(τ)=Σ|x_i−x_{i+τ}|` — dips at period multiples, amplitude-insensitive (removes ACF's
too-low mechanism), cheap (favourite on embedded). Moving ACF→difference function drops YIN's
gross error 10.0%→1.95%. But global min at τ=0 and deeper higher-order dips still cause octave
errors; not enough alone for distorted guitar ([AMDF ref](https://hajim.rochester.edu/ece/sites/zduan/teaching/ece472/projects/2014/Lio_Chen_SpeechPitchDetection.pdf)).

## D3. YIN
Six steps, each killing an error class: (1) ACF 10.0%; (2) squared difference function 1.95%;
(3) **CMNDF** `d'(τ)=d(τ)/[(1/τ)Σd(j)]`, starts at 1 → removes zero-lag trap (no upper freq
limit) and too-high errors → 1.69%; (4) **absolute threshold 0.1**, smallest τ below it →
attacks sub-octave → 0.78%; (5) parabolic interpolation (sub-sample); (6) best local estimate
+ ±20% restricted re-search → **0.50%**. Only method with *balanced* low/high error. **d'(T)
doubles as confidence** (small = confident). Latency ≈ T_max+T. *Guitar caveat:* fixed 0.1 +
"smallest τ" can still report octave-up when the fundamental is very weak ([YIN](http://audition.ens.fr/adc/pdf/2002_JASA_YIN.pdf)).

## D4. McLeod Pitch Method (MPM) / NSDF — the recommended engine
Built for exactly the guitar case (strong harmonics, no LPF prefilter, cents accuracy, RT).
**NSDF** `n'(τ)=2r'(τ)/m'(τ)` is bounded to **[−1,+1]** for any amplitude (m' = the running
energy term, computed incrementally). Peak-picking is the octave-robust trick: find **key
maxima** (highest value between positive- and negative-sloped zero crossings), parabolically
refine, set threshold **k·n_max (k≈0.8–1.0, ~0.9)**, and **take the FIRST key maximum above
it** — so on a strong-2nd-harmonic signal with near-equal maxima at the half-period and the
true period, it picks the **longer true period** → resists octave-**up** error. **Clarity =
the NSDF value at the chosen peak** (0–1), a free amplitude-independent confidence. Works from
as few as **2 periods** → better vibrato/slide tracking, smaller window. Same asymptotic cost
as YIN. This is what real guitar tuners use ([McLeod & Wyvill 2005](https://www.cs.otago.ac.nz/graphics/Geoff/tartini/papers/A_Smarter_Way_to_Find_Pitch.pdf)). **NAM Rig's `Tuner.h` already implements this.**

## D5. Zero-crossing / period tracking (analog dividers)
Cheapest (comparator + counter), near-zero latency, but needs **aggressive LPF** because guitar
harmonics cause extra zero-crossings; fails on distorted/poly. Cycfi's software refinement uses
**peak detection + Schmitt** instead of zero-crossings to reject multi-triggers — very low
latency for clean tone, but still octave errors on hard onsets/evolving tone. Fine for a clean
octave/synth lock, not a distorted harmoniser ([ValveWizard U-Boat](https://www.valvewizard.co.uk/uboat.html), [Cycfi](https://www.cycfi.com/2017/10/fast-and-efficient-pitch-detection/)).

## D6. Real-time constraints + recommendation
**Low E (~82 Hz, period ~12.1 ms)** sets the window floor: need ≥2 periods (~24 ms) before a
trustworthy estimate → the common **2048-sample window**. Short window = jittery/octave-prone;
long = stable but sluggish on slides. Run **incrementally**: update per hop (256–512 samples,
75% overlap) not per sample; **seed the lag search from the previous estimate (±1 semitone)**,
full-range only on onset/confidence-drop (biggest CPU win, also suppresses octave jumps);
optionally **decimate** to ~12 kHz for the correlation stage. **Gate the effect on confidence**
(MPM clarity / YIN d') + RMS, with hysteresis/median smoothing and reject low-confidence exact
×2/×0.5 jumps.

**Recommendation:** **MPM/NSDF as primary** (octave-up-robust, free clarity gate, no prefilter,
2-period window) — already proven in tuners and already in `Tuner.h`. Factor a shared RT
`PitchTracker` with incremental seeded search. **Params (48 kHz):** window 2048 (~42.7 ms),
hop 256–512, k≈0.9, clarity gate ~0.85, ±1-semitone locked search. **Latency ≈ 20–25 ms at
low E** (window-dominated; less on high strings — physics, not a bug); a **variable window**
(shorter high, longer low) is a pragmatic option. pYIN/CREPE are accuracy benchmarks, too
heavy/laggy for the live path ([pYIN](https://qmro.qmul.ac.uk/xmlui/bitstream/handle/123456789/6040/MAUCHpYINFundamental2014Accepted.pdf?sequence=2)).

---

# Consolidated map → NAM Rig blocks

- **`EnvFilterBlock` (mono pre):** TPT/ZDF SVF (LP/BP/HP + notch) + rectifier→exp-map envelope
  follower with a modeled control-element lag. Voice-select Mu-Tron (LDR, smooth, self-osc) /
  FX25 (OTA, fast, ripple) / Boss (photocoupler + inductor bandpass, +LFO on AW-2) /
  MXR (limited-Q, synth) as fixed voicings. Zero latency.
- **`PitchBlock` (mono pre, before drive):** Type-select engine —
  - *Sub-octave (OC-2)* + *Blue Box* + *Octavia up*: analog-divider / rectifier models, O(1),
    zero latency, no tracking. **Ship first.**
  - *Detune / octave-up / whammy*: granular 2-tap (`f=(β−1)fs/w`), no tracking, low latency.
  - *Clean harmony*: PSOLA/WSOLA gated on tracker confidence (needs `PitchTracker`).
  - *Poly (POG/OC-5-style)*: phase vocoder — deferred/optional, higher latency+CPU.
- **`PitchTracker.h`:** factor MPM/NSDF out of `Tuner.h`, add RT incremental seeded mode +
  clarity gate; shared by tuner, harmony, and (optionally) the divider tightness logic.

Latency: report per block via `latencySamples()` (dividers/env-filter = 0; PSOLA ≈ 1 period;
phase vocoder = FFT+hop). Keep defaults bypassed for byte-exact preset regression.

---

# Sources

**Envelope filters**
- [GEO — Technology of Auto-Wahs / ECFs (Hammer/Keen; Mu-Tron teardown + control-element table)](http://www.geofex.com/article_folders/ecftech/ecftech.htm)
- [Aion FX — Lumitron (Mu-Tron III BOM/schematic + optocoupler history)](https://aionfx.com/project/lumitron-resonant-filter/) · [build PDF](https://aionfx.com/app/files/docs/lumitron_documentation.pdf)
- [diystompboxes — MXR M-120 CMOS switched-resistor SVF (Mark Hammer)](https://www.diystompboxes.com/smfforum/index.php?topic=20811.0) · [Effects Freak M-120](https://effectsfreak.com/effect/m-120-envelope-filter/)
- [mirosol — DOD FX25B (LM13600 datasheet Fig.14 SVF)](https://mirosol.kapsi.fi/2014/06/dod-fx25b-envelope-filter/) · [Effectslayouts FX25](http://effectslayouts.blogspot.com/2015/07/dod-fx25-envelope-filter.html)
- [ESP AN001 — precision rectifiers](http://sound-au.com/appnotes/an001.htm) · [Balmatronics — envelope detector design](https://balmatronics.wordpress.com/2019/11/16/designing-an-envelope-detector-for-bass-and-bass-drum-signals/)
- [Open Music Labs — BA662 OTA](http://wiki.openmusiclabs.com/wiki/BA662) · [Boss AW-3](https://www.boss.info/us/products/aw-3/) · [elektrotanya TW-1 schematic](https://elektrotanya.com/boss_tw-1_touchwah_sch.pdf/download.html)
- [Electric Druid — CEM3320 filter designs (exp CV→cutoff reference)](https://electricdruid.net/cem3320-filter-designs/)

**Analog pitch**
- [toshi.life — DIY Boss OC-2 (operating principle, flip-flops, latency vs OC-3)](https://toshi.life.coocan.jp/review/en_diy_analog_octaver.html)
- [ValveWizard — U-Boat sub-octave (frequency-division / invert-alternate-cycles)](https://www.valvewizard.co.uk/uboat.html)
- [Champion Leccy — Rotund Robot pt.2 (OC-2 schematic dissection)](https://championleccy.com/2018/01/01/project-2-the-rotund-robot-part-2-analysing-schematics/) · [hobby-hour OC-2](https://www.hobby-hour.com/electronics/s/oc2-octave.php)
- [tagboardeffects — MXR Blue Box](https://tagboardeffects.blogspot.com/2013/04/mxr-blue-box.html)
- [Fuzz Central — Tycobrahe Octavia](https://fuzzcentral.ssguitar.com/octavia.php) · [Aion Octahedron](https://aionfx.com/project/octahedron-octave-fuzz/) · [Roger Mayer Octavia](https://www.roger-mayer.co.uk/octavia.htm)
- [GuitarPedalX — Boss OC-5](https://www.guitarpedalx.com/news/gpx-blog/bosss-3rd-generation-oc-5-octave-introduces-state-of-the-art-tracking-and-new-upper-octave-while-expanding-on-the-very-best-of-the-oc-2-and-oc-3) · [Boss OC-3 manual](https://static.roland.com/assets/media/pdf/OC-3_e01_W.pdf)

**Digital pitch pedals**
- [Valhalla — Eventide H910](https://valhalladsp.com/2010/05/07/early-pitch-shifting-the-eventide-h910-harmonizer/) · [H949 / de-glitching (patent US 4,464,784)](https://valhalladsp.com/2010/05/07/pitch-shifting-the-h949-and-de-glitching/)
- [SOS — IVL/Fred Speckeen "Pitch Craft" (Whammy DSP primary source)](https://www.soundonsound.com/people/fred-speckeen-ivl-tech-pitch-craft) · [SOS — Whammy 5th Gen](https://www.soundonsound.com/reviews/digitech-whammy-5th-gen) · [Wikipedia — DigiTech Whammy](https://en.wikipedia.org/wiki/DigiTech_Whammy)
- [EHX — POG2](https://www.ehx.com/products/pog2/) · [POG2 manual](https://www.ehx.com/wp-content/uploads/2021/01/pog-2-manual.pdf) · [KVR — how POG/HOG work](https://www.kvraudio.com/forum/viewtopic.php?t=208046)

**Pitch-shift algorithms**
- [Puckette — Theory & Technique, Pitch Shifting (granular formula)](https://msp.ucsd.edu/techniques/v0.11/book-html/node115.html)
- [Bernsee — Pitch Shifting Using the FT](http://blogs.zynaptiq.com/bernsee/pitch-shifting-using-the-ft/) · [Time/Pitch Overview](http://blogs.zynaptiq.com/bernsee/time-pitch-overview/) · [smbPitchShift.cpp](http://downloads.dspdimension.com/smbPitchShift.cpp)
- [Laroche & Dolson 1999 — phase-vocoder pitch-shift/harmonize (peak-locking)](https://www.ee.columbia.edu/~dpwe/papers/LaroD99-pvoc.pdf)
- [Verhelst & Roelands — WSOLA (1993)](https://www.isca-archive.org/eurospeech_1993/roelands93_eurospeech.html) · [Aalto — PSOLA](https://speechprocessingbook.aalto.fi/Representations/Pitch-Synchoronous_Overlap-Add_PSOLA.html) · [TD-PSOLA code](https://github.com/sannawag/TD-PSOLA)
- [DAFX Book Ch.9 — formant-preserving pitch shift](https://www.dafx.de/DAFX_Book_Page/chapter9.html) · [Dattorro — Effect Design Pt.2](https://ccrma.stanford.edu/~dattorro/EffectDesignPart2.pdf)

**Pitch detection**
- [YIN — de Cheveigné & Kawahara 2002](http://audition.ens.fr/adc/pdf/2002_JASA_YIN.pdf) · [MPM — McLeod & Wyvill 2005](https://www.cs.otago.ac.nz/graphics/Geoff/tartini/papers/A_Smarter_Way_to_Find_Pitch.pdf)
- [Cycfi — Fast and Efficient Pitch Detection (guitar octave errors)](https://www.cycfi.com/2017/10/fast-and-efficient-pitch-detection/) · [sevagh/pitch-detection (C++ MPM & YIN)](https://github.com/sevagh/pitch-detection)
- [pYIN — Mauch & Dixon 2014](https://qmro.qmul.ac.uk/xmlui/bitstream/handle/123456789/6040/MAUCHpYINFundamental2014Accepted.pdf?sequence=2) · [CCRMA — pitch detection review](https://ccrma.stanford.edu/~pdelac/154/m154paper.htm)
