#pragma once
// DriveBlock — a 3-slot SERIES rack of drive pedals (shared pre, before the
// A/B split: one board feeding both rigs, the common real two-amp live rig).
//
// Voicings (each reinterprets the same Drive / Tone / Level knobs):
//   Off         : out of the path — bit-exact passthrough.
//   Treble Boost: Rangemaster-style — an aggressive low-cut for the treble lift
//                 into a germanium-ish asymmetric soft stage; a bright, mostly
//                 clean boost that breaks up when pushed.
//   Overdrive   : Tube-Screamer-style — a midrange HUMP (~720 Hz peak) feeding
//                 an ASYMMETRIC soft (tanh) clip; warm, vocal, mid-forward.
//   Distortion  : RAT/DS-style — high-gain SMOOTH saturation with an upper-mid
//                 presence bump; aggressive without the buzzy hard-clip edge.
//   Fuzz        : strongly asymmetric, near-square (hard) clip; raw and gated,
//                 cleans up as you back off the input level.
//
// Signal per slot:  drive gain -> pre low-cut -> mid/presence peak -> waveshaper
//                   (1st-order ADAA) -> tone tilt -> level.
//
// Anti-aliasing: every shaper runs 1st-order ANTIDERIVATIVE anti-aliasing
// (ADAA): y = (F(x1)-F(x0))/(x1-x0) from the shaper's antiderivative F, with a
// midpoint-f fallback when |dx| is tiny. Zero latency, one sample of state,
// strong fold-back reduction. An all-Off rack is bit-exact (RigChain bypasses
// the block when empty).
//
// Two base shapers cover every voicing; asymmetry (even harmonics) comes from an
// input bias, removed as DC afterwards:
//   soft (tanh): warm, rounded  — Treble Boost / Overdrive / Distortion
//   hard (clip): raw, square    — Fuzz
//
// The mid/presence peak is a shared RBJ Biquad (see Biquad.h), reconfigured only
// when the slot's voicing changes. Tone: per-voicing one-pole TILT about a pivot
// (+/- ~9 dB; 0.5 = transparent). All params atomic. Verified by tests/drive_test.cpp.

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

    // ---- parameters (per slot, thread-safe) ----
    void setKind(int slot, int k)      { at(slot).kind.store(k); }
    void setDrive(int slot, float v)   { at(slot).drive.store(clamp01(v)); }   // 0..1
    void setTone(int slot, float v)    { at(slot).tone.store(clamp01(v)); }    // 0..1 (0.5 flat)
    void setLevelDb(int slot, float v) { at(slot).levelDb.store(v); }          // -12..+12

    bool anyActive() const
    {
        for (int s = 0; s < kSlots; ++s)
            if ((Kind)mSlot[s].kind.load() != Kind::Off)
                return true;
        return false;
    }

    // Per-voicing character (single source of truth for process + tests).
    struct Voicing
    {
        int   clip;        // base shaper: 0 soft (tanh), 1 hard clip
        float gMin, gMax;  // pre-gain range (linear), log-mapped from Drive knob
        float lowCutHz;    // pre-shaper one-pole high-pass (tighten); 0 = off
        float midHz, midDb, midQ;  // pre-shaper peak (hump/presence); 0 dB = off
        float bias;        // input bias -> clipping asymmetry (even harmonics)
        float pivotHz;     // tone tilt pivot
        float outTrim;     // voicing output compensation
    };

    static Voicing voicingFor(Kind k)
    {
        switch (k)
        {                       // clip  gMin    gMax  lowCut   midHz  midDb midQ  bias   pivot   outTrim
        case Kind::Boost:      return { 0, 1.0f,  10.0f, 600.0f,   0.0f, 0.0f, 0.7f, 0.25f, 1600.0f, 0.90f};
        case Kind::Overdrive:  return { 0, 1.5f,  35.0f, 160.0f, 720.0f, 6.0f, 0.8f, 0.30f,  720.0f, 0.70f};
        case Kind::Distortion: return { 0, 2.0f, 120.0f, 120.0f,1800.0f, 4.0f, 0.7f, 0.12f, 1200.0f, 0.45f};
        case Kind::Fuzz:       return { 1, 6.0f, 350.0f,  70.0f,   0.0f, 0.0f, 0.7f, 0.45f,  700.0f, 0.40f};
        case Kind::Off:
        default:               return { 0, 1.0f,   1.0f,   0.0f,   0.0f, 0.0f, 0.7f, 0.00f,  700.0f, 1.00f};
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
            if (k == Kind::Off)
                continue;
            const Voicing v = voicingFor(k);

            // (Re)configure the pre-shaper peak only when the voicing changes,
            // so its filter state isn't cleared every block.
            if ((int)k != s.lastKind)
            {
                s.lastKind = (int)k;
                s.mid = (v.midDb != 0.0f) ? Biquad::peaking(sr, v.midHz, v.midQ, v.midDb)
                                          : Biquad::identity();
            }

            const float preGain = v.gMin * std::pow(v.gMax / v.gMin, s.drive.load()); // log
            const float hpCoef = (v.lowCutHz > 0.0f) ? coefForHz(v.lowCutHz, sr) : 0.0f;
            const bool useMid = (v.midDb != 0.0f);
            const float dcSub = clipF(v.clip, v.bias);
            const float levelLin = std::pow(10.0f, s.levelDb.load() * 0.05f) * v.outTrim;

            // tone tilt: tone 0..1 -> tilt -1..+1 -> +/- maxTiltDb about pivot.
            const float tilt = (s.tone.load() - 0.5f) * 2.0f;
            const float trebleG = std::pow(10.0f, (tilt * kMaxTiltDb) * 0.05f);
            const float bassG   = std::pow(10.0f, (-tilt * kMaxTiltDb) * 0.05f);
            const float toneCoef = coefForHz(v.pivotHz, sr);

            float hp = s.hp, x0 = s.x0, low = s.toneLp;

            for (int i = 0; i < numSamples; ++i)
            {
                float u = mono[i] * preGain;

                if (hpCoef > 0.0f)              // pre-shaper low-cut (tighten)
                {
                    hp += hpCoef * (u - hp);
                    u = u - hp;
                }
                if (useMid)                    // pre-shaper mid / presence peak
                    u = s.mid.processSample(u);

                // waveshaper with 1st-order ADAA; asymmetry via the input bias.
                const float xb = u + v.bias;
                const float d = xb - x0;
                float y;
                if (std::abs(d) > 1.0e-6f)
                    y = (clipAD(v.clip, xb) - clipAD(v.clip, x0)) / d;
                else
                    y = clipF(v.clip, 0.5f * (xb + x0));
                x0 = xb;
                const float c = y - dcSub;     // remove the DC the bias introduces

                // tone tilt (post-shaper): at tone=0.5 low+high == c (transparent).
                low += toneCoef * (c - low);
                const float high = c - low;
                const float toned = low * bassG + high * trebleG;

                mono[i] = toned * levelLin;
            }

            s.hp = flush(hp);
            s.x0 = x0;
            s.toneLp = flush(low);
        }
    }

private:
    static constexpr float kMaxTiltDb = 9.0f;

    struct Slot
    {
        std::atomic<int> kind{(int)Kind::Off};
        std::atomic<float> drive{0.5f};
        std::atomic<float> tone{0.5f};
        std::atomic<float> levelDb{0.0f};
        float hp = 0.0f, x0 = 0.0f, toneLp = 0.0f;
        int lastKind = -1;
        Biquad mid; // pre-shaper peak (state preserved across blocks)
        void resetState()
        {
            hp = 0.0f; x0 = 0.0f; toneLp = 0.0f;
            lastKind = -1;
            mid.reset();
        }
    };

    Slot &at(int slot) { return mSlot[juce::jlimit(0, kSlots - 1, slot)]; }

    // ---- base shapers: f and its antiderivative F (for ADAA) ----
    // clip 0 = soft (tanh):    f = tanh(x),  F = log(cosh(x))
    // clip 1 = hard clip +/-1: f = clamp,    F = piecewise quadratic/linear
    static float clipF(int type, float x)
    {
        if (type == 0)
            return std::tanh(x);
        return x > 1.0f ? 1.0f : (x < -1.0f ? -1.0f : x);
    }
    static float clipAD(int type, float x)
    {
        if (type == 0)
            return logCosh(x);
        const float a = std::abs(x);
        return a <= 1.0f ? 0.5f * x * x : (a - 0.5f);
    }
    static float logCosh(float x)
    {
        const float a = std::abs(x);
        return a + std::log1p(std::exp(-2.0f * a)) - 0.69314718056f; // log(cosh x)
    }

    static float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
    static float coefForHz(double hz, double sr)
    {
        return 1.0f - (float)std::exp(-2.0 * 3.14159265358979323846 * hz / sr);
    }
    static float flush(float v) { return std::abs(v) < 1.0e-30f ? 0.0f : v; }

    Slot mSlot[kSlots];
    double mSampleRate = 48000.0;
    bool mPrepared = false;
};

} // namespace nam_rig
