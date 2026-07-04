#pragma once
// DriveBlock — a 3-slot SERIES rack of drive pedals (shared pre, before the
// A/B split: one board feeding both rigs, the common real two-amp live rig).
//
// Voicings are tuned to the MEASURED behaviour of the classic circuits they
// model (from published circuit analyses + measured frequency responses):
//   Off          : out of the path — bit-exact passthrough.
//   Boost        : germanium treble booster ("Range '65") / FET clean boost
//                  ("Plex Boost"). Range '65 = a one-pole input-cap high-pass
//                  (the 3-way switch moves the corner) + soft germanium clip.
//                  TWO models: 0 "Range '65" = the circuit-fit Dallas Rangemaster,
//                  1 "Plex Boost" = the circuit-fit Echoplex EP-3 / Xotic EP Booster
//                  (see below). The v1 stand-ins have been removed from the catalog.
//   Plex Boost   : the Maestro Echoplex EP-3 preamp (single JFET common-source) as the
//                  Xotic EP Booster. The pure EP-3 stage is ~FLAT across audio (fit
//                  ep3_response.py) -- the character is clean headroom + very high
//                  input Z + JFET 2nd-harmonic, FULL-RANGE (opposite of the
//                  Rangemaster). Voiced as the EP Booster: a gentle broad presence
//                  high-shelf (low-Q peak ~5 kHz, +4 dB) + full low end + a small JFET
//                  bias for warmth. High headroom: mostly clean, a little hair maxed.
//   Range '65    : the Dallas Rangemaster (OC44 germanium common-emitter), FIT to
//                  the schematic (rangemaster_response.py): the whole audio-band
//                  voicing is the 5nF input cap into the ~12k input impedance = a
//                  1st-order high-pass at ~2.65 kHz, flat above (it is a TREBLE
//                  booster -- output stays bright, no top roll). The high-pass sits
//                  PRE-clip so bass clips LEAST. Real gain Gv = gm*Rc ~ 80 (38 dB) at
//                  full Volume (gMax 80, vs the stand-in's 20). Soft germanium clip
//                  (tanh) with an off-centre bias -> asymmetric even-harmonic warmth
//                  + soft compression when strummed hard.
//   Green Drive  : a green-box mid-hump overdrive (TS-style). FOUR OD models:
//                  model 0 "Green Drive" = the feedback-clip TS808; model 1
//                  "Super Drive" = the Boss SD-1; model 2 "Gold Horse" = the Klon
//                  Centaur; model 3 "Breaker Drive" = the Marshall Bluesbreaker
//                  (all see below). 0-1 share the ~720 Hz mid hump; the Klon's is a
//                  broader ~1 kHz band-pass. The v1 tanh stand-in has been removed.
//   Super Drive  : the Boss SD-1 Super Overdrive (OD-1 lineage). Small-signal
//                  voicing ~= the TS808 (fit to the schematic, RMS 0.63 dB), but
//                  the identity is ASYMMETRIC clipping: 3 diodes (2+1) = a ~2:1
//                  threshold ratio -> persistent even harmonics (a 2nd-harmonic
//                  "crunch"). Built on clip type 4 (asym cubic) so the asymmetry
//                  survives at high gain; bias 0.35 (kn 0.65) = the soft feedback
//                  knee softened from the literal 2:1. Noticeably hotter than Green Drive
//                  (gMin 6/gMax 120) + a touch more output. Same feedback-clip
//                  pre/de-emphasis feel as Green Drive. See docs/drive/sd1.md.
//   Gold Horse   : the Klon Centaur (TL072 + germanium diodes-to-ground). NOT a TS:
//                  a ~1 kHz BAND-PASS op-amp gain stage, SYMMETRIC germanium HARD clip
//                  (clip 1 + 2nd-order ADAA), SUMMED with a big parallel CLEAN path =
//                  the "transparent overdrive". Modelled as a heavy clean blend taken
//                  from the RAW input (restores the lows the mid-focused clip drops) +
//                  shapeTrack bloom (near-clean boost at low Drive). Bright/open top,
//                  lots of output (also a boost). See docs/drive/klon.md.
//   Black Rodent : a hard-clip distortion (ProCo RAT). ONE model (0): the circuit-fit
//                  RAT (see below) -- symmetric HARD clip with the LM308 gain-stage
//                  voicing, 2nd-order ADAA and the "Filter" tone. (v1 stand-in removed.)
//   Black Rodent : the LM308 clipper amp's bass-cut + ~935 Hz hump + top roll-off
//                  (FIT to the schematic, proco_rat_response.py) sit PRE-clip and
//                  bloom with Drive, so mids hit the silicon diodes (to ground)
//                  hardest and bass clips LEAST — the RAT's frequency-selective
//                  grind. Hard clip on 2nd-order ADAA (square corners fizz most).
//                  Tone is the passive "Filter" low-pass: darker CLOCKWISE (opposite
//                  of a TS). Hot, calibration-referenced gain range (LM308 Gv ~2300).
//   Round Fuzz   : a vintage germanium fuzz. Minimal EQ (keeps the highs, trims
//                  only the deep bass, no tone control — as measured) + strongly
//                  ASYMMETRIC clipping: a musical 2nd harmonic at low/mid Fuzz
//                  that squares up toward both rails when cranked.
//
// Signal per slot:  drive gain -> pre low-cut -> mid/treble peak -> waveshaper
//                   (ADAA) -> post low-pass -> DC blocker -> tone -> level.
//
// Anti-aliasing: shapers run ANTIDERIVATIVE anti-aliasing (ADAA) in DOUBLE (in
// float the antiderivative subtraction loses precision at small signal and
// crackles). The legacy shapers (tanh / hard / asym) use 1st-order ADAA:
//   y = (F1(x1)-F1(x0))/(x1-x0)  (midpoint-f fallback for tiny dx).
// The cubic soft-clip (Green Drive) and the hard clip (Black Rodent) can run
// 2nd-order ADAA (Parker/Bilbao): from F2, three samples, with L'Hopital fallbacks
// + a peak guard — markedly less fizz. Selected per-voicing (clip 3 always; clip 1
// when v.adaa2 is set).
// Zero latency, all-Off rack bit-exact.
//
// Base shapers:
//   0 soft (tanh)            — Boost (asymmetry via bias). 1st-order ADAA; with
//                              v.adaa2: the tanh-FIT poly saturator on 2nd-order
//                              ADAA (Range '65 — see kSat* below)
//   1 hard clip +/-1 (sym)   — Distortion (1st-order ADAA, or 2nd-order if v.adaa2)
//   2 hard clip, ASYM rails  — Fuzz (positive rail +1, negative rail -(1-bias))
//   3 cubic soft (poly)      — Overdrive v2 (cheap F1+F2 -> 2nd-order ADAA)
//
// Green Drive authentic-TS extras (clip 3 only, all zero-latency):
//   * pre/de-emphasis high-shelf pair around ~700 Hz: boosts mids/highs INTO
//     the clipper and cuts them after -> bass is clipped LEAST (the TS feedback
//     HPF) and the clip corners are softened (the 51 pF cap). Net small-signal
//     response stays ~flat; the DISTORTION is frequency-selective.
//   * clean blend: sums a little un-clipped band-limited signal back in (the TS
//     "secret" / Klon feed-forward) -> preserves dynamics, never pure fizz.
//   * envelope dynamics: a follower on the input nudges the clean blend so soft
//     picking cleans up and digging in bites — touch sensitivity.
//   * STATIC voicing (shapeTrack 0): the ~720 Hz mid hump + bass-cut are present
//     even at Drive 0, so the pedal works as an always-on mid SHAPER (drive off,
//     tone past noon) — like the real fixed tone stack. Only the clipping (gain
//     + emphasis) scales with Drive; the floor gain (gMin) leaves it breaking up
//     a little even at minimum, as the real circuit does.
//   * AUTHENTIC gain range (gMin 12 -> gMax 118 = the REAL TS: (51k + 500k pot)
//     over 4k7 -- min Drive keeps +21.6 dB into the diodes, so a TS never fully
//     cleans up; refit 2026-07-04, was the approximate 5..80): the clip
//     threshold is FIXED, so distortion tracks the actual input LEVEL — hot
//     pickups/DI drive harder than weak ones, exactly like the real pedal. It is
//     voiced for the app's calibration reference (CalNorm kReferenceDbu): with
//     Calibrate Input ON the guitar is trimmed to that reference, so the response
//     is level-accurate to a real guitar (and consistent across interfaces).
//     Touch dynamics are strongest at low-to-mid Drive (a cranked TS compresses).
//     The mid peak (+3.6 dB
//     @ 820 Hz, Q0.7) + low-cut (220) + top LP (1900) are FIT to the measured
//     TS808 small-signal transfer function (see docs/drive/circuit-accuracy.md):
//     +5.5 dB hump over 200 Hz @ ~720 Hz, matched to ~1 dB. (Our first pass was
//     ~2x over-humped; the schematic, not the ear, settled it.)
//   * TREBLE-shelf Tone (bass fixed): the Tone knob moves the treble above
//     ~1.2 kHz and leaves bass/low-mids put — the TS tone control, not a tilt.
//     Drive itself is the engine's log/audio-taper map (gain ~ pot resistance).
//
// The mid/treble peak is a shared RBJ Biquad (Biquad.h), reconfigured only on a
// voicing change. A DC blocker after the shaper removes the offset asymmetric
// clipping introduces. Tone: per-voicing one-pole TILT (0.5 = transparent).
// All params atomic. Verified by tests/drive_test.cpp.

#include "Blocks.h"
#include "Biquad.h"
#include "IoStage.h"
#include <atomic>
#include <cmath>

namespace nam_rig
{

class DriveBlock : public MonoBlock
{
public:
    const char *name() const override { return "Drive"; }

    static constexpr int kSlots = 3;
    enum class Kind { Off = 0, Boost = 1, Overdrive = 2, Distortion = 3, Fuzz = 4 };

    void setKind(int slot, int k)      { at(slot).kind.store(k); }
    void setDrive(int slot, float v)   { at(slot).drive.store(clamp01(v)); }
    void setTone(int slot, float v)    { at(slot).tone.store(clamp01(v)); }
    void setLevel(int slot, float v) { at(slot).level01.store(clamp01(v)); } // Volume/Level KNOB 0..1 (per-model pot curve, see volFor/volPot)
    void setRange(int slot, int r) { at(slot).range.store(r); } // treble-boost cap switch: 0 Treble/1 Mid/2 Full
    void setOn(int slot, bool on) { at(slot).on.store(on); }            // footswitch (default on)
    void setModel(int slot, int m) { at(slot).model.store(m); }         // model within the category
    void setGateOn(int slot, bool on) { at(slot).gateOn.store(on); }    // fuzz bias-starved gate enable (default on)
    void setMigrateFull(int slot, bool full) { at(slot).migrateFull.store(full); } // RAT hump migration range: false Tight (default) / true Full

    bool anyActive() const
    {
        for (int s = 0; s < kSlots; ++s)
            if ((Kind)mSlot[s].kind.load() != Kind::Off && mSlot[s].on.load())
                return true;
        return false;
    }

    // Per-voicing character (single source of truth for process + tests).
    struct Voicing
    {
        int   clip;        // 0 soft tanh, 1 hard sym, 2 hard ASYM rails, 3 cubic soft, 4 ASYM cubic (fuzz)
        float gMin, gMax;  // pre-gain range (linear), log-mapped from Drive
        float lowCutHz;    // pre-shaper one-pole high-pass (tighten); 0 = off
        float midHz, midDb, midQ;  // pre-shaper peak (mid hump / treble peak); 0 dB = off
        float lpHz;        // post-shaper one-pole low-pass (top roll-off); 0 = off
        float bias;        // type 0/1/3: input bias; type 2: negative-rail = -(1-bias)
        float pivotHz;     // tone tilt pivot
        float outTrim;     // voicing output compensation
        float shapeTrack;  // 0 = pre-shaper EQ always on; 1 = EQ (low-cut + mid)
                           //     scales with the Drive knob (the mid hump blooms
                           //     with gain instead of being a fixed band-pass)
        float midPost;     // 0 = mid peak PRE-clip (treble booster input cap);
                           //     1 = POST-clip (overdrive/distortion tone stack -> peak freq is
                           //     level-stable instead of dragged down by clipping)
        // --- clip-3 (cubic) authentic-TS extras; all 0 = behave like a plain shaper ---
        float emphDb;      // pre/de-emphasis high-shelf depth (dB); 0 = off
        float emphHz;      // emphasis corner (Hz)
        float cleanBlend;  // 0..1 clean signal summed back after clipping
        float dynDepth;    // 0..1 envelope -> clean-blend modulation (touch)
        // --- distortion (RAT) extras; 0 = behave like the legacy tilt-tone shaper ---
        float toneFilterHz;// >0: Tone = a SWEEPABLE post-clip low-pass (the RAT
                           //     "Filter": darker CW), this value = the darkest
                           //     (full-CW) corner; 0 = use the tilt/treble-shelf tone
        float adaa2;       // >0: run this clip type through 2nd-order ADAA (hard
                           //     clip gets the polynomial F2 path); 0 = 1st-order
        // --- fuzz (clip 4) extra; 0 = no gate (every existing model zero-fills it) ---
        float gate;        // 0..1 bias-starved gate depth: as a note decays past a
                           //     threshold the cold-biased stage collapses the output
                           //     (the germanium "velcro"/splat); 0 = off
        // --- Klon active treble shelf; 0 = use the legacy tilt/treble-shelf tone ---
        float trebleShelfDb; // >0: Tone = an ACTIVE high-shelf at pivotHz (bass FIXED),
                             //     asymmetric range +trebleShelfDb (full CW) down to
                             //     -0.44*trebleShelfDb (full CCW) = the Klon's +18/-8 dB
                             //     active treble control; noon (0.5) = flat. 0 = off
        // --- Big Muff cascade; 0 = single shaper (every existing model zero-fills these) ---
        float muffStages;    // >1: run an N-stage SOFT-clip CASCADE (the Big Muff's two
                             //     consecutive diode-in-feedback clip stages) instead of a
                             //     single shaper. Currently 2. 0/1 = the normal single clip.
        float muffLpHz;      // the input-booster Miller low-pass BEFORE clip stage 1
                             //     (the Muff's dark, smooth, no-fizz voice: clipping a
                             //     low-passed signal sounds smoother). Only used when muffStages>1.
        float muffInterLpHz; // the clip-1 Miller low-pass BEFORE clip stage 2 (the real
                             //     circuit's distinct, HIGHER corner ~1.78 kHz -> keeps the
                             //     low-mid grind that a single shared corner over-darkens).
                             //     0 -> fall back to muffLpHz. Only used when muffStages>1.
        // --- ProCo RAT authenticity extras; 0 = the fixed-peak / no-slew behaviour ---
        float midMigrate;    // >0: the mid PEAK FREQUENCY slides DOWN as Drive climbs
                             //     (the LM308 gain-bandwidth product collapsing the closed-
                             //     loop bandwidth -> the RAT's ~2.3 kHz hump at low gain
                             //     drops toward ~300 Hz cranked). The endpoints are picked
                             //     by the runtime Tight/Full toggle (setMigrateFull); the
                             //     peak is log-interpolated by the Drive knob (circuit-
                             //     consistent since log(preGain) is linear in Drive). 0 =
                             //     the fixed midHz peak (every other model zero-fills this).
        float slewMax;       // >0: LM308 slew-rate limit on the pre-clip op-amp output, in
                             //     clip-normalized units/sample at a 48 kHz reference (scaled
                             //     by 48000/fs at runtime). Rounds fast edges the way the
                             //     RAT's slow op-amp does (smooth-but-aggressive grind; large-
                             //     signal HF loss when cranked) -> level/rate dependent, so
                             //     it's invisible to the small-signal frequency response.
                             //     Applied in the hard-clip (adaa2) branch only. 0 = off.
        // --- Klon DUAL-GANG clean/dirty balance; 0 = flat cleanBlend (every other model zero-fills) ---
        float cleanBlendLo;  // >0: the clean-blend BASE tracks the Drive knob. The real Klon
                             //     Gain pot is DUAL-GANGED -- it raises the dirty-path gain
                             //     (preGain) AND rebalances the clean/dirty sum. This value =
                             //     the clean fraction at MIN Drive (near-clean boost, clean
                             //     dominates); cleanBlend = the fraction at MAX Drive (dirty
                             //     path dominates, clean still fills underneath). Linearly
                             //     interpolated by Drive, then the dynDepth envelope nudge
                             //     rides on top. The clean leg stays LEVEL-CONSTANT either way
                             //     (the real behaviour). 0 = the flat cleanBlend at every
                             //     Drive (byte-exact; only the Klon hard-clip path uses it).
        // --- Fuzz Face bias SAG; 0 = static bias (every other model zero-fills) ---
        float sagDepth;      // >0 (clip 4 only): dig in past the calibrated picking
                             //     reference and Q1's operating point SAGS -- the negative
                             //     knee (kn) closes further over ~60 ms (the bias network's
                             //     big caps, not instant) and recovers in ~250 ms. At fuzz
                             //     gain the wave is rail-to-rail, so the closing rail is
                             //     heard as the note DIPPING ~1 dB on a hard attack then
                             //     recovering (bias-sag compression) + a touch more duty
                             //     asymmetry -- the attack-end complement of the decay-end
                             //     gate. Thresholded ABOVE the reference level so normal
                             //     picking (T30-T37) is untouched. 0 = off (byte-exact).
    };

    // A specific pedal MODEL inside a category (Type). A category can hold several
    // models; the UI picks the Type (category) then the model. hasRange = the
    // model exposes the 3-way input-cap switch (treble booster only).
    struct Model { const char *name, *sub; Voicing v; bool hasRange; };

    static const Model *modelsFor(Kind cat, int &count)
    {                       // clip  gMin    gMax  lowCut   midHz  midDb midQ   lpHz   bias   pivot   outTrim shp post  emphDb emphHz clean  dyn   toneF  adaa2
        static const Model boost[] = {
            // model 0: circuit-fit Dallas Rangemaster (OC44 germanium, common-emitter).
            // The voicing is a single 1st-order high-pass = the 5nF input cap into the
            // ~12k input impedance, fc ~2.65 kHz, FLAT above (no peak / no top roll) --
            // FIT to the schematic (docs/drive/rangemaster_response.py, RMS 0.01 dB). The
            // Range switch is the input-cap mod (5/10/47 nF -> 2653/1326/282 Hz via
            // applyRange). The high-pass sits PRE-clip so bass clips LEAST and the
            // full-gain treble clips most (the booster's frequency-selective grind).
            // Lifelike gain range gMin 4 -> gMax 80 = the real Gv = gm*Rc ~ 80 (38 dB) at
            // full Volume (the stand-in's gMax 20 was ~4x too low, like the early TS).
            // Soft germanium clip (tanh-fit poly saturator, adaa2 1 -> 2nd-order ADAA:
            // the treble booster clips almost pure top-octave content, where the old
            // 1st-order tanh aliased worst of ALL models; dominant 13 kHz fold -18 dB,
            // h1-h3 within ~0.4 dB of the legacy tanh) with an OFF-CENTRE bias (the Rangemaster's
            // deliberately asymmetric operating point) -> even-harmonic warmth + soft
            // compression when strummed hard. Static (shapeTrack 0): the input-cap
            // network is fixed, so it shapes even at Drive 0. Calibration-referenced.
            {"Range '65", "Germanium Treble Boost",
             { 0, 4.0f, 80.0f, 2653.0f,   0.0f, 0.0f, 0.7f,    0.0f, 0.30f, 2500.0f, 0.50f, 0.0f, 0.0f,  0.0f, 700.0f, 0.0f, 0.0f,   0.0f, 1.0f}, true},
            // model 1: Echoplex EP-3 preamp / Xotic EP Booster (single JFET common-source).
            // The PURE EP-3 stage measures essentially FLAT across the audio band (the
            // Cin/Rgate HPF sits ~3 Hz, the source is unbypassed, the 220 pF roll is
            // >70 kHz) -- its magic is clean headroom + very high input Z + subtle JFET
            // 2nd-harmonic, NOT an EQ. It is FULL-RANGE (the opposite of the Rangemaster's
            // treble-only high-pass). "Plex Boost" as a pedal = the Xotic EP Booster, which
            // adds gentle TONE-SHAPING: a broad presence high-shelf + full low end. We fit
            // our low-Q pre-shaper peak (midHz 5000, Q 0.35, +4 dB) to that gentle shelf
            // (docs/drive/ep3_response.py, RMS 0.35 dB), low-cut at ~15 Hz (full bass).
            // High-headroom CLEAN soft (tanh) clip with a small off-centre bias for the
            // JFET even-harmonic warmth -> mostly clean, a little hair only when cranked.
            {"Plex Boost", "Clean Full-Range Boost",
             { 0, 1.3f,  6.0f,  15.0f, 5000.0f, 4.0f, 0.35f,   0.0f, 0.10f, 1200.0f, 0.74f, 0.0f, 1.0f,  0.0f, 700.0f, 0.0f, 0.0f,   0.0f, 0.0f}, false},
        };
        static const Model od[] = {
            // model 0: the reworked feedback-clip TS808 (was "Green Drive II").
            // 2026-07-04 AUTHENTIC REFIT (docs/drive/green-drive.md): gain range now the
            // REAL 12..118 ((51k+500k)/4k7 -- min Drive keeps hair with a humbucker, a
            // TS never fully cleans up); the clean base now TRACKS Drive via cleanBlendLo
            // (0.35 min -> 0.12 max = the non-inverting topology's always-unity dry leg
            // shrinking in PROPORTION as the clipped component grows -- the old flat 0.20
            // was too small at low Drive, too big cranked). Noon RMS matched to the old
            // voicing within 2% -> outTrim untouched; sweep now keeps growing to max
            // instead of flat-lining past drive 0.75. T70.
            {"Green Drive", "Mid-Hump Overdrive",
             { 3,12.0f,118.0f, 220.0f,  820.0f, 3.6f, 0.7f, 1900.0f, 0.00f, 1200.0f, 1.15f, 0.0f, 1.0f,  9.0f, 700.0f, 0.12f, 0.40f, 0.0f, 0.0f,
               0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, /*cleanBlendLo*/0.35f}, false},
            // model 1: circuit-fit Boss SD-1 Super Overdrive (OD-1 lineage, uPC4558
            // feedback-clip). Small-signal voicing is ~the TS808 (fit sd1_response.py,
            // RMS 0.63 dB: same ~+5 dB hump @ 720-900 Hz, slightly fuller bass / a hair
            // brighter -- the SD-1's "more open" reputation, confirmed by the circuit).
            // The IDENTITY is ASYMMETRIC clipping (3 diodes, 2+1 -> a ~2:1 threshold
            // ratio): clip type 4 (asym cubic) so the even-harmonic crunch PERSISTS at
            // gain (a symmetric clip + DC bias would just square up symmetrically). Real
            // ratio 2:1 = bias 0.50, softened to bias 0.35 (kn 0.65) for the diodes'
            // soft FEEDBACK-loop knee -- clearly asymmetric (h2/h1 ~0.04-0.075), milder
            // than the fuzz. Noticeably hotter than Green Drive (gMin 6/gMax 120, the 1M drive
            // pot + 0.9V 1S2473 diodes) + a touch more output (outTrim 1.25). Same
            // feedback-clip feel as Green Drive: pre/de-emphasis (bass clips least), small clean
            // blend + touch dynamics. Static (shapeTrack 0), mid post-clip, calibrated.
            {"Super Drive", "Asymmetric Overdrive",
             { 4, 6.0f,120.0f, 160.0f,  900.0f, 5.0f, 0.5f, 2000.0f, 0.35f, 1200.0f, 1.25f, 0.0f, 1.0f, 10.0f, 700.0f, 0.15f, 0.40f, 0.0f, 0.0f}, false},
            // model 2: circuit-fit Klon Centaur (TL072 + germanium diodes-to-ground).
            // NOT a TS: the op-amp gain stage is a ~1 kHz BAND-PASS (fit klon_response.py,
            // RMS 0.14 dB) clipped by SYMMETRIC germanium (hard, clip 1 + 2nd-order ADAA),
            // then SUMMED with a big parallel CLEAN feedforward -> the "transparent
            // overdrive". We model the clean sum as a HEAVY clean blend taken from the RAW
            // input (level-constant, + dynDepth 0.30 touch): the mid-focused clipped path
            // drops the lows (lowCut 210, hump 980) and the full-range clean restores them
            // -> big open low end + dynamics. The Klon Gain pot is DUAL-GANGED, so the
            // clean/dirty BALANCE tracks Drive too: cleanBlendLo 0.85 (near-clean boost at
            // min) -> cleanBlend 0.30 (dirty path dominates at max), the clean still filling
            // underneath. shapeTrack 1 = the hump also BLOOMS with Drive, so at low Drive it
            // is a near-clean boost (the Klon reputation), distorting more as Drive climbs.
            // Bright/open top (lpHz 4700, the 27V headroom feel). Modest
            // gMin (genuinely clean min), moderate gMax (~the real 40 dB), lots of output
            // (outTrim -- it is also a boost). Calibrated.
            // Tone = ACTIVE treble shelf (trebleShelfDb 18 @ pivot 408 Hz): the real
            // Klon high-shelf (bass fixed, +18/-8 dB), noon = flat. NOT the engine tilt.
            {"Gold Horse", "Transparent Overdrive",
             { 1, 2.0f, 70.0f, 210.0f,  980.0f, 3.2f, 0.3f, 4700.0f, 0.00f,  408.0f, 0.95f, 1.0f, 0.0f,  0.0f, 700.0f, 0.30f, 0.30f, 0.0f, 1.0f, 0.0f, 18.0f,
               /*muff*/0.0f, 0.0f, 0.0f, /*midMigrate*/0.0f, /*slewMax*/0.0f, /*cleanBlendLo*/0.85f}, false},
            // model 3: circuit-fit Marshall Bluesbreaker (the early-'90s pedal, the
            // King of Tone / Timmy / Morning Glory ancestor). A TL072 non-inverting
            // boost+filter (IC1A) into an INVERTING soft-clip stage (IC1B, 4x 1N914 in
            // the feedback loop: high 1.2V threshold + a 6k8 series R -> SOFT, warm),
            // then a passive treble-rolloff tone. Derived + fit in bluesbreaker_response.py
            // (RMS 0.35 dB, 40-6k). The small-signal voice is the OPPOSITE of a TS:
            // WIDE-OPEN low end (input HPF C1 10n / R1 1M = ~16 Hz, NOT a 220 Hz bass cut)
            // + a GENTLE bright presence shelf (~+4.7 dB toward 4-7 kHz) from the IC1A gain
            // stage, sitting PRE-clip (midPost 0) so it both colours the tone AND clips the
            // highs a touch = the BB's mild grain when pushed. STATIC (shapeTrack 0): the
            // circuit's emphasis saturates fast (it is ~flat only at the very bottom of the
            // Drive pot and essentially FULL from noon up), so a fixed shelf matches the
            // usable range far better than a linear bloom -- and the "clean till you push
            // it" feel comes from the low gMin + soft clip, not the EQ. SYMMETRIC cubic soft
            // clip (bias 0) -- gentler than Green Drive/SD-1: softer knee, MILDER pre/de-emphasis
            // (emphDb, frequency-selective clip; cancels in the linear path so it doesn't
            // touch the small-signal fit) + a touch more clean blend (0.22) + touch (0.45)
            // since the BB famously keeps guitar timbre/dynamics. LOWER, softer gain range
            // than Green Drive (gMin/gMax) -- clean till pushed, "fairly low output, breaks up late"
            // (the real pedal). Treble-shelf tone (bass fixed, soft-poly path), pivot 1200.
            // Calibration-referenced. See docs/drive/bluesbreaker.md.
            {"Breaker Drive", "Soft Low-Gain Overdrive",
             { 3, 3.0f, 48.0f,  20.0f, 4000.0f, 4.7f, 0.68f,13000.0f, 0.00f, 1200.0f, 1.15f, 0.0f, 0.0f,  5.0f, 700.0f, 0.22f, 0.45f, 0.0f, 0.0f}, false},
        };
        static const Model dist[] = {
            // model 0: circuit-fit ProCo RAT (the sole Distortion model -- the old
            // simple hard-clip stand-in was retired, so this IS "Black Rodent"). Pre-clip
            // gain-stage EQ (gentle low-cut + a mid hump + top roll) FIT to the LM308
            // stage (docs/drive/proco_rat_response.py, RMS 0.03 dB), bloomed with Drive
            // (shapeTrack 1, PRE-clip so bass clips LEAST). Symmetric HARD clip (silicon
            // diodes to ground) on 2nd-order ADAA. Tone = the RAT "Filter" sweepable
            // low-pass (darker CW). Hot, calibration-referenced range (LM308 Gv up to ~2300).
            // Two RAT-authentic behaviours the fixed voicing missed: (1) the hump
            // MIGRATES down with Drive (midMigrate 1 -> the LM308 GBW collapse; Tight/Full
            // toggle picks the endpoints); (2) the LM308 SLEW limit rounds fast edges
            // (slewMax). midHz 935 stays the nominal noon anchor; the migration overrides
            // the peak frequency per block. Trailing fields: gate 0, trebleShelfDb 0,
            // muffStages/muffLpHz/muffInterLpHz 0, then midMigrate 1, slewMax.
            {"Black Rodent", "Hard-Clip Distortion",
             { 1, 4.0f,150.0f,  62.0f,  935.0f,17.0f, 0.5f, 4800.0f, 0.00f, 1500.0f, 0.47f, 1.0f, 0.0f,  0.0f, 700.0f, 0.0f, 0.0f, 475.0f, 1.0f,
               0.0f, 0.0f, 0.0f, 0.0f, 0.0f, /*midMigrate*/1.0f, /*slewMax*/2.5f}, false},
        };
        static const Model fuzz[] = {
            // model 0: circuit-fit germanium Fuzz Face (AC128, the "round" one). Voicing is
            // just a bass trim (one-pole low-cut ~50 Hz, fit fuzz_face_response.py), flat &
            // bright above (NO top roll). The identity is the ASYMMETRIC cubic clip (type 4):
            // persistent asymmetry at all gains (cold-biased Q1) -> soft for small signals,
            // a tilted square when cranked; 2nd-order ADAA (polynomial, no dilog). dynDepth
            // gives the touch/volume cleanup (soft picking -> cleaner); gate gives the
            // bias-starved "velcro"/splat on decay; sagDepth 0.35 gives the ATTACK-end
            // complement (dig in past the reference level and the bias point sags -> the
            // hard-hit note dips ~1 dB over ~60 ms then recovers ~250 ms; reference-level
            // playing untouched). Hot, calibration-referenced gain range.
            {"Round Fuzz", "Germanium Fuzz",
             { 4, 8.0f,200.0f,  50.0f,    0.0f, 0.0f, 0.7f,    0.0f, 0.45f,  700.0f, 0.65f, 0.0f, 0.0f,  0.0f, 700.0f, 0.0f, 0.50f,  0.0f, 0.0f, 0.6f,
               0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, /*sagDepth*/0.35f}, false},
            // model 1: circuit-fit EHX Big Muff Pi (Ram's Head '73). Filed under FUZZ
            // (it is marketed/perceived as a fuzz, though technically a diode distortion).
            // The Muff is NOT a single shaper -- it is TWO consecutive SOFT-clip stages
            // (silicon 1N914 back-to-back diodes in each transistor's collector->base
            // FEEDBACK loop, ~+/-0.6 V), so we run a real 2-stage cubic CASCADE
            // (muffStages 2): distinct per-stage Miller-cap low-passes BEFORE each clip
            // (muffLpHz 1200 pre stage 1, muffInterLpHz 1780 pre stage 2) -> the dark,
            // smooth, no-fizz voice (clipping a low-passed signal sounds smoother) and the
            // dense, compressed double-clip "wall" a single clip can't make. A fixed
            // inter-stage gain (kMuffStage2Gain) drives stage 1's output into stage 2's
            // knee. Pre-clip low-cut 70 Hz (+ an inter-stage HP off the same lowCutHz)
            // tightens the lows; gentle post LP 1170 keeps it dark. TONE = the REAL PASSIVE
            // Big Muff tone stack (a treble high-pass + bass low-pass blended by the Tone
            // pot), a 2nd-order nodal network bilinear-discretised, recomputed per block
            // (the `cascade` branch below ~L603; T56 asserts it): PASSIVE, so it can only
            // ATTENUATE -- CCW full/dark, CW thin/bright, noon scooped ~1 kHz, peak ~250 Hz.
            // midDb 0: the old static post-clip notch AND the see-saw tone tilt were BOTH
            // removed in the tone-stack rework -- do NOT restore them. The Muff is the only
            // fuzz with a Tone knob (the Fuzz panel shows it only for this model).
            // bias 0 (symmetric clipping). MODERATE default / HIGH-gain ceiling: gMin 3 =
            // controllable crunch at low Sustain, gMax 55 + the inter-stage gain = the
            // full saturated wall + max sustain at the top. Calibration-referenced.
            {"Violet Ram", "Thick Sustain Fuzz",
             { 3, 3.0f, 55.0f,  70.0f,    0.0f, 0.0f, 0.7f, 1170.0f, 0.00f, 1000.0f, 1.38f, 0.0f, 0.0f,  0.0f, 700.0f, 0.0f, 0.0f,   0.0f, 0.0f, 0.0f, 0.0f, 2.0f, 1200.0f, 1780.0f}, false},
        };
        switch (cat)
        {
        case Kind::Boost:      count = 2; return boost;
        case Kind::Overdrive:  count = 4; return od;
        case Kind::Distortion: count = 1; return dist;
        case Kind::Fuzz:       count = 2; return fuzz;
        default:               count = 0; return nullptr;
        }
    }

    static int modelCount(Kind c) { int n = 0; modelsFor(c, n); return n; }
    static const char *modelName(Kind c, int m)
    { int n = 0; const Model *a = modelsFor(c, n); return (a && n > 0) ? a[juce::jlimit(0, n - 1, m)].name : ""; }
    static const char *modelSub(Kind c, int m)
    { int n = 0; const Model *a = modelsFor(c, n); return (a && n > 0) ? a[juce::jlimit(0, n - 1, m)].sub : ""; }
    static bool modelHasRange(Kind c, int m)
    { int n = 0; const Model *a = modelsFor(c, n); return a && n > 0 && a[juce::jlimit(0, n - 1, m)].hasRange; }
    static bool modelHasGate(Kind c, int m) // exposes the bias-starved gate toggle (fuzz)
    { return voicingFor(c, m).gate > 0.0f; }
    static bool modelHasMigrate(Kind c, int m) // exposes the RAT Tight/Full hump-migration toggle
    { return voicingFor(c, m).midMigrate > 0.0f; }

    static Voicing voicingFor(Kind c, int m)
    {
        int n = 0; const Model *a = modelsFor(c, n);
        if (!a || n == 0) return { 0, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.7f, 0.0f, 0.0f, 700.0f, 1.0f, 0.0f, 0.0f, 0.0f, 700.0f, 0.0f, 0.0f, 0.0f, 0.0f };
        return a[juce::jlimit(0, n - 1, m)].v;
    }
    static Voicing voicingFor(Kind c) { return voicingFor(c, 0); } // compat (model 0)

    // ---- authentic INPUT + OUTPUT stage per model (impedance loading + coupling caps) ----
    // The guitar is DI'd into the plugin through a ~1 MOhm Hi-Z interface, so the
    // pickup loading already happened at capture. We can only model the DELTA from
    // that reference: a low pedal input impedance DAMPS the pickup's resonant peak
    // (~2.5-3 kHz) and drops a little level; a high/buffered input barely loads it
    // (just the DC-blocking coupling high-passes). Anchors are FIT to the measured
    // circuits (ElectroSmash analyses + Lemme's "<=47 kOhm makes the resonant peak
    // vanish"). Applied via IoStage (front-end before the drive gain, back-end after
    // the level). Buffered voicings are audibly transparent; only the low-Z inputs
    // (Fuzz Face / Rangemaster / Big Muff) colour the tone, with a small TS/RAT damp.
    struct IoAnchors { float inHpHz, shelfHz, shelfCutDb, inLevelDb, inLpHz, outHpHz, outLevelDb; };

    static IoAnchors ioFor(Kind c, int model)
    {
        switch (c)
        {
        case Kind::Boost:
            // 0 Range '65 (Rangemaster): germanium common-emitter, Zin ~10-12 kOhm ->
            // STRONG loading (well below 47 kOhm, the peak is heavily damped). inHp 0:
            // the 5 nF input cap IS the voicing's 2653 Hz high-pass, don't double it.
            // It stays bright because its GAIN stage re-emphasises treble after the damp.
            if (model == 0) return { 0.0f, 2800.0f, -4.0f, -0.7f, 0.0f, 7.0f, 0.0f };
            // 1 Plex Boost (EP Booster): JFET, Zin ~2.2 MOhm, buffered out -> transparent.
            return { 4.0f, 0.0f, 0.0f, 0.0f, 0.0f, 7.0f, 0.0f };
        case Kind::Overdrive:
            // 0 Green Drive (TS808): BJT emitter-follower buffer, Zin 446 kOhm (ElectroSmash)
            // -- high but NOT a megohm, so a GENTLE resonance damp, not flat. Coupling ~8 Hz.
            if (model == 0) return { 8.0f, 3000.0f, -1.0f, -0.2f, 0.0f, 8.0f, 0.0f };
            // 1 Super Drive (SD-1): Boss BJT input buffer ~1 MOhm -> transparent.
            if (model == 1) return { 7.0f, 0.0f, 0.0f, 0.0f, 0.0f, 7.0f, 0.0f };
            // 2 Gold Horse (Klon): buffered bypass, high Zin -> transparent.
            if (model == 2) return { 4.0f, 0.0f, 0.0f, 0.0f, 0.0f, 7.0f, 0.0f };
            // 3 Breaker Drive (Bluesbreaker): TL072, R1 1 MOhm pulldown -> transparent.
            // inHp 0: the C1 10n / R1 1M ~16 Hz coupling IS the voicing's 20 Hz low-cut.
            return { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 7.0f, 0.0f };
        case Kind::Distortion:
            // 0 Black Rodent (RAT): NOT buffered -- the LM308 non-inverting input sees a
            // 1M||1M pulldown = Zin 494 kOhm (ElectroSmash). ~470 kOhm like the TS -> a
            // GENTLE damp, not flat. Coupling 22 nF / 1M = 7.2 Hz. Out = JFET follower.
            return { 7.0f, 3000.0f, -1.0f, -0.2f, 0.0f, 7.0f, 0.0f };
        case Kind::Fuzz:
            // 0 Round Fuzz (Fuzz Face): PNP common-emitter, Zin ~5-8 kOhm -> STRONG loading
            // (the resonant peak vanishes; "needs to see the pickup inductance"). Coupling
            // 2.2uF / 5k = 14 Hz; output cap / vol = ~31 Hz. Volume-cleanup deferred.
            if (model == 0) return { 14.0f, 2700.0f, -5.0f, -1.0f, 0.0f, 31.0f, 0.0f };
            // 1 Violet Ram (Big Muff): R2 39 kOhm series input -> "low input impedance,
            // tone sucking" (ElectroSmash); below 47 kOhm so the peak nearly vanishes ->
            // a real loading shelf (stronger than a mild buffer damp). Coupling ~3.8 Hz.
            return { 4.0f, 2800.0f, -4.0f, -0.6f, 0.0f, 4.0f, 0.0f };
        default:
            return { 8.0f, 0.0f, 0.0f, 0.0f, 0.0f, 8.0f, 0.0f };
        }
    }

    // Configure an IoStage from a model's anchors (loaded if there's a shelf cut,
    // else a near-transparent buffered front/back-end). IoStage must be prepared first.
    static void applyIo(IoStage &io, const IoAnchors &a)
    {
        if (a.shelfCutDb != 0.0f)
            io.setLoaded(a.inHpHz, a.shelfHz, a.shelfCutDb, a.inLevelDb, a.outHpHz, a.outLevelDb);
        else
            io.setBuffered(a.inHpHz, a.outHpHz, a.inLpHz, a.outLevelDb);
    }

    // ---- authentic Volume/Level POT per model (the output stage) ----
    // Every drive category EXCEPT Boost has a Volume/Level/Output knob (the real
    // Dallas Rangemaster + Echoplex EP-3 have no output pot, so Boost keeps its fixed
    // voicing makeup). On the real pedals the pot is an attenuator from a HOT internal
    // (clipped) signal down toward silence, so UNITY sits BELOW the top of the sweep and
    // the amount of clean boost ON TAP varies enormously: Bluesbreaker + Fuzz Face barely
    // reach unity even wide open, while Klon / RAT / Big Muff are very loud. The old design
    // was a symmetric +/-12 dB trim sitting on a loudness-matched outTrim -> every model
    // read ~+10..+13 dB at noon and the overdrives could not even reach unity (measured).
    //
    // We model the real pot as: a per-model UNITY MAKEUP (unityTrimDb) that cancels the
    // model's internal drive gain so the knob reads directly as output-vs-input dB, THEN a
    // pot curve (volPot) from silence (knob 0) through noonDb (knob 0.5, noon) up to maxDb
    // (full CW). `taper` = the lower-half pot law: 1 = LINEAR pot, ~2 = AUDIO/LOG pot (more
    // of the sweep spent fading to silence, so the usable range sits higher). Values are
    // calibrated to the MEASURED internal gain (unityTrimDb) + the researched real pots
    // (docs/drive/VOLUME_AUTHENTICITY_2026-07-04.md): TS/SD modest boost (unity ~11:00),
    // Klon/RAT/Muff loud with unity below noon + big boost on tap, Bluesbreaker/Fuzz Face
    // quiet with unity high (near max). STATIC per-model taper -- no reactive gain-riding.
    struct VolCurve { float unityTrimDb, noonDb, maxDb, taper; };

    static VolCurve volFor(Kind c, int model)
    {
        switch (c)
        {
        case Kind::Overdrive:
            if (model == 0) return { -11.7f,  2.0f,  9.0f, 2.0f }; // Green Drive (TS808): 100k AUDIO, modest boost, unity ~11:00
            if (model == 1) return { -11.4f,  2.0f,  9.0f, 1.0f }; // Super Drive (SD-1): 100k LINEAR, modest, unity ~11:00
            if (model == 2) return { -13.0f,  5.0f, 18.0f, 1.3f }; // Gold Horse (Klon): 10k lin (network-shaped), LOUD, big boost on tap
            return                 { -11.6f, -3.0f,  2.0f, 1.0f }; // Breaker Drive (Bluesbreaker): 100k LINEAR, low output, unity near max
        case Kind::Distortion:
            return                 { -16.6f,  5.0f, 16.0f, 2.0f }; // Black Rodent (RAT): 100k AUDIO, loud, unity ~10:00
        case Kind::Fuzz:
            if (model == 0) return { -10.9f, -2.0f,  2.0f, 2.0f }; // Round Fuzz (Fuzz Face): 500k AUDIO, deliberately quiet, unity near max
            return                 {  -7.1f,  5.0f, 18.0f, 2.0f }; // Violet Ram (Big Muff): 100k LOG, very loud on tap, unity low
        default:
            return                 {   0.0f,  0.0f,  0.0f, 1.0f }; // Boost: no volume knob (unused)
        }
    }

    // The pot curve: knob k in [0,1] -> a linear gain multiplier on the unity-referenced
    // signal. Silence at k=0, 10^(noonDb/20) at noon (0.5), 10^(maxDb/20) at full CW (1).
    // Top half is linear-in-dB (a gentle, musical boost on tap); the bottom half is a
    // power-law amplitude taper down to TRUE silence (taper 1 = linear pot, ~2 = audio pot
    // -> more sweep spent fading out, usable range higher). Continuous + monotonic at 0.5.
    static float volPot(float k, const VolCurve &vc)
    {
        k = k < 0.0f ? 0.0f : (k > 1.0f ? 1.0f : k);
        if (k >= 0.5f)
        {
            const float t = (k - 0.5f) * 2.0f;                 // 0..1 over the top half
            const float db = vc.noonDb + t * (vc.maxDb - vc.noonDb);
            return std::pow(10.0f, db * 0.05f);
        }
        const float noonLin = std::pow(10.0f, vc.noonDb * 0.05f);
        const float frac = k * 2.0f;                           // 0..1 over the bottom half
        return noonLin * std::pow(frac, vc.taper);
    }

    // Treble-boost input-cap switch: larger cap (Mid/Full) lets more low-end
    // through and shifts the emphasis down (Treble = bright, Full = fat).
    static void applyRange(Voicing &v, int rng)
    {
        // Input-cap mod: a single one-pole high-pass whose corner moves with the
        // cap. Stock 5nF -> 2.6 kHz; 10nF -> 1.3 kHz; 47nF -> 0.3 kHz. No peak.
        switch (rng)
        {
        case 1: v.lowCutHz = 1300.0f; break; // Mid  (10nF, fuller)
        case 2: v.lowCutHz =  300.0f; break; // Full (47nF, near full-range)
        default: v.lowCutHz = 2600.0f; break; // Treble (5nF stock)
        }
    }

    void prepare(const BlockContext &ctx) override
    {
        mSampleRate = ctx.sampleRate;
        reset();
        for (auto &s : mSlot) s.io.prepare(mSampleRate); // set IoStage sample rate (reconfigured per voicing on first process)
        mPrepared = true;
    }

    void reset() override
    {
        for (auto &s : mSlot)
            s.resetState();
    }

    void process(float *mono, int numSamples) override
    {
        if (!mPrepared)
            return;
        const double sr = mSampleRate;

        for (int si = 0; si < kSlots; ++si)
        {
            Slot &s = mSlot[si];
            const Kind k = (Kind)s.kind.load();
            if (k == Kind::Off || !s.on.load()) // footswitch off -> bypass this slot
                continue;
            const int model = s.model.load();
            Voicing v = voicingFor(k, model);
            const bool hasRange = modelHasRange(k, model);
            const int rng = hasRange ? s.range.load() : 0;
            if (hasRange)
                applyRange(v, rng);

            const int cfg = ((int)k * 16 + model) * 8 + rng; // reconfigure peak on type/model/range change
            if (cfg != s.lastKind)
            {
                s.lastKind = cfg;
                s.mid = (v.midDb != 0.0f) ? Biquad::peaking(sr, v.midHz, v.midQ, v.midDb)
                                          : Biquad::identity();
                if ((v.clip == 3 || v.clip == 4) && v.emphDb > 0.0f) // pre/de-emphasis pair (frequency-selective clip; clip 4 = SD-1)
                {
                    s.emphPre  = Biquad::highshelf(sr, v.emphHz,  v.emphDb);
                    s.emphPost = Biquad::highshelf(sr, v.emphHz, -v.emphDb);
                }
                else { s.emphPre = Biquad::identity(); s.emphPost = Biquad::identity(); }
                applyIo(s.io, ioFor(k, model)); // authentic input/output stage (impedance loading + coupling caps)
            }

            const float drv = s.drive.load();
            const float preGain = v.gMin * std::pow(v.gMax / v.gMin, drv); // log
            // How much of the pre-shaper EQ is engaged: static voicings use the
            // full EQ; for the overdrive the hump + bass-tighten scale with Drive.
            const float shapeAmt = 1.0f - v.shapeTrack * (1.0f - drv);
            // ProCo RAT hump MIGRATION: the LM308's gain-bandwidth product collapses the
            // closed-loop bandwidth as the Distortion pot climbs, so the pre-clip mid PEAK
            // slides DOWN with Drive. Re-derive the peaking biquad per block (state-
            // preserving via copyCoeffsFrom -> no click) at a log-interpolated corner; the
            // Tight/Full toggle picks the endpoints. Every other model has midMigrate 0 and
            // keeps the config-time fixed peak (byte-exact).
            if (v.midMigrate > 0.0f && v.midDb != 0.0f)
            {
                const bool full = s.migrateFull.load();
                const float fHi = full ? kRatHumpFullHi : kRatHumpTightHi;
                const float fLo = full ? kRatHumpFullLo : kRatHumpTightLo;
                const float peakHz = fHi * std::pow(fLo / fHi, drv); // log-interp by the Drive knob
                s.mid.copyCoeffsFrom(Biquad::peaking(sr, peakHz, v.midQ, v.midDb));
            }
            const float hpCoef = (v.lowCutHz > 0.0f) ? coefForHz(v.lowCutHz, sr) : 0.0f;
            const float lpCoef = (v.lpHz > 0.0f) ? coefForHz(v.lpHz, sr) : 0.0f;
            const bool useMid = (v.midDb != 0.0f);
            const bool midPost = (v.midPost > 0.5f);
            const bool useLp = (v.lpHz > 0.0f);
            const bool cubic = (v.clip == 3);
            const bool asymCubic = (v.clip == 4);         // asymmetric cubic fuzz (Round Fuzz)
            const bool cascade = (v.muffStages > 1.0f);   // Big Muff 2-stage soft-clip cascade
            const float millerCoef = (v.muffLpHz > 0.0f) ? coefForHz(v.muffLpHz, sr) : 0.0f; // input-booster Miller LP (pre clip 1)
            const float interMillerHz = (v.muffInterLpHz > 0.0f) ? v.muffInterLpHz : v.muffLpHz; // clip-1 Miller (pre clip 2): distinct higher corner
            const float interMillerCoef = (interMillerHz > 0.0f) ? coefForHz(interMillerHz, sr) : 0.0f;
            const bool softPoly = (cubic || asymCubic) && !cascade; // single-shaper poly path (cascade has its own branch)
            const double kn = asymCubic ? (1.0 - (double)v.bias) : 1.0; // clip-4 negative knee (asymmetry)
            const bool adaa2 = (v.adaa2 > 0.5f);          // 2nd-order ADAA for this clip (hard clip)
            const bool satAdaa2 = adaa2 && (v.clip == 0); // tanh-fit poly saturator on ADAA2 (Range '65)
            const bool useSlew = (v.slewMax > 0.0f);      // LM308 slew-rate limit (RAT hard-clip branch)
            const float slewStep = useSlew ? v.slewMax * (48000.0f / (float)sr) : 0.0f; // units/sample, 48k-referenced
            const bool ratTone = (v.toneFilterHz > 0.0f); // Tone = sweepable post-clip LP (RAT "Filter")
            const double asym = (v.clip == 2) ? (double)v.bias : 0.0;     // type-2 rail
            const double inBias = (v.clip == 2 || v.clip == 4) ? 0.0 : (double)v.bias; // type 0/1/3 input bias (4 = in-shaper asym)
            const bool useGate = (v.gate > 0.0f) && s.gateOn.load(); // model has a gate AND it's switched on
            // ---- Volume/Level knob -> output gain. Boost has NO level pot (the real
            // Rangemaster/EP-3 have none) so it keeps its fixed voicing makeup (outTrim);
            // every other category is an authentic per-model VOLUME POT (volFor/volPot):
            // a unity makeup that cancels the internal drive gain (so the knob reads as
            // output-vs-input) times the pot curve (silence -> noon -> max, per-model taper).
            float levelLin;
            if (k == Kind::Boost)
                levelLin = v.outTrim;                          // no volume knob (fixed makeup)
            else
            {
                const VolCurve vc = volFor(k, model);
                levelLin = std::pow(10.0f, vc.unityTrimDb * 0.05f) * volPot(s.level01.load(), vc);
            }

            const float tilt = (s.tone.load() - 0.5f) * 2.0f;
            // Active treble shelf (Klon): bass FIXED, treble swings asymmetrically
            // +trebleShelfDb (CW) .. -0.44*trebleShelfDb (CCW), noon = flat. Else the
            // legacy symmetric tilt (+/-kMaxTiltDb see-saw). Non-shelf path is byte-exact.
            const bool trebleShelf = (v.trebleShelfDb > 0.0f);
            const float trebleDb = trebleShelf
                ? (tilt >= 0.0f ? tilt * v.trebleShelfDb : tilt * v.trebleShelfDb * 0.44f)
                : (tilt * kMaxTiltDb);
            const float trebleG = std::pow(10.0f, trebleDb * 0.05f);
            const float bassG   = trebleShelf ? 1.0f : std::pow(10.0f, (-tilt * kMaxTiltDb) * 0.05f);
            // Klon active treble shelf = a PROPER 1st-order high-shelf, bilinear-discretised:
            // zero fixed at pivotHz, POLE at pivotHz*trebleG (rides up with the knob). This
            // keeps the passband flat even at full boost (the real Klon: +0.25 dB @ 100 Hz),
            // unlike the low/high blend which leaked ~+6 dB. noon (trebleG=1) -> exactly flat.
            float shB0 = 1.0f, shB1 = 0.0f, shA1 = 0.0f;
            if (trebleShelf)
            {
                const double aa = sr / (3.14159265358979323846 * (double)v.pivotHz); // 2*SR/wz
                const double bb = aa / (double)trebleG;                              // 2*SR/wp, wp=wz*G
                const double inv = 1.0 / (1.0 + bb);
                shB0 = (float)((1.0 + aa) * inv);
                shB1 = (float)((1.0 - aa) * inv);
                shA1 = (float)((1.0 - bb) * inv);
            }
            // RAT "Filter": one-pole LP whose corner sweeps from bright (tone 0, ~18 kHz
            // = effectively open) DOWN to v.toneFilterHz at full CW (tone 1) -> darker
            // clockwise, the opposite of the TS treble shelf. Else: the tilt pivot.
            float toneCoef;
            if (ratTone)
            {
                const float t = s.tone.load();
                const float corner = std::exp(std::log(18000.0f) * (1.0f - t) + std::log(v.toneFilterHz) * t);
                toneCoef = coefForHz(corner, sr);
            }
            else
                toneCoef = coefForHz(v.pivotHz, sr);

            // ---- passive Big Muff TONE STACK (cascade only): the REAL network, not a
            // tilt. A treble high-pass + bass low-pass blended by the Tone pot ->
            // PASSIVE (it can only attenuate, never boost), so CCW is full/dark, CW is
            // thin/bright, noon is scooped ~800 Hz, and the loudness gradient is gentle
            // (the old see-saw tilt actively boosted the lows +9 dB -> a huge, loud low
            // end CCW + a quiet CW; this fixes that). 2nd-order nodal solve (Rsrc 15k,
            // Ct 10n, Rt 47k, Cb 6.8n, pot 100k, load 100k -- values fit so the FULL
            // small-signal chain matches the real Muff: peak ~250 Hz, low-mid grind
            // present, scoop ~1 kHz) bilinear-discretised, coeffs recomputed per block
            // from the knob. Derivation: big_muff_response.py.
            float mtB0 = 1.0f, mtB1 = 0.0f, mtB2 = 0.0f, mtA1 = 0.0f, mtA2 = 0.0f;
            if (cascade)
            {
                const double tn = (double)s.tone.load();
                const double Rpt = (1.0 - tn) * 100000.0 + 1.0; // treble half of the 100k pot
                const double Rpb = tn * 100000.0 + 1.0;         // bass half
                const double b2 = 39950000.0 * Rpb;
                const double b1 = 125000000000.0 * Rpb + 125000000000.0 * Rpt + 5875000000000000.0;
                const double b0 = 12500000000000000000.0;
                const double a2 = 527.0 * Rpb * Rpt + 58692500.0 * Rpb + 52700000.0 * Rpt + 599250000000.0;
                const double a1 = 1250000.0 * Rpb * Rpt + 196450000000.0 * Rpb + 202500000000.0 * Rpt + 12026250000000000.0;
                const double a0 = 125000000000000.0 * Rpb + 20250000000000000000.0;
                const double K = 2.0 * sr;                  // bilinear s = K(1-z^-1)/(1+z^-1)
                const double d0 = a2 * K * K + a1 * K + a0;  // normaliser
                mtB0 = (float)((b2 * K * K + b1 * K + b0) / d0);
                mtB1 = (float)((-2.0 * b2 * K * K + 2.0 * b0) / d0);
                mtB2 = (float)((b2 * K * K - b1 * K + b0) / d0);
                mtA1 = (float)((-2.0 * a2 * K * K + 2.0 * a0) / d0);
                mtA2 = (float)((a2 * K * K - a1 * K + a0) / d0);
            }

            // envelope-follower coefficients (touch dynamics, clip 3)
            const float envAtk = 1.0f - (float)std::exp(-1.0 / (0.005 * sr)); // ~5 ms
            const float envRel = 1.0f - (float)std::exp(-1.0 / (0.120 * sr)); // ~120 ms
            const float invEnvRef = 1.0f / 0.25f; // picking-level reference
            const float gpkDecay = (float)std::exp(-1.0 / (0.5 * sr)); // gate peak-hold release ~500 ms
            // Fuzz Face bias sag (clip 4 + sagDepth): a SLOWER follower than the touch
            // env -- the bias network reacts over ~60 ms and recovers in ~250 ms (the
            // real bias caps are huge: 22 uF x 33 k ~ 0.7 s, so 60 ms is conservative).
            const bool useSag = asymCubic && (v.sagDepth > 0.0f);
            const float sagAtk = 1.0f - (float)std::exp(-1.0 / (0.060 * sr));
            const float sagRel = 1.0f - (float)std::exp(-1.0 / (0.250 * sr));

            s.io.processIn(mono, numSamples); // front-end: input coupling HP + impedance-loading shelf (colours what the drive sees)

            float hp = s.hp, lpz = s.lp, low = s.toneLp;
            float dcx = s.dcX1, dcy = s.dcY1;
            double x0 = s.x0, adx1 = s.adaaX1, adx2 = s.adaaX2;
            float env = s.env, gpk = s.gpk, sagEnv = s.sagEnv;
            float shx1 = s.shX1, shy1 = s.shY1;
            double adx1b = s.adaaX1b, adx2b = s.adaaX2b; // Big Muff stage-2 ADAA history
            float mLpPre = s.mLpPre, mHpInt = s.mHpInt, mLpInt = s.mLpInt; // cascade Miller LPs / inter HP
            float mtx1 = s.mtX1, mtx2 = s.mtX2, mty1 = s.mtY1, mty2 = s.mtY2; // Muff tone-stack biquad state
            float slewPrev = s.slewPrev; // LM308 slew limiter running output (RAT)

            for (int i = 0; i < numSamples; ++i)
            {
                const float xin = mono[i];
                float u = xin * preGain;

                if (hpCoef > 0.0f) { hp += hpCoef * (u - hp); const float hipassed = u - hp; u += shapeAmt * (hipassed - u); } // pre low-cut (drive-scaled)
                if (useMid && !midPost) { const float m = s.mid.processSample(u); u += shapeAmt * (m - u); } // pre-clip peak (treble booster)

                float c;
                if (cascade)
                {
                    // ---- Big Muff: TWO consecutive SOFT clips (cubic, 2nd-order ADAA each),
                    // with the Miller-cap low-pass BEFORE each stage. u already carries the
                    // pre-clip low-cut (lowCutHz, the ~80 Hz tighten) from the block above.
                    // Stage 1 -> a fixed inter-stage gain -> stage 2: the dense, compressed
                    // double-clip "wall". The post-clip mid SCOOP + tone tilt + top LP run
                    // in the shared post section below. ----
                    float s1 = u;
                    if (millerCoef > 0.0f) { mLpPre += millerCoef * (s1 - mLpPre); s1 = mLpPre; } // input-booster Miller LP
                    const double xb1 = (double)s1;
                    const double y1 = clipCubicADAA2(xb1, adx1, adx2);
                    adx2 = adx1; adx1 = xb1;

                    float s2 = (float)y1 * kMuffStage2Gain;
                    if (hpCoef > 0.0f) { mHpInt += hpCoef * (s2 - mHpInt); s2 = s2 - mHpInt; } // inter-stage HP (tighten)
                    if (interMillerCoef > 0.0f) { mLpInt += interMillerCoef * (s2 - mLpInt); s2 = mLpInt; } // clip-1 Miller LP (distinct ~1.78k corner)
                    const double xb2 = (double)s2;
                    const double y2 = clipCubicADAA2(xb2, adx1b, adx2b);
                    adx2b = adx1b; adx1b = xb2;
                    c = (float)y2;
                }
                else if (softPoly)
                {
                    // ---- polynomial soft clips (clip 3 cubic / clip 4 asym-cubic fuzz):
                    //   pre-emphasis -> 2nd-order ADAA -> de-emphasis -> envelope clean blend (+ gate) ----
                    const float clean = u / preGain; // INPUT-level clean (TS-correct). Blending the
                    // gained u summed a huge signal that the envelope ripple modulated -> crackle.
                    float aenv = std::abs(xin);
                    env += (aenv > env ? envAtk : envRel) * (aenv - env);
                    const float envN = clamp01(env * invEnvRef);
                    // Clean BASE tracks Drive when cleanBlendLo is set (Green Drive: the
                    // TS's always-unity dry leg shrinks in PROPORTION as the clipped
                    // component grows). cleanBlendLo 0 -> the flat base (byte-exact).
                    const float bBase = (v.cleanBlendLo > 0.0f)
                        ? v.cleanBlendLo + (v.cleanBlend - v.cleanBlendLo) * drv
                        : v.cleanBlend;
                    float bEff = bBase + v.dynDepth * (0.5f - envN); // soft picking -> more clean
                    bEff = bEff < 0.0f ? 0.0f : (bEff > 0.9f ? 0.9f : bEff);

                    // ---- Fuzz Face BIAS SAG (useSag): dig in past the calibrated picking
                    // reference and the negative knee closes over ~60 ms -> the slammed
                    // (rail-to-rail) note DIPS ~1 dB then recovers (~250 ms) as the bias
                    // network settles -- bias-sag compression, the attack-end complement
                    // of the decay-end gate. Threshold 60% of reference -> full sag at
                    // 160%, so soft/reference-level playing (and the voiced T30-T37
                    // behaviour) is untouched. kn varies SLOWLY vs the ADAA history (like
                    // a swept filter) -> the antiderivative mismatch is negligible.
                    // Floored so the shaper stays well-conditioned. ----
                    double knS = kn;
                    if (useSag)
                    {
                        sagEnv += (aenv > sagEnv ? sagAtk : sagRel) * (aenv - sagEnv);
                        const float over = clamp01(sagEnv * invEnvRef - 0.6f);
                        float knMul = 1.0f - v.sagDepth * over;
                        knS = kn * (double)(knMul < 0.25f ? 0.25f : knMul);
                    }
                    const double xb = (double)s.emphPre.processSample(u) + inBias;
                    const double y = asymCubic ? clipAsymCubicADAA2(xb, adx1, adx2, knS)
                                               : clipCubicADAA2(xb, adx1, adx2);
                    adx2 = adx1; adx1 = xb;
                    float cc = s.emphPost.processSample((float)y);
                    c = (1.0f - bEff) * cc + bEff * clean;

                    if (useGate)
                    {
                        // bias-starved gate ("velcro"/splat), RELATIVE to the note's own
                        // peak so it works at ANY input level (a hard OR soft strum both
                        // bloom, then choke as they decay past a fraction of their peak).
                        // An absolute threshold gated quiet/uncalibrated rigs all the time.
                        // gpk = peak-hold of the input env (instant attack, ~500 ms decay).
                        gpk = (aenv > gpk) ? aenv : gpk * gpkDecay;
                        const float ratio = env / (gpk + 1.0e-5f);      // ~1 at the attack, falls on decay
                        float gOpen = clamp01((ratio - 0.20f) * (1.0f / 0.35f)); // choke <20% of peak, open >55%
                        gOpen *= gOpen;                                 // sharper knee = the abrupt cut
                        c *= (1.0f - v.gate * (1.0f - gOpen));
                    }
                }
                else if (satAdaa2)
                {
                    // ---- soft saturator (tanh-fit odd poly) on 2nd-order ADAA: Range '65.
                    // The legacy 1st-order tanh aliased worst HERE of all models (everything
                    // the 2.65 kHz input-cap HP leaves is top-octave, gain up to 80). Same
                    // Parker/Bilbao kernel + peak guard as the other polys (kSat* below).
                    // Voicing preserved: unit small-signal gain, h1-h3 within ~0.4 dB of
                    // tanh; ceiling flat at 0.964 (=tanh(2)) vs tanh's creep to 1.0 ->
                    // <=0.3 dB extra squash on extreme peaks (if anything, more germanium). ----
                    const double xb = (double)u + inBias;
                    const double y = clipSatADAA2(xb, adx1, adx2);
                    adx2 = adx1; adx1 = xb;
                    c = (float)y;
                }
                else if (adaa2)
                {
                    // ---- hard clip on 2nd-order ADAA (peak-guarded): Black Rodent
                    // (RAT) and Gold Horse (Klon). The pre-clip EQ (low-cut + mid hump) is
                    // already in u, so mids hit the diodes hardest and bass clips least. ----
                    // LM308 SLEW LIMIT (RAT only, useSlew): the slow op-amp output can't move
                    // faster than slewStep/sample, so it rounds the transit THROUGH the diode-
                    // clamp band -> the hard clip's edges become ramps instead of vertical =
                    // the RAT's aggressive-but-not-buzzy grind (and large-signal HF loss when
                    // cranked). Applied to the op-amp output (u) before the diodes; the ceil
                    // bounds the swing so the slew ramps don't alias past what the ADAA fixes.
                    // The slew is a FOLLOWER of u (no feedback), so it can't wind up. Klon has
                    // slewMax 0 -> untouched (byte-exact).
                    if (useSlew)
                    {
                        float du = u - slewPrev;
                        if (du > slewStep) du = slewStep;
                        else if (du < -slewStep) du = -slewStep;
                        slewPrev += du;
                        if (slewPrev > kRatSlewCeil) slewPrev = kRatSlewCeil;
                        else if (slewPrev < -kRatSlewCeil) slewPrev = -kRatSlewCeil;
                        u = slewPrev;
                    }
                    const double xb = (double)u + inBias;
                    const double y = clipHardADAA2(xb, adx1, adx2);
                    adx2 = adx1; adx1 = xb;
                    float cc = (float)y;
                    // HEAVY parallel clean blend (the Klon "transparent" sum): mix the RAW
                    // full-range input back in -> restores the low end + dynamics that the
                    // mid-focused clipped path drops. Clean is at INPUT level (xin, NOT the
                    // gained signal -> no crackle). The envelope nudges it (touch: soft
                    // picking cleans up). With cleanBlend 0 AND dynDepth 0 this is a no-op,
                    // so Black Rodent stays byte-exact.
                    if (v.cleanBlend > 0.0f || v.dynDepth > 0.0f)
                    {
                        const float aenv = std::abs(xin);
                        env += (aenv > env ? envAtk : envRel) * (aenv - env);
                        const float envN = clamp01(env * invEnvRef);
                        // Klon DUAL-GANG balance: the clean/dirty BASE tracks the Drive knob
                        // (cleanBlendLo at min -> cleanBlend at max), then the envelope nudge
                        // rides on top. Models with cleanBlendLo 0 keep the flat cleanBlend
                        // base (byte-exact; RAT skips this branch entirely, cleanBlend 0).
                        const float bBase = (v.cleanBlendLo > 0.0f)
                            ? v.cleanBlendLo + (v.cleanBlend - v.cleanBlendLo) * drv
                            : v.cleanBlend;
                        float bEff = bBase + v.dynDepth * (0.5f - envN);
                        bEff = bEff < 0.0f ? 0.0f : (bEff > 0.9f ? 0.9f : bEff);
                        // The clipped path is bounded ~+/-1, so the RAW input clean (xin)
                        // must be scaled up to a comparable level or the "heavy" blend is
                        // inaudible. kCleanScale brings a nominal pick level (~0.2) up to
                        // the clip threshold so cleanBlend is meaningful = the Klon's clean
                        // sum. Fixed (NOT preGain) + bounded by the input -> no crackle.
                        const float clean = xin * kCleanScale;
                        c = (1.0f - bEff) * cc + bEff * clean;
                    }
                    else
                        c = cc;
                }
                else
                {
                    // ---- legacy shapers (tanh / hard / asym): 1st-order ADAA in DOUBLE ----
                    const double xb = (double)u + inBias;
                    const double d = xb - x0;
                    double y;
                    if (std::abs(d) > 1.0e-6)
                        y = (clipAD(v.clip, xb, asym) - clipAD(v.clip, x0, asym)) / d;
                    else
                        y = clipF(v.clip, 0.5 * (xb + x0), asym);
                    x0 = xb;
                    c = (float)y;
                }

                if (useMid && midPost) { const float m = s.mid.processSample(c); c += shapeAmt * (m - c); } // post-clip peak (OD/dist tone stack; level-stable)
                if (useLp) { lpz += lpCoef * (c - lpz); c += shapeAmt * (lpz - c); } // post low-pass (drive-scaled)

                // DC blocker (one-pole HPF ~4 Hz): removes the offset asymmetric
                // clipping introduces, so no static bias subtraction is needed.
                const float dcOut = c - dcx + kDcR * dcy;
                dcx = c; dcy = dcOut; c = dcOut;

                // tone. RAT "Filter": pure swept low-pass (darker CW). Else the tilt
                // (at tone=0.5 low+high == c = transparent; clip-3 fixes bass for a
                // TS-style treble shelf).
                low += toneCoef * (c - low);
                float toned;
                if (cascade)
                {
                    // passive Big Muff tone stack (the real network), Direct Form I
                    const float y = mtB0 * c + mtB1 * mtx1 + mtB2 * mtx2 - mtA1 * mty1 - mtA2 * mty2;
                    mtx2 = mtx1; mtx1 = c; mty2 = mty1; mty1 = y;
                    toned = y;
                }
                else if (ratTone)
                    toned = low; // the low-passed signal IS the output
                else if (trebleShelf)
                {
                    // proper 1st-order high-shelf (Klon): flat passband, treble boost/cut
                    const float sh = shB0 * c + shB1 * shx1 - shA1 * shy1;
                    shx1 = c; shy1 = sh; toned = sh;
                }
                else
                {
                    const float high = c - low;
                    const float bG = softPoly ? 1.0f : bassG;
                    toned = low * bG + high * trebleG;
                }
                const float outv = toned * levelLin;
                mono[i] = std::isfinite(outv) ? outv : 0.0f; // never emit NaN/Inf downstream
            }

            s.io.processOut(mono, numSamples); // back-end: output coupling HP + output level

            s.hp = flush(hp); s.lp = flush(lpz); s.toneLp = flush(low);
            s.dcX1 = flush(dcx); s.dcY1 = flush(dcy); s.x0 = flushD(x0);
            s.adaaX1 = flushD(adx1); s.adaaX2 = flushD(adx2); s.env = flush(env); s.gpk = flush(gpk);
            s.sagEnv = flush(sagEnv);
            s.shX1 = flush(shx1); s.shY1 = flush(shy1);
            s.adaaX1b = flushD(adx1b); s.adaaX2b = flushD(adx2b);
            s.mLpPre = flush(mLpPre); s.mHpInt = flush(mHpInt); s.mLpInt = flush(mLpInt);
            s.mtX1 = flush(mtx1); s.mtX2 = flush(mtx2); s.mtY1 = flush(mty1); s.mtY2 = flush(mty2);
            s.slewPrev = flush(slewPrev);
        }
    }

private:
    static constexpr float kMaxTiltDb = 9.0f;
    static constexpr float kDcR = 0.9995f; // DC blocker pole (~4 Hz corner @ 48k)
    static constexpr float kCleanScale = 3.0f; // raw-input clean -> clip-level gain (hard-clip clean blend, Klon).
                                               // Trimmed 3.5->3.0 when the blend went dual-gang: the higher
                                               // clean fraction at low Drive (cleanBlendLo 0.85) lifted peaks;
                                               // this keeps the level-constant clean leg within headroom (T50).
    static constexpr float kMuffStage2Gain = 2.0f; // fixed inter-stage gain into the Big Muff's
                                                   // 2nd soft clip (the real circuit's ~+25 dB stage;
                                                   // drives clip-1's output into clip-2's knee -> the
                                                   // dense, compressed double-clip "wall". Tuned so BOTH
                                                   // stages clip from just below noon while the Drive
                                                   // knob keeps a controllable floor + a high-gain/
                                                   // sustain ceiling at max — Robbie's moderate-default
                                                   // /hot-ceiling brief, instead of the always-pinned
                                                   // stock Muff)
    // ---- ProCo RAT hump-migration endpoints (Hz), log-interpolated by the Drive knob.
    // The peak slides from *Hi (Drive 0, LM308 wide-band) down to *Lo (Drive max, GBW
    // collapsed). Tight = a tasteful, bounded slide that keeps a mid honk cranked; Full
    // = the authentic collapse toward the real ~300 Hz. Both anchor near the fixed-voicing
    // 935 Hz honk around noon (geo-mean ~965 / ~852). Ear-tunable (Robbie play-tests). ----
    static constexpr float kRatHumpTightHi = 1500.0f, kRatHumpTightLo = 620.0f;
    static constexpr float kRatHumpFullHi  = 2200.0f, kRatHumpFullLo  = 330.0f;
    // LM308 slew: cap the op-amp output swing into the diodes (~rails, in clip-normalized
    // units) so the slew-limited ramps stay bounded -> no runaway aliasing the ADAA can't
    // catch. 1.5 = mildly-over-threshold (keeps hard-clip character, controls fold-back).
    static constexpr float kRatSlewCeil = 1.5f;

    struct Slot
    {
        std::atomic<int> kind{(int)Kind::Off};
        std::atomic<float> drive{0.5f};
        std::atomic<float> tone{0.5f};
        std::atomic<float> level01{0.5f}; // Volume/Level knob position 0..1 (noon default)
        std::atomic<int> range{0};
        std::atomic<int> model{0};
        std::atomic<bool> on{true};
        std::atomic<bool> gateOn{true}; // fuzz bias-starved gate enable (control, not DSP state)
        std::atomic<bool> migrateFull{false}; // RAT hump migration range: false Tight / true Full (control)
        float hp = 0.0f, lp = 0.0f, toneLp = 0.0f, dcX1 = 0.0f, dcY1 = 0.0f;
        double x0 = 0.0; // 1st-order ADAA history (double)
        double adaaX1 = 0.0, adaaX2 = 0.0; // 2nd-order ADAA history (cubic, double)
        float env = 0.0f; // envelope follower (touch dynamics)
        float gpk = 0.0f; // gate peak-hold (relative bias-starved gate)
        float sagEnv = 0.0f; // Fuzz Face bias-sag follower (slow, ~15/250 ms)
        float shX1 = 0.0f, shY1 = 0.0f; // Klon treble-shelf 1st-order filter state
        // Big Muff 2-stage cascade state (only used when muffStages>1):
        double adaaX1b = 0.0, adaaX2b = 0.0; // stage-2 cubic 2nd-order ADAA history
        float mLpPre = 0.0f, mHpInt = 0.0f, mLpInt = 0.0f; // pre-clip Miller LP, inter-stage HP, inter-stage Miller LP
        float mtX1 = 0.0f, mtX2 = 0.0f, mtY1 = 0.0f, mtY2 = 0.0f; // passive Muff tone-stack biquad (Direct Form I)
        float slewPrev = 0.0f; // LM308 slew-rate limiter running output (RAT)
        int lastKind = -1;
        Biquad mid;     // pre/post-shaper peak (state preserved across blocks)
        Biquad emphPre, emphPost; // pre/de-emphasis pair (clip 3)
        IoStage io;     // authentic input/output stage (impedance loading + coupling caps)
        void resetState()
        {
            hp = lp = toneLp = dcX1 = dcY1 = 0.0f; x0 = 0.0;
            adaaX1 = adaaX2 = 0.0; env = 0.0f; gpk = 0.0f; sagEnv = 0.0f; shX1 = shY1 = 0.0f;
            adaaX1b = adaaX2b = 0.0; mLpPre = mHpInt = mLpInt = 0.0f;
            mtX1 = mtX2 = mtY1 = mtY2 = 0.0f; slewPrev = 0.0f;
            lastKind = -1; mid.reset(); emphPre.reset(); emphPost.reset(); io.reset();
        }
    };

    Slot &at(int slot) { return mSlot[juce::jlimit(0, kSlots - 1, slot)]; }

    // ---- base shapers: f and its antiderivative F1 (for 1st-order ADAA), double ----
    // 0 soft (tanh): f=tanh, F1=logcosh.  1 hard +/-1: f=clamp, F1 piecewise.
    // 2 hard ASYM: positive rail +1, negative rail -(1-asym).
    static double clipF(int type, double x, double asym)
    {
        if (type == 0) return std::tanh(x);
        if (type == 1) return x > 1.0 ? 1.0 : (x < -1.0 ? -1.0 : x);
        // type 2 (fuzz): soft saturation on the positive half (tanh -> +1), hard
        // CUTOFF on the negative half (clamp to -lo). Different SHAPES per polarity
        // -> strong even harmonics at all levels (the vintage-fuzz character).
        const double lo = 1.0 - asym;
        return x >= 0.0 ? std::tanh(x) : (x < -lo ? -lo : x);
    }
    static double clipAD(int type, double x, double asym)
    {
        if (type == 0) return logCosh(x);
        if (type == 1) { const double a = std::abs(x); return a <= 1.0 ? 0.5 * x * x : a - 0.5; }
        const double lo = 1.0 - asym;
        if (x >= 0.0) return logCosh(x);
        return x < -lo ? (-lo * x - 0.5 * lo * lo) : 0.5 * x * x;
    }
    static double logCosh(double x)
    {
        const double a = std::abs(x);
        return a + std::log1p(std::exp(-2.0 * a)) - 0.6931471805599453; // log(cosh x)
    }

    // ---- cubic soft-clip (type 3): unit slope at 0, saturates at +/-2/3 ----
    //   f(x)  = x - x^3/3        (|x| <= 1),    sign(x)*2/3     (|x| > 1)
    //   F1(x) = x^2/2 - x^4/12   (|x| <= 1),    (2/3)|x| - 1/4  (|x| > 1)   [even]
    //   F2(x) = x^3/6 - x^5/60   (|x| <= 1),    s*((1/3)x^2 - |x|/4 + 1/15) [odd]
    static double cubF(double x)
    {
        if (x > 1.0) return 2.0 / 3.0;
        if (x < -1.0) return -2.0 / 3.0;
        return x - x * x * x / 3.0;
    }
    static double cubF1(double x)
    {
        const double a = std::abs(x);
        if (a <= 1.0) return 0.5 * x * x - x * x * x * x / 12.0;
        return (2.0 / 3.0) * a - 0.25;
    }
    static double cubF2(double x)
    {
        const double a = std::abs(x);
        if (a <= 1.0) return x * x * x / 6.0 - x * x * x * x * x / 60.0;
        const double s = x < 0.0 ? -1.0 : 1.0;
        return s * ((1.0 / 3.0) * a * a - 0.25 * a + 1.0 / 15.0);
    }
    // (F2(a)-F2(b))/(a-b) with the L'Hopital limit F1((a+b)/2) for a~=b.
    static double cubD(double a, double b)
    {
        const double d = a - b;
        if (std::abs(d) < 1.0e-5) return cubF1(0.5 * (a + b));
        return (cubF2(a) - cubF2(b)) / d;
    }
    // 2nd-order ADAA of the cubic (Parker/Bilbao). x newest, x1=x[n-1], x2=x[n-2].
    static double clipCubicADAA2(double x, double x1, double x2)
    {
        const double TOL = 1.0e-5;
        if (std::abs(x - x1) < TOL) // x ~= x[n-1]: degenerate, expand via F1/f
        {
            const double xBar = 0.5 * (x + x2);
            const double delta = xBar - x1;
            if (std::abs(delta) < TOL)
                return cubF(0.5 * (xBar + x1));
            return (2.0 / delta) * (cubF1(xBar) + (cubF2(x1) - cubF2(xBar)) / delta);
        }
        // x ~= x[n-2] but NOT ~= x[n-1]: the OUTER denominator (x - x2) collapses
        // while x1 sits apart — the near-Nyquist alternation x[n]==x[n-2]!=x[n-1].
        // The |x-x1| guard above never catches this, so without a fallback the
        // 2/(x-x2) below is a divide-by-zero -> Inf/NaN that then poisons every
        // downstream block's state (amp engine, cab convolver). Fall back to the
        // well-conditioned 1st-order ADAA over the current step.
        if (std::abs(x - x2) < TOL)
            return (cubF1(x) - cubF1(x1)) / (x - x1); // proper 1st-order ADAA (cubD used F2 -> wrong scale)
        return (2.0 / (x - x2)) * (cubD(x, x1) - cubD(x1, x2));
    }

    // ---- asymmetric cubic soft-clip (type 4, Round Fuzz) ----
    // The germanium fuzz clips ASYMMETRICALLY at ALL gains (a cold-biased stage:
    // one semicycle swings further than the other), so a symmetric shaper + DC bias
    // won't do -- at high gain a biased odd shaper just squares up symmetrically.
    // This shape has the POSITIVE knee at 1 (rail +2/3, the plain cubic) and the
    // NEGATIVE knee at kn = 1 - bias (rail -(2/3)kn): the negative half saturates
    // sooner, so the asymmetry PERSISTS into hard clipping (soft for small signals,
    // a tilted square for big ones -- the Fuzz Face signature). Polynomial -> exact
    // F1/F2 -> cheap 2nd-order ADAA (no dilogarithm). bias=0 (kn=1) == the cubic.
    static double asymF(double x, double kn)
    {
        if (x >= 0.0) return x > 1.0 ? 2.0 / 3.0 : x - x * x * x / 3.0;
        return x < -kn ? -(2.0 / 3.0) * kn : x - x * x * x / (3.0 * kn * kn);
    }
    static double asymF1(double x, double kn) // antiderivative (1st-order ADAA)
    {
        if (x >= 0.0) { if (x <= 1.0) return 0.5 * x * x - x * x * x * x / 12.0; return 5.0 / 12.0 + (2.0 / 3.0) * (x - 1.0); }
        if (x >= -kn) return 0.5 * x * x - x * x * x * x / (12.0 * kn * kn);
        const double Ln = (2.0 / 3.0) * kn; return (5.0 / 12.0) * kn * kn - Ln * (x + kn);
    }
    static double asymF2(double x, double kn) // 2nd antiderivative (2nd-order ADAA)
    {
        if (x >= 0.0) { if (x <= 1.0) return x * x * x / 6.0 - x * x * x * x * x / 60.0; const double t = x - 1.0; return 3.0 / 20.0 + (5.0 / 12.0) * t + (1.0 / 3.0) * t * t; }
        if (x >= -kn) return x * x * x / 6.0 - x * x * x * x * x / (60.0 * kn * kn);
        const double Ln = (2.0 / 3.0) * kn, t = x + kn; return -3.0 * kn * kn * kn / 20.0 + (5.0 / 12.0) * kn * kn * t - Ln * t * t / 2.0;
    }
    static double asymD(double a, double b, double kn) // (F2(a)-F2(b))/(a-b), L'Hopital -> F1(mid)
    {
        const double d = a - b;
        if (std::abs(d) < 1.0e-5) return asymF1(0.5 * (a + b), kn);
        return (asymF2(a, kn) - asymF2(b, kn)) / d;
    }
    // Same Parker/Bilbao 2nd-order kernel + the SAME peak guard as the cubic.
    static double clipAsymCubicADAA2(double x, double x1, double x2, double kn)
    {
        const double TOL = 1.0e-5;
        if (std::abs(x - x1) < TOL)
        {
            const double xBar = 0.5 * (x + x2);
            const double delta = xBar - x1;
            if (std::abs(delta) < TOL)
                return asymF(0.5 * (xBar + x1), kn);
            return (2.0 / delta) * (asymF1(xBar, kn) + (asymF2(x1, kn) - asymF2(xBar, kn)) / delta);
        }
        if (std::abs(x - x2) < TOL)
            return (asymF1(x, kn) - asymF1(x1, kn)) / (x - x1); // peak guard: 1st-order over the step
        return (2.0 / (x - x2)) * (asymD(x, x1, kn) - asymD(x1, x2, kn));
    }

    // ---- hard clip (type 1) 2nd-order ADAA (Black Rodent) ----
    // Hard clipping is the harshest shaper (square corners -> the most fold-back),
    // so it benefits most from 2nd-order. The clamp is piecewise-polynomial, so F1
    // and F2 are exact closed forms (no dilogarithm) -> ADAA2 is as cheap as the
    // cubic's. f=clamp(x,-1,1):
    //   F1(x) = x^2/2                 (|x|<=1),  |x|-1/2                 (|x|>1)  [even]
    //   F2(x) = x^3/6                 (|x|<=1),  s*(a^2/2 - a/2 + 1/6)   (|x|>1)  [odd]
    static double hardF(double x) { return x > 1.0 ? 1.0 : (x < -1.0 ? -1.0 : x); }
    static double hardF1(double x) { const double a = std::abs(x); return a <= 1.0 ? 0.5 * x * x : a - 0.5; }
    static double hardF2(double x)
    {
        const double a = std::abs(x);
        if (a <= 1.0) return x * x * x / 6.0;
        const double s = x < 0.0 ? -1.0 : 1.0;
        return s * (0.5 * a * a - 0.5 * a + 1.0 / 6.0); // continuous: both branches = 1/6 at |x|=1
    }
    static double hardD(double a, double b) // (F2(a)-F2(b))/(a-b), L'Hopital -> F1(mid)
    {
        const double d = a - b;
        if (std::abs(d) < 1.0e-5) return hardF1(0.5 * (a + b));
        return (hardF2(a) - hardF2(b)) / d;
    }
    // Same Parker/Bilbao 2nd-order kernel + the SAME peak guard as the cubic: the
    // x[n]==x[n-2]!=x[n-1] alternation at signal peaks would divide by zero, so fall
    // back to well-conditioned 1st-order ADAA over the step (F1, not F2).
    static double clipHardADAA2(double x, double x1, double x2)
    {
        const double TOL = 1.0e-5;
        if (std::abs(x - x1) < TOL)
        {
            const double xBar = 0.5 * (x + x2);
            const double delta = xBar - x1;
            if (std::abs(delta) < TOL)
                return hardF(0.5 * (xBar + x1));
            return (2.0 / delta) * (hardF1(xBar) + (hardF2(x1) - hardF2(xBar)) / delta);
        }
        if (std::abs(x - x2) < TOL)
            return (hardF1(x) - hardF1(x1)) / (x - x1); // peak guard: proper 1st-order over the step
        return (2.0 / (x - x2)) * (hardD(x, x1) - hardD(x1, x2));
    }

    // ---- soft saturator (type 0 + v.adaa2, Range '65): tanh-FIT odd 7th-order poly ----
    // s(x) = x + c3 x^3 + c5 x^5 + c7 x^7 on |x| <= A, +/-L beyond (constrained LSQ fit
    // to tanh on [0,2]: c1 pinned to 1 -> unit small-signal gain, s(A)=tanh(2) and
    // s'(A)=0 -> C1 join, max fit error 0.6%, monotone). Polynomial -> exact closed-form
    // F1/F2 (the constants keep both continuous at |x|=A, like hardF2's 1/6) -> the same
    // cheap Parker/Bilbao 2nd-order ADAA + peak guard as the cubic/hard paths. Fit:
    // docs/drive/rangemaster_sat_fit.py. Replaces the legacy 1st-order tanh for the
    // treble booster; measured 13 kHz fold (5 kHz probe, max Drive) -18 dB.
    static constexpr double kSatC3 = -0.292616402, kSatC5 = 0.064248717, kSatC7 = -0.005867189;
    static constexpr double kSatA   = 2.0;
    static constexpr double kSatL   = kSatA + kSatC3 * 8.0 + kSatC5 * 32.0 + kSatC7 * 128.0;            // s(A) = 0.964028 (tanh 2)
    static constexpr double kSatF1A = 2.0 + kSatC3 * 4.0 + kSatC5 * (64.0 / 6.0) + kSatC7 * 32.0;       // F1(A)
    static constexpr double kSatF2A = 8.0 / 6.0 + kSatC3 * 1.6 + kSatC5 * (128.0 / 42.0) + kSatC7 * (512.0 / 72.0); // F2(A)
    static double satF(double x)
    {
        if (std::abs(x) <= kSatA) { const double x2 = x * x; return x * (1.0 + x2 * (kSatC3 + x2 * (kSatC5 + x2 * kSatC7))); }
        return x < 0.0 ? -kSatL : kSatL;
    }
    static double satF1(double x) // antiderivative (even)
    {
        const double a = std::abs(x);
        if (a <= kSatA) { const double x2 = x * x; return x2 * (0.5 + x2 * (kSatC3 / 4.0 + x2 * (kSatC5 / 6.0 + x2 * (kSatC7 / 8.0)))); }
        return kSatF1A + kSatL * (a - kSatA);
    }
    static double satF2(double x) // 2nd antiderivative (odd)
    {
        const double a = std::abs(x);
        if (a <= kSatA) { const double x2 = x * x; return x * x2 * (1.0 / 6.0 + x2 * (kSatC3 / 20.0 + x2 * (kSatC5 / 42.0 + x2 * (kSatC7 / 72.0)))); }
        const double sg = x < 0.0 ? -1.0 : 1.0, t = a - kSatA;
        return sg * (kSatF2A + kSatF1A * t + 0.5 * kSatL * t * t);
    }
    static double satD(double a, double b) // (F2(a)-F2(b))/(a-b), L'Hopital -> F1(mid)
    {
        const double d = a - b;
        if (std::abs(d) < 1.0e-5) return satF1(0.5 * (a + b));
        return (satF2(a) - satF2(b)) / d;
    }
    // Same Parker/Bilbao 2nd-order kernel + the SAME peak guard as the cubic/hard clips.
    static double clipSatADAA2(double x, double x1, double x2)
    {
        const double TOL = 1.0e-5;
        if (std::abs(x - x1) < TOL)
        {
            const double xBar = 0.5 * (x + x2);
            const double delta = xBar - x1;
            if (std::abs(delta) < TOL)
                return satF(0.5 * (xBar + x1));
            return (2.0 / delta) * (satF1(xBar) + (satF2(x1) - satF2(xBar)) / delta);
        }
        if (std::abs(x - x2) < TOL)
            return (satF1(x) - satF1(x1)) / (x - x1); // peak guard: proper 1st-order over the step
        return (2.0 / (x - x2)) * (satD(x, x1) - satD(x1, x2));
    }

    static float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
    static float coefForHz(double hz, double sr)
    {
        return 1.0f - (float)std::exp(-2.0 * 3.14159265358979323846 * hz / sr);
    }
    // Flush denormals AND non-finite (NaN/Inf) to 0. The non-finite catch is the
    // safety net: a single bad sample must never be latched into a state variable
    // and carried forward forever (which silently bricks the rig until reset).
    static float flush(float v) { return (std::isfinite(v) && std::abs(v) >= 1.0e-30f) ? v : 0.0f; }
    static double flushD(double v) { return std::isfinite(v) ? v : 0.0; }

    Slot mSlot[kSlots];
    double mSampleRate = 48000.0;
    bool mPrepared = false;
};

} // namespace nam_rig
