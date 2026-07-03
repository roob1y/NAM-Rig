#pragma once
// PitchBlock — pitch effects, mono, DAW rate, pre-amp. After the gate and BEFORE
// the envelope filter / compressor. Default-bypassed (raw chain stays bit-exact).
//
// TWO selectable octave ENGINES (both a Type toggle Down/Up):
//
//   * GRAIN (character) — a pitch-synchronous granular shifter (OctaveShifter.h)
//     driven by the MPM tracker (PitchTracker.h). Mono, ZERO latency, and has the
//     gritty/characterful voice Robbie liked; it glitches on chords by nature
//     (it can only follow one note) — that's part of its vintage character.
//   * POLY (clean) — a phase-vocoder STFT shifter (SpectralShifter.h) that moves
//     every spectral peak at once, so it octaves CHORDS cleanly. Costs a fixed
//     ~16 ms STFT latency (reported for PDC; the dry path is delayed to match).
//
//   OCT_DOWN: x0.5 (OCT1) + x0.25 (OCT2) + DIRECT dry + sub TONE.
//   OCT_UP:   clean x2 up + DRY blend + TONE + VOLUME.
//
// Latency depends on the engine: GRAIN = 0, POLY = fftFrameSize-hop; both 0 when
// bypassed (SoloA bit-exact). The processor re-reports PDC when the engine or the
// on/off state changes. Param IDs kept stable (Tightness -> sub Tone, Fuzz -> up
// Dry). See docs/pitch_env/PITCH_TRACKING_RESEARCH.md. JUCE-free; pitch_test.cpp.

#include "Blocks.h"
#include "Biquad.h"
#include "Svf.h"
#include "Saturation.h"
#include "IoStage.h"
#include "PitchTracker.h"
#include "OctaveShifter.h"
#include "SpectralShifter.h"
#include <atomic>
#include <algorithm>
#include <cmath>
#include <vector>

namespace nam_rig
{

class PitchBlock : public MonoBlock
{
public:
    enum Type   { kOctDown = 0, kOctUp = 1, kPog = 2 };
    enum Engine { kPoly = 0, kGrain = 1 };

    PitchBlock() { setBypassed(true); }

    const char *name() const override { return "Pitch"; }

    void setType(int t)         { mType.store(t < 0 ? 0 : (t > kPog ? kPog : t)); }
    void setEngine(int e)       { mEngine.store(e == kGrain ? kGrain : kPoly); }
    void setFilter(float v)     { mFilter.store(clamp01(v)); }  // POG resonant LPF cutoff
    void setAttack(float v)     { mAttack.store(clamp01(v)); }  // POG swell attack
    void setDirect(float v)     { mDirect.store(clamp01(v)); }
    void setOct1(float v)       { mOct1.store(clamp01(v)); }
    void setOct2(float v)       { mOct2.store(clamp01(v)); }
    void setTightness(float v)  { mToneDn.store(clamp01(v)); }  // -> sub tone
    void setFuzz(float v)       { mDryUp.store(clamp01(v)); }   // -> up dry blend
    void setTone(float v)       { mToneUp.store(clamp01(v)); }
    void setOctave(float v)     { mOctUp.store(clamp01(v)); }
    void setVolume(float v)     { mVolume.store(clamp01(v)); }

    void prepare(const BlockContext &ctx) override
    {
        mSampleRate = ctx.sampleRate;
        // Poly (phase vocoder)
        mPolyD1.prepare(mSampleRate); mPolyD1.setRatio(0.5f);
        mPolyD2.prepare(mSampleRate); mPolyD2.setRatio(0.25f);
        mPolyUp.prepare(mSampleRate); mPolyUp.setRatio(2.0f);
        mPolyLatency = mPolyD1.latency();
        mDry.assign((size_t)std::max(1, mPolyLatency), 0.0f);
        // Grain (tracked granular)
        mTracker.prepare(mSampleRate);
        mGrD1.prepare(mSampleRate); mGrD2.prepare(mSampleRate); mGrUp.prepare(mSampleRate);
        mWSmooth = coefForMs(15.0f, mSampleRate);
        // GRAIN = the vintage character engine: Boss OC-2-style buffered I/O
        // (coupling HPs + gentle top smoothing). POLY stays transparent.
        mIoGrain.prepare(mSampleRate);
        mIoGrain.setBuffered(7.0f, 1.6f, 12000.0f, 0.0f);
        // shared
        const int mb = std::max(1, ctx.maxBlockSize);
        mWork1.assign((size_t)mb, 0.0f);
        mWork2.assign((size_t)mb, 0.0f);
        mToneLpDn = Biquad::lowpass(mSampleRate, 4000.0);
        mToneLpUp = Biquad::lowpass(mSampleRate, 4000.0);
        mLastToneDnHz = mLastToneUpHz = -1.0f;
        mPogFilter.prepare(mSampleRate);
        reset();
        mPrepared = true;
    }

    void reset() override
    {
        mPolyD1.reset(); mPolyD2.reset(); mPolyUp.reset();
        mTracker.reset(); mGrD1.reset(); mGrD2.reset(); mGrUp.reset();
        mToneLpDn.reset(); mToneLpUp.reset();
        mIoGrain.reset(); mGritX1 = 0.0;
        mPogFilter.reset(); mPogAtt = 0.0f;
        std::fill(mDry.begin(), mDry.end(), 0.0f); mDryPos = 0;
        mClarityGate = 0.0f;
        mLastW = (float)(2.0 * mSampleRate / 120.0);
    }

    // GRAIN engine = zero latency; POLY = fixed STFT latency. 0 when bypassed.
    double latencySamples() const override
    {
        if (isBypassed()) return 0.0;
        // POG is always the poly (STFT) engine; Down/Up are poly OR grain(=0).
        const bool poly = (mType.load() == kPog) || (mEngine.load() == kPoly);
        return poly ? (double)mPolyLatency : 0.0;
    }

    void process(float *mono, int numSamples) override
    {
        if (!mPrepared || numSamples > (int)mWork1.size()) return;
        const int type = mType.load();
        if (type == kPog) { polyPog(mono, numSamples); return; } // full octaver, poly only
        const bool up = (type == kOctUp);
        if (mEngine.load() == kPoly) { up ? polyUp(mono, numSamples)  : polyDown(mono, numSamples); }
        else                         { up ? grainUp(mono, numSamples) : grainDown(mono, numSamples); }
    }

    float trackedHz() const { return mTrackedHz.load(); }

private:
    // ---------- POLY (phase vocoder) : clean, polyphonic, ~16 ms latency ----------
    void polyDown(float *mono, int numSamples)
    {
        const float direct = mDirect.load(), l1 = mOct1.load(), l2 = mOct2.load();
        const bool run2 = l2 > 1.0e-4f;
        float *w1 = mWork1.data(), *w2 = mWork2.data();
        std::copy(mono, mono + numSamples, w1);
        mPolyD1.process(w1, numSamples);
        if (run2) { std::copy(mono, mono + numSamples, w2); mPolyD2.process(w2, numSamples); }
        updateTone(mToneLpDn, mLastToneDnHz, 500.0f * std::pow(2.0f, mToneDn.load() * 4.0f));
        for (int i = 0; i < numSamples; ++i)
        {
            const float dry = pushDry(mono[i]);
            const float sub = mToneLpDn.processSample(l1 * w1[i] + (run2 ? l2 * w2[i] : 0.0f));
            mono[i] = direct * dry + sub;
        }
        mTrackedHz.store(0.0f);
    }

    void polyUp(float *mono, int numSamples)
    {
        const float dryLvl = mDryUp.load(), upLvl = mOctUp.load(), vol = mVolume.load();
        float *w1 = mWork1.data();
        std::copy(mono, mono + numSamples, w1);
        mPolyUp.process(w1, numSamples);
        updateTone(mToneLpUp, mLastToneUpHz, 800.0f * std::pow(2.0f, mToneUp.load() * 3.3f));
        for (int i = 0; i < numSamples; ++i)
        {
            const float dry = pushDry(mono[i]);
            mono[i] = vol * (dryLvl * dry + upLvl * mToneLpUp.processSample(w1[i]));
        }
        mTrackedHz.store(0.0f);
    }

    // ---------- POG (poly full octaver) : dry + sub + up, resonant filter, swell ----------
    void polyPog(float *mono, int numSamples)
    {
        const float dryLvl = mDirect.load(), subLvl = mOct1.load(), upLvl = mOctUp.load();
        float *w1 = mWork1.data(), *w2 = mWork2.data();
        std::copy(mono, mono + numSamples, w1);
        mPolyD1.process(w1, numSamples);   // x0.5 sub
        std::copy(mono, mono + numSamples, w2);
        mPolyUp.process(w2, numSamples);   // x2 up

        const float fHz = 300.0f * std::pow(2.0f, mFilter.load() * 4.7f); // ~300 Hz .. ~7.8 kHz
        mPogFilter.setCoeffs(fHz, kPogQ);
        // Attack = organ/volume swell: the WET voices fade in with this time constant
        // whenever a note is present (auto-retriggers per note); 0 = instant.
        const float attMs = 0.5f * std::pow(2.0f, mAttack.load() * 11.0f); // 0.5 .. ~1000 ms
        const float attCoef = coefForMs(attMs, mSampleRate);
        const float relCoef = coefForMs(120.0f, mSampleRate);

        float att = mPogAtt;
        for (int i = 0; i < numSamples; ++i)
        {
            const float dry = pushDry(mono[i]); // delayed to match the STFT wet voices
            const float pres = (std::abs(dry) > 1.0e-3f) ? 1.0f : 0.0f;
            att += (pres > att ? attCoef : relCoef) * (pres - att);
            const float wet = mPogFilter.tick(subLvl * w1[i] + upLvl * w2[i]).lp;
            mono[i] = dryLvl * dry + wet * att;
        }
        mPogFilter.flushDenorms();
        mPogAtt = att;
        mTrackedHz.store(0.0f);
    }

    // ---------- GRAIN (tracked granular) : character, mono, zero latency ----------
    void grainDown(float *mono, int numSamples)
    {
        const float direct = mDirect.load(), l1 = mOct1.load(), l2 = mOct2.load();
        mIoGrain.processIn(mono, numSamples); // Boss buffered front-end (tracker + dry see this)
        mTracker.process(mono, numSamples);
        const bool voiced = mTracker.voiced();
        const float t0 = mTracker.periodSamples();
        if (voiced && t0 > 4.0f) mLastW = 2.0f * t0;
        const float wT = mLastW, ws = mWSmooth;
        const float clarTarget = voiced ? 1.0f : 0.0f;
        const float clarCoef = coefForMs(35.0f, mSampleRate);
        updateTone(mToneLpDn, mLastToneDnHz, 500.0f * std::pow(2.0f, mToneDn.load() * 4.0f));
        float clarG = mClarityGate; double gx1 = mGritX1;
        for (int i = 0; i < numSamples; ++i)
        {
            const float x = mono[i];
            clarG += clarCoef * (clarTarget - clarG);
            const float s1 = mGrD1.process(x, wT, 0.5f, ws);
            const float s2 = mGrD2.process(x, wT, 0.25f, ws);
            // OC-2 germanium grit on the sub (asymmetric ADAA soft-clip -> even+odd
            // harmonics, level-dependent) — the vintage character vs the clean Poly.
            const double raw = (double)(l1 * s1 + l2 * s2);
            const float grit = (float)sat::tanhADAA1(raw, gx1, kGritG, kGritB);
            gx1 = raw;
            const float sub = mToneLpDn.processSample(grit);
            mono[i] = direct * x + sub * clarG;
        }
        mGritX1 = gx1;
        mClarityGate = clarG;
        mIoGrain.processOut(mono, numSamples); // Boss buffered back-end
        mTrackedHz.store(voiced ? mTracker.hz() : 0.0f);
    }

    void grainUp(float *mono, int numSamples)
    {
        const float dryLvl = mDryUp.load(), upLvl = mOctUp.load(), vol = mVolume.load();
        mIoGrain.processIn(mono, numSamples);
        mTracker.process(mono, numSamples);
        const bool voiced = mTracker.voiced();
        const float t0 = mTracker.periodSamples();
        if (voiced && t0 > 4.0f) mLastW = 2.0f * t0;
        const float wT = mLastW, ws = mWSmooth;
        const float clarTarget = voiced ? 1.0f : 0.0f;
        const float clarCoef = coefForMs(35.0f, mSampleRate);
        updateTone(mToneLpUp, mLastToneUpHz, 800.0f * std::pow(2.0f, mToneUp.load() * 3.3f));
        float clarG = mClarityGate; double gx1 = mGritX1;
        for (int i = 0; i < numSamples; ++i)
        {
            const float x = mono[i];
            clarG += clarCoef * (clarTarget - clarG);
            const double raw = (double)mGrUp.process(x, wT, 2.0f, ws);
            const float grit = (float)sat::tanhADAA1(raw, gx1, kGritG, kGritB);
            gx1 = raw;
            const float u = mToneLpUp.processSample(grit);
            mono[i] = vol * (dryLvl * x + upLvl * u * clarG);
        }
        mGritX1 = gx1;
        mClarityGate = clarG;
        mIoGrain.processOut(mono, numSamples);
        mTrackedHz.store(voiced ? mTracker.hz() : 0.0f);
    }

    inline float pushDry(float x)
    {
        const float d = mDry[(size_t)mDryPos];
        mDry[(size_t)mDryPos] = x;
        if (++mDryPos >= (int)mDry.size()) mDryPos = 0;
        return d;
    }
    void updateTone(Biquad &f, float &last, float hz)
    {
        if (std::abs(hz - last) > 1.0f) { f.copyCoeffsFrom(Biquad::lowpass(mSampleRate, hz)); last = hz; }
    }
    static float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
    static float coefForMs(float ms, double sr)
    {
        return 1.0f - (float)std::exp(-1.0 / (std::max(0.05f, ms) * 0.001 * sr));
    }

    std::atomic<int>   mType{kOctDown};
    std::atomic<int>   mEngine{kPoly};
    std::atomic<float> mDirect{1.0f};
    std::atomic<float> mOct1{0.7f};
    std::atomic<float> mOct2{0.0f};
    std::atomic<float> mToneDn{0.5f};
    std::atomic<float> mDryUp{0.5f};
    std::atomic<float> mToneUp{0.5f};
    std::atomic<float> mOctUp{0.7f};
    std::atomic<float> mVolume{0.7f};
    std::atomic<float> mFilter{0.7f};  // POG resonant LPF cutoff
    std::atomic<float> mAttack{0.0f};  // POG swell attack

    // Poly engine
    SpectralShifter mPolyD1, mPolyD2, mPolyUp;
    Svf mPogFilter;
    float mPogAtt = 0.0f;
    static constexpr float kPogQ = 2.0f; // POG resonant filter Q (moderate)
    std::vector<float> mDry;
    int mDryPos = 0, mPolyLatency = 768;
    // Grain engine
    PitchTracker mTracker;
    OctaveShifter mGrD1, mGrD2, mGrUp;
    IoStage mIoGrain;
    static constexpr double kGritG = 2.5, kGritB = 0.15; // germanium grit (curvature/bias)
    double mGritX1 = 0.0;
    float mClarityGate = 0.0f, mLastW = 800.0f, mWSmooth = 0.0014f;
    // shared
    Biquad mToneLpDn, mToneLpUp;
    std::vector<float> mWork1, mWork2;
    float mLastToneDnHz = -1.0f, mLastToneUpHz = -1.0f;
    std::atomic<float> mTrackedHz{0.0f};
    double mSampleRate = 48000.0;
    bool mPrepared = false;
};

} // namespace nam_rig
