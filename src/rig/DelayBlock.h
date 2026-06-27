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
    // SpaceTape = the multi-head tape echo: the SAME tape voicing as
    // Tape, but three playback heads read one tape loop at 1x/2x/3x the base time,
    // selected in combinations by the head mode. Mono (authentic).
    enum class Character { Clean = 0, Tape = 1, SpaceTape = 2 };

    // multi-head tape echo mode dial: 11 echo modes + reverb-only (12 positions). Each is a head
    // combination (bit0=H1, bit1=H2, bit2=H3; 0 = reverb only, no echo). Modes 5-11
    // and Reverb also engage the spring -> handled by the rig's Spring reverb, wired
    // from the processor (spaceTapeReverbOn) when Space Tape is active.
    static constexpr int kNumHeadModes = 12;
    static constexpr std::array<int, 12> kStHeadMask = {
        0b001, 0b010, 0b100, 0b110,  // 1-4 echo only: H1, H2, H3, H2+3
        0b001, 0b010, 0b100,         // 5-7 +rev:      H1, H2, H3
        0b011, 0b110, 0b101, 0b111,  // 8-11 +rev:     H1+2, H2+3, H1+3, all
        0b000};                      // 12 reverb only (no echo)
    static bool spaceTapeReverbOn(int mode) { return mode >= 4; } // modes 5-11 + reverb-only
    // Real multi-head tape echo heads are ~1:1.9:2.76 (not the reissue's idealised 1:2:3).
    static constexpr std::array<float, 3> kHeadRatio = {1.0f, 1.9f, 2.76f};
    // Authentic head-1 (Repeat Rate) span; the Time knob remaps onto this for Space Tape.
    static constexpr float kStHead1MinMs = 69.0f, kStHead1MaxMs = 177.0f;

    // What a character switches on in the feedback loop. All-zero = behaves like
    // the clean delay (a plain transparent shaper). Every nonlinear/colour stage
    // here recirculates, so aliasing/level effects compound per repeat -- the
    // saturation runs 2nd-order ADAA for exactly this reason.
    struct DelayVoicing
    {
        TimeMode timeMode;     // time-change feel (Tape glide = pitch swoop)
        float satDrive;        // record level into the tape soft-clip; 0 = no saturation.
                               //   gentle below ~1/satDrive, compresses above -> bounds the loop.
        float satAsym;         // even-harmonic amount: 0 = symmetric (the odd-only cubic; pure
                               //   3rd-harmonic). >0 adds a clamped-cosh EVEN harmonic series
                               //   (strong 2nd, tapering 4th/6th) like real tape's asymmetric
                               //   record transfer -- the warmth the level-domain saturation
                               //   curve AND the magnitude spectra are both blind to. DC-blocked
                               //   in-loop so nothing accumulates. Fit to the reference harmonic
                               //   null (delay_analysis/null_probe.py).
        float headBumpHz;      // IN-LOOP low-mid "head bump" peak (builds the tape bloom across
        float headBumpDb;      //   repeats); 0 dB = off
        float headBumpQ;
        float gapLossHz;       // head-gap HF roll-off (IN-LOOP): caps the High Cut at this corner
                               //   (knob darkens further, never brighter); 0 = no cap.
        float outBassHz;       // OUTPUT-once bass shaping: corner/centre (applied once, NOT
        float outBassDb;       //   recirculated -> shapes timbre without compounding the tail).
        float outBassQ;        //   >0 = PEAKING cut at outBassHz (cancels the in-loop head-bump
                               //   on the single repeat so the bloom only lives in the tail);
                               //   0 = low-shelf (Space Tape's full low-end boost).
        float wowMul;          // multiplier on the base wow depth (kWowDepthMs)
        float flutterMul;      // multiplier on the base flutter depth (kFlutterDepthMs)
        float driftHz;         // slow random pitch drift rate (worn-transport wander); 0 = off
        float driftDepthMs;    //   drift depth in ms
        float preampShelfHz;   // OUTPUT-once presence PEAK (preamp brightness; a peaking band
        float preampShelfDb;   //   applied once, not recirculated); 0 dB = off
        float fbCeiling;       // effective feedback ceiling for this character (<= kMaxFeedbackHard)
    };

    static DelayVoicing voicingFor(Character c)
    {
        switch (c)
        {
        case Character::SpaceTape:
            // multi-head tape echo voicing, fit to a MEASURED reference (echo-only modes). It is
            // VERY different from the single-head Tape: flat/boosted bass (low-shelf BOOST, not
            // cut), a SMALLER low-mid bloom, and a BRIGHTER, gentler HF roll-off.
            //   in-loop: bump +2.5dB@300 (Q0.6); gap-loss 1-POLE ~2k (a gentle
            //   ~6dB/oct HF roll-off, vs the single-head Tape's 2-pole); sat 1.4.
            //   output-once: low-shelf +2.5dB@180 (the multi-head tape echo's full low end).
            //   Head ratios 1:1.9:2.76 (processSpaceTape).
            //          tm  sat  asym hbHz  hbDb  hbQ  gapHz  obHz  obDb  wowM   flutM  drHz  drMs   ppHz   ppDb fbCeil
            return { TimeMode::Tape, 1.4f, 0.0f, 300.0f, 2.5f, 0.6f, 2000.0f, 180.0f, 2.5f, 0.0f,
                     0.05f, 0.055f, 0.5f, 0.05f, 2500.0f, 0.0f, 1.10f };
        case Character::Tape:
            // Tape-echo voicing fit to a MEASURED tape-echo reference with the metric
            // battery (delay_analysis/), split into PER-PASS (in-loop) and ONCE
            // (output) stages from a multi-repeat tail capture (a single-repeat fit
            // alone gives the wrong tail). Fitted to the reference graphs:
            //   - PER-PASS (rep[n+1]/rep[n]): broad low-mid BLOOM +9.5dB @ 260Hz (Q0.50)
            //     + gap-loss HF roll (2-pole LP ~1.95 kHz). Drives the tail darken+bloom.
            //   - ONCE at output: bass-thinning low-shelf -8.5dB @ 560Hz + a presence
            //     PEAK +6dB @ 2.2kHz (a 2 kHz lift that falls again by 4 kHz, matching
            //     the reference HF lift -- a PEAKING band, NOT a rising shelf). Applied
            //     once, so they shape timbre without compounding down the tail.
            //   - saturation 1.2 (matches the reference level-sweep compression; the top
            //     step sits just under the soft-clip clamp). wow/flutter ~0.1% peak;
            //     Tape glide; fb ceiling 1.10 (the in-loop sat tames runaway).
            //   Residual ~3dB @ 250Hz (single/tilt) = the peak-bump-vs-shelf-cut floor.
            //   The in-loop head bump is BIG (+14 @260) -> the reference's strong tail bloom
            //   (per-pass ~+13.7 dB); the output-once stage CANCELS it on the single repeat
            //   with a matching PEAKING cut (-13 @260, same 0.35 Q) so the single echo stays
            //   flat at 250 with full lows, while the bloom only compounds down the tail.
            //   Preamp presence peak moved to 3.5 kHz (was a 2 kHz lift that doubled the
            //   single-repeat hump). gap-loss 2-pole ~2.1 kHz.
            //          sat   asym   hbHz  hbDb   hbQ   gapHz  obHz  obDb   obQ   wowM   flutM  drHz  drMs   ppHz   ppDb fbCeil
            return { TimeMode::Tape, 0.06f, 0.0015f, 260.0f, 14.0f, 0.35f, 2100.0f, 260.0f, -13.0f, 0.35f,
                     0.05f, 0.055f, 0.5f, 0.05f, 3500.0f, 4.0f, 1.10f };
        case Character::Clean:
        default:
            return { TimeMode::Digital, 0.0f, 0.0f, 0.0f, 0.0f, 0.7f, 0.0f, 0.0f, 0.0f, 0.0f,
                     0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, kMaxFeedback };
        }
    }

    const char *name() const override { return "Delay"; }

    void prepare(const BlockContext &ctx) override
    {
        mFs = ctx.sampleRate;
        // 3x headroom so Space Tape head 3 (reads at ~2.76x the base time) fits when
        // the base follows the full Time/Sync range.
        const int maxDelay =
            (int)std::ceil((3.0 * (kMaxTimeMs + kWowDepthMs + kFlutterDepthMs) + 4.0) * 0.001 * mFs);
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
        mDcX1[0] = mDcX1[1] = mDcY1[0] = mDcY1[1] = 0.0;      // tape asym DC-blocker state
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
        mTapeOn = (c != Character::Clean);        // tape voicing engaged for Tape + SpaceTape
        mMultiHead = (c == Character::SpaceTape); // multi-head read path
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
        setCharacter(c == (int)Character::SpaceTape ? Character::SpaceTape
                     : c == (int)Character::Tape ? Character::Tape : Character::Clean);
    }

    // Offline voicing-analysis hook: inject a tape voicing to audition / fit candidates
    // (used by delay_analysis/delay_render + delay_fit_staged.py). Call AFTER
    // setCharacter(Tape|SpaceTape). No effect on the default audio path -- the
    // processor never calls this, so Clean stays byte-for-byte the original engine.
    void setTapeVoicingOverride(const DelayVoicing &v)
    {
        mVoicing = v;
        if (mPrepared) { updateTapeFilters(false); updateTone(false); updateLowCut(false); }
    }
    DelayVoicing currentVoicing() const { return mVoicing; } // for verification
    // multi-head tape echo head combination (0..kNumHeadModes-1); only used by the SpaceTape character.
    void setHeadMode(int m) { mHeadMode = std::clamp(m, 0, kNumHeadModes - 1); }
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
        if (mMultiHead) { processSpaceTape(left, right, numSamples); return; }
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
    // Multi-head tape echo: one tape loop read by up to 3 playback heads at
    // 1x/2x/3x the base time (the head mode picks the combo), summed, shaped by the
    // shared tape playback EQ + record saturation, and re-recorded for feedback.
    // Mono (authentic). Reuses the tape voicing, glide, wow/flutter and the
    // bump-normalised feedback. The base (head 1) time = the Repeat Rate (Time knob).
    void processSpaceTape(float *left, float *right, int numSamples)
    {
        const bool stereo = (left != right);
        const int mask = kStHeadMask[(size_t)std::clamp(mHeadMode, 0, kNumHeadModes - 1)];
        int nActive = 0;
        float leadingRatio = 1.0f; // ratio of the lowest active head (for sync snapping)
        bool gotLead = false;
        for (int h = 0; h < 3; ++h)
            if (mask & (1 << h))
            {
                ++nActive;
                if (!gotLead) { leadingRatio = kHeadRatio[(size_t)h]; gotLead = true; }
            }
        const float tapGain = nActive > 0 ? 1.0f / std::sqrt((float)nActive) : 1.0f;

        // Head 1 = Repeat Rate; heads 2/3 follow at the fixed ratios. FREE: the Time
        // knob maps onto the authentic multi-head tape echo tape-speed span (~69-177ms head 1; from
        // the service-manual 12-40 cm/s capstan + the 1:1.9:2.76 head spacing). SYNC:
        // the LEADING ACTIVE head lands on the host division (base = division /
        // leadingRatio), like the multi-head tape echo emulations, so single-head modes sit on the grid.
        const float baseTarget = (mSyncIdx > 0)
            ? currentTimeMs() / leadingRatio
            : kStHead1MinMs + (std::clamp(mTimeMs, kMinTimeMs, kMaxTimeMs) - kMinTimeMs)
                              / (kMaxTimeMs - kMinTimeMs) * (kStHead1MaxMs - kStHead1MinMs);
        const float fb = std::min(mFeedback, mVoicing.fbCeiling) / mLoopPeakGain;
        const double fsK = 0.001 * mFs;

        for (int i = 0; i < numSamples; ++i)
        {
            mMixZ += mSmoothK * (mMix - mMixZ);
            const double wv = (double)mWow.value(), fv = (double)mFlutter.value();
            mDriftZ += (double)mDriftK * ((double)mDrift.value() - mDriftZ);
            const double modMs = (double)mVoicing.wowMul * kWowDepthMs * wv
                               + (double)mVoicing.flutterMul * kFlutterDepthMs * fv
                               + (double)mVoicing.driftDepthMs * mDriftZ;
            mDrift.advance(); mWow.advance(); mFlutter.advance();

            // Glide the base time -> tape-speed pitch ramp on a Repeat-Rate change.
            mBaseZ[0] += (double)mTimeK * ((double)baseTarget - mBaseZ[0]);
            if (std::abs((double)baseTarget - mBaseZ[0]) < 1.0e-4) mBaseZ[0] = baseTarget;
            const double base = mBaseZ[0];

            const float dry = stereo ? 0.5f * (left[i] + right[i]) : left[i]; // mono sum in

            // Sum active playback heads (head k reads at k x base; wow/flutter scales
            // with head distance, so a further head wobbles k x as much).
            float wet = 0.0f;
            for (int h = 0; h < 3; ++h)
                if (mask & (1 << h))
                {
                    const double d = std::max(2.0, (double)kHeadRatio[(size_t)h] * (base + modMs) * fsK);
                    wet += mLine[0].readFrac6(d - 1.0);
                }
            wet *= tapGain;

            // Shared tape playback EQ (head bump + gap-loss + low cut) then record sat.
            if (mHeadOn) wet = mHeadBump[0].processSample(wet);
            if (mToneOn) wet = mTone[0].processSample(wet);
            if (mLowCutOn) wet = mLowCut[0].processSample(wet);
            wet = tapeSat(0, wet);

            mLine[0].write(dry + fb * wet); // re-record (feedback)

            float outw = wet; // output-once shaping (not recirculated)
            if (mOutBassOn) outw = mOutBass[0].processSample(outw);
            if (mPreampOn) outw = mPreamp[0].processSample(outw);

            // Reverb-only mode (no heads): pass dry through so the rig Spring has
            // signal (otherwise full Mix would mute it).
            const float o = (nActive == 0) ? dry : (1.0f - mMixZ) * dry + mMixZ * outw;
            left[i] = o;
            if (stereo) right[i] = o; // authentic mono
        }

        for (auto *arr : {&mTone, &mLowCut, &mHeadBump, &mOutBass, &mPreamp})
            for (auto &t : *arr)
            {
                if (std::abs(t.z1) < 1.0e-30f) t.z1 = 0.0f;
                if (std::abs(t.z2) < 1.0e-30f) t.z2 = 0.0f;
            }
    }

    // Tape record-level soft-clip on 2nd-order ADAA (shared kernel, Saturation.h).
    // Transparent below ~1/satDrive, compresses above -> bounds the feedback loop.
    // Per-channel two-sample history; output is bounded and NaN-guarded.
    float tapeSat(int ch, float x)
    {
        const double drive = (double)mVoicing.satDrive;
        const double asym  = (double)mVoicing.satAsym;       // even-harmonic (2nd) amount
        const double xb = (double)x * drive;
        const double y = sat::cubicADAA2(xb, mSatX1[(size_t)ch], mSatX2[(size_t)ch]);
        mSatX2[(size_t)ch] = mSatX1[(size_t)ch];
        mSatX1[(size_t)ch] = xb;
        double yb = y / drive;
        if (asym != 0.0)
        {
            // The odd cubic alone is 3rd-harmonic only; real tape's warmth is an EVEN
            // harmonic SERIES (strong 2nd, tapering 4th/6th) from an asymmetric record
            // transfer. A clamped cosh adds exactly that smooth even series -- a bare
            // square would give only a lone 2nd. Taken from the PRE-drive sample so the
            // even amount is set by satAsym alone (independent of the bounding drive) and
            // stays bounded in the loop; the in-loop gap-loss LP band-limits it so it does
            // not alias; a one-pole DC blocker removes its DC so nothing accumulates down
            // the tail. Symmetric tape (asym 0) skips this -> bit-identical to before.
            const double xc = (double)x > 1.0 ? 1.0 : ((double)x < -1.0 ? -1.0 : (double)x);
            const double in = yb + asym * (std::cosh(kEvenShape * xc) - 1.0);
            yb = in - mDcX1[(size_t)ch] + kDcBlockR * mDcY1[(size_t)ch];
            mDcX1[(size_t)ch] = in;
            mDcY1[(size_t)ch] = yb;
        }
        const float out = (float)yb;
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
                ? ((mVoicing.outBassQ > 0.0f)  // >0 = PEAKING cut (cancels the in-loop bump on
                                               //   the single pass while the bloom compounds);
                   ? Biquad::peaking(mFs, std::min((double)mVoicing.outBassHz, 0.45 * mFs),
                                     (double)mVoicing.outBassQ, (double)mVoicing.outBassDb)
                   : Biquad::lowshelf(mFs, std::min((double)mVoicing.outBassHz, 0.45 * mFs),
                                      (double)mVoicing.outBassDb))  // 0 = low-shelf (Space Tape)
                : Biquad::identity();
            for (auto &t : mOutBass) { const float z1 = t.z1, z2 = t.z2; t = c; t.z1 = z1; t.z2 = z2; }
        }
        if (mPreampOn || force)
        {
            // OUTPUT-once presence peak: a ~2 kHz lift that falls again above, matching the
            // measured reference HF lift (a peaking band, NOT a rising high-shelf). Q ~0.7.
            // SpaceTape leaves this off (preampShelfDb = 0), so only Tape is affected.
            const auto c = (mPreampOn)
                ? Biquad::peaking(mFs, std::min((double)mVoicing.preampShelfHz, 0.45 * mFs),
                                  0.7, (double)mVoicing.preampShelfDb)
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
        if (mTapeOn && mVoicing.gapLossHz > 0.0f)
        {
            // Remap the High Cut knob onto a per-character tape span centred so the
            // knob default (~8k) lands at the voiced gap-loss corner (gapLossHz).
            const double ln = std::log(1000.0), hn = std::log(20000.0);
            const double pos = (std::log((double)std::clamp(mToneHz, 1000.0f, 20000.0f)) - ln) / (hn - ln);
            const double lo = std::log(0.143 * (double)mVoicing.gapLossHz);
            const double hi = std::log(2.4 * (double)mVoicing.gapLossHz);
            corner = (float)std::exp(lo + pos * (hi - lo));
        }
        mToneOn = corner < 20000.0f;
        if (mToneOn || force)
        {
            // Space Tape uses a 1-pole gap-loss (the multi-head tape echo's gentle ~6 dB/oct HF
            // roll-off); Tape/Clean keep the 2-pole High Cut.
            const double cz = std::min((double)corner, 0.45 * mFs);
            const auto coeffs = (mCharacter == Character::SpaceTape)
                ? Biquad::lowpass1(mFs, cz)
                : Biquad::lowpass(mFs, cz);
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
    bool mMultiHead = false;    // SpaceTape: multi-head read path active
    int mHeadMode = 0;          // SpaceTape: head combination (index into kHeadMask)

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
    double mDcX1[2] = {0.0, 0.0}, mDcY1[2] = {0.0, 0.0};   // tape asym DC-blocker (only when satAsym>0)
    static constexpr double kDcBlockR = 0.9995;            // ~3.8 Hz one-pole DC blocker pole
    static constexpr double kEvenShape = 2.0;              // cosh even-harmonic richness (sets H4/H2 spread)
    double mDriftZ = 0.0;                                  // smoothed random drift state
    float mDriftK = 0.001f;                                // drift smoother coefficient

    float mMixZ = 0.25f, mWidthZ = 1.0f;
    float mTimeK = 0.001f, mSmoothK = 0.01f;
};

} // namespace nam_rig
