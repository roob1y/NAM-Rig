// room3_v2.cpp (from room3_clean) - Dattorro/Griesinger allpass tank, DUAL-INSTANCE true stereo.
// HONEST REBUILD (2026-06-30): voiced ONLY through genuine architectural levers.
// NO decay-scaled damping (effDamp is a real constant), PLUS a clean DESIGNED full-band late-reverb stage (faithful convex two-slope tail, fit to per-band EDR),
// NO parallel ER FIR patch (the tank's 7-tap output IS the early field).
// Two independent mono-in->stereo-out cores: L->coreL (take L out), R->coreR (take R out).
// build: g++ -std=c++17 -O2 -I../src -Istub room3_clean.cpp -o /tmp/room_rebuild/roomv2
// env levers (all genuine): PRE BW SHELFG SHELFHZ EARLYSEND ESLP LATEPRE LATELOOPHP DECAY DDIF1 DDIF2 IDIF1 IDIF2 DAMP EXC SIZE LATEG LATEDECAY LATEDAMP
#include "rig/ReverbBlock.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace nam_rig;
static double E(const char*k,double d){const char*v=getenv(k);return v?atof(v):d;}

struct TankCore {
    double fs; int pre; float bw,decay,dd1,dd2,id1,id2,damp; double exc;
    FracDelayLine id[6]; int idLen[6];
    FracDelayLine apL1,dA,apL2,dB,apR1,dC,apR2,dD,predelay;
    int apL1Len,dAlen,apL2Len,dBlen,apR1Len,dClen,apR2Len,dDlen; int tap[14];
    float bwS,shpS,shK,shG,dpL,dpR,effDamp=0,earlySend=0,esLpK=1,esLpS=0; Lfo lfo1,lfo2;
    // clean full-band late-reverb stage (4-line Householder FDN, modulated, per-line damp)
    FracDelayLine lfdn[4],lfPre; int lfLen[4],lfPreLen=1; float lfFb[4], lfLp[4]={0,0,0,0}, lfLoop[4]={0,0,0,0}, lfDampK=0, lfLoopK=0, lfHpK=0, lfHpS=0, lateG=0; Lfo lfLfo;
    void prepare(double f,float seedphase){
        fs=f; double S=f/29761.0; double T=S*(float)E("SIZE",1.0);
        pre=(int)std::round(E("PRE",2.5)*0.001*f);
        bw=(float)E("BW",0.9995);decay=(float)E("DECAY",0.50);dd1=(float)E("DDIF1",0.70);dd2=(float)E("DDIF2",0.50);
        id1=(float)E("IDIF1",0.75);id2=(float)E("IDIF2",0.625);damp=(float)E("DAMP",0.6);exc=E("EXC",16.0)*S;
        effDamp=damp; // HONEST: genuine in-loop damping constant, NOT decay-scaled
        shG=(float)E("SHELFG",1.0); shK=(float)reverb_detail::onePole(E("SHELFHZ",3000.0),f); shpS=0; earlySend=(float)E("EARLYSEND",0.0); esLpK=(float)reverb_detail::onePole(E("ESLP",8000.0),f); esLpS=0; // early-send LP (darker early reflections)
        // late stage
        lateG=(float)E("LATEG",0.0); float ltdec=(float)E("LATEDECAY",8.0); lfDampK=(float)reverb_detail::onePole(E("LATEDAMP",6000.0),f); lfHpK=(float)reverb_detail::onePole(E("LATEHP",250.0),f); lfLoopK=(float)reverb_detail::onePole(E("LATELOOPHP",200.0),f);
        double ltm[4]={53.0,67.0,79.0,97.0}; for(int k=0;k<4;++k){lfLen[k]=(int)std::round(ltm[k]*0.001*f*(double)E("SIZE",1.0));lfdn[k].prepare(lfLen[k]+8);lfLp[k]=0;
            lfFb[k]=(float)std::clamp(std::pow(10.0,-3.0*ltm[k]/((double)ltdec*1000.0)),0.0,0.999);}
        lfLfo.prepare(f); lfLfo.setRateHz(0.31f); lfPreLen=std::max(1,(int)std::round(E("LATEPRE",0.0)*0.001*f)); lfPre.prepare(lfPreLen+8);
        int idl[6]={(int)std::round(142*S),(int)std::round(107*S),(int)std::round(379*S),(int)std::round(277*S),(int)std::round(193*S),(int)std::round(457*S)};
        for(int k=0;k<6;++k){idLen[k]=idl[k]+(int)std::round(seedphase*(3.0+2.0*k));id[k].prepare(idLen[k]+8);}
        apL1Len=(int)std::round(672*T);dAlen=(int)std::round(4453*T);apL2Len=(int)std::round(1800*T);dBlen=(int)std::round(3720*T);
        apR1Len=(int)std::round(908*T);dClen=(int)std::round(4217*T);apR2Len=(int)std::round(2656*T);dDlen=(int)std::round(3163*T);
        apL1.prepare(apL1Len+(int)exc+16);dA.prepare(dAlen+16);apL2.prepare(apL2Len+16);dB.prepare(dBlen+16);
        apR1.prepare(apR1Len+(int)exc+16);dC.prepare(dClen+16);apR2.prepare(apR2Len+16);dD.prepare(dDlen+16);
        predelay.prepare((int)std::ceil(0.1*f)+8);
        lfo1.prepare(f);lfo1.setRateHz(0.70f); lfo2.prepare(f);lfo2.setRateHz(0.50f);
        double tp[14]={266,2974,1913,1996,1990,187,1066, 353,3627,1228,2673,2111,335,121};
        for(int i=0;i<14;++i)tap[i]=(int)std::round(tp[i]*T);
        reset();
    }
    void reset(){for(auto&l:id)l.reset();apL1.reset();dA.reset();apL2.reset();dB.reset();apR1.reset();dC.reset();apR2.reset();dD.reset();predelay.reset();lfo1.reset();lfo2.reset();lfLfo.reset();bwS=0;shpS=0;esLpS=0;dpL=0;dpR=0;for(auto&l:lfdn)l.reset();lfPre.reset();for(auto&z:lfLp)z=0;for(auto&z:lfLoop)z=0;lfHpS=0;}
    static inline float ap(FracDelayLine&dl,int len,float g,float x){float z=dl.readInt(len);float y=-g*x+z;dl.write(x+g*y);return y;}
    static inline float apMod(FracDelayLine&dl,int len,float g,double mod,float x){float z=(float)dl.readFrac((double)len+mod);float y=-g*x+z;dl.write(x+g*y);return y;}
    // mono in -> stereo out (oL, oR)
    inline void process(float in,float&oL,float&oR){
        predelay.write(in); float x=predelay.readInt(pre);
        bwS += bw*(x-bwS); float sh=bwS; if(shG<1.0f){shpS += shK*(bwS-shpS); sh=shpS + shG*(bwS-shpS);} x=sh;
        x=ap(id[0],idLen[0],id1,x); x=ap(id[1],idLen[1],id1,x); x=ap(id[2],idLen[2],id2,x); x=ap(id[3],idLen[3],id2,x); x=ap(id[4],idLen[4],id2,x); x=ap(id[5],idLen[5],id2,x);
        float fromLeft=dB.readInt(dBlen), fromRight=dD.readInt(dDlen);
        float lt=x+decay*fromRight; lt=apMod(apL1,apL1Len,-dd1,exc*lfo1.value(0.0),lt);
        dA.write(lt); float a=dA.readInt(dAlen); dpL=a+effDamp*(dpL-a); a=dpL; lt=ap(apL2,apL2Len,dd2,decay*a); dB.write(lt);
        float rt=x+decay*fromLeft; rt=apMod(apR1,apR1Len,-dd1,exc*lfo2.value(0.0),rt);
        dC.write(rt); float c=dC.readInt(dClen); dpR=c+effDamp*(dpR-c); c=dpR; rt=ap(apR2,apR2Len,dd2,decay*c); dD.write(rt);
        oL = 0.6f*dC.readInt(tap[0])+0.6f*dC.readInt(tap[1])-0.6f*apR2.readInt(tap[2])+0.6f*dD.readInt(tap[3])-0.6f*dA.readInt(tap[4])-0.6f*apL2.readInt(tap[5])-0.6f*dB.readInt(tap[6]);
        oR = 0.6f*dA.readInt(tap[7])+0.6f*dA.readInt(tap[8])-0.6f*apL2.readInt(tap[9])+0.6f*dB.readInt(tap[10])-0.6f*dC.readInt(tap[11])-0.6f*apR2.readInt(tap[12])-0.6f*dD.readInt(tap[13]);
        if(lateG>0){
            // full-band late reverb: fed by diffused input x (bright, full-band), Householder 4x4
            float r0=lfdn[0].readInt(lfLen[0]), r1=lfdn[1].readInt(lfLen[1]);
            float r2=(float)lfdn[2].readFrac((double)lfLen[2]+1.5*lfLfo.value(0.0)), r3=lfdn[3].readInt(lfLen[3]);
            // per-line damping
            lfLp[0]+=lfDampK*(r0-lfLp[0]); r0=lfLp[0]; lfLp[1]+=lfDampK*(r1-lfLp[1]); r1=lfLp[1];
            lfLoop[0]+=lfLoopK*(r0-lfLoop[0]); r0-=lfLoop[0]; lfLoop[1]+=lfLoopK*(r1-lfLoop[1]); r1-=lfLoop[1];
            lfLp[2]+=lfDampK*(r2-lfLp[2]); r2=lfLp[2]; lfLp[3]+=lfDampK*(r3-lfLp[3]); r3=lfLp[3];
            lfLoop[2]+=lfLoopK*(r2-lfLoop[2]); r2-=lfLoop[2]; lfLoop[3]+=lfLoopK*(r3-lfLoop[3]); r3-=lfLoop[3];
            float sm=0.5f*(r0+r1+r2+r3); // Householder reflection y=x-2/N*sum
            float h0=r0-sm,h1=r1-sm,h2=r2-sm,h3=r3-sm;
            lfHpS+=lfHpK*(x-lfHpS); float lin=x-lfHpS; lfPre.write(lin); lin=lfPre.readInt(lfPreLen); // HP + predelay (sustain after attack)
            lfdn[0].write(lin+lfFb[0]*h0); lfdn[1].write(lin+lfFb[1]*h1); lfdn[2].write(lin+lfFb[2]*h2); lfdn[3].write(lin+lfFb[3]*h3);
            float lateL=h0-h1+h2-h3, lateR=h0+h1-h2-h3; // decorrelated L/R taps
            oL += lateG*0.5f*lateL; oR += lateG*0.5f*lateR;
            lfLfo.advance();
        }
        if(earlySend>0){ esLpS+=esLpK*(x-esLpS); float es=earlySend*esLpS; oL+=es; oR+=es; }
        lfo1.advance(); lfo2.advance();
    }
};
class Dattorro {
public:
    void prepare(double fs){ mCoreL.prepare(fs,0.0f); mCoreR.prepare(fs,0.37f); }
    void reset(){mCoreL.reset();mCoreR.reset();}
    void process(float*left,float*right,int n){
        const bool st=(left!=right);
        for(int s=0;s<n;++s){
            float inL=left[s], inR=(st?right[s]:left[s]);
            float lL,lR,rL,rR; mCoreL.process(inL,lL,lR); mCoreR.process(inR,rL,rR);
            left[s]=lL; if(st)right[s]=rR;
        }
    }
private:
    TankCore mCoreL,mCoreR;
};
int main(int argc,char**argv){
    if(argc<3){fprintf(stderr,"usage: %s in.f32 out.f32\n",argv[0]);return 1;}
    const double SR=48000;const int BLK=256;
    FILE*fi=fopen(argv[1],"rb");fseek(fi,0,SEEK_END);long b=ftell(fi);fseek(fi,0,SEEK_SET);
    long nn=b/(long)(2*sizeof(float));std::vector<float>L(nn),R(nn);
    for(long i=0;i<nn;++i){float ss[2];if(fread(ss,sizeof(float),2,fi)!=2){nn=i;L.resize(nn);R.resize(nn);break;}L[i]=ss[0];R[i]=ss[1];}
    fclose(fi);
    Dattorro v;v.prepare(SR);
    for(long p=0;p<nn;p+=BLK){int m=(int)std::min((long)BLK,nn-p);v.process(L.data()+p,R.data()+p,m);}
    FILE*fo=fopen(argv[2],"wb");for(long i=0;i<nn;++i){float ss[2]={L[i],R[i]};fwrite(ss,sizeof(float),2,fo);}fclose(fo);
    fprintf(stderr,"roomc dual: %ld frames\n",nn);return 0;
}
