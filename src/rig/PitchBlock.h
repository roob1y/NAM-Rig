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
    enum Type   { kOctDown = 0, kOctUp = 1, kPog = 2, kOctavia = 3, kWhammy = 4 };
    enum Engine { kPoly = 0, kGrain = 1 };

    PitchBlock() { setBypassed(true); }

    const char *name() const override { return "Pitch"; }

    void setType(int t)         { mType.store(t < 0 ? 0 : (t > kWhammy ? kWhammy : t)); }
    void setEngine(int e)       { mEngine.store(e == kGrain ? kGrain : kPoly); }
    void setWhammyMode(int m)   { mWMode.store(m < 0 ? 0 : (m > 4 ? 4 : m)); } // target interval
    void setWhammy(float v)     { mWhammyPos.store(clamp01(v)); }              // treadle 0..1
    void setWhammyPoly(bool p)  { mWhammyPolyMode.store(p); } // Chords (poly PV) vs Classic (mono)
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
        mWhammy.prepare(mSampleRate);
        mWhammyPoly.prepare(mSampleRate); mWhammyPoly.setRatio(1.0f);
        mWSmooth = coefForMs(15.0f, mSampleRate);
        // GRAIN = the vintage character engine: Boss OC-2-style buffered I/O
        // (coupling HPs + gentle top smoothing). POLY stays transparent.
        mIoGrain.prepare(mSampleRate);
        mIoGrain.setBuffered(7.0f, 1.6f, 12000.0f, 0.0f);
        // OC-2 classic divider filters: detection band-limit + sub output smoothing.
        mDetLp1 = Biquad::lowpass(mSampleRate, kDetLpHz);
        mDetLp2 = Biquad::lowpass(mSampleRate, kDetLpHz);
        mOctLp1 = Biquad::lowpass(mSampleRate, kOctLpHz);
        mOctLp2 = Biquad::lowpass(mSampleRate, kOctLpHz);
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
        mWhammy.reset(); mWhammyPoly.reset(); mWhammyRatio = 1.0f; mWhammyWet = 0.0f;
        mToneLpDn.reset(); mToneLpUp.reset();
        mIoGrain.reset();
        mDetLp1.reset(); mDetLp2.reset(); mOctLp1.reset(); mOctLp2.reset();
        mDetPeak = 0.0f; mGate = 0.0f; mDPrev = 0.0f; mArmed = false; mDetCutHz = -1.0f;
        mFf1 = false; mFf2 = false; mEdgeSamples = 0; mLastPeriod = 0;
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
        if (type == kOctavia) return 0.0;                        // fuzz = zero latency
        if (type == kWhammy) return mWhammyPolyMode.load() ? (double)mPolyLatency : 0.0; // Chords vs Classic
        if (type == kOctDown && mEngine.load() == kGrain) return 0.0; // OC-2 classic divider = 0 latency
        // POGs (and the clean phase-vocoder down/up) use the STFT subs -> STFT latency.
        return (double)mPolyLatency;
    }

    void process(float *mono, int numSamples) override
    {
        if (!mPrepared || numSamples > (int)mWork1.size()) return;
        const int type = mType.load();
        if (type == kWhammy)   whammy(mono, numSamples);        // continuous pitch bend
        else if (type == kOctavia)  octavia(mono, numSamples);  // octave-up fuzz
        else if (type == kPog) polyPog(mono, numSamples);       // full octaver, poly only
        else if (type == kOctUp)   polyUp(mono, numSamples);   // clean +1 octave (phase vocoder)
        else if (mEngine.load() == kGrain) grainDown(mono, numSamples); // OC-2 classic divider (0 latency)
        else                       polyDown(mono, numSamples); // clean sub (phase vocoder)
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

    // ---------- WHAMMY (continuous pitch bend) : granular shifter, mono, 0 latency ----------
    void whammy(float *mono, int numSamples)
    {
        static constexpr float kCents[5] = { 2400.0f, 1200.0f, 700.0f, -1200.0f, -2400.0f };
        const float targetCents = kCents[mWMode.load()];
        const float treadle = mWhammyPos.load();
        // Treadle -> shift ratio in the LOG (cents) domain: heel (0) = unison,
        // toe (1) = the full mode interval.
        const float targetRatio = std::pow(2.0f, treadle * targetCents / 1200.0f);
        // Blend to PURE DRY at the heel so "no whammy" is clean — the shifter itself
        // colours/warbles even at unison, so we crossfade it in as the treadle moves.
        // The wet blend is SMOOTHED per-sample: the knob is control-rate, so a raw
        // per-block wet stepped audibly (crackle in the bottom range of the knob).
        const float wetTarget = clamp01(treadle * 10.0f); // fully wet by ~0.1 treadle
        const float wetCoef = coefForMs(6.0f, mSampleRate);
        float wetS = mWhammyWet;

        float *w1 = mWork1.data();
        if (mWhammyPolyMode.load())
        {
            // CHORDS: phase vocoder — clean on chords, STFT latency. Dry delayed to match.
            mWhammyRatio += 0.5f * (targetRatio - mWhammyRatio);
            mWhammyPoly.setRatio(mWhammyRatio);
            std::copy(mono, mono + numSamples, w1);
            mWhammyPoly.process(w1, numSamples);
            for (int i = 0; i < numSamples; ++i)
            {
                const float dry = pushDry(mono[i]);
                wetS += wetCoef * (wetTarget - wetS);
                mono[i] = (1.0f - wetS) * dry + wetS * w1[i];
            }
            mWhammyWet = wetS;
            mTrackedHz.store(0.0f);
            return;
        }
        // CLASSIC: mono granular shift, zero latency, warbles on chords (authentic).
        mTracker.process(mono, numSamples);
        const bool voiced = mTracker.voiced();
        const float t0 = mTracker.periodSamples();
        if (voiced && t0 > 4.0f) mLastW = 2.0f * t0;
        const float wT = mLastW, ws = mWSmooth;
        const float rSlew = coefForMs(20.0f, mSampleRate);
        float ratio = mWhammyRatio;
        for (int i = 0; i < numSamples; ++i)
        {
            const float dry = mono[i];
            ratio += rSlew * (targetRatio - ratio);
            const float sh = mWhammy.process(mono[i], wT, ratio, ws);
            wetS += wetCoef * (wetTarget - wetS);
            mono[i] = (1.0f - wetS) * dry + wetS * sh; // heel = dry, no shifter warble
        }
        mWhammyRatio = ratio;
        mWhammyWet = wetS;
        mTrackedHz.store(voiced ? mTracker.hz() : 0.0f);
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

    // ---------- OC-2 : the CLASSIC analog divider (frequency division), 0 latency ----------
    // The real OC-2 is NOT a pitch shifter — it low-passes the note to a near-sine,
    // squares it (peak-hold comparator, stable as the note decays), divides with
    // flip-flops (÷2 = OCT1, ÷4 = OCT2), and reconstructs the sub-octave by
    // sign-flipping a SILICON half-wave-rectified copy of the note every cycle -> a
    // smooth sub that carries the note's own timbre, zero latency, phase-locked
    // (no warble on single notes; glitches on chords by nature, like the real one).
    // The MPM tracker only DEBOUNCES the comparator (rejects harmonic double-triggers);
    // it never delays the audio. Flip at the carrier's zero-crossing = declick.
    void grainDown(float *mono, int numSamples)
    {
        const float direct = mDirect.load(), l1 = mOct1.load(), l2 = mOct2.load();
        mIoGrain.processIn(mono, numSamples);                 // Boss buffered front-end
        mTracker.process(mono, numSamples);                   // pitch reference (no audio delay)
        const bool voiced = mTracker.voiced();
        const float t0 = mTracker.periodSamples();
        const long trackerGap = (voiced && t0 > 1.0f) ? (long)(0.6f * t0) : 0;
        // KEY to killing the gurgle: tune the detection LPF to the tracked
        // FUNDAMENTAL so the comparator always sees a clean sine (a fixed LPF lets
        // harmonics through on many notes -> extra zero-crossings -> the flip-flop
        // mis-counts -> gurgle). Cutoff ~1.3*f0 (fundamental passes, 2nd+ harmonics
        // well down); re-derived per block, state preserved (no click).
        if (voiced)
        {
            float cut = mTracker.hz() * 1.3f;
            cut = cut < 90.0f ? 90.0f : (cut > 1600.0f ? 1600.0f : cut);
            if (std::abs(cut - mDetCutHz) > 2.0f)
            {
                mDetLp1.copyCoeffsFrom(Biquad::lowpass(mSampleRate, cut));
                mDetLp2.copyCoeffsFrom(Biquad::lowpass(mSampleRate, cut));
                mDetCutHz = cut;
            }
        }
        const float clarTarget = voiced ? 1.0f : 0.0f;
        const float peakDecay = coefForMs(180.0f, mSampleRate);
        const float gAtt = coefForMs(3.0f, mSampleRate), gRel = coefForMs(120.0f, mSampleRate);
        const float clarCoef = coefForMs(35.0f, mSampleRate);

        float peak = mDetPeak, gate = mGate, dPrev = mDPrev, clarG = mClarityGate;
        bool armed = mArmed, f1 = mFf1, f2 = mFf2;
        long edge = mEdgeSamples, lastPeriod = mLastPeriod;
        for (int i = 0; i < numSamples; ++i)
        {
            const float x = mono[i];
            const float d = mDetLp2.processSample(mDetLp1.processSample(x)); // near-sine carrier
            const float ad = std::abs(d);
            peak = (ad > peak) ? ad : peak - (peak - ad) * peakDecay;        // peak-hold "AGC" ref
            const bool live = peak > kGateFloor;
            const float gTgt = live ? 1.0f : 0.0f;
            gate += (gTgt > gate ? gAtt : gRel) * (gTgt - gate);
            clarG += clarCoef * (clarTarget - clarG);
            const long refractory = std::max(trackerGap, (long)(0.5f * (float)lastPeriod));
            const float hyst = 0.05f * peak;
            if (d < -hyst) armed = true;
            ++edge;
            if (live && armed && dPrev <= 0.0f && d > 0.0f && edge >= refractory)
            {
                const bool prev1 = f1; f1 = !f1; if (f1 && !prev1) f2 = !f2;
                if (edge > 1 && edge < (long)mSampleRate) lastPeriod = edge;
                edge = 0; armed = false;
            }
            dPrev = d;
            const float hw = (d > 0.0f) ? d : kSiLeak * d;    // silicon half-wave (small leak)
            const float s1 = mOctLp1.processSample(hw * (f1 ? 1.0f : -1.0f));
            const float s2 = mOctLp2.processSample(hw * (f2 ? 1.0f : -1.0f));
            const float g = gate * clarG;
            mono[i] = direct * x + (l1 * s1 + l2 * s2) * g;
        }
        mDetPeak = flush(peak); mGate = flush(gate); mDPrev = flush(dPrev); mClarityGate = flush(clarG);
        mArmed = armed; mFf1 = f1; mFf2 = f2; mEdgeSamples = edge; mLastPeriod = lastPeriod;
        mIoGrain.processOut(mono, numSamples); // Boss buffered back-end
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
    static float flush(float v) { return std::abs(v) < 1.0e-30f ? 0.0f : v; }
    static constexpr float kDetLpHz = 650.0f;    // OC-2 detection band-limit (near-sine)
    static constexpr float kOctLpHz = 2200.0f;   // OC-2 sub output smoothing
    static constexpr float kGateFloor = 5.0e-4f; // OC-2 silence gate (post input-cal)
    static constexpr float kSiLeak = 0.05f;      // silicon half-wave negative leak
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
    OctaveShifter mGrD1, mGrD2, mGrUp, mWhammy;
    SpectralShifter mWhammyPoly;       // Whammy Chords (poly) engine
    std::atomic<int>   mWMode{1};      // Whammy target interval (default +1 oct)
    std::atomic<float> mWhammyPos{0.0f}; // treadle
    std::atomic<bool>  mWhammyPolyMode{false}; // Chords (poly) vs Classic (mono)
    float mWhammyRatio = 1.0f;         // slewed shift ratio
    float mWhammyWet = 0.0f;           // smoothed dry/wet blend (anti-crackle)
    IoStage mIoGrain;
    // OC-2 classic divider state
    Biquad mDetLp1, mDetLp2, mOctLp1, mOctLp2;
    float mDetPeak = 0.0f, mGate = 0.0f, mDPrev = 0.0f, mDetCutHz = -1.0f;
    bool mArmed = false, mFf1 = false, mFf2 = false;
    long mEdgeSamples = 0, mLastPeriod = 0;
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
