#pragma once
// DriveBlock — a 3-slot SERIES rack of drive pedals (shared pre, before the
// A/B split: one board feeding both rigs, the common real two-amp live rig).
//
// Voicings are tuned to the MEASURED behaviour of the classic circuits they
// model (from published circuit analyses + measured frequency responses):
//   Off          : out of the path — bit-exact passthrough.
//   Boost        : germanium treble booster ("Range '65") / FET clean boost
//                  ("EP Boost"). Range '65 = a one-pole input-cap high-pass
//                  (the 3-way switch moves the corner) + soft germanium clip.
//                  FOUR models: 0 "Range '65" (the original stand-in, kept
//                  byte-for-byte), 1 "EP Boost" (original stand-in), 2 "Range '65 II"
//                  = the circuit-fit Dallas Rangemaster, 3 "EP Boost II" = the
//                  circuit-fit Echoplex EP-3 / Xotic EP Booster (see below).
//   EP Boost II  : the Maestro Echoplex EP-3 preamp (single JFET common-source) as the
//                  Xotic EP Booster. The pure EP-3 stage is ~FLAT across audio (fit
//                  ep3_response.py) -- the character is clean headroom + very high
//                  input Z + JFET 2nd-harmonic, FULL-RANGE (opposite of the
//                  Rangemaster). Voiced as the EP Booster: a gentle broad presence
//                  high-shelf (low-Q peak ~5 kHz, +4 dB) + full low end + a small JFET
//                  bias for warmth. High headroom: mostly clean, a little hair maxed.
//   Range '65 II : the Dallas Rangemaster (OC44 germanium common-emitter), FIT to
//                  the schematic (rangemaster_response.py): the whole audio-band
//                  voicing is the 5nF input cap into the ~12k input impedance = a
//                  1st-order high-pass at ~2.65 kHz, flat above (it is a TREBLE
//                  booster -- output stays bright, no top roll). The high-pass sits
//                  PRE-clip so bass clips LEAST. Real gain Gv = gm*Rc ~ 80 (38 dB) at
//                  full Volume (gMax 80, vs the stand-in's 20). Soft germanium clip
//                  (tanh) with an off-centre bias -> asymmetric even-harmonic warmth
//                  + soft compression when strummed hard.
//   Green Drive  : a green-box mid-hump overdrive (TS-style). FOUR models:
//                  model 0 "Green Drive" = the original memoryless tanh voicing;
//                  model 1 "Green Drive II" = the reworked feedback-clip model
//                  (see below); model 2 "Super Drive" = the Boss SD-1 (see below);
//                  model 3 "Gold Horse" = the Klon Centaur (see below). 0-2 share
//                  the ~720 Hz mid hump; the Klon's is a broader ~1 kHz band-pass.
//   Super Drive  : the Boss SD-1 Super Overdrive (OD-1 lineage). Small-signal
//                  voicing ~= the TS808 (fit to the schematic, RMS 0.63 dB), but
//                  the identity is ASYMMETRIC clipping: 3 diodes (2+1) = a ~2:1
//                  threshold ratio -> persistent even harmonics (a 2nd-harmonic
//                  "crunch"). Built on clip type 4 (asym cubic) so the asymmetry
//                  survives at high gain; bias 0.35 (kn 0.65) = the soft feedback
//                  knee softened from the literal 2:1. Noticeably hotter than GD2
//                  (gMin 6/gMax 120) + a touch more output. Same feedback-clip
//                  pre/de-emphasis feel as GD2. See docs/drive/sd1.md.
//   Gold Horse   : the Klon Centaur (TL072 + germanium diodes-to-ground). NOT a TS:
//                  a ~1 kHz BAND-PASS op-amp gain stage, SYMMETRIC germanium HARD clip
//                  (clip 1 + 2nd-order ADAA), SUMMED with a big parallel CLEAN path =
//                  the "transparent overdrive". Modelled as a heavy clean blend taken
//                  from the RAW input (restores the lows the mid-focused clip drops) +
//                  shapeTrack bloom (near-clean boost at low Drive). Bright/open top,
//                  lots of output (also a boost). See docs/drive/klon.md.
//   Black Rodent : a hard-clip distortion (ProCo RAT). TWO models:
//                  model 0 "Black Rodent" = the original simple hard-clip stand-in;
//                  model 1 "Black Rodent II" = the circuit-fit RAT (see below). Both
//                  are symmetric HARD clips; II adds the LM308 gain-stage voicing,
//                  2nd-order ADAA and the "Filter" tone.
//   Black Rodent II: the LM308 clipper amp's bass-cut + ~935 Hz hump + top roll-off
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
// The cubic soft-clip (Green Drive II) and the hard clip (Black Rodent II) can run
// 2nd-order ADAA (Parker/Bilbao): from F2, three samples, with L'Hopital fallbacks
// + a peak guard — markedly less fizz. Selected per-voicing (clip 3 always; clip 1
// when v.adaa2 is set), so model 0 keeps its byte-exact 1st-order path.
// Zero latency, all-Off rack bit-exact.
//
// Base shapers:
//   0 soft (tanh)            — Treble Boost / Overdrive v1 (asymmetry via bias)
//   1 hard clip +/-1 (sym)   — Distortion (1st-order ADAA, or 2nd-order if v.adaa2)
//   2 hard clip, ASYM rails  — Fuzz (positive rail +1, negative rail -(1-bias))
//   3 cubic soft (poly)      — Overdrive v2 (cheap F1+F2 -> 2nd-order ADAA)
//
// Green Drive II authentic-TS extras (clip 3 only, all zero-latency):
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
//   * LIFELIKE gain range (gMin 5 -> gMax 80, ~the real TS's 12..118): the clip
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
    void setLevelDb(int slot, float v) { at(slot).levelDb.store(v); }
    void setAutoGain(bool on) { mAutoGain.store(on); } // level-compensate Drive/Tone (default off)
    void setRange(int slot, int r) { at(slot).range.store(r); } // treble-boost cap switch: 0 Treble/1 Mid/2 Full
    void setOn(int slot, bool on) { at(slot).on.store(on); }            // footswitch (default on)
    void setModel(int slot, int m) { at(slot).model.store(m); }         // model within the category
    void setGateOn(int slot, bool on) { at(slot).gateOn.store(on); }    // fuzz bias-starved gate enable (default on)

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
        float muffLpHz;      // the Miller-cap low-pass applied BEFORE EACH cubic clip stage
                             //     (the Muff's dark, smooth, no-fizz voice: clipping a
                             //     low-passed signal sounds smoother). Only used when muffStages>1.
    };

    // A specific pedal MODEL inside a category (Type). A category can hold several
    // models; the UI picks the Type (category) then the model. hasRange = the
    // model exposes the 3-way input-cap switch (treble booster only).
    struct Model { const char *name, *sub; Voicing v; bool hasRange; };

    static const Model *modelsFor(Kind cat, int &count)
    {                       // clip  gMin    gMax  lowCut   midHz  midDb midQ   lpHz   bias   pivot   outTrim shp post  emphDb emphHz clean  dyn   toneF  adaa2
        static const Model boost[] = {
            {"Range '65", "germanium treble boost",
             { 0, 2.0f, 20.0f, 2600.0f,   0.0f, 0.0f, 0.7f,    0.0f, 0.20f, 2500.0f, 0.95f, 0.0f, 0.0f,  0.0f, 700.0f, 0.0f, 0.0f,   0.0f, 0.0f}, true},
            {"EP Boost", "FET clean boost",
             { 0, 1.0f,  6.0f,  40.0f, 5000.0f, 3.0f, 0.6f,    0.0f, 0.05f, 1000.0f, 0.95f, 0.0f, 1.0f,  0.0f, 700.0f, 0.0f, 0.0f,   0.0f, 0.0f}, false},
            // model 2: circuit-fit Dallas Rangemaster (OC44 germanium, common-emitter).
            // The voicing is a single 1st-order high-pass = the 5nF input cap into the
            // ~12k input impedance, fc ~2.65 kHz, FLAT above (no peak / no top roll) --
            // FIT to the schematic (docs/drive/rangemaster_response.py, RMS 0.01 dB). The
            // Range switch is the input-cap mod (5/10/47 nF -> 2653/1326/282 Hz via
            // applyRange). The high-pass sits PRE-clip so bass clips LEAST and the
            // full-gain treble clips most (the booster's frequency-selective grind).
            // Lifelike gain range gMin 4 -> gMax 80 = the real Gv = gm*Rc ~ 80 (38 dB) at
            // full Volume (the stand-in's gMax 20 was ~4x too low, like the early TS).
            // Soft germanium clip (tanh) with an OFF-CENTRE bias (the Rangemaster's
            // deliberately asymmetric operating point) -> even-harmonic warmth + soft
            // compression when strummed hard. Static (shapeTrack 0): the input-cap
            // network is fixed, so it shapes even at Drive 0. Calibration-referenced.
            {"Range '65 II", "Dallas Rangemaster (OC44 germanium)",
             { 0, 4.0f, 80.0f, 2653.0f,   0.0f, 0.0f, 0.7f,    0.0f, 0.30f, 2500.0f, 0.50f, 0.0f, 0.0f,  0.0f, 700.0f, 0.0f, 0.0f,   0.0f, 0.0f}, true},
            // model 3: Echoplex EP-3 preamp / Xotic EP Booster (single JFET common-source).
            // The PURE EP-3 stage measures essentially FLAT across the audio band (the
            // Cin/Rgate HPF sits ~3 Hz, the source is unbypassed, the 220 pF roll is
            // >70 kHz) -- its magic is clean headroom + very high input Z + subtle JFET
            // 2nd-harmonic, NOT an EQ. It is FULL-RANGE (the opposite of the Rangemaster's
            // treble-only high-pass). "EP Boost" as a pedal = the Xotic EP Booster, which
            // adds gentle TONE-SHAPING: a broad presence high-shelf + full low end. We fit
            // our low-Q pre-shaper peak (midHz 5000, Q 0.35, +4 dB) to that gentle shelf
            // (docs/drive/ep3_response.py, RMS 0.35 dB), low-cut at ~15 Hz (full bass).
            // High-headroom CLEAN soft (tanh) clip with a small off-centre bias for the
            // JFET even-harmonic warmth -> mostly clean, a little hair only when cranked.
            {"EP Boost II", "Echoplex EP-3 / Xotic EP Booster (JFET)",
             { 0, 1.3f,  6.0f,  15.0f, 5000.0f, 4.0f, 0.35f,   0.0f, 0.10f, 1200.0f, 0.74f, 0.0f, 1.0f,  0.0f, 700.0f, 0.0f, 0.0f,   0.0f, 0.0f}, false},
        };
        static const Model od[] = {
            {"Green Drive", "mid-hump overdrive (v1 tanh)",
             { 0, 1.5f, 30.0f, 560.0f,  780.0f, 6.0f, 0.7f, 1300.0f, 0.05f,  720.0f, 1.10f, 1.0f, 1.0f,  0.0f, 700.0f, 0.00f, 0.0f,  0.0f, 0.0f}, false},
            {"Green Drive II", "feedback-clip overdrive (v2)",
             { 3, 5.0f, 80.0f, 220.0f,  820.0f, 3.6f, 0.7f, 1900.0f, 0.00f, 1200.0f, 1.15f, 0.0f, 1.0f,  9.0f, 700.0f, 0.20f, 0.40f, 0.0f, 0.0f}, false},
            // model 2: circuit-fit Boss SD-1 Super Overdrive (OD-1 lineage, uPC4558
            // feedback-clip). Small-signal voicing is ~the TS808 (fit sd1_response.py,
            // RMS 0.63 dB: same ~+5 dB hump @ 720-900 Hz, slightly fuller bass / a hair
            // brighter -- the SD-1's "more open" reputation, confirmed by the circuit).
            // The IDENTITY is ASYMMETRIC clipping (3 diodes, 2+1 -> a ~2:1 threshold
            // ratio): clip type 4 (asym cubic) so the even-harmonic crunch PERSISTS at
            // gain (a symmetric clip + DC bias would just square up symmetrically). Real
            // ratio 2:1 = bias 0.50, softened to bias 0.35 (kn 0.65) for the diodes'
            // soft FEEDBACK-loop knee -- clearly asymmetric (h2/h1 ~0.04-0.075), milder
            // than the fuzz. Noticeably hotter than GD2 (gMin 6/gMax 120, the 1M drive
            // pot + 0.9V 1S2473 diodes) + a touch more output (outTrim 1.25). Same
            // feedback-clip feel as GD2: pre/de-emphasis (bass clips least), small clean
            // blend + touch dynamics. Static (shapeTrack 0), mid post-clip, calibrated.
            {"Super Drive", "Boss SD-1 (asymmetric overdrive)",
             { 4, 6.0f,120.0f, 160.0f,  900.0f, 5.0f, 0.5f, 2000.0f, 0.35f, 1200.0f, 1.25f, 0.0f, 1.0f, 10.0f, 700.0f, 0.15f, 0.40f, 0.0f, 0.0f}, false},
            // model 3: circuit-fit Klon Centaur (TL072 + germanium diodes-to-ground).
            // NOT a TS: the op-amp gain stage is a ~1 kHz BAND-PASS (fit klon_response.py,
            // RMS 0.14 dB) clipped by SYMMETRIC germanium (hard, clip 1 + 2nd-order ADAA),
            // then SUMMED with a big parallel CLEAN feedforward -> the "transparent
            // overdrive". We model the clean sum as a HEAVY clean blend taken from the RAW
            // input (cleanBlend 0.50 + dynDepth 0.30): the mid-focused clipped path drops
            // the lows (lowCut 210, hump 980) and the full-range clean restores them ->
            // big open low end + dynamics. shapeTrack 1 = the hump BLOOMS with Drive, so
            // at low Drive it is a near-clean boost (the Klon reputation), distorting more
            // as Drive climbs. Bright/open top (lpHz 4700, the 27V headroom feel). Modest
            // gMin (genuinely clean min), moderate gMax (~the real 40 dB), lots of output
            // (outTrim -- it is also a boost). Tone = treble tilt ~450 Hz (the active
            // treble-shelf corner ~408 Hz, approximated by the engine tilt). Calibrated.
            // tone = ACTIVE treble shelf (trebleShelfDb 18 @ pivot 408 Hz): the real
            // Klon high-shelf (bass fixed, +18/-8 dB), noon = flat. NOT the engine tilt.
            {"Gold Horse", "Klon Centaur (transparent overdrive)",
             { 1, 2.0f, 70.0f, 210.0f,  980.0f, 3.2f, 0.3f, 4700.0f, 0.00f,  408.0f, 0.95f, 1.0f, 0.0f,  0.0f, 700.0f, 0.50f, 0.30f, 0.0f, 1.0f, 0.0f, 18.0f}, false},
        };
        static const Model dist[] = {
            // model 0: the original simple hard-clip stand-in (kept byte-for-byte for A/B).
            {"Black Rodent", "hard-clip distortion",
             { 1, 2.0f,160.0f, 300.0f, 1000.0f, 4.0f, 0.6f, 5000.0f, 0.00f, 1500.0f, 0.44f, 1.0f, 1.0f,  0.0f, 700.0f, 0.0f, 0.0f,   0.0f, 0.0f}, false},
            // model 1: circuit-fit ProCo RAT. Pre-clip gain-stage EQ (gentle low-cut +
            // ~935 Hz hump + top roll) FIT to the LM308 stage at a ~1 kHz-hump Distortion
            // setting (docs/drive/proco_rat_response.py, RMS 0.03 dB), bloomed with Drive
            // (shapeTrack 1, PRE-clip so bass clips LEAST). Symmetric HARD clip (silicon
            // diodes to ground) on 2nd-order ADAA. Tone = the RAT "Filter" sweepable
            // low-pass (darker CW). Hot, calibration-referenced range (LM308 Gv up to ~2300).
            {"Black Rodent II", "ProCo RAT (LM308, diodes-to-ground)",
             { 1, 4.0f,150.0f,  62.0f,  935.0f,17.0f, 0.5f, 4800.0f, 0.00f, 1500.0f, 0.47f, 1.0f, 0.0f,  0.0f, 700.0f, 0.0f, 0.0f, 475.0f, 1.0f}, false},
        };
        static const Model fuzz[] = {
            {"Round Fuzz", "germanium fuzz",
             { 2, 6.0f,300.0f,  40.0f,    0.0f, 0.0f, 0.7f,    0.0f, 0.45f,  700.0f, 0.45f, 0.0f, 0.0f,  0.0f, 700.0f, 0.0f, 0.0f,   0.0f, 0.0f}, false},
            // model 1: circuit-fit germanium Fuzz Face (AC128, the "round" one). Voicing is
            // just a bass trim (one-pole low-cut ~50 Hz, fit fuzz_face_response.py), flat &
            // bright above (NO top roll). The identity is the ASYMMETRIC cubic clip (type 4):
            // persistent asymmetry at all gains (cold-biased Q1) -> soft for small signals,
            // a tilted square when cranked; 2nd-order ADAA (polynomial, no dilog). dynDepth
            // gives the touch/volume cleanup (soft picking -> cleaner); gate gives the
            // bias-starved "velcro"/splat on decay. Hot, calibration-referenced gain range.
            {"Round Fuzz II", "Fuzz Face (AC128 germanium, asym + gate)",
             { 4, 8.0f,200.0f,  50.0f,    0.0f, 0.0f, 0.7f,    0.0f, 0.45f,  700.0f, 0.65f, 0.0f, 0.0f,  0.0f, 700.0f, 0.0f, 0.50f,  0.0f, 0.0f, 0.6f}, false},
            // model 2: circuit-fit EHX Big Muff Pi (Ram's Head '73). Filed under FUZZ
            // (it is marketed/perceived as a fuzz, though technically a diode distortion).
            // The Muff is NOT a single shaper -- it is TWO consecutive SOFT-clip stages
            // (silicon 1N914 back-to-back diodes in each transistor's collector->base
            // FEEDBACK loop, ~+/-0.6 V), so we run a real 2-stage cubic CASCADE
            // (muffStages 2): each stage has the Miller-cap low-pass (muffLpHz 1300)
            // BEFORE it -> the dark, smooth, no-fizz voice (clipping a low-passed signal
            // sounds smoother) and the dense, compressed double-clip "wall" a single clip
            // can't make. A fixed inter-stage gain (kMuffStage2Gain) drives stage 1's
            // output into stage 2's knee. Pre-clip low-cut 80 Hz tightens the lows (clip
            // stages HP 55/94 Hz); gentle post LP 1600 keeps it dark. The famous passive
            // tone-stack mid SCOOP is a static post-clip notch FIT to ElectroSmash's
            // MEASURED tone-noon response (midHz 1000, Q 0.80, -6.5 dB = the 1 kHz notch
            // 6.5 dB below the shelves; big_muff_response.py). Tone = the engine see-saw
            // tilt @ 1 kHz (the real Muff bass/treble blend) -- UNLIKE the other fuzzes,
            // the Muff exposes a Tone knob (the Fuzz panel shows it only for this model).
            // bias 0 (symmetric clipping). MODERATE default / HIGH-gain ceiling: gMin 3 =
            // controllable crunch at low Sustain, gMax 55 + the inter-stage gain = the
            // full saturated wall + max sustain at the top. Calibration-referenced.
            {"Violet Ram", "EHX Big Muff (Ram's Head, 2-stage)",
             { 3, 3.0f, 55.0f,  80.0f, 1000.0f,-6.5f, 0.80f,1600.0f, 0.00f, 1000.0f, 0.80f, 0.0f, 1.0f,  0.0f, 700.0f, 0.0f, 0.0f,   0.0f, 0.0f, 0.0f, 0.0f, 2.0f, 1300.0f}, false},
        };
        switch (cat)
        {
        case Kind::Boost:      count = 4; return boost;
        case Kind::Overdrive:  count = 4; return od;
        case Kind::Distortion: count = 2; return dist;
        case Kind::Fuzz:       count = 3; return fuzz;
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

    static Voicing voicingFor(Kind c, int m)
    {
        int n = 0; const Model *a = modelsFor(c, n);
        if (!a || n == 0) return { 0, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.7f, 0.0f, 0.0f, 700.0f, 1.0f, 0.0f, 0.0f, 0.0f, 700.0f, 0.0f, 0.0f, 0.0f, 0.0f };
        return a[juce::jlimit(0, n - 1, m)].v;
    }
    static Voicing voicingFor(Kind c) { return voicingFor(c, 0); } // compat (model 0)

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
            }

            const float preGain = v.gMin * std::pow(v.gMax / v.gMin, s.drive.load()); // log
            // How much of the pre-shaper EQ is engaged: static voicings use the
            // full EQ; for the overdrive the hump + bass-tighten scale with Drive.
            const float shapeAmt = 1.0f - v.shapeTrack * (1.0f - s.drive.load());
            const float hpCoef = (v.lowCutHz > 0.0f) ? coefForHz(v.lowCutHz, sr) : 0.0f;
            const float lpCoef = (v.lpHz > 0.0f) ? coefForHz(v.lpHz, sr) : 0.0f;
            const bool useMid = (v.midDb != 0.0f);
            const bool midPost = (v.midPost > 0.5f);
            const bool useLp = (v.lpHz > 0.0f);
            const bool cubic = (v.clip == 3);
            const bool asymCubic = (v.clip == 4);         // asymmetric cubic fuzz (Round Fuzz II)
            const bool cascade = (v.muffStages > 1.0f);   // Big Muff 2-stage soft-clip cascade
            const float millerCoef = (v.muffLpHz > 0.0f) ? coefForHz(v.muffLpHz, sr) : 0.0f; // pre-clip Miller LP
            const bool softPoly = (cubic || asymCubic) && !cascade; // single-shaper poly path (cascade has its own branch)
            const double kn = asymCubic ? (1.0 - (double)v.bias) : 1.0; // clip-4 negative knee (asymmetry)
            const bool adaa2 = (v.adaa2 > 0.5f);          // 2nd-order ADAA for this clip (hard clip)
            const bool ratTone = (v.toneFilterHz > 0.0f); // Tone = sweepable post-clip LP (RAT "Filter")
            const double asym = (v.clip == 2) ? (double)v.bias : 0.0;     // type-2 rail
            const double inBias = (v.clip == 2 || v.clip == 4) ? 0.0 : (double)v.bias; // type 0/1/3 input bias (4 = in-shaper asym)
            const bool useGate = (v.gate > 0.0f) && s.gateOn.load(); // model has a gate AND it's switched on
            float levelLin = std::pow(10.0f, s.levelDb.load() * 0.05f) * v.outTrim;
            if (mAutoGain.load()) // OFF by default: Drive then naturally pushes the amp harder
                levelLin *= driveMakeup(k, model, s.drive.load()) * toneMakeup(k, model, s.tone.load());

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

            // envelope-follower coefficients (touch dynamics, clip 3)
            const float envAtk = 1.0f - (float)std::exp(-1.0 / (0.005 * sr)); // ~5 ms
            const float envRel = 1.0f - (float)std::exp(-1.0 / (0.120 * sr)); // ~120 ms
            const float invEnvRef = 1.0f / 0.25f; // picking-level reference
            const float gpkDecay = (float)std::exp(-1.0 / (0.5 * sr)); // gate peak-hold release ~500 ms

            float hp = s.hp, lpz = s.lp, low = s.toneLp;
            float dcx = s.dcX1, dcy = s.dcY1;
            double x0 = s.x0, adx1 = s.adaaX1, adx2 = s.adaaX2;
            float env = s.env, gpk = s.gpk;
            float shx1 = s.shX1, shy1 = s.shY1;
            double adx1b = s.adaaX1b, adx2b = s.adaaX2b; // Big Muff stage-2 ADAA history
            float mLpPre = s.mLpPre, mHpInt = s.mHpInt, mLpInt = s.mLpInt; // cascade Miller LPs / inter HP

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
                    if (millerCoef > 0.0f) { mLpInt += millerCoef * (s2 - mLpInt); s2 = mLpInt; } // clip-stage Miller LP
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
                    float bEff = v.cleanBlend + v.dynDepth * (0.5f - envN); // soft picking -> more clean
                    bEff = bEff < 0.0f ? 0.0f : (bEff > 0.9f ? 0.9f : bEff);

                    const double xb = (double)s.emphPre.processSample(u) + inBias;
                    const double y = asymCubic ? clipAsymCubicADAA2(xb, adx1, adx2, kn)
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
                else if (adaa2)
                {
                    // ---- hard clip on 2nd-order ADAA (peak-guarded): Black Rodent II
                    // (RAT) and Gold Horse (Klon). The pre-clip EQ (low-cut + mid hump) is
                    // already in u, so mids hit the diodes hardest and bass clips least. ----
                    const double xb = (double)u + inBias;
                    const double y = clipHardADAA2(xb, adx1, adx2);
                    adx2 = adx1; adx1 = xb;
                    const float cc = (float)y;
                    // HEAVY parallel clean blend (the Klon "transparent" sum): mix the RAW
                    // full-range input back in -> restores the low end + dynamics that the
                    // mid-focused clipped path drops. Clean is at INPUT level (xin, NOT the
                    // gained signal -> no crackle). The envelope nudges it (touch: soft
                    // picking cleans up). With cleanBlend 0 AND dynDepth 0 this is a no-op,
                    // so Black Rodent II stays byte-exact.
                    if (v.cleanBlend > 0.0f || v.dynDepth > 0.0f)
                    {
                        const float aenv = std::abs(xin);
                        env += (aenv > env ? envAtk : envRel) * (aenv - env);
                        const float envN = clamp01(env * invEnvRef);
                        float bEff = v.cleanBlend + v.dynDepth * (0.5f - envN);
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
                if (ratTone)
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

            s.hp = flush(hp); s.lp = flush(lpz); s.toneLp = flush(low);
            s.dcX1 = flush(dcx); s.dcY1 = flush(dcy); s.x0 = flushD(x0);
            s.adaaX1 = flushD(adx1); s.adaaX2 = flushD(adx2); s.env = flush(env); s.gpk = flush(gpk);
            s.shX1 = flush(shx1); s.shY1 = flush(shy1);
            s.adaaX1b = flushD(adx1b); s.adaaX2b = flushD(adx2b);
            s.mLpPre = flush(mLpPre); s.mHpInt = flush(mHpInt); s.mLpInt = flush(mLpInt);
        }
    }

private:
    static constexpr float kMaxTiltDb = 9.0f;
    static constexpr float kDcR = 0.9995f; // DC blocker pole (~4 Hz corner @ 48k)
    static constexpr float kCleanScale = 3.5f; // raw-input clean -> clip-level gain (hard-clip clean blend, Klon)
    static constexpr float kMuffStage2Gain = 2.0f; // fixed inter-stage gain into the Big Muff's
                                                   // 2nd soft clip (the real circuit's ~+25 dB stage;
                                                   // drives clip-1's output into clip-2's knee -> the
                                                   // dense, compressed double-clip "wall". Tuned so BOTH
                                                   // stages clip from just below noon while the Drive
                                                   // knob keeps a controllable floor + a high-gain/
                                                   // sustain ceiling at max — Robbie's moderate-default
                                                   // /hot-ceiling brief, instead of the always-pinned
                                                   // stock Muff)

    struct Slot
    {
        std::atomic<int> kind{(int)Kind::Off};
        std::atomic<float> drive{0.5f};
        std::atomic<float> tone{0.5f};
        std::atomic<float> levelDb{0.0f};
        std::atomic<int> range{0};
        std::atomic<int> model{0};
        std::atomic<bool> on{true};
        std::atomic<bool> gateOn{true}; // fuzz bias-starved gate enable (control, not DSP state)
        float hp = 0.0f, lp = 0.0f, toneLp = 0.0f, dcX1 = 0.0f, dcY1 = 0.0f;
        double x0 = 0.0; // 1st-order ADAA history (double)
        double adaaX1 = 0.0, adaaX2 = 0.0; // 2nd-order ADAA history (cubic, double)
        float env = 0.0f; // envelope follower (touch dynamics)
        float gpk = 0.0f; // gate peak-hold (relative bias-starved gate)
        float shX1 = 0.0f, shY1 = 0.0f; // Klon treble-shelf 1st-order filter state
        // Big Muff 2-stage cascade state (only used when muffStages>1):
        double adaaX1b = 0.0, adaaX2b = 0.0; // stage-2 cubic 2nd-order ADAA history
        float mLpPre = 0.0f, mHpInt = 0.0f, mLpInt = 0.0f; // pre-clip Miller LP, inter-stage HP, inter-stage Miller LP
        int lastKind = -1;
        Biquad mid;     // pre/post-shaper peak (state preserved across blocks)
        Biquad emphPre, emphPost; // pre/de-emphasis pair (clip 3)
        void resetState()
        {
            hp = lp = toneLp = dcX1 = dcY1 = 0.0f; x0 = 0.0;
            adaaX1 = adaaX2 = 0.0; env = 0.0f; gpk = 0.0f; shX1 = shY1 = 0.0f;
            adaaX1b = adaaX2b = 0.0; mLpPre = mHpInt = mLpInt = 0.0f;
            lastKind = -1; mid.reset(); emphPre.reset(); emphPost.reset();
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

    // ---- asymmetric cubic soft-clip (type 4, Round Fuzz II) ----
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

    // ---- hard clip (type 1) 2nd-order ADAA (Black Rodent II) ----
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

    // ---- auto-gain: keep output level ~constant as Drive / Tone move ----
    // Per-voicing makeup measured against a guitar-like reference, normalised so
    // the knob centres (drive 0.5, tone 0.5) = unity (default sound unchanged).
    // Drive table is 6 points (0..1), Tone table 5 points (0..1).
    static float lerpTbl(const float *t, int n, float x)
    {
        const float pos = clamp01(x) * (float)(n - 1);
        int i = (int)pos;
        if (i >= n - 1) return t[n - 1];
        return t[i] + (pos - (float)i) * (t[i + 1] - t[i]);
    }
    // Auto-gain is UNITY-REFERENCED: the drive table = rms_in / rms_out(drive),
    // so Auto Gain ON brings the pedal to ~bypass level at every Drive (was
    // normalised to the pedal's own mid-drive level, which sat +11..+18 dB hot).
    // Per-MODEL because Boost holds two very different models (Range '65 / EP
    // Boost) a single category table can't level. Tone table stays relative.
    static float driveMakeup(Kind k, int model, float drive)
    {
        static const float B0[6] = {1.505f, 0.988f, 0.639f, 0.416f, 0.278f, 0.197f};
        static const float B1[6] = {1.203f, 0.846f, 0.597f, 0.425f, 0.307f, 0.228f};
        static const float B2[6] = {1.556f, 0.916f, 0.557f, 0.378f, 0.295f, 0.260f}; // Range '65 II (Rangemaster, pink-noise ref)
        static const float B3[6] = {1.073f, 0.801f, 0.601f, 0.456f, 0.352f, 0.279f}; // EP Boost II (clean boost, pink-noise ref)
        static const float O[6]  = {0.666f, 0.435f, 0.296f, 0.212f, 0.161f, 0.129f};
        static const float O2[6] = {0.438f, 0.410f, 0.401f, 0.398f, 0.396f, 0.396f}; // Super Drive (SD-1, asym cubic, pink-noise ref; near-flat = the clipper compresses)
        static const float O3[6] = {0.427f, 0.322f, 0.240f, 0.206f, 0.199f, 0.201f}; // Gold Horse (Klon, hard clip + heavy clean blend, pink-noise ref)
        static const float D[6]  = {1.229f, 0.568f, 0.310f, 0.242f, 0.223f, 0.214f};
        static const float F[6]  = {0.543f, 0.367f, 0.302f, 0.280f, 0.273f, 0.271f};
        static const float F1[6] = {0.439f, 0.371f, 0.346f, 0.338f, 0.335f, 0.334f}; // Round Fuzz II (asym cubic, pink-noise ref)
        static const float F2[6] = {0.599f, 0.399f, 0.315f, 0.284f, 0.273f, 0.269f}; // Violet Ram (Big Muff 2-stage cascade, pink-noise ref; compresses as both stages saturate)
        const float *t = (k == Kind::Boost) ? (model <= 0 ? B0 : model == 1 ? B1 : model == 2 ? B2 : B3)
                       : (k == Kind::Overdrive) ? (model >= 3 ? O3 : model == 2 ? O2 : O)
                       : (k == Kind::Distortion) ? D
                       : (model <= 0 ? F : model == 1 ? F1 : F2);
        return lerpTbl(t, 6, drive);
    }
    static float toneMakeup(Kind k, int model, float tone)
    {
        static const float B0[5] = {0.604f, 0.895f, 1.000f, 0.767f, 0.490f};
        static const float B1[5] = {0.499f, 0.789f, 1.000f, 0.818f, 0.522f};
        // Boost has no user Tone (processor pins it to 0.5) -> the centre point is unity;
        // Range '65 II reuses B0's symmetric tilt table (Tone never moves in the UI).
        static const float O[5]  = {0.472f, 0.757f, 1.000f, 0.851f, 0.548f};
        static const float D[5]  = {0.450f, 0.725f, 1.000f, 0.916f, 0.609f};
        static const float F[5]  = {0.533f, 0.833f, 1.000f, 0.773f, 0.485f};
        static const float F2[5] = {0.371f, 0.619f, 1.000f, 1.350f, 1.171f}; // Violet Ram (Big Muff see-saw Tone, pink-noise ref)
        // Round Fuzz models pin Tone to 0.5 (no tone) -> F centre is unity; the Muff
        // (model 2) is the only fuzz with a real Tone knob, so it gets its own table.
        const float *t = (k == Kind::Boost) ? (model <= 0 ? B0 : model == 1 ? B1 : B0)
                       : (k == Kind::Overdrive) ? O
                       : (k == Kind::Distortion) ? D
                       : (model == 2 ? F2 : F);
        return lerpTbl(t, 5, tone);
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
    std::atomic<bool> mAutoGain{false};
    double mSampleRate = 48000.0;
    bool mPrepared = false;
};

} // namespace nam_rig
