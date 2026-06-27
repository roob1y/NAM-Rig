// delay_render.cpp — offline, no-DAW render of the NAM Rig delay CHARACTERS,
// straight from the committed engine in src/rig/DelayBlock.h (no verbatim copy,
// so it can never drift). Compiles the real DelayBlock with a tiny JUCE stub.
//
//   build:  g++ -std=c++17 -O2 -I../src -Istub delay_render.cpp -o delay_render
//           (create stub/juce_audio_basics/juce_audio_basics.h containing "#pragma once")
//   run:    ./delay_render --char tape|space --mode N --test impulse|levels|sustain|tail \
//                          [--time MS] [--fb F] [--impAmp A] [voicing overrides] --out x.f32
//
// Output is interleaved stereo float32 (L,R); the tape characters are MONO (L==R),
// written to both channels so the Python battery (reshape -1,2) reads it like the
// reverb renders. 100% wet (mix=1) so the file IS the echo train; High Cut knob at
// its default (~8 kHz) so the in-loop gap-loss sits at the VOICED corner; Low Cut off.
//
// Test signals mirror the private wet reference captures in delay_references/:
//   impulse  unit (or --impAmp) impulse -> a clean single repeat (single-repeat spectrum)
//   tail     unit impulse, higher feedback -> long repeat train (per-pass transfer)
//   levels   5 geometric 440 Hz tone-burst steps (300 ms on / 200 ms off) (saturation)
//   sustain  long continuous 440 Hz tone at feedback 0 -> ONE clean delayed tap (wow/flutter)
//
// VOICING OVERRIDES (optional; default = the committed voicingFor). Used by the
// fitters to audition candidate Tape/Space voicings without editing the engine:
//   --hbDb --hbHz --hbQ --gapHz --obDb --obHz --ppDb --ppHz --sat
// For the single-repeat spectrum, render the impulse at a LOW --impAmp (e.g. 0.12)
// so the echo is near-linear, matching a feedback-down quiet reference impulse
// (a unit impulse over-saturates its own bump and skews the output-once fit).
#include "rig/DelayBlock.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>

using namespace nam_rig;

static const char* argval(int c, char** v, const char* k, const char* d)
{
    for (int i = 1; i + 1 < c; ++i) if (std::strcmp(v[i], k) == 0) return v[i + 1];
    return d;
}
static float OV(int c, char** v, const char* k, float d)
{
    const char* s = argval(c, v, k, nullptr); return s ? (float)atof(s) : d;
}

int main(int argc, char** argv)
{
    const std::string ch = argval(argc, argv, "--char", "tape");
    const std::string test = argval(argc, argv, "--test", "impulse");
    const int mode = std::atoi(argval(argc, argv, "--mode", "0"));
    const float timeMs = OV(argc, argv, "--time", 350.0f);
    const float impAmp = OV(argc, argv, "--impAmp", 0.5f);  // match the reference dry impulse (0.5)
    const char* fbArg = argval(argc, argv, "--fb", "");
    const std::string out = argval(argc, argv, "--out", "out.f32");
    // Sync drive: the multi-head reference captures are SYNCED (head 1 = 250 ms = 1/8 @120,
    // above the free-mode 69-177 ms cap), so the Space Tape fit must drive the engine in sync.
    // --sync N indexes kSyncBeats (9 = 1/8); --bpm sets the tempo. 0 = free (the old behaviour).
    const int syncIdx = std::atoi(argval(argc, argv, "--sync", "0"));
    const float bpm = OV(argc, argv, "--bpm", 120.0f);

    const double SR = 48000.0;
    const int BLK = 512;

    DelayBlock d;
    DelayBlock::Character C = (ch == "space") ? DelayBlock::Character::SpaceTape
                            : (ch == "clean") ? DelayBlock::Character::Clean
                                              : DelayBlock::Character::Tape;
    d.setCharacter(C);
    if (ch == "space") d.setHeadMode(mode);

    // optional voicing overrides (default = the committed voicing)
    if (C != DelayBlock::Character::Clean)
    {
        DelayBlock::DelayVoicing v = DelayBlock::voicingFor(C);
        v.headBumpDb    = OV(argc, argv, "--hbDb",  v.headBumpDb);
        v.headBumpHz    = OV(argc, argv, "--hbHz",  v.headBumpHz);
        v.headBumpQ     = OV(argc, argv, "--hbQ",   v.headBumpQ);
        v.gapLossHz     = OV(argc, argv, "--gapHz", v.gapLossHz);
        v.outBassDb     = OV(argc, argv, "--obDb",  v.outBassDb);
        v.outBassQ      = OV(argc, argv, "--obQ",   v.outBassQ);
        v.outBassHz     = OV(argc, argv, "--obHz",  v.outBassHz);
        v.preampShelfDb = OV(argc, argv, "--ppDb",  v.preampShelfDb);
        v.preampShelfHz = OV(argc, argv, "--ppHz",  v.preampShelfHz);
        v.satDrive      = OV(argc, argv, "--sat",   v.satDrive);
        v.satAsym       = OV(argc, argv, "--asym",  v.satAsym);
        v.loopHpHz      = OV(argc, argv, "--loopHp", v.loopHpHz);
        v.wowMul        = OV(argc, argv, "--wowM",  v.wowMul);
        v.flutterMul    = OV(argc, argv, "--flutM", v.flutterMul);
        d.setTapeVoicingOverride(v);
    }

    d.setTimeMs(timeMs);
    d.setToneHz(8000.0f);   // default knob -> voiced gap-loss corner (no extra darkening)
    d.setLowCutHz(OV(argc, argv, "--loCut", 20.0f)); // user Low Cut utility (default off here)
    d.setWidth(0.0f);       // mono wet
    d.setMix(1.0f);         // 100% wet -> the echo train
    d.setModAmount(0.0f);   // tape characters drive their own wow/flutter
    d.setBpm((double)bpm); d.setSyncIndex(syncIdx); // sync N -> currentTimeMs = beat*60000/bpm

    float fb; double durSec;
    if      (test == "impulse") { fb = 0.0f;  durSec = 2.5; }   // feedback down -> single repeat
    else if (test == "tail")    { fb = 0.55f; durSec = 9.0; }
    else if (test == "levels")  { fb = 0.0f;  durSec = 2.95; }   // single pass -> clean saturation harmonics
    else if (test == "sustain") { fb = 0.0f;  durSec = 5.0; }
    else { std::fprintf(stderr, "unknown --test '%s'\n", test.c_str()); return 1; }
    if (fbArg[0]) fb = (float)std::atof(fbArg);
    d.setFeedback(fb);

    d.prepare({SR, BLK});

    // settle the glide / drift smoothers on 1 s of silence
    {
        std::vector<float> z((size_t)SR, 0.0f), z2 = z;
        for (size_t p = 0; p < z.size(); p += BLK)
            d.process(z.data() + p, z2.data() + p, (int)std::min<size_t>(BLK, z.size() - p));
    }

    const size_t N = (size_t)(durSec * SR);
    std::vector<float> L(N, 0.0f), R;

    if (test == "impulse")      { L[0] = impAmp; }
    else if (test == "tail")    { L[0] = 1.0f; }
    else if (test == "levels")
    {
        // 1 kHz tone (matches the measured reference levels capture), stepping up to
        // the TRUE calibrated operating ceiling ~0.30 (the reference's hottest dry burst),
        // NOT an arbitrary 0.80 -- so the saturation knee + harmonic character are exercised
        // at the real operating point. The harmonic null lives in the top burst.
        const float amp[5] = {0.05f, 0.10f, 0.16f, 0.22f, 0.30f};
        const size_t onN = (size_t)(0.30 * SR), cyc = (size_t)(0.50 * SR), edge = (size_t)(0.005 * SR);
        for (int s = 0; s < 5; ++s)
        {
            const size_t base = (size_t)s * cyc;
            for (size_t i = 0; i < onN && base + i < N; ++i)
            {
                float w = 1.0f;
                if (i < edge) w = 0.5f * (1.0f - std::cos((float)M_PI * i / edge));
                else if (i > onN - edge) w = 0.5f * (1.0f - std::cos((float)M_PI * (onN - i) / edge));
                L[base + i] = amp[s] * w * std::sin(2.0f * (float)M_PI * 1000.0f * i / (float)SR);
            }
        }
    }
    else // sustain — long continuous tone (10 ms fade-in only)
    {
        const size_t edge = (size_t)(0.010 * SR);
        for (size_t i = 0; i < N; ++i)
        {
            float w = (i < edge) ? 0.5f * (1.0f - std::cos((float)M_PI * i / edge)) : 1.0f;
            L[i] = 0.22f * w * std::sin(2.0f * (float)M_PI * 440.0f * i / (float)SR);
        }
    }
    R = L;

    for (size_t p = 0; p < N; p += BLK)
        d.process(L.data() + p, R.data() + p, (int)std::min<size_t>(BLK, N - p));

    FILE* fo = std::fopen(out.c_str(), "wb");
    if (!fo) { std::perror("out"); return 1; }
    for (size_t i = 0; i < N; ++i) { float s[2] = {L[i], R[i]}; std::fwrite(s, sizeof(float), 2, fo); }
    std::fclose(fo);
    std::fprintf(stderr, "rendered %s/%s mode%d time%.0f fb%.2f -> %s\n",
                 ch.c_str(), test.c_str(), mode, timeMs, fb, out.c_str());
    return 0;
}
