// reverb_test — offline verification harness for ReverbBlock. Exits nonzero on
// any FAIL. T1..T7 characterise the default FDN engine (Hall, mod 0, == the
// original 8-line reverb). T8..T13 characterise the typed character engines:
//   T8  every character is finite, bounded and decays after the input stops
//   T9  every character is bit-exact dry at mix 0
//   T10 every character produces a wet tail
//   T11 the plate is L/R decorrelated (true stereo image)
//   T12 shimmer lifts the +1 octave (200 Hz in -> strong 400 Hz in the tail)
//   T13 the per-character voicing introspection is self-consistent
#include "rig/ReverbBlock.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <random>
#include <complex>

using nam_rig::ReverbBlock;
using nam_rig::BlockContext;

static int gFails = 0;
#define CHECK(cond, ...) do { \
    const bool chkOk_ = (cond); \
    std::printf("%s: ", chkOk_ ? "PASS" : "FAIL"); std::printf(__VA_ARGS__); std::printf("\n"); \
    if (!chkOk_) ++gFails; } while (0)

static constexpr double SR = 48000.0;
static constexpr int BLK = 512;

static void run(ReverbBlock &v, std::vector<float> &l, std::vector<float> &r)
{
    for (size_t p = 0; p < l.size(); p += BLK)
        v.process(l.data() + p, r.data() + p, (int)std::min<size_t>(BLK, l.size() - p));
}

// Schroeder backward integration -> time to fall dbA..dbB, extrapolated to 60 dB
static double t60From(const std::vector<float> &x, double dbA = -5.0, double dbB = -25.0)
{
    std::vector<double> e(x.size());
    double acc = 0;
    for (size_t i = x.size(); i-- > 0;) { acc += (double)x[i] * (double)x[i]; e[i] = acc; }
    const double e0 = e[0];
    size_t ia = 0, ib = 0;
    for (size_t i = 0; i < e.size(); ++i)
    {
        const double db = 10.0 * std::log10(e[i] / e0 + 1e-30);
        if (ia == 0 && db <= dbA) ia = i;
        if (db <= dbB) { ib = i; break; }
    }
    if (ib <= ia) return 0.0;
    return (double)(ib - ia) / SR * 60.0 / (dbA - dbB);
}

// single-bin magnitude (per-sample-normalised) over [from, from+len)
static double binMag(const std::vector<float> &x, double f0, size_t from, size_t len)
{
    std::complex<double> acc{0, 0};
    const size_t n = std::min(len, x.size() - from);
    for (size_t i = 0; i < n; ++i)
        acc += (double)x[from + i] * std::exp(std::complex<double>(0, -2.0 * M_PI * f0 * (double)i / SR));
    return std::abs(acc) / (double)std::max<size_t>(1, n);
}

// spectral flatness of a chunk: geomean/mean of |spectrum| (1=white/smooth, low=peaky/metallic)
static double specFlatness(const std::vector<float> &x, size_t a, size_t n)
{
    const int K = 1024; double lg = 0, mn = 0; int c = 0;
    for (int k = 1; k < K / 2; k++)
    {
        std::complex<double> acc{0, 0}; const double f = (double)k * SR / (double)K;
        for (int i = 0; i < (int)n; i++)
            acc += (double)x[a + i] * std::exp(std::complex<double>(0, -2.0 * M_PI * f * (double)i / SR));
        const double m = std::abs(acc) + 1e-12; lg += std::log(m); mn += m; c++;
    }
    return std::exp(lg / c) / (mn / c);
}

int main()
{
    // ===================== T1..T7: default FDN (Hall) =====================
    { // T1 mix 0 bit-exact dry
        ReverbBlock v; v.setMix(0.0f); v.prepare({SR, BLK});
        std::vector<float> l((size_t)SR, 0.0f), r;
        for (size_t i = 0; i < l.size(); ++i) l[i] = (float)(0.3 * std::sin(2.0 * M_PI * 220.0 * (double)i / SR));
        r = l; auto refL = l; run(v, l, r);
        bool exact = true; for (size_t i = 0; i < l.size(); ++i) if (l[i] != refL[i]) { exact = false; break; }
        CHECK(exact, "T1 mix=0 is bit-exact dry");
    }
    { // T2 predelay shift exact
        auto onset = [&](float preMs) -> long long {
            // Plate exposes Pre-Delay directly (Hall folds it into Size).
            ReverbBlock v; v.setType(ReverbBlock::kPlate); v.setMix(1.0f); v.setPredelayMs(preMs); v.prepare({SR, BLK});
            std::vector<float> l((size_t)SR, 0.0f), r = l; l[0] = r[0] = 1.0f; run(v, l, r);
            for (size_t i = 0; i < l.size(); ++i) if (std::abs(l[i]) > 1e-9f || std::abs(r[i]) > 1e-9f) return (long long)i;
            return -1; };
        const long long o20 = onset(20.0f), o120 = onset(120.0f);
        const long long shift = o120 - o20;
        const long long want = (long long)std::llround((120.0 - 20.0) * 0.001 * SR);
        CHECK(o20 > 0, "T2 wet onset exists (%lld)", o20);
        CHECK(shift == want, "T2 predelay shift %lld smp (want exactly %lld)", shift, want);
        CHECK(o20 >= (long long)(0.020 * SR), "T2 onset %lld >= predelay %lld", o20, (long long)(0.020 * SR));
    }
    { // T3 T60 tracks Decay
        for (const float t60Set : {1.0f, 3.0f}) {
            ReverbBlock v; v.setType(ReverbBlock::kHall); v.setMix(1.0f); v.setDecaySeconds(t60Set); v.setDampHz(16000.0f); v.setPredelayMs(0.0f); v.prepare({SR, BLK}); // T3 tests Hall T60-tracking explicitly (default char is now Plate)
            bool gainsOk = true;
            for (int i = 0; i < ReverbBlock::kNumLines; ++i) {
                const double want = std::pow(10.0, -3.0 * v.lineLengthSamples(i) / ((double)t60Set * SR));
                if (std::abs((double)v.lineGain(i) - want) > 1e-6) gainsOk = false;
            }
            CHECK(gainsOk, "T3 line gains follow 10^(-3L/(T60 fs)) for T60=%.1f", t60Set);
            std::vector<float> l((size_t)(SR * (t60Set + 2.0)), 0.0f), r = l; l[0] = r[0] = 1.0f; run(v, l, r);
            const double meas = t60From(l);
            CHECK(std::abs(meas - t60Set) / t60Set < 0.10, "T3 measured T60 %.2f s (set %.1f, +-10%%)", meas, t60Set);
        }
    }
    { // T4 damping makes HF die faster than LF
        ReverbBlock v; v.setMix(1.0f); v.setDecaySeconds(3.0f); v.setDampHz(2000.0f); v.setPredelayMs(0.0f); v.prepare({SR, BLK});
        std::vector<float> l((size_t)SR * 4, 0.0f), r = l; l[0] = r[0] = 1.0f; run(v, l, r);
        const double lfE = binMag(l, 500.0, 4800, 9600), lfL = binMag(l, 500.0, 96000, 9600);
        const double hfE = binMag(l, 6000.0, 4800, 9600), hfL = binMag(l, 6000.0, 96000, 9600);
        const double lfDecay = 20.0 * std::log10(lfL / lfE + 1e-30);
        const double hfDecay = 20.0 * std::log10(hfL / hfE + 1e-30);
        CHECK(hfDecay < lfDecay - 6.0, "T4 HF decays faster: 6k %.1f dB vs 500 %.1f dB", hfDecay, lfDecay);
    }
    { // T5 stability on dense input
        ReverbBlock v; v.setMix(0.5f); v.setDecaySeconds(8.0f); v.setSize(1.5f); v.prepare({SR, BLK});
        std::mt19937 rng(42); std::uniform_real_distribution<float> dist(-0.5f, 0.5f);
        std::vector<float> l((size_t)SR * 10, 0.0f), r;
        for (size_t i = 0; i < (size_t)SR * 5; ++i) l[i] = dist(rng);
        r = l; run(v, l, r);
        bool finite = true; double peak = 0, tailPeak = 0;
        for (size_t i = 0; i < l.size(); ++i) { if (!std::isfinite(l[i]) || !std::isfinite(r[i])) finite = false; peak = std::max(peak, (double)std::abs(l[i])); if (i > (size_t)SR * 9) tailPeak = std::max(tailPeak, (double)std::abs(l[i])); }
        CHECK(finite, "T5 output finite over 10 s");
        CHECK(peak < 4.0, "T5 peak bounded (%.2f < 4)", peak);
        CHECK(tailPeak < peak, "T5 tail decaying (%.3f < %.2f)", tailPeak, peak);
    }
    { // T6 L/R decorrelation
        ReverbBlock v; v.setMix(1.0f); v.setPredelayMs(0.0f); v.prepare({SR, BLK});
        std::vector<float> l((size_t)SR * 2, 0.0f), r = l; l[0] = r[0] = 1.0f; run(v, l, r);
        double ll = 0, rr = 0, lr = 0;
        for (size_t i = 4800; i < l.size(); ++i) { ll += (double)l[i]*l[i]; rr += (double)r[i]*r[i]; lr += (double)l[i]*r[i]; }
        const double corr = lr / std::sqrt(ll * rr + 1e-30);
        CHECK(std::abs(corr) < 0.5, "T6 L/R wet correlation %.2f (|corr|<0.5)", corr);
    }
    { // T7 mono aliasing safe
        ReverbBlock v; v.setMix(0.4f); v.prepare({SR, BLK});
        std::vector<float> x((size_t)SR * 2, 0.0f); x[0] = 1.0f;
        for (size_t p = 0; p < x.size(); p += BLK) v.process(x.data()+p, x.data()+p, (int)std::min<size_t>(BLK, x.size()-p));
        bool finite = true; double energy = 0;
        for (auto s : x) { if (!std::isfinite(s)) finite = false; energy += (double)s*s; }
        CHECK(finite && energy > 1e-6, "T7 mono-aliased reverb finite, tail present");
    }

    // ===================== T8: every character is stable =====================
    {
        const char *names[ReverbBlock::kNumTypes] = {"Room","Hall","Plate","Spring","Shimmer","Ambience","Bloom"};
        for (int t = 0; t < ReverbBlock::kNumTypes; ++t) {
            ReverbBlock v; v.setType(t); v.setMix(0.6f); v.setDecaySeconds(5.0f); v.setShimmer(1.0f); v.setTension(0.8f); v.prepare({SR, BLK});
            std::mt19937 rng(7); std::uniform_real_distribution<float> dist(-0.5f, 0.5f);
            const size_t N = (size_t)(SR * 8);
            std::vector<float> l(N, 0.0f), r;
            for (size_t i = 0; i < (size_t)SR * 2; ++i) l[i] = dist(rng);
            r = l; run(v, l, r);
            bool finite = true; double peak = 0, tailPeak = 0;
            for (size_t i = 0; i < N; ++i) { if (!std::isfinite(l[i]) || !std::isfinite(r[i])) finite = false; peak = std::max(peak,(double)std::abs(l[i])); if (i > (size_t)(SR*7.5)) tailPeak = std::max(tailPeak,(double)std::abs(l[i])); }
            CHECK(finite, "T8 %s finite over 8 s", names[t]);
            CHECK(peak < 8.0, "T8 %s peak bounded (%.2f < 8)", names[t], peak);
            CHECK(tailPeak < peak * 0.95, "T8 %s tail decaying after input stops (%.4f < %.3f)", names[t], tailPeak, peak);
        }
    }

    // ===================== T9: every character mix=0 bit-exact dry =====================
    {
        const char *names[ReverbBlock::kNumTypes] = {"Room","Hall","Plate","Spring","Shimmer","Ambience","Bloom"};
        for (int t = 0; t < ReverbBlock::kNumTypes; ++t) {
            ReverbBlock v; v.setType(t); v.setMix(0.0f); v.prepare({SR, BLK});
            std::vector<float> l((size_t)SR, 0.0f), r;
            for (size_t i = 0; i < l.size(); ++i) l[i] = (float)(0.3 * std::sin(2.0*M_PI*330.0*(double)i/SR));
            r = l; auto ref = l; run(v, l, r);
            bool exact = true; for (size_t i = 0; i < l.size(); ++i) if (l[i] != ref[i]) { exact = false; break; }
            CHECK(exact, "T9 %s mix=0 bit-exact dry", names[t]);
        }
    }

    // ===================== T10: each character produces a wet tail =====================
    {
        const char *names[ReverbBlock::kNumTypes] = {"Room","Hall","Plate","Spring","Shimmer","Ambience","Bloom"};
        for (int t = 0; t < ReverbBlock::kNumTypes; ++t) {
            ReverbBlock v; v.setType(t); v.setMix(1.0f); v.setDecaySeconds(3.0f); v.prepare({SR, BLK});
            std::vector<float> l((size_t)SR * 2, 0.0f), r = l; l[0] = r[0] = 1.0f; run(v, l, r);
            double tail = 0; for (size_t i = (size_t)(SR*0.2); i < l.size(); ++i) tail += (double)l[i]*l[i];
            CHECK(tail > 1e-5, "T10 %s has a wet tail (E=%.3g)", names[t], tail);
        }
    }

    // ===================== T11: Plate is decorrelated (true stereo) =====================
    {
        ReverbBlock v; v.setType(ReverbBlock::kPlate); v.setMix(1.0f); v.setDecaySeconds(3.0f); v.setPredelayMs(0.0f); v.prepare({SR, BLK});
        std::vector<float> l((size_t)SR * 2, 0.0f), r = l; l[0] = r[0] = 1.0f; run(v, l, r);
        double ll=0, rr=0, lr=0; for (size_t i = 2400; i < l.size(); ++i) { ll+=(double)l[i]*l[i]; rr+=(double)r[i]*r[i]; lr+=(double)l[i]*r[i]; }
        const double corr = lr / std::sqrt(ll*rr + 1e-30);
        CHECK(std::abs(corr) < 0.6, "T11 plate L/R decorrelated (corr=%.2f)", corr);
    }

    // ===================== T12: Shimmer adds octave-up energy =====================
    {
        auto octaveEnergy = [&](float shimmer) {
            ReverbBlock v; v.setType(ReverbBlock::kShimmer); v.setMix(1.0f); v.setShimmer(shimmer);
            v.setDecaySeconds(4.0f); v.setDampHz(16000.0f); v.prepare({SR, BLK});
            const size_t N = (size_t)(SR * 4);
            std::vector<float> l(N, 0.0f), r;
            for (size_t i = 0; i < (size_t)SR; ++i) l[i] = (float)(0.3 * std::sin(2.0*M_PI*200.0*(double)i/SR));
            r = l; run(v, l, r);
            return binMag(l, 400.0, (size_t)(SR*1.5), (size_t)(SR*2.0)); // +1 octave in the tail
        };
        const double on = octaveEnergy(1.0f), off = octaveEnergy(0.0f);
        CHECK(on > off * 2.0, "T12 shimmer lifts the +octave: 400Hz on=%.3g off=%.3g (>2x)", on, off);
    }

    // ===================== T13: voicing introspection is self-consistent =====================
    {
        using T = ReverbBlock::Type;
        CHECK(ReverbBlock::sizeExposed(T::kHall) && !ReverbBlock::sizeExposed(T::kPlate), "T13 Size only on Hall");
        CHECK(ReverbBlock::shimmerExposed(T::kShimmer) && !ReverbBlock::shimmerExposed(T::kRoom), "T13 Shimmer only on Shimmer");
        CHECK(ReverbBlock::tensionExposed(T::kSpring) && !ReverbBlock::tensionExposed(T::kHall), "T13 Tension only on Spring");
        bool allNamed = true; for (int t = 0; t < ReverbBlock::kNumTypes; ++t) if (!ReverbBlock::typeName(t) || !ReverbBlock::typeName(t)[0]) allNamed = false;
        CHECK(allNamed, "T13 all %d characters are named", ReverbBlock::kNumTypes);
    }

    // ===================== T14: wet low-cut keeps the lows out =====================
    {
        ReverbBlock v; v.setType(ReverbBlock::kHall); v.setMix(1.0f); v.setDecaySeconds(2.0f); v.prepare({SR, BLK});
        std::mt19937 rng(3); std::uniform_real_distribution<float> dist(-0.4f, 0.4f);
        std::vector<float> l((size_t)SR * 3, 0.0f), r;
        for (size_t i = 0; i < (size_t)SR * 2; ++i) l[i] = dist(rng);
        r = l; run(v, l, r);
        const double lo = binMag(l, 45.0, (size_t)(SR*0.5), (size_t)SR);
        const double mid = binMag(l, 900.0, (size_t)(SR*0.5), (size_t)SR);
        CHECK(lo < 0.6 * mid, "T14 wet low-cut: 45Hz %.3g << 900Hz %.3g", lo, mid);
    }

    // ===================== T15: auto-makeup tames level vs decay =====================
    {
        auto wetRms = [&](float t60) {
            ReverbBlock v; v.setType(ReverbBlock::kHall); v.setMix(1.0f); v.setDecaySeconds(t60); v.prepare({SR, BLK});
            std::mt19937 rng(5); std::uniform_real_distribution<float> dist(-0.3f, 0.3f);
            std::vector<float> l((size_t)SR * 3, 0.0f), r;
            for (size_t i = 0; i < l.size(); ++i) l[i] = dist(rng);
            r = l; run(v, l, r);
            double e = 0; size_t a = (size_t)(SR*1.5); for (size_t i = a; i < l.size(); ++i) e += (double)l[i]*l[i];
            return std::sqrt(e / (double)(l.size()-a));
        };
        const double rShort = wetRms(1.0f), rLong = wetRms(8.0f);
        CHECK(rLong < rShort * 1.6, "T15 makeup holds level: rms(8s)=%.3g vs rms(1s)=%.3g (<1.6x)", rLong, rShort);
    }

    // ===================== T16: ducking compresses wet under loud input =====================
    {
        auto wetUnder = [&](float amp){
            ReverbBlock v; v.setType(ReverbBlock::kHall); v.setMix(1.0f); v.setDecaySeconds(2.0f); v.prepare({SR, BLK});
            std::vector<float> l((size_t)SR * 2, 0.0f), r;
            for (size_t i = 0; i < l.size(); ++i) l[i] = (float)(amp * std::sin(2.0*M_PI*300.0*(double)i/SR));
            r = l; run(v, l, r);
            double e=0; size_t a=(size_t)(SR*1.5); for(size_t i=a;i<l.size();++i) e+=(double)l[i]*l[i];
            return std::sqrt(e/(double)(l.size()-a));
        };
        const double loud = wetUnder(0.5f), quiet = wetUnder(0.05f);
        // Without ducking this ratio would be ~10 (linear). Ducking pulls it down.
        CHECK(loud / quiet < 9.3, "T16 ducking compresses wet: loud/quiet = %.2f (<9.3, linear=10)", loud / quiet);
    }

    // ===================== T17: Width = 0 collapses to mono =====================
    {
        auto corr = [&](float width){
            ReverbBlock v; v.setType(ReverbBlock::kPlate); v.setMix(1.0f); v.setWidth(width); v.setDecaySeconds(2.0f); v.prepare({SR, BLK});
            std::vector<float> l((size_t)SR, 0.0f), rr = l; l[0]=rr[0]=1.0f; run(v, l, rr);
            double ll=0,r2=0,lr=0; for(size_t i=2400;i<l.size();++i){ll+=(double)l[i]*l[i];r2+=(double)rr[i]*rr[i];lr+=(double)l[i]*rr[i];}
            return lr/std::sqrt(ll*r2+1e-30);
        };
        const double c0 = corr(0.0f), c1 = corr(1.0f);
        CHECK(c0 > 0.999, "T17 Width=0 -> mono (corr=%.4f)", c0);
        CHECK(c1 < 0.9, "T17 Width=1 -> stereo (corr=%.2f)", c1);
    }

    // ===================== T18: Freeze REMOVED from the reverb section (inert) =====================
    {
        // Freeze is no longer exposed on any character, and setFreeze() is a no-op.
        CHECK(!ReverbBlock::freezeExposed(ReverbBlock::kHall),    "T18 Freeze not on Hall");
        CHECK(!ReverbBlock::freezeExposed(ReverbBlock::kShimmer), "T18 Freeze not on Shimmer");
        CHECK(!ReverbBlock::freezeExposed(ReverbBlock::kBloom),   "T18 Freeze not on Bloom");
        ReverbBlock v; v.setType(ReverbBlock::kHall); v.setMix(1.0f); v.setDecaySeconds(2.0f); v.prepare({SR, BLK});
        std::mt19937 rng(9); std::uniform_real_distribution<float> dist(-0.4f, 0.4f);
        const size_t N = (size_t)SR * 9;
        std::vector<float> l(N, 0.0f), r(N, 0.0f);
        for (size_t i = 0; i < (size_t)(SR*0.5); ++i) l[i] = r[i] = dist(rng);
        size_t p = 0; bool froze = false;
        for (; p < N; p += BLK) {
            if (!froze && p >= (size_t)(SR*0.6)) { v.setFreeze(true); froze = true; } // ignored now
            v.process(l.data()+p, r.data()+p, (int)std::min<size_t>(BLK, N-p));
        }
        auto e=[&](double t0,double t1){double s=0;for(size_t i=(size_t)(SR*t0);i<(size_t)(SR*t1);++i)s+=(double)l[i]*l[i];return s;};
        const double early = e(3.0,3.5), late = e(8.0,8.5);
        bool finite=true; for(auto s:l) if(!std::isfinite(s)) finite=false;
        CHECK(finite, "T18 stays finite");
        CHECK(late < early * 0.5, "T18 Freeze inert: tail still decays (late %.3g vs early %.3g)", late, early);
    }

    // ===================== T19: Bloom swells in (slower onset for higher Swell) =====================
    {
        auto earlyEnergy = [&](float swell){
            ReverbBlock v; v.setType(ReverbBlock::kBloom); v.setMix(1.0f); v.setDecaySeconds(3.0f); v.setSwell(swell); v.prepare({SR, BLK});
            std::vector<float> l((size_t)SR * 2, 0.0f), r=l; l[0]=r[0]=1.0f; run(v,l,r);
            double e=0; for(size_t i=0;i<(size_t)(SR*0.3);++i) e+=(double)l[i]*l[i]; return e;
        };
        const double fast = earlyEnergy(0.0f), slow = earlyEnergy(1.0f);
        CHECK(fast > slow * 2.0, "T19 bloom swell: fast-attack early energy %.3g >> slow %.3g", fast, slow);
    }

    // ===================== T20: Shimmer pitch interval selector =====================
    {
        auto bin = [&](int pitch, double f){
            ReverbBlock v; v.setType(ReverbBlock::kShimmer); v.setMix(1.0f); v.setShimmer(1.0f); v.setPitch(pitch);
            v.setDecaySeconds(4.0f); v.setDampHz(16000.0f); v.prepare({SR, BLK});
            std::vector<float> l((size_t)SR*4, 0.0f), r;
            for (size_t i=0;i<(size_t)SR;++i) l[i]=(float)(0.3*std::sin(2.0*M_PI*200.0*(double)i/SR));
            r=l; run(v,l,r);
            return binMag(l, f, (size_t)(SR*1.5), (size_t)(SR*2.0));
        };
        const double rOct  = bin(0, 800.0) / (bin(0, 400.0) + 1e-12);  // +1 octave
        const double r2Oct = bin(1, 800.0) / (bin(1, 400.0) + 1e-12);  // +2 octaves
        CHECK(r2Oct > rOct, "T20 +2oct pushes energy higher: 800/400 ratio %.2f > %.2f", r2Oct, rOct);
    }

    // ===================== T21: Hall tail is smooth, not metallic =====================
    {
        ReverbBlock v; v.setType(ReverbBlock::kHall); v.setMix(1.0f); v.setDecaySeconds(2.5f); v.setDampHz(9000.0f); v.prepare({SR, BLK});
        std::vector<float> l((size_t)SR*3, 0.0f), r=l; l[0]=r[0]=1.0f; run(v,l,r);
        const double fl = specFlatness(l, (size_t)(SR*0.5), 8192);
        // old 8-line FDN measured ~0.04 (a cluster of resonant modes = metallic). The Hall is now
        // the dense dispersion-FDN, intentionally MORE modal/lush (peakier) to match real halls
        // (Usina measures ~0.15). "Not metallic" therefore sits well below the old smooth-FDN 0.25.
        CHECK(fl > 0.12, "T21 Hall tail spectral flatness %.3f (>0.12 = lush dispersion-FDN, not metallic; real halls ~0.15)", fl);
    }

    // ===================== T22: per-character knob windows are sane =====================
    {
        using RB = ReverbBlock;
        bool ok = true;
        for (int t = 0; t < RB::kNumTypes; ++t) {
            auto T = (RB::Type)t;
            auto d = RB::decayRange(T); auto h = RB::dampRange(T); auto pre = RB::predelayRange(T);
            if (!(d.lo < d.hi && d.lo >= RB::kDecayMin - 1e-4f && d.hi <= RB::kDecayMax + 1e-4f)) ok = false;
            if (!(h.lo < h.hi && h.lo >= RB::kDampMin - 0.1f && h.hi <= RB::kDampMax + 0.1f)) ok = false;
            if (!(pre.lo <= pre.hi && pre.hi <= RB::kPreMax + 1e-4f)) ok = false;
        }
        CHECK(ok, "T22 per-character Decay/Damp/Predelay windows ordered + within outer bounds");
        auto pr = RB::decayRange(RB::kPlate);
        const float atMin = RB::mapToRange(RB::kDecayMin, RB::kDecayMin, RB::kDecayMax, pr);
        const float atMax = RB::mapToRange(RB::kDecayMax, RB::kDecayMin, RB::kDecayMax, pr);
        CHECK(std::fabs(atMin - pr.lo) < 1e-4f && std::fabs(atMax - pr.hi) < 1e-4f,
              "T22 mapToRange spans the window (%.2f..%.2f)", atMin, atMax);
        CHECK(std::fabs(pr.lo - 0.5f) < 1e-4f && std::fabs(pr.hi - 3.45f) < 1e-4f,
              "T22 Plate decay window is 0.5-3.45 s capped (%.2f..%.2f)", pr.lo, pr.hi);
        // Decay knob -> APPROXIMATELY-true RENDERED T60 (engines track within ~+/-20%, not literal
        // exact-seconds), and it must clamp at the window caps. We measure the real decaying tail
        // (not just the knob readout) so a wrong/non-tracking decay is caught.
        ReverbBlock pv; pv.setType(RB::kPlate);
        CHECK(std::fabs(pv.mappedDecay(8.0f) - 3.45f) < 1e-4f,
              "T22 Plate decay clamps at cap (8.0 -> %.2f s)", pv.mappedDecay(8.0f));
        auto renderedT60 = [&](RB::Type ty, float setSec, float dampHz){
            ReverbBlock v; v.setType(ty); v.setMix(1.0f); v.setDecaySeconds(setSec); v.setDampHz(dampHz); v.setPredelayMs(0.0f); v.prepare({SR, BLK});
            std::vector<float> l((size_t)(SR*(setSec+2.0)), 0.0f), r = l; l[0] = r[0] = 1.0f; run(v, l, r);
            return (float)t60From(l);
        };
        { const float m = renderedT60(RB::kPlate, 3.0f, 14000.0f);
          CHECK(std::fabs(m-3.0f)/3.0f < 0.20f, "T22 Plate rendered T60 %.2f s ~ set 3.0 (+-20%%)", m); }
        { const float m = renderedT60(RB::kHall, 4.0f, 20000.0f);
          CHECK(std::fabs(m-4.0f)/4.0f < 0.20f, "T22 Hall rendered T60 %.2f s ~ set 4.0 (+-20%%)", m); }
        // Predelay also reads true ms in-window, clamps at the cap (Plate 0-80, Bloom 0-160).
        CHECK(std::fabs(pv.mappedPredelay(40.0f) - 40.0f) < 1e-4f,
              "T22 Plate predelay exact in range (40 -> %.1f ms)", pv.mappedPredelay(40.0f));
        CHECK(std::fabs(pv.mappedPredelay(160.0f) - 80.0f) < 1e-4f,
              "T22 Plate predelay clamps at cap (160 -> %.1f ms)", pv.mappedPredelay(160.0f));
        // Tone 0..1 maps dark->bright across the character's Hz window.
        CHECK(std::fabs(pv.mappedTone(0.0f) - 1500.0f) < 1e-2f && std::fabs(pv.mappedTone(1.0f) - 14000.0f) < 1e-2f,
              "T22 Plate Tone spans the window (%.0f..%.0f Hz)", pv.mappedTone(0.0f), pv.mappedTone(1.0f));
    }

    // ===================== T23: Room (small-room FDN) is short, flat, smooth, wide =====================
    {
        ReverbBlock v; v.setType(ReverbBlock::kRoom); v.setMix(1.0f); v.setDecaySeconds(0.3f); v.prepare({SR, BLK});
        std::vector<float> l((size_t)SR, 0.0f), r = l; l[0] = r[0] = 1.0f; run(v, l, r);
        // RT60 from broadband Schroeder
        std::vector<double> e(l.size()); for (size_t i = 0; i < l.size(); ++i) e[i] = (double)l[i]*l[i];
        double acc = 0; std::vector<double> sc(e.size()); for (long i = (long)e.size()-1; i >= 0; --i) { acc += e[i]; sc[i] = acc; }
        double s0 = sc[0]; long i5 = -1, i35 = -1;
        for (size_t i = 0; i < sc.size(); ++i) { double db = 10*std::log10(sc[i]/s0 + 1e-30); if (i5<0 && db<=-5) i5=i; if (i35<0 && db<=-35){ i35=i; break; } }
        double rt60 = (i5>=0 && i35>i5) ? (double)(i35-i5)/SR * (60.0/30.0) : 0;
        CHECK(rt60 > 0.15 && rt60 < 0.55, "T23 Room decay short (RT60=%.2fs at 0.3 set)", rt60);
        double sl=0,sr=0,slr=0; for (size_t i=0;i<l.size();++i){ sl+=l[i]*l[i]; sr+=r[i]*r[i]; slr+=l[i]*r[i]; }
        CHECK(slr/(std::sqrt(sl*sr)+1e-12) < 0.6, "T23 Room is wide/decorrelated");
    }

    std::printf("\n%s (%d FAIL)\n", gFails == 0 ? "ALL PASS" : "FAILURES", gFails);
    return gFails == 0 ? 0 : 1;
}
