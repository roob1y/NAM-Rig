#pragma once
// IoStage — a reusable pedal INPUT + OUTPUT stage (impedance loading, coupling
// caps, output level), the same idea EnvFilterBlock already uses for its pedal
// front-end. The guitar is DI'd into the plugin, so the pickup loading already
// happened at capture; we can only model the DELTA from a reference ~1 MΩ
// buffered/amp interface, plus the pedal's coupling high-passes and output level
// (docs/pitch_env/PEDAL_VOICINGS_PLAN.md §A).
//
//   front-end (processIn):  input coupling HP -> input-Z loading high-shelf CUT
//                           -> optional RF/Miller LP -> input level
//   back-end  (processOut): output coupling HP -> output level
//
// Buffered pedals (POG ~2 MΩ, Boss/Whammy ~1 MΩ) present a high input impedance
// that barely loads the guitar -> shelf = 0 dB, just the DC-blocking coupling HPs
// (~7 Hz) + a gentle top smoothing = near-transparent. Only a LOW-Z fuzz input
// (Octavia, Fuzz-Face-class ~5-10 kΩ) gets a real loading shelf: a high-shelf cut
// (calibration anchor: a resistive load <=47 kΩ makes the pickup resonant peak
// vanish — Lemme) + a small level drop. JUCE-free (Biquad.h); tests/pitch_test.cpp.

#include "Biquad.h"
#include <cmath>

namespace nam_rig
{

class IoStage
{
public:
    void prepare(double sampleRate)
    {
        mSr = sampleRate > 0.0 ? sampleRate : 48000.0;
        setTransparent();
    }

    // Identity passthrough (default) — bit-exact, no colour.
    void setTransparent()
    {
        mInHp = Biquad::identity(); mShelf = Biquad::identity(); mInLp = Biquad::identity();
        mOutHp = Biquad::identity();
        mInLevel = 1.0f; mOutLevel = 1.0f; mEnabled = false;
        reset();
    }

    // Buffered pedal (high input Z): just the coupling HPs + optional gentle HF
    // smoothing + output level. inLpHz <= 0 disables the smoothing.
    void setBuffered(float inHpHz, float outHpHz, float inLpHz, float outLevelDb)
    {
        configure(inHpHz, 0.0f, 0.0f, 0.0f, inLpHz, outHpHz, outLevelDb);
    }

    // Low-Z (fuzz) input: coupling HP + a loading high-shelf CUT (shelfCutDb < 0)
    // at ~pickup-resonance + a small input level drop; near-transparent output.
    void setLoaded(float inHpHz, float shelfHz, float shelfCutDb, float inLevelDb,
                   float outHpHz, float outLevelDb)
    {
        configure(inHpHz, shelfHz, shelfCutDb, inLevelDb, 0.0f, outHpHz, outLevelDb);
    }

    void reset()
    {
        mInHp.reset(); mShelf.reset(); mInLp.reset(); mOutHp.reset();
    }

    bool enabled() const { return mEnabled; }

    // Front-end: colours what the effect (and its tracker) sees.
    void processIn(float *buf, int n)
    {
        if (!mEnabled) return;
        mInHp.process(buf, n);
        mShelf.process(buf, n);
        mInLp.process(buf, n);
        if (mInLevel != 1.0f) for (int i = 0; i < n; ++i) buf[i] *= mInLevel;
    }

    // Back-end: colours the final mix.
    void processOut(float *buf, int n)
    {
        if (!mEnabled) return;
        mOutHp.process(buf, n);
        if (mOutLevel != 1.0f) for (int i = 0; i < n; ++i) buf[i] *= mOutLevel;
    }

private:
    void configure(float inHpHz, float shelfHz, float shelfCutDb, float inLevelDb,
                   float inLpHz, float outHpHz, float outLevelDb)
    {
        mInHp  = (inHpHz  > 0.0f) ? Biquad::highpass1(mSr, inHpHz)  : Biquad::identity();
        mShelf = (shelfCutDb != 0.0f) ? Biquad::highshelf(mSr, shelfHz, shelfCutDb) : Biquad::identity();
        mInLp  = (inLpHz  > 0.0f) ? Biquad::lowpass1(mSr, inLpHz)   : Biquad::identity();
        mOutHp = (outHpHz > 0.0f) ? Biquad::highpass1(mSr, outHpHz) : Biquad::identity();
        mInLevel  = dbToLin(inLevelDb);
        mOutLevel = dbToLin(outLevelDb);
        mEnabled = true;
        reset();
    }
    static float dbToLin(float db) { return db == 0.0f ? 1.0f : std::pow(10.0f, db / 20.0f); }

    double mSr = 48000.0;
    Biquad mInHp, mShelf, mInLp, mOutHp;
    float mInLevel = 1.0f, mOutLevel = 1.0f;
    bool mEnabled = false;
};

} // namespace nam_rig
