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
    enum Type   { kOctDown = 0, kOctUp = 1, kPog = 2, kOctavia = 3 };
    enum Engine { kPoly = 0, kGrain = 1 };

    PitchBlock() { setBypassed(true); }

    const char *name() const override { return "Pitch"; }

    void setType(int t)         { mType.store(t < 0 ? 0 : (t > kOctavia ? kOctavia : t)); }
    void setEngine(int e)       { mEngine.store(e == kGrain ? kGrain : kPoly); }
    void setFilter(float v)     { mFilter.store(clamp01(v)); }  // POG resonant LPF cutoff
    void setAttack(float v)     { mAttack.store(clamp01(v)); }  // POG swell attack
    void setUp2(float v)        { mUp2.store(clamp01(v)); }     // POG2 +2 octave (x4) level
    void setQ(float v)          { mQ.store(clamp01(v)); }       // POG2 filter resonance
    void setDetune(float v)     { mDetune.store(clamp01(v)); }  // POG2 upper-octave detune
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
        mPolyUp2.prepare(mSampleRate); mPolyUp2.setRatio(4.0f);
        mPolyLatency = mPolyD1.latency();
        mDry.assign((size_t)std::max(1, mPolyLatency), 0.0f);
        mChorusBuf.assign((size_t)kChorusSize, 0.0f);
        mChorusBase = (float)(mSampleRate * 0.008); // ~8 ms base delay
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
        mWork3.assign((size_t)mb, 0.0f);
        mWork4.assign((size_t)mb, 0.0f);
        mToneLpDn = Biquad::lowpass(mSampleRate, 4000.0);
        mToneLpUp = Biquad::lowpass(mSampleRate, 4000.0);
        mLastToneDnHz = mLastToneUpHz = -1.0f;
        mPogFilter.prepare(mSampleRate);
        // Octavia octave-up fuzz: band-limit before the rectifier + AC-couple after,
        // wrapped in a low-Z fuzz input (pickup loading, delta vs the ~1 MΩ capture).
        // Kept BRIGHT + not over-loaded so it screams rather than sounding starved.
        mPreRectLp = Biquad::lowpass(mSampleRate, 6000.0);
        mAcHp = Biquad::highpass1(mSampleRate, 25.0);
        mIoFuzz.prepare(mSampleRate);
        mIoFuzz.setLoaded(14.0f, 2800.0f, -3.0f, 0.0f, 20.0f, 0.0f);
        reset();
        mPrepared = true;
    }

    void reset() override
    {
        mPolyD1.reset(); mPolyD2.reset(); mPolyUp.reset(); mPolyUp2.reset();
        std::fill(mChorusBuf.begin(), mChorusBuf.end(), 0.0f);
        mChorusPos = 0; mChorusPhase = 0.0f;
        mTracker.reset(); mGrD1.reset(); mGrD2.reset(); mGrUp.reset();
        mToneLpDn.reset(); mToneLpUp.reset();
        mIoGrain.reset(); mGritX1 = 0.0;
        mPogFilter.reset(); mPogAtt = 0.0f;
        mPreRectLp.reset(); mAcHp.reset(); mIoFuzz.reset();
        mFuzzU1 = mFuzzU2 = mRectR1 = mRectR2 = 0.0;
        std::fill(mDry.begin(), mDry.end(), 0.0f); mDryPos = 0;
        mClarityGate = 0.0f;
        mLastW = (float)(2.0 * mSampleRate / 120.0);
    }

    // GRAIN engine = zero latency; POLY = fixed STFT latency. 0 when bypassed.
    double latencySamples() const override
    {
        if (isBypassed()) return 0.0;
        const int type = mType.load();
        if (type == kOctavia) return 0.0;                 // analog-style fuzz, zero latency
        // POG is always the poly (STFT) engine; Down/Up are poly OR grain(=0).
        const bool poly = (type == kPog) || (mEngine.load() == kPoly);
        return poly ? (double)mPolyLatency : 0.0;
    }

    void process(float *mono, int numSamples) override
    {
        if (!mPrepared || numSamples > (int)mWork1.size()) return;
        const int type = mType.load();
        if (type == kOctavia)  octavia(mono, numSamples);       // octave-up fuzz
        else if (type == kPog) polyPog(mono, numSamples);       // full octaver, poly only
        else
        {
            const bool up = (type == kOctUp);
            if (mEngine.load() == kPoly) { up ? polyUp(mono, numSamples)  : polyDown(mono, numSamples); }
            else                         { up ? grainUp(mono, numSamples) : grainDown(mono, numSamples); }
        }
        // Foolproof output guard: transparent below ~0.9, soft-limits peaks so
        // stacking Direct + several octave voices can never hard-clip the amp input.
        for (int i = 0; i < numSamples; ++i) mono[i] = softLimit(mono[i]);
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
        mPolyUp.setRatio(2.0f); // POG may have left this detuned
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
    // POG (Micro POG / POG2): dry + up to 4 octave voices (x0.25/x0.5/x2/x4) with a
    // resonant filter, attack swell, and an upper-octave detune LFO. Micro POG pins
    // the extras (down2/up2 = 0, filter open, attack 0, detune 0).
    void polyPog(float *mono, int numSamples)
    {
        const float dryLvl = mDirect.load();
        const float lD1 = mOct1.load();   // -1 (x0.5)
        const float lD2 = mOct2.load();   // -2 (x0.25)
        const float lU1 = mOctUp.load();  // +1 (x2)
        const float lU2 = mUp2.load();    // +2 (x4)
        const bool rD2 = lD2 > 1.0e-4f, rU2 = lU2 > 1.0e-4f;

        // Exact clean octaves; DETUNE is a modulated-delay CHORUS on the upper
        // voices (a phase vocoder can't cleanly do a sub-semitone shift — its
        // integer-bin mapping quantizes it away — so the beating/thickening is done
        // with a chorus, which is what POG detune sounds like). Depth+rate couple.
        mPolyUp.setRatio(2.0f);
        mPolyUp2.setRatio(4.0f);
        const float det = mDetune.load();
        const bool detOn = det > 1.0e-4f;
        const float chDepth = det * (float)(mSampleRate * 0.004); // up to ~4 ms sweep
        const float chRate = 0.3f + det * 3.0f;                   // ~0.3 .. 3.3 Hz

        float *w1 = mWork1.data(), *w2 = mWork2.data(), *w3 = mWork3.data(), *w4 = mWork4.data();
        std::copy(mono, mono + numSamples, w1); mPolyD1.process(w1, numSamples);   // x0.5
        std::copy(mono, mono + numSamples, w2); mPolyUp.process(w2, numSamples);   // x2
        if (rD2) { std::copy(mono, mono + numSamples, w3); mPolyD2.process(w3, numSamples); }  // x0.25
        if (rU2) { std::copy(mono, mono + numSamples, w4); mPolyUp2.process(w4, numSamples); } // x4

        const float fHz = 300.0f * std::pow(2.0f, mFilter.load() * 4.7f); // ~300 Hz .. ~7.8 kHz
        const float q = 0.7f * std::pow(11.4f, mQ.load());                // ~0.7 .. ~8
        mPogFilter.setCoeffs(fHz, q);
        const float attMs = 0.5f * std::pow(2.0f, mAttack.load() * 11.0f); // 0.5 .. ~1000 ms
        const float attCoef = coefForMs(attMs, mSampleRate);
        const float relCoef = coefForMs(120.0f, mSampleRate);

        float att = mPogAtt;
        for (int i = 0; i < numSamples; ++i)
        {
            const float dry = pushDry(mono[i]);
            const float pres = (std::abs(dry) > 1.0e-3f) ? 1.0f : 0.0f;
            att += (pres > att ? attCoef : relCoef) * (pres - att);
            float up = lU1 * w2[i];
            if (rU2) up += lU2 * w4[i];
            if (detOn) up = detuneChorus(up, chDepth, chRate);
            float voices = lD1 * w1[i] + up;
            if (rD2) voices += lD2 * w3[i];
            const float wet = mPogFilter.tick(voices).lp;
            mono[i] = dryLvl * dry + wet * att;
        }
        mPogFilter.flushDenorms();
        mPogAtt = att;
        mTrackedHz.store(0.0f);
    }

    // ---------- OCTAVIA (octave-up fuzz) : fuzz -> full-wave rectify, mono, 0 latency ----------
    void octavia(float *mono, int numSamples)
    {
        const float drive = std::pow(2.0f, mDryUp.load() * 6.0f); // Fuzz knob -> up to ~64x
        const float octAmt = mOctUp.load();  // octave-defeat blend (0 = fuzz, 1 = octave)
        const float vol = mVolume.load();
        updateTone(mToneLpUp, mLastToneUpHz, 800.0f * std::pow(2.0f, mToneUp.load() * 3.3f));

        // Low-Z fuzz input: pickup loading + coupling (IoStage, setLoaded).
        mIoFuzz.processIn(mono, numSamples);

        double u1 = mFuzzU1, u2 = mFuzzU2, r1 = mRectR1, r2 = mRectR2;
        for (int i = 0; i < numSamples; ++i)
        {
            // Fuzz: cubic soft-clip, 2nd-order ADAA (band-limited hard clip).
            const double u = (double)mono[i] * drive;
            const float f = (float)sat::cubicADAA2(u, u1, u2);
            u2 = u1; u1 = u;
            // Band-limit BEFORE the rectifier so its doubled spectrum stays under Nyquist.
            const float fl = mPreRectLp.processSample(f);
            // Full-wave rectify (asymmetric), 2nd-order ADAA — the octave-up. The
            // rectifier is the worst aliaser, so ADAA matters most here.
            const float r = (float)sat::rectADAA2((double)fl, r1, r2);
            r2 = r1; r1 = (double)fl;
            // Octave-defeat: blend rectified octave vs the (band-limited) pure fuzz.
            const float wet = octAmt * r + (1.0f - octAmt) * fl;
            float y = mAcHp.processSample(wet); // remove the f-f DC term
            y = mToneLpUp.processSample(y);
            mono[i] = kOctaviaMakeup * vol * y;
        }
        mFuzzU1 = u1; mFuzzU2 = u2; mRectR1 = r1; mRectR2 = r2;
        mIoFuzz.processOut(mono, numSamples);
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

    // POG2 detune: mix the up-octave with a slowly-modulated delayed copy of itself
    // (a chorus) -> beating/thickening. depth (samples) + rate (Hz) couple off Detune.
    inline float detuneChorus(float x, float depth, float rate)
    {
        mChorusBuf[(size_t)mChorusPos] = x;
        mChorusPhase += rate / (float)mSampleRate;
        mChorusPhase -= std::floor(mChorusPhase);
        const float lfo = 0.5f * (1.0f - std::cos(6.28318530718f * mChorusPhase)); // 0..1
        const float rp = (float)mChorusPos - (mChorusBase + depth * lfo);
        const int i0 = (int)std::floor(rp);
        const float f = rp - (float)i0;
        const float a = mChorusBuf[(size_t)(i0 & kChorusMask)];
        const float b = mChorusBuf[(size_t)((i0 + 1) & kChorusMask)];
        mChorusPos = (mChorusPos + 1) & kChorusMask;
        return 0.5f * x + 0.5f * (a + f * (b - a));
    }
    void updateTone(Biquad &f, float &last, float hz)
    {
        if (std::abs(hz - last) > 1.0f) { f.copyCoeffsFrom(Biquad::lowpass(mSampleRate, hz)); last = hz; }
    }
    static float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
    // Identity below 0.9, then a soft knee ceilinged at ~1.0 (safety only).
    static float softLimit(float x)
    {
        const float a = std::abs(x);
        if (a <= 0.9f) return x;
        const float s = x < 0.0f ? -1.0f : 1.0f;
        return s * (0.9f + 0.1f * std::tanh((a - 0.9f) * 10.0f));
    }
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
    std::atomic<float> mUp2{0.0f};     // POG2 +2 octave (x4) level
    std::atomic<float> mQ{0.3f};       // POG2 filter resonance
    std::atomic<float> mDetune{0.0f};  // POG2 upper-octave detune

    // Poly engine
    SpectralShifter mPolyD1, mPolyD2, mPolyUp, mPolyUp2;
    Svf mPogFilter;
    float mPogAtt = 0.0f;
    static constexpr int kChorusSize = 2048, kChorusMask = kChorusSize - 1;
    std::vector<float> mChorusBuf;
    int mChorusPos = 0;
    float mChorusPhase = 0.0f, mChorusBase = 384.0f; // ~8 ms base delay @48k
    std::vector<float> mDry;
    int mDryPos = 0, mPolyLatency = 768;
    // Grain engine
    PitchTracker mTracker;
    OctaveShifter mGrD1, mGrD2, mGrUp;
    IoStage mIoGrain;
    static constexpr double kGritG = 2.5, kGritB = 0.15; // germanium grit (curvature/bias)
    double mGritX1 = 0.0;
    // Octavia octave-up fuzz
    Biquad mPreRectLp, mAcHp;
    IoStage mIoFuzz;
    double mFuzzU1 = 0.0, mFuzzU2 = 0.0, mRectR1 = 0.0, mRectR2 = 0.0;
    static constexpr float kOctaviaMakeup = 1.6f;
    float mClarityGate = 0.0f, mLastW = 800.0f, mWSmooth = 0.0014f;
    // shared
    Biquad mToneLpDn, mToneLpUp;
    std::vector<float> mWork1, mWork2, mWork3, mWork4;
    float mLastToneDnHz = -1.0f, mLastToneUpHz = -1.0f;
    std::atomic<float> mTrackedHz{0.0f};
    double mSampleRate = 48000.0;
    bool mPrepared = false;
};

} // namespace nam_rig
