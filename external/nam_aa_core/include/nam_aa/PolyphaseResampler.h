#pragma once
// Fixed-ratio L/M polyphase FIR resampler, linear phase, JUCE-free.
//
// Built for the plugin's rational rate families: 44.1k<->48k and 88.2k<->96k are
// both exactly 147:160, so one normalized prototype covers every direction of
// both families. The prototype is a Kaiser-windowed sinc generated at prepare()
// time (deterministic, no coefficient tables).
//
// Quality/latency knob: kTapsPerPhase (K). Latency is exactly K/2 input samples
// per direction (K even). With K=48 and 96 dB stopband the transition band lands
// at roughly 16.5..22.05 kHz: flat to ~16.5k, -96 dB at the fold frequency.
// Compare: JUCE WindowedSincInterpolator is ~100 input samples per direction.
//
// Verified by tools/verify_polyphase.py (passband ripple, stopband, exact
// latency) and the chain-level rigs (tests/latency_test.cpp, ASR sweeps).

#include <cassert>
#include <cmath>
#include <cstring>
#include <vector>

namespace nam_aa
{

class PolyphaseResampler
{
public:
    static constexpr int kTapsPerPhase = 48; // K (even). Latency = K/2 input samples.
    static constexpr double kStopbandDb = 96.0;

    // upFactor L / downFactor M, e.g. up 44.1->48 is L=160, M=147.
    void prepare(int upFactor, int downFactor)
    {
        L = upFactor;
        M = downFactor;
        const int N = kTapsPerPhase * L; // prototype length (one extra implicit zero tap)

        // Kaiser design. Cutoff: the lower Nyquist in prototype-normalized
        // frequency is min(1/L, 1/M) * 0.5 * 2 = ... work in units of the
        // prototype rate: input Nyquist = 1/(2L), output Nyquist = 1/(2M)
        // (as fractions of the prototype rate). Anti-alias/anti-image cutoff
        // must sit at the smaller of the two, with the transition band placed
        // entirely BELOW it so the stopband starts at the fold frequency.
        const double foldNorm = 0.5 / std::max(L, M); // cycles/sample @ proto rate
        const double beta = 0.1102 * (kStopbandDb - 8.7);
        // Kaiser transition width estimate: dF = (A - 7.95) / (2.285 * N) in
        // normalized rad/(2pi) units => cycles/sample.
        const double dF = (kStopbandDb - 7.95) / (2.285 * 2.0 * 3.14159265358979323846 * N);
        const double fc = foldNorm - dF * 0.5 - (0.25 / N); // center transition below fold

        proto.assign((size_t)N, 0.0f);
        const double mid = (N - 1) * 0.5;
        const double i0b = besselI0(beta);
        for (int n = 0; n < N; n++)
        {
            const double x = n - mid;
            const double s = (x == 0.0) ? 2.0 * fc
                                        : std::sin(2.0 * 3.14159265358979323846 * fc * x)
                                            / (3.14159265358979323846 * x);
            const double r = 2.0 * x / (N - 1); // in [-1, 1]
            const double w = besselI0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0b;
            proto[(size_t)n] = (float)(s * w * L); // gain L compensates polyphase split
        }

        hist.assign((size_t)kTapsPerPhase, 0.0f);
        reset();
    }

    void reset()
    {
        std::fill(hist.begin(), hist.end(), 0.0f);
        histPos = 0;
        phase = 0;
        primed = 0;
    }

    // Exact group delay of the linear-phase prototype, in INPUT samples.
    double latencyInputSamples() const
    {
        return (double)(kTapsPerPhase * L - 1) / (2.0 * L);
    }

    // Push numIn input samples, produce output samples (returns count written).
    // outCap must be >= ceil((numIn + 1) * L / M).
    int process(const float *in, int numIn, float *out, int outCap)
    {
        int produced = 0;
        for (int i = 0; i < numIn; i++)
        {
            // push input sample into history ring (newest at histPos)
            histPos = (histPos + 1) % kTapsPerPhase;
            hist[(size_t)histPos] = in[i];
            if (primed < kTapsPerPhase)
                primed++;

            // produce all outputs whose phase counter falls within this input
            while (phase < L)
            {
                assert(produced < outCap);
                // y = sum_k proto[phase + k*L] * x[newest - k]
                double acc = 0.0;
                int idx = histPos;
                const float *p = proto.data() + phase;
                for (int k = 0; k < kTapsPerPhase; k++)
                {
                    acc += (double)p[(size_t)k * (size_t)L] * hist[(size_t)idx];
                    idx = (idx == 0) ? kTapsPerPhase - 1 : idx - 1;
                }
                out[produced++] = (float)acc;
                phase += M;
            }
            phase -= L; // consumed one input sample
        }
        return produced;
    }

private:
    static double besselI0(double x)
    {
        // series expansion, converges quickly for the beta range we use
        double sum = 1.0, term = 1.0;
        const double hx = x * 0.5;
        for (int k = 1; k < 64; k++)
        {
            term *= (hx / k) * (hx / k);
            sum += term;
            if (term < 1e-18 * sum)
                break;
        }
        return sum;
    }

    int L = 160, M = 147;
    std::vector<float> proto; // prototype, length K*L, phase p taps at proto[p + k*L]
    std::vector<float> hist;  // last K input samples (ring, newest at histPos)
    int histPos = 0;
    int phase = 0;  // 0..L-1 phase accumulator (advances by M per output)
    int primed = 0;
};

} // namespace nam_aa
