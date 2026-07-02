#pragma once
// Sample-rate-conversion layer (DAW rate <-> Rsrc family rate).
// Single source of truth for the plugin (PluginProcessor::processBlock stages 1/3),
// the latency test (tests/latency_test.cpp), and the offline chain processor
// (tests/chain_process.cpp).
//
// Two modes, chosen automatically in prepare():
//  - Polyphase (preferred): exact rational ratios (44.1<->48 and 88.2<->96 are both
//    147:160; 176.4->96 is 147:80; 192->96 is 2:1). Fixed Kaiser-designed FIR,
//    ~24 samples latency per direction — ~4x less than the sinc path.
//  - Windowed-sinc fallback: any other (non-rational / unusual) rate pair.
//
// Latency (DAW samples) is reported by chainLatencyDawSamples(), mode-aware, and
// verified by tests/latency_test.cpp. The polyphase down-stage uses an
// output-domain FIFO primed with a fixed number of zeros at prepare() time, so
// latency is independent of the host's block size (same property the sinc path's
// floor+1 priming provides).

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <numeric>
#include <vector>

#include "PolyphaseResampler.h"
#include "RoutingMath.h"

namespace nam_aa
{

class SrcLayer
{
public:
    // Call from prepareToPlay. dawRate determines the conversion ratio (Rsrc is
    // implied: >72k DAW -> 96k family, else 48k family). maxDawBlock: largest host
    // block; srcCap: capacity of the caller's Rsrc-rate buffer.
    void prepare(int maxDawBlock, int srcCap, double dawRate)
    {
        mSrcCap = srcCap;
        mDawRate = dawRate;
        mRsrc = (dawRate > 72000.0) ? 96000.0 : 48000.0;

        // Rational ratio with small factors? Use the polyphase path.
        mUsePolyphase = false;
        const long daw = (long)std::llround(dawRate);
        const long rsrc = (long)std::llround(mRsrc);
        if (std::abs(dawRate - (double)daw) < 1e-9 && daw != rsrc)
        {
            const long g = std::gcd(daw, rsrc);
            const long Lup = rsrc / g, Mup = daw / g;
            if (Lup <= 512 && Mup <= 512)
            {
                mUsePolyphase = true;
                mUp.prepare((int)Lup, (int)Mup);   // DAW -> Rsrc
                mDown.prepare((int)Mup, (int)Lup); // Rsrc -> DAW
            }
        }

        mUpInterp.reset();
        mDownInterp.reset();
        mInCarry.clear();
        mInCarry.reserve((size_t)maxDawBlock * 3);
        mOutLeftover.clear();
        mOutLeftover.reserve((size_t)srcCap);
        mDownWork.clear();
        mDownWork.reserve((size_t)srcCap * 2 + 256);

        // Polyphase output FIFO, primed with a fixed number of zeros so the down
        // stage can always deliver exactly numSamples (cumulative stage rounding
        // can leave us up to 2 samples short; prime 3 for margin). Fixed prime =>
        // block-size-independent latency.
        mOutFifo.clear();
        mOutFifo.reserve((size_t)maxDawBlock * 2 + 64);
        if (mUsePolyphase)
            mOutFifo.assign((size_t)kPolyPrime, 0.0f);
    }

    bool usingPolyphase() const { return mUsePolyphase; }

    // Full SRC contribution to chain latency, in DAW samples (0 if rates match).
    double chainLatencyDawSamples() const
    {
        if (std::abs(mDawRate - mRsrc) <= 1.0)
            return 0.0;
        if (mUsePolyphase)
        {
            return mUp.latencyInputSamples()                              // DAW domain
                   + mDown.latencyInputSamples() * (mDawRate / mRsrc)     // Rsrc -> DAW
                   + (double)kPolyPrime;
        }
        const double base = mUpInterp.getBaseLatency();
        return base + (base + kSrcKernelGuard + 1) * (mDawRate / mRsrc);
    }

    // ---- DAW rate -> Rsrc. Returns number of Rsrc samples written to dst. ----
    int up(const float *dawIn, int numSamples, float *dst, double dawRate, double Rsrc)
    {
        if (mUsePolyphase)
        {
            jassert(std::abs(dawRate - mDawRate) < 1.0 && std::abs(Rsrc - mRsrc) < 1.0);
            return mUp.process(dawIn, numSamples, dst, mSrcCap);
        }

        // --- windowed-sinc fallback (carry logic unchanged) ---
        jassert(mInCarry.size() + (size_t)numSamples <= mInCarry.capacity());
        mInCarry.insert(mInCarry.end(), dawIn, dawIn + numSamples);
        const int inLen = (int)mInCarry.size();
        const double ratioUp = dawRate / Rsrc; // input samples per output sample

        int want = (int)std::floor(inLen / ratioUp);
        jassert(want <= mSrcCap);
        want = juce::jlimit(0, mSrcCap, want);
        if (want <= 0)
            return 0;

        const int used = mUpInterp.process(ratioUp, mInCarry.data(), dst, want);
        jassert(used <= inLen);
        mInCarry.erase(mInCarry.begin(), mInCarry.begin() + juce::jmin(used, (int)mInCarry.size()));
        return want;
    }

    // ---- Rsrc -> DAW rate, producing exactly numSamples into dawOut. ----
    void down(const float *src, int L, float *dawOut, int numSamples, double dawRate, double Rsrc)
    {
        if (mUsePolyphase)
        {
            // Append resampled output to the FIFO, then pop exactly numSamples.
            const size_t before = mOutFifo.size();
            mOutFifo.resize(before + (size_t)numSamples + 8);
            const int got = mDown.process(src, L, mOutFifo.data() + before,
                                          (int)(mOutFifo.size() - before));
            mOutFifo.resize(before + (size_t)got);

            // After a dropout/short first block the FIFO could briefly be short;
            // backfill with zeros at the FRONT (never happens in steady state).
            if ((int)mOutFifo.size() < numSamples)
                mOutFifo.insert(mOutFifo.begin(),
                                (size_t)(numSamples - (int)mOutFifo.size()), 0.0f);

            std::memcpy(dawOut, mOutFifo.data(), (size_t)numSamples * sizeof(float));
            mOutFifo.erase(mOutFifo.begin(), mOutFifo.begin() + numSamples);

            // FIFO should hover around kPolyPrime; bound it for safety.
            jassert((int)mOutFifo.size() <= kPolyPrime + 4);
            return;
        }

        // --- windowed-sinc fallback (floor+1 priming; see latencySamples note) ---
        const double ratioDown = Rsrc / dawRate;
        const int need = (int)std::floor(numSamples * ratioDown) + 1 + kSrcKernelGuard;

        jassert(mOutLeftover.size() + (size_t)L <= mDownWork.capacity());
        mDownWork.clear();
        mDownWork.insert(mDownWork.end(), mOutLeftover.begin(), mOutLeftover.end());
        mDownWork.insert(mDownWork.end(), src, src + L);

        if ((int)mDownWork.size() < need)
            mDownWork.insert(mDownWork.begin(), (size_t)(need - (int)mDownWork.size()), 0.0f);

        jassert((int)mDownWork.size() >= need);
        const int used = mDownInterp.process(ratioDown, mDownWork.data(), dawOut, numSamples);
        jassert(used <= (int)mDownWork.size());

        const int keepFrom = juce::jmin(used, (int)mDownWork.size());
        mOutLeftover.assign(mDownWork.begin() + keepFrom, mDownWork.end());

        if ((int)mOutLeftover.size() > mSrcCap)
            mOutLeftover.erase(mOutLeftover.begin(),
                               mOutLeftover.begin() + ((int)mOutLeftover.size() - mSrcCap));
    }

private:
    static constexpr int kPolyPrime = 3; // fixed down-FIFO prime (DAW samples)

    // Polyphase path
    PolyphaseResampler mUp, mDown;
    std::vector<float> mOutFifo; // DAW-domain output FIFO for the down stage
    bool mUsePolyphase = false;

    // Sinc fallback path
    juce::WindowedSincInterpolator mUpInterp;
    juce::WindowedSincInterpolator mDownInterp;
    std::vector<float> mInCarry;
    std::vector<float> mOutLeftover;
    std::vector<float> mDownWork;

    double mDawRate = 48000.0, mRsrc = 48000.0;
    int mSrcCap = 0;
};

} // namespace nam_aa
