// mod_test — offline verification harness for the modulation section
// (measurement-first). Exits nonzero on any FAIL.
//
// ModVoice = one voiced effect; ModBlock = 3-slot series section.
//   T1  tremolo depth tracks the depth knob (now full-range)
//   T2  tremolo rate via envelope period
//   T3  chorus stays inside its designed sweep window
//   T4  phaser notches move (hardwired sweep)
//   T5  mix 0 is dry
//   T6  stereo spread ~90 deg (tremolo, Width 1)
//   T7  mono-aliased buffer == stereo left (bit-exact)
//   T8  tempo sync resolves rate from BPM + division
//   T9  section = voices in series (1 slot == single voice; 2nd slot stacks)
//   T10 new types finite + non-silent
//   T11 harmonic tremolo bands anti-correlated
//   T12 Width controls the stereo spread
//   T13 per-effect voicing constants (depthMax / bakedBbd / authenticWave)
//   T14 tremolo at full depth chops to near-silence (full-depth chop)
//   T15 only Tremolo honours the Shape waveform; others hardwire their LFO
//   T16 flanger stays bounded at max feedback (incl. Invert on)
//   T17 ZDF/TPT phaser: bounded at high feedback + resonance grows with feedback
//   T18 flanger Manual shifts the static comb position
//   T19 flanger Invert flips the comb polarity (notch <-> peak)
//   T20 free Rate capped per effect (flanger 20 Hz, chorus 3.5 Hz, others 10 Hz; sync uncapped)
//   T21 Bi-Phase finite + non-silent (parallel & series)
//   T22 Bi-Phase parallel stereo A/B split scales with Width
//   T23 Bi-Phase resonance grows with feedback (impulse ring)
//   T24 Bi-Phase second sweep generator (P2Ratio) changes the motion
//   T25 photocell warp lopsided (slow cool > fast heat) + more so when faster
//   T26 Uni-Vibe ZDF: bounded across depth/rate extremes; sweep moves the spectrum
//       (incl. at depth 0, where the depth floor keeps it breathing)
//   T27 Leslie 2D angle-EQ model: horn-tilt gain law + highs swing with rotor
//       angle more than mids (CCRMA/Smith-Lee differential-EQ behaviour)
//   T28 Leslie tube amp: saturation curve bounded + compresses; Drive adds
//       harmonics; finite/bounded across the drive range
//   T29 Rotary Horn/Drum balance: neutral at center, solos a rotor at each
//       extreme (drum-band tone ducks toward horn, horn-band toward drum)
//   T30 section Level Lock: corrects a static level offset to unity; locks the
//       long-term level of an AM (tremolo) signal while preserving its pulse;
//       silence-safe (no noise-floor boost / runaway); disabled = bit-exact;
//       integration: a 50/50 flanger's section level is pulled back to input
//   T31 section routing: Cartesian pad weights (pure nodes / equal centroid /
//       sum=1 / clamped outside); parallel single-slot == standalone voice; the
//       pad blends between nodes; a bypassed node drops out; pad + Level Lock
//       hold steady loudness as the puck moves
//   T32 momentary solo: isolates a slot in series + parallel (== standalone),
//       overrides bypass, and clears back to the full mix
//   T33 POST block: runs at the END of the section (== standalone when alone,
//       == hand-chained after a front slot); its bypass works
//   T34 series chain order: default {0,1,2} == fixed-order series (bit-exact);
//       reordering two effects changes the sound; a malformed order is rejected
//   T35 tremolo Width per shape: Square/S&H mono at 0, amplitude auto-pan at 1;
//       sine spreads via quadrature
//   T36 chorus + vibrato use the 6-point 5th-order Lagrange read: it reproduces
//       a cubic ~exactly (the 4-point cubic-Hermite read can't)
//   T37 harm-trem LR4 recombines magnitude-flat at unity AM (depth 0); bounded
//   T38 harm-trem LR4 band separation is steep (24 dB/oct): a tone 2 octaves
//       above the crossover modulates near-fully (a one-pole split couldn't)
//   T39 vibrato 6-point-Lagrange interp: flat magnitude at a steady delay; a
//       fast/deep sweep stays low-ripple (FIR -> no integer-crossing glitch,
//       unlike the recursive all-pass which rang at ~0.224 here)
//   T40 tremolo DC-offset comp law: center 0 = cut-only (mean drops with depth),
//       center 1 = mean-preserving (avg gain 1 at any depth); both full-chop;
//       and the multiplier adds no DC to a zero-mean signal
//   T41 rate-dependent sweep-width guardrail (phaser + uni-vibe + bi-phase):
//       full width <=2 Hz, narrows monotonically to the floor at the rate cap;
//       phaser stays bounded driven fast
//   T42 Extreme switch reassigns controls to their wild range (Normal untouched):
//       phaser/bi-phase ring harder at same fb knob; bi-phase ratio -> strong detune
//   T43 chorus Extreme: rate knob remaps into the disjoint fast band [3.5,10] Hz
//       (always fast, never overlaps Normal) + sweep excursion widens (deeper tap);
//       stays finite/bounded
//   T44 vibrato Extreme: rate knob remaps into the disjoint fast band [10,16] Hz
//       + pitch sweep deepens (single tap lands later at a matched 10 Hz rate);
//       stays finite/bounded
//   T45 flanger Extreme = true through-zero: tap-delay law (swept tap crosses the
//       reference at lfo=0 and inverts sign, both taps stay in [guard,kFlMaxMs]);
//       Invert=cancel mode produces a deep broadband null the Normal flanger can't;
//       stays finite/bounded with feedback
#include "rig/ModBlock.h"
#include <cstdio>
#include <cmath>
#include <cstdint>
#include <vector>
#include <complex>

using nam_rig::BlockContext;
using nam_rig::Lfo;
using nam_rig::ModBlock;
using nam_rig::ModVoice;

static int gFails = 0;
#define CHECK(cond, ...) do { \
    const bool chkOk_ = (cond); \
    std::printf("%s: ", chkOk_ ? "PASS" : "FAIL"); std::printf(__VA_ARGS__); std::printf("\n"); \
    if (!chkOk_) ++gFails; } while (0)

static constexpr double SR = 48000.0;
static constexpr int BLK = 512;

template <class M>
static void run(M &m, std::vector<float> &l, std::vector<float> &r)
{
    for (size_t p = 0; p < l.size(); p += BLK)
    {
        const int n = (int)std::min<size_t>(BLK, l.size() - p);
        m.process(l.data() + p, r.data() + p, n);
    }
}

static std::vector<float> tone(double freq, double amp, int n)
{
    std::vector<float> v((size_t)n);
    for (int i = 0; i < n; ++i)
        v[(size_t)i] = (float)(amp * std::sin(2.0 * M_PI * freq * i / SR));
    return v;
}

static std::vector<double> envelope(const std::vector<float> &x, int w)
{
    std::vector<double> e;
    for (size_t p = 0; p + (size_t)w <= x.size(); p += (size_t)w)
    {
        double pk = 0;
        for (int i = 0; i < w; ++i)
            pk = std::max(pk, (double)std::abs(x[p + (size_t)i]));
        e.push_back(pk);
    }
    return e;
}

static double mag(const std::vector<float> &x, size_t from, size_t len, double freq)
{
    std::complex<double> acc{0, 0};
    for (size_t i = 0; i < len; ++i)
        acc += (double)x[from + i]
               * std::exp(std::complex<double>(0, -2.0 * M_PI * freq * (double)i / SR));
    return std::abs(acc) * 2.0 / (double)len;
}

// run a single voice over a 1 s 1 kHz tone, return the left channel
static std::vector<float> runWave(int type, int wave, float depth)
{
    ModVoice m;
    m.setType(type);
    m.setRateHz(2.0f);
    m.setDepth(depth);
    m.setMix(1.0f);
    m.setWaveform(wave);
    m.prepare({SR, BLK});
    auto l = tone(1000.0, 0.5, (int)SR), r = l;
    run(m, l, r);
    return l;
}

int main()
{
    // ---- T1: tremolo depth, capped to its voiced maximum ----
    {
        ModVoice m;
        m.setType(ModVoice::kTremolo);
        m.setRateHz(4.0f);
        m.setDepth(0.6f);
        m.setMix(1.0f);
        m.prepare({SR, BLK});
        auto l = tone(1000.0, 0.5, (int)SR * 3), r = l;
        run(m, l, r);
        std::vector<float> tail(l.begin() + (int)SR, l.end());
        auto env = envelope(tail, 48);
        double mn = 1e9, mx = 0;
        for (auto e : env) { mn = std::min(mn, e); mx = std::max(mx, e); }
        const double meas = 1.0 - mn / mx;
        // Trough/peak envelope ratio from the actual gain law: with DC-offset
        // comp (kTremCenter) the peak gain boosts above 1, so 1-min/max is no
        // longer simply the depth knob. Derive want from tremGain at the extremes
        // (LFO modulator = +1 trough, -1 peak); independent of kTremTaper (=+-1 ends).
        const float d = 0.6f * ModVoice::depthMax(ModVoice::kTremolo);
        const double minG = ModVoice::tremGain(d, 1.0f, ModVoice::kTremCenter);
        const double maxG = ModVoice::tremGain(d, -1.0f, ModVoice::kTremCenter);
        const double want = 1.0 - minG / maxG;
        CHECK(std::abs(meas - want) < 0.05, "T1 tremolo depth %.3f (voiced want %.3f)", meas, want);
    }

    // ---- T2: tremolo rate ----
    {
        ModVoice m;
        m.setType(ModVoice::kTremolo);
        m.setRateHz(4.0f);
        m.setDepth(0.6f);
        m.setMix(1.0f);
        m.prepare({SR, BLK});
        auto l = tone(1000.0, 0.5, (int)SR * 3), r = l;
        run(m, l, r);
        std::vector<float> tail(l.begin() + (int)SR, l.end());
        auto env = envelope(tail, 48);
        double mn = 1e9, mx = 0;
        for (auto e : env) { mn = std::min(mn, e); mx = std::max(mx, e); }
        int minima = 0;
        for (size_t i = 1; i + 1 < env.size(); ++i)
            if (env[i] < env[i - 1] && env[i] <= env[i + 1] && env[i] < mn + 0.1 * (mx - mn))
                ++minima;
        const double rate = minima / 2.0;
        CHECK(std::abs(rate - 4.0) <= 0.5, "T2 tremolo rate %.1f Hz (set 4.0)", rate);
    }

    // ---- T3: chorus stays inside its sweep window ----
    {
        ModVoice m;
        m.setType(ModVoice::kChorus);
        m.setRateHz(0.5f);
        m.setDepth(1.0f);
        m.setMix(1.0f);
        m.prepare({SR, BLK});
        std::vector<float> l((size_t)SR * 2, 0.0f);
        for (size_t i = 0; i < l.size(); i += 4800)
            l[i] = 1.0f;
        auto r = l;
        run(m, l, r);
        double dMin = 1e9, dMax = 0;
        for (size_t i = 48000; i + 4800 < l.size(); i += 4800)
        {
            double pk = 0;
            size_t pkAt = i;
            for (size_t j = i; j < i + 4700; ++j)
                if (std::abs(l[j]) > pk) { pk = std::abs(l[j]); pkAt = j; }
            const double dMs = (double)(pkAt - i) * 1000.0 / SR;
            dMin = std::min(dMin, dMs);
            dMax = std::max(dMax, dMs);
        }
        CHECK(dMin > 6.0 && dMax < 18.0, "T3 chorus delay window [%.1f, %.1f] ms (design [7,17])", dMin, dMax);
        CHECK(dMax - dMin > 3.0, "T3 chorus sweeps (range %.1f ms > 3)", dMax - dMin);
    }

    // ---- T4: phaser notches move ----
    {
        ModVoice m;
        m.setType(ModVoice::kPhaser);
        m.setRateHz(0.2f);
        m.setMix(0.5f);
        m.prepare({SR, BLK});
        std::vector<float> l((size_t)SR * 5, 0.0f);
        for (int k = 0; k < 40; ++k)
        {
            const double f = 100.0 * std::pow(80.0, k / 39.0);
            for (size_t i = 0; i < l.size(); ++i)
                l[i] += (float)(0.02 * std::sin(2.0 * M_PI * f * (double)i / SR + k));
        }
        auto r = l;
        run(m, l, r);
        double diff = 0, ref = 0;
        for (int k = 0; k < 40; ++k)
        {
            const double f = 100.0 * std::pow(80.0, k / 39.0);
            const double m1 = mag(l, (size_t)SR, (size_t)SR / 2, f);
            const double m2 = mag(l, (size_t)(SR * 3.5), (size_t)SR / 2, f);
            diff += std::abs(m1 - m2);
            ref += std::max(m1, m2);
        }
        CHECK(diff / ref > 0.10, "T4 phaser spectrum moves (rel diff %.2f > 0.10)", diff / ref);
    }

    // ---- T5: mix 0 ~= dry ----
    {
        ModVoice m;
        m.setType(ModVoice::kChorus);
        m.setMix(0.0f);
        m.prepare({SR, BLK});
        auto l = tone(440.0, 0.25, (int)SR), r = l;
        auto ref = l;
        run(m, l, r);
        double err = 0;
        for (size_t i = 4800; i < l.size(); ++i)
            err = std::max(err, (double)std::abs(l[i] - ref[i]));
        CHECK(err < 1e-6, "T5 mix=0 dry error %.2e < 1e-6", err);
    }

    // ---- T6: stereo spread ~90 deg (tremolo, Width 1) ----
    {
        ModVoice m;
        m.setType(ModVoice::kTremolo);
        m.setRateHz(2.0f);
        m.setDepth(0.8f);
        m.setMix(1.0f);
        m.prepare({SR, BLK});
        auto l = tone(1000.0, 0.5, (int)SR * 3), r = l;
        run(m, l, r);
        std::vector<float> lt(l.begin() + (int)SR, l.end()), rt(r.begin() + (int)SR, r.end());
        auto el = envelope(lt, 48), er = envelope(rt, 48);
        double mL = 0, mR = 0;
        for (auto e : el) mL += e;
        for (auto e : er) mR += e;
        mL /= (double)el.size();
        mR /= (double)er.size();
        const double period = 1000.0 / 2.0;
        int bestLag = 0;
        double bestC = -1e18;
        for (int lag = 0; lag < (int)period; ++lag)
        {
            double c = 0;
            int n = 0;
            for (size_t i = 0; i + (size_t)lag < er.size() && i < el.size(); ++i, ++n)
                c += (el[i] - mL) * (er[i + (size_t)lag] - mR);
            if (n > 0) c /= (double)n;
            if (c > bestC) { bestC = c; bestLag = lag; }
        }
        const double frac = bestLag / period;
        const double d90 = std::min(std::abs(frac - 0.25), std::abs(frac - 0.75));
        CHECK(d90 < 0.06, "T6 L/R envelope offset %.2f cycles (want 0.25/0.75)", frac);
    }

    // ---- T7: mono aliasing (left == right) equals stereo left ----
    {
        ModVoice m1, m2;
        for (auto *m : {&m1, &m2})
        {
            m->setType(ModVoice::kChorus);
            m->setRateHz(1.0f);
            m->setDepth(0.7f);
            m->setMix(0.5f);
            m->prepare({SR, BLK});
        }
        auto x = tone(330.0, 0.3, (int)SR);
        auto aliased = x;
        for (size_t p = 0; p < aliased.size(); p += BLK)
            m1.process(aliased.data() + p, aliased.data() + p,
                       (int)std::min<size_t>(BLK, aliased.size() - p));
        auto lref = x, rref = x;
        run(m2, lref, rref);
        bool same = true;
        for (size_t i = 0; i < x.size(); ++i)
            if (aliased[i] != lref[i]) { same = false; break; }
        CHECK(same, "T7 mono-aliased buffer == stereo left (bit-exact)");
    }

    // ---- T8: tempo sync ----
    {
        ModVoice m;
        m.setBpm(120.0);
        m.setRateHz(0.1f);
        m.setSyncIndex(3); // 1/4 = 1 beat -> 2 Hz
        CHECK(std::abs(m.effectiveRateHz() - 2.0f) < 0.01f, "T8 sync 1/4 @120 -> %.3f Hz", m.effectiveRateHz());
        m.setSyncIndex(6); // 1/8 -> 4 Hz
        CHECK(std::abs(m.effectiveRateHz() - 4.0f) < 0.01f, "T8 sync 1/8 @120 -> %.3f Hz", m.effectiveRateHz());
        m.setSyncIndex(0);
        CHECK(std::abs(m.effectiveRateHz() - 0.1f) < 1e-4f, "T8 sync Off -> free %.3f", m.effectiveRateHz());
    }

    // ---- T9: section = voices in series ----
    {
        ModBlock sec;
        sec.setLevelLock(false); // bit-exact series check: no section makeup gain
        sec.setType(0, ModVoice::kTremolo);
        sec.setRateHz(0, 4.0f);
        sec.setDepth(0, 0.6f);
        sec.setMix(0, 1.0f);
        sec.setSlotBypassed(0, false);
        for (int s = 1; s < ModBlock::kSlots; ++s)
            sec.setSlotBypassed(s, true);
        sec.prepare({SR, BLK});

        ModVoice one;
        one.setType(ModVoice::kTremolo);
        one.setRateHz(4.0f);
        one.setDepth(0.6f);
        one.setMix(1.0f);
        one.prepare({SR, BLK});

        auto x = tone(1000.0, 0.5, (int)SR);
        auto la = x, ra = x, lb = x, rb = x;
        run(sec, la, ra);
        run(one, lb, rb);
        bool same = true;
        for (size_t i = 0; i < x.size(); ++i)
            if (la[i] != lb[i]) { same = false; break; }
        CHECK(same, "T9 section 1-slot == single ModVoice (bit-exact)");

        auto makeChorus = [](ModBlock &m) {
            m.setType(0, ModVoice::kChorus);
            m.setRateHz(0, 1.0f);
            m.setDepth(0, 0.7f);
            m.setMix(0, 1.0f);
            m.setSlotBypassed(0, false);
            m.setSlotBypassed(1, true);
            m.setSlotBypassed(2, true);
        };
        ModBlock chorusOnly, stacked;
        chorusOnly.setLevelLock(false); // compare raw series sums, no makeup gain
        stacked.setLevelLock(false);
        makeChorus(chorusOnly);
        makeChorus(stacked);
        stacked.setType(1, ModVoice::kTremolo);
        stacked.setRateHz(1, 5.0f);
        stacked.setDepth(1, 0.8f);
        stacked.setMix(1, 1.0f);
        stacked.setSlotBypassed(1, false);
        chorusOnly.prepare({SR, BLK});
        stacked.prepare({SR, BLK});
        auto l1 = x, r1 = x, l2 = x, r2 = x;
        run(chorusOnly, l1, r1);
        run(stacked, l2, r2);
        double d = 0;
        for (size_t i = 0; i < x.size(); ++i)
            d = std::max(d, (double)std::abs(l2[i] - l1[i]));
        CHECK(d > 0.01, "T9 second slot stacks in series (max diff %.3f)", d);
    }

    // ---- T10: new types finite + non-silent ----
    {
        bool ok = true;
        for (int t : {ModVoice::kVibrato, ModVoice::kRotary, ModVoice::kUniVibe, ModVoice::kHarmTrem})
        {
            ModVoice m;
            m.setType(t);
            m.setRateHz(2.0f);
            m.setDepth(0.7f);
            m.setMix(1.0f);
            m.prepare({SR, BLK});
            auto l = tone(440.0, 0.3, (int)SR), r = l;
            run(m, l, r);
            double e = 0;
            bool fin = true;
            for (float v : l) { if (!std::isfinite(v)) fin = false; e += (double)v * v; }
            if (!fin || e < 1e-3) ok = false;
        }
        CHECK(ok, "T10 new types (vibrato/rotary/uni-vibe/harm-trem) finite + non-silent");
    }

    // ---- T11: harmonic tremolo bands anti-correlated ----
    {
        ModVoice m;
        m.setType(ModVoice::kHarmTrem);
        m.setRateHz(3.0f);
        m.setDepth(0.9f);
        m.setMix(1.0f);
        m.prepare({SR, BLK});
        std::vector<float> l((size_t)SR * 2);
        for (size_t i = 0; i < l.size(); ++i)
            l[i] = (float)(0.3 * std::sin(2.0 * M_PI * 200.0 * (double)i / SR) +
                           0.3 * std::sin(2.0 * M_PI * 3000.0 * (double)i / SR));
        auto r = l;
        run(m, l, r);
        std::vector<double> eLo, eHi;
        for (size_t p = (size_t)SR; p + 480 <= l.size(); p += 480)
        {
            eLo.push_back(mag(l, p, 480, 200.0));
            eHi.push_back(mag(l, p, 480, 3000.0));
        }
        double mL = 0, mH = 0;
        for (size_t i = 0; i < eLo.size(); ++i) { mL += eLo[i]; mH += eHi[i]; }
        mL /= (double)eLo.size();
        mH /= (double)eHi.size();
        double cov = 0, vL = 0, vH = 0;
        for (size_t i = 0; i < eLo.size(); ++i)
        {
            const double a = eLo[i] - mL, b = eHi[i] - mH;
            cov += a * b; vL += a * a; vH += b * b;
        }
        const double corr = cov / (std::sqrt(vL * vH) + 1e-12);
        CHECK(corr < -0.3, "T11 harmonic trem bands anti-correlated (corr %.2f)", corr);
    }

    // ---- T12: Width controls the stereo spread ----
    {
        auto spread = [](float width) {
            ModVoice m;
            m.setType(ModVoice::kTremolo);
            m.setRateHz(2.0f);
            m.setDepth(0.8f);
            m.setMix(1.0f);
            m.setWidth(width);
            m.prepare({SR, BLK});
            auto l = tone(1000.0, 0.5, (int)SR), r = l;
            run(m, l, r);
            double d = 0;
            for (size_t i = 4800; i < l.size(); ++i)
                d = std::max(d, (double)std::abs(l[i] - r[i]));
            return d;
        };
        CHECK(spread(0.0f) < 1e-6, "T12 width 0 -> L==R (max diff %.2e)", spread(0.0f));
        CHECK(spread(1.0f) > 0.05, "T12 width 1 -> spread (max diff %.3f)", spread(1.0f));
    }

    // ---- T13: per-effect voicing constants ----
    {
        const bool ok =
            ModVoice::depthMax(ModVoice::kTremolo) == 1.0f &&   // tremolo chops to full silence
            ModVoice::depthMax(ModVoice::kHarmTrem) == 1.0f &&   // harm-trem now full-chop (LR4 recombines flat)
            ModVoice::depthMax(ModVoice::kChorus) == 1.0f &&
            ModVoice::bakedBbd(ModVoice::kChorus) > 0.0f &&
            ModVoice::bakedBbd(ModVoice::kPhaser) == 0.0f &&
            ModVoice::authenticWave(ModVoice::kFlanger) == Lfo::Triangle &&
            ModVoice::authenticWave(ModVoice::kChorus) == Lfo::Sine &&
            ModVoice::mixFor(ModVoice::kFlanger, 1.0f) == 0.5f &&  // flanger Mix caps at 50/50
            ModVoice::mixFor(ModVoice::kFlanger, 0.0f) == 0.0f;    // ...and reaches fully dry
        CHECK(ok, "T13 voicing constants (tremolo + harm-trem chop full, BBD baked on delay types, flanger=triangle, flanger mix caps 50/50)");
    }

    // ---- T14: tremolo at full depth chops to near-silence (full-depth chop) ----
    {
        ModVoice m;
        m.setType(ModVoice::kTremolo);
        m.setRateHz(4.0f);
        m.setDepth(1.0f);
        m.setMix(1.0f);
        m.prepare({SR, BLK});
        auto l = tone(1000.0, 0.5, (int)SR * 2), r = l;
        run(m, l, r);
        std::vector<float> tail(l.begin() + (int)SR, l.end());
        auto env = envelope(tail, 48);
        double mn = 1e9, mx = 0;
        for (auto e : env) { mn = std::min(mn, e); mx = std::max(mx, e); }
        CHECK(mn / mx < 0.10, "T14 full depth chops deep (min/max %.2f < 0.10 = near-silence trough)", mn / mx);
    }

    // ---- T15: only Tremolo honours the Shape waveform ----
    {
        auto cs = runWave(ModVoice::kChorus, Lfo::Sine, 0.7f);
        auto cq = runWave(ModVoice::kChorus, Lfo::Square, 0.7f);
        bool chorusSame = true;
        for (size_t i = 0; i < cs.size(); ++i)
            if (cs[i] != cq[i]) { chorusSame = false; break; }
        auto ts = runWave(ModVoice::kTremolo, Lfo::Sine, 0.7f);
        auto tq = runWave(ModVoice::kTremolo, Lfo::Square, 0.7f);
        double td = 0;
        for (size_t i = 0; i < ts.size(); ++i)
            td = std::max(td, (double)std::abs(ts[i] - tq[i]));
        CHECK(chorusSame && td > 0.01,
              "T15 chorus hardwires LFO (sq==sine), tremolo honours Shape (diff %.3f)", td);
    }

    // ---- T16: flanger stays bounded at max feedback (tone-shaped regen) ----
    {
        ModVoice m;
        m.setType(ModVoice::kFlanger);
        m.setRateHz(0.3f);
        m.setDepth(0.8f);
        m.setFeedback(0.95f); // new Regen ceiling
        m.setMix(0.5f);
        m.prepare({SR, BLK});
        auto l = tone(220.0, 0.4, (int)SR * 2), r = l;
        run(m, l, r);
        bool fin = true;
        double pk = 0;
        for (float v : l) { if (!std::isfinite(v)) fin = false; pk = std::max(pk, (double)std::abs(v)); }
        CHECK(fin && pk < 4.0, "T16 flanger max feedback stays bounded (peak %.2f)", pk);

        // same, with Invert on (negative regen) -> must also stay bounded
        ModVoice mi;
        mi.setType(ModVoice::kFlanger);
        mi.setRateHz(0.3f);
        mi.setDepth(0.8f);
        mi.setFeedback(0.95f);
        mi.setInvert(true);
        mi.setMix(0.5f);
        mi.prepare({SR, BLK});
        auto li = tone(220.0, 0.4, (int)SR * 2), ri = li;
        run(mi, li, ri);
        bool fin2 = true;
        double pk2 = 0;
        for (float v : li) { if (!std::isfinite(v)) fin2 = false; pk2 = std::max(pk2, (double)std::abs(v)); }
        CHECK(fin2 && pk2 < 4.0, "T16 flanger invert + max feedback stays bounded (peak %.2f)", pk2);
    }

    // ---- T17: ZDF/TPT phaser — high feedback bounded; resonance grows with feedback ----
    {
        // Run the phaser over a 40-tone log-spaced bed (same construction as T4),
        // with the sweep almost frozen (slow rate) so the spectrum is ~stationary.
        auto runPhaser = [](float fb) {
            ModVoice m;
            m.setType(ModVoice::kPhaser);
            m.setRateHz(0.05f); // near-frozen sweep -> stable notch positions to measure
            m.setFeedback(fb);
            m.setMix(0.5f);
            m.prepare({SR, BLK});
            std::vector<float> l((size_t)SR * 2, 0.0f);
            for (int kk = 0; kk < 40; ++kk)
            {
                const double f = 100.0 * std::pow(80.0, kk / 39.0);
                for (size_t i = 0; i < l.size(); ++i)
                    l[i] += (float)(0.02 * std::sin(2.0 * M_PI * f * (double)i / SR + kk));
            }
            auto r = l;
            run(m, l, r);
            return l;
        };

        // (a) unconditional stability: full-up feedback stays finite + bounded
        auto hi = runPhaser(0.99f);
        bool fin = true;
        double pk = 0;
        for (float v : hi) { if (!std::isfinite(v)) fin = false; pk = std::max(pk, (double)std::abs(v)); }
        CHECK(fin && pk < 4.0, "T17 phaser high feedback stays bounded (peak %.2f)", pk);

        // (b) resonance: feedback pushes the loop poles toward the unit circle, so
        // the network rings. Freeze the sweep (rate 0 -> stationary fc) and feed an
        // impulse; the decay-tail energy must grow monotonically with feedback.
        // (A spectral-probe metric is unreliable here: the resonant peaks are
        // narrow and fall between coarse probe tones.)
        auto tailEnergy = [](float fb) {
            ModVoice m;
            m.setType(ModVoice::kPhaser);
            m.setRateHz(0.0f); // frozen sweep -> stationary resonant frequency
            m.setFeedback(fb);
            m.setMix(0.5f);
            m.prepare({SR, BLK});
            std::vector<float> l((size_t)SR / 4, 0.0f); // 0.25 s
            l[0] = 1.0f;                                // impulse
            auto r = l;
            run(m, l, r);
            double e = 0;
            for (size_t i = 400; i < l.size(); ++i) // skip the direct transient
                e += (double)l[i] * (double)l[i];
            return e;
        };
        const double e0 = tailEnergy(0.0f);
        const double e4 = tailEnergy(0.4f);
        const double e8 = tailEnergy(0.8f);
        CHECK(e0 < e4 && e4 < e8 && e8 > 1e-6,
              "T17 phaser rings more with feedback (tail energy %.2e < %.2e < %.2e)", e0, e4, e8);
    }

    // ---- T18: flanger Manual shifts the static comb position ----
    {
        // depth 0 -> no sweep, so the delay = Manual base only. The delayed copy
        // shows up as a secondary peak in the impulse response; its time = delay.
        auto combDelayMs = [](float manual) {
            ModVoice m;
            m.setType(ModVoice::kFlanger);
            m.setDepth(0.0f);     // static comb (no sweep)
            m.setFeedback(0.0f);
            m.setManual(manual);
            m.setMix(0.5f);
            m.prepare({SR, BLK});
            std::vector<float> l((size_t)SR / 10, 0.0f);
            l[0] = 1.0f;          // impulse
            auto r = l;
            run(m, l, r);
            size_t pkAt = 0;
            double pk = 0;
            for (size_t i = 8; i < l.size(); ++i) // skip the dry impulse
                if (std::abs(l[i]) > pk) { pk = std::abs(l[i]); pkAt = i; }
            return (double)pkAt * 1000.0 / SR;
        };
        const double dLo = combDelayMs(0.0f); // ~kFlMinMs (0.5 ms)
        const double dHi = combDelayMs(1.0f); // ~kFlManualMaxMs (8 ms)
        CHECK(dHi > dLo + 2.0, "T18 Manual moves the comb (delay %.2f -> %.2f ms)", dLo, dHi);
    }

    // ---- T19: flanger Invert flips the comb polarity (notch <-> peak) ----
    {
        // Static comb; invert negates the delayed path, so dry+wet becomes dry-wet
        // -> peaks and notches swap. Per-frequency magnitudes should anti-correlate.
        auto combMags = [](bool inv) {
            ModVoice m;
            m.setType(ModVoice::kFlanger);
            m.setDepth(0.0f);
            m.setFeedback(0.0f);
            m.setManual(0.5f); // mid delay -> several notches across the probe band
            m.setInvert(inv);
            m.setMix(0.5f);
            m.prepare({SR, BLK});
            std::vector<float> l((size_t)SR, 0.0f);
            for (int kk = 0; kk < 40; ++kk)
            {
                const double f = 200.0 + kk * 55.0; // 200..2345 Hz
                for (size_t i = 0; i < l.size(); ++i)
                    l[i] += (float)(0.02 * std::sin(2.0 * M_PI * f * (double)i / SR + kk));
            }
            auto r = l;
            run(m, l, r);
            std::vector<double> mags;
            for (int kk = 0; kk < 40; ++kk)
                mags.push_back(mag(l, (size_t)SR / 2, (size_t)SR / 2, 200.0 + kk * 55.0));
            return mags;
        };
        auto off = combMags(false), on = combMags(true);
        double mo = 0, mn = 0;
        for (size_t i = 0; i < off.size(); ++i) { mo += off[i]; mn += on[i]; }
        mo /= (double)off.size();
        mn /= (double)on.size();
        double cov = 0, vo = 0, vn = 0;
        for (size_t i = 0; i < off.size(); ++i)
        {
            const double a = off[i] - mo, b = on[i] - mn;
            cov += a * b; vo += a * a; vn += b * b;
        }
        const double corr = cov / (std::sqrt(vo * vn) + 1e-12);
        CHECK(corr < -0.3, "T19 flanger Invert flips comb polarity (corr %.2f < -0.3)", corr);
    }

    // ---- T20: free Rate is capped per effect (flanger 20 Hz, others 10 Hz) ----
    {
        ModVoice f;
        f.setType(ModVoice::kFlanger);
        f.setRateHz(20.0f);
        CHECK(std::abs(f.effectiveRateHz() - 20.0f) < 0.01f,
              "T20 flanger free rate reaches 20 Hz (%.2f)", f.effectiveRateHz());

        ModVoice t;
        t.setType(ModVoice::kTremolo);
        t.setRateHz(20.0f);
        CHECK(std::abs(t.effectiveRateHz() - 10.0f) < 0.01f,
              "T20 standard free rate capped at 10 Hz (%.2f)", t.effectiveRateHz());

        // chorus is held slower still so it never tips into vibrato/warble
        ModVoice c;
        c.setType(ModVoice::kChorus);
        c.setRateHz(20.0f);
        CHECK(std::abs(c.effectiveRateHz() - ModVoice::kChorusMaxRateHz) < 0.01f,
              "T20 chorus free rate capped at %.2f Hz (%.2f)", ModVoice::kChorusMaxRateHz, c.effectiveRateHz());

        // sync ignores the cap (honours the host division): 1/16 @240 BPM = 16 Hz
        ModVoice s;
        s.setType(ModVoice::kChorus);
        s.setBpm(240.0);
        s.setSyncIndex(9);
        CHECK(s.effectiveRateHz() > 10.0f, "T20 synced rate not capped (%.2f Hz)", s.effectiveRateHz());
    }

    // ---- T21: Bi-Phase finite + non-silent (parallel and series) ----
    {
        bool ok = true;
        for (bool series : {false, true})
        {
            ModVoice m;
            m.setType(ModVoice::kBiPhase);
            m.setRateHz(2.0f);
            m.setDepth(0.7f);
            m.setFeedback(0.5f);
            m.setP2Ratio(1.5f);
            m.setSeries(series);
            m.prepare({SR, BLK});
            auto l = tone(440.0, 0.3, (int)SR), r = l;
            run(m, l, r);
            double e = 0;
            bool fin = true;
            for (float v : l) { if (!std::isfinite(v)) fin = false; e += (double)v * v; }
            if (!fin || e < 1e-3) ok = false;
        }
        CHECK(ok, "T21 Bi-Phase finite + non-silent (parallel & series)");
    }

    // ---- T22: Bi-Phase parallel stereo A/B split scales with Width ----
    {
        auto spread = [](float width) {
            ModVoice m;
            m.setType(ModVoice::kBiPhase);
            m.setRateHz(2.0f);
            m.setDepth(0.7f);
            m.setP2Ratio(1.5f);
            m.setSeries(false); // parallel
            m.setWidth(width);
            m.prepare({SR, BLK});
            auto l = tone(1000.0, 0.5, (int)SR), r = l;
            run(m, l, r);
            double d = 0;
            for (size_t i = 4800; i < l.size(); ++i)
                d = std::max(d, (double)std::abs(l[i] - r[i]));
            return d;
        };
        CHECK(spread(0.0f) < 1e-6, "T22 Bi-Phase width 0 -> L==R (max diff %.2e)", spread(0.0f));
        CHECK(spread(1.0f) > 0.02, "T22 Bi-Phase width 1 -> A/B stereo split (max diff %.3f)", spread(1.0f));
    }

    // ---- T23: Bi-Phase resonance grows with feedback (impulse ring, frozen sweep) ----
    {
        auto tailEnergy = [](float fb) {
            ModVoice m;
            m.setType(ModVoice::kBiPhase);
            m.setRateHz(0.0f); // frozen sweep -> stationary resonant frequency
            m.setDepth(0.7f);
            m.setFeedback(fb);
            m.setSeries(false);
            m.prepare({SR, BLK});
            std::vector<float> l((size_t)SR / 4, 0.0f);
            l[0] = 1.0f; // impulse
            auto r = l;
            run(m, l, r);
            double e = 0;
            for (size_t i = 400; i < l.size(); ++i)
                e += (double)l[i] * (double)l[i];
            return e;
        };
        const double e0 = tailEnergy(0.0f), e4 = tailEnergy(0.4f), e8 = tailEnergy(0.8f);
        CHECK(e0 < e4 && e4 < e8 && e8 > 1e-6,
              "T23 Bi-Phase rings more with feedback (tail %.2e < %.2e < %.2e)", e0, e4, e8);
    }

    // ---- T24: Bi-Phase second sweep generator (P2Ratio) changes the motion ----
    {
        auto runRatio = [](float ratio) {
            ModVoice m;
            m.setType(ModVoice::kBiPhase);
            m.setRateHz(2.0f);
            m.setDepth(0.7f);
            m.setP2Ratio(ratio);
            m.setSeries(false);
            m.setWidth(0.0f); // mono-sum both cores so Gen 2 (Core B) is in the measured channel
            m.prepare({SR, BLK});
            auto l = tone(1000.0, 0.5, (int)SR), r = l;
            run(m, l, r);
            return l;
        };
        auto a = runRatio(1.0f), b = runRatio(2.5f);
        double d = 0;
        for (size_t i = 4800; i < a.size(); ++i)
            d = std::max(d, (double)std::abs(a[i] - b[i]));
        CHECK(d > 0.02, "T24 Bi-Phase Gen 2 ratio changes the sweep (max diff %.3f)", d);
    }

    // ---- T25: photocell warp is asymmetric, and more lopsided when faster ----
    {
        // Drive the photocell warp DIRECTLY with a SYMMETRIC triangle (so the
        // input contributes no asymmetry) at two rates. The lamp heats fast and
        // cools slow, so the warped control spends LONGER falling than rising --
        // and, because the lamp's time constants are fixed, that bias GROWS as the
        // drive speeds up (the lamp can't keep up). Measure (fall - rise) sample
        // counts over a steady cycle; a symmetric warp would give ~0.
        auto fallBias = [](double rateHz) {
            ModVoice m;
            m.setType(ModVoice::kUniVibe);
            m.prepare({SR, BLK}); // lamp coeffs are set here (rate-independent)
            const int period = (int)(SR / rateHz);
            std::vector<float> ctl;
            for (int c = 0; c < 30; ++c) // run several cycles to reach steady state
                for (int i = 0; i < period; ++i)
                {
                    const float f = (float)i / (float)period;                  // 0..1
                    const float tri = (f < 0.5f) ? (-1.0f + 4.0f * f)          // symmetric
                                                 : (3.0f - 4.0f * f);          // triangle [-1,1]
                    ctl.push_back(m.uniVibeWarp(0, tri));
                }
            int rising = 0, falling = 0; // over the last (steady) cycle
            for (size_t i = ctl.size() - (size_t)period; i + 1 < ctl.size(); ++i)
            {
                if (ctl[i + 1] > ctl[i]) ++rising;
                else if (ctl[i + 1] < ctl[i]) ++falling;
            }
            return (double)(falling - rising) / (double)period; // >0 = slow cool dominates
        };
        const double slow = fallBias(1.0);
        const double fast = fallBias(8.0);
        CHECK(fast > slow + 0.01 && fast > 0.0,
              "T25 photocell warp lopsided, more so when faster (fall bias slow %.3f -> fast %.3f)",
              slow, fast);
    }

    // ---- T26: Uni-Vibe ZDF bounded across extremes; sweep moves the spectrum ----
    {
        // (a) the zero-delay positive-feedback resolve stays finite + bounded at
        // the control extremes (depth 0/1 x rate 0/10).
        bool ok = true;
        double pk = 0;
        for (float d : {0.0f, 1.0f})
            for (float rt : {0.0f, 10.0f})
            {
                ModVoice m;
                m.setType(ModVoice::kUniVibe);
                m.setRateHz(rt);
                m.setDepth(d);
                m.prepare({SR, BLK});
                auto l = tone(440.0, 0.4, (int)SR), r = l;
                run(m, l, r);
                for (float v : l) { if (!std::isfinite(v)) ok = false; pk = std::max(pk, (double)std::abs(v)); }
            }
        CHECK(ok && pk < 4.0, "T26 uni-vibe ZDF bounded across depth/rate extremes (peak %.2f)", pk);

        // (b)/(c) the swept all-pass chain (hardwired 50/50 mix) moves the spectrum
        // over time. Tested at full depth AND at depth 0, where the depth floor
        // (kUniDepthMin) must keep it breathing rather than freezing static.
        auto specMoves = [](float depth) {
            ModVoice m;
            m.setType(ModVoice::kUniVibe);
            m.setRateHz(0.2f);
            m.setDepth(depth);
            m.prepare({SR, BLK});
            std::vector<float> l((size_t)SR * 5, 0.0f);
            for (int kk = 0; kk < 40; ++kk)
            {
                const double f = 100.0 * std::pow(80.0, kk / 39.0);
                for (size_t i = 0; i < l.size(); ++i)
                    l[i] += (float)(0.02 * std::sin(2.0 * M_PI * f * (double)i / SR + kk));
            }
            auto r = l;
            run(m, l, r);
            double diff = 0, ref = 0;
            for (int kk = 0; kk < 40; ++kk)
            {
                const double f = 100.0 * std::pow(80.0, kk / 39.0);
                const double m1 = mag(l, (size_t)SR, (size_t)SR / 2, f);
                const double m2 = mag(l, (size_t)(SR * 3.5), (size_t)SR / 2, f);
                diff += std::abs(m1 - m2);
                ref += std::max(m1, m2);
            }
            return diff / ref;
        };
        CHECK(specMoves(0.9f) > 0.10, "T26 uni-vibe sweeps the spectrum at depth 0.9 (rel diff %.2f)", specMoves(0.9f));
        CHECK(specMoves(0.0f) > 0.02, "T26 depth-floor keeps it breathing at depth 0 (rel diff %.2f)", specMoves(0.0f));
    }

    // ---- T27: Leslie 2D angle-EQ model — horn brightness tracks rotor angle ----
    {
        // (a) the angle-tilt gain LAW (single source of truth used in kRotary):
        // facing the mic = flat/bright (gain 1); pointing fully away = darkest
        // (1 - kRotHornTiltDepth); monotonic in facing; depth 0 disables it.
        const float gFace = ModVoice::hornTiltGain(1.0f, 1.0f);
        const float gAway = ModVoice::hornTiltGain(0.0f, 1.0f);
        CHECK(std::abs(gFace - 1.0f) < 1e-6f, "T27 angle-tilt flat when horn faces mic (g %.3f)", gFace);
        CHECK(std::abs(gAway - (1.0f - ModVoice::kRotHornTiltDepth)) < 1e-6f && gAway < 1.0f,
              "T27 angle-tilt cuts highs fully away (g %.3f)", gAway);
        CHECK(ModVoice::hornTiltGain(0.25f, 1.0f) < ModVoice::hornTiltGain(0.75f, 1.0f),
              "T27 angle-tilt monotonic in facing");
        CHECK(std::abs(ModVoice::hornTiltGain(0.0f, 0.0f) - 1.0f) < 1e-6f,
              "T27 angle-tilt off at depth 0");

        // (b) ISOLATE the angle EQ from the common directional "wom". Run a 6 kHz
        // tone and a 1 kHz tone (both inside the horn band, above the 800 Hz
        // crossover) through IDENTICAL rotor motion -- deterministic from reset, so
        // both see the same angle at the same time. The wom multiplies both
        // equally so it CANCELS in the per-window envelope ratio 6k/1k; what's left
        // modulates at the rotor rate only because the shelf darkens the 6 kHz
        // (high band) far more than the 1 kHz as the horn turns away. A static body
        // EQ would leave the ratio constant. Expected swing ~0.2 (first-order
        // crossover: 6k shelf 1.0->0.63 vs 1k 1.0->0.96 facing->away); levels cancel.
        auto env = [](double freq) {
            ModVoice m;
            m.setType(ModVoice::kRotary);
            m.setDepth(1.0f);
            m.setRotFast(true);
            m.prepare({SR, BLK});
            auto l = tone(freq, 0.4, (int)SR * 2), r = l;
            run(m, l, r);
            return envelope(l, (int)(SR * 0.005)); // ~5 ms peak windows
        };
        auto e6 = env(6000.0), e1 = env(1000.0);
        double lo = 1e9, hi = 0;
        for (size_t i = e6.size() / 2; i < e6.size(); ++i) // skip the spin-up half
        {
            const double ratio = e1[i] > 1e-9 ? e6[i] / e1[i] : 0.0;
            lo = std::min(lo, ratio); hi = std::max(hi, ratio);
        }
        const double ratioSwing = hi > 0 ? (hi - lo) / (hi + lo) : 0.0;
        CHECK(ratioSwing > 0.08, "T27 angle EQ modulates the high/mid ratio with rotor angle (swing %.3f, ~0.2 expected)", ratioSwing);

        // (c) the added throat biquad + angle shelf leave kRotary finite + bounded
        // across slow/fast x depth extremes with a broadband input.
        bool ok = true; double pk = 0;
        for (bool fast : {false, true})
            for (float d : {0.0f, 1.0f})
            {
                ModVoice m;
                m.setType(ModVoice::kRotary);
                m.setDepth(d);
                m.setRotFast(fast);
                m.prepare({SR, BLK});
                std::vector<float> l((size_t)SR, 0.0f);
                for (int kk = 0; kk < 30; ++kk)
                {
                    const double f = 120.0 * std::pow(60.0, kk / 29.0);
                    for (size_t i = 0; i < l.size(); ++i)
                        l[i] += (float)(0.02 * std::sin(2.0 * M_PI * f * (double)i / SR + kk));
                }
                auto r = l;
                run(m, l, r);
                for (float v : l) { if (!std::isfinite(v)) ok = false; pk = std::max(pk, (double)std::abs(v)); }
            }
        CHECK(ok && pk < 4.0, "T27 rotary finite + bounded with throat EQ + angle shelf (peak %.2f)", pk);
    }

    // ---- T28: Leslie tube-amp realism (always-on voice + ADAA + mid breakup) ----
    {
        // (a) the saturation CURVE (memoryless; processSample applies it via ADAA)
        // is bounded and compresses (sub-linear) at full drive, monotonic.
        const float c1 = ModVoice::ampShape(1.0f, 1.0f);
        CHECK(std::abs(c1) < 1.5f && std::abs(ModVoice::ampShape(-1.0f, 1.0f)) < 1.5f,
              "T28 amp curve bounded at full drive (%.3f)", c1);
        const float cHi = ModVoice::ampShape(1.0f, 1.0f), cLo = ModVoice::ampShape(0.25f, 1.0f);
        CHECK(cLo > 0 && cHi / cLo < 4.0f,
              "T28 amp curve compresses at full drive (4x in -> %.2fx out)", cHi / cLo);
        CHECK(ModVoice::ampShape(0.5f, 1.0f) > ModVoice::ampShape(0.2f, 1.0f),
              "T28 amp curve monotonic");

        // (b) Drive engages: a 1 kHz tone picks up far more 3rd-harmonic content at
        // full drive than clean. Rotor modulation frozen (depth 0) to isolate the
        // amp; the amp is pre-crossover so the harmonics reach the output.
        auto h3ratio = [](float drive) {
            ModVoice m;
            m.setType(ModVoice::kRotary);
            m.setDepth(0.0f);
            m.setDrive(drive);
            m.prepare({SR, BLK});
            auto l = tone(1000.0, 0.5, (int)SR), r = l;
            run(m, l, r);
            const double f0 = mag(l, (size_t)SR / 2, (size_t)SR / 2, 1000.0);
            const double f3 = mag(l, (size_t)SR / 2, (size_t)SR / 2, 3000.0);
            return f0 > 1e-9 ? f3 / f0 : 0.0;
        };
        const double clean = h3ratio(0.0f), driven = h3ratio(1.0f);
        CHECK(driven > clean + 0.02, "T28 Drive adds harmonics (3rd/fund clean %.3f -> driven %.3f)", clean, driven);

        // (c) finite + bounded across the drive range with broadband input.
        bool ok = true; double pk = 0;
        for (float d : {0.0f, 0.5f, 1.0f})
        {
            ModVoice m;
            m.setType(ModVoice::kRotary);
            m.setDrive(d);
            m.setDepth(1.0f);
            m.prepare({SR, BLK});
            std::vector<float> l((size_t)SR, 0.0f);
            for (int kk = 0; kk < 30; ++kk)
            {
                const double f = 120.0 * std::pow(60.0, kk / 29.0);
                for (size_t i = 0; i < l.size(); ++i)
                    l[i] += (float)(0.02 * std::sin(2.0 * M_PI * f * (double)i / SR + kk));
            }
            auto r = l;
            run(m, l, r);
            for (float v : l) { if (!std::isfinite(v)) ok = false; pk = std::max(pk, (double)std::abs(v)); }
        }
        CHECK(ok && pk < 4.0, "T28 rotary amp finite + bounded across drive (peak %.2f)", pk);
    }

    // ---- T29: Rotary Horn/Drum balance — neutral center, fades to solo ----
    {
        // (a) the balance LAW (single source of truth): 0.5 = both rotors unity
        // (neutral, bit-exact to no-knob); full horn mutes the drum and vice versa.
        CHECK(ModVoice::rotorBalance(0.5f, true) == 1.0f && ModVoice::rotorBalance(0.5f, false) == 1.0f,
              "T29 balance neutral at center (both unity)");
        CHECK(ModVoice::rotorBalance(1.0f, true) == 1.0f && ModVoice::rotorBalance(1.0f, false) == 0.0f,
              "T29 full horn mutes the drum");
        CHECK(ModVoice::rotorBalance(0.0f, true) == 0.0f && ModVoice::rotorBalance(0.0f, false) == 1.0f,
              "T29 full drum mutes the horn");

        // (b) audio: a low (drum-band, <800 Hz) tone collapses toward full horn; a
        // high (horn-band) tone collapses toward full drum. Rotor frozen (depth 0)
        // so we read steady levels; the crossover/amp/cabinet are common to both
        // knob settings so they cancel in the ratio.
        auto level = [](double freq, float knob) {
            ModVoice m;
            m.setType(ModVoice::kRotary);
            m.setDepth(0.0f);
            m.setHornDrum(knob);
            m.prepare({SR, BLK});
            auto l = tone(freq, 0.4, (int)SR), r = l;
            run(m, l, r);
            return mag(l, (size_t)SR / 2, (size_t)SR / 2, freq);
        };
        CHECK(level(120.0, 1.0f) < 0.6 * level(120.0, 0.5f),
              "T29 full horn ducks the drum-band tone (%.4f vs center %.4f)", level(120.0, 1.0f), level(120.0, 0.5f));
        CHECK(level(2000.0, 0.0f) < 0.6 * level(2000.0, 0.5f),
              "T29 full drum ducks the horn-band tone (%.4f vs center %.4f)", level(2000.0, 0.0f), level(2000.0, 0.5f));
    }

    // ---- T30: section Level Lock keeps output level steady across changes ----
    {
        using nam_rig::ModLevelLock;
        const int N = (int)SR * 3;
        auto rms = [](const std::vector<float> &x, size_t from, size_t len) {
            double s = 0;
            for (size_t i = 0; i < len; ++i) s += (double)x[from + i] * x[from + i];
            return std::sqrt(s / (double)len);
        };

        // (a) a static section offset (x0.6) is pulled back to the input level.
        {
            ModLevelLock lk; lk.prepare(SR);
            auto in = tone(1000.0, 0.5, N);
            std::vector<float> out(in.size());
            for (size_t p = 0; p < in.size(); p += BLK)
            {
                const int n = (int)std::min<size_t>(BLK, in.size() - p);
                for (int i = 0; i < n; ++i) out[p + (size_t)i] = in[p + (size_t)i] * 0.6f;
                lk.observeInput(in.data() + p, in.data() + p, n);
                lk.applyOutput(out.data() + p, out.data() + p, n);
            }
            const size_t w = (size_t)SR / 2;
            const double ratio = rms(out, out.size() - w, w) / rms(in, in.size() - w, w);
            CHECK(std::abs(ratio - 1.0) < 0.08, "T30 static offset corrected to unity (out/in %.3f)", ratio);
        }

        // (b) a 4 Hz tremolo (depth 0.8) on the output: long-term level locked to
        // the input, but the rhythmic pulse passes through (slow TC ignores it).
        {
            ModLevelLock lk; lk.prepare(SR);
            auto in = tone(1000.0, 0.5, N);
            std::vector<float> out(in.size());
            for (size_t p = 0; p < in.size(); p += BLK)
            {
                const int n = (int)std::min<size_t>(BLK, in.size() - p);
                for (int i = 0; i < n; ++i)
                {
                    const double t = (double)(p + (size_t)i) / SR;
                    const float g = (float)(1.0 - 0.8 * (0.5 + 0.5 * std::sin(2.0 * M_PI * 4.0 * t)));
                    out[p + (size_t)i] = in[p + (size_t)i] * g;
                }
                lk.observeInput(in.data() + p, in.data() + p, n);
                lk.applyOutput(out.data() + p, out.data() + p, n);
            }
            const size_t w = (size_t)SR; // integer number of tremolo cycles
            const double ratio = rms(out, out.size() - w, w) / rms(in, in.size() - w, w);
            CHECK(std::abs(ratio - 1.0) < 0.12, "T30 AM long-term level locked (out/in %.3f)", ratio);
            std::vector<float> tail(out.end() - (long)w, out.end());
            auto env = envelope(tail, 48);
            double mn = 1e9, mx = 0;
            for (auto e : env) { mn = std::min(mn, e); mx = std::max(mx, e); }
            CHECK(mn / mx < 0.4, "T30 tremolo pulse preserved through lock (min/max %.2f < 0.4)", mn / mx);
        }

        // (c) silence-safe: silent input holds unity (no noise-floor boost); a
        // silenced output under live input doesn't run the gain away.
        {
            ModLevelLock lk; lk.prepare(SR);
            std::vector<float> z((size_t)SR, 0.0f), small((size_t)SR, 1.0e-4f);
            for (size_t p = 0; p < z.size(); p += BLK)
            {
                const int n = (int)std::min<size_t>(BLK, z.size() - p);
                lk.observeInput(z.data() + p, z.data() + p, n);
                lk.applyOutput(small.data() + p, small.data() + p, n);
            }
            bool finite = true;
            for (float v : small) if (!std::isfinite(v)) finite = false;
            CHECK(finite && std::abs(lk.gain() - 1.0f) < 0.05f,
                  "T30 silent input holds unity gain (g %.3f)", lk.gain());

            ModLevelLock lk2; lk2.prepare(SR);
            auto in = tone(1000.0, 0.5, (int)SR);
            std::vector<float> out((size_t)SR, 0.0f); // effect killed the signal
            for (size_t p = 0; p < in.size(); p += BLK)
            {
                const int n = (int)std::min<size_t>(BLK, in.size() - p);
                lk2.observeInput(in.data() + p, in.data() + p, n);
                lk2.applyOutput(out.data() + p, out.data() + p, n);
            }
            CHECK(lk2.gain() <= ModLevelLock::kMaxGain + 1e-6f,
                  "T30 silent output keeps the gain bounded (g %.3f)", lk2.gain());
        }

        // (d) disabled = bit-exact passthrough.
        {
            ModLevelLock lk; lk.prepare(SR); lk.setEnabled(false);
            auto out = tone(500.0, 0.7, (int)SR / 2), ref = out;
            lk.observeInput(out.data(), out.data(), (int)out.size());
            lk.applyOutput(out.data(), out.data(), (int)out.size());
            bool same = true;
            for (size_t i = 0; i < out.size(); ++i) if (out[i] != ref[i]) { same = false; break; }
            CHECK(same, "T30 disabled lock is bit-exact passthrough");
        }

        // (e) integration: a 50/50 flanger shifts the section level; with the lock
        // on, the section output RMS sits closer to the input than raw.
        {
            auto runFlanger = [&](bool lock) {
                ModBlock m;
                m.setLevelLock(lock);
                m.setType(0, ModVoice::kFlanger);
                m.setRateHz(0, 0.3f);
                m.setDepth(0, 0.7f);
                m.setMix(0, 1.0f);
                m.setSlotBypassed(0, false);
                m.setSlotBypassed(1, true);
                m.setSlotBypassed(2, true);
                m.prepare({SR, BLK});
                auto l = tone(800.0, 0.5, N), r = l;
                run(m, l, r);
                return l;
            };
            auto in = tone(800.0, 0.5, N);
            const size_t w = (size_t)SR;
            const double rin = rms(in, in.size() - w, w);
            const double rLocked = rms(runFlanger(true), (size_t)N - w, w);
            const double rRaw = rms(runFlanger(false), (size_t)N - w, w);
            CHECK(std::abs(rLocked - rin) < std::abs(rRaw - rin) || std::abs(rLocked / rin - 1.0) < 0.1,
                  "T30 lock tightens section level (locked %.3f raw %.3f vs in %.3f)", rLocked, rRaw, rin);
        }
    }

    // ---- T31: section routing — Cartesian blend pad + Series/Parallel ----
    {
        auto rms = [](const std::vector<float> &x, size_t from, size_t len) {
            double s = 0;
            for (size_t i = 0; i < len; ++i) s += (double)x[from + i] * x[from + i];
            return std::sqrt(s / (double)len);
        };
        const int N = (int)SR * 2;

        // (a) pad weight math: pure at the 3 nodes, equal at the centroid, always
        // sums to 1, clamps + renormalises outside the triangle.
        {
            float w[3];
            ModBlock::padWeights(0.5f, 1.0f, w);
            const bool n0 = std::abs(w[0] - 1.0f) < 1e-5f && w[1] < 1e-5f && w[2] < 1e-5f;
            ModBlock::padWeights(0.0f, 0.0f, w);
            const bool n1 = std::abs(w[1] - 1.0f) < 1e-5f && w[0] < 1e-5f && w[2] < 1e-5f;
            ModBlock::padWeights(1.0f, 0.0f, w);
            const bool n2 = std::abs(w[2] - 1.0f) < 1e-5f && w[0] < 1e-5f && w[1] < 1e-5f;
            ModBlock::padWeights(0.5f, 1.0f / 3.0f, w);
            const bool ctr = std::abs(w[0] - 1.0f / 3.0f) < 1e-4f && std::abs(w[1] - 1.0f / 3.0f) < 1e-4f
                             && std::abs(w[2] - 1.0f / 3.0f) < 1e-4f;
            ModBlock::padWeights(5.0f, -3.0f, w); // far outside the triangle
            const bool out = std::abs((w[0] + w[1] + w[2]) - 1.0f) < 1e-5f && w[0] >= 0 && w[1] >= 0 && w[2] >= 0;
            CHECK(n0 && n1 && n2 && ctr && out, "T31 pad weights: pure nodes, equal centroid, sum=1, clamped outside");
        }

        // (b) parallel, one active slot, puck on its node, modMix=1, lock off:
        // the section output matches a standalone ModVoice of that effect.
        {
            ModBlock m;
            m.setLevelLock(false);
            m.setParallel(true);
            m.setModMix(1.0f);
            m.setPad(0.5f, 1.0f); // node 0
            m.setType(0, ModVoice::kChorus);
            m.setRateHz(0, 1.0f); m.setDepth(0, 0.7f); m.setMix(0, 1.0f);
            m.setSlotBypassed(0, false); m.setSlotBypassed(1, true); m.setSlotBypassed(2, true);
            m.prepare({SR, BLK});

            ModVoice ref;
            ref.setType(ModVoice::kChorus);
            ref.setRateHz(1.0f); ref.setDepth(0.7f); ref.setMix(1.0f);
            ref.prepare({SR, BLK});

            auto x = tone(700.0, 0.5, N);
            auto la = x, ra = x, lb = x, rb = x;
            run(m, la, ra);
            run(ref, lb, rb);
            double d = 0;
            for (size_t i = (size_t)SR; i < x.size(); ++i) // skip the weight-ramp settle
                d = std::max(d, (double)std::abs(la[i] - lb[i]));
            CHECK(d < 1e-3, "T31 parallel single-slot == standalone voice after settle (max diff %.2e)", d);
        }

        // (c) the pad actually blends: node0 vs node1 give different outputs, both
        // finite + non-silent (two distinct effects in parallel).
        {
            auto runAt = [&](float px, float py) {
                ModBlock m;
                m.setLevelLock(false);
                m.setParallel(true);
                m.setModMix(1.0f);
                m.setPad(px, py);
                m.setType(0, ModVoice::kTremolo); m.setRateHz(0, 4.0f); m.setDepth(0, 0.8f);
                m.setType(1, ModVoice::kVibrato);  m.setRateHz(1, 5.0f); m.setDepth(1, 0.7f);
                m.setSlotBypassed(0, false); m.setSlotBypassed(1, false); m.setSlotBypassed(2, true);
                m.prepare({SR, BLK});
                auto l = tone(700.0, 0.5, N), r = l;
                run(m, l, r);
                return l;
            };
            auto at0 = runAt(0.5f, 1.0f); // node 0 (tremolo)
            auto at1 = runAt(0.0f, 0.0f); // node 1 (vibrato)
            double diff = 0, pk0 = 0, pk1 = 0;
            for (size_t i = (size_t)SR; i < at0.size(); ++i)
            {
                diff = std::max(diff, (double)std::abs(at0[i] - at1[i]));
                pk0 = std::max(pk0, (double)std::abs(at0[i]));
                pk1 = std::max(pk1, (double)std::abs(at1[i]));
            }
            bool finite = true;
            for (float v : at0) if (!std::isfinite(v)) finite = false;
            for (float v : at1) if (!std::isfinite(v)) finite = false;
            CHECK(finite && diff > 0.05 && pk0 > 0.05 && pk1 > 0.05,
                  "T31 pad blends between nodes (node0 vs node1 diff %.2f, both non-silent)", diff);
        }

        // (d) bypassed slot drops out: puck aimed at a bypassed node still yields
        // the active slot's output (weights renormalise; no silence).
        {
            ModBlock m;
            m.setLevelLock(false);
            m.setParallel(true);
            m.setModMix(1.0f);
            m.setPad(1.0f, 0.0f); // node 2 ...
            m.setType(0, ModVoice::kChorus); m.setRateHz(0, 1.0f); m.setDepth(0, 0.7f); m.setMix(0, 1.0f);
            m.setSlotBypassed(0, false); m.setSlotBypassed(1, true); m.setSlotBypassed(2, true); // ... slot2 bypassed
            m.prepare({SR, BLK});
            auto l = tone(700.0, 0.5, N), r = l;
            run(m, l, r);
            double pk = 0; bool finite = true;
            for (size_t i = (size_t)SR; i < l.size(); ++i) pk = std::max(pk, (double)std::abs(l[i]));
            for (float v : l) if (!std::isfinite(v)) finite = false;
            CHECK(finite && pk > 0.1, "T31 puck on a bypassed node still passes the active slot (peak %.2f)", pk);
        }

        // (e) pad + Level Lock = steady loudness across the puck: two effects with
        // different inherent levels land at ~the same output level at either node.
        {
            auto runAt = [&](float px, float py) {
                ModBlock m;
                m.setParallel(true); // Level Lock ON (default)
                m.setModMix(1.0f);
                m.setPad(px, py);
                m.setType(0, ModVoice::kTremolo); m.setRateHz(0, 5.0f); m.setDepth(0, 0.8f); // AM -> quieter raw
                m.setType(1, ModVoice::kChorus);  m.setRateHz(1, 1.0f); m.setDepth(1, 0.7f); m.setMix(1, 1.0f);
                m.setSlotBypassed(0, false); m.setSlotBypassed(1, false); m.setSlotBypassed(2, true);
                m.prepare({SR, BLK});
                auto l = tone(700.0, 0.5, N), r = l;
                run(m, l, r);
                return l;
            };
            const size_t w2 = (size_t)SR;
            auto a = runAt(0.5f, 1.0f); // node 0 (tremolo)
            auto b = runAt(0.0f, 0.0f); // node 1 (chorus)
            const double r0 = rms(a, a.size() - w2, w2);
            const double r1 = rms(b, b.size() - w2, w2);
            CHECK(std::abs(r0 / r1 - 1.0) < 0.15,
                  "T31 Level Lock holds loudness across the pad (node0 %.3f vs node1 %.3f)", r0, r1);
        }
    }

    // ---- T32: momentary solo isolates a slot (series + parallel; overrides bypass) ----
    {
        const int N = (int)SR * 2;
        auto x = tone(700.0, 0.5, N);
        auto setup3 = [](ModBlock &m) {
            m.setType(0, ModVoice::kChorus);  m.setRateHz(0, 1.0f); m.setDepth(0, 0.7f); m.setMix(0, 1.0f);
            m.setType(1, ModVoice::kTremolo); m.setRateHz(1, 4.0f); m.setDepth(1, 0.6f); m.setMix(1, 1.0f);
            m.setType(2, ModVoice::kVibrato); m.setRateHz(2, 5.0f); m.setDepth(2, 0.7f);
            m.setSlotBypassed(0, false);
            m.setSlotBypassed(1, false);
            m.setSlotBypassed(2, false);
        };
        auto refTrem = [](ModVoice &v) {
            v.setType(ModVoice::kTremolo);
            v.setRateHz(4.0f);
            v.setDepth(0.6f);
            v.setMix(1.0f);
        };

        // (a) SERIES: solo slot 1 -> output == standalone tremolo (others muted).
        {
            ModBlock m;
            m.setLevelLock(false);
            setup3(m);
            m.setSlotSolo(1, true);
            m.prepare({SR, BLK});
            ModVoice ref;
            refTrem(ref);
            ref.prepare({SR, BLK});
            auto la = x, ra = x, lb = x, rb = x;
            run(m, la, ra);
            run(ref, lb, rb);
            double d = 0;
            for (size_t i = 0; i < x.size(); ++i) d = std::max(d, (double)std::abs(la[i] - lb[i]));
            CHECK(d < 1e-6, "T32 series solo isolates the slot (== standalone, diff %.2e)", d);
        }

        // (b) PARALLEL: solo slot 1 -> the bus is just that branch (after settle).
        {
            ModBlock m;
            m.setLevelLock(false);
            m.setParallel(true);
            m.setModMix(1.0f);
            m.setPad(0.5f, 1.0f / 3.0f);
            setup3(m);
            m.setSlotSolo(1, true);
            m.prepare({SR, BLK});
            ModVoice ref;
            refTrem(ref);
            ref.prepare({SR, BLK});
            auto la = x, ra = x, lb = x, rb = x;
            run(m, la, ra);
            run(ref, lb, rb);
            double d = 0;
            for (size_t i = (size_t)SR; i < x.size(); ++i) d = std::max(d, (double)std::abs(la[i] - lb[i]));
            CHECK(d < 1e-3, "T32 parallel solo isolates the branch (== standalone after settle, diff %.2e)", d);
        }

        // (c) solo OVERRIDES bypass: all slots bypassed, solo slot 1 -> still heard.
        {
            ModBlock m;
            m.setLevelLock(false);
            setup3(m);
            m.setSlotBypassed(0, true);
            m.setSlotBypassed(1, true);
            m.setSlotBypassed(2, true);
            m.setSlotSolo(1, true);
            m.prepare({SR, BLK});
            auto l = x, r = x;
            run(m, l, r);
            double pk = 0;
            for (float v : l) pk = std::max(pk, (double)std::abs(v));
            CHECK(pk > 0.1, "T32 solo overrides bypass (soloed slot still heard, peak %.2f)", pk);
        }

        // (d) clearing solo restores the full series mix (differs from soloed).
        {
            ModBlock soloed, full;
            soloed.setLevelLock(false);
            full.setLevelLock(false);
            setup3(soloed);
            setup3(full);
            soloed.setSlotSolo(1, true);
            soloed.prepare({SR, BLK});
            full.prepare({SR, BLK});
            auto ls = x, rs = x, lf = x, rf = x;
            run(soloed, ls, rs);
            run(full, lf, rf);
            double d = 0;
            for (size_t i = 0; i < x.size(); ++i) d = std::max(d, (double)std::abs(ls[i] - lf[i]));
            CHECK(d > 0.05, "T32 full mix differs from soloed (no-solo restores others, diff %.2f)", d);
        }
    }

    // ---- T33: POST block runs at the end of the section ----
    {
        const int N = (int)SR * 2;
        auto x = tone(700.0, 0.5, N);

        // (a) post-only (all front slots off) == a standalone voice of that effect.
        {
            ModBlock m;
            m.setLevelLock(false);
            for (int s = 0; s < ModBlock::kSlots; ++s) m.setSlotBypassed(s, true);
            m.setPostType(ModVoice::kTremolo);
            m.setPostRateHz(4.0f); m.setPostDepth(0.6f); m.setPostMix(1.0f);
            m.setPostBypassed(false);
            m.prepare({SR, BLK});
            ModVoice ref;
            ref.setType(ModVoice::kTremolo);
            ref.setRateHz(4.0f); ref.setDepth(0.6f); ref.setMix(1.0f);
            ref.prepare({SR, BLK});
            auto la = x, ra = x, lb = x, rb = x;
            run(m, la, ra);
            run(ref, lb, rb);
            double d = 0;
            for (size_t i = 0; i < x.size(); ++i) d = std::max(d, (double)std::abs(la[i] - lb[i]));
            CHECK(d < 1e-6, "T33 post-only == standalone voice (diff %.2e)", d);
        }

        // (b) post runs AFTER the front section: series chorus -> post tremolo ==
        // the same two voices chained by hand.
        {
            ModBlock m;
            m.setLevelLock(false); // series (default)
            m.setType(0, ModVoice::kChorus); m.setRateHz(0, 1.0f); m.setDepth(0, 0.7f); m.setMix(0, 1.0f);
            m.setSlotBypassed(0, false);
            m.setSlotBypassed(1, true); m.setSlotBypassed(2, true);
            m.setPostType(ModVoice::kTremolo);
            m.setPostRateHz(4.0f); m.setPostDepth(0.6f); m.setPostMix(1.0f);
            m.setPostBypassed(false);
            m.prepare({SR, BLK});

            ModVoice front, post;
            front.setType(ModVoice::kChorus); front.setRateHz(1.0f); front.setDepth(0.7f); front.setMix(1.0f);
            post.setType(ModVoice::kTremolo); post.setRateHz(4.0f); post.setDepth(0.6f); post.setMix(1.0f);
            front.prepare({SR, BLK});
            post.prepare({SR, BLK});

            auto la = x, ra = x, lb = x, rb = x;
            run(m, la, ra);
            run(front, lb, rb); // chorus
            run(post, lb, rb);  // then tremolo on the chorus output
            double d = 0;
            for (size_t i = 0; i < x.size(); ++i) d = std::max(d, (double)std::abs(la[i] - lb[i]));
            CHECK(d < 1e-6, "T33 post runs after the front section (== hand-chained, diff %.2e)", d);
        }

        // (c) post bypass works: enabling the post effect changes the output.
        {
            auto runPost = [&](bool postOn) {
                ModBlock m;
                m.setLevelLock(false);
                m.setType(0, ModVoice::kChorus); m.setRateHz(0, 1.0f); m.setDepth(0, 0.7f); m.setMix(0, 1.0f);
                m.setSlotBypassed(0, false); m.setSlotBypassed(1, true); m.setSlotBypassed(2, true);
                m.setPostType(ModVoice::kTremolo); m.setPostRateHz(4.0f); m.setPostDepth(0.8f); m.setPostMix(1.0f);
                m.setPostBypassed(!postOn);
                m.prepare({SR, BLK});
                auto l = x, r = x;
                run(m, l, r);
                return l;
            };
            auto off = runPost(false), on = runPost(true);
            double d = 0;
            for (size_t i = 0; i < x.size(); ++i) d = std::max(d, (double)std::abs(on[i] - off[i]));
            CHECK(d > 0.05, "T33 post bypass works (post changes the output, diff %.2f)", d);
        }

        // (d) solo mutes the post effect: soloing a front slot with the post on
        // yields ONLY that slot (== standalone), the post is silenced.
        {
            ModBlock m;
            m.setLevelLock(false);
            m.setType(0, ModVoice::kChorus); m.setRateHz(0, 1.0f); m.setDepth(0, 0.7f); m.setMix(0, 1.0f);
            m.setSlotBypassed(0, false); m.setSlotBypassed(1, true); m.setSlotBypassed(2, true);
            m.setPostType(ModVoice::kTremolo); m.setPostRateHz(4.0f); m.setPostDepth(0.8f); m.setPostMix(1.0f);
            m.setPostBypassed(false);
            m.setSlotSolo(0, true); // solo front slot 0 -> post should mute
            m.prepare({SR, BLK});
            ModVoice ref;
            ref.setType(ModVoice::kChorus);
            ref.setRateHz(1.0f); ref.setDepth(0.7f); ref.setMix(1.0f);
            ref.prepare({SR, BLK});
            auto la = x, ra = x, lb = x, rb = x;
            run(m, la, ra);
            run(ref, lb, rb);
            double d = 0;
            for (size_t i = 0; i < x.size(); ++i) d = std::max(d, (double)std::abs(la[i] - lb[i]));
            CHECK(d < 1e-6, "T33 solo mutes the post effect (== soloed slot alone, diff %.2e)", d);
        }
    }

    // ---- T34: SERIES chain order (reorderable processing sequence) ----
    {
        const int N = (int)SR * 2;
        auto x = tone(700.0, 0.5, N);

        // Two slots with DISTINCT, order-sensitive effects: a flanger (feedback
        // comb) and a chorus, both enabled. cfg sets everything but the order.
        auto cfg = [&](ModBlock &m) {
            m.setLevelLock(false); // bit-exact series comparisons
            m.setType(0, ModVoice::kFlanger); m.setRateHz(0, 0.5f); m.setDepth(0, 0.8f);
            m.setFeedback(0, 0.7f); m.setMix(0, 0.5f); m.setManual(0, 0.4f);
            m.setType(1, ModVoice::kChorus); m.setRateHz(1, 1.3f); m.setDepth(1, 0.7f); m.setMix(1, 0.5f);
            m.setSlotBypassed(0, false); m.setSlotBypassed(1, false); m.setSlotBypassed(2, true);
        };

        // (a) default order {0,1,2} is bit-exact to a section that never touched
        // the order (the old fixed-order series loop).
        {
            ModBlock def, plain;
            cfg(def); def.setChainOrder(0, 1, 2); def.prepare({SR, BLK});
            cfg(plain); plain.prepare({SR, BLK}); // chain order never set -> identity
            auto la = x, ra = x, lb = x, rb = x;
            run(def, la, ra);
            run(plain, lb, rb);
            double d = 0;
            for (size_t i = 0; i < x.size(); ++i) d = std::max(d, (double)std::abs(la[i] - lb[i]));
            CHECK(d < 1e-6, "T34 default chain order == fixed-order series (diff %.2e)", d);
        }

        // (b) swapping the two effects' order changes the output (order matters).
        {
            ModBlock fwd, rev;
            cfg(fwd); fwd.setChainOrder(0, 1, 2); fwd.prepare({SR, BLK}); // flanger -> chorus
            cfg(rev); rev.setChainOrder(1, 0, 2); rev.prepare({SR, BLK}); // chorus -> flanger
            auto la = x, ra = x, lb = x, rb = x;
            run(fwd, la, ra);
            run(rev, lb, rb);
            double d = 0;
            for (size_t i = 0; i < x.size(); ++i) d = std::max(d, (double)std::abs(la[i] - lb[i]));
            CHECK(d > 0.02, "T34 reordering the chain changes the sound (diff %.2f)", d);
        }

        // (c) a malformed order (duplicate / out-of-range slot) is rejected and the
        // identity is kept, so no slot is ever dropped or doubled.
        {
            ModBlock bad;
            cfg(bad); bad.setChainOrder(0, 1, 2); bad.prepare({SR, BLK});
            bad.setChainOrder(0, 0, 1); // duplicate -> ignored
            bad.setChainOrder(3, 1, 2); // out of range -> ignored
            const bool ok = (bad.chainOrder(0) == 0 && bad.chainOrder(1) == 1 && bad.chainOrder(2) == 2);
            CHECK(ok, "T34 malformed chain order rejected (keeps a valid permutation)");
        }
    }

    // ---- T35: per-shape tremolo Width. Square/S&H are MONO at Width 0 (L==R) and
    //      amplitude AUTO-PAN at Width 1 (L!=R, in-phase base -> no quadrature
    //      lurch); sine spreads via the quadrature offset (also covered by T6) ----
    {
        auto lrDiff = [](int wave, float width) {
            ModVoice m;
            m.setType(ModVoice::kTremolo);
            m.setWaveform(wave);
            m.setRateHz(3.0f);
            m.setDepth(0.9f);
            m.setMix(1.0f);
            m.setWidth(width);
            m.prepare({SR, BLK});
            auto l = tone(1000.0, 0.5, (int)SR), r = l;
            run(m, l, r);
            double d = 0;
            for (size_t i = 0; i < l.size(); ++i) d = std::max(d, (double)std::abs(l[i] - r[i]));
            return d;
        };
        const double sq0 = lrDiff(Lfo::Square, 0.0f), sq1 = lrDiff(Lfo::Square, 1.0f);
        const double sh0 = lrDiff(Lfo::SampleHold, 0.0f), sh1 = lrDiff(Lfo::SampleHold, 1.0f);
        const double si1 = lrDiff(Lfo::Sine, 1.0f);
        CHECK(sq0 < 1e-6, "T35 Square Width 0 = mono (L==R, diff %.2e)", sq0);
        CHECK(sq1 > 0.05, "T35 Square Width 1 = auto-pan (L!=R, diff %.3f)", sq1);
        CHECK(sh0 < 1e-6, "T35 S&H Width 0 = mono (L==R, diff %.2e)", sh0);
        CHECK(sh1 > 0.02, "T35 S&H Width 1 = auto-pan (L!=R, diff %.3f)", sh1);
        CHECK(si1 > 0.02, "T35 sine tremolo still spreads L/R (diff %.3f)", si1);
    }

    // ---- T36: Chorus + Vibrato use the 6-point 5th-order Lagrange read. A 6-point
    //      Lagrange reproduces polynomials up to degree 5 EXACTLY, so it nails a
    //      cubic; the 4-point cubic-Hermite readFrac (3rd-order, exact only to
    //      quadratics) does not. Drive FracDelayLine (rig/Lfo.h) with a cubic and
    //      read at many fractional delays -> readFrac6 ~exact, readFrac visibly off.
    //      (This is the interpolation upgrade behind chorus/vibrato.) ----
    {
        nam_rig::FracDelayLine dl;
        dl.prepare(64);
        auto q = [](double n) { return 0.01 * n * n * n; }; // a genuine cubic
        const int N = 20;
        for (int n = 0; n < N; ++n) dl.write((float)q(n));
        double err6 = 0.0, errH = 0.0;
        for (double D = 3.2; D <= 6.0; D += 0.1)
        {
            const double npos = (double)(N - 1) - D; // absolute position read
            err6 = std::max(err6, std::abs((double)dl.readFrac6(D) - q(npos)));
            errH = std::max(errH, std::abs((double)dl.readFrac(D) - q(npos)));
        }
        CHECK(err6 < 1.0e-4, "T36 6-point Lagrange reproduces a cubic (max err %.2e < 1e-4)", err6);
        CHECK(errH > 5.0e-4, "T36 ...where the 4-point cubic-Hermite read cannot (err %.2e)", errH);
    }

    // ---- T37: harm-trem LR4 recombines magnitude-FLAT at unity AM. At depth 0
    //      both band gains are 1, so wet = LP+HP = a Linkwitz-Riley allpass sum;
    //      its magnitude is unity at every frequency (no colouring of the dry
    //      tone), including right at the crossover. ----
    {
        auto ratioAt = [](double f) {
            ModVoice m;
            m.setType(ModVoice::kHarmTrem);
            m.setRateHz(3.0f);
            m.setDepth(0.0f);
            m.setMix(1.0f);
            m.prepare({SR, BLK});
            auto l = tone(f, 0.4, (int)SR), r = l;
            auto ref = l;
            run(m, l, r);
            const double out = mag(l, (size_t)SR / 2, (size_t)SR / 2, f);
            const double in = mag(ref, (size_t)SR / 2, (size_t)SR / 2, f);
            return out / (in + 1e-12);
        };
        double worst = 0.0;
        bool finite = true;
        for (double f : {100.0, 400.0, 800.0, 1600.0, 4000.0})
        {
            const double rr = ratioAt(f);
            if (!std::isfinite(rr)) finite = false;
            worst = std::max(worst, std::abs(rr - 1.0));
        }
        CHECK(finite && worst < 0.06,
              "T37 LR4 recombines magnitude-flat at unity AM (worst dev %.3f < 0.06)", worst);
    }

    // ---- T38: harm-trem LR4 band separation is STEEP (24 dB/oct). A tone two
    //      octaves above the crossover lands ~48 dB down in the low band, so at
    //      full depth the complementary AM modulates it almost completely
    //      (min/max ~0.004). The old one-pole split (6 dB/oct) only reached ~0.25
    //      here, so this threshold is what proves the LR4 rework. ----
    {
        ModVoice m;
        m.setType(ModVoice::kHarmTrem);
        m.setRateHz(1.0f);
        m.setDepth(1.0f);
        m.setMix(1.0f);
        m.prepare({SR, BLK});
        auto l = tone(3200.0, 0.4, (int)SR * 3), r = l; // 3200 = 800 * 4 (2 octaves up)
        run(m, l, r);
        std::vector<float> tail(l.begin() + (int)(SR * 1.5), l.end());
        auto env = envelope(tail, 480);
        double mn = 1e9, mx = 0;
        for (double e : env) { mn = std::min(mn, e); mx = std::max(mx, e); }
        const double ratio = mn / (mx + 1e-12);
        CHECK(ratio < 0.1,
              "T38 LR4 steep separation: 2-octave tone modulates fully (min/max %.4f < 0.1; one-pole ~0.25)", ratio);
    }

    // ---- T39: vibrato 6-point-Lagrange interpolation is CLEAN under a fast/deep
    //      sweep. Because the read is FIR (no recursive state), it is a continuous
    //      function of the fractional delay -> no integer-crossing transients, so a
    //      hard sweep of a high tone keeps a near-constant peak amplitude. This is
    //      the SAME signal that the recursive all-pass rippled at ~0.224; the FIR
    //      read should land far lower. Also (a) flat magnitude at a steady delay. ----
    {
        // (a) fixed-delay flat magnitude: equal 250 + 1500 Hz tones, depth 0 (delay
        // constant), full wet. The ratio cancels the common BBD soft-clip gain,
        // leaving the Lagrange read + BBD HF rolloff -> ~flat (<5%).
        auto fixedMag = [](double f) {
            ModVoice m;
            m.setType(ModVoice::kVibrato);
            m.setRateHz(2.0f);
            m.setDepth(0.0f);
            m.setMix(1.0f);
            m.prepare({SR, BLK});
            auto l = tone(f, 0.3, (int)SR), r = l;
            auto ref = l;
            run(m, l, r);
            return mag(l, (size_t)SR / 2, (size_t)SR / 2, f)
                 / (mag(ref, (size_t)SR / 2, (size_t)SR / 2, f) + 1e-12);
        };
        const double rel = fixedMag(1500.0) / (fixedMag(250.0) + 1e-12);
        CHECK(std::abs(rel - 1.0) < 0.05,
              "T39a Lagrange flat magnitude at steady delay (1500/250 ratio %.3f ~ 1)", rel);

        // (b) fast/deep sweep of a 6 kHz tone: FIR read -> low amplitude ripple
        // (the all-pass gave 0.224 here; Lagrange has no integer-crossing snap).
        ModVoice m;
        m.setType(ModVoice::kVibrato);
        m.setRateHz(5.0f);
        m.setDepth(0.6f);
        m.setMix(1.0f);
        m.prepare({SR, BLK});
        auto l = tone(6000.0, 0.4, (int)SR * 2), r = l;
        run(m, l, r);
        bool finite = true;
        for (float v : l) if (!std::isfinite(v)) finite = false;
        // wide window (~32 cycles at 6 kHz) so peak detection is steady and the
        // ripple we measure is real amplitude variation, not sampling-phase noise.
        std::vector<float> tail(l.begin() + (int)SR, l.end());
        auto env = envelope(tail, 256);
        double mn = 1e9, mx = 0.0, mean = 0.0;
        for (double e : env) { mn = std::min(mn, e); mx = std::max(mx, e); mean += e; }
        mean /= (double)env.size();
        const double ripple = (mx - mn) / (mean + 1e-12);
        CHECK(finite && mean > 0.05, "T39b vibrato finite + non-silent (mean env %.3f)", mean);
        CHECK(ripple < 0.12, "T39b FIR sweep stays flat: ripple %.3f < 0.12 (all-pass was 0.224)", ripple);
    }

    // ---- T40: tremolo DC-offset compensation law. center 0 = cut-only (mean
    //      gain drops with depth, the original voicing); center 1 = mean-
    //      preserving (avg gain 1 at any depth); both reach 0 at the trough
    //      (full chop). The AM of a zero-mean signal must add no DC. ----
    {
        auto meanGain = [](float depth, float center) {
            const int N = 2001;
            double s = 0.0;
            for (int i = 0; i < N; ++i)
            {
                const float mod = -1.0f + 2.0f * (float)i / (float)(N - 1);
                s += (double)ModVoice::tremGain(depth, mod, center);
            }
            return s / (double)N;
        };
        const bool law =
            std::abs(meanGain(0.8f, 0.0f) - 0.6) < 0.01 &&      // cut-only: mean = 1 - 0.5*depth
            std::abs(meanGain(0.8f, 1.0f) - 1.0) < 0.01 &&      // DC-comp: mean = 1
            std::abs(meanGain(0.3f, 1.0f) - 1.0) < 0.01 &&      // ...independent of depth
            std::abs(ModVoice::tremGain(1.0f, 1.0f, 0.0f)) < 1e-6 && // full chop (cut-only)
            std::abs(ModVoice::tremGain(1.0f, 1.0f, 1.0f)) < 1e-6;   // full chop (DC-comp)
        CHECK(law, "T40 tremolo DC-comp law (cut-only mean drops with depth, DC-comp mean=1, both full-chop)");

        ModVoice m;
        m.setType(ModVoice::kTremolo);
        m.setRateHz(5.0f);
        m.setDepth(1.0f);
        m.setMix(1.0f);
        m.prepare({SR, BLK});
        auto l = tone(1000.0, 0.4, (int)SR), r = l;
        run(m, l, r);
        double dc = 0.0;
        for (size_t i = 4800; i < l.size(); ++i) dc += (double)l[i];
        dc /= (double)(l.size() - 4800);
        CHECK(std::abs(dc) < 1.0e-3, "T40 tremolo adds no DC to a zero-mean signal (DC %.2e)", dc);
    }

    // ---- T41: rate-dependent sweep-width guardrail. The width scale is full
    //      (1.0) at/below kSweepFullRateHz and narrows monotonically to
    //      kSweepNarrowFloor at the rate cap, so a fast phaser shimmers instead of
    //      wobbling. (a) the law; (b) the phaser stays finite/bounded when driven
    //      to its rate cap with the narrowed sweep. ----
    {
        const float mx = ModVoice::maxRateHz(ModVoice::kPhaser); // 10 Hz
        const bool law =
            ModVoice::sweepWidthScale(0.5f, mx) == 1.0f &&                          // slow = full width
            ModVoice::sweepWidthScale(ModVoice::kSweepFullRateHz, mx) == 1.0f &&    // knee = still full
            std::abs(ModVoice::sweepWidthScale(mx, mx) - ModVoice::kSweepNarrowFloor) < 1e-6f && // cap = floor
            ModVoice::sweepWidthScale(6.0f, mx) < 1.0f &&                           // mid = narrowed
            ModVoice::sweepWidthScale(6.0f, mx) > ModVoice::kSweepNarrowFloor;
        bool mono = true;
        float prev = 1.0f;
        for (float rr = ModVoice::kSweepFullRateHz; rr <= mx; rr += 0.5f)
        {
            const float w = ModVoice::sweepWidthScale(rr, mx);
            if (w > prev + 1e-6f) mono = false;
            prev = w;
        }
        CHECK(law && mono,
              "T41 sweep-width guardrail: full <=2 Hz, narrows monotonically to the floor at the cap");

        // per-effect floor: the narrow-sweep Uni-Vibe tames far less than the wide
        // phaser/bi-phase, so it stays lively when driven fast.
        const bool floors =
            ModVoice::sweepFloor(ModVoice::kUniVibe) > ModVoice::sweepFloor(ModVoice::kPhaser) &&
            ModVoice::sweepFloor(ModVoice::kBiPhase) == ModVoice::sweepFloor(ModVoice::kPhaser) &&
            ModVoice::sweepWidthScale(mx, mx, ModVoice::sweepFloor(ModVoice::kUniVibe))
                > ModVoice::sweepWidthScale(mx, mx, ModVoice::sweepFloor(ModVoice::kPhaser));
        CHECK(floors, "T41 Uni-Vibe keeps a higher sweep-width floor than phaser/bi-phase");

        // (b) phaser at the rate cap (with the narrowed sweep) stays well-behaved
        ModVoice m;
        m.setType(ModVoice::kPhaser);
        m.setRateHz(20.0f); // capped to 10 Hz; sweep width at the floor
        m.setFeedback(0.5f);
        m.setMix(0.5f);
        m.prepare({SR, BLK});
        auto l = tone(1000.0, 0.3, (int)SR), r = l;
        run(m, l, r);
        bool fin = true;
        double pk = 0.0, energy = 0.0;
        for (float v : l) { if (!std::isfinite(v)) fin = false; pk = std::max(pk, (double)std::abs(v)); energy += (double)v * v; }
        CHECK(fin && pk < 4.0 && energy > 0.0, "T41 phaser at the rate cap stays finite + bounded (peak %.2f)", pk);
    }

    // ---- T42: Extreme switch reassigns controls to their WILD range (Normal
    //      untouched). At the SAME feedback knob, Extreme maps into the high-
    //      resonance band so the phaser/bi-phase ring markedly harder; and the
    //      Bi-Phase Sweep-2 ratio is squared, changing the motion. Frozen sweep +
    //      impulse tail energy isolates the resonance. ----
    {
        auto tail = [](int type, bool extreme) {
            ModVoice m;
            m.setType(type);
            m.setRateHz(0.0f);   // frozen sweep -> stationary resonance
            m.setDepth(0.7f);
            m.setFeedback(0.5f); // SAME knob in both modes
            m.setMix(0.5f);
            m.setSeries(false);
            m.setExtreme(extreme);
            m.prepare({SR, BLK});
            std::vector<float> l((size_t)SR / 4, 0.0f);
            l[0] = 1.0f; // impulse
            auto r = l;
            run(m, l, r);
            double e = 0.0;
            for (size_t i = 400; i < l.size(); ++i) e += (double)l[i] * (double)l[i];
            return e;
        };
        const double pn = tail(ModVoice::kPhaser, false), pe = tail(ModVoice::kPhaser, true);
        const double bn = tail(ModVoice::kBiPhase, false), be = tail(ModVoice::kBiPhase, true);
        CHECK(std::isfinite(pe) && pe > pn * 1.5,
              "T42 phaser Extreme rings harder at the same knob (tail %.2e vs Normal %.2e)", pe, pn);
        CHECK(std::isfinite(be) && be > bn * 1.3,
              "T42 bi-phase Extreme rings harder at the same knob (tail %.2e vs Normal %.2e)", be, bn);

        // Bi-Phase Sweep-2 ratio reassigned to the wild strong-detune band in
        // Extreme -> motion differs at the same ratio knob (feedback left at 0 so
        // this isolates the ratio change).
        auto biRun = [](bool extreme) {
            ModVoice m;
            m.setType(ModVoice::kBiPhase);
            m.setRateHz(2.0f);
            m.setDepth(0.7f);
            m.setP2Ratio(1.5f);
            m.setSeries(false);
            m.setWidth(0.0f);
            m.setExtreme(extreme);
            m.prepare({SR, BLK});
            auto l = tone(1000.0, 0.5, (int)SR), r = l;
            run(m, l, r);
            return l;
        };
        auto rn = biRun(false), re = biRun(true);
        double d = 0.0;
        for (size_t i = 4800; i < rn.size(); ++i) d = std::max(d, (double)std::abs(rn[i] - re[i]));
        CHECK(d > 0.02, "T42 bi-phase Extreme reassigns Sweep-2 ratio to strong detune (motion differs, max diff %.3f)", d);
    }

    // ---- T43: Chorus Extreme. (a) The rate knob remaps into the DISJOINT fast
    //      band [3.5,10] Hz only: at the knob top Extreme reaches ~10 Hz vs
    //      Normal's 3.5 cap, and even at the knob bottom Extreme is still >=3.5 Hz
    //      (always fast -- never overlaps Normal's slow range, matching the
    //      phaser/bi-phase feedback reassignment idiom). (b) The sweep excursion
    //      widens by kExtremeSweepWiden: at a MATCHED 3.5 Hz rate + same depth, an
    //      impulse's latest wet tap lands later in Extreme (deeper pitch sweep).
    //      (c) Stays finite + bounded. Normal path is bit-identical (T1-T42). ----
    {
        // (a) rate-remap law -- deterministic, no audio needed.
        auto rateOf = [](float knob, bool extreme) {
            ModVoice m;
            m.setType(ModVoice::kChorus);
            m.setRateHz(knob);
            m.setExtreme(extreme);
            return m.effectiveRateHz();
        };
        const float rnTop = rateOf(3.5f, false); // Normal: capped at 3.5
        const float reTop = rateOf(3.5f, true);   // Extreme top -> ~10
        const float reLow = rateOf(0.0f, true);   // Extreme bottom -> floor of the band
        CHECK(reTop > rnTop + 0.5f && std::abs(reTop - 10.0f) < 0.01f,
              "T43 chorus Extreme lifts the rate to the fast band (top %.2f Hz vs Normal %.2f Hz)", reTop, rnTop);
        CHECK(reLow >= 3.5f - 1e-3f,
              "T43 chorus Extreme rate band is disjoint (bottom %.2f Hz >= Normal cap 3.50 Hz)", reLow);

        // (b) depth-widen -- matched 3.5 Hz so only the excursion differs. Full wet
        //     (Mix=1) so the impulse response is purely the 3 swept taps.
        auto lastTap = [](bool extreme, double &peakOut) {
            ModVoice m;
            m.setType(ModVoice::kChorus);
            m.setRateHz(extreme ? 0.0f : 3.5f); // both -> 3.5 Hz effective (Extreme floor == Normal cap)
            m.setDepth(0.9f);
            m.setMix(1.0f);
            m.setExtreme(extreme);
            m.prepare({SR, BLK});
            std::vector<float> l((size_t)(SR * 0.06), 0.0f);
            l[0] = 1.0f; // impulse
            auto r = l;
            run(m, l, r);
            double peak = 0.0;
            for (float v : l) peak = std::max(peak, (double)std::abs(v));
            peakOut = peak;
            int last = 0;
            for (size_t i = 0; i < l.size(); ++i)
                if ((double)std::abs(l[i]) > 0.15 * peak) last = (int)i;
            return last;
        };
        double pkN = 0.0, pkE = 0.0;
        const int nLast = lastTap(false, pkN), eLast = lastTap(true, pkE);
        CHECK(eLast > nLast + 30,
              "T43 chorus Extreme deepens the sweep (latest tap %d vs Normal %d samples)", eLast, nLast);
        CHECK(std::isfinite(pkE) && pkE > 0.0 && pkE < 2.0,
              "T43 chorus Extreme stays finite + bounded (peak %.3f)", pkE);
    }

    // ---- T44: Vibrato Extreme. (a) Rate knob remaps into the DISJOINT fast band
    //      [10,16] Hz only -- Extreme top ~16 Hz vs Normal's 10 cap, floor >=10 Hz
    //      (always fast). (b) The single-tap pitch sweep deepens by kExtremeSweepWiden:
    //      at a MATCHED 10 Hz rate + same depth, an impulse's wet tap (vibrato is
    //      full wet, single tap) peaks later in Extreme. (c) Stays finite + bounded.
    //      Normal path is bit-identical (T1-T43). ----
    {
        // (a) rate-remap law.
        auto rateOf = [](float knob, bool extreme) {
            ModVoice m;
            m.setType(ModVoice::kVibrato);
            m.setRateHz(knob);
            m.setExtreme(extreme);
            return m.effectiveRateHz();
        };
        const float rnTop = rateOf(10.0f, false); // Normal: capped at 10
        const float reTop = rateOf(10.0f, true);   // Extreme top -> ~16
        const float reLow = rateOf(0.0f, true);    // Extreme bottom -> band floor
        CHECK(reTop > rnTop + 0.5f && std::abs(reTop - 16.0f) < 0.01f,
              "T44 vibrato Extreme lifts the rate to the fast band (top %.2f Hz vs Normal %.2f Hz)", reTop, rnTop);
        CHECK(reLow >= 10.0f - 1e-3f,
              "T44 vibrato Extreme rate band is disjoint (bottom %.2f Hz >= Normal cap 10.00 Hz)", reLow);

        // (b) depth-widen -- matched 10 Hz (Extreme knob 0 floor == Normal cap) so
        //     only the excursion differs. Vibrato is full wet + single tap -> the
        //     impulse response is one swept echo; its peak position is the tap delay.
        auto peakAt = [](bool extreme, double &peakOut) {
            ModVoice m;
            m.setType(ModVoice::kVibrato);
            m.setRateHz(extreme ? 0.0f : 10.0f); // both -> 10 Hz effective
            m.setDepth(0.9f);
            m.setExtreme(extreme);
            m.prepare({SR, BLK});
            std::vector<float> l((size_t)(SR * 0.04), 0.0f);
            l[0] = 1.0f; // impulse
            auto r = l;
            run(m, l, r);
            double peak = 0.0;
            int at = 0;
            for (size_t i = 0; i < l.size(); ++i)
                if ((double)std::abs(l[i]) > peak) { peak = (double)std::abs(l[i]); at = (int)i; }
            peakOut = peak;
            return at;
        };
        double pkN = 0.0, pkE = 0.0;
        const int nAt = peakAt(false, pkN), eAt = peakAt(true, pkE);
        CHECK(eAt > nAt + 20,
              "T44 vibrato Extreme deepens the sweep (tap peak %d vs Normal %d samples)", eAt, nAt);
        CHECK(std::isfinite(pkE) && pkE > 0.0 && pkE < 2.0,
              "T44 vibrato Extreme stays finite + bounded (peak %.3f)", pkE);
    }

    // ---- T45: Flanger Extreme = TRUE THROUGH-ZERO. (a) Tap-delay law via the
    //      public tzfTaps(): at lfo=0 the swept tap meets the reference (through-zero
    //      point); at lfo=+/-1 it sits symmetric about the reference and the comb
    //      difference INVERTS sign; both taps stay in [guard, kFlMaxMs]. (b) In
    //      cancel mode (Invert ON) the two taps subtract, so as the sweep passes
    //      through 0 ms the whole signal nulls broadband -- a deep envelope dip the
    //      Normal flanger (a moving spectral notch) cannot produce. (c) Finite +
    //      bounded with heavy feedback. Normal path bit-identical (T1-T44).
    //      [thresholds first-run estimates -- sandbox down, not yet run on hardware] ----
    {
        // (a) tap-delay law -- deterministic, exact.
        bool tzAtZero = true, tzBounds = true;
        for (float mz : {0.0f, 0.5f, 1.0f})
        {
            const auto z = ModVoice::tzfTaps(mz, 1.0f, 0.0f);
            if (std::abs(z.sweptMs - z.refMs) > 1e-9) tzAtZero = false; // lfo=0 -> taps coincide
            for (float lf : {-1.0f, 1.0f})
            {
                const auto t = ModVoice::tzfTaps(mz, 1.0f, lf);
                if (t.sweptMs < 0.2 - 1e-6 || t.sweptMs > 14.0 + 1e-6) tzBounds = false; // [guard, kFlMaxMs]
                if (t.refMs < 0.5 - 1e-6 || t.refMs > 8.0 + 1e-6) tzBounds = false;        // [kFlMin, kFlManualMax]
            }
        }
        const auto tp = ModVoice::tzfTaps(0.5f, 1.0f, 1.0f);
        const auto tm = ModVoice::tzfTaps(0.5f, 1.0f, -1.0f);
        const double dp = tp.sweptMs - tp.refMs, dm = tm.sweptMs - tm.refMs;
        CHECK(tzAtZero, "T45 TZF through-zero: swept tap meets the reference at lfo=0");
        CHECK(dp > 0.0 && dm < 0.0 && std::abs(dp + dm) < 1e-9,
              "T45 TZF comb difference inverts sign + is symmetric about zero (+%.3f / %.3f ms)", dp, dm);
        CHECK(tzBounds, "T45 TZF both taps stay readable + within the line across Manual/sweep");

        // The defining TZF property is BROADBAND-SIMULTANEOUS cancellation: at
        // delta=0 the two taps are identical, so the WHOLE signal nulls at one
        // instant. White noise can't show this (the taps decorrelate within a sample
        // -> ref-swp never nulls except sub-sample). Two well-separated tones do: in
        // cancel mode both vanish together at delta=0; the Normal flanger (a single
        // moving notch, plus its 0.5 dry floor) can never silence both at once.
        auto twoTone = [](int n) {
            auto a = tone(250.0, 0.4, n);
            auto b = tone(1800.0, 0.4, n);
            for (size_t i = 0; i < a.size(); ++i) a[i] += b[i];
            return a;
        };
        // min/mean of the short-time RMS envelope -> how deep the deepest dip is
        // relative to the average level. A broadband null drives this toward 0.
        auto nullRatio = [](std::vector<float> &l) {
            const int W = 256;
            double sumMean = 0.0, minR = 1e30; int nb = 0;
            for (size_t b = 0; b + (size_t)W <= l.size(); b += (size_t)W, ++nb)
            {
                double e = 0.0;
                for (int i = 0; i < W; ++i) e += (double)l[b + (size_t)i] * (double)l[b + (size_t)i];
                const double rms = std::sqrt(e / W);
                sumMean += rms;
                if (nb >= 4 && rms < minR) minR = rms; // skip warmup blocks
            }
            const double mean = sumMean / std::max(1, nb);
            return mean > 0.0 ? minR / mean : 1.0;
        };
        auto runFlanger = [&](bool extreme, bool invert) {
            ModVoice m;
            m.setType(ModVoice::kFlanger);
            m.setRateHz(0.5f);   // slow -> a window sits inside the through-zero null
            m.setDepth(1.0f);
            m.setManual(0.5f);   // mid Manual -> a wide symmetric sweep through zero
            m.setInvert(invert);
            m.setFeedback(0.0f); // isolate the null (no regen)
            m.setMix(1.0f);      // Extreme: full two-tape pair; Normal: caps to 0.5 internally
            m.setExtreme(extreme);
            m.prepare({SR, BLK});
            auto l = twoTone((int)(SR * 4.0)); auto r = l; // 4 s -> 4 zero crossings
            run(m, l, r);
            return l;
        };
        auto extL = runFlanger(true, true);   // through-zero cancel: both tones null together
        auto norL = runFlanger(false, true);  // Normal M-126: never silences both
        const double extNull = nullRatio(extL), norNull = nullRatio(norL);
        CHECK(extNull < 0.4 && extNull < norNull * 0.6,
              "T45 TZF cancel mode nulls both tones together (dip ratio %.3f vs Normal %.3f)", extNull, norNull);

        // (c) bounded with heavy feedback.
        ModVoice mb;
        mb.setType(ModVoice::kFlanger);
        mb.setRateHz(2.0f);
        mb.setDepth(0.8f);
        mb.setManual(0.5f);
        mb.setInvert(true);
        mb.setFeedback(0.9f);
        mb.setMix(0.5f);
        mb.setExtreme(true);
        mb.prepare({SR, BLK});
        auto bl = tone(440.0, 0.7, (int)(SR / 2)); auto br = bl;
        run(mb, bl, br);
        double pk = 0.0; bool fin = true;
        for (float v : bl) { pk = std::max(pk, (double)std::abs(v)); if (!std::isfinite(v)) fin = false; }
        CHECK(fin && pk < 4.0, "T45 TZF stays finite + bounded with heavy feedback (peak %.2f)", pk);
    }

    std::printf("\n%s (%d FAIL)\n", gFails == 0 ? "ALL PASS" : "FAILURES", gFails);
    return gFails == 0 ? 0 : 1;
}
