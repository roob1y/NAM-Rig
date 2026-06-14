#pragma once
// DriveBlock — a 3-slot SERIES rack of drive pedals (shared pre, before the
// A/B split: one board feeding both rigs, the common real two-amp live rig).
//
// Voicings are tuned to the MEASURED behaviour of the classic circuits they
// model (ElectroSmash circuit analyses + published responses):
//   Off          : out of the path — bit-exact passthrough.
//   Treble Boost : Dallas Rangemaster — a germanium treble booster. Broad treble
//                  PEAK (~3.5 kHz) keeping the lows near unity + soft asymmetric
//                  germanium clip. Bright, mostly clean, breaks up when pushed.
//   Overdrive    : Ibanez TS808 — the ~720 Hz midrange HUMP into a SYMMETRIC
//                  soft clip (odd harmonics, warm "dirty + clean" layering),
//                  with the gentle top-end roll-off above ~5 kHz.
//   Distortion   : Pro Co RAT — a HARD symmetric clip (diodes to ground) tamed
//                  by the RAT's filter: strong attenuation above ~5 kHz so it is
//                  aggressive without fizz; lows pass, mids emphasised.
//   Fuzz         : Fuzz Face — ASYMMETRIC clipping (one rail soft-ish, the other
//                  to cutoff): a prominent 2nd harmonic and a "tilted" clip.
//
// Signal per slot:  drive gain -> pre low-cut -> mid/treble peak -> waveshaper
//                   (1st-order ADAA) -> post low-pass -> DC blocker -> tone -> level.
//
// Anti-aliasing: each shaper runs 1st-order ANTIDERIVATIVE anti-aliasing (ADAA):
// y = (F(x1)-F(x0))/(x1-x0) from the shaper's antiderivative F (midpoint-f
// fallback for tiny dx). Evaluated in DOUBLE — in float the antiderivative
// subtraction loses precision at small signal and crackles. Zero latency, one
// sample of state, strong fold-back reduction. An all-Off rack is bit-exact.
//
// Base shapers:
//   0 soft (tanh)            — Treble Boost / Overdrive   (asymmetry via input bias)
//   1 hard clip +/-1 (sym)   — Distortion
//   2 hard clip, ASYM rails  — Fuzz (positive rail +1, negative rail -(1-bias))
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
        int   clip;        // 0 soft tanh, 1 hard sym, 2 hard ASYM rails
        float gMin, gMax;  // pre-gain range (linear), log-mapped from Drive
        float lowCutHz;    // pre-shaper one-pole high-pass (tighten); 0 = off
        float midHz, midDb, midQ;  // pre-shaper peak (mid hump / treble peak); 0 dB = off
        float lpHz;        // post-shaper one-pole low-pass (top roll-off); 0 = off
        float bias;        // type 0/1: input bias; type 2: negative-rail = -(1-bias)
        float pivotHz;     // tone tilt pivot
        float outTrim;     // voicing output compensation
    };

    static Voicing voicingFor(Kind k)
    {
        switch (k)
        {                       // clip  gMin    gMax  lowCut   midHz  midDb midQ   lpHz   bias   pivot   outTrim
        case Kind::Boost:      return { 0, 1.0f,  10.0f, 120.0f, 3500.0f, 8.0f, 0.5f,    0.0f, 0.20f, 2500.0f, 0.85f};
        case Kind::Overdrive:  return { 0, 1.5f,  30.0f, 160.0f,  720.0f, 6.0f, 0.8f, 5000.0f, 0.05f,  720.0f, 0.70f};
        case Kind::Distortion: return { 1, 2.0f, 130.0f,  50.0f, 1000.0f, 3.0f, 0.6f, 5000.0f, 0.00f, 1500.0f, 0.42f};
        case Kind::Fuzz:       return { 2, 6.0f, 300.0f,  70.0f,    0.0f, 0.0f, 0.7f,    0.0f, 0.45f,  700.0f, 0.45f};
        case Kind::Off:
        default:               return { 0, 1.0f,   1.0f,   0.0f,    0.0f, 0.0f, 0.7f,    0.0f, 0.00f,  700.0f, 1.00f};
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

            if ((int)k != s.lastKind) // reconfigure the peak only on a voicing change
            {
                s.lastKind = (int)k;
                s.mid = (v.midDb != 0.0f) ? Biquad::peaking(sr, v.midHz, v.midQ, v.midDb)
                                          : Biquad::identity();
            }

            const float preGain = v.gMin * std::pow(v.gMax / v.gMin, s.drive.load()); // log
            const float hpCoef = (v.lowCutHz > 0.0f) ? coefForHz(v.lowCutHz, sr) : 0.0f;
            const float lpCoef = (v.lpHz > 0.0f) ? coefForHz(v.lpHz, sr) : 0.0f;
            const bool useMid = (v.midDb != 0.0f);
            const bool useLp = (v.lpHz > 0.0f);
            const double asym = (v.clip == 2) ? (double)v.bias : 0.0;     // type-2 rail
            const double inBias = (v.clip == 2) ? 0.0 : (double)v.bias;   // type 0/1 input bias
            const float levelLin = std::pow(10.0f, s.levelDb.load() * 0.05f) * v.outTrim;

            const float tilt = (s.tone.load() - 0.5f) * 2.0f;
            const float trebleG = std::pow(10.0f, (tilt * kMaxTiltDb) * 0.05f);
            const float bassG   = std::pow(10.0f, (-tilt * kMaxTiltDb) * 0.05f);
            const float toneCoef = coefForHz(v.pivotHz, sr);

            float hp = s.hp, lpz = s.lp, low = s.toneLp;
            float dcx = s.dcX1, dcy = s.dcY1;
            double x0 = s.x0;

            for (int i = 0; i < numSamples; ++i)
            {
                float u = mono[i] * preGain;

                if (hpCoef > 0.0f) { hp += hpCoef * (u - hp); u = u - hp; } // pre low-cut
                if (useMid) u = s.mid.processSample(u);                     // mid / treble peak

                // waveshaper, 1st-order ADAA in DOUBLE (avoids float cancellation crackle)
                const double xb = (double)u + inBias;
                const double d = xb - x0;
                double y;
                if (std::abs(d) > 1.0e-6)
                    y = (clipAD(v.clip, xb, asym) - clipAD(v.clip, x0, asym)) / d;
                else
                    y = clipF(v.clip, 0.5 * (xb + x0), asym);
                x0 = xb;
                float c = (float)y;

                if (useLp) { lpz += lpCoef * (c - lpz); c = lpz; }   // post low-pass (top roll-off)

                // DC blocker (one-pole HPF ~4 Hz): removes the offset asymmetric
                // clipping introduces, so no static bias subtraction is needed.
                const float dcOut = c - dcx + kDcR * dcy;
                dcx = c; dcy = dcOut; c = dcOut;

                // tone tilt: at tone=0.5 low+high == c (transparent).
                low += toneCoef * (c - low);
                const float high = c - low;
                mono[i] = (low * bassG + high * trebleG) * levelLin;
            }

            s.hp = flush(hp); s.lp = flush(lpz); s.toneLp = flush(low);
            s.dcX1 = flush(dcx); s.dcY1 = flush(dcy); s.x0 = x0;
        }
    }

private:
    static constexpr float kMaxTiltDb = 9.0f;
    static constexpr float kDcR = 0.9995f; // DC blocker pole (~4 Hz corner @ 48k)

    struct Slot
    {
        std::atomic<int> kind{(int)Kind::Off};
        std::atomic<float> drive{0.5f};
        std::atomic<float> tone{0.5f};
        std::atomic<float> levelDb{0.0f};
        float hp = 0.0f, lp = 0.0f, toneLp = 0.0f, dcX1 = 0.0f, dcY1 = 0.0f;
        double x0 = 0.0; // ADAA history in double
        int lastKind = -1;
        Biquad mid; // pre-shaper peak (state preserved across blocks)
        void resetState()
        {
            hp = lp = toneLp = dcX1 = dcY1 = 0.0f; x0 = 0.0;
            lastKind = -1; mid.reset();
        }
    };

    Slot &at(int slot) { return mSlot[juce::jlimit(0, kSlots - 1, slot)]; }

    // ---- base shapers: f and its antiderivative F (for ADAA), in double ----
    // 0 soft (tanh): f=tanh, F=logcosh.  1 hard +/-1: f=clamp, F piecewise.
    // 2 hard ASYM: positive rail +1, negative rail -(1-asym).
    static double clipF(int type, double x, double asym)
    {
        if (type == 0) return std::tanh(x);
        if (type == 1) return x > 1.0 ? 1.0 : (x < -1.0 ? -1.0 : x);
        // type 2 (fuzz): soft saturation on the positive half (tanh -> +1), hard
        // CUTOFF on the negative half (clamp to -lo). Different SHAPES per polarity
        // -> strong even harmonics at all levels (the Fuzz Face character).
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
