# Pitch pedal equivalents + I/O stages + foolproofing — build plan (2026-07-03)

Grounds the next phase of the octaver: turn the two engines (GRAIN = mono granular character,
POLY = phase-vocoder polyphonic) into recognisable **pedal equivalents**, add faithful **input/
output stages** (impedance loading, coupling caps, output level) the way `EnvFilterBlock` already
does, and add a **foolproof layer** (sane defaults, auto-level, guard rails). Cited, house style;
single-source/inferred/image-only claims flagged. Companion to `PITCH_TRACKING_RESEARCH.md`.

Method: 4-angle fan-out research (EHX POG family; Boss OC family + modern trackers; octave-up/
detune/harmony + fuzz I/O; general pedal I/O-stage DSP modelling), primary sources fetched.

---

## 0. TL;DR — how the pedals map onto our two engines

We already have the hard part (two clean engines + tracker + FFT shifter). The pedal equivalents are
mostly **voicings + presets + an I/O stage**, not new DSP — with two genuinely new pieces (a shared
`IoStage`, and the POG "all voices at once" fuller voice set).

| Pedal (equivalent) | Engine | What it needs beyond today |
|---|---|---|
| **Micro / Nano POG** | POLY | nothing — it *is* Dry + Sub(×½) + Up(×2). Ship as a preset. Buffered I/O. |
| **POG2** | POLY | dry + **4 simultaneous** voices (−2/−1/+1/+2) + resonant LPF (4 Q steps) + Attack swell env + Detune LFO on the up-octaves. |
| **HOG** (later) | POLY | add fifth-family ratios (×1.5, ×3, ×5) — just-intonation, exact ratios. |
| **Boss OC-2 / OC-5 Vintage** | GRAIN | a germanium "grit" character on the sub + Boss I/O front/back-end. |
| **Octavia** (octave fuzz) | (own voice) | fuzz→full-wave-rectify (we had this) + **low-Z fuzz input model** (pickup loading + volume cleanup). Distinct from a clean octave. |
| **MicroPitch** (detune) | granular (Phase 3) | dual ±cents + independent A/B delay + feedback. |
| **Whammy / Pitch Fork / PS-6** | POLY/granular (later) | continuous interval / bend + harmony key. |

---

## A. Input/output stage model — a reusable `IoStage` (front-end + back-end)

Same idea as the env filter's pedal front-end. The guitar is DI'd into the plugin, so the pickup
loading **already happened at capture** — we can only model the *delta* from a reference interface
(the near-universal **~1 MΩ** buffered/amp input), plus the pedal's coupling high-passes and output
level ([GeoFex effects compatibility](http://www.geofex.com/effxfaq/effects_compatibility.htm),
[BOSS buffers](https://articles.boss.info/why-buffers-are-important-for-great-tone/), and the DDSP
tone-stack-as-shelves precedent [arXiv 2408.11405](https://arxiv.org/html/2408.11405v1)).

**Signal chain:** `input HP → input-Z loading shelf → [RF/Miller LP] → (effect) → output HP → output level`

Reusable parameter block (per pedal voicing):
```
inputHpHz        // input coupling HP corner (4-20 Hz)
inputZ_shelfHz   // ~2500 Hz (pickup-resonance region)
inputZ_shelfCutDb// loading delta vs 1 MOhm ref: 0 (buffered) .. -8 (fuzz)
inputZ_levelDb   // slight level drop from loading: 0 .. -2
inputLpHz        // optional RF/Miller high-cut (default off)
outputHpHz       // output coupling HP corner (4-20 Hz)
outputLevelDb    // output make-up / level
```

**Physics anchors (cited):**
- Pickup = inductive source (~1–10 H), output impedance **rises with frequency** — R.G. Keen: 5 H ≈
  **157 kΩ at 5 kHz** ([GeoFex bypass](http://www.geofex.com/article_folders/bypass/bypass.htm),
  arithmetic-verified). Resonant peak ~2–5 kHz; "know the peak + its height and you know ~90% of a
  pickup" ([Lemme](http://www.buildyourguitar.com/resources/lemme/)).
- Loading calibration hard number: **a resistive load of ~47 kΩ or less makes the resonant peak
  vanish**, leaving just HF roll-off ([Lemme](http://www.buildyourguitar.com/resources/lemme/)).
- Coupling cap → 1-pole HP at `f = 1/(2πRC)`. Measured corners: Fuzz Face ~14 Hz, Tube Screamer
  ~8 Hz, Big Muff ~4 Hz, RAT ~7 Hz ([ElectroSmash](https://www.electrosmash.com/fuzz-face)).
- RF/Miller shunt caps are ultrasonic (~100+ kHz) → skip for audio accuracy or model as a gentle
  top-octave tilt; Boss's ~100 pF buffer cap gives the "smooth top" signature
  ([ElectroSmash CE-2](https://www.electrosmash.com/boss-ce-2-analysis)).

**Per-voicing anchors:**
- **Buffered pitch pedals (POG / Boss / Whammy / PS-6 / MicroPitch):** high-Z in (Boss/DigiTech/PS-6
  documented **1 MΩ**; POG family **2 MΩ**; MicroPitch 700 k–990 k), low-Z out (~250 Ω–1 kΩ) →
  **no loading shelf** (shelfCut 0 dB), coupling HP ~7–8 Hz both ends, output flat unity. Boss adds a
  gentle HF smoothing (the 100 pF). *These are near-transparent front-ends — the character is in the
  DSP, not the I/O.*
- **Octavia / fuzz-class:** low, dynamic input Z (~5–10 kΩ, Fuzz-Face-class) → **strong high-shelf
  cut ~−6 dB @2.5 kHz + ~−1 dB level + resonant-peak damp**, input HP ~14 Hz. And the signature
  **volume-cleanup**: model guitar-volume as series source-R so rolling down = less cut + less gain
  (gain ∝ Rfb/source-Z) — a buffer in front kills it, which is authentic
  ([ElectroSmash Fuzz Face](https://www.electrosmash.com/fuzz-face),
  [AMZ buffers-before-fuzz](http://www.muzique.com/news/buffers-before-fuzz/),
  [GeoFex](https://www.geofex.com/effxfaq/distn101.htm)). ⚠️ exact Octavia input-Z is *inferred by
  analogy* to the Fuzz Face, not measured.

**Build:** `src/rig/IoStage.h` (JUCE-free, two Biquad HPs + one high-shelf + one optional LP + gain;
reuse the env-filter front-end pattern). Each pitch voicing owns an `IoStage` with its anchors.
Default (any buffered pitch pedal) = HP ~8 Hz both ends, no shelf, unity out → audibly transparent,
so turning it on doesn't colour the tone unless a fuzz voicing is chosen.

---

## B. Pedal voicings — controls, voicing, I/O (cited)

### POG family (POLY engine) — clean polyphonic octaves
Controls confirmed from EHX manuals; DSP algorithm is closed (inferred FFT/phase-vocoder, one KVR
thread) — which **validates our POLY/SpectralShifter path** ([KVR](https://www.kvraudio.com/forum/viewtopic.php?t=208046),
[POG2 manual](https://www.ehx.com/wp-content/uploads/2020/10/pog2-manual.pdf)).
- **Micro / Nano POG:** Dry, Sub (×½), Octave Up (×2). Polyphonic, glitch-free. I/O **2 MΩ / 250 Ω,
  buffered**; Micro/Nano have a dedicated buffered **Dry Out**. → today's POLY + a preset. *(Nano dry
  out is always unity.)*
- **POG2:** Dry + **−2 / −1 / +1 / +2** (×¼/×½/×2/×4) simultaneously + **2-pole resonant LPF** (cutoff
  slider + **4 discrete Q steps**, no published Hz/Q) + **Attack** (swell env; up = organ pad) +
  **Detune** on the **+1/+2 up-octaves only** (one slider raises depth AND rate together — a chorus
  LFO, cents unpublished → voice by ear). True bypass, 2 MΩ / ~800 Ω. **DSP:** run up to four
  `SpectralShifter`s (skip voices at 0), post-shifter resonant `Svf` LPF, per-voice attack envelope,
  LFO detune on the two up voices. CPU: gate voices on level>0.
- **HOG (later):** 10 voices incl. fifth-family **×1.5, ×3, ×5** (just-intonation, exact ratios) +
  filter freq/resonance + split envelope. Same engine, more ratios.

### Boss OC family (GRAIN engine) — gritty mono sub
- **OC-2:** Direct, Oct 1 (×½), Oct 2 (×¼). Analog divider → **gritty synthy sub, zero latency**,
  mono, glitches on chords by design ([Roland spec](https://support.roland.com/hc/en-us/articles/201967419-OC-2-Specifications),
  [toshi.life](https://toshi.life.coocan.jp/review/en_diy_analog_octaver.html)). → our GRAIN engine +
  a **germanium grit** waveshaper on the sub (the OC-2 half-wave-rectify-and-flip character) to
  distinguish it from the clean POLY sub.
- **OC-5 Vintage vs Poly:** Vintage = OC-2 replica, "more grit and grunt"; Poly = "smoother, less
  pronounced" ([GuitarPedalX](https://www.guitarpedalx.com/news/gpx-blog/bosss-3rd-generation-oc-5-octave-introduces-state-of-the-art-tracking-and-new-upper-octave-while-expanding-on-the-very-best-of-the-oc-2-and-oc-3)).
  → Vintage = GRAIN + grit; Poly = our POLY. The **RANGE / "Lowest"** control (octave only the lowest
  note of a chord) is a cheap, musical POLY add — a pitch-threshold gate on which bins feed the sub.
- **Boss I/O (spec sheets, 4-source):** input **1 MΩ** (real circuit loads ~470 kΩ), output **1 kΩ**,
  nominal **−20 dBu**, buffered bypass, BJT emitter-follower buffers, input coupling ~7 Hz / output
  ~1.6 Hz, gentle 100 pF HF smoothing. *(Correction to earlier notes: the input buffer is a BJT
  emitter-follower, not a JFET; JFETs are the bypass switches.)*

### Octave-up fuzz — Octavia (own character, GRAIN-adjacent)
- **Octave-up = full-wave rectification** of a fuzzed signal (germanium 1N34A, ~0.3 V knee), removes
  the fundamental → 2nd harmonic; "metallic clang" on chords is intrinsic, keep it. Controls =
  Volume + Boost/Octave (octave amount = drive amount). Best on neck pickup, tone rolled off
  ([Fuzz Central](https://fuzzcentral.ssguitar.com/octavia.php),
  [Aion Octahedron](https://aionfx.com/app/files/docs/octahedron_documentation.pdf),
  [geofex](https://www.geofex.com/effxfaq/distn101.htm)). We already built this (the retired Octavia
  path). **Its I/O is the interesting part:** true-bypass, **low-Z fuzz input** → model the loading
  shelf + volume cleanup (see §A). This is the one pitch pedal where the input stage genuinely shapes
  the sound.

### Detune / harmony / whammy (later phases)
- **MicroPitch:** dual detune **Pitch A 0→+50 c / Pitch B 0→−50 c**, each into its **own 0–3 s delay**
  (the A/B offset is the thickener), shared feedback, tilt Tone; buffered ~1 M / 220 Ω
  ([Eventide](https://www.eventideaudio.com/pedals/micropitch/)). → Phase 3 granular detune.
- **Whammy / Pitch Fork / PS-6:** continuous interval / bend / diatonic harmony (key-aware). Buffered
  **1 MΩ / 1 kΩ**. → later; POLY handles the poly ones, granular the mono glide.

---

## C. Foolproof layer

The point Robbie keeps hitting: it should *just work* and not surprise you. Concrete adds:
1. **Sane defaults per voicing (presets).** Ship named presets — "Micro POG", "POG2 Organ",
   "OC-2 Sub", "Octavia" — with level/tone/engine/latency pre-set so a first-time enable sounds right.
2. **Auto-level / no volume jump.** Enabling the octave shouldn't change perceived loudness. Reuse the
   rig's level-match idea: normalise each voicing so Direct≈unity and the octave sits at a musical
   default; optionally an output trim that equal-loudness-compensates the wet mix.
3. **Guard rails (mostly done, make explicit):** GRAIN clarity-gate out on chords/noise (no gargle);
   tracker clamped to the guitar range; POLY PDC reported + dry-delayed; RigChain NaN self-heal already
   catches non-finite. Add: clamp the summed output so stacking Direct+Oct1+Oct2+Up can't clip.
4. **Latency honesty.** Header/tooltip shows the engine's latency (GRAIN 0, POLY ~16 ms) so the choice
   is informed; PDC already re-reported on engine/on-off change.
5. **Buffered-by-default I/O.** Default `IoStage` = transparent (HP ~8 Hz, no shelf) so the block never
   dulls the tone unless a fuzz voicing is chosen.

---

## D. Build order (proposed)

1. **`IoStage.h`** (shared front/back-end) + wire into `PitchBlock` with a per-voicing anchor set;
   default transparent. Offline test: buffered = ~flat; fuzz anchor = measurable HF cut + HP corners.
2. **GRAIN "OC-2 Vintage" grit** on the sub (germanium half-wave character) + Boss I/O anchors.
3. **POG voicing on POLY:** dry + 4 simultaneous voices + resonant `Svf` LPF + Attack env + up-octave
   detune LFO. (The one real DSP addition.)
4. **Octavia voicing** (bring back fuzz→rectify) with the low-Z input model + volume cleanup.
5. **Presets + auto-level foolproofing** (named presets, output trim).
6. **Later:** HOG ratios, MicroPitch detune (Phase 3 granular), Whammy/PS-6 harmony.

Each step: JUCE-free core + offline `pitch_test` checks, default-bypassed, build on Windows + ear.

---

## Sources
**POG:** [POG2 manual](https://www.ehx.com/wp-content/uploads/2020/10/pog2-manual.pdf) ·
[Micro POG](https://www.ehx.com/wp-content/uploads/2021/01/micro-pog-manual.pdf) ·
[Nano POG](https://www.ehx.com/wp-content/uploads/2021/01/nano-pog-manual.pdf) ·
[HOG2](https://www.ehx.com/wp-content/uploads/2020/10/hog2-manual.pdf) ·
[False Electronics teardowns](http://falseelectronics.blogspot.com/2017/05/electro-harmonix-pog2.html) ·
[KVR "how POG works"](https://www.kvraudio.com/forum/viewtopic.php?t=208046)
**Boss / trackers:** [OC-2 spec](https://support.roland.com/hc/en-us/articles/201967419-OC-2-Specifications) ·
[OC-3 manual](https://static.roland.com/assets/media/pdf/OC-3_e01_W.pdf) ·
[OC-5](https://www.boss.info/global/products/oc-5/) ·
[GuitarPedalX OC-5](https://www.guitarpedalx.com/news/gpx-blog/bosss-3rd-generation-oc-5-octave-introduces-state-of-the-art-tracking-and-new-upper-octave-while-expanding-on-the-very-best-of-the-oc-2-and-oc-3) ·
[MOOER Pure Octave](https://www.mooeraudio.com/product/Pure-Octave--129.html) ·
[Aguilar Octamizer manual](https://cdn.shopify.com/s/files/1/0735/6835/4580/files/aguilar-octamizer-analog-octave-bass-pedal-owner_s-manual.pdf)
**Octave-up / detune / harmony:** [Fuzz Central Octavia](https://fuzzcentral.ssguitar.com/octavia.php) ·
[Aion Octahedron](https://aionfx.com/app/files/docs/octahedron_documentation.pdf) ·
[Eventide MicroPitch](https://www.eventideaudio.com/pedals/micropitch/) ·
[DigiTech Whammy manual](https://digitech.com/wp-content/uploads/2022/09/Whammy_OM_EN.pdf) ·
[EHX Pitch Fork](https://www.ehx.com/products/pitch-fork/) ·
[Boss PS-6 spec](https://www.boss.info/us/products/ps-6/specifications/)
**I/O stages:** [ElectroSmash Fuzz Face](https://www.electrosmash.com/fuzz-face) ·
[ElectroSmash Tube Screamer](https://www.electrosmash.com/tube-screamer-analysis) ·
[ElectroSmash RAT](https://www.electrosmash.com/proco-rat) ·
[GeoFex bypass / impedance](http://www.geofex.com/article_folders/bypass/bypass.htm) ·
[GeoFex effects compatibility](http://www.geofex.com/effxfaq/effects_compatibility.htm) ·
[Lemme — pickup secrets](http://www.buildyourguitar.com/resources/lemme/) ·
[AMZ buffers before fuzz](http://www.muzique.com/news/buffers-before-fuzz/) ·
[BOSS buffers](https://articles.boss.info/why-buffers-are-important-for-great-tone/)
