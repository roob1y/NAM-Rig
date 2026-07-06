#pragma once
// CabDynamicsBlock — the "Dynamic Cab" DELTA wrapper around the existing static
// IR convolution (CabBlock). 3Sigma captures are full speaker+cabinet+mic
// snapshots: box resonance, cone colour and the small-signal impedance curve are
// ALREADY in the IR. This block adds ONLY what a static linear IR physically
// cannot: LEVEL-DEPENDENT and TIME-VARIANT deviation. Every stage is a delta on
// top of the IR and collapses to nothing at rest.
//
// Full derivation: docs/cabdyn/DESIGN.md (written before this code), upgraded
// by docs/cabdyn/PHYSICS_UPGRADE.md which pushes the block from broadband-
// envelope-driven to CONE-DISPLACEMENT-driven and adds the couplings a real
// speaker has that a static IR cannot: a shared excursion model (RBJ LP2 at
// the box resonance -> xd/xs/dispPush) re-drives A1/B3/B1; an LF->HF
// intermodulation delta (Bl(x) AM + Doppler FM, sidebands-only two-tap, base
// rate); per-cab Fs from the IR (setSpeakerResonance, LfResonance.h); thermal
// voice-coil compression (multi-second sag); and modal shaping of the breakup
// exciter. All additive-delta, all bit-exact at rest (T1/T8 unchanged).
//
// Signal flow (the block owns NO convolver — it wraps the engine already in the
// codebase via two process stages):
//
//   processPre():  [in] -> A. Reactive Impedance Delta -> B. Cone Breakup Delta -> [out to IR conv]
//   ( existing CabBlock::process runs the static IR here )
//   processPost(): [in from IR conv] -> C. Enclosure Air/Damping Delta -> [out]
//
// Two instances run (Cab A / Cab B lanes); ALL state is per-instance.
//
// The DELTA / bit-exact contract: every stage is y = x + (macro*envelope)*delta(x).
// When the controlling macro is 0 the delta term is *0, so the stage is skipped
// (early return, buffer untouched). Therefore ALL THREE MACROS AT 0 => byte-
// identical output (memcmp==0). A stage engages/disengages by ramping to the
// transparent edge THEN leaving the path (house CutFilters pattern), so turning a
// macro to 0 fades out click-free rather than snapping.
//
// NO loudness compensation, EVER. Envelope drive is physical modelling only (a
// real speaker behaves differently when pushed). There is no RMS makeup, no
// auto-level, no broadband gain that restores level. The single gain-*reduction*
// (low-band cone power compression) drops level when pushed and never adds it back.
//
// Modulation ceilings (all small, all bounded): A1 90Hz resonance +3 dB; A2 2kHz
// inductance shelf -3 dB; B midband drive <=3.8x (band 0.8-3.8 kHz); B low-band
// compression -2 dB; C diffuse air wet mix <=0.12 (~ -18 dB).
//
// JUCE-free core (Biquad.h only) so it compiles in a plain offline g++ harness,
// verified by tests/cabdyn_test.cpp. Zero added latency on the dry path (deltas
// are ADDED; dry passes straight through).

#include "Biquad.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <vector>

namespace nam_rig
{

class CabDynamicsBlock
{
public:
    // ======================= 2x polyphase halfband oversampler =======================
    // Linear-phase halfband FIR, coefficients derived in design() from a Kaiser-
    // windowed sinc with the even offset taps forced to exactly zero (the halfband
    // property). Run as two polyphase branches: up() turns one base sample into two
    // 2x samples, down() turns two 2x samples into one base sample. No library.
    //
    // The clipper's odd harmonics of a <=3.8 kHz fundamental reach the 7th (>24 kHz)
    // which would fold at 48 k; at 2x (96 k) they stay below the intermediate Nyquist
    // and the decimator's deep stopband removes them on the way down. L=65 taps
    // (center index 32 is even -> the even-offset taps are the zeroed ones); the
    // 30 kHz decimator stopband measures ~ -96 dB (test T2a).
    struct Halfband
    {
        static constexpr int L = 65;        // taps (L % 4 == 1 -> center C is even)
        static constexpr int C = (L - 1) / 2; // 32
        float h[L] = {0};                   // prototype: DC gain 1, halfband
        float xh[C + 1] = {0};              // up: input history (newest at 0)
        float wh[L] = {0};                  // down: 2x history (newest at 0)

        static double i0(double x) // modified Bessel I0 (Kaiser window)
        {
            double s = 1.0, t = 1.0, x2 = x * x / 4.0;
            for (int k = 1; k < 60; ++k)
            {
                t *= x2 / ((double)k * k);
                s += t;
                if (t < 1.0e-14 * s) break;
            }
            return s;
        }

        void design()
        {
            constexpr double kPi = 3.14159265358979323846;
            const double beta = 9.0, i0b = i0(beta); // deep, narrow-transition halfband
            double sum = 0.0;
            for (int k = 0; k < L; ++k)
            {
                const int m = k - C;
                const double t = 0.5 * (double)m;
                const double s = (m == 0) ? 1.0 : std::sin(kPi * t) / (kPi * t); // sinc
                double v = 0.5 * s; // ideal halfband LP, cutoff pi/2, DC gain 1
                const double r = (double)m / (double)C;
                const double w = i0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0b;
                v *= w;
                if (m != 0 && (m % 2 == 0)) v = 0.0; // force even-offset taps to zero
                h[k] = (float)v;
                sum += v;
            }
            for (int k = 0; k < L; ++k) h[k] = (float)(h[k] / sum); // normalize DC to 1
            reset();
        }

        void reset()
        {
            for (int i = 0; i <= C; ++i) xh[i] = 0.0f;
            for (int i = 0; i < L; ++i) wh[i] = 0.0f;
        }

        // one base sample -> two 2x samples (interpolator, gain-2 halfband)
        inline void up(float x, float &u0, float &u1)
        {
            for (int i = C; i > 0; --i) xh[i] = xh[i - 1];
            xh[0] = x;
            u0 = 2.0f * h[C] * xh[C / 2];            // even phase = single center tap
            float s = 0.0f;
            for (int k = 1; k < L; k += 2) s += 2.0f * h[k] * xh[(k - 1) / 2]; // odd phase
            u1 = s;
        }

        // two 2x samples -> one base sample (decimator, DC-gain-1 halfband)
        inline float down(float w0, float w1)
        {
            for (int i = L - 1; i > 1; --i) wh[i] = wh[i - 2];
            wh[1] = w0;
            wh[0] = w1;
            float y = h[C] * wh[C];                  // center even tap
            for (int k = 1; k < L; k += 2) y += h[k] * wh[k]; // odd taps
            return y;
        }
    };

    // Integer group delay (base samples) of an up()->down() round trip. Only the
    // ADDED harmonic delta carries this delay; the dry path is undelayed.
    static constexpr int kOsGroupDelay = Halfband::C; // 32 samples @ any fs (~0.67 ms @48k)

    // ======================= Schroeder allpass w/ in-loop LP damping =======================
    struct DampAllpass
    {
        std::vector<float> buf;
        int size = 1, pos = 0;
        float lp = 0.0f, dampCoef = 1.0f;

        void prepare(int n, double dampHz, double fs)
        {
            size = std::max(1, n);
            buf.assign((size_t)size, 0.0f);
            pos = 0;
            lp = 0.0f;
            constexpr double kPi = 3.14159265358979323846;
            dampCoef = (float)(1.0 - std::exp(-2.0 * kPi * dampHz / fs));
        }
        void reset()
        {
            std::fill(buf.begin(), buf.end(), 0.0f);
            pos = 0;
            lp = 0.0f;
        }
        // Flush denormals from BOTH the damping one-pole AND the recursive delay
        // line (the line feeds back through process(), so a denormal there keeps
        // recirculating in the offline g++ harness that has no ScopedNoDenormals).
        void flushDenormals()
        {
            if (std::fabs(lp) < 1.0e-30f) lp = 0.0f;
            for (float &v : buf)
                if (std::fabs(v) < 1.0e-30f) v = 0.0f;
        }
        inline float process(float x, float g)
        {
            float d = buf[(size_t)pos];
            lp += dampCoef * (d - lp); // one-pole LP damping inside the loop
            d = lp;
            const float w = x + g * d;
            const float y = d - g * w; // allpass
            buf[(size_t)pos] = w;
            if (++pos >= size) pos = 0;
            return y;
        }
    };

    // ================================ lifecycle ================================
    void prepare(double sampleRate, int /*maxBlockSize*/ = 512)
    {
        mFs = sampleRate > 0.0 ? sampleRate : 48000.0;

        // Smoothers: ~25 ms macro de-zip; ~25 ms mix/size de-zip.
        mMacroK = onePoleK(0.025);
        mMixK   = onePoleK(0.025);
        // Envelope: 8 ms attack / 160 ms release (slow, program-dependent).
        mEnvAtk = onePoleK(0.008);
        mEnvRel = onePoleK(0.160);
        mEnvAtkPost = mEnvAtk;
        mEnvRelPost = mEnvRel;
        // Band-envelope follower for describing-function fundamental cancellation
        // (Stage B1) — fast enough to track note dynamics, slow enough to stay
        // program-dependent (no audio-rate modulation of G).
        mBandAtk = onePoleK(0.005);
        mBandRel = onePoleK(0.040);

        // Stage B fixed crossover (isolate the driven midband) + low-comp band.
        mHp800  = Biquad::highpass(mFs, std::min(800.0, 0.45 * mFs));
        mLp3800 = Biquad::lowpass(mFs, std::min(3800.0, 0.45 * mFs));

        // ---- PHYSICS UPGRADE prepare (docs/cabdyn/PHYSICS_UPGRADE.md) ----
        // Excursion follower (§1): 3 ms attack / 100 ms release on |xd|.
        mDEnvAtk = onePoleK(0.003);
        mDEnvRel = onePoleK(0.100);
        // Thermal integrator (§5): tau = 3.5 s, attack = release.
        mThermK  = onePoleK((double)kThermTau);
        // Intermodulation ring (§3): kDop = fs * 10.2 us (samples); integer
        // center tap tauC = ceil(kDop)+2 (Hermite needs +/-2 guard); ring is
        // the next pow2 >= tauC + kDop + 4 (16 @ 48k).
        mImKDop = (float)(mFs * kDopUs);
        mImTauC = (int)std::ceil((double)mImKDop) + 2;
        int need = mImTauC + (int)std::ceil((double)mImKDop) + 4;
        int rlen = 1;
        while (rlen < need) rlen <<= 1;
        mImRing.assign((size_t)rlen, 0.0f);
        mImMask = rlen - 1;
        // Excursion LP2 + per-cab-tuned filters from the default resonance; the
        // control-rate boundary retunes them when an IR estimate arrives.
        retuneResonance(kFsDefault, /*force*/ true);

        mHb.design();
        buildGtable();

        // Stage C subsonic high-pass on the wet enclosure output (see processPost).
        mEncHp = Biquad::highpass(mFs, std::min((double)kEncSubHpHz, 0.45 * mFs));

        // Stage C: THREE archetype networks, prime delays (<10 ms) scaled to fs.
        // Cab Size crossfades A->B->C in two segments (see processPost), so the box
        // bump glides continuously across a wide, musical range instead of morphing
        // between two far-apart bumps. Calibrated WITH the subsonic HP in path (its
        // phase shifts the dry+wet interference peak). Summed-bump tunings:
        //   A (tight, guitar small/tight)  ~103 Hz   Size 0.0
        //   B (mid,   guitar 4x12 / big)    ~62 Hz   Size 0.5
        //   C (deep,  big bass cab)         ~35 Hz   Size 1.0
        static const int a48[3] = {43, 71, 97};      // tight   (~103 Hz)
        static const int b48[3] = {47, 73, 101};     // mid     (~62 Hz)
        static const int c48[3] = {127, 191, 263};   // deep    (~35 Hz, bass)
        const double sc = mFs / 48000.0;
        for (int i = 0; i < 3; ++i)
        {
            mApA[i].prepare(std::max(2, (int)std::lround(a48[i] * sc)), kDampA_Hz, mFs);
            mApB[i].prepare(std::max(2, (int)std::lround(b48[i] * sc)), kDampB_Hz, mFs);
            mApC[i].prepare(std::max(2, (int)std::lround(c48[i] * sc)), kDampC_Hz, mFs);
        }

        reset();
        mPrepared = true;
    }

    void reset()
    {
        mAgeSmPre = mThumpSmPre = mAgeSmPost = mThumpSmPost = mSizeSm = 0.0f;
        mPreWasActive = mPostWasActive = false;
        mEnvPre = mEnvPost = 0.0f;
        mBandEnv = 0.0f;
        mEncMixSm = 0.0f;
        mA1.reset();
        mA2.reset();
        mLowShelf.reset();
        mHp800.reset();
        mLp3800.reset();
        mHb.reset();
        for (int i = 0; i < 3; ++i) { mApA[i].reset(); mApB[i].reset(); mApC[i].reset(); }
        mCtrl = 0;
        // PHYSICS UPGRADE: consume the current resonance target (re-tune from cold) and
        // clear all new state so a re-prepare/reset starts transparent (cold coil).
        mPromN = clamp01(mPromTarget.load(std::memory_order_relaxed) / 8.0f);
        retuneResonance(mFsTarget.load(std::memory_order_relaxed), /*force*/ true);
        mDEnv = 0.0f; mDispPush = 0.0f; mXs = 0.0f;
        mPwr = 0.0f; mThermGain = 1.0f; mThermDbg = 0.0f;
        mImPos = 0; mImWet = 0.0f;
        std::fill(mImRing.begin(), mImRing.end(), 0.0f);
        rebuildStageA(0.0f, 0.0f); // identity at rest
        mA1Db = mA2Db = 0.0f;
        mDriveDbg = 1.0f;
    }

    // =============================== parameters ===============================
    // Exactly three macros, floats 0..1, all default 0 = bit-exact bypass.
    void setAgeDrive(float v) { mAgeTarget   = clamp01(v); }
    void setThump(float v)    { mThumpTarget = clamp01(v); }
    void setCabSize(float v)  { mSizeTarget  = clamp01(v); }

    // Whole-block bypass. Independent of the macros (which stay at their set
    // values, so the UI knobs are remembered). When bypassed, the effective
    // targets read as 0, so the existing smoother ramps every stage down to the
    // transparent edge click-free and the active-gate then early-returns -> the
    // buffer is byte-identical to input, exactly like all-macros-0.
    void setBypassed(bool b) { mBypassed = b; }
    bool isBypassed() const  { return mBypassed; }

    // Per-cab LF resonance from the IR analysis (docs/cabdyn/PHYSICS_UPGRADE.md §4).
    // Called from the MESSAGE thread on IR load (rare); the audio thread consumes
    // these atomics at the control-rate boundary and retunes the excursion LP2, the
    // A1/B3 base tunings and the modal centers state-preservingly. clear() falls the
    // block back to the generic 90 Hz / 0 dB default (an IR that failed/estimate
    // invalid must clear so a stale per-cab tuning never lingers).
    void setSpeakerResonance(float fsHz, float promDb)
    {
        mFsTarget.store(fsHz, std::memory_order_relaxed);
        mPromTarget.store(promDb, std::memory_order_relaxed);
    }
    void clearSpeakerResonance()
    {
        mFsTarget.store(kFsDefault, std::memory_order_relaxed);
        mPromTarget.store(0.0f, std::memory_order_relaxed);
    }

    // ---- Factory rig presets (0..1 macro triples) --------------------------
    // Physically-motivated starting points, one per popular amp->cab pairing.
    // Age tracks SPEAKER WEAR (vintage / broken-in cone), NOT preamp gain: modern
    // hi-fi cabs (fresh, tight) get LOW Age; old broken-in cabs get HIGH Age.
    // Thump = low-end bloom; Size = enclosure box (tight -> big). These set the
    // plugin macros only; the loaded IR still supplies the cab's static voicing.
    // "group" ("Guitar" / "Bass") lets the UI show section headings; presets are
    // ordered by group. Bass cabs are big boxes -> high Size (the block's deepest
    // box tuning is ~61 Hz); Thump high for low-end weight; Age low for modern
    // hi-fi rigs, moderate for vintage Ampeg grind.
    struct Preset { const char *name; const char *group; float age, thump, size; };
    static const Preset *presets(int *count = nullptr) // array of numPresets() entries
    {
        static const Preset kP[] = {
            // ---- Guitar (Size re-fit for the 3-network sweep; box-bump Hz noted) ----
            { "Recto 4x12",   "Guitar", 0.20f, 0.50f, 0.65f }, // modern, tight, fresh V30 - low wear  (~66 Hz)
            { "Dumble 2x12",  "Guitar", 0.30f, 0.30f, 0.36f }, // boutique, lightly broken-in           (~80 Hz)
            { "Twin 2x12",    "Guitar", 0.35f, 0.25f, 0.28f }, // clean but vintage cones               (~88 Hz)
            { "Deluxe 1x12",  "Guitar", 0.45f, 0.20f, 0.20f }, // old, well-worn small open-back         (~93 Hz)
            { "Bassman 4x10", "Guitar", 0.45f, 0.35f, 0.43f }, // old, loose, broken-in (gtr)            (~76 Hz)
            { "JCM800 4x12",  "Guitar", 0.50f, 0.40f, 0.52f }, // 80s, breaks up, moderately aged        (~69 Hz)
            { "AC30 2x12",    "Guitar", 0.55f, 0.25f, 0.28f }, // alnico Blue, early breakup, vintage    (~88 Hz)
            { "Plexi 4x12",   "Guitar", 0.60f, 0.45f, 0.52f }, // worn Greenbacks, earliest breakup      (~69 Hz)
            // ---- Bass (big boxes -> deep box tuning via network C) ----
            { "SVT 8x10",     "Bass",   0.35f, 0.60f, 0.77f }, // Ampeg fridge - sealed, grind when pushed (~54 Hz)
            { "B-15 1x15",    "Bass",   0.45f, 0.55f, 0.74f }, // vintage flip-top, warm & round          (~60 Hz)
            { "Ampeg 4x10",   "Bass",   0.25f, 0.55f, 0.83f }, // modern ported punchy 4x10               (~45 Hz)
            { "Aguilar 2x12", "Bass",   0.15f, 0.50f, 0.85f }, // hi-fi, clean, tight ported              (~40 Hz)
            { "Acoustic 360", "Bass",   0.30f, 0.65f, 1.00f }, // 1x18 folded horn, very deep             (~35 Hz)
        };
        if (count) *count = (int)(sizeof(kP) / sizeof(kP[0]));
        return kP;
    }
    static int numPresets() { int n = 0; presets(&n); return n; }

    double latencySamples() const { return 0.0; } // dry path undelayed

    // ============================ pre-conv processing ============================
    // Stage A (reactive impedance delta) + Stage B (band-limited cone breakup).
    void processPre(float *buf, int numSamples)
    {
        if (!mPrepared) return;   // halfband undesigned / G-table zero before prepare()
        if (!preActive()) { mPreWasActive = false; return; } // bit-exact bypass

        if (!mPreWasActive) resetPreState(); // inactive->active edge: no stale env/filters
        mPreWasActive = true;

        for (int i = 0; i < numSamples; ++i)
        {
            const float x = buf[i];

            // ---- smoothers + envelope (per sample; pre owns its OWN macro state so
            // running both stages can't double-step the de-zip) ----
            mAgeSmPre   += mMacroK * (ageTgt()   - mAgeSmPre);
            mThumpSmPre += mMacroK * (thumpTgt() - mThumpSmPre);
            const float r = std::fabs(x);
            mEnvPre += (r > mEnvPre ? mEnvAtk : mEnvRel) * (r - mEnvPre);
            const float push = pushOf(mEnvPre);

            // ---- Excursion model (PHYSICS_UPGRADE §1): cone displacement is a 2nd-
            // order resonant LP of the pre-conv INPUT at the box resonance. xd is the
            // calibrated displacement, xs=tanh(xd) the signed bounded excursion that
            // drives the IM stage; dEnv (3ms/100ms) -> dispPush drives coefficients. ----
            const float xd = kXCal * mExcLp.processSample(x);
            mXs = std::tanh(xd);
            const float axd = std::fabs(xd);
            mDEnv += (axd > mDEnv ? mDEnvAtk : mDEnvRel) * (axd - mDEnv);
            {
                const float dp = (mDEnv - kDispLo) / (kDispHi - kDispLo);
                mDispPush = dp < 0.0f ? 0.0f : (dp > 1.0f ? 1.0f : dp);
            }

            // ---- control-rate block: consume the per-cab resonance atomics, rebuild
            // Stage A from (push, dispPush), and recompute the thermal series gain.
            // env/dispPush/pwr all move slowly so the coeffs step in tiny increments. ----
            if (mCtrl == 0)
            {
                const float fsT = mFsTarget.load(std::memory_order_relaxed);
                mPromN = clamp01(mPromTarget.load(std::memory_order_relaxed) / 8.0f);
                retuneResonance(fsT, /*force*/ false); // state-preserving on change only
                rebuildStageA(push, mDispPush);
                // Thermal droop (§5): -1.5 dB * clamp01(pwr/0.5) * Age, reduction only.
                // Age=0 -> droopDb=0 -> gain exactly 1.0f (bit-exact bypass preserved).
                const float pwrN = clamp01(mPwr / kThermFull);
                mThermDbg = -kThermDb * pwrN * mAgeSmPre;
                mThermGain = (mThermDbg == 0.0f) ? 1.0f : dbToLin(mThermDbg);
            }
            if (++mCtrl >= kCtrl) mCtrl = 0;

            // ---- thermal power integrator (§5): one-pole on the INPUT x^2 (terminal-
            // voltage proxy), tau=3.5 s. Reads the pre-delta input, NOT the block's
            // own output -> no self-feedback path by construction. The input is
            // clamped at 8x the calibrated full-power point so a signal far hotter
            // than the rig calibration can't wind the integrator up and leave the
            // droop pinned long after the signal stops (bounded recovery ~2 tau). ----
            const float xx = x * x;
            mPwr += mThermK * ((xx < kThermInMax ? xx : kThermInMax) - mPwr);

            // ---- Stage A: series impedance-delta filters (identity at rest) ----
            float s = mA2.processSample(mA1.processSample(x));
            // thermal voice-coil sag: broadband series gain (can't comb), folded in
            // BEFORE the band split so breakup sees the sagged drive (motor droops).
            s *= mThermGain;
            // ---- Stage B3: dynamic low-band cone POWER COMPRESSION as a clean series
            // magnitude low-shelf (identity at rest; a shelf can't comb the dry path) ----
            s = mLowShelf.processSample(s);

            // ---- shared HP800 (PHYSICS_UPGRADE §3): hp feeds both the B1 breakup band
            // (band = LP3800(hp)) and the IM ring. Run every sample while active so the
            // crossover/ring states stay warm (re-engage click-free). ----
            const float hp = mHp800.processSample(s);
            const float band = mLp3800.processSample(hp);

            // ---- Stage B1: band-limited cone breakup as a HARMONICS-ONLY exciter,
            // then MODAL-shaped (§6). The delta has no fundamental, so series-filtering
            // it shapes the 'cry' spectrum without re-combing the dry path. ----
            const float Wb = mAgeSmPre; // engage = Age (0 at rest)
            if (ageTgt() > 0.0f || Wb > 1.0e-6f)
            {
                // breakup drive gains an excursion term (§2): rebalanced env + dispPush,
                // ceiling 3.8x. Midband level (push) still drives it; excursion adds LF
                // stiffening of the cone edge (earlier/harder breakup) + free LF->mid IMD.
                float drive = 1.0f + mAgeSmPre * (kDriveBase + kDriveEnv * push + kDriveDisp * mDispPush);
                drive = std::min(drive, kDriveMax);
                mDriveDbg = drive;
                // band amplitude envelope -> describing-function fundamental gain G
                const float ab = std::fabs(band);
                mBandEnv += (ab > mBandEnv ? mBandAtk : mBandRel) * (ab - mBandEnv);
                const float G = describingG(mBandEnv, drive);
                // HARMONICS-ONLY delta at 2x: nl(band) - G*band cancels the (phase-
                // shifted) fundamental. Band-limit on the way down.
                float b0, b1;
                mHb.up(band, b0, b1);
                const float invd = 1.0f / drive;
                const float n0 = std::tanh(b0 * drive) * invd - G * b0;
                const float n1 = std::tanh(b1 * drive) * invd - G * b1;
                float db = mHb.down(n0, n1);
                // modal shaping of the exciter (§6): two paper-cone bending-wave peaks
                // + a polite fizz shelf. Applied at base rate after the halfband down().
                db = mModLp.processSample(mModHs.processSample(mModM2.processSample(mModM1.processSample(db))));
                s = s + Wb * db;
            }

            // ---- LF->HF intermodulation (§3): Bl(x) force-factor droop (AM) + Doppler
            // (FM) couple loud lows into the highs — neither is elsewhere in the block.
            // Sidebands-only two-tap: difference a Doppler-modulated Hermite tap against
            // a clean integer tap of the SAME delay line, so at rest (xs=0, gAM=0) the
            // delta is EXACTLY 0 (no static comb) and only the modulation products add.
            // Runs at base rate (xs is band-limited <~500 Hz; products don't fold). ----
            const float Wim = clamp01(kImAge * mAgeSmPre + kImThump * mThumpSmPre);
            mImWet = Wim;
            if (Wim > 1.0e-6f)
            {
                mImRing[(size_t)mImPos] = hp;                         // write shared HP800(s)
                const float clean = mImRing[(size_t)((mImPos - mImTauC) & mImMask)]; // integer tap
                const float mod   = imRead((float)mImTauC + mImKDop * mXs);          // Doppler tap
                // Bl(x) droop: symmetric x^2 term + asymmetric x term. The asym part has
                // a BASE component (guitar speakers ship with a deliberate coil-out Bl
                // offset — 1.6 mm measured on the Greenback) plus an Age-scaled wear term.
                const float gAM = kAMsym * mXs * mXs
                                  + (kAMasymBase + kAMasymAge * mAgeSmPre) * mXs;
                const float dIM = (1.0f - gAM) * mod - clean;        // sidebands-only
                s = s + Wim * dIM;
                mImPos = (mImPos + 1) & mImMask;
            }

            buf[i] = s;
        }
        flushDenormalsPre();
    }

    // ============================ post-conv processing ============================
    // Stage C (enclosure air / damping delta). Post-conv is physically correct:
    // panels are excited by the driver's ACOUSTIC output = the post-conv signal.
    void processPost(float *buf, int numSamples)
    {
        if (!mPrepared) return;
        if (!postActive()) { mPostWasActive = false; return; } // bit-exact bypass

        if (!mPostWasActive) resetPostState(); // inactive->active edge: no stale env/AP state
        mPostWasActive = true;

        for (int i = 0; i < numSamples; ++i)
        {
            const float x = buf[i];

            // post owns its OWN Age/Thump smoother state (see processPre note)
            mSizeSm      += mMixK   * (sizeTgt()  - mSizeSm);
            mThumpSmPost += mMacroK * (thumpTgt() - mThumpSmPost);
            mAgeSmPost   += mMacroK * (ageTgt()   - mAgeSmPost);
            const float r = std::fabs(x);
            mEnvPost += (r > mEnvPost ? mEnvAtkPost : mEnvRelPost) * (r - mEnvPost);
            const float push = pushOf(mEnvPost);

            // wet mix: level-dependent, bounded <= 0.12; default 0
            float encTarget = kEncBase * (0.5f * mThumpSmPost + 0.5f * mThumpSmPost * push
                                          + 0.25f * mAgeSmPost * push);
            encTarget = std::min(encTarget, kEncMax);
            mEncMixSm += mMixK * (encTarget - mEncMixSm);

            // enclosure feedback scaled by Thump (bounded)
            const float gA = std::min(kGmaxHard, kGsetA + kGthump * mThumpSmPost);
            const float gB = std::min(kGmaxHard, kGsetB + kGthump * mThumpSmPost);
            const float gC = std::min(kGmaxHard, kGsetC + kGthump * mThumpSmPost);

            // three parallel diffusion networks (tight A / mid B / deep C), crossfaded
            // by Cab Size in two segments: 0..0.5 fades A->B, 0.5..1 fades B->C. We
            // crossfade OUTPUTS, never delay times -> no pitch warble.
            float a = x, b = x, c = x;
            for (int k = 0; k < 3; ++k) a = mApA[k].process(a, gA);
            for (int k = 0; k < 3; ++k) b = mApB[k].process(b, gB);
            for (int k = 0; k < 3; ++k) c = mApC[k].process(c, gC);
            float net;
            if (mSizeSm < 0.5f) { const float t = mSizeSm * 2.0f;          net = (1.0f - t) * a + t * b; }
            else                { const float t = (mSizeSm - 0.5f) * 2.0f; net = (1.0f - t) * b + t * c; }
            // subsonic high-pass on the WET enclosure output only: the diffusion
            // loops build up sub-30 Hz energy (box resonance sits low), so trim it
            // to keep the delta as "air", not rumble. Dry path stays bit-exact.
            net = mEncHp.processSample(net);

            buf[i] = x + mEncMixSm * net;
        }
        flushDenormalsPost();
    }

    // ============================ verification hooks ============================
    float dbgA1Db()   const { return mA1Db; }   // last-built 90 Hz resonance gain (dB)
    float dbgA2Db()   const { return mA2Db; }   // last-built 2 kHz shelf gain (dB)
    float dbgEncMix() const { return mEncMixSm; } // current enclosure wet mix
    float dbgDrive()  const { return mDriveDbg; } // current Stage B drive multiplier
    float dbgLowCompDb() const { return mLowCompDb; } // current B3 low-shelf gain (dB)
    float dbgEnvPre() const { return mEnvPre; }
    double sampleRate() const { return mFs; }
    // PHYSICS UPGRADE hooks (docs/cabdyn/PHYSICS_UPGRADE.md §9).
    float dbgDispPush()   const { return mDispPush; }   // slow displacement push [0,1]
    float dbgXs()         const { return mXs; }         // signed instantaneous excursion
    float dbgThermDb()    const { return mThermDbg; }   // current thermal droop (dB, <=0)
    float dbgFsEst()      const { return mFsEst; }      // consumed per-cab resonance (Hz)
    double dbgA1CenterHz()const { return mA1CenterDbg; }// last-built A1 center (Hz)
    float dbgImWet()      const { return mImWet; }      // current IM engage Wim

    // Test-only: re-prepare the Stage C allpass delays (base-48k samples, scaled to
    // fs) so an offline probe can sweep delay sets and read the box-resonance peak
    // without recompiling the header. Not used by the plugin.
    void dbgSetEncDelays(const int a48[3], const int b48[3], const int c48[3])
    {
        const double sc = mFs / 48000.0;
        for (int i = 0; i < 3; ++i)
        {
            mApA[i].prepare(std::max(2, (int)std::lround(a48[i] * sc)), kDampA_Hz, mFs);
            mApB[i].prepare(std::max(2, (int)std::lround(b48[i] * sc)), kDampB_Hz, mFs);
            mApC[i].prepare(std::max(2, (int)std::lround(c48[i] * sc)), kDampC_Hz, mFs);
        }
    }

private:
    // ------------------------------- helpers -------------------------------
    static float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
    // effective macro targets: forced to 0 while bypassed so the smoother/gate
    // fade the block out click-free and then leave the path (bit-exact).
    float ageTgt()   const { return mBypassed ? 0.0f : mAgeTarget; }
    float thumpTgt() const { return mBypassed ? 0.0f : mThumpTarget; }
    float sizeTgt()  const { return mBypassed ? 0.0f : mSizeTarget; }
    float onePoleK(double tauSec) const { return (float)(1.0 - std::exp(-1.0 / (tauSec * mFs))); }
    static float dbToLin(float db) { return std::pow(10.0f, db / 20.0f); }

    // 4-point (cubic) Hermite interpolation. Horner form so that at frac=0 it
    // returns y1 BIT-EXACTLY (every frac factor is 0) — this is what makes the IM
    // two-tap delta vanish exactly at rest (docs/cabdyn/PHYSICS_UPGRADE.md §3).
    static inline float hermite4(float frac, float y0, float y1, float y2, float y3)
    {
        return y1 + 0.5f * frac * (y2 - y0
               + frac * (2.0f * y0 - 5.0f * y1 + 4.0f * y2 - y3
               + frac * (3.0f * (y1 - y2) + y3 - y0)));
    }
    // Read the IM ring at a fractional delay d (samples behind the write cursor)
    // with 4-pt Hermite. d in [1, len-2] so the +/-1 guard taps stay in range.
    inline float imRead(float d) const
    {
        const int di = (int)d;               // integer part
        const float fr = d - (float)di;      // fractional part in [0,1)
        const int base = (mImPos - di) & mImMask; // sample at delay di
        const float y1 = mImRing[(size_t)base];
        const float y0 = mImRing[(size_t)((base + 1) & mImMask)];       // one newer (delay di-1)
        const float y2 = mImRing[(size_t)((base - 1) & mImMask)];       // one older (delay di+1)
        const float y3 = mImRing[(size_t)((base - 2) & mImMask)];       // two older (delay di+2)
        return hermite4(fr, y0, y1, y2, y3);
    }

    // env -> push in [0,1] with a soft knee (quiet barely modulates, loud pushes)
    static float pushOf(float env)
    {
        const float p = (env - kEnvLo) / (kEnvHi - kEnvLo);
        return p < 0.0f ? 0.0f : (p > 1.0f ? 1.0f : p);
    }

    // Describing-function fundamental gain of the soft clipper nl(u)=tanh(u*d)/d for a
    // band envelope A: G = g(beta)/beta, beta = d*A, with
    //   g(beta) = (2/pi) INT_0^pi tanh(beta*sin t) sin t dt   (the tanh describing fn).
    // Subtracting G*band from nl(band) removes the (phase-shifted) fundamental so the
    // Stage B delta is HARMONICS ONLY and cannot comb against the un-filtered dry path.
    void buildGtable()
    {
        constexpr double kPi = 3.14159265358979323846;
        const int P = 2048;
        for (int i = 0; i < kGN; ++i)
        {
            const double beta = (double)kGBetaMax * (double)i / (double)(kGN - 1);
            double acc = 0.0;
            for (int p = 0; p < P; ++p)
            {
                const double th = kPi * ((double)p + 0.5) / (double)P;
                acc += std::tanh(beta * std::sin(th)) * std::sin(th);
            }
            mGtab[i] = (float)((2.0 / kPi) * acc * (kPi / (double)P));
        }
    }
    float describingG(float A, float d) const
    {
        const float beta = d * A;
        if (beta < 1.0e-4f) return 1.0f;                          // small signal -> unity
        constexpr double kPi = 3.14159265358979323846;
        if (beta >= kGBetaMax) return (float)(4.0 / kPi) / beta;  // g(inf) = 4/pi
        const float xr = beta / kGBetaMax * (float)(kGN - 1);
        const int i = (int)xr;
        const float f = xr - (float)i;
        const float g = mGtab[i] * (1.0f - f) + mGtab[i + 1] * f;
        return g / beta;
    }

    // Consume a new per-cab resonance (§4): set FsEst/promN, retune the excursion
    // LP2 and the modal centers state-preservingly (copyCoeffsFrom keeps the running
    // z-state so an IR swap doesn't click). A1/B3 key on mFsEst and are rebuilt every
    // control block by rebuildStageA, so setting mFsEst here is enough for them.
    // mScale: a bass cab (low Fs -> bigger cone) has lower breakup modes.
    void retuneResonance(float fsHz, bool force)
    {
        fsHz = std::max(45.0f, std::min(200.0f, fsHz));
        if (!force && fsHz == mFsApplied) return;
        mFsEst = fsHz;
        // Excursion LP2 at the box resonance, Q = Qtc (state-preserving unless forced).
        const Biquad exc = Biquad::lowpass(mFs, std::min((double)fsHz, 0.45 * mFs), (double)kXQ);
        if (force) { mExcLp = exc; mExcLp.reset(); } else mExcLp.copyCoeffsFrom(exc);
        // Modal exciter peaks + fizz shelf (fixed except on FsEst change).
        const float mScale = (fsHz < 65.0f) ? 0.75f : 1.0f;
        const Biquad m1 = Biquad::peaking(mFs, std::min((double)(kModM1Hz * mScale), 0.45 * mFs), (double)kModM1Q, (double)kModM1Db);
        const Biquad m2 = Biquad::peaking(mFs, std::min((double)(kModM2Hz * mScale), 0.45 * mFs), (double)kModM2Q, (double)kModM2Db);
        const Biquad hs = Biquad::highshelf(mFs, std::min((double)kModHsHz, 0.45 * mFs), (double)kModHsDb, 0.7);
        // Cone-mass HF collapse: a real 12" cone's mechanical output dies above
        // ~6 kHz, so the exciter's upper tanh harmonics must too (measured 10-20 kHz
        // spray at -10.8 dBc on the render probe before this LP1 — not physical).
        const Biquad lp = Biquad::lowpass1(mFs, std::min((double)(kModLpHz * mScale), 0.45 * mFs));
        if (force) { mModM1 = m1; mModM2 = m2; mModHs = hs; mModLp = lp;
                     mModM1.reset(); mModM2.reset(); mModHs.reset(); mModLp.reset(); }
        else { mModM1.copyCoeffsFrom(m1); mModM2.copyCoeffsFrom(m2); mModHs.copyCoeffsFrom(hs);
               mModLp.copyCoeffsFrom(lp); }
        mFsApplied = fsHz;
    }

    // Stage A biquads from the current envelope push AND cone-displacement dispPush
    // (docs/cabdyn/PHYSICS_UPGRADE.md §2). All identity at rest. A1 now blooms from
    // DISPLACEMENT, not the broadband envelope, so a loud HF bend no longer fattens
    // the 90 Hz resonance (guarded by T10/T17).
    void rebuildStageA(float push, float dispPush)
    {
        // A1: Fs resonance peak. Gain keys on Thump*dispPush (ceiling +3 dB). At large
        // excursion the suspension stiffens (Fs shifts UP +6% max via kA1FsShift) and
        // gets lossier (Q DROOPS -20% max via kA1QDroop); promN couples the IR's box-
        // bump prominence into the base Q (a resonant capture => a high-Qtc box).
        const float f0 = mFsEst * (1.0f - 0.12f * mThumpSmPre) * (1.0f + kA1FsShift * dispPush);
        const float Q  = (0.9f + 0.9f * mThumpSmPre) * (1.0f - kA1QDroop * dispPush) * (1.0f + kA1PromQ * mPromN);
        float g1 = kDepthA1 * mThumpSmPre * dispPush;
        g1 = std::min(g1, 3.0f);
        const Biquad n1 = Biquad::peaking(mFs, std::min((double)f0, 0.45 * mFs), (double)Q, (double)g1);
        mA1.copyCoeffsFrom(n1);
        mA1Db = g1;
        mA1CenterDbg = std::min((double)f0, 0.45 * mFs);

        // A2: 2 kHz Le inductance shelf — dynamic droop (env) + static Age ease. -3 dB floor.
        float g2 = -(kDepthA2 * push + kHfEase * mAgeSmPre);
        g2 = std::max(g2, -3.0f);
        const Biquad n2 = Biquad::highshelf(mFs, std::min(2000.0, 0.45 * mFs), (double)g2, 0.7);
        mA2.copyCoeffsFrom(n2);
        mA2Db = g2;

        // B3: dynamic low-band cone POWER COMPRESSION as a clean magnitude low-shelf
        // (series filter, NOT a parallel delta -> no phase-comb). Now excursion-driven
        // (dispPush) — excursion compression IS displacement-driven by definition. The
        // shelf corner tracks the cab: clamp(2.2*FsEst,140,260) (FsEst=90 -> ~198 Hz,
        // ~ the old fixed 200). Gain reduction only, floored at -2 dB; identity at rest.
        float grDb = -(kAgeComp * mAgeSmPre + kEnvComp * mAgeSmPre * dispPush);
        grDb = std::max(grDb, -2.0f);
        const float fShelf = std::max(140.0f, std::min(260.0f, 2.2f * mFsEst));
        const Biquad n3 = Biquad::lowshelf(mFs, std::min((double)fShelf, 0.45 * mFs), (double)grDb, 0.7);
        mLowShelf.copyCoeffsFrom(n3);
        mLowCompDb = grDb;
    }

    // inactive->active edge: clear the frozen stage state (envelopes + filter/AP
    // memory) so re-engage starts from the transparent edge. DESIGN.md §0 says the
    // gate covers smoothers/envelope; this makes the code honour that. The macro
    // smoothers are intentionally left alone — they ramp from ~0 and scale every
    // delta, so this reset is inaudible (guarded by T8's click-free re-engage).
    void resetPreState()
    {
        mEnvPre  = 0.0f;
        mBandEnv = 0.0f;
        mA1.reset(); mA2.reset(); mLowShelf.reset();
        mHp800.reset(); mLp3800.reset();
        mHb.reset();
        mCtrl = 0;
        // PHYSICS UPGRADE pre-state: excursion LP2, displacement follower/push, IM
        // ring, thermal power, and modal biquads all return to the transparent edge
        // (re-engage = cold coil, no stale displacement/comb/heat).
        mExcLp.reset();
        mDEnv = 0.0f; mDispPush = 0.0f; mXs = 0.0f;
        mImPos = 0; mImWet = 0.0f;
        std::fill(mImRing.begin(), mImRing.end(), 0.0f);
        mPwr = 0.0f; mThermGain = 1.0f; mThermDbg = 0.0f;
        mModM1.reset(); mModM2.reset(); mModHs.reset(); mModLp.reset();
        rebuildStageA(0.0f, 0.0f); // identity coeffs at the transparent edge
    }
    void resetPostState()
    {
        mEnvPost  = 0.0f;
        mEncMixSm = 0.0f;
        for (int i = 0; i < 3; ++i) { mApA[i].reset(); mApB[i].reset(); mApC[i].reset(); }
        mEncHp.reset();
    }

    bool preActive() const
    {
        return ageTgt() > 0.0f || thumpTgt() > 0.0f
               || std::fabs(mAgeSmPre - ageTgt()) > kSettle
               || std::fabs(mThumpSmPre - thumpTgt()) > kSettle;
    }
    bool postActive() const
    {
        return thumpTgt() > 0.0f || ageTgt() > 0.0f
               || std::fabs(mThumpSmPost - thumpTgt()) > kSettle
               || std::fabs(mAgeSmPost - ageTgt()) > kSettle
               || std::fabs(mSizeSm - sizeTgt()) > kSettle
               || mEncMixSm > kSettle;
    }

    void flushDenormalsPre()
    {
        // includes the excursion LP2 and the three modal exciter biquads (§1/§6).
        for (Biquad *b : {&mA1, &mA2, &mLowShelf, &mHp800, &mLp3800,
                          &mExcLp, &mModM1, &mModM2, &mModHs, &mModLp})
        {
            if (std::fabs(b->z1) < 1.0e-30f) b->z1 = 0.0f;
            if (std::fabs(b->z2) < 1.0e-30f) b->z2 = 0.0f;
        }
        if (std::fabs(mEnvPre) < 1.0e-30f) mEnvPre = 0.0f;
        if (std::fabs(mBandEnv) < 1.0e-30f) mBandEnv = 0.0f; // Stage B1 band follower
        if (std::fabs(mDEnv) < 1.0e-30f) mDEnv = 0.0f;       // displacement follower (§1)
        if (std::fabs(mPwr)  < 1.0e-30f) mPwr  = 0.0f;       // thermal power integrator (§5)
    }
    void flushDenormalsPost()
    {
        for (int i = 0; i < 3; ++i)
        {
            mApA[i].flushDenormals(); // one-pole damping + recursive delay line
            mApB[i].flushDenormals();
            mApC[i].flushDenormals();
        }
        if (std::fabs(mEncHp.z1) < 1.0e-30f) mEncHp.z1 = 0.0f;
        if (std::fabs(mEncHp.z2) < 1.0e-30f) mEncHp.z2 = 0.0f;
        if (std::fabs(mEnvPost) < 1.0e-30f) mEnvPost = 0.0f;
        if (std::fabs(mEncMixSm) < 1.0e-30f) mEncMixSm = 0.0f;
    }

    // ------------------------------- constants -------------------------------
    static constexpr int   kCtrl = 32;      // Stage A coeff recompute period
    static constexpr int   kGN = 257;       // tanh describing-function table size
    static constexpr float kGBetaMax = 12.0f;
    static constexpr float kSettle = 1.0e-6f;
    // push knee — RIG-CALIBRATED (§12, corrected +10.1 dB to INTERNAL level): a
    // cranked amp is a limiter; the internal envelope spans only ~0.16 (genuinely
    // quiet) to ~0.36 (riffing p90). 0.16..0.48 puts normal playing at push
    // ~0.5-0.6, quiet ~0, digging-in headroom to 1.
    static constexpr float kEnvLo = 0.16f, kEnvHi = 0.48f;
    // Stage A depths
    static constexpr float kDepthA1 = 4.0f; // 90 Hz peak scale (clamped +3 dB)
    static constexpr float kDepthA2 = 2.0f; // 2 kHz dynamic droop scale
    static constexpr float kHfEase  = 1.5f; // static Age HF ease (into the -3 dB shelf)
    // Stage B — drive rebalanced toward DYNAMIC terms (§12): the measured on/off
    // delta showed constant -14 dBc grit at 2.5-20 kHz ("fizz on fizz" on a hot
    // compressed high-gain input) while the level-dependent terms never moved. The
    // static floor drops 1.0 -> 0.5 and the env/excursion terms carry more, so
    // breakup now GROWS with playing instead of idling hot. Max 1+(0.5+1.2+1.0)=3.7.
    static constexpr float kDriveBase = 0.5f; // static wear floor (was implicit 1.0) [EAR]
    static constexpr float kDriveEnv  = 1.2f; // midband-envelope contribution to drive
    static constexpr float kAgeComp  = 0.6f; // static low-comp from Age
    static constexpr float kEnvComp  = 1.8f; // dynamic low-comp from Age*env
    // Stage C
    static constexpr float kEncBase  = 0.12f;
    static constexpr float kEncMax   = 0.12f;
    static constexpr float kGsetA = 0.50f, kGsetB = 0.62f, kGsetC = 0.66f;
    static constexpr float kGthump = 0.10f, kGmaxHard = 0.74f;
    static constexpr double kDampA_Hz = 6000.0, kDampB_Hz = 3000.0, kDampC_Hz = 2200.0;
    static constexpr int   kEncSubHpHz = 22;  // subsonic HP on wet enclosure out (low, for bass reach)

    // ---- PHYSICS UPGRADE constants (docs/cabdyn/PHYSICS_UPGRADE.md) ----
    // Excursion model (§1): displacement x(t) = 2nd-order resonant LP of the
    // pre-conv input at the box resonance Fc=FsEst, Q=Qtc. RBJ LP gain at Fc is Q,
    // so kXCal calibrates xs->+/-1 only when the cab is truly slammed AT resonance.
    static constexpr float kFsDefault = 90.0f; // default box resonance (no IR estimate)
    static constexpr float kXQ    = 0.9f;  // generic sealed-ish guitar-cab Qtc
    // RIG-CALIBRATED (2026-07-06, docs/cabdyn/renders analysis, PHYSICS_UPGRADE §12):
    // the renders were captured with the Mix Output Gain at -10.1 dB (post-chain, so
    // it never affects this block internally) -> INTERNAL pre-cab level = render
    // +10.1 dB. Measured chug |LP2@90| p99 = 0.044 on the render == 0.141 internal
    // == the 2.2 mm slam point; kXCal maps that to tanh input ~1.23 -> xs ~ 0.85.
    // (The original 1.25 assumed a FULL-SCALE sine at resonance -- ~15 dB hotter
    // than the real internal level; that's why thump/IM never engaged.)
    static constexpr float kXCal  = 8.75f;
    // dispPush knee on dEnv, from measured per-style percentiles (x22.4 rescale):
    // chugs p50 0.89 / chords p90 0.74 / riffing p50 0.37 p90 0.54 / quiet p99 0.15
    // / leads p90 0.19 -> knee 0.20..0.80: chugs pin ~1, riffing mid, quiet/leads ~0.
    static constexpr float kDispLo = 0.20f, kDispHi = 0.80f;
    // A1 excursion modifiers (§2): stiffening Kms(x) shifts Fs up +15% max and drops
    // effective Q -20% max; promN couples the IR box-bump prominence into base Q.
    static constexpr float kA1FsShift = 0.15f; // DATA-ANCHORED: Cms falls to 75% at XC=2.3mm
                                               // (Klippel, G12H Greenback, Voice Coil 2/2015)
                                               // -> k x1.33 -> Fs x sqrt(1.33) ~ +15%
    static constexpr float kA1QDroop  = 0.20f; // Q down with excursion (Rms lossier)    [EAR]
    static constexpr float kA1PromQ   = 0.15f; // IR-prominence -> higher base Q         [EAR]
    // B1 breakup drive (§2): rebalanced env term + new excursion term, ceiling 3.8x.
    static constexpr float kDriveDisp = 1.0f;  // excursion contribution to breakup drive
    static constexpr float kDriveMax  = 3.8f;  // breakup drive ceiling (was 3.2x)
    // Intermodulation stage (§3): LF->HF coupling the block otherwise lacks. All four
    // constants below are DATA-ANCHORED to the Klippel analysis of the Celestion
    // G12H(55) Greenback (Vance Dickason, Voice Coil Feb 2015 / audioXpress Test
    // Bench): XBl(82% Bl) = 2.0 mm, XC(75% Cms) = 2.3 mm, deliberate coil-out Bl
    // offset 1.6 mm ("increases 2nd-order HD... sounds really good with electric
    // guitar"), Le(x) swing 0.04 mH (negligible -> we rightly don't model Le(x)).
    // |xs| = 1 is calibrated to the 2.2 mm slam point (XBl + 10%).
    static constexpr double kDopUs = 6.4e-6;  // Doppler swing at |xs|=1: 2.2 mm / c(343 m/s)
    static constexpr float kAMsym  = 0.22f;   // Bl droop at slam: 82% at 2.0 mm, parabolic
                                              // -> ~78% at 2.2 mm = 22% droop
    static constexpr float kAMasymBase = 0.10f; // designed-in coil-out offset (1.6 mm meas.)
                                                // -> asym AM exists even on a FRESH cone
    static constexpr float kAMasymAge  = 0.10f; // wear/fatigue adds asymmetry on top    [EAR]
    static constexpr float kImAge   = 0.6f;   // IM engage from Age
    static constexpr float kImThump = 0.5f;   // IM engage from Thump
    // Thermal voice-coil compression (§5): Re rises with dissipated power (tau~sec),
    // sensitivity droops a couple dB; compression only, never restored. Scaled by Age.
    static constexpr float kThermTau = 3.5f;  // voice-coil thermal time constant (s)
    // RIG-CALIBRATED (§12, corrected to INTERNAL level): "full power" = sustained
    // hard playing on THIS rig, not a full-scale sine (which left thermal at <1% on
    // real material). Internal riffing mean x^2 = 0.040 -> 0.041: sustained riffing
    // reaches full droop, quiet playing sits at ~16%. Hot test tones clamp to 1.
    static constexpr float kThermFull = 0.041f;
    static constexpr float kThermInMax = 8.0f * kThermFull; // integrator input clamp
                                              // (+9 dB over cal): bounds recovery lag
    static constexpr float kThermDb  = 1.5f;  // max broadband droop at full power       [EAR]
    // Modal shaping of the harmonics-only breakup exciter (§6): real cone breakup
    // hits discrete bending-wave modes (~1-4 kHz), so the 'cry' has structure. Safe
    // because delta has no fundamental -> series-filtering it cannot re-comb.
    static constexpr float kModM1Hz = 2050.0f, kModM1Q = 2.8f, kModM1Db = 3.5f; // [EAR]
    static constexpr float kModM2Hz = 3350.0f, kModM2Q = 3.5f, kModM2Db = 2.5f; // [EAR]
    static constexpr float kModHsHz = 5000.0f, kModHsDb = -2.5f; // keep fizz polite; deepened
                                              // -1.5 -> -2.5 after the render delta measured
                                              // -14 dBc grit extending to 20 kHz (§12) [EAR]
    static constexpr float kModLpHz = 6500.0f; // cone-mass HF collapse on the exciter (LP1):
                                               // real cone output dies above ~6 kHz, so the
                                               // upper tanh harmonics must too (scaled by
                                               // mScale for bass cones)

    // -------------------------------- state --------------------------------
    double mFs = 48000.0;
    bool mPrepared = false;

    // macro targets + smoothers. Pre and post keep SEPARATE Age/Thump smoother
    // state: the shared macros are stepped once in each stage's per-sample loop, so
    // the de-zip is a true ~25 ms no matter which stages are active (previously both
    // stages stepped the same members -> ~12.5 ms, and rate-dependent on the mix).
    float mAgeTarget = 0.0f, mThumpTarget = 0.0f, mSizeTarget = 0.0f;
    bool  mBypassed = false;   // whole-block bypass (macros retained; see setBypassed)
    float mAgeSmPre = 0.0f, mThumpSmPre = 0.0f;
    float mAgeSmPost = 0.0f, mThumpSmPost = 0.0f;
    float mSizeSm = 0.0f;
    // inactive->active edge trackers (see resetPreState/resetPostState)
    bool mPreWasActive = false, mPostWasActive = false;

    // smoothing coefficients
    float mMacroK = 0.02f, mMixK = 0.02f;
    float mEnvAtk = 0.02f, mEnvRel = 0.005f, mEnvAtkPost = 0.02f, mEnvRelPost = 0.005f;

    // envelopes
    float mEnvPre = 0.0f, mEnvPost = 0.0f;

    // Stage A + B3 low-shelf
    Biquad mA1, mA2, mLowShelf;
    int mCtrl = 0;
    float mA1Db = 0.0f, mA2Db = 0.0f, mLowCompDb = 0.0f, mDriveDbg = 1.0f;
    double mA1CenterDbg = 90.0; // last-built A1 center (Hz), dbg for T17

    // Stage B
    Biquad mHp800, mLp3800;
    Halfband mHb;
    float mBandEnv = 0.0f, mBandAtk = 0.02f, mBandRel = 0.005f;
    float mGtab[kGN] = {0}; // tanh describing-function g(beta) lookup

    // Stage C
    DampAllpass mApA[3], mApB[3], mApC[3];
    Biquad mEncHp;          // subsonic high-pass on the wet enclosure output
    float mEncMixSm = 0.0f;

    // ---- PHYSICS UPGRADE state (docs/cabdyn/PHYSICS_UPGRADE.md) ----
    // Per-cab resonance targets (§4). Written by setSpeakerResonance/clear from the
    // MESSAGE thread (rare, on IR load); consumed at the control-rate boundary in
    // processPre. std::atomic float, not Biquad, so the audio thread only reads a
    // plain value and retunes state-preservingly when it changes.
    std::atomic<float> mFsTarget{kFsDefault};   // captured box resonance (Hz)
    std::atomic<float> mPromTarget{0.0f};       // IR box-bump prominence (dB)
    float mFsEst = kFsDefault;                  // current consumed Fs (control-rate)
    float mPromN = 0.0f;                        // clamp01(promDb/8): base-Q coupling
    float mFsApplied = -1.0f;                   // last Fs baked into the retuned filters

    // Excursion model (§1): RBJ LP2 at FsEst,Q=kXQ on the pre-conv input -> xd/xs;
    // slow displacement envelope dEnv (3ms/100ms) -> dispPush (coefficient driver).
    Biquad mExcLp;                              // 2nd-order resonant displacement LP
    float mDEnv = 0.0f;                         // displacement follower (|xd|)
    float mDEnvAtk = 0.02f, mDEnvRel = 0.005f;  // 3 ms / 100 ms
    float mDispPush = 0.0f;                     // clamp01((dEnv-lo)/(hi-lo)) slow
    float mXs = 0.0f;                           // tanh(xd): signed instantaneous excursion

    // Intermodulation stage (§3): shared HP800 already lives in mHp800; a small
    // pow2 ring holds hp for the two-tap sidebands-only trick (clean integer tap +
    // Doppler-modulated Hermite tap). Allocated in prepare().
    std::vector<float> mImRing;                 // pow2 ring of HP800(s)
    int mImMask = 0, mImPos = 0;                // ring index mask + write cursor
    int mImTauC = 0;                            // integer center tap (>= ceil(kDop)+2)
    float mImKDop = 0.0f;                       // Doppler swing in samples (fs*kDopUs)
    float mImWet = 0.0f;                        // last Wim (dbg)

    // Thermal voice-coil compression (§5): one-pole power integrator on input x^2.
    float mPwr = 0.0f;                          // dissipated-power estimate (x^2, slow)
    float mThermK = 0.0f;                       // one-pole coef for tau=kThermTau
    float mThermGain = 1.0f;                    // dbToLin(droopDb), series broadband
    float mThermDbg = 0.0f;                     // last droopDb (dbg)

    // Modal shaping of the breakup exciter (§6): series peaks + shelf + cone-mass
    // LP1 on the delta only.
    Biquad mModM1, mModM2, mModHs, mModLp;      // fixed except on FsEst change
};

} // namespace nam_rig
