// delay_null.cpp — process an ARBITRARY dry input through the real DelayBlock
// (Tape/Space/Clean) and write the wet output, so a null test can drive ours
// with the EXACT dry signal that produced a wet reference capture (delay_ref/ ->
// delay_references/). Same voicing-override CLI as delay_render.cpp so the fitter
// can audition candidates against the null residual, not just the magnitude.
//
//   build: g++ -std=c++17 -O2 -I src -I /tmp/stub delay_analysis/delay_null.cpp -o delay_null
//   run:   ./delay_null --char tape --in dry.f32 --out wet.f32 --fb 0.0 \
//                       [--time MS] [--sat S --hbDb .. --ppDb ..]
//
// in.f32 / out.f32 are mono float32 (one channel). 100% wet (mix=1) so the file
// IS the echo train, matching the fully-wet reference captures. High Cut at the
// knob default (-> voiced gap-loss corner), Low Cut off, width 0 (mono).
#include "rig/DelayBlock.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#include <algorithm>

using namespace nam_rig;
static const char* argval(int c, char** v, const char* k, const char* d)
{ for (int i = 1; i + 1 < c; ++i) if (!std::strcmp(v[i], k)) return v[i + 1]; return d; }
static float OV(int c, char** v, const char* k, float d)
{ const char* s = argval(c, v, k, nullptr); return s ? (float)atof(s) : d; }

int main(int argc, char** argv)
{
    const std::string ch  = argval(argc, argv, "--char", "tape");
    const std::string in  = argval(argc, argv, "--in", "in.f32");
    const std::string out = argval(argc, argv, "--out", "out.f32");
    const float timeMs    = OV(argc, argv, "--time", 350.0f);
    const float fb        = OV(argc, argv, "--fb", 0.0f);
    const double SR = 48000.0; const int BLK = 512;

    // read mono float32 input
    FILE* fi = std::fopen(in.c_str(), "rb");
    if (!fi) { std::perror("in"); return 1; }
    std::fseek(fi, 0, SEEK_END); long bytes = std::ftell(fi); std::fseek(fi, 0, SEEK_SET);
    std::vector<float> x((size_t)(bytes / 4));
    if (std::fread(x.data(), 4, x.size(), fi) != x.size()) { std::fclose(fi); return 1; }
    std::fclose(fi);

    DelayBlock d;
    DelayBlock::Character C = (ch == "space") ? DelayBlock::Character::SpaceTape
                            : (ch == "clean") ? DelayBlock::Character::Clean
                                              : DelayBlock::Character::Tape;
    d.setCharacter(C);
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
        v.wowMul        = OV(argc, argv, "--wowM",  v.wowMul);
        v.flutterMul    = OV(argc, argv, "--flutM", v.flutterMul);
        d.setTapeVoicingOverride(v);
    }
    d.setTimeMs(timeMs);
    d.setToneHz(8000.0f);
    d.setLowCutHz(20.0f);
    d.setWidth(0.0f);
    d.setMix(1.0f);
    d.setModAmount(0.0f);
    d.setFeedback(fb);
    d.prepare({SR, BLK});

    // settle smoothers on 1 s of silence (matches delay_render)
    { std::vector<float> z((size_t)SR, 0.0f), z2 = z;
      for (size_t p = 0; p < z.size(); p += BLK)
          d.process(z.data() + p, z2.data() + p, (int)std::min<size_t>(BLK, z.size() - p)); }

    std::vector<float> L = x, R = x;
    for (size_t p = 0; p < L.size(); p += BLK)
        d.process(L.data() + p, R.data() + p, (int)std::min<size_t>(BLK, L.size() - p));

    FILE* fo = std::fopen(out.c_str(), "wb");
    if (!fo) { std::perror("out"); return 1; }
    std::fwrite(L.data(), 4, L.size(), fo);   // mono out
    std::fclose(fo);
    std::fprintf(stderr, "null: %s in=%zu fb=%.2f -> %s\n", ch.c_str(), L.size(), fb, out.c_str());
    return 0;
}
