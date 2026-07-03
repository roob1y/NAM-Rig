#pragma once
// SpectralShifter — a phase-vocoder (STFT) pitch shifter for EXACT octave ratios,
// POLYPHONIC and glitch-free on chords. This is the engine the mono granular /
// analog-divider approaches couldn't be: because it shifts every spectral peak
// independently, it octaves two, three, six notes at once without the mono
// tracker's chord glitching (the play-test-#3 problem).
//
// It's the canonical Bernsee smbPitchShift algorithm (public domain) with a
// self-contained radix-2 FFT, so the whole thing stays JUCE-free and is verified
// offline by tests/pitch_test.cpp. Fixed ratio (2.0 up, 0.5 / 0.25 down); exact
// octaves are the easy, clean case for a phase vocoder (integer-ish bin shift).
//
// Latency = fftFrameSize - stepSize (the analysis FIFO fill). At N=1024, 4x
// overlap (hop 256) that's 768 samples (~16 ms @ 48k) — the block reports it so
// the DAW delay-compensates, and the caller must delay the DRY path to match.
//
// CPU: one FFT pair per hop. Run only the voices you need (skip when level is 0).

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace nam_rig
{

class SpectralShifter
{
public:
    // frameSize must be a power of two; osamp = overlap factor (4 => 75% overlap).
    void prepare(double sampleRate, int frameSize = 1024, int osamp = 4)
    {
        mSr = sampleRate > 0.0 ? sampleRate : 48000.0;
        mN = frameSize;
        mOsamp = osamp;
        mStep = mN / mOsamp;
        mLatency = mN - mStep;
        const int half = mN / 2;
        mInFIFO.assign((size_t)mN, 0.0f);
        mOutFIFO.assign((size_t)mN, 0.0f);
        mFFT.assign((size_t)(2 * mN), 0.0f);
        mLastPhase.assign((size_t)(half + 1), 0.0);
        mSumPhase.assign((size_t)(half + 1), 0.0);
        mOutAccum.assign((size_t)(2 * mN), 0.0f);
        mAnaMagn.assign((size_t)mN, 0.0);
        mAnaFreq.assign((size_t)mN, 0.0);
        mSynMagn.assign((size_t)mN, 0.0);
        mSynFreq.assign((size_t)mN, 0.0);
        reset();
    }

    void reset()
    {
        std::fill(mInFIFO.begin(), mInFIFO.end(), 0.0f);
        std::fill(mOutFIFO.begin(), mOutFIFO.end(), 0.0f);
        std::fill(mFFT.begin(), mFFT.end(), 0.0f);
        std::fill(mLastPhase.begin(), mLastPhase.end(), 0.0);
        std::fill(mSumPhase.begin(), mSumPhase.end(), 0.0);
        std::fill(mOutAccum.begin(), mOutAccum.end(), 0.0f);
        mRover = mLatency;
    }

    int latency() const { return mLatency; }

    void setRatio(float r) { mRatio = r; }

    // Shift `buf` in place by mRatio. Output is delayed by latency() samples.
    void process(float *buf, int n)
    {
        const int N = mN, N2 = mN / 2, step = mStep, osamp = mOsamp;
        const double freqPerBin = mSr / (double)N;
        const double expct = 2.0 * kPi * (double)step / (double)N;
        const float ratio = mRatio;

        for (int i = 0; i < n; ++i)
        {
            mInFIFO[(size_t)mRover] = buf[i];
            buf[i] = mOutFIFO[(size_t)(mRover - mLatency)];
            ++mRover;
            if (mRover < N) continue;
            mRover = mLatency;

            // analysis window + real FFT input
            for (int k = 0; k < N; ++k)
            {
                const double w = -0.5 * std::cos(2.0 * kPi * (double)k / (double)N) + 0.5;
                mFFT[(size_t)(2 * k)] = (float)(mInFIFO[(size_t)k] * w);
                mFFT[(size_t)(2 * k + 1)] = 0.0f;
            }
            fft(mFFT.data(), N, -1);

            for (int k = 0; k <= N2; ++k)
            {
                const double real = mFFT[(size_t)(2 * k)];
                const double imag = mFFT[(size_t)(2 * k + 1)];
                const double magn = 2.0 * std::sqrt(real * real + imag * imag);
                const double phase = std::atan2(imag, real);
                double tmp = phase - mLastPhase[(size_t)k];
                mLastPhase[(size_t)k] = phase;
                tmp -= (double)k * expct;
                long qpd = (long)(tmp / kPi);
                if (qpd >= 0) qpd += qpd & 1; else qpd -= qpd & 1;
                tmp -= kPi * (double)qpd;
                tmp = (double)osamp * tmp / (2.0 * kPi);
                tmp = (double)k * freqPerBin + tmp * freqPerBin;
                mAnaMagn[(size_t)k] = magn;
                mAnaFreq[(size_t)k] = tmp;
            }

            // pitch shift: move each bin's energy to k*ratio
            for (int k = 0; k <= N2; ++k) { mSynMagn[(size_t)k] = 0.0; mSynFreq[(size_t)k] = 0.0; }
            for (int k = 0; k <= N2; ++k)
            {
                const int index = (int)((double)k * ratio);
                if (index >= 0 && index <= N2)
                {
                    mSynMagn[(size_t)index] += mAnaMagn[(size_t)k];
                    mSynFreq[(size_t)index] = mAnaFreq[(size_t)k] * ratio;
                }
            }

            for (int k = 0; k <= N2; ++k)
            {
                const double magn = mSynMagn[(size_t)k];
                double tmp = mSynFreq[(size_t)k];
                tmp -= (double)k * freqPerBin;
                tmp /= freqPerBin;
                tmp = 2.0 * kPi * tmp / (double)osamp;
                tmp += (double)k * expct;
                mSumPhase[(size_t)k] += tmp;
                const double phase = mSumPhase[(size_t)k];
                mFFT[(size_t)(2 * k)] = (float)(magn * std::cos(phase));
                mFFT[(size_t)(2 * k + 1)] = (float)(magn * std::sin(phase));
            }
            for (int k = N + 2; k < 2 * N; ++k) mFFT[(size_t)k] = 0.0f;

            fft(mFFT.data(), N, 1); // inverse

            for (int k = 0; k < N; ++k)
            {
                const double w = -0.5 * std::cos(2.0 * kPi * (double)k / (double)N) + 0.5;
                mOutAccum[(size_t)k] += (float)(2.0 * w * mFFT[(size_t)(2 * k)] / ((double)N2 * (double)osamp));
            }
            for (int k = 0; k < step; ++k) mOutFIFO[(size_t)k] = mOutAccum[(size_t)k];
            std::memmove(mOutAccum.data(), mOutAccum.data() + step, (size_t)N * sizeof(float));
            for (int k = 0; k < mLatency; ++k) mInFIFO[(size_t)k] = mInFIFO[(size_t)(k + step)];
        }
    }

private:
    static constexpr double kPi = 3.14159265358979323846;

    // In-place interleaved radix-2 FFT (Bernsee smbFft). sign -1 = forward, +1 = inverse.
    static void fft(float *b, int n, int sign)
    {
        // bit-reversal
        for (int i = 2; i < 2 * n - 2; i += 2)
        {
            int j = 0;
            for (int bitm = 2; bitm < 2 * n; bitm <<= 1)
            {
                if (i & bitm) j++;
                j <<= 1;
            }
            if (i < j)
            {
                std::swap(b[i], b[j]);
                std::swap(b[i + 1], b[j + 1]);
            }
        }
        const int stages = (int)(std::log((double)n) / std::log(2.0) + 0.5);
        int le = 2;
        for (int k = 0; k < stages; ++k)
        {
            le <<= 1;
            const int le2 = le >> 1;
            double ur = 1.0, ui = 0.0;
            const double arg = kPi / (double)(le2 >> 1);
            const double wr = std::cos(arg);
            const double wi = (double)sign * std::sin(arg);
            for (int j = 0; j < le2; j += 2)
            {
                for (int i = j; i < 2 * n; i += le)
                {
                    const double tr = b[i + le2] * ur - b[i + le2 + 1] * ui;
                    const double ti = b[i + le2] * ui + b[i + le2 + 1] * ur;
                    b[i + le2] = (float)(b[i] - tr);
                    b[i + le2 + 1] = (float)(b[i + 1] - ti);
                    b[i] = (float)(b[i] + tr);
                    b[i + 1] = (float)(b[i + 1] + ti);
                }
                const double tr = ur * wr - ui * wi;
                ui = ur * wi + ui * wr;
                ur = tr;
            }
        }
    }

    double mSr = 48000.0;
    int mN = 1024, mOsamp = 4, mStep = 256, mLatency = 768, mRover = 768;
    float mRatio = 1.0f;
    std::vector<float> mInFIFO, mOutFIFO, mFFT, mOutAccum;
    std::vector<double> mLastPhase, mSumPhase, mAnaMagn, mAnaFreq, mSynMagn, mSynFreq;
};

} // namespace nam_rig
