// render_character.cpp — offline, no-DAW render of ANY shipped reverb character,
// straight from the committed engine in src/rig/ReverbBlock.h (no verbatim copy, so
// it can never drift). Compiles the real ReverbBlock with a tiny JUCE stub.
//
//   build:  g++ -std=c++17 -O2 -I../src -Istub render_character.cpp -o render_character
//           (create stub/juce_audio_basics/juce_audio_basics.h containing "#pragma once")
//   run:    ./render_character <room|hall|plate|spring|shimmer|ambience|bloom> in.f32 out.f32
//   params (env): RV_T60 (decay s), RV_DAMP (Tone Hz), RV_SIZE (0.8..1.6),
//                 RV_PRE (predelay ms), RV_TENSION (spring), RV_SHIMMER, RV_PITCH (0..2)
//
// Renders the SHIPPED signal path (engine + the shared GuardMixer foolproofing) at
// 100% wet, width=1. That is what a user actually hears; the battery metrics
// (RT60(f), tonal balance, modal depth, C80, mid/side, EDR) are level-normalised so
// the makeup gain doesn't skew them. For a bare-engine render (no guardrails),
// instantiate the engine class directly instead (see platefdn_driver.cpp pattern).
#include "rig/ReverbBlock.h"
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>

using namespace nam_rig;

static double envd(const char* k, double dflt){ const char* v=getenv(k); return v?atof(v):dflt; }

int main(int argc, char** argv)
{
    if (argc < 4) { fprintf(stderr,"usage: %s <character> in.f32 out.f32\n",argv[0]); return 1; }
    std::string c = argv[1];
    int type = -1;
    if      (c=="room")     type = ReverbBlock::kRoom;
    else if (c=="hall")     type = ReverbBlock::kHall;
    else if (c=="plate")    type = ReverbBlock::kPlate;
    else if (c=="spring")   type = ReverbBlock::kSpring;
    else if (c=="shimmer")  type = ReverbBlock::kShimmer;
    else if (c=="ambience") type = ReverbBlock::kAmbience;
    else if (c=="bloom")    type = ReverbBlock::kBloom;
    else { fprintf(stderr,"unknown character '%s'\n",c.c_str()); return 1; }

    const double SR = 48000.0; const int BLK = 256;

    // read stereo float32 input (interleaved L,R)
    FILE* fi = fopen(argv[2],"rb"); if(!fi){ perror("in"); return 1; }
    fseek(fi,0,SEEK_END); long bytes=ftell(fi); fseek(fi,0,SEEK_SET);
    long n = bytes/ (long)(2*sizeof(float));
    std::vector<float> L((size_t)n), R((size_t)n);
    for (long i=0;i<n;++i){ float s[2]; if(fread(s,sizeof(float),2,fi)!=2){ n=i; L.resize((size_t)n); R.resize((size_t)n); break; } L[(size_t)i]=s[0]; R[(size_t)i]=s[1]; }
    fclose(fi);

    ReverbBlock v;
    v.setType(type);
    v.setMix(1.0f);             // 100% wet -> the IR
    v.setWidth(1.0f);           // neutral stereo
    v.setDecaySeconds((float)envd("RV_T60", 2.5));
    v.setDampHz((float)envd("RV_DAMP", 6000.0));
    v.setSize((float)envd("RV_SIZE", 1.2));
    v.setPredelayMs((float)envd("RV_PRE", 0.0));
    v.setTension((float)envd("RV_TENSION", 0.5));
    v.setShimmer((float)envd("RV_SHIMMER", 1.0));
    v.setPitch((int)envd("RV_PITCH", 0));
    v.prepare({SR, BLK});

    for (long p=0; p<n; p+=BLK){ int m=(int)std::min((long)BLK, n-p); v.process(L.data()+p, R.data()+p, m); }

    FILE* fo = fopen(argv[3],"wb"); if(!fo){ perror("out"); return 1; }
    for (long i=0;i<n;++i){ float s[2]={L[(size_t)i],R[(size_t)i]}; fwrite(s,sizeof(float),2,fo); }
    fclose(fo);
    fprintf(stderr,"rendered %s: %ld frames (%.2fs) -> %s\n", c.c_str(), n, n/SR, argv[3]);
    return 0;
}
