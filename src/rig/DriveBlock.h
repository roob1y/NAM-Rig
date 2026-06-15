#pragma once
// DriveBlock — a 3-slot SERIES rack of drive pedals (shared pre, before the
// A/B split: one board feeding both rigs, the common real two-amp live rig).
//
// Voicings are tuned to the MEASURED behaviour of the classic circuits they
// model (from published circuit analyses + measured frequency responses):
//   Off          : out of the path — bit-exact passthrough.
//   Treble Boost : a germanium treble booster (1960s style). Broad treble
//                  PEAK (~3.5 kHz) keeping the lows near unity + soft asymmetric
//                  germanium clip. Bright, mostly clean, breaks up when pushed.
//   Overdrive    : a green-box mid-hump overdrive — the ~720 Hz midrange HUMP into a SYMMETRIC
//                  soft clip (odd harmonics, warm "dirty + clean" layering),
//                  with the gentle top-end roll-off above ~5 kHz.
//   Distortion   : a hard-clip distortion — a HARD symmetric clip (diodes to ground) tamed
//                  by a post-clip filter: strong attenuation above ~5 kHz so it is
//                  aggressive without fizz; lows pass, mids emphasised.
//   Fuzz         : a vintage germanium fuzz — ASYMMETRIC clipping (one rail soft-ish, the other
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
    void setAutoGain(bool on) { mAutoGain.store(on); } // level-compensate Drive/Tone (default off)
    void setRange(int slot, int r) { at(slot).range.store(r); } // treble-boost cap switch: 0 Treble/1 Mid/2 Full
    void setOn(int slot, bool on) { at(slot).on.store(on); }            // footswitch (default on)
    void setModel(int slot, int m) { at(slot).model.store(m); }         // model within the category

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
        int   clip;        // 0 soft tanh, 1 hard sym, 2 hard ASYM rails
        float gMin, gMax;  // pre-gain range (linear), log-mapped from Drive
        float lowCutHz;    // pre-shaper one-pole high-pass (tighten); 0 = off
        float midHz, midDb, midQ;  // pre-shaper peak (mid hump / treble peak); 0 dB = off
        float lpHz;        // post-shaper one-pole low-pass (top roll-off); 0 = off
        float bias;        // type 0/1: input bias; type 2: negative-rail = -(1-bias)
        float pivotHz;     // tone tilt pivot
        float outTrim;     // voicing output compensation
        float shapeTrack;  // 0 = pre-shaper EQ always on; 1 = EQ (low-cut + mid)
                           //     scales with the Drive knob (the mid hump blooms
                           //     with gain instead of being a fixed band-pass)
        float midPost;     // 0 = mid peak PRE-clip (treble booster input cap);
                           //     1 = POST-clip (overdrive/distortion tone stack -> peak freq is
                           //     level-stable instead of dragged down by clipping)
    };

    // A specific pedal MODEL inside a category (Type). A category can hold several
    // models; the UI picks the Type (category) then the model. hasRange = the
    // model exposes the 3-way input-cap switch (treble booster only).
    struct Model { const char *name, *sub; Voicing v; bool hasRange; };

    static const Model *modelsFor(Kind cat, int &count)
    {                       // clip  gMin    gMax  lowCut   midHz  midDb midQ   lpHz   bias   pivot   outTrim shp post
        static const Model boost[] = {
            {"Range '65", "treble boost  rev2",
             { 0, 1.0f, 20.0f, 2600.0f,   0.0f, 0.0f, 0.7f,    0.0f, 0.20f, 2500.0f, 0.95f, 0.0f, 0.0f}, true},
            {"EP Boost", "FET clean boost",
             { 0, 1.0f,  6.0f,  40.0f, 5000.0f, 3.0f, 0.6f,    0.0f, 0.05f, 1000.0f, 0.95f, 0.0f, 1.0f}, false},
        };
        static const Model od[] = {
            {"Overdrive", "mid-hump overdrive",
             { 0, 1.5f, 30.0f, 560.0f,  780.0f, 6.0f, 0.7f, 1300.0f, 0.05f,  720.0f, 1.10f, 1.0f, 1.0f}, false},
        };
        static const Model dist[] = {
            {"Distortion", "hard-clip distortion",
             { 1, 2.0f,130.0f,  50.0f, 1000.0f, 3.0f, 0.6f, 5000.0f, 0.00f, 1500.0f, 0.42f, 0.0f, 1.0f}, false},
        };
        static const Model fuzz[] = {
            {"Fuzz", "germanium fuzz",
             { 2, 6.0f,300.0f,  70.0f,    0.0f, 0.0f, 0.7f,    0.0f, 0.45f,  700.0f, 0.45f, 0.0f, 0.0f}, false},
        };
        switch (cat)
        {
        case Kind::Boost:      count = 2; return boost;
        case Kind::Overdrive:  count = 1; return od;
        case Kind::Distortion: count = 1; return dist;
        case Kind::Fuzz:       count = 1; return fuzz;
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

    static Voicing voicingFor(Kind c, int m)
    {
        int n = 0; const Model *a = modelsFor(c, n);
        if (!a || n == 0) return { 0, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.7f, 0.0f, 0.0f, 700.0f, 1.0f, 0.0f, 0.0f };
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
            const double asym = (v.clip == 2) ? (double)v.bias : 0.0;     // type-2 rail
            const double inBias = (v.clip == 2) ? 0.0 : (double)v.bias;   // type 0/1 input bias
            float levelLin = std::pow(10.0f, s.levelDb.load() * 0.05f) * v.outTrim;
            if (mAutoGain.load()) // OFF by default: Drive then naturally pushes the amp harder
                levelLin *= driveMakeup(k, s.drive.load()) * toneMakeup(k, s.tone.load());

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

                if (hpCoef > 0.0f) { hp += hpCoef * (u - hp); const float hipassed = u - hp; u += shapeAmt * (hipassed - u); } // pre low-cut (drive-scaled)
                if (useMid && !midPost) { const float m = s.mid.processSample(u); u += shapeAmt * (m - u); } // pre-clip peak (treble booster)

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

                if (useMid && midPost) { const float m = s.mid.processSample(c); c += shapeAmt * (m - c); } // post-clip peak (OD/dist tone stack; level-stable)
                if (useLp) { lpz += lpCoef * (c - lpz); c += shapeAmt * (lpz - c); } // post low-pass (drive-scaled)

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
        std::atomic<int> range{0};
        std::atomic<int> model{0};
        std::atomic<bool> on{true};
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
    static float driveMakeup(Kind k, float drive)
    {
        static const float B[6] = {2.259f, 1.547f, 1.129f, 0.899f, 0.773f, 0.705f};
        static const float O[6] = {2.297f, 1.524f, 1.112f, 0.909f, 0.807f, 0.738f};
        static const float D[6] = {2.270f, 1.262f, 1.029f, 0.973f, 0.961f, 0.956f};
        static const float F[6] = {1.266f, 1.078f, 1.013f, 0.991f, 0.985f, 0.985f};
        const float *t = (k == Kind::Boost) ? B : (k == Kind::Overdrive) ? O
                       : (k == Kind::Distortion) ? D : F;
        return lerpTbl(t, 6, drive);
    }
    static float toneMakeup(Kind k, float tone)
    {
        static const float B[5] = {0.473f, 0.751f, 1.000f, 0.892f, 0.595f};
        static const float O[5] = {0.554f, 0.857f, 1.000f, 0.756f, 0.473f};
        static const float D[5] = {0.451f, 0.728f, 1.000f, 0.914f, 0.607f};
        static const float F[5] = {0.643f, 0.953f, 1.000f, 0.708f, 0.436f};
        const float *t = (k == Kind::Boost) ? B : (k == Kind::Overdrive) ? O
                       : (k == Kind::Distortion) ? D : F;
        return lerpTbl(t, 5, tone);
    }

    static float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
    static float coefForHz(double hz, double sr)
    {
        return 1.0f - (float)std::exp(-2.0 * 3.14159265358979323846 * hz / sr);
    }
    static float flush(float v) { return std::abs(v) < 1.0e-30f ? 0.0f : v; }

    Slot mSlot[kSlots];
    std::atomic<bool> mAutoGain{false};
    double mSampleRate = 48000.0;
    bool mPrepared = false;
};

} // namespace nam_rig
    