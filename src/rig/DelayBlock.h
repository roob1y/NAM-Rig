#pragma once
// DelayBlock — stereo delay (post-cab): free time or host-tempo sync
// (straight / dotted / triplet), feedback tone LPF inside the loop, normal or
// ping-pong routing, wow+flutter modulated repeats, mix, width. Time changes
// glide (one-pole smooth + Hermite fractional read) for tape-style repitch
// instead of clicks. Zero latency. Verified by tests/delay_test.cpp.
//
// Mono-buffer rule (left == right): mono in-place delay on the left line.

#include "Blocks.h"
#include "Lfo.h"
#include "Biquad.h"
#include "Saturation.h"
#include <array>
#include <algorithm>

namespace nam_rig
{

class DelayBlock : public StereoBlock
{
public:
    static constexpr float kMaxTimeMs = 2000.0f, kMinTimeMs = 20.0f;
    static constexpr float kMaxFeedback = 0.95f;     // clean-delay param ceiling (unchanged)
    static constexpr float kMaxFeedbackHard = 1.10f; // absolute safety clamp; a tape character
                                                     // can voice its own ceiling up to this for
                                                     // self-oscillation (the in-loop saturation
                                                     // bounds the level so it never runs away)
    static constexpr float kMinLowCutHz = 20.0f; // Low Cut at/below this = off
    // Tape High Cut knob remap span (the clean 1k-20k knob position maps here so the
    // whole travel is live; default ~8k -> ~2k = the voiced tape tone).
    static constexpr float kTapeHiCutMin = 300.0f, kTapeHiCutMax = 5000.0f;
    // Wow/flutter at full knob: slow deep + fast shallow, in ms of sweep.
    static constexpr double kWowHz = 0.9, kWowDepthMs = 2.5;
    static constexpr double kFlutterHz = 6.5, kFlutterDepthMs = 0.25;

    // Sync divisions in quarter-note beats; index 0 = Free (use time knob).
    // Order is the parameter StringArray order — never reorder, only append.
    static constexpr std::array<double, 14> kSyncBeats = {
        0.0,        // Free
        4.0,        // 1/1
        3.0,        // 1/2.
        2.0,        // 1/2
        4.0 / 3.0,  // 1/2T
        1.5,        // 1/4.
        1.0,        // 1/4
        2.0 / 3.0,  // 1/4T
        0.75,       // 1/8.
        0.5,        // 1/8
        1.0 / 3.0,  // 1/8T
        0.375,      // 1/16.
        0.25,       // 1/16
        1.0 / 6.0}; // 1/16T

    // Time-change behaviour (a per-CHARACTER property, like the reverb voicings):
    //   Tape    = glide the read position -> classic pitch swoop on time/sync change.
    //   Digital = equal-power crossfade between the old and new FIXED positions ->
    //             clean, no repitch. Wow/Flutter still modulates either way.
    enum class TimeMode { Tape, Digital };

    // DELAY CHARACTERS (like the reverb voicings). Clean = the transparent
    // engine (no colour); the tape/analog characters grow from it by engaging
    // the per-character loop stages below. Default is Clean so the engine is
    // byte-for-byte the original delay unless a character is selected -- order
    // is the parameter StringArray order, append only.
    enum class Character { Clean = 0, Tape = 1 };

    // What a character switches on in the feedback loop. All-zero = behaves like
    // the clean delay (a plain transparent shaper). Every nonlinear/colour stage
    // here recirculates, so aliasing/level effects compound per repeat -- the
    // saturation runs 2nd-order ADAA for exactly this reason.
    struct DelayVoicing
    {
        TimeMode timeMode;     // time-change feel (Tape glide = pitch swoop)
        float satDrive;        // record level into the tape soft-clip; 0 = no saturation.
                               //   gentle below ~1/satDrive, compresses above -> bounds the loop.
        float headBumpHz;      // IN-LOOP low-mid "head bump" peak (builds the tape bloom across
        float headBumpDb;      //   repeats); 0 dB = off
        float headBumpQ;
        float gapLossHz;       // head-gap HF roll-off (IN-LOOP): caps the High Cut at this corner
                               //   (knob darkens further, never brighter); 0 = no cap.
        float outBassHz;       // OUTPUT-once bass thinning: low-shelf corner (applied once, NOT
        float outBassDb;       //   recirculated -> thins the echo without compounding the tail).
        float wowMul;          // multiplier on the base wow depth (kWowDepthMs)
        float flutterMul;      // multiplier on the base flutter depth (kFlutterDepthMs)
        float driftHz;         // slow random pitch drift rate (worn-transport wander); 0 = off
        float driftDepthMs;    //   drift depth in ms
        float preampShelfHz;   // OUTPUT-once high-shelf (FET-preamp brightness, applied once, not
        float preampShelfDb;   //   recirculated); 0 dB = off
        float fbCeiling;       // effective feedback ceiling for this character (<= kMaxFeedbackHard)
    };

    static DelayVoicing voicingFor(Character c)
    {
        switch (c)
        {
        case Character::Tape:
            // Tape-echo voicing fit to a MEASURED reference, split into PER-PASS
            // (in-loop) and ONCE (output) stages from a multi-repeat tail capture
            // (the single-repeat fit alone gave the wrong tail). Measured:
            //   - PER-PASS (rep[n+1]/rep[n]): low-mid BLOOM +5.7dB @ 330Hz (Q0.6),
            //     flat bass, gap-loss HF roll (2-pole LP ~2 kHz). This is what makes
            //     the tail darken+bloom correctly instead of thinning.
            //   - ONCE at output: bass thinning low-shelf -6dB @ 480Hz + FET-preamp
            //     high-shelf +4dB @ 1.4kHz. Applied once (NOT recirculated), so they
            //     shape the timbre without compounding down the tail.
            //   - saturation gentle (ref ~linear, feedback asymptote ~0.85); wow/
            //     flutter ~0.1% peak; Tape glide; fb ceiling 1.10 (sat tames runaway).
            //          sat  hbHz  hbDb  hbQ  gapHz  obHz  obDb  wowM   flutM  drHz  drMs   ppHz   ppDb fbCeil
            return { TimeMode::Tape, 1.2f, 330.0f, 4.0f, 0.6f, 2000.0f, 480.0f, -6.0f,
                     0.05f, 0.055f, 0.5f, 0.05f, 1400.0f, 4.0f, 1.10f };
        case Character::Clean:
        default:
            return { TimeMode::Digital, 0.0f, 0.0f, 0.0f, 0.7f, 0.0f, 0.0f, 0.0f,
                     0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, kMaxFeedback };
        }
    }

    const char *name() const override { return "Delay"; }

    void prepare(const BlockContext &ctx) override
    {
        mFs = ctx.sampleRate;
        const int maxDelay =
            (int)std::ceil((kMaxTimeMs + kWowDepthMs + kFlutterDepthMs + 4.0) * 0.001 * mFs);
        for (auto &d : mLine)
            d.prepare(maxDelay);
        mWow.prepare(mFs);
        mWow.setRateHz((float)kWowHz);
        mFlutter.prepare(mFs);
        mFlutter.setRateHz((float)kFlutterHz);
        mDrift.prepare(mFs); // slow random transport wander (tape characters only)
        mDrift.setWaveform(Lfo::SampleHold);
        mDrift.setRateHz(mVoicing.driftHz > 0.0f ? mVoicing.driftHz : 1.2f);
        mDriftK = 1.0f - std::exp((float)(-1.0 / (0.090 * mFs))); // ~90 ms smoother turns the
                                                                  // S&H steps into a continuous
                                                                  // random walk (no click on jumps)
        // Time glide ~80 ms; mix/width zipper guard 10 ms.
        mTimeK = 1.0f - std::exp((float)(-1.0 / (0.080 * mFs)));
        mSmoothK = 1.0f - std::exp((float)(-1.0 / (0.010 * mFs)));
        mXfadeInc = (float)(1.0 / (0.020 * mFs)); // ~20 ms digital time-change crossfade
        updateTone(true);
        updateLowCut(true);
        updateTapeFilters(true);
        reset();
        mPrepared = true;
    }

    void reset() override
    {
        for (auto &d : mLine)
            d.reset();
        for (auto &t : mTone)
            t.reset();
        for (auto &t : mLowCut)
            t.reset();
        for (auto &t : mHeadBump)
            t.reset();
        for (auto &t : mOutBass)
            t.reset();
        for (auto &t : mPreamp)
            t.reset();
        mWow.reset();
        mFlutter.reset();
        mDrift.reset();
        mDriftZ = 0.0;
        mSatX1[0] = mSatX1[1] = mSatX2[0] = mSatX2[1] = 0.0; // tape saturation ADAA history
        const double b0 = (double)currentTimeMs(), b1 = (double)currentTimeMsR();
        mBaseZ[0] = b0;
        mBaseZ[1] = b1;
        mTap[0] = {b0, b0, 1.0f, false}; // active, prev, xfade, fading
        mTap[1] = {b1, b1, 1.0f, false};
        mMixZ = mMix;
        mWidthZ = mWidth;
    }

    // ---- parameters (audio thread) ----
    void setTimeMs(float ms) { mTimeMs = std::clamp(ms, kMinTimeMs, kMaxTimeMs); }
    void setSyncIndex(int idx) { mSyncIdx = std::clamp(idx, 0, (int)kSyncBeats.size() - 1); }
    // Right-side division: index 0 = Link (R mirrors L); 1..13 select the same
    // musical divisions as kSyncBeats[1..13]. Unlinking puts the delay in DUAL
    // mode (independent per-side time + feedback) -- the dotted-1/8 + 1/4 trick.
    void setSyncIndexR(int idx) { mSyncIdxR = std::clamp(idx, 0, (int)kSyncBeats.size() - 1); }
    void setBpm(double bpm) { mBpm = (bpm > 1.0) ? bpm : 120.0; }
    void setFeedback(float f) { mFeedback = std::clamp(f, 0.0f, kMaxFeedbackHard); }
    void setToneHz(float hz)
    {
        if (hz != mToneHz)
        {
            mToneHz = hz;
            if (mPrepared)
                updateTone(false);
        }
    }
    // Feedback high-pass (Low Cut): clears bass build-up so stacked repeats stay
    // defined. <= kMinLowCutHz is treated as off.
    void setLowCutHz(float hz)
    {
        if (hz != mLowCutHz)
        {
            mLowCutHz = hz;
            if (mPrepared)
                updateLowCut(false);
        }
    }
    void setPingPong(bool pp) { mPingPong = pp; }
    void setTimeMode(TimeMode m) { mTimeMode = m; } // per-character; clean delay = Digital

    // Select a delay character. Engages the per-character loop stages + the
    // time-change feel. Clean leaves the engine transparent and does NOT touch
    // the TimeMode (the processor sets that for the clean delay), so the
    // default/clean path stays byte-for-byte the original delay.
    void setCharacter(Character c)
    {
        mCharacter = c;
        mTapeOn = (c != Character::Clean);
        mVoicing = voicingFor(c);
        if (mTapeOn)
        {
            mTimeMode = mVoicing.timeMode;
            mDrift.setRateHz(mVoicing.driftHz > 0.0f ? mVoicing.driftHz : 1.2f);
        }
        if (mPrepared)
        {
            updateTapeFilters(false);
            updateTone(false);   // (re)apply / release the tape gap-loss cap on the High Cut
            updateLowCut(false); // (re)apply / release the tape bass-rolloff floor on the Low Cut
        }
    }
    void setCharacter(int c)
    {
        setCharacter(c == (int)Character::Tape ? Character::Tape : Character::Clean);
    }
    void setWidth(float w) { mWidth = std::clamp(w, 0.0f, 1.0f); }
    void setModAmount(float m) { mModAmt = std::clamp(m, 0.0f, 1.0f); }
    void setMix(float m) { mMix = std::clamp(m, 0.0f, 1.0f); }

    // Effective LEFT time after sync resolution (exposed for verification + UI).
    float currentTimeMs() const
    {
        if (mSyncIdx > 0)
            return std::clamp((float)(kSyncBeats[(size_t)mSyncIdx] * 60000.0 / mBpm),
                              kMinTimeMs, kMaxTimeMs);
        return mTimeMs;
    }

    // Effective RIGHT time: Link (idx 0) mirrors the left; otherwise its own
    // synced division. Used by the dual routing and the tap visualiser.
    float currentTimeMsR() const
    {
        if (mSyncIdxR <= 0)
            return currentTimeMs();
        return std::clamp((float)(kSyncBeats[(size_t)mSyncIdxR] * 60000.0 / mBpm),
                          kMinTimeMs, kMaxTimeMs);
    }
    bool dualTime() const { return mSyncIdxR > 0; } // R unlinked -> independent sides

    const Biquad &toneFilter() const { return mTone[0]; } // for verification

    void process(float *left, float *right, int numSamples) override
    {
        const bool stereo = (left != right);
        const float targetMsL = currentTimeMs();
        const float targetMsR = stereo ? currentTimeMsR() : targetMsL;
        const bool dual = stereo && dualTime(); // independent L/R times + feedback
        // Effective feedback. Clean is capped at kMaxFeedback (byte-for-byte with the
        // original). Tape: the in-loop head bump raises the loop gain at its peak,
        // which would make the delay self-oscillate at a LOW knob setting (squashing
        // the usable decaying range into the bottom of the knob). Normalise by the
        // in-loop peak gain so the knob reaches oscillation near the TOP, like clean,
        // while the bump still shapes the per-repeat tone. A tape character can run
        // slightly past unity (fbCeiling) because the saturation bounds the level.
        const float fb = mTapeOn
            ? std::min(mFeedback, mVoicing.fbCeiling) / mLoopPeakGain
            : std::min(mFeedback, kMaxFeedback);

        for (int i = 0; i < numSamples; ++i)
        {
            mMixZ += mSmoothK * (mMix - mMixZ);
            mWidthZ += mSmoothK * (mWidth - mWidthZ);

            const double wv = (double)mWow.value(), fv = (double)mFlutter.value();
            double modMs;
            if (mTapeOn)
            {
                // Tape transport wander: periodic wow + flutter scaled by the
                // character, plus a slow random drift (worn capstan/motor).
                mDriftZ += (double)mDriftK * ((double)mDrift.value() - mDriftZ); // smooth S&H -> walk
                modMs = (double)mVoicing.wowMul * kWowDepthMs * wv
                        + (double)mVoicing.flutterMul * kFlutterDepthMs * fv
                        + (double)mVoicing.driftDepthMs * mDriftZ;
                mDrift.advance();
            }
            else
            {
                modMs = (double)mModAmt * (kWowDepthMs * wv + kFlutterDepthMs * fv);
            }
            mWow.advance();
            mFlutter.advance();

            const float dryL = left[i];
            const float dryR = stereo ? right[i] : dryL;

            // Per-side wet read. Tape glides the read position (pitch swoop on a
            // time/sync change); Digital crossfades old<->new fixed positions (no
            // repitch). Wow/Flutter modulates the read in both modes.
            float wetL = readWet(0, (double)targetMsL, modMs);
            float wetR = stereo ? readWet(1, (double)targetMsR, modMs) : wetL;

            // Tape head bump: a low resonant rise (constructive reflection at the
            // head), in the loop so it builds across repeats. Tape characters only.
            if (mTapeOn && mHeadOn)
            {
                wetL = mHeadBump[0].processSample(wetL);
                wetR = stereo ? mHeadBump[1].processSample(wetR) : wetL;
            }

            // In-loop tone shaping, re-applied every repeat: high-cut (Tone) then
            // low-cut, so the two together act as a feedback band-pass.
            if (mToneOn)
            {
                wetL = mTone[0].processSample(wetL);
                wetR = stereo ? mTone[1].processSample(wetR) : wetL;
            }
            if (mLowCutOn)
            {
                wetL = mLowCut[0].processSample(wetL);
                wetR = stereo ? mLowCut[1].processSample(wetR) : wetL;
            }

            // Tape saturation (record-level soft-clip) on 2nd-order ADAA. It sits
            // INSIDE the loop so it compresses hot repeats and BOUNDS self-
            // oscillation (the level can't run away), and the ADAA keeps aliasing
            // from compounding on every repeat. Tape characters only.
            if (mTapeOn && mVoicing.satDrive > 0.0f)
            {
                wetL = tapeSat(0, wetL);
                wetR = stereo ? tapeSat(1, wetR) : wetL;
            }

            if (dual)
            {
                // Independent L/R delays (e.g. dotted-1/8 left, 1/4 right). Each
                // side feeds back into itself; ping-pong doesn't apply here.
                mLine[0].write(dryL + fb * wetL);
                mLine[1].write(dryR + fb * wetR);
            }
            else if (mPingPong && stereo)
            {
                // Mono-summed input enters L; L bounces to R; R feeds back to L.
                mLine[0].write(0.5f * (dryL + dryR) + fb * wetR);
                mLine[1].write(wetL);
            }
            else
            {
                mLine[0].write(dryL + fb * wetL);
                if (stereo)
                    mLine[1].write(dryR + fb * wetR);
            }

            // OUTPUT-once tape shaping (post-feedback, NOT recirculated -> shapes the
            // timbre without compounding down the tail): bass thinning low-shelf, then
            // FET-preamp brightness high-shelf. Tape characters only.
            if (mTapeOn && mOutBassOn)
            {
                wetL = mOutBass[0].processSample(wetL);
                wetR = stereo ? mOutBass[1].processSample(wetR) : wetL;
            }
            if (mTapeOn && mPreampOn)
            {
                wetL = mPreamp[0].processSample(wetL);
                wetR = stereo ? mPreamp[1].processSample(wetR) : wetL;
            }

            // Width: M/S scale on the wet signal only.
            if (stereo)
            {
                const float m = 0.5f * (wetL + wetR);
                const float s = 0.5f * (wetL - wetR) * mWidthZ;
                wetL = m + s;
                wetR = m - s;
            }

            left[i] = (1.0f - mMixZ) * dryL + mMixZ * wetL;
            if (stereo)
                right[i] = (1.0f - mMixZ) * dryR + mMixZ * wetR;
        }

        for (auto &t : mTone) // flush denormal filter state between blocks
        {
            if (std::abs(t.z1) < 1.0e-30f) t.z1 = 0.0f;
            if (std::abs(t.z2) < 1.0e-30f) t.z2 = 0.0f;
        }
        for (auto &t : mLowCut)
        {
            if (std::abs(t.z1) < 1.0e-30f) t.z1 = 0.0f;
            if (std::abs(t.z2) < 1.0e-30f) t.z2 = 0.0f;
        }
        if (mTapeOn)
        {
            for (auto *arr : {&mHeadBump, &mOutBass, &mPreamp})
                for (auto &t : *arr)
                {
                    if (std::abs(t.z1) < 1.0e-30f) t.z1 = 0.0f;
                    if (std::abs(t.z2) < 1.0e-30f) t.z2 = 0.0f;
                }
        }
    }

private:
    // Tape record-level soft-clip on 2nd-order ADAA (shared kernel, Saturation.h).
    // Transparent below ~1/satDrive, compresses above -> bounds the feedback loop.
    // Per-channel two-sample history; output is bounded and NaN-guarded.
    float tapeSat(int ch, float x)
    {
        const double drive = (double)mVoicing.satDrive;
        const double xb = (double)x * drive;
        const double y = sat::cubicADAA2(xb, mSatX1[(size_t)ch], mSatX2[(size_t)ch]);
        mSatX2[(size_t)ch] = mSatX1[(size_t)ch];
        mSatX1[(size_t)ch] = xb;
        const float out = (float)(y / drive);
        return std::isfinite(out) ? out : 0.0f;
    }

    // Configure the per-character loop filters (head bump + wet-output preamp
    // tilt) from the current voicing, preserving state across a swap (like
    // updateTone). No-ops with state cleared when the stage is off.
    void updateTapeFilters(bool force)
    {
        // Loop-gain normaliser: the largest linear gain the in-loop filters add (the
        // head-bump peak, lightly damped by the gap-loss LP at that frequency). The
        // feedback is divided by this so the knob's oscillation point isn't dragged
        // down by the bump. >= 1 so it only ever attenuates feedback.
        if (mTapeOn)
        {
            const double bump = std::pow(10.0, (double)mVoicing.headBumpDb / 20.0);
            const double lpAtBump = (mVoicing.gapLossHz > 0.0f)
                ? 1.0 / std::sqrt(1.0 + std::pow((double)mVoicing.headBumpHz / (double)mVoicing.gapLossHz, 4.0))
                : 1.0;
            mLoopPeakGain = std::max(1.0f, (float)(bump * lpAtBump));
        }
        else
            mLoopPeakGain = 1.0f;
        mHeadOn = mTapeOn && mVoicing.headBumpDb != 0.0f && mVoicing.headBumpHz > 0.0f;     // IN-LOOP bloom
        mOutBassOn = mTapeOn && mVoicing.outBassDb != 0.0f && mVoicing.outBassHz > 0.0f;    // OUTPUT bass thin
        mPreampOn = mTapeOn && mVoicing.preampShelfDb != 0.0f && mVoicing.preampShelfHz > 0.0f; // OUTPUT HF lift
        if (mHeadOn || force)
        {
            const auto c = (mHeadOn)
                ? Biquad::peaking(mFs, std::min((double)mVoicing.headBumpHz, 0.45 * mFs),
                                  (double)mVoicing.headBumpQ, (double)mVoicing.headBumpDb)
                : Biquad::identity();
            for (auto &t : mHeadBump) { const float z1 = t.z1, z2 = t.z2; t = c; t.z1 = z1; t.z2 = z2; }
        }
        if (mOutBassOn || force)
        {
            const auto c = (mOutBassOn)
                ? Biquad::lowshelf(mFs, std::min((double)mVoicing.outBassHz, 0.45 * mFs),
                                   (double)mVoicing.outBassDb)
                : Biquad::identity();
            for (auto &t : mOutBass) { const float z1 = t.z1, z2 = t.z2; t = c; t.z1 = z1; t.z2 = z2; }
        }
        if (mPreampOn || force)
        {
            const auto c = (mPreampOn)
                ? Biquad::highshelf(mFs, std::min((double)mVoicing.preampShelfHz, 0.45 * mFs),
                                    (double)mVoicing.preampShelfDb)
                : Biquad::identity();
            for (auto &t : mPreamp) { const float z1 = t.z1, z2 = t.z2; t = c; t.z1 = z1; t.z2 = z2; }
        }
    }

    // One side's wet tap, honouring the time mode. baseTargetMs = sync-resolved
    // base delay (no mod); modMs = wow/flutter offset, applied in both modes.
    float readWet(int ch, double baseTargetMs, double modMs)
    {
        const double fsK = 0.001 * mFs;
        if (mTimeMode == TimeMode::Tape)
        {
            // Glide the read position (double + snap; a float one-pole stalls an
            // ulp-starved ~0.1 ms short -> echo lands a few samples early).
            mBaseZ[(size_t)ch] += (double)mTimeK * (baseTargetMs - mBaseZ[(size_t)ch]);
            if (std::abs(baseTargetMs - mBaseZ[(size_t)ch]) < 1.0e-4)
                mBaseZ[(size_t)ch] = baseTargetMs;
            const double d = std::max(2.0, (mBaseZ[(size_t)ch] + modMs) * fsK);
            return mLine[(size_t)ch].readFrac(d - 1.0);
        }
        // Digital: on a base-time change, crossfade old->new FIXED positions (no
        // repitch). A deadband ignores sub-sample jitter so a steady setting reads
        // straight through; a real move (knob/sync) triggers a short crossfade.
        TapXfade &t = mTap[(size_t)ch];
        if (!t.fading)
        {
            if (std::abs(baseTargetMs - t.active) > kXfadeSnapMs)
            {
                t.prev = t.active;
                t.active = baseTargetMs;
                t.x = 0.0f;
                t.fading = true;
            }
            else
                t.active = baseTargetMs;
        }
        else
            t.active = baseTargetMs; // lock the incoming tap to the latest target
        const double dNew = std::max(2.0, (t.active + modMs) * fsK);
        const float wetNew = mLine[(size_t)ch].readFrac(dNew - 1.0);
        if (!t.fading)
            return wetNew;
        const double dPrev = std::max(2.0, (t.prev + modMs) * fsK);
        const float wetPrev = mLine[(size_t)ch].readFrac(dPrev - 1.0);
        t.x += mXfadeInc;
        const float xx = std::min(1.0f, t.x);
        if (t.x >= 1.0f)
            t.fading = false;
        const float g0 = std::cos(xx * 1.57079633f), g1 = std::sin(xx * 1.57079633f);
        return g0 * wetPrev + g1 * wetNew; // equal-power -> steady level through the fade
    }

    void updateTone(bool force)
    {
        // Tape: REMAP the High Cut knob's position onto a tape-useful span so the
        // whole travel is live. Tape's natural roll-off is low, so the clean
        // 1k-20k knob range would otherwise sit mostly above it and feel inert.
        // The knob's default (~8 kHz) lands at ~2 kHz (the voiced tape tone); full
        // down ~kTapeHiCutMin (very dark), full up ~kTapeHiCutMax (brighter than
        // the real unit, by choice). Clean is unchanged (literal Hz).
        float corner = mToneHz;
        if (mTapeOn)
        {
            const double lo = std::log(1000.0), hi = std::log(20000.0);
            const double pos = (std::log((double)std::clamp(mToneHz, 1000.0f, 20000.0f)) - lo) / (hi - lo);
            const double cl = std::log((double)kTapeHiCutMin), ch = std::log((double)kTapeHiCutMax);
            corner = (float)std::exp(cl + pos * (ch - cl));
        }
        mToneOn = corner < 20000.0f;
        if (mToneOn || force)
        {
            const auto coeffs = Biquad::lowpass(mFs, std::min((double)corner, 0.45 * mFs));
            for (auto &t : mTone)
            {
                const float z1 = t.z1, z2 = t.z2; // keep state, swap coefficients
                t = coeffs;
                t.z1 = z1;
                t.z2 = z2;
            }
        }
    }

    void updateLowCut(bool force)
    {
        // The in-loop Low Cut is the user's feedback high-pass (both characters).
        // Tape bass thinning is a SEPARATE once-at-output low-shelf (updateTapeFilters),
        // so it doesn't compound down the tail.
        mLowCutOn = mLowCutHz > kMinLowCutHz;
        if (mLowCutOn || force)
        {
            const auto coeffs = Biquad::highpass(mFs, std::clamp((double)mLowCutHz, 20.0, 0.45 * mFs));
            for (auto &t : mLowCut)
            {
                const float z1 = t.z1, z2 = t.z2; // keep state, swap coefficients
                t = coeffs;
                t.z1 = z1;
                t.z2 = z2;
            }
        }
    }

    double mFs = 48000.0, mBpm = 120.0;
    std::array<FracDelayLine, 2> mLine;
    std::array<Biquad, 2> mTone, mLowCut;
    std::array<Biquad, 2> mHeadBump;          // tape: IN-LOOP low-mid bloom
    std::array<Biquad, 2> mOutBass, mPreamp;  // tape: OUTPUT-once bass thin + HF lift
    Lfo mWow, mFlutter, mDrift;               // mDrift = slow random transport wander (tape)

    float mTimeMs = 350.0f, mFeedback = 0.35f, mToneHz = 8000.0f;
    float mLowCutHz = 20.0f; // feedback high-pass (Low Cut); <= kMinLowCutHz = off
    float mWidth = 1.0f, mModAmt = 0.0f, mMix = 0.25f;
    int mSyncIdx = 0, mSyncIdxR = 0; // mSyncIdxR: 0 = Link (mirror L)
    bool mPingPong = false, mToneOn = true, mLowCutOn = false, mPrepared = false;
    bool mTapeOn = false, mHeadOn = false, mOutBassOn = false, mPreampOn = false; // tape stage gates
    float mLoopPeakGain = 1.0f; // tape: in-loop peak gain, normalises feedback so the knob feel is sane

    // Time-change behaviour + per-channel state.
    static constexpr double kXfadeSnapMs = 0.3; // base move under this = no crossfade (deadband)
    struct TapXfade { double active = 350.0, prev = 350.0; float x = 1.0f; bool fading = false; };
    TimeMode mTimeMode = TimeMode::Tape;     // processor bakes the clean delay to Digital
    double mBaseZ[2] = {350.0, 350.0};       // Tape: glided base delay (ms) per side
    TapXfade mTap[2];                        // Digital: crossfade state per side
    float mXfadeInc = 0.001f;                // per-sample ramp for the digital crossfade

    // Delay-character state. Default Clean -> mTapeOn false -> every stage above
    // is skipped and the engine is byte-for-byte the original delay.
    Character mCharacter = Character::Clean;
    DelayVoicing mVoicing = voicingFor(Character::Clean);
    double mSatX1[2] = {0.0, 0.0}, mSatX2[2] = {0.0, 0.0}; // tape saturation ADAA history
    double mDriftZ = 0.0;                                  // smoothed random drift state
    float mDriftK = 0.001f;                                // drift smoother coefficient

    float mMixZ = 0.25f, mWidthZ = 1.0f;
    float mTimeK = 0.001f, mSmoothK = 0.01f;
};

} // namespace nam_rig
