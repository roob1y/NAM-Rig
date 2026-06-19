// FDN-based studio spring "spring" prototype: the rig's 16-line FWHT FdnReverb (smooth,
// instantly-dense by design) wrapped in studio spring voicing — input darkening + presence
// + low-mid scoop, 2nd-order top rolloff, short predelay for articulate attack.
#include "src/ReverbBlock.h"
#include "src/Biquad.h"
#include "wavio.h"
#include <vector>
struct AP1{float x1=0,y1=0; inline float proc(float a,float x){float y=a*x+x1-a*y1;x1=x;y1=y;return y;}};
// stretched first-order allpass A(z^k)=(a+z^-k)/(1+a z^-k): k-sample delays -> big chirp
struct APk{ std::vector<float> xb,yb; int k=1,w=0; void set(int kk){k=std::max(1,kk);xb.assign(k,0.f);yb.assign(k,0.f);w=0;}
  inline float proc(float a,float x){ float xk=xb[w], yk=yb[w]; float y=a*x+xk-a*yk; xb[w]=x; yb[w]=y; if(++w>=k)w=0; return y; } };
#include <cstdlib>
using namespace std;
int main(int argc,char**argv){
    double decay=argc>1?atof(argv[1]):3.0;
    const char*out=argc>2?argv[2]:"/tmp/spring/fdn.wav";
    int sr=48000; size_t N=10*sr;
    auto ev=[&](const char*k,double d){const char*v=getenv(k);return v?atof(v):d;};
    N=(size_t)(ev("IMPLEN",10.0)*sr);
    nam_rig::FdnReverb fdn; fdn.prepare(sr);
    fdn.setSize((float)ev("SIZE",1.0)); fdn.setDecaySeconds((float)decay);
    fdn.setDampHz((float)ev("FDAMP",6500)); fdn.setPredelayMs((float)ev("PRE",8));
    fdn.setModDepth((float)ev("MOD",0.3)); fdn.setDiffusion((float)ev("DIFF",0.7));
    // voicing filters
    nam_rig::Biquad topcut=nam_rig::Biquad::lowpass(sr,(float)ev("HICUT",5000),(float)ev("HIQ",0.7));
    nam_rig::Biquad pres=nam_rig::Biquad::peaking(sr,(float)ev("PRESHZ",3000),0.9f,(float)ev("PRES",0));
    nam_rig::Biquad scoop=nam_rig::Biquad::peaking(sr,(float)ev("SCOOPHZ",450),0.8f,(float)ev("SCOOP",0));
    double darkK=1.0-exp(-2*M_PI*ev("DARK",3200)/sr); float darkZ=0;
    int Mdisp=(int)ev("DISP",0); float aDisp=(float)ev("APA",0.6); int Kstr=(int)ev("K",1); std::vector<APk> disp(Mdisp); for(auto&d:disp)d.set(Kstr);
    int earlyDiff=(int)ev("EARLY",0); float earlyG=(float)ev("EARLYG",0.7);
    float earlyTap=(float)ev("EARLYTAP",0.0); std::vector<float> vin(N,0.f);
    float earlyHp=(float)ev("EARLYHP",0.0); double earlyHpK=earlyHp>0?(1.0-exp(-2*M_PI*earlyHp/sr)):0.0; float ehzL=0,ehzR=0;
    float swellMs=(float)ev("SWELL",0.0); float swellK=swellMs>0? (float)(1.0-exp(-1.0/(swellMs*0.001*sr))):1.0f;
    float relK=(float)(1.0-exp(-1.0/(0.5*sr))); float swellEnv=0.f;
    nam_rig::FracDelayLine etL, etR; int etLlen=(int)std::round(0.0071*sr), etRlen=(int)std::round(0.0093*sr);
    etL.prepare(etLlen+8); etR.prepare(etRlen+8);
    const double edms[8]={2.4,3.6,5.1,7.3,9.7,12.9,16.1,19.7};
    std::vector<nam_rig::FracDelayLine> ed(8); std::vector<int> edlen(8);
    for(int k=0;k<8;++k){ edlen[k]=(int)std::round(edms[k]*0.001*sr); ed[k].prepare(edlen[k]+8); }
    vector<float> L,R; const char*inp=getenv("INWAV");
    if(inp){int isr; if(!wavRead(inp,L,R,isr)){printf("in fail\n");return 1;} N=L.size();}
    else {L.assign(N,0.f);R.assign(N,0.f);L[0]=1;R[0]=1;}
    // pre-voice the INPUT (darken sets centroid; scoop de-congests) before the FDN
    for(size_t i=0;i<N;++i){ float m=0.5f*(L[i]+R[i]);
        darkZ+=(float)darkK*(m-darkZ); float v=darkZ; v=scoop.processSample(v); v=pres.processSample(v);
        for(int k=0;k<Mdisp;++k) v=disp[k].proc(aDisp,v); // stretched-allpass dispersion = spring chirp
        for(int k=0;k<earlyDiff;++k){ float z=ed[k].readInt(edlen[k]); float y=-earlyG*v+z; ed[k].write(v+earlyG*y); v=y; } // dense early diffuser (size-independent)
        vin[i]=v;
        L[i]=v; R[i]=v; }
    const int B=256; for(size_t i=0;i<N;i+=B){int n=(int)min((size_t)B,N-i); fdn.process(&L[i],&R[i],n);}
    // band-limit the top of the wet (studio spring dies >~5k)
    nam_rig::Biquad tcR=topcut;
    for(size_t i=0;i<N;++i){
        if(swellMs>0){ float mag=0.5f*(fabsf(L[i])+fabsf(R[i])); float tgt=mag>1e-5f?1.f:0.f;
            swellEnv += (tgt>swellEnv?swellK:relK)*(tgt-swellEnv); L[i]*=swellEnv; R[i]*=swellEnv; } // FDN body blooms in
        float el=etL.readInt(etLlen); etL.write(vin[i]); float er=etR.readInt(etRlen); etR.write(vin[i]);
        if(earlyHp>0){ ehzL+=(float)earlyHpK*(el-ehzL); el-=ehzL; ehzR+=(float)earlyHpK*(er-ehzR); er-=ehzR; } // early tap = ping only (highs)
        L[i]=topcut.processSample(L[i]+earlyTap*el); R[i]=tcR.processSample(R[i]+earlyTap*er);
    }
    wavWrite(out,L,R,sr); printf("fdn %s decay=%.2f\n",out,decay); return 0;
}
