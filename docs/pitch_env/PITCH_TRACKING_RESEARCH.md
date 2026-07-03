# Octaver tracking + pitch-detection — deep research (2026-07-03)

Grounding research for a **from-scratch rebuild of the octave-down (and up)** after play-test #1
came back "gargly / buzzy / clicky, pitch tracking doesn't work." Same rigor and citation style as
`RESEARCH.md`: primary/teardown sources inline as [title](url), corroboration level noted, disputed
or image-only claims flagged. Companion to `RESEARCH.md` (§B0–B3 first pass) and `BUILD_PLAN.md`.

Method: 5 parallel fan-out research passes (OC-2 circuit, OC-3/OC-5 poly, pitch-detection algorithms,
real-time constraints, tracked-octave synthesis), sources fetched and cross-checked, single-source /
image-only claims flagged explicitly.

---

## 0. TL;DR + the decision

**Why the first cut gargled** — three named, literature-backed mechanisms (all of which our
comparator-only divider hit at once):
1. **Mistracked / phase-misaligned period** — one spurious comparator edge (a harmonic crossing the
   threshold, or a noisy attack) double-triggers the flip-flop → the sub-octave jumps an octave or
   sign-flips mid-waveform = click. This was our headline bug.
2. **Crossfade / splice cancellation** — phase-mismatched splices comb-filter into a "stutter"
   ([Valhalla H949](https://valhalladsp.com/2010/05/07/pitch-shifting-the-h949-and-de-glitching/)).
3. **Constant-power crossfade law failing on correlated input** — the granular sin²+cos² law only
   holds for *uncorrelated* taps; a sustained tonal guitar note makes the taps correlated → beating/
   tremolo/warble ([Puckette §Pitch shifting](http://msp.ucsd.edu/techniques/v0.11/book-html/node115.html)).

**The core finding:** the Boss OC-2's clean, near-zero-latency tracking is NOT a comparator alone —
it is a **peak-hold-*referenced* comparator** (the switching threshold floats just below the audio
peaks and decays with the note) feeding a toggle divider, and the sub-octave is a **half-wave-
rectified copy of the real note whose gain sign is flipped every other cycle** (not the bare square).
That peak-referenced threshold is exactly the "AGC" that keeps a stable square as the note decays,
and it is *"very difficult to build… down to small signal levels"* — which is why the OC-2 is prized
([ValveWizard U-Boat](https://www.valvewizard.co.uk/uboat.html), [toshi.life](https://toshi.life.coocan.jp/review/en_diy_analog_octaver.html)).

**Recommendation for NAM Rig (2000s rock + indie/acoustic, latency-sensitive):** a **tiered/hybrid
octaver mirroring the OC-5's own answer**, not one algorithm:

- **Octave-DOWN, default = a hardened analog divider *debounced by a lightweight pitch tracker*.**
  Reuse the MPM/NSDF tracker already in `Tuner.h` to supply an expected period `T0` + clarity, and
  **gate every comparator edge to a tolerance window around the expected next zero-crossing** (reject
  early harmonic double-triggers); crossfade the octave out when clarity drops (chord/mute). Keep the
  OC-2 synthesis (peak-referenced comparator → toggle → **half-wave-rectify × ±sign**, output LPF,
  envelope follower). This keeps the ~zero-latency, gritty OC-2 identity on single notes and directly
  kills the warble with the tracker — **Option (i) in the brief, and the right first move.**
- **Octave-UP + a "clean" octave-down voice = PSOLA/WSOLA driven by the same tracked period.**
  Formant-preserved for free, ~1-period + window latency — the indie/acoustic clean-octave voice.
- **Poly / chord octaves (POG mode) = integer-ratio phase vocoder with identity phase-locking.**
  The only glitch-free-on-chords path; ×2/×0.5 stays at 50 % overlap (cheap) and dodges fractional-
  bin sidebands. **Higher latency/CPU → keep it as the later Phase-4 poly engine, not now.**

Rationale: this fixes the reported problem at its root (tracker-debounced division), preserves the
zero-latency analog voice players expect from an octaver, and reuses code we already have (MPM).
Full re-synthesis for *every* mode (Option ii) buys robustness we don't need on single-note down-
octave and costs latency we don't want.

---

## A. Boss OC-2 — circuit-level operating principle (what to actually model)

Sources: [hobby-hour OC-2 schematic + service-manual BOM](https://www.hobby-hour.com/electronics/s/oc2-octave.php),
[toshi.life DIY OC-2 (scope traces)](https://toshi.life.coocan.jp/review/en_diy_analog_octaver.html),
[ValveWizard U-Boat (OC-2 method, deliberately cloned)](https://www.valvewizard.co.uk/uboat.html),
[Champion Leccy "Rotund Robot" schematic analysis](https://championleccy.com/2018/01/01/project-2-the-rotund-robot-part-2-analysing-schematics/).

**Signal flow (2+ sources).** FET input buffer (1 MΩ) → split: a clean **DIRECT** path straight to
the output mixer (the "Direct Level" knob), and a detection/synthesis path.

**Detection LPF (qualitatively 2+ sources; exact numbers single-source/image-only).** A **2nd-order
(~12 dB/oct) low-pass ~1 kHz with ~×4.5 (≈13 dB) gain** turns the note into a big near-sine before
squaring — ValveWizard's stated OC-2-equivalent stage: *"make the small wiggly guitar signal as much
like a big, pure sinewave as possible"* ([ValveWizard](https://www.valvewizard.co.uk/uboat.html)).
The *qualitative* claim (aggressive LPF + gain before the comparator; roll off treble / use the neck
pickup for truer tracking) is corroborated by Champion Leccy and toshi. **The exact 1 kHz / ×4.5
are ValveWizard's clone target, not a measured original-OC-2 value — the real RC values are image-
only in the schematic.**

**The tracking secret — peak-hold-*referenced* comparator (2+ sources, THE key detail).** After the
LPF, a **peak detector produces a DC reference that floats just below the audio peaks**; a comparator
fires on each peak that exceeds the (decaying) reference, and a **balanced in-phase + inverted
zero-crossing detector into an SR latch** locks the square exactly in phase with the audio and
rejects spurious crossings ([ValveWizard](https://www.valvewizard.co.uk/uboat.html)). toshi confirms
*"a stable pitch waveform (square wave) can be obtained by applying a peak hold circuit"*
([toshi.life](https://toshi.life.coocan.jp/review/en_diy_analog_octaver.html)). Because the threshold
*scales with the shrinking signal*, the square stays clean and correctly-timed as the note decays —
this is the implicit "AGC," and comparator op-amp speed matters (LM833/NE5532 track far better than
TL072; a true fast comparator glitches). **This peak-referenced, phase-locked switching is the single
most emulation-relevant detail — our first cut used a fixed-fraction threshold with no phase lock.**

**Dividers (2+ sources).** The pitch square clocks a **cascade of toggle flip-flops**: ÷2 → OCT1
(one octave down); that stage clocks a second toggle → ÷4 → OCT2 (two down). Original uses the ROHM
**BA634** (T-type, obsolete) + **µPD4013C**; DIY clones substitute **CD4027 (JK)**
([hobby-hour BOM](https://www.hobby-hour.com/electronics/s/oc2-octave.php), [toshi.life](https://toshi.life.coocan.jp/review/en_diy_analog_octaver.html)).
⚠️ **DISPUTED/image-only:** the exact ÷2-vs-÷4 IC-to-octave mapping isn't resolvable from text — it
needs the schematic image. (Doesn't affect our DSP: two cascaded toggles regardless.)

**The synthesis trick — CONFIRMED (2+ sources incl. scope traces).** The OC-2 does **not** output the
bare divider square. It **half-wave rectifies the original tone through SILICON diodes** (D10/D11,
1S1588-class — CORRECTED 2026-07-03: the OC-2 is silicon; germanium 1N34A is the *Octavia*, not the OC-2)
1S-188FM) and, driven by the divider, **inverts (flips the op-amp gain sign) every other cycle**,
stitching a slice of the *real waveform* into a wave of twice the wavelength = one octave down, so the
sub-octave inherits the note's timbre and dynamics ([toshi.life, with oscilloscope traces](https://toshi.life.coocan.jp/review/en_diy_analog_octaver.html):
*"the half-wave rectified tone is inverted every other wave and converted into a signal with twice the
wavelength"*; [ValveWizard](https://www.valvewizard.co.uk/uboat.html): a JFET switch *"alternately
selects either the in-phase or out-of-phase audio… chops up the audio and stitches it together"*).
**Contrast — MXR Blue Box outputs the raw square** → flat dynamics, sputtery decay, bad tracking (a
−2-oct fuzz). ValveWizard frames the taxonomy: *"Boss method (invert on the peak of the audio) =
smoothest"* vs zero-crossing/raw-square = more harmonics, easier to build.

> **Modelling correction vs our current block:** we multiply the *full band-limited carrier* × ±sign.
> The OC-2 multiplies a **half-wave-rectified** copy × ±sign through **silicon** (harder-knee,
> slightly asymmetric) rectifier. Half-wave-rectify-then-sign-flip is what gives the characteristic
> growl and is worth adopting. Our zero-crossing phase-alignment (declick) is correct and matches the
> OC-2's phase-locked switching in spirit; the missing piece is the *peak-referenced* threshold and
> the half-wave rectified carrier.

**Output stage (2+ sources).** An **output LPF** tames the chop/switching hash; an **envelope
follower** makes the octave track the dry note's dynamics and **gate on silence**
([ValveWizard](https://www.valvewizard.co.uk/uboat.html), [Champion Leccy](https://championleccy.com/2018/01/01/project-2-the-rotund-robot-part-2-analysing-schematics/)).
⚠️ **DISPUTED:** which op-amp block is the envelope follower vs the tracking filter (Champion Leccy vs
another modder) — unresolved without the schematic; irrelevant to our model.

**Latency (single strong source + physics).** Worst case **one half-cycle** of the note; effectively
zero added delay — the sub-octave is generated *at the waveform level, simultaneously*, not shifted-
then-delayed ([toshi.life](https://toshi.life.coocan.jp/review/en_diy_analog_octaver.html)). **So an
authentic OC-2 emulation must stay sample-by-sample — do NOT bolt an FFT/lookahead pitch stage onto
the down-octave.**

---

## B. OC-3 / OC-5 — why "poly" tracks chords but adds latency

Sources: [Boss OC-3 manual](https://static.roland.com/assets/media/pdf/OC-3_e01_W.pdf),
[GuitarPedalX OC-5](https://www.guitarpedalx.com/news/gpx-blog/bosss-3rd-generation-oc-5-octave-introduces-state-of-the-art-tracking-and-new-upper-octave-while-expanding-on-the-very-best-of-the-oc-2-and-oc-3),
[BOSS OC-5](https://www.boss.info/us/products/oc-5/),
[TalkBass latency-measurement thread](https://www.talkbass.com/threads/digital-octaver-pitch-shifter-latency-measurements-thread.1421455/).

- **OC-3 (2003)** = first compact **polyphonic** octaver, DSP; three modes: OC-2-style **mono (OCT)**,
  **Drive**, **Poly**. The manual: *"a monophonic-input effects processor… Except when set to POLY"*
  and *"take care not to play chords with monophonic input"* — i.e. the mono modes are the classic
  frequency-division path; only Poly is a separate DSP engine ([OC-3 manual](https://static.roland.com/assets/media/pdf/OC-3_e01_W.pdf)).
  A single flip-flop cannot resolve multiple pitches, so **poly = DSP pitch analysis + transpose, not
  ÷2 division** (inference; no first-party algorithm published).
- **Latency, measured (2+ sources):** analog OC-2 / analog MXR octave = **no detectable latency**;
  genuinely digital pitch pedals measured **~22 ms flat** ([TalkBass](https://www.talkbass.com/threads/digital-octaver-pitch-shifter-latency-measurements-thread.1421455/)).
  toshi (owns both): the OC-3's octave *"was heard separately… there is a delay in the octave sound."*
- **The ≥1-cycle floor:** any period-based estimator must observe ≥1 full waveform cycle before it can
  transpose → *"a low E… one cycle is already 20 ms"* (bass); guitar low E ≈ 12 ms/cycle. Analog
  division sidesteps it because it never *detects* pitch.
- **RANGE / "Lowest" control (2+ sources):** a movable low-frequency crossover on the octave path —
  at the extreme, *"only the very lowest note… triggers a sub-octave while the other notes pass
  through unaffected."* Cheap and musically strong; **worth stealing** (a lowest-fundamental picker).
- **OC-5 (2020, fully digital):** new tracking engine ("no latency" = marketing; a review puts octave-
  *down* ~1 ms, octave-*up* ~10 ms, consistent with the cycle floor), adds +1 upper octave (3-octave
  span), refined Range, and a **Vintage mode = digital OC-2 emulation**. GuitarPedalX notes the
  timbre tell: *Vintage "a little more grit and grunt," Poly "noticeably smoother and slightly less
  pronounced"* — the synthetic-vs-organic tradeoff. BOSS **rejected an all-analog Waza OC-2 reissue**
  because it would give *"less accurate tracking and more inconsistent octave playback."*

**Takeaway:** offer BOTH like the OC-5 — a fast mono division-style sub-octave for the immediate
aggressive low-latency feel, and a separate poly path for chords accepting its ~1-cycle latency.

---

## C. Pitch detection — MPM/NSDF is the tracker; why guitar is octave-error-prone

Sources: [McLeod & Wyvill 2005 (MPM/NSDF)](http://dl.icdst.org/pdfs/files4/b56e1f975f0b9b3fca904fb2a7778c15.pdf),
[de Cheveigné & Kawahara 2002 (YIN)](http://audition.ens.fr/adc/pdf/2002_JASA_YIN.pdf),
[Mauch & Dixon 2014 (pYIN)](https://www.eecs.qmul.ac.uk/~simond/pub/2014/MauchDixon-PYIN-ICASSP2014.pdf),
[Cycfi — Bliss! / Revisited](https://www.cycfi.com/2018/04/fast-and-efficient-pitch-detection-bliss/),
[sevagh/pitch-detection (C++ MPM/YIN)](https://github.com/sevagh/pitch-detection).

**Why guitar mis-tracks.** The plucked string's **fundamental is often weaker than its 2nd/3rd
harmonics** (the pickup differentiates), and distortion multiplies partials → many near-equal
candidate periods; the estimator's whole job is *which* peak/dip to pick. Cycfi's measured plucked-D
spectrum: *"the 2nd and 3rd harmonics overpower the fundamental… can easily generate multiple peaks
per cycle."* The octave-up trap: the 2nd-harmonic dip deepens until it nearly equals the
fundamental's ([Cycfi Bliss!](https://www.cycfi.com/2018/04/fast-and-efficient-pitch-detection-bliss/)).
Concrete failure: naive autocorrelation on a clean open-E (82.41 Hz) returns **282 Hz** — *"just
wrong"* ([sevagh McLeod README](https://github.com/sevagh/pitch-detection/blob/master/misc/mcleod/README.md)).

**The estimator ladder (YIN Table I gross-error, 25 ms window):** ACF **10.0 %** → squared-difference
function **1.95 %** → CMNDF **1.69 %** → absolute-threshold-0.1-pick-smallest-τ **0.78 %** → parabolic
interp **0.77 %** → best-local ±20 % re-search **0.50 %** ([YIN §II, Table I](http://audition.ens.fr/adc/pdf/2002_JASA_YIN.pdf)).
The single biggest step is **ACF → difference function** (amplitude-insensitive; kills the growing-
peak "too-low" error). YIN's `d'(T)` doubles as a confidence (aperiodic/total power ratio).

**MPM / NSDF — the recommendation for guitar.** NSDF `n'(τ) = 2·r'(τ)/m'(τ)` is **bounded to
[−1,+1] for any amplitude** (m'(τ) = running energy) — so a decaying pluck or a hot distorted signal
doesn't bias peak ranking. Octave-robustness comes from **key-maximum picking**: take the highest
maximum between each positive- and negative-sloped zero crossing (ignore lag 0), parabolically
refine, set threshold `k · n_max`, and **take the FIRST key maximum above it** — resisting octave-UP
error (a strong 2nd harmonic makes a near-equal peak at *half* the period; the first-above-threshold
rule keeps you on the fundamental). **`k ≈ 0.8–1.0`** (sevagh ships `MPM_CUTOFF 0.93`). **Clarity =
the NSDF value at the chosen peak** = free, amplitude-independent confidence. Works from **~2
periods**, **no LPF prefilter** (right for distorted/harmonic-rich tone), cent-accurate, real-time —
*"it is literally the guitar-tuner algorithm"* (Otago Tartini) ([McLeod §4–8](http://dl.icdst.org/pdfs/files4/b56e1f975f0b9b3fca904fb2a7778c15.pdf)).

> **Verification note:** the earlier `RESEARCH.md` phrasing conflated MPM's *key-maximum
> identification* ("highest between the zero crossings") with the *threshold pick* ("first key max
> above `k·n_max`"). They are distinct sub-steps — both required. Also, on sevagh's degraded-audio
> benchmark MPM ≈ YIN (not a blowout); the real argument for MPM on guitar is *no-LPF + amplitude-
> bounded NSDF + octave-up-resistant first-key-max + it's the tuner algorithm*, not a lower headline
> error rate. **`Tuner.h` already implements MPM/NSDF — reuse it.**

**For fewer cross-frame octave flips:** layer **pYIN**-style probabilistic smoothing (Beta prior over
thresholds → candidate (f, p) pairs → Viterbi/HMM, ≤2.5 semitones/frame) — octave-error 0.5–1.7 %
([pYIN](https://www.eecs.qmul.ac.uk/~simond/pub/2014/MauchDixon-PYIN-ICASSP2014.pdf)); but Viterbi is
non-causal → **benchmark only, not the live path**. **Cycfi BACF** (1-bit zero-cross stream, XOR +
popcount) is an ultra-cheap alternative (~50 ns/sample) whose known failure is octave jumps on
evolving harmonics/hard onsets — hence its Bias/median stage (below).

---

## D. Real-time recipe (latency budget + the octave-error killers)

Sources: [Derrien DAFx-14 (very-low-latency guitar→MIDI)](https://www.dafx14.fau.de/papers/dafx14_olivier_derrien_a_very_low_latency_pitch_.pdf),
[Pardue et al. NIME 2014 (low-latency, sensor-assisted)](https://www.eecs.qmul.ac.uk/~andrewm/pardue_nime2014.pdf),
[Cycfi Revisited](https://www.cycfi.com/2020/07/fast-and-efficient-pitch-detection-revisited/),
[JUCE forum: lowest-latency pitch detection](https://forum.juce.com/t/lowest-latency-real-time-pitch-detection/51741).

1. **Latency floor is physics.** Low E ≈ 82.41 Hz → 1 period ≈ 12.1 ms; a reliable estimate needs
   **~2 periods ≈ 25 ms** ([Derrien](https://www.dafx14.fau.de/papers/dafx14_olivier_derrien_a_very_low_latency_pitch_.pdf),
   [Cycfi](https://www.cycfi.com/2020/07/fast-and-efficient-pitch-detection-revisited/)). Short window
   = jittery/octave-prone (Pardue on real signal: 2048→4.8 %, 512→21.1 %, 256→41.1 % error); long =
   stable but sluggish. **Much cheaper on high strings** (short period). Musician latency perception
   ~20–30 ms, interactive target <10 ms — so ~20–25 ms on low strings is genuinely good.
2. **Process per-hop**, not per-sample/per-buffer: hop **256 samples (~5.8 ms), 75 % overlap**.
3. **Seed the lag search ±1 semitone around the previous estimate** — *the biggest single win*, for
   CPU **and** octave-error: Pardue's restricted search *"nearly eliminates harmonic errors,"* +25–48 %
   accuracy at 256-sample windows; full-range search only on onset / confidence-drop
   ([Pardue](https://www.eecs.qmul.ac.uk/~andrewm/pardue_nime2014.pdf)).
4. **Decimate to ~11–12 kHz** for the correlation stage (guitar tops out ~1 kHz + a few harmonics) —
   ~3.7× cheaper, no resolution loss ([Derrien](https://www.dafx14.fau.de/papers/dafx14_olivier_derrien_a_very_low_latency_pitch_.pdf)).
5. **Variable window** (short/high, long/low); when the window is too short for the fundamental,
   **search around the 2nd harmonic** instead → sub-period latency on high strings ([Pardue](https://www.eecs.qmul.ac.uk/~andrewm/pardue_nime2014.pdf)).
6. **Gate + smooth + reject octave jumps:** clarity/periodicity gate + RMS gate (< ~−48 dB = silence);
   **3-point median** + hysteresis; **reject low-confidence exact ×2/×0.5 jumps** by preferring the
   stable prior when the new value is a harmonic within a few cents (Cycfi's "Bias" stage). Encode the
   perceptual asymmetry: wrong-high→correct-low is *"barely perceptible,"* wrong-low→correct-high is
   *"more pronounced"* → bias uncertain estimates slightly **high** ([Cycfi Revisited](https://www.cycfi.com/2020/07/fast-and-efficient-pitch-detection-revisited/)).
7. **Benchmark** offline vs pYIN/CREPE; keep them out of the live path.

---

## E. Synthesising a clean octave — methods, latency, and what causes gargle

Sources: [Puckette §Pitch shifting](http://msp.ucsd.edu/techniques/v0.11/book-html/node115.html),
[Laroche & Dolson 1999 (peak-locking PV)](https://www.ee.columbia.edu/~dpwe/papers/LaroD99-pvoc.pdf),
[Bernsee (PV pitch shift)](http://blogs.zynaptiq.com/bernsee/pitch-shifting-using-the-ft/),
[Valhalla H949 de-glitching](https://valhalladsp.com/2010/05/07/pitch-shifting-the-h949-and-de-glitching/),
[Verhelst & Roelands WSOLA 1993](https://www.semanticscholar.org/paper/An-overlap-add-technique-based-on-waveform-(WSOLA)-Verhelst-Roelands/d94abd77e52a56c425e4b86e6c7d692583ea406d),
[Parviainen — time/pitch scaling](https://www.surina.net/article/time-and-pitch-scaling.html),
[DAFX Ch.9 formant preservation](https://www.dafx.de/DAFX_Book_Page/chapter9.html).

| Method | Latency | CPU | Mono/Poly | Gargle cause it hits |
|---|---|---|---|---|
| Granular 2-tap delay-line | lowest (~w, 30–100 ms) | lowest | both | constant-power law fails on tonal input → warble |
| **Divider debounced by tracker** | **~0 (single notes)** | **lowest** | mono | mistrack — *fixed* by tracker gate |
| PSOLA / WSOLA | ~1 period + window | low | **mono** | mislocated pitch mark → click |
| Integer-ratio PV (POG) | highest (~100 ms) | highest | **poly** | (glitch-free; slight phasiness) |

- **Granular (Puckette):** `f = (β−1)·fs/w`, two taps a half-window apart, half-sine windows, 4-point
  cubic interp; `w ≈ 30–100 ms`. **The warble is structural**: the constant-power law only holds for
  *uncorrelated* taps — a held guitar note makes them correlated, so power isn't flat → beating. Right
  for *whammy/detune* (Phase 3), wrong for a clean octave.
- **Divider validated by the tracked period (our pick for down-octave):** only accept a comparator
  edge inside a tolerance window around the *expected* next zero-crossing (≈ one tracked period `T0`
  after the last accepted edge); reject early edges (harmonic double-triggers); crossfade out on low
  clarity. This is the time-domain analogue of the Eventide H949 de-glitcher, which *"compares… true
  phase similarities… periodic signals have a high degree of autocorrelation, so the de-glitching
  hardware can find excellent splicing points"* ([Valhalla](https://valhalladsp.com/2010/05/07/pitch-shifting-the-h949-and-de-glitching/))
  — we substitute the MPM period for the autocorrelation search. **~zero latency, keeps the OC-2 voice.**
- **PSOLA / WSOLA (clean mono octave):** window ~2-period grains at pitch marks, overlap-add at new
  spacing `T0/β` → **formants preserved for free**; ~1 period + window latency, low CPU; **mono only**;
  one mislocated mark = a click. WSOLA removes explicit marks via a waveform-similarity search. Use
  the MPM period to place marks / size the search — biggest quality lever.
- **Integer-ratio phase vocoder (POG, later):** ×2/×0.5 = integer-bin shift → *"merely copying STFT
  values… at 50 % overlap Δω is a multiple of π, making the phase update trivial"* — cheap and no
  arctan; **identity phase-locking** (rotate a peak's region of influence by one angle) *"dramatically
  minimizes phasiness"* ([Laroche & Dolson](https://www.ee.columbia.edu/~dpwe/papers/LaroD99-pvoc.pdf)).
  Glitch-free on chords, but ~100 ms latency + high CPU. Fractional (arbitrary-interval) shifts need
  75 % overlap or you get −21 dB sidebands → that's why a POG (fixed octaves) is cleaner/cheaper than
  a Whammy.
- **Formant preservation** (avoid chipmunk/thin up-octave, boxy down-octave): source-filter decouple
  (cepstral or LPC envelope), shift the source, **re-impose the original envelope** — `synMag =
  envelope[target]·(anaMag/envelope[source])`. **PSOLA gets it for free**; a bare Bernsee PV does not.
  Matters most on the up-octave.

---

## F. Build plan for the octaver rework

**Phase 2b (now) — harden the octave-down with the tracker (Option i).**
1. Factor `src/rig/PitchTracker.h` out of `Tuner.h`: MPM/NSDF, **incremental per-hop** (hop 256, ~2-
   period window), **decimate to ~12 kHz**, **seed ±1 semitone** from the previous estimate, full-
   range only on onset/clarity-drop, output `{ periodSamples, clarity }`. Reuse the existing MPM peak-
   pick + parabolic refine; add the seeded-search + median/octave-jump-reject smoothing.
2. Rework `PitchBlock` OCT_DOWN: keep the zero-crossing-aligned sign-flip synthesis (declick already
   proven), but (a) make the comparator threshold **peak-referenced** (float just below the held peak,
   decaying) like the OC-2, (b) **debounce edges against the tracked `T0`** (reject edges arriving <
   ~0.6·T0 after the last), (c) **crossfade the octave out when clarity < gate** (kills chord/mute
   gargle), (d) switch the carrier to a **half-wave-rectified** (slightly asymmetric — silicon)
   copy × ±sign for the authentic OC-2 growl, (e) keep the output LPF + envelope follower.
   Latency stays ~0 (the tracker only *validates* edges; it doesn't delay the audio path).
3. Offline tests: divider holds `f/2` on strong-2nd/3rd-harmonic tones AND on a pitch glide; no
   octave jump across a note change; declick still ~0 HF hash; clarity gate mutes chords; finite/
   bounded; latency 0.

**Phase 2c (optional, clean voice) — PSOLA octave** driven by the same `PitchTracker` for a smooth
octave-up + a "clean" octave-down alt (indie/acoustic). ~1-period latency, formants free.

**Phase 4 (later) — POG-style integer-ratio phase vocoder** for polyphonic/chord octaves. Deferred
(latency/CPU); it was already the Phase-4 poly engine in `BUILD_PLAN.md`.

**Don't:** put an FFT/lookahead stage on the down-octave (breaks the OC-2 zero-latency identity); use
the granular engine for a sustained octave (structural warble — save it for whammy/detune).

---

## Sources
**OC-2 / OC-3 / OC-5:** [hobby-hour OC-2](https://www.hobby-hour.com/electronics/s/oc2-octave.php) ·
[toshi.life DIY OC-2](https://toshi.life.coocan.jp/review/en_diy_analog_octaver.html) ·
[ValveWizard U-Boat](https://www.valvewizard.co.uk/uboat.html) ·
[Champion Leccy Rotund Robot](https://championleccy.com/2018/01/01/project-2-the-rotund-robot-part-2-analysing-schematics/) ·
[Boss OC-3 manual](https://static.roland.com/assets/media/pdf/OC-3_e01_W.pdf) ·
[GuitarPedalX OC-5](https://www.guitarpedalx.com/news/gpx-blog/bosss-3rd-generation-oc-5-octave-introduces-state-of-the-art-tracking-and-new-upper-octave-while-expanding-on-the-very-best-of-the-oc-2-and-oc-3) ·
[BOSS OC-5](https://www.boss.info/us/products/oc-5/) ·
[TalkBass latency thread](https://www.talkbass.com/threads/digital-octaver-pitch-shifter-latency-measurements-thread.1421455/)

**Pitch detection:** [McLeod & Wyvill 2005 (MPM)](http://dl.icdst.org/pdfs/files4/b56e1f975f0b9b3fca904fb2a7778c15.pdf) ·
[YIN 2002](http://audition.ens.fr/adc/pdf/2002_JASA_YIN.pdf) ·
[pYIN 2014](https://www.eecs.qmul.ac.uk/~simond/pub/2014/MauchDixon-PYIN-ICASSP2014.pdf) ·
[Cycfi Bliss!](https://www.cycfi.com/2018/04/fast-and-efficient-pitch-detection-bliss/) ·
[Cycfi Revisited](https://www.cycfi.com/2020/07/fast-and-efficient-pitch-detection-revisited/) ·
[sevagh/pitch-detection](https://github.com/sevagh/pitch-detection) ·
[Derrien DAFx-14](https://www.dafx14.fau.de/papers/dafx14_olivier_derrien_a_very_low_latency_pitch_.pdf) ·
[Pardue NIME 2014](https://www.eecs.qmul.ac.uk/~andrewm/pardue_nime2014.pdf) ·
[JUCE forum thread](https://forum.juce.com/t/lowest-latency-real-time-pitch-detection/51741)

**Synthesis:** [Puckette T&T](http://msp.ucsd.edu/techniques/v0.11/book-html/node115.html) ·
[Laroche & Dolson 1999](https://www.ee.columbia.edu/~dpwe/papers/LaroD99-pvoc.pdf) ·
[Bernsee](http://blogs.zynaptiq.com/bernsee/pitch-shifting-using-the-ft/) ·
[Valhalla H949](https://valhalladsp.com/2010/05/07/pitch-shifting-the-h949-and-de-glitching/) ·
[Verhelst & Roelands WSOLA](https://www.semanticscholar.org/paper/An-overlap-add-technique-based-on-waveform-(WSOLA)-Verhelst-Roelands/d94abd77e52a56c425e4b86e6c7d692583ea406d) ·
[Parviainen time/pitch scaling](https://www.surina.net/article/time-and-pitch-scaling.html) ·
[DAFX Ch.9](https://www.dafx.de/DAFX_Book_Page/chapter9.html) ·
[Grondin Guitar Pitch Shifter](https://www.guitarpitchshifter.com/algorithm.html)

---

## G. OC-2 DIVIDER REBUILD — research-first, DEFINITIVE comparator mechanism (2026-07-03)

Prior divider builds all failed the same way (gurgle / double-clock / gargle) because every
one used a **single fixed-fraction-threshold comparator** and then patched the symptoms (adaptive
LPF, MPM debounce, makeup gain). Root cause, now confirmed against the two primary sources with
the actual circuit described in full:

**The OC-2 does NOT use one comparator. It uses a peak-referenced SR latch (a Schmitt in effect),
then a CD4013 D-flip-flop chain.** From ValveWizard's U-Boat writeup (a deliberate OC-2-method
clone, full prose description) + toshi.life (OC-2 MOD, scope traces):

Detection path (both sources agree):
1. **Buffer → 2nd-order LPF ~1 kHz with ~×4.5 (13 dB) gain.** Purpose stated verbatim: *"make the
   small wiggly guitar signal as much like a big, pure sinewave as possible"* — kill the harmonics
   that make a comparator switch at the wrong time. Aggressive treble roll-off = better tracking;
   this is why the OC-2 tracks low notes/neck pickup and falls apart high/bright (authentic).
2. **Balanced split** (signal + its inverse) into a **low-slew-rate op-amp comparator** (NOT a real
   fast comparator — ValveWizard: *"a real comparator is not suitable as it will switch too fast and
   cause many glitches"*; LM833/NE5532 beat TL072). Balanced feed = 2× zero-cross resolution.
3. **Peak level detector** → a DC reference that *floats just below the audio peaks and decays with
   the note* = the implicit AGC ("stable square as the note decays"). A second comparator fires when
   an audio peak rises above that reference.
4. **CD4013 SR latch**: the zero-crossing **SETs**, the peak **RESETs**. Output = a square exactly
   **phase-locked** to the audio, one clean edge per cycle, immune to harmonic ripple (ripple near
   zero can't RESET because it never reaches the peak reference; extra SETs are idempotent). The DPDT
   "synth" mode just swaps set/reset order → 90° phase.
5. **CD4013 D-flip-flop ÷2 → OCT1**; a second toggle **÷4 → OCT2** (ROHM BA634 + µPD4013C originally;
   clones use CD4027 JK). Divider clocks on the **low→high edge** of the pitch square.

Synthesis (toshi scope traces, CONFIRMED): the divider drives a JFET switch that **alternately
selects the in-phase vs inverted audio every other cycle** — the OC-2 rectifies the tone (toshi says
germanium; the OC-2 service-manual BOM is silicon 1S1588 — SOURCE CONFLICT, hold loosely; DSP knee is
near-identical either way) and **inverts every other wave → a wave of twice the wavelength that
inherits the note's timbre and dynamics.** ValveWizard's taxonomy: **invert on the audio PEAK = Boss
method = smoothest**; invert on the **zero-crossing = U-Boat method = more harmonics, easier to build,
click-free by construction** (d = 0 at the flip). Contrast MXR Blue Box = raw divider square = flat/
sputtery (a −2-oct fuzz), which is what our earlier "carrier × sign" cuts effectively were.

**Latency: ~one half-cycle, effectively zero — the sub is generated at the waveform level,
simultaneously. Do NOT bolt an FFT/tracker onto the down-octave** (both sources; it's the whole point
of the OC-2 vs the OC-3/OC-5's ~10-20 ms).

### LOCKED DSP SPEC (faithful, zero-latency, no tracker, no adaptive filter)

- **Detection near-sine `d`** = input → cascaded 2-pole LP ×2 (≈4-pole) @ ~1 kHz, unity. (4-pole
  instead of the circuit's 2-pole = a cleaner near-sine so the Schmitt can't be fooled; the extra
  group delay is self-consistent because we synthesize FROM `d`, so it can't cause clicks. For low
  notes `d` keeps some harmonics → natural growl; high notes → pure → thin. Authentic note-dependence.)
- **Peak-hold** `mDetPeak` = |d| with fast attack (~1 ms) / slow release (~300 ms). `hyst =
  kHystFrac·mDetPeak` (≈0.20). This is the peak-referenced AGC: thresholds scale with the decaying note.
- **Schmitt comparator** on `d`: `mHi` goes TRUE at `d > +hyst`, FALSE at `d < −hyst`. Ripple within
  ±hyst can't add edges. Gate off (force `mHi=false`, no clock) when `mDetPeak < kGateFloor`.
- **Dividers**: on the Schmitt **FALLING edge** (d dropped below −hyst, i.e. d<0 so the half-wave
  carrier is already ≈0 → the sign flip is click-free): `mFf1 = !mFf1`; toggle `mFf2` every second
  falling edge (÷4). Flipping in the d<0 region is the key click-free trick (replaces all the prior
  zero-cross-declick hacks).
- **Synthesis (half-wave × alternating sign, Boss method)**: `hw = d>0 ? d : kSiLeak·d` (kSiLeak≈0.05,
  soft silicon-ish knee). `OCT1 = hw · (mFf1?+1:−1)`; `OCT2 = hw · (mFf2?+1:−1)`. Alternating the
  half-wave lobes' sign every cycle (÷2) / every 2 cycles (÷4) = f/2 / f/4, timbre-carrying.
- **Output** = `mOctLp` (≈2.8 kHz, Tone-adjustable) of `l1·OCT1 + l2·OCT2`, × silence gate `mGate`,
  × `kSubMakeup` (≈2.5). Direct = clean full-band dry. Boss buffered IoStage front/back (near-transparent).
- **Verify offline**: f/2 dominant on a pure tone (T8), f/2 STABLE across the tail on a harmonic-rich
  tone with no period-doubling (T21), sub RMS healthy (T22), 0 latency (T9), finite/bounded.
- **What changed vs every prior build**: single fixed-threshold comparator → **peak-referenced Schmitt
  + falling-edge clock in the d<0 region**. No MPM tracker, no per-block adaptive cutoff, no makeup-
  chasing. The hysteresis + the aggressive fixed pre-LPF are the entire de-gurgle mechanism, exactly
  as the circuit does it.

### G.1 Refractory guard — octave-UP jumping on sustain (play-test fix, 2026-07-03)

Play-test of the §G Schmitt divider: *"the same note when I sustain seems to snap to the octave
above then back down then above again."* Reproduced offline (a sustained low note with a beating,
slightly-inharmonic 2nd partial): 2/18 windows had energy at **f** dominate **f/2** — the ÷2 was
intermittently locking to **2f**. Mechanism: a real string's 2nd harmonic beats in and out; when it
momentarily dominates the ~800 Hz-filtered near-sine, `d` shows **two swings per fundamental period**
→ the comparator clocks twice → the sub jumps an octave up, then back as the beat evolves. **Filtering
cannot fix this on low notes — f and 2f are adjacent, and 2f always survives any usable detection LP.**
The cure is a TIME-domain constraint (a real analog-divider technique — a retriggerable one-shot /
blanking monostable; the memory's flagged "PLL divider" escalation): a **self-referential refractory**
that rejects any clock arriving sooner than `kRefracFrac` (0.70) of the running clock interval, so the
period can never suddenly halve. The interval **lengthens readily / shortens cautiously** (bias toward
the correct lower octave) and **re-establishes on silence** (each fresh note locks clean, no slow-track
regression: onset locks < 40 ms). Offline: beating-harmonic sustain 0/18 jumps (110 Hz AND worst-case
82 Hz low-E), onset lock fast, steady tones still clean. This is NOT a pitch tracker (no MPM/FFT) — just
a monostable, so it stays 0-latency and instant-feel. Knobs if it still jumps: raise kRefracFrac toward
0.8; if it lags on fast downward runs: lower it / speed the lengthen coefficient.

### G.2 Refractory REJECTED; the octave-jump is intrinsic — the real fix (2026-07-03)

Play-test of §G.1: *"absolutely atrocious now — revert it."* Reverted the refractory. WHY it failed
(diagnosed, not guessed): the self-referential refractory has **no independent octave reference** — its
clock-interval estimate is derived from the very edges it's trying to police, so on real playing
(dynamics, vibrato, bends, and the 2nd harmonic genuinely becoming the loudest partial) the estimate
wanders and it starts **rejecting legitimate clocks** → dropouts / wrong octave / stutter across normal
playing. A time-domain guard is the right idea but it MUST be anchored to something that knows the true
fundamental independently.

**The octave-jump is intrinsic to analog dividers — real OC-2s do it too.** Community-confirmed
(TalkBass/Basschat): the divider "follows the loudest frequency in range; the fundamental decays faster
than the overtones, so before long the overtones are louder" → it climbs to 2f. On low notes f and 2f
are both in-band and 2f can genuinely be the largest component, so **filtering + hysteresis cannot cure
it** (you can't separate f from 2f by frequency when 2f is louder). The real-world "fixes" are all input
conditioning: neck pickup, roll off tone, **compressor before the pedal**, play above the 7th fret.

**The research-grounded fix = this doc's OWN §D/§E recommendation, which we have not properly tried with
the now-clean synthesis: gate the divider clock to an octave-ROBUST MPM/NSDF pitch tracker.**
- MPM/NSDF (already implemented, `PitchTracker.h`) estimates the true fundamental period `T0` via
  normalised autocorrelation, which is **octave-error resistant BY DESIGN** — it sees the real period
  even when the 2nd harmonic is the loudest component (that's exactly why MPM beats zero-cross / FFT-peak
  for distorted guitar). This is the independent octave anchor the refractory lacked.
- The divider stays sample-accurate and phase-locked to the audio (0 added latency); the tracker only
  **validates** each Schmitt edge: accept a clock only inside a tolerance window around the expected next
  edge (~`T0` after the last accepted one), reject early (harmonic) edges, and crossfade the sub out when
  clarity is low (chords/mutes). = the time-domain analogue of the Eventide H949 de-glitcher (§E).
- Cost: MPM needs ~2 periods (~25 ms at low E) to gain confidence, so the very first onset runs the
  divider FREE briefly before the gate engages — fine (transient). History: Phase 2b built a crude
  version of this (`≥0.6·T0` one-sided debounce) and play-test #2 said "not tracking accurately," but
  that predates the clean half-wave×sign synthesis and used a one-sided gate, not the full tolerance
  window + clarity crossfade.

**Options for Robbie (his call — character vs robustness):**
- **(A) Tracker-gated divider** — the §E-recommended fix; octave-robust; ~0-latency audio; cleaner than a
  real OC-2. Risk: reintroduces the MPM tracker (mixed history) + possible brief onset freewheel.
- **(B) Faithful asymmetric peak-latch only** — replace the symmetric ±hyst Schmitt with the TRUE OC-2
  comparator (zero-cross SET + high peak-referenced RESET ~0.8·peak). More double-clock-resistant than the
  symmetric Schmitt, pure/simple/0-latency, NO tracker — but still glitches like a real OC-2 (authentic,
  not cured).
- **(C) Accept-authentic + input conditioning** — keep §G, add an internal fundamental-focus aid
  (compressor + aggressive LP before detection); frame the residual glitching as authentic OC-2 behaviour.

### G.3 The octave jump, from first principles (supersedes the §G.2 anecdote, 2026-07-03)

The §G.2 "real OC-2s do it too" claim leaned on forum anecdote — not evidence. Here is the grounded
version: theory + peer-reviewed literature + a reproducible experiment (`docs/pitch_env/octave_experiment.py`).

**Mechanism (deterministic, from Fourier).** A comparator / zero-crossing detector is an *instantaneous
waveform* detector: it clocks the divider on the waveform's upward threshold crossings. For a signal
`x(t) = sin(2πft) + a·sin(2π·2f·t + φ)` (fundamental + 2nd harmonic), once `a` is large enough the
composite waveform has **two** upward zero-crossings per fundamental period, so the ÷2 clocks at 2f and
the sub is one octave too high. This is not noise or a tuning error — it is the true zero-crossing count
of the signal. A detection low-pass only helps when 2f is above its cutoff, i.e. only for high notes;
for low/mid notes f and 2f are both in-band, so filtering cannot separate them.

**Literature (peer-reviewed).** This is the classic **"octave error"** of F0 estimation:
- **de Cheveigné & Kawahara, "YIN," JASA 111(4) 2002** — names it a *"subharmonic error, sometimes
  called 'octave error'"*; shows threshold/instantaneous methods fail and cures it with the *cumulative
  mean normalized difference function* (kills "too-high"/octave-up errors) + an *absolute threshold*
  picking the first dip (kills "too-low"). Error ~3× lower than prior methods. LP prefiltering is only a
  minor factor in their evaluation (Fig. 6c).
- **McLeod & Wyvill, "A Smarter Way to Find Pitch" (MPM), ICMC 2005** — Fig. 2 *literally* shows *"the
  NSDF of a signal with a strong second harmonic … the real pitch has a period of 190, but close matches
  are made at half this period,"* and cures it by choosing the **first key maximum above `k·nmax`,
  k∈0.8–1.0** (the lowest-frequency near-global peak = the true fundamental). MPM *"operates without
  low-pass filtering"* — it is robust to strong harmonics *by the autocorrelation structure*, not by EQ.

**Experiment (reproducible; `octave_experiment.py`).**
- Exp 1 — threshold divider (4-pole 800 Hz detect LP, worst-case phase): tracks the fundamental for
  2nd-harmonic ratio a≤0.5, but **DOUBLES to 2f for a≥0.8 at f=98 Hz AND f=196 Hz**; only the high note
  (587 Hz) survives (its 2f is filtered). Quantitatively confirms: no comparator/hysteresis/filter tune
  fixes low/mid notes.
- Exp 2 — MPM/NSDF on the *same* signals, no filtering: **period ratio 1.00 (CORRECT) across a=0…3** at
  all three pitches. The autocorrelation octave authority is exactly what the comparator lacks.

**Conclusion (grounded).** The octave jump is a fundamental limitation of *instantaneous threshold
detection*, provably unfixable by comparator/filter tuning on low notes. The scientifically-validated
cure is **period-based (autocorrelation/NSDF) octave estimation** — i.e. Option A: let the MPM tracker
(`PitchTracker.h`, already octave-robust by design) be the octave AUTHORITY that validates/gates the
0-latency divider's clock. The earlier refractory (§G.1) failed precisely because it was still an
*instantaneous/interval* guard with no autocorrelation octave authority. This is not ear-patching; it is
the published fix for a named, formally-characterized error.

### G.4 Note-change lag — the seeded-search octave trap + the NSDF(lag/2) escape (2026-07-03)

Play-test: *"if I play octaves (not at the same time) it wants to stay on the first note rather than
the second."* Diagnosed to the TRACKER, not the divider gate. `PitchTracker` seeds its lag search
±1 semitone around the previous estimate (great for stability/CPU, Pardue) and explicitly rejects
low-confidence ×2/×0.5 jumps. But when a note jumps UP an octave, **the new note is still periodic at
the old lag L** (a signal of period L/2 is also periodic at L), so `NSDF(L) ≈ 1` and the seeded search
never even proposes the higher octave — it self-traps on the old (lower) note indefinitely. (Octave
DOWN is self-correcting: the lower note is NOT periodic at the old shorter lag, so the seed fails its
gate and a full re-acquire picks it up.)

Onset-triggered re-acquire was tried and rejected: a steady-level re-pluck doesn't raise amplitude
enough to detect reliably, and forcing a full re-acquire mid-transition (window still full of the old
note) just re-locks to the old note.

**The fix (grounded, legato-safe): an NSDF(lag/2) octave-up check.** While locked at lag L, also
evaluate `NSDF(L/2)`. Measured discriminator (`octave_up_escape_test.py`):
- **Genuine octave-up** (note truly at L/2, old fundamental gone): `NSDF(L/2) ≈ 0.995–0.997`.
- **Sustain doubling** (note still at L, 2nd harmonic grows — the case §G.1–G.3 must NOT re-introduce):
  `NSDF(L/2)` climbs only to ~0.92 *even with the 2nd harmonic at 5× the fundamental*, because the
  fundamental persists and keeps the half-period from correlating.

So `NSDF(L/2) ≥ kOctaveUp (0.95)` cleanly means "the note genuinely went up an octave" → follow to L/2.
It is NOT onset-based (works for legato/hammer-ons), it self-limits (clean single notes have low
NSDF(L/2) so it never fires or cascades — verified across 82–440 Hz), and it can't re-introduce sustain
doubling (0.95 sits above the ~0.92 doubling ceiling). The 3-point median makes it require ~2 frames,
rejecting single-frame false escapes. Implemented in `PitchTracker::analyze()` (seeded branch); benefits
BOTH tracker-driven models (OC-2 divider gate + Whammy Classic grain window). Needs Windows build +
play-test. Lever: raise kOctaveUp toward 0.97 if any note false-escapes; lower toward 0.93 if a real
octave-up is sluggish. NOTE: a crude hard-cut model showed a transition artifact on a P5 DOWN leap
(196→130 → garbage) that did NOT reproduce on clean single notes — likely the model's mixed-window, but
worth an ear-check on big downward leaps.

### G.5 POG phase-vocoder quality — formant preservation (the thin up-octave) (2026-07-03)

Not an octave-error (the PV octaves are exact); a NATURALNESS fix. The Bernsee shifter moves every
bin to k·ratio, which drags the spectral ENVELOPE (formants) up with the pitch — so the +1/+2 octave
sounds thin / chipmunky. Fix (standard source-filter, DAFX Ch.9 / Bernsee): estimate the spectral
envelope, WHITEN the spectrum by it (excitation), shift the excitation, then RE-IMPOSE the ORIGINAL
(unshifted) envelope → the harmonics move up but the formants stay put → the up-octave keeps its body.

Envelope = cepstral lifter: `env = exp( liftered real-cepstrum )`, lifter cutoff ~1 ms of quefrency
(keeps the broad envelope, excludes the pitch periodicity; good to ~1 kHz fundamental). Validated:
- `formant_experiment.py` (numpy): lifter≈50 samples recovers F1/F2 across f0 100–220 Hz; the preserved
  shift holds F1 at ~680 Hz vs the naive 1664 Hz (chipmunk), and retains 0.78× the 400–1000 Hz body vs
  the naive 0.18×.
- Compiled C++ (`SpectralShifter` + `ss_test.cpp`): builds clean; formant-OFF is bit-identical run-to-run
  (existing voices untouched — it's opt-in, default off); formant-ON is finite and lifts the
  low/high body ratio on the up-octave from 0.148 (naive) to 4.326.

Implemented as `SpectralShifter::setFormantPreserve(bool)` (off by default; costs 2 extra FFTs/hop, so
enabled ONLY on the up-octave voices `mPolyUp`/`mPolyUp2` = Micro POG +1 / POG2 +2). Down-octaves and
Whammy Chords stay bit-identical. Needs Windows build + play-test. If CPU is a concern, it's up-voice-only;
if he wants an A/B, expose the toggle as a param. STILL TODO (next increment): identity/peak PHASE-LOCKING
(Laroche & Dolson) to cut the watery phasiness — a separate change to the phase-propagation loop.

### G.6 POG phasiness — identity/peak phase-locking (2026-07-03)

The plain phase vocoder propagates every bin's phase independently, so bins belonging to one
sinusoid drift out of vertical alignment across frames → the watery "phasiness" (Laroche & Dolson
1999, "Improved phase vocoder"). Fix = **identity / peak phase-locking**: find spectral peaks and
slave each bin's synthesis phase to its region's peak — the peak evolves freely (normal accumulation)
and its neighbours keep the current frame's phase relationship to it:
`outPhase(k) = sumPhase(peak) + [anaPhase(src k) − anaPhase(src peak)]`, where `src ≈ k/ratio` is the
analysis bin that fed output bin k. Restores vertical coherence.

Implemented as `SpectralShifter::setPhaseLock(bool)` (default OFF): stores per-bin analysis phase,
splits the synthesis loop into (accumulate)→(lock)→(write), finds peaks (local max over ±2 above a
1e-4·max floor), assigns each bin its nearest peak, and re-phases. Validated in compiled C++
(`ss_phaselock_test.cpp`): builds clean; OFF is bit-identical run-to-run; ON is finite, still shifts
exactly up (220→440, 1.6e6× separation) AND down (0.5), works combined with formant, and measurably
alters the phase structure (its purpose).

**Left OFF by default in PitchBlock** (a commented one-liner enables it on the up/down voices) for two
honest reasons: (1) phasiness was NOT a reported complaint — the reported issue was thinness, fixed by
§G.5 formant preservation; (2) phase-locking is the one change I can't judge offline and it can trade
phasiness for a slightly metallic/transient-smeared character. So the shipped build gets the clean
formant win to evaluate; phase-locking is one uncomment away to A/B by ear (or wire a temp toggle).
