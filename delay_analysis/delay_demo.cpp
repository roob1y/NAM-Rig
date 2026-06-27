// delay_demo.cpp — render a real input (e.g. a guitar DI) through DelayBlock with
// optional tape-voicing overrides, to audition voicing candidates by ear. Uses the
// committed engine's setTapeVoicingOverride hook (no copy, can't drift).
//
//   build: g++ -std=c++17 -O2 -I../src -Istub delay_demo.cpp -o delay_demo
//   run:   ./delay_demo --char tape --in di.f32 --mix 1.0 --fb 0.45 [overrides] --out wet.f32
//   overrides: --hbDb --hbHz --hbQ --gapHz --obDb --obHz --ppDb --ppHz --sat --time
#include "rig/DelayBlock.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#include <algorithm>
using namespace nam_rig;
static const char* A(int c, char** v, const char* k, const char* d){ for (int i=1;i+1<c;++i) if(!std::strcmp(v[i],k)) return v[i+1]; return d; }
static float F(int c, char** v, const char* k, float d){ const char* s=A(c,v,k,nullptr); return s?(float)atof(s):d; }
int main(int argc, char** argv)
{
    const std::string ch = A(argc,argv,"--char","tape");
    const std::string in = A(argc,argv,"--in","");
    const std::string out = A(argc,argv,"--out","out.f32");
    const double SR = 48000.0; const int BLK = 512;
    DelayBlock d;
    DelayBlock::Character C = (ch=="space") ? DelayBlock::Character::SpaceTape : DelayBlock::Character::Tape;
    d.setCharacter(C);
    DelayBlock::DelayVoicing v = DelayBlock::voicingFor(C);
    v.headBumpDb    = F(argc,argv,"--hbDb",  v.headBumpDb);
    v.headBumpHz    = F(argc,argv,"--hbHz",  v.headBumpHz);
    v.headBumpQ     = F(argc,argv,"--hbQ",   v.headBumpQ);
    v.gapLossHz     = F(argc,argv,"--gapHz", v.gapLossHz);
    v.outBassDb     = F(argc,argv,"--obDb",  v.outBassDb);
    v.outBassHz     = F(argc,argv,"--obHz",  v.outBassHz);
    v.preampShelfDb = F(argc,argv,"--ppDb",  v.preampShelfDb);
    v.preampShelfHz = F(argc,argv,"--ppHz",  v.preampShelfHz);
    v.satDrive      = F(argc,argv,"--sat",   v.satDrive);
    d.setTapeVoicingOverride(v);
    d.setTimeMs(F(argc,argv,"--time",350.0f));
    d.setFeedback(F(argc,argv,"--fb",0.35f));
    d.setMix(F(argc,argv,"--mix",0.40f));
    d.setToneHz(8000.0f); d.setLowCutHz(20.0f); d.setWidth(0.0f); d.setModAmount(0.0f);
    d.prepare({SR,BLK});
    FILE* fi=std::fopen(in.c_str(),"rb"); if(!fi){ std::perror("in"); return 1; }
    std::fseek(fi,0,SEEK_END); long b=std::ftell(fi); std::fseek(fi,0,SEEK_SET);
    long n=b/(long)(2*sizeof(float)); std::vector<float> L(n),R(n);
    for(long i=0;i<n;++i){ float s[2]; if(std::fread(s,sizeof(float),2,fi)!=2){ n=i; L.resize(n); R.resize(n); break; } L[i]=s[0]; R[i]=s[1]; }
    std::fclose(fi);
    for(long p=0;p<n;p+=BLK) d.process(L.data()+p,R.data()+p,(int)std::min((long)BLK,n-p));
    FILE* fo=std::fopen(out.c_str(),"wb"); if(!fo){ std::perror("out"); return 1; }
    for(long i=0;i<n;++i){ float s[2]={L[i],R[i]}; std::fwrite(s,sizeof(float),2,fo); }
    std::fclose(fo);
    std::fprintf(stderr,"demo %s hb%.1f@%.0f gap%.0f ob%.1f@%.0f pp%.1f@%.0f sat%.2f -> %s\n",
        ch.c_str(),v.headBumpDb,v.headBumpHz,v.gapLossHz,v.outBassDb,v.outBassHz,v.preampShelfDb,v.preampShelfHz,v.satDrive,out.c_str());
    return 0;
}
