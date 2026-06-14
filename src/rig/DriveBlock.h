#pragma once
// DriveBlock — a 3-slot SERIES rack of drive pedals. Lives in the shared mono
// PRE, after the comp and BEFORE the A/B split: one pedalboard feeding both
// rigs, which is the common real two-amp live rig (board -> splitter -> amps).
//
// Each slot is one voicing, reusing the same Drive / Tone / Level knobs:
//   Off        : out of the path — bit-exact passthrough.
//   Boost      : clean linear gain + tone tilt, no clipping (transparent push).
//   Overdrive  : TS-style — pre low-cut into a soft (tanh) clipper, smooth.
//   Distortion : RAT/DS-style — harder, slightly asymmetric clip, brighter.
//   Fuzz       : high-gain, strongly asymmetric near-square clip, raw.
//
// Anti-aliasing: every clipper uses 1st-order ANTIDERIVATIVE anti-aliasing
// (ADAA). A memoryless clipper makes harmonics above Nyquist that fold back as
// inharmonic "fizz"; ADAA evaluates y = (F(x1)-F(x0))/(x1-x0) from the
// nonlinearity's antiderivative F, strongly reducing that aliasing at ZERO
// latency and a single sample of state. Stacking pedals adds no PDC, and an
// all-Off rack is bit-exact (RigChain bypasses the block when empty).
//
// Tone: a one-pole TILT (post-clip, +/- ~9 dB about a per-voicing pivot; 0.5 =
// flat/transparent) plus a per-voicing pre-emphasis low-cut that tightens the
// clipper. All parameters are atomics. Verified by tests/drive_test.cpp.

#include "Blocks.h"
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

    // True when at least one slot is active. The processor uses this to set the
    // chain bypass so an empty rack is skipped entirely -> bit-exact.
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
        int   clip;        // -1 none (boost), 0 soft (tanh), 1 hard
        float gMin, gMax;  // pre-gain range (linear), log-mapped from Drive knob
        float lowCutHz;    // pre-clip high-pass (tighten); 0 = off
        float bias;        // clip input bias -> asymmetry (even harmonics)
        float pivotHz;     // tone tilt pivot
        float outTrim;     // voicing output compensation
    };

    static Voicing voicingFor(Kind k)
    {
        switch (k)
        {
        case Kind::Boost:      return {-1, 1.0f,   8.0f,   0.0f, 0.00f, 700.0f, 1.00f};
        case Kind::Overdrive:  return { 0, 1.5f,  45.0f, 220.0f, 0.00f, 720.0f, 0.72f};
        case Kind::Distortion: return { 1, 2.0f,  90.0f, 150.0f, 0.12f, 900.0f, 0.50f};
        case Kind::Fuzz:       return { 1, 6.0f, 300.0f,  60.0f, 0.45f, 500.0f, 0.42f};
        case Kind::Off:
        default:               return {-1, 1.0f,   1.0f,   0.0f, 0.00f, 700.0f, 1.00f};
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

            const float preGain = v.gMin * std::pow(v.gMax / v.gMin, s.drive.load()); // log
            const float hpCoef = (v.lowCutHz > 0.0f) ? coefForHz(v.lowCutHz, sr) : 0.0f;
            const float dcSub = (v.clip >= 0) ? clipF(v.clip, v.bias) : 0.0f;
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

                // pre-clip low-cut (one-pole HPF) tightens the clipper
                if (hpCoef > 0.0f)
                {
                    hp += hpCoef * (u - hp);
                    u = u - hp;
                }

                // nonlinearity with 1st-order ADAA (boost = linear)
                float c;
                if (v.clip < 0)
                {
                    c = u;
                }
                else
                {
                    const float xb = u + v.bias;
                    const float d = xb - x0;
                    float y;
                    if (std::abs(d) > 1.0e-6f)
                        y = (clipAD(v.clip, xb) - clipAD(v.clip, x0)) / d;
                    else
                        y = clipF(v.clip, 0.5f * (xb + x0));
                    x0 = xb;
                    c = y - dcSub; // remove the DC the bias introduces
                }

                // tone tilt (post-clip): split low/high about pivot, reweight.
                // At tone=0.5 both gains are 1 -> low+high == c (transparent).
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
        void resetState() { hp = 0.0f; x0 = 0.0f; toneLp = 0.0f; }
    };

    Slot &at(int slot) { return mSlot[juce::jlimit(0, kSlots - 1, slot)]; }

    // ---- nonlinearities: f and its antiderivative F (for ADAA) ----
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
