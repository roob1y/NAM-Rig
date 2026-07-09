#pragma once
// PreModBlock — a modulation pedal that sits IN FRONT OF THE AMP, in the
// shared pre section (after the drive rack, before the A/B split). Mono-in, with a
// mono process() path that stays bit-exact and an optional mono-in/stereo-out
// processStereo() path (one shared LFO read at two phases, Spread-controlled) for
// dual-amp rigs. This is the
// "pedalboard modulation" position: a modulation stompbox feeding the amp, so the
// modulated signal is coloured by the amp's nonlinearity — deliberately DISTINCT
// from the post-cab STEREO ModBlock, which only ever sees the finished, cabinet-
// filtered tone.
//
// This is its OWN engine, built from how real BBD choruses actually behave
// (research: ElectroSmash CE-2 teardown, Electric Druid's BBD-chorus study,
// Raffel & Smith DAFx-2010 "Practical Modeling of BBD Circuits", Dattorro):
//
//   * SINGLE BBD voice — the shipped chorus is voiced to the BOSS CE-2, which is a
//     one-voice BBD: a single tap on the delay line at the CE-2's ~9.5 ms delay
//     centre, swept by a small triangle LFO (classic/subtle, not a wide studio
//     ensemble). The tap engine can read kVoices taps at DIFFERENT fixed delay
//     offsets (each at its own LFO phase) for a fuller, decorrelated ensemble, but
//     kVoices == 1 ships the authentic mono CE-2.
//   * BBD NONLINEARITY as a gentle, LEVEL-INDEPENDENT 3rd-order polynomial
//     (x - a·x² - b·x³, a=1/8 b=1/18), NOT a tanh soft-clip. The x² term gives the
//     2nd-harmonic, x³ the 3rd; the point is a fixed subtle colour, not a clipper
//     that distorts more as you push it (the common emulation mistake). Applied
//     with exact first-order ADAA (polynomial antiderivative) so it never aliases.
//   * DARK WET path — a 2-pole ~6.6 kHz low-pass (the CE-2's Sallen-Key
//     reconstruction) so the wet FUSES with the bright dry instead of phasing
//     against it. A bright wet is the classic "digital chorus doesn't gel" fault.
//   * FIR fractional reads (6-point Lagrange) so swept taps never click; a subsonic
//     trim removes the polynomial's even-harmonic DC; de-zippered params.
//
// STATE: all five are voiced — CHORUS (CE-2), PHASER (Phase 90 / Small Stone),
// FLANGER (MXR EVH117 / M117R -- identical circuit), TREMOLO (Boss TR-2) and UNI-VIBE
// (Shin-ei). Mono-in; bit-exact mono path plus an optional stereo-out widen; zero
// reported latency. JUCE-free core DSP, verified by tests/premod_test.cpp.

#include "Blocks.h"
#include "Lfo.h"
#include "IoStage.h"
#include <algorithm>
#include <cmath>

namespace nam_rig
{

class PreModBlock : public MonoBlock
{
public:
    enum Type { kChorus = 0, kPhaser, kFlanger, kTremolo, kUniVibe, kNumTypes };

    // ---- chorus voicing (front-of-amp, analog BBD) ----
    // Voiced to the Boss CE-2: a SINGLE BBD voice at the CE-2's ~9.5 ms delay
    // centre with its small modulation swing — the authentic mono CE-2. (kVoices is
    // a compile-time constant, not a knob: it ships at 1 = the real CE-2. Raising it
    // clusters extra taps around the same centre for a fuller, less strictly-
    // authentic chorus, but that is a code change, not a user parameter.)
    static constexpr int kVoices = 1;
    static constexpr double kDelayMinMs = 8.0;   // (with kVoices==1 the tap sits at the mean)
    static constexpr double kDelayMaxMs = 11.0;  // ~9.5 ms mean = CE-2 delay centre
    static constexpr double kModDepthMs = 1.1;   // max +/- sweep at depth 1 = authentic CE-2 (~±1.1 ms)
    static constexpr float kChorusMaxRateHz = 3.5f; // real chorus lives <~4 Hz (faster -> vibrato/warble)
    static constexpr double kWetLpHz = 6600.0;   // CE-2 reconstruction ceiling -> dark wet that fuses
    static constexpr double kWetHpHz = 40.0;     // subsonic trim (kills the x^2 DC + tightens lows)
    // BBD 3rd-order polynomial colour (Raffel & Smith): f(x)=x - a·x² - b·x³.
    static constexpr double kBbdA = 1.0 / 8.0;
    static constexpr double kBbdB = 1.0 / 18.0;

    // ---- phaser voicing (Phase 90 / Small Stone family) ----
    // 4 first-order all-pass stages (2 swept notches), EQUAL stage frequencies,
    // mixed 50/50 with dry (Mix sets notch depth). The all-pass corner sweeps
    // EXPONENTIALLY (log/octave) about kPhaserCenterHz by ±kPhaserOctaves·Depth,
    // driven by the triangle LFO. Feedback around the chain is the character knob:
    // 0 = smooth (Script Phase 90); up = resonant/"vocal" (Block / Small Stone
    // Color) with the mid-hump "throb". Built as ZERO-DELAY-FEEDBACK / TPT all-passes
    // (closed-form feedback) so the resonance stays tuned and stable even on a fast
    // sweep — a plain unit-delay loop mistunes the notches and blows up (per DAFx).
    static constexpr int kPhaserStages = 4;
    static constexpr double kPhaserRestHz = 141.0;   // at-rest all-pass corner (JFET at max R):
                                                     // notches sit at 58.5 & 340.8 Hz (ElectroSmash)
    static constexpr double kPhaserOctaves = 3.9;    // UPWARD sweep span (octaves) at Depth 1 --
                                                     // the JFET only ever RAISES the corner from rest.
                                                     // Shipped Depth 0.60 -> ~2.34 oct sweep: corner
                                                     // 141 Hz -> ~713 Hz, so notch2 tops ~1.7 kHz. Kept
                                                     // deliberately low/concentrated -- matches the
                                                     // Phase 90's dark low-mid voice (Eichas DAFx-14
                                                     // spectrograms Fig 7/8; rest verified vs ElectroSmash).
                                                     // Sweeping higher would read swooshy/hi-fi, un-P90.
    static constexpr float kPhaserFbMax = 0.70f;     // musical feedback ceiling; kept modest so the swept
                                                     // resonance peak doesn't spike the level as it crosses a note

    // ---- flanger voicing (BBD flanger: MXR EVH117 / M117R -- identical circuit) ----
    // A SHORT swept delay makes a harmonically-spaced comb (notches at odd multiples
    // of 1/2t, peaks at n/t); as the delay sweeps, the whole comb sweeps = the "jet".
    // Feedback (regen) reinforces the peaks into the resonant metallic sweep; it's
    // taken around the delay itself (naturally delayed by the tap, so a plain loop is
    // correct + stable below unity), tone-shaped to tame fizz. Mix 50/50 = deepest
    // notches. Reuses the BBD polynomial colour + dark wet for the analog character.
    // Manual (base/centre delay, like the M117 / Mistress / A/DA) sets where the
    // comb sits; Depth sweeps the delay UP from there. Manual 0 = shortest (comb
    // highest), 1 = longest (comb lowest); the swept delay is clamped to kFlMaxMs.
    static constexpr double kFlManualMinMs = 0.5;  // Manual 0 -> shortest base delay
    static constexpr double kFlManualMaxMs = 8.0;  // Manual 1 -> longest base delay
    static constexpr double kFlSweepMs = 6.0;      // sweep excursion above the base at Depth 1
    static constexpr double kFlMaxMs = 12.8;       // EVH117/M117R published max delay (12.8 ms)
    static constexpr float kFlFbMax = 0.78f;    // regen ceiling; the feedback state is also soft-clipped
                                                // so high regen self-limits (like an analog flanger) instead of spiking
    static constexpr double kFlFbLpHz = 6500.0; // one-pole low-pass in the feedback path (tames fizz)
    static constexpr double kFlWetLpHz = 8200.0; // flanger reconstruction ceiling -- brighter than the CE-2's 6.6k so the comb's upper notches (the "jet") stay audible

    // ---- tremolo voicing (Boss TR-2: VCA amplitude modulation) ----
    // Wave morphs the triangle LFO toward a TRAPEZOID (steeper sides, flat top) by
    // amplifying + clamping it — soft "pulsey" at Wave 0, choppy at Wave 1 (not a
    // hard square; the slew keeps it click-free, like the TR-2's anti-tick edges).
    // Gain is CUT-ONLY (g in [1-depth, 1], peak at unity) — the authentic TR-2 law,
    // including its signature perceived volume drop as Depth rises. Clean AM (no EQ).
    static constexpr float kTremWaveK = 16.0f; // max triangle->trapezoid sharpening gain (Wave 1 ~ square; the slew still de-clicks the edges)
    static constexpr float kTremSlewMs = 1.5f; // de-click slew on the gain envelope

    // ---- uni-vibe voicing (Shin-ei Uni-Vibe: 4 STAGGERED opto all-pass stages) ----
    // Unlike the phaser's equal stages, the four stages have DIFFERENT centre freqs
    // (from the real staggered caps 0.015/0.22µF/470pF/0.0047µF) -> uneven, non-
    // harmonic notches (~2 audible, the "double beat"). One lamp sweeps all four via
    // photocells with an ASYMMETRIC thermal lag (heats fast, cools slow) -> the
    // lopsided throb. Mix = Chorus (dry+wet, ~0.5) .. Vibrato (wet only, 1.0). Stock
    // has no feedback; the Feedback knob is the classic hot-rod (adds resonance).
    static constexpr double kUniCenterHz = 430.0; // geometric centre of the staggered stages
    static constexpr double kUniMult[4] = {0.616, 0.042, 19.6, 1.97}; // staggered ratios (from the caps)
    static constexpr double kUniOctaves = 1.2;    // sweep width (octaves) at Depth 1
    static constexpr float kUniLampHeatMs = 12.0f; // lamp filament heats fast (~10-40 ms, DAFx-19)
    static constexpr float kUniLampCoolMs = 110.0f;// ...and cools much slower -> the lopsided "throb"
    static constexpr float kUniGamma = 1.5f;      // LDR power-law transfer (fc ~ light^gamma)
    static constexpr float kUniAmDepth = 0.12f;   // photocell amplitude throb (DAFx-19 real unit ran higher; reads more "vibe", less "phaser")
    static constexpr float kUniFbMax = 0.5f;      // hot-rod feedback ceiling (stock = 0)

    // Tempo-sync division table (index 0 = Off = free), shared convention with the rig.
    static constexpr int kNumSync = 10;
    static double syncBeats(int i)
    {
        static const double beats[kNumSync] = {0.0, 4.0, 2.0, 1.0, 1.5, 2.0 / 3.0,
                                               0.5, 0.75, 1.0 / 3.0, 0.25};
        return (i > 0 && i < kNumSync) ? beats[i] : 0.0;
    }

    static float maxRateHz(Type t)
    {
        if (t == kChorus) return kChorusMaxRateHz; // 3.5 Hz (CE-2 ceiling)
        if (t == kPhaser) return 5.0f;             // Phase 90 tops ~5 Hz
        if (t == kTremolo) return 12.0f;           // TR-2 tops ~11 Hz
        if (t == kUniVibe) return 7.6f;            // Uni-Vibe tops ~7.6 Hz (DAFx-19 measured)
        return 10.0f;                              // flanger (A/DA to 10 Hz)
    }

    // Per-type wet reconstruction ceiling: the CE-2 chorus is dark (~6.6 kHz Sallen-
    // Key reconstruction); the EVH117 flanger runs a faster BBD clock and stays
    // brighter so its comb sings. Non-BBD types don't use the wet low-pass.
    static double wetLpHzFor(Type t) { return (t == kFlanger) ? kFlWetLpHz : kWetLpHz; }

    // ---- authentic input/output stage (impedance loading + coupling caps) ----
    // The guitar is DI'd at ~1 MOhm, so we model only the DELTA each pedal's real
    // input impedance adds vs that reference: a low Zin damps the pickup's resonant
    // peak (~2.7-3 kHz) and drops a little level; a buffered/high-Z input is just its
    // DC-blocking coupling high-passes. Verified Zin per pedal (research 2026-07-04):
    //   CE-2     ~407 kOhm (R2 470k)        -> gentle damp   (ElectroSmash)
    //   Phase 90 ~470 kOhm, C5 10n          -> gentle damp, ~33 Hz coupling
    //   EVH117   470 kOhm (spec), 1k out    -> gentle damp
    //   TR-2     1 MOhm FET input           -> transparent (just coupling)
    //   Uni-Vibe 69 kOhm (22k + 47k divider)-> STRONG treble-suck load (geofex)
    struct IoAnchors { float inHpHz, shelfHz, shelfCutDb, inLevelDb, inLpHz, outHpHz, outLevelDb; };
    static IoAnchors ioFor(Type t)
    {
        switch (t)
        {
        // CE-2: buffered emitter-follower, but Zin ~407k (below a modern 1M) loads the
        // pickup slightly -> a gentle resonance damp like the TS. Coupling ~8 Hz;
        // reconstruction output HP ~14.6 Hz.
        case kChorus:  return { 8.0f, 2800.0f, -1.0f, -0.2f, 0.0f, 15.0f, 0.0f };
        // Phase 90: buffered input ~470k with C5 10n -> ~33 Hz coupling; gentle damp;
        // discrete PNP output stage HP ~22 Hz.
        case kPhaser:  return { 33.0f, 3000.0f, -1.0f, -0.2f, 0.0f, 22.0f, 0.0f };
        // EVH117/M117R: op-amp buffered ~470k -> gentle damp; 1k output, low coupling.
        case kFlanger: return { 8.0f, 3000.0f, -1.0f, -0.2f, 0.0f, 8.0f, 0.0f };
        // TR-2: 1 MOhm JFET input -> transparent (matches the DI); just the coupling
        // caps (C1 27n ~5.9 Hz in, C4 0.1u out).
        case kTremolo: return { 6.0f, 0.0f, 0.0f, 0.0f, 0.0f, 7.0f, 0.0f };
        // Uni-Vibe: 22k series + 47k-to-ground = 69k load -> "significant treble loss
        // to single coils" (geofex). Above 47k so a strong (not total) loading shelf +
        // a small level drop. Input coupling ~45 Hz; output effectively full-range.
        case kUniVibe: return { 45.0f, 2800.0f, -3.0f, -0.5f, 0.0f, 12.0f, 0.0f };
        default:       return { 8.0f, 0.0f, 0.0f, 0.0f, 0.0f, 8.0f, 0.0f };
        }
    }
    static void applyIo(IoStage &io, const IoAnchors &a)
    {
        if (a.shelfCutDb != 0.0f)
            io.setLoaded(a.inHpHz, a.shelfHz, a.shelfCutDb, a.inLevelDb, a.outHpHz, a.outLevelDb);
        else
            io.setBuffered(a.inHpHz, a.outHpHz, a.inLpHz, a.outLevelDb);
    }

    const char *name() const override { return "Pre Mod"; }

    // Per-lane state: everything that carries memory sample-to-sample. The mono
    // process() uses mLaneL ONLY (so it stays bit-exact to the pre-stereo block);
    // processStereo runs mLaneL at LFO phase 0 and mLaneR at phase 0.5·spread, both
    // driven by the ONE mLfo clock + the shared smoothed params (so the two lanes can
    // never drift out of phase-lock).
    struct Lane
    {
        FracDelayLine line;                 // chorus/flanger delay line
        float lpZ1 = 0.0f, lpZ2 = 0.0f;     // dark-wet 2-pole reconstruction LP state
        float hpLp = 0.0f;                  // subsonic HP state
        double nlX1 = 0.0, nlF1 = 0.0;      // BBD polynomial ADAA state (double: no cancellation noise)
        float ap[kPhaserStages] = {0.0f, 0.0f, 0.0f, 0.0f}; // phaser / uni-vibe all-pass TPT states
        float flFbState = 0.0f, flFbLp = 0.0f;              // flanger regen state + tone-shape
        float tremG = 1.0f;                 // tremolo smoothed gain
        float uniLamp = 0.5f;               // uni-vibe lamp thermal state
        IoStage io;                         // authentic per-pedal input/output stage
    };

    void prepare(const BlockContext &ctx) override
    {
        mFs = ctx.sampleRate;
        const double maxDelayMs = std::max(kDelayMaxMs + kModDepthMs, kFlMaxMs) + 2.0;
        const int lineLen = (int)std::ceil(maxDelayMs * 0.001 * mFs);
        mLaneL.line.prepare(lineLen);
        mLaneR.line.prepare(lineLen);
        mLfo.prepare(mFs);
        mSmoothK = 1.0f - std::exp((float)(-1.0 / (0.010 * mFs))); // 10 ms de-zip
        rbjLowpass(wetLpHzFor(mType), 0.70710678, mFs, mLpB0, mLpB1, mLpB2, mLpA1, mLpA2);
        mHpCoef = coefForHz(kWetHpHz, mFs);
        mLaneL.io.prepare(mFs);
        mLaneR.io.prepare(mFs);
        applyIo(mLaneL.io, ioFor(mType)); // authentic input/output stage (same config both lanes)
        applyIo(mLaneR.io, ioFor(mType));
        mFlFbCoef = coefForHz(kFlFbLpHz, mFs);
        mTremCoef = coefForMs(kTremSlewMs, mFs);
        mUniHeatCoef = coefForMs(kUniLampHeatMs, mFs);
        mUniCoolCoef = coefForMs(kUniLampCoolMs, mFs);
        reset();
        mPrepared = true;
    }

    void reset() override
    {
        mLfo.reset();
        resetLane(mLaneL);
        resetLane(mLaneR);
        mDepthZ = mDepth;
        mMixZ = mMix;
        mFeedbackZ = mFeedback;
        mManualZ = mManual;
        mWaveZ = mWave;
        mSpreadZ = mSpread;
        mPlaying = false;
        mWasPlaying = false; // next transport-start edge re-snaps the synced LFO phase
    }

    // Reset one lane's memory. tremG starts at unity (no chop on the first sample);
    // uniLamp at mid brightness (no startup snap) — mirrors the pre-stereo defaults.
    static void resetLane(Lane &ln)
    {
        ln.line.reset();
        ln.io.reset();
        ln.lpZ1 = ln.lpZ2 = 0.0f;
        ln.hpLp = 0.0f;
        ln.nlX1 = 0.0;
        ln.nlF1 = 0.0;
        for (float &s : ln.ap) s = 0.0f;
        ln.flFbState = 0.0f;
        ln.flFbLp = 0.0f;
        ln.tremG = 1.0f;
        ln.uniLamp = 0.5f;
    }

    // ---- parameters (audio thread) ----
    void setType(int t)
    {
        const Type ty = (Type)std::min(std::max(t, 0), (int)kNumTypes - 1);
        if (ty != mType)
        {
            mType = ty;
            if (mPrepared)
            {
                // per-type wet ceiling + the new pedal's authentic input/output stage
                rbjLowpass(wetLpHzFor(mType), 0.70710678, mFs, mLpB0, mLpB1, mLpB2, mLpA1, mLpA2);
                applyIo(mLaneL.io, ioFor(mType));
                applyIo(mLaneR.io, ioFor(mType));
                reset();
            }
        }
    }
    void setRateHz(float hz) { mFreeRateHz = hz; }
    void setSyncIndex(int i) { mSyncIndex = i; } // 0 = Off (free)
    void setBpm(double bpm) { if (bpm > 0.0) mBpm = bpm; }
    // Host transport, for PPQ phase-resync of the tempo-synced LFO. playing = transport
    // running; ppqPosition = playhead position in quarter-note beats. Call once per
    // block (before process()/processStereo()); harmless when the host reports neither.
    void setTransport(bool playing, double ppqPosition) { mPlaying = playing; mPpq = ppqPosition; }
    // NB: the member defaults below are NOT the shipped voice. The fool-proof panel
    // hides most knobs and PluginProcessor.cpp (~1130-1148) pins them per-type — e.g.
    // the shipped phaser runs feedback 0.35 + depth 0.60, the flanger Manual 0.15.
    // Audit the pin table there, not these defaults, to know how a pedal actually sounds.
    void setDepth(float d) { mDepth = d; }
    void setMix(float m) { mMix = m; }
    void setFeedback(float f) { mFeedback = f; } // phaser resonance / flanger regen
    void setManual(float m) { mManual = m; }     // flanger base/centre delay (0..1)
    void setWave(float w) { mWave = w; }         // tremolo shape morph: triangle (0) -> trapezoid (1)
    // Stereo width for processStereo: 0 = both lanes in phase (dual-mono), 1 = the R
    // lane's LFO read is 180° out of phase with L (widest swirl / anti-phase auto-pan
    // for the tremolo). De-zippered (mSpreadZ) so automation can't step the R lane's
    // phase at block boundaries; mono process() ignores it.
    void setSpread(float s) { mSpread = std::min(std::max(s, 0.0f), 1.0f); }
    // Snap the de-zippered Spread to its target immediately (no smoothing ramp).
    // Called on discontinuous state changes that aren't live automation moves —
    // e.g. engaging the stereo feature — so the first stereo block honours the
    // current Spread exactly (Spread 0 -> bit-exact dual-mono from sample 0).
    void snapSpread() { mSpreadZ = mSpread; }

    float effectiveRateHz() const
    {
        const double beats = syncBeats(mSyncIndex);
        if (beats > 0.0) return (float)((mBpm / 60.0) / beats);
        return std::min(mFreeRateHz, maxRateHz(mType));
    }

    void process(float *mono, int numSamples) override
    {
        // front-end: coupling HP + impedance-loading shelf -> colours what the pedal
        // (and its LFO-swept filters/delays) sees, like the real input stage loading
        // the guitar.
        mLaneL.io.processIn(mono, numSamples);
        mLfo.setRateHz(effectiveRateHz());
        // Uni-Vibe's LFO is a sine (then the lamp lag skews it); the others use triangle.
        mLfo.setWaveform(mType == kUniVibe ? Lfo::Sine : Lfo::Triangle);
        maybeResyncPhase(); // tempo-sync: snap LFO phase to the beat on transport start
        for (int i = 0; i < numSamples; ++i)
        {
            advanceSmoothed();
            mono[i] = voiceLane(mono[i], mLaneL, 0.0); // mono = the L lane at LFO phase 0
            mLfo.advance();
        }
        mLaneL.io.processOut(mono, numSamples); // back-end: output coupling HP + level
        flushDenormals(mLaneL);
    }

    // Mono-in / stereo-out front pedal: ONE LFO clock read at two phases so the two
    // lanes are always sample-accurately phase-locked (the whole point of stereo
    // chorus/flanger/vibe, and of anti-phase tremolo = auto-pan). L is the mono voice
    // (phase 0); R is offset by 0.5·spread cycles (spread 1 -> 180°). Each lane keeps
    // its OWN delay line / filter / regen / lamp state AND its own input+output stage,
    // so it can modulate a DIFFERENT dry — e.g. the clean-tap (Amp A) vs the driven
    // bus (Amp B) at the RigChain split — with a decorrelated sweep. The shared
    // smoothed params advance ONCE per sample and feed both lanes.
    void processStereo(float *L, float *R, int numSamples)
    {
        mLaneL.io.processIn(L, numSamples);
        mLaneR.io.processIn(R, numSamples);
        mLfo.setRateHz(effectiveRateHz());
        mLfo.setWaveform(mType == kUniVibe ? Lfo::Sine : Lfo::Triangle);
        maybeResyncPhase(); // tempo-sync: snap LFO phase to the beat on transport start
        for (int i = 0; i < numSamples; ++i)
        {
            advanceSmoothed();
            const double phaseR = 0.5 * (double)mSpreadZ; // de-zippered offset: 0..0.5 = 0..180°
            L[i] = voiceLane(L[i], mLaneL, 0.0);
            R[i] = voiceLane(R[i], mLaneR, phaseR);
            mLfo.advance();
        }
        mLaneL.io.processOut(L, numSamples);
        mLaneR.io.processOut(R, numSamples);
        flushDenormals(mLaneL);
        flushDenormals(mLaneR);
    }

    double latencySamples() const override { return 0.0; }

private:
    // Advance the shared de-zippered params one sample. Called ONCE per sample by
    // both process() and processStereo() (before the lane calls) so the two lanes
    // always see identical smoothed values — kept in this exact order so the mono
    // path stays bit-exact to the pre-stereo block.
    // On the rising edge of the host transport, and ONLY when tempo-synced, snap the
    // LFO phase so cycle-start (phase 0) lands on the beat grid at the current PPQ.
    // The synced LFO then free-runs at the exact synced rate, so a synced tremolo/
    // flanger chop relates to the groove and re-recording a section gives the identical
    // wobble every take. Free-running (or when the host never reports play) it does
    // nothing -> the offline tests, which never set transport, stay bit-exact.
    void maybeResyncPhase()
    {
        const double beats = syncBeats(mSyncIndex);
        if (beats > 0.0 && mPlaying && !mWasPlaying)
            mLfo.setPhase(mPpq / beats); // setPhase wraps to [0,1)
        mWasPlaying = mPlaying;
    }

    void advanceSmoothed()
    {
        mDepthZ += mSmoothK * (mDepth - mDepthZ);
        mMixZ += mSmoothK * (mMix - mMixZ);
        mFeedbackZ += mSmoothK * (mFeedback - mFeedbackZ);
        mManualZ += mSmoothK * (mManual - mManualZ);
        mWaveZ += mSmoothK * (mWave - mWaveZ);
        mSpreadZ += mSmoothK * (mSpread - mSpreadZ); // stereo width; only read by processStereo (mono path unaffected -> still bit-exact)
    }

    // Voice one sample for one lane. lanePhase is the extra LFO phase offset (cycles)
    // for this lane's swept reads — 0 for the mono/L lane, 0.5·spread for R. All
    // sample-to-sample state lives in `ln`; the LFO clock + smoothed params are shared
    // (advanced once per sample by the caller). With lanePhase 0 and ln == mLaneL this
    // is byte-identical to the old mono processSample().
    float voiceLane(float x, Lane &ln, double lanePhase)
    {
        switch (mType)
        {
        case kChorus:
        {
            // Multi-tap chorus: kVoices taps at DIFFERENT fixed delay centres, each
            // swept a little at its own LFO phase (decorrelated), summed. The taps
            // sit at different comb positions -> ensemble shimmer, not one vibrato.
            ln.line.write(x);
            float wet = 0.0f;
            for (int v = 0; v < kVoices; ++v)
            {
                const double frac = (kVoices > 1) ? (double)v / (double)(kVoices - 1) : 0.5;
                const double baseMs = kDelayMinMs + (kDelayMaxMs - kDelayMinMs) * frac;
                const float lv = mLfo.value((double)v / (double)kVoices + lanePhase); // spread phases (incl. anti-phase)
                const double sweepMs = baseMs + (double)mDepthZ * kModDepthMs * (double)lv;
                wet += ln.line.readFrac6(std::max(3.0, sweepMs * 0.001 * mFs));
            }
            wet *= 1.0f / (float)kVoices;
            wet = bbdColor(wet, ln);
            return (1.0f - mMixZ) * x + mMixZ * wet;
        }
        case kPhaser:
        {
            // 4-stage ZDF/TPT all-pass phaser. All stages share one swept corner
            // (equal frequencies = Phase 90 / Small Stone). Each TPT all-pass is
            // affine in its input: ap = alpha·in + beta, alpha = 2G-1 (depends only
            // on the corner), beta = 2(1-G)·s (depends only on stored state). The
            // chain collapses to y = A·u + B, so the NEGATIVE feedback loop (the
            // Phase-90 notch structure) resolves in closed form with no unit delay:
            //   u = (x - k·B)/(1 + k·A);  denom > 0 for all k >= 0 -> always stable.
            const float lfo = mLfo.value(lanePhase); // triangle [-1, 1]
            // Authentic Phase 90 sweep: the JFET only ever RAISES the all-pass corner
            // above its rest value, so it sweeps UPWARD from kPhaserRestHz (notches
            // 58.5/340.8 Hz at rest) rather than symmetrically about a centre.
            const double up = 0.5 + 0.5 * (double)lfo; // triangle -> [0, 1]
            const double fc = std::min(
                0.45 * mFs,
                std::max(20.0, kPhaserRestHz * std::pow(2.0, up * kPhaserOctaves
                                                                    * (double)mDepthZ)));
            const double g = std::tan(3.14159265358979323846 * fc / mFs);
            const float G = (float)(g / (1.0 + g));
            const float alpha = 2.0f * G - 1.0f;

            float beta[kPhaserStages];
            float B = 0.0f;
            for (int s = 0; s < kPhaserStages; ++s)
            {
                beta[s] = 2.0f * (1.0f - G) * ln.ap[s];
                B = alpha * B + beta[s]; // Horner: B = alpha^3·b0 + ... + b3
            }
            const float a2 = alpha * alpha;
            const float A = a2 * a2; // alpha^4

            const float k = kPhaserFbMax * mFeedbackZ;    // negative feedback (resonance)
            const float u = (x - k * B) / (1.0f + k * A); // zero-delay resolved chain input

            float in = u;
            for (int s = 0; s < kPhaserStages; ++s)
            {
                const float out = alpha * in + beta[s]; // true stage output
                const float v = (in - ln.ap[s]) * G;    // TPT integrator update
                ln.ap[s] += 2.0f * v;
                in = out;
            }
            // Real Phase 90 / Small Stone mix dry + phased at a FIXED 50/50 (that's
            // what makes the deepest notch); no mix control on the pedal, so it's
            // hardwired (the Mix knob is greyed for the phaser in the panel).
            return 0.5f * x + 0.5f * in;
        }
        case kFlanger:
        {
            // BBD flanger: one short delay tap swept by the triangle LFO, with a
            // tone-shaped regeneration loop. The tap sweeps UP from kFlBaseMs (comb
            // high) by Depth·kFlSweepMs, so the harmonic comb sweeps down/up = jet.
            // Positive feedback reinforces the peaks (resonant); the feedback is
            // added at the delay INPUT so its loop delay is the tap itself (correct
            // + stable for fb < 1). Wet gets the BBD colour; feedback is low-passed
            // to keep high regen from turning to fizz.
            const float lfo = mLfo.value(lanePhase); // triangle [-1, 1]
            // Manual sets the base/centre delay; Depth sweeps UP from there; clamp.
            const double base = kFlManualMinMs + (double)mManualZ * (kFlManualMaxMs - kFlManualMinMs);
            const double sweep = (double)mDepthZ * kFlSweepMs * (0.5 + 0.5 * (double)lfo);
            const double delayMs = std::min(kFlMaxMs, std::max(kFlManualMinMs, base + sweep));
            const float fb = kFlFbMax * mFeedbackZ;
            ln.line.write(x + fb * ln.flFbState);
            float wet = ln.line.readFrac6(std::max(3.0, delayMs * 0.001 * mFs));
            wet = bbdColor(wet, ln);
            ln.flFbLp += mFlFbCoef * (wet - ln.flFbLp);   // tone-shape the regen (tame fizz)
            ln.flFbState = std::tanh(ln.flFbLp);          // soft-clip the loop -> self-limits at high regen
                                                      // (near-linear at normal levels, so low regen is unchanged)
            // Classic flangers sit at a fixed ~50/50 for the deepest comb; expose Mix
            // as dry..50/50 so the knob spans dry -> deepest flange (can't over-wet
            // past the sweet spot, where the notches would start filling back in).
            const float m = mMixZ * 0.5f;
            return (1.0f - m) * x + m * wet;
        }
        case kTremolo:
        {
            // Boss TR-2: VCA amplitude modulation. Wave morphs the triangle LFO to a
            // trapezoid (amplify + clamp -> steeper sides, flat top); the gain is
            // cut-only (peak unity, dips by Depth) and de-clicked with a short slew.
            const float tri = mLfo.value(lanePhase);              // triangle [-1, 1]
            const float sharpen = 1.0f + mWaveZ * kTremWaveK;     // Wave -> trapezoid gain
            const float shaped = std::max(-1.0f, std::min(1.0f, tri * sharpen));
            const float sn = 0.5f * (shaped + 1.0f);              // -> [0, 1] (1 = loud)
            const float target = (1.0f - mDepthZ) + mDepthZ * sn; // cut-only: [1-depth, 1]
            ln.tremG += mTremCoef * (target - ln.tremG);          // slew de-click
            return x * ln.tremG;
        }
        case kUniVibe:
        {
            // Shin-ei Uni-Vibe: 4 STAGGERED opto all-pass stages swept by one lamp.
            // The lamp has an asymmetric thermal lag (heats fast, cools slow) and the
            // LDR a power-law transfer -> the lopsided "throb". Stages use the real
            // staggered ratios (kUniMult) so the notches are uneven / non-harmonic.
            const float lfo = mLfo.value(lanePhase); // sine [-1, 1]
            const float drive = 0.5f + 0.5f * lfo; // -> lamp drive [0, 1]
            ln.uniLamp += (drive > ln.uniLamp ? mUniHeatCoef : mUniCoolCoef) * (drive - ln.uniLamp);
            const float cell = std::pow(std::max(0.0f, ln.uniLamp), kUniGamma); // LDR light [0,1]
            const float warp = cell * 2.0f - 1.0f;                            // sweep control [-1,1]

            // per-stage ZDF/TPT all-pass: gather G_i / alpha_i / beta_i, build the
            // chain collapse y = A·u + B (A = prod alpha_i, B = nested), then resolve
            // the POSITIVE feedback loop with zero delay: u = (x + k·B)/(1 - k·A).
            float G[4], alpha[4], beta[4];
            float A = 1.0f, B = 0.0f;
            // Loop-invariant across all 4 stages: hoist the pow out (bit-exact — the
            // per-stage multiply order kUniCenterHz * kUniMult[s] * sweep is unchanged).
            const double sweep = std::pow(2.0, (double)warp * kUniOctaves * (double)mDepthZ);
            for (int s = 0; s < 4; ++s)
            {
                const double fc = std::min(0.45 * mFs, std::max(20.0,
                    kUniCenterHz * kUniMult[s] * sweep));
                const double g = std::tan(3.14159265358979323846 * fc / mFs);
                G[s] = (float)(g / (1.0 + g));
                alpha[s] = 2.0f * G[s] - 1.0f;
                beta[s] = 2.0f * (1.0f - G[s]) * ln.ap[s];
                A *= alpha[s];
                B = alpha[s] * B + beta[s];
            }
            const float k = kUniFbMax * mFeedbackZ;         // positive feedback (hot-rod; 0 = stock)
            const float u = (x + k * B) / (1.0f - k * A);   // zero-delay resolved chain input
            float in = u;
            for (int s = 0; s < 4; ++s)
            {
                const float out = alpha[s] * in + beta[s];
                const float v = (in - ln.ap[s]) * G[s];
                ln.ap[s] += 2.0f * v;
                in = out;
            }
            in *= 1.0f - kUniAmDepth * mDepthZ * cell;      // subtle photocell amplitude throb
            // Mix: Chorus (dry+wet, ~0.5) .. Vibrato (wet only, 1.0).
            return (1.0f - mMixZ) * x + mMixZ * in;
        }
        default:
            return x;
        }
    }

    // Bucket-brigade colour: the gentle level-independent BBD polynomial (ADAA'd),
    // then the dark reconstruction low-pass, then a subsonic trim. Order mirrors the
    // circuit (BBD distorts, reconstruction filter darkens, DC/subsonics removed).
    float bbdColor(float wet, Lane &ln)
    {
        // --- BBD 3rd-order polynomial via exact first-order ADAA ---
        double u = (double)wet;
        u = std::min(1.5, std::max(-1.5, u)); // keep the cubic well-behaved
        const double Fc = bbdAntideriv(u);
        const double du = u - ln.nlX1;
        const double shaped = (std::abs(du) > 1.0e-7) ? (Fc - ln.nlF1) / du : bbdShape(ln.nlX1);
        ln.nlX1 = u;
        ln.nlF1 = Fc;
        // --- dark reconstruction low-pass (2-pole ~6.6 kHz) ---
        float y = biquadTDF2((float)shaped, mLpB0, mLpB1, mLpB2, mLpA1, mLpA2, ln.lpZ1, ln.lpZ2);
        // --- subsonic trim (also removes the polynomial's even-harmonic DC) ---
        ln.hpLp += mHpCoef * (y - ln.hpLp);
        return y - ln.hpLp;
    }
    // f(x) = x - a·x² - b·x³   (gentle, level-independent BBD colour)
    static double bbdShape(double x) { return x - kBbdA * x * x - kBbdB * x * x * x; }
    // Antiderivative F(x) = x²/2 - a·x³/3 - b·x⁴/4 (exact; makes the ADAA aliasing-free).
    static double bbdAntideriv(double x)
    {
        const double x2 = x * x;
        return 0.5 * x2 - (kBbdA / 3.0) * x2 * x - (kBbdB / 4.0) * x2 * x2;
    }

    static void flushDenormals(Lane &ln)
    {
        for (float *p : {&ln.lpZ1, &ln.lpZ2, &ln.hpLp})
            if (std::abs(*p) < 1.0e-30f) *p = 0.0f;
        for (float &s : ln.ap)
            if (std::abs(s) < 1.0e-30f) s = 0.0f;
        if (std::abs(ln.flFbState) < 1.0e-30f) ln.flFbState = 0.0f;
        if (std::abs(ln.flFbLp) < 1.0e-30f) ln.flFbLp = 0.0f;
    }

    static float coefForHz(double hz, double fs)
    {
        const double fc = std::min(std::max(hz, 1.0), 0.45 * fs);
        return (float)(1.0 - std::exp(-2.0 * 3.14159265358979323846 * fc / fs));
    }
    static float coefForMs(float ms, double fs)
    {
        return 1.0f - (float)std::exp(-1.0 / ((double)std::max(0.05f, ms) * 0.001 * fs));
    }
    static void rbjLowpass(double fc, double Q, double fs,
                           float &b0, float &b1, float &b2, float &a1, float &a2)
    {
        const double w0 = 2.0 * 3.14159265358979323846 * fc / fs;
        const double cw = std::cos(w0), sw = std::sin(w0), al = sw / (2.0 * Q);
        const double a0 = 1.0 + al;
        b0 = (float)(((1.0 - cw) * 0.5) / a0);
        b1 = (float)((1.0 - cw) / a0);
        b2 = b0;
        a1 = (float)((-2.0 * cw) / a0);
        a2 = (float)((1.0 - al) / a0);
    }
    static float biquadTDF2(float x, float b0, float b1, float b2, float a1, float a2,
                            float &z1, float &z2)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    double mFs = 48000.0;
    Type mType = kChorus;
    Lfo mLfo; // single shared LFO clock; the stereo lanes read it at a phase offset
    Lane mLaneL, mLaneR;

    // Shared, read-only per sample: dark-wet biquad coeffs + one-pole coefficients.
    float mLpB0 = 1.0f, mLpB1 = 0.0f, mLpB2 = 0.0f, mLpA1 = 0.0f, mLpA2 = 0.0f;
    float mHpCoef = 1.0f;                            // subsonic HP one-pole
    float mFlFbCoef = 1.0f;                          // flanger feedback tone-shape one-pole
    float mTremCoef = 1.0f;                          // tremolo de-click slew
    float mUniHeatCoef = 1.0f, mUniCoolCoef = 1.0f;  // uni-vibe lamp heat/cool
    float mSpread = 0.5f, mSpreadZ = 0.5f; // stereo L/R LFO phase offset: 0 = dual-mono, 1 = 180° anti-phase (Z = de-zippered)

    float mDepth = 0.5f, mMix = 0.5f, mFeedback = 0.0f, mManual = 0.15f, mWave = 0.3f;
    float mDepthZ = 0.5f, mMixZ = 0.5f, mFeedbackZ = 0.0f, mManualZ = 0.15f, mWaveZ = 0.3f, mSmoothK = 0.01f;
    float mFreeRateHz = 1.0f;
    int mSyncIndex = 0;
    double mBpm = 120.0;
    // Host transport, for PPQ phase-resync of the tempo-synced LFO. mPpq is the
    // playhead position in quarter-note beats; the LFO phase is snapped to the beat
    // grid on the rising edge of mPlaying (see maybeResyncPhase()).
    bool mPlaying = false, mWasPlaying = false;
    double mPpq = 0.0;
    bool mPrepared = false;
};

} // namespace nam_rig
