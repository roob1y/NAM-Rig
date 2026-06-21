// platefdn_driver.cpp — offline render of the COMMITTED PlateFdn engine
// (verbatim copy from src/rig/ReverbBlock.h + the FracDelayLine / reverb_detail
// it depends on, all JUCE-free). Reads a stereo float32 input, writes stereo
// float32 wet. Params via env: FDN_T60, FDN_DAMP, FDN_SIZE, FDN_PRE, FDN_NFRAMES.
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cmath>
#include <vector>
#include <array>
#include <algorithm>
#include <string>

// ---- FracDelayLine (verbatim from src/rig/Lfo.h) --------------------------
class FracDelayLine {
public:
    void prepare(int maxDelaySamples){ int size=16; while(size<maxDelaySamples+8) size<<=1;
        mBuf.assign((size_t)size,0.0f); mMask=(uint32_t)size-1; mW=0; mMaxDelay=maxDelaySamples; }
    void reset(){ std::fill(mBuf.begin(),mBuf.end(),0.0f); }
    void write(float x){ mW=(mW+1)&mMask; mBuf[mW]=x; }
    float readInt(int d) const { return mBuf[(mW-(uint32_t)d)&mMask]; }
    float readFrac(double delaySamples) const {
        const int di=(int)delaySamples; const float t=(float)(delaySamples-di);
        const float xm1=readInt(di-1),x0=readInt(di),x1=readInt(di+1),x2=readInt(di+2);
        const float c=(x1-xm1)*0.5f,v=x0-x1,w=c+v,a=w+v+(x2-x0)*0.5f,b=w+a;
        return ((((a*t)-b)*t+c)*t+x0); }
private:
    std::vector<float> mBuf; uint32_t mMask=0,mW=0; int mMaxDelay=0;
};

// ---- reverb_detail (verbatim from src/rig/ReverbBlock.h) -------------------
namespace reverb_detail {
constexpr double kPi = 3.14159265358979323846;
inline float onePole(double hz,double fs){ const double fc=std::min(std::max(hz,1.0),0.45*fs);
    return (float)(1.0-std::exp(-2.0*kPi*fc/fs)); }
inline float allpassInt(FracDelayLine&line,int len,float g,float x){
    const float z=line.readInt(len); const float y=-g*x+z; line.write(x+g*y); return y; }
inline void flush(float&z){ if(std::abs(z)<1.0e-30f) z=0.0f; }
}

// ===== PlateFdn (verbatim from src/rig/ReverbBlock.h) ======================
class PlateFdn {
public:
    static constexpr int kNumLines = 32;
    static constexpr std::array<double, kNumLines> kLineMs = {
        6.5,  7.9,  9.2, 10.6, 12.1, 13.7, 15.2, 16.8, 18.5, 20.1, 21.8,
        23.6, 25.3, 27.1, 29.0, 30.8, 32.7, 34.6, 36.6, 38.5, 40.5, 42.6,
        44.7, 46.8, 49.0, 51.2, 53.4, 55.7, 58.0, 60.4, 62.8, 65.3};
    static constexpr std::array<double, 6> kDiffMs = {0.5, 0.9, 1.5, 2.3, 3.3, 4.7};
    static constexpr std::array<double, kNumLines> kDispMs = {
        0.7, 1.0, 1.3, 1.6, 0.8, 1.1, 1.4, 1.7, 0.9, 1.2, 1.5, 1.8, 2.1, 2.4, 2.0, 2.3,
        2.6, 2.9, 1.9, 2.2, 2.5, 2.8, 3.1, 3.4, 2.7, 3.0, 3.3, 1.5, 1.8, 2.1, 2.4, 2.7};
    static constexpr float kDispG = 0.62f;
    static constexpr std::array<double, kNumLines> kDispMs2 = {
        1.9, 2.3, 1.1, 2.7, 1.5, 3.1, 1.3, 2.9, 1.7, 2.5, 1.0, 3.3, 1.4, 2.1, 2.8, 1.2,
        3.0, 1.6, 2.4, 1.8, 3.2, 1.5, 2.6, 2.0, 1.1, 3.4, 1.9, 2.2, 1.3, 2.7, 1.6, 3.1};
    static constexpr float kDispG2 = 0.55f;
    static constexpr float kMinSize = 0.8f, kMaxSize = 1.6f;

    void prepare(double fs){
        mFs = fs;
        for (int i=0;i<kNumLines;++i)
            mLine[(size_t)i].prepare((int)std::ceil(kLineMs[(size_t)i]*kMaxSize*0.001*mFs)+8);
        for (size_t i=0;i<mDiff.size();++i)
            mDiff[i].prepare((int)std::ceil(kDiffMs[i]*kMaxSize*0.001*mFs)+8);
        for (int i=0;i<kNumLines;++i){
            mDisp[(size_t)i].prepare((int)std::ceil(kDispMs[(size_t)i]*0.001*mFs)+8);
            mDisp2[(size_t)i].prepare((int)std::ceil(kDispMs2[(size_t)i]*0.001*mFs)+8);
        }
        mPredelay.prepare((int)std::ceil(0.2*mFs)+8);
        hadamardRow(1,mSignL); hadamardRow(2,mSignR); hadamardRow(13,mInj);
        updateGeometry(); reset(); mPrepared=true;
    }
    void reset(){
        for(auto&l:mLine)l.reset(); for(auto&d:mDiff)d.reset();
        for(auto&d:mDisp)d.reset(); for(auto&d:mDisp2)d.reset();
        mPredelay.reset(); std::fill(mDampState.begin(),mDampState.end(),0.0f);
        mBw1=mBw2=0.0f; mLcL=mLcR=0.0f;
    }
    void setSize(float s){ s=std::clamp(s,kMinSize,kMaxSize); if(s!=mSize){mSize=s; if(mPrepared)updateGeometry();} }
    void setDecaySeconds(float t60){ t60=std::max(0.1f,t60); if(t60!=mT60){mT60=t60; if(mPrepared)updateGeometry();} }
    void setDampHz(float hz){ if(hz!=mDampHz){mDampHz=hz; if(mPrepared)updateGeometry();} }
    void setPredelayMs(float ms){ mPredelayMs=std::clamp(ms,0.0f,200.0f); }
    void setFreeze(bool f){ mFreeze=f; }

    void process(float*left,float*right,int numSamples){
        using namespace reverb_detail;
        const bool stereo=(left!=right);
        const int pre=(int)std::round((double)mPredelayMs*0.001*mFs);
        const float inGain=mFreeze?0.0f:1.0f;
        const float invsq=1.0f/std::sqrt((float)kNumLines);
        for(int n=0;n<numSamples;++n){
            const float dryL=left[n]; const float dryR=stereo?right[n]:dryL;
            mPredelay.write(0.5f*(dryL+dryR));
            float x=inGain*mPredelay.readInt(std::max(1,pre));
            mBw1+=mDrvK*(x-mBw1); mBw2+=mDrvK*(mBw1-mBw2); x=mBw2;
            for(size_t a=0;a<mDiff.size();++a) x=allpassInt(mDiff[a],mDiffLen[a],0.62f,x);
            std::array<float,kNumLines> o;
            for(int i=0;i<kNumLines;++i){
                float r=mLine[(size_t)i].readInt(mLen[(size_t)i]);
                if(mDampOn){auto&z=mDampState[(size_t)i]; z+=mDampK*(r-z); r=z;}
                o[(size_t)i]=r;
            }
            float wetL=0.0f,wetR=0.0f;
            for(int i=0;i<kNumLines;++i){ wetL+=mSignL[i]*o[(size_t)i]; wetR+=mSignR[i]*o[(size_t)i]; }
            const float early=mEarly*x;
            float oL=invsq*wetL+early, oR=invsq*wetR+early;
            mLcL+=mLcK*(oL-mLcL); oL-=mLcL;
            mLcR+=mLcK*(oR-mLcR); oR-=mLcR;
            left[n]=oL; if(stereo)right[n]=oR;
            std::array<float,kNumLines> fb;
            for(int i=0;i<kNumLines;++i){
                float v=(mFreeze?1.0f:mGain[(size_t)i])*o[(size_t)i];
                v=allpassInt(mDisp[(size_t)i],mDispLen[(size_t)i],kDispG,v);
                v=allpassInt(mDisp2[(size_t)i],mDispLen2[(size_t)i],kDispG2,v);
                fb[(size_t)i]=v;
            }
            fwhtN(fb.data());
            const float injIn=0.5f*x;
            for(int i=0;i<kNumLines;++i)
                mLine[(size_t)i].write(invsq*fb[(size_t)i]+(float)mInj[i]*injIn);
        }
        for(auto&z:mDampState)reverb_detail::flush(z);
        reverb_detail::flush(mBw1); reverb_detail::flush(mBw2);
    }
private:
    static void fwhtN(float*a){
        for(int len=1;len<kNumLines;len<<=1)
            for(int i=0;i<kNumLines;i+=len<<1)
                for(int j=i;j<i+len;++j){ const float x=a[j],y=a[j+len]; a[j]=x+y; a[j+len]=x-y; }
    }
    static void hadamardRow(int row,int*out){
        for(int i=0;i<kNumLines;++i){ int b=0,m=row&i; while(m){b^=1;m&=m-1;} out[i]=b?-1:1; }
    }
    void updateGeometry(){
        using namespace reverb_detail;
        for(int i=0;i<kNumLines;++i){
            int len=(int)std::round(kLineMs[(size_t)i]*(double)mSize*0.001*mFs); len|=1;
            mLen[(size_t)i]=std::max(3,len);
            mDispLen[(size_t)i]=std::max(1,(int)std::round(kDispMs[(size_t)i]*0.001*mFs));
            mDispLen2[(size_t)i]=std::max(1,(int)std::round(kDispMs2[(size_t)i]*0.001*mFs));
            const int loopLen=mLen[(size_t)i]+mDispLen[(size_t)i]+mDispLen2[(size_t)i];
            mGain[(size_t)i]=(float)std::pow(10.0,-3.0*(double)loopLen/((double)mT60*mFs));
        }
        for(size_t a=0;a<mDiff.size();++a)
            mDiffLen[a]=std::max(2,(int)std::round(kDiffMs[a]*(double)mSize*0.001*mFs));
        mDampOn=mDampHz<15500.0f;
        mDampK=onePole(mDampHz,mFs);
        const double drv=std::clamp((double)mDampHz*1.4+2500.0,4000.0,13000.0);
        mDrvK=onePole(drv,mFs);
        mLcK=onePole(80.0,mFs);
    }
    double mFs=48000.0;
    std::array<FracDelayLine,kNumLines> mLine;
    std::array<FracDelayLine,6> mDiff;
    std::array<FracDelayLine,kNumLines> mDisp,mDisp2;
    FracDelayLine mPredelay;
    std::array<int,kNumLines> mLen{};
    std::array<int,6> mDiffLen{};
    std::array<int,kNumLines> mDispLen{},mDispLen2{};
    std::array<float,kNumLines> mGain{};
    std::array<float,kNumLines> mDampState{};
    int mSignL[kNumLines]{},mSignR[kNumLines]{},mInj[kNumLines]{};
    float mBw1=0.0f,mBw2=0.0f,mLcL=0.0f,mLcR=0.0f;
    float mDrvK=1.0f,mDampK=1.0f,mLcK=0.0f,mEarly=0.30f;
    float mSize=1.2f,mT60=2.0f,mDampHz=6000.0f,mPredelayMs=0.0f;
    bool mDampOn=true,mPrepared=false,mFreeze=false;
};
constexpr std::array<double,PlateFdn::kNumLines> PlateFdn::kLineMs;
constexpr std::array<double,6> PlateFdn::kDiffMs;
constexpr std::array<double,PlateFdn::kNumLines> PlateFdn::kDispMs;
constexpr std::array<double,PlateFdn::kNumLines> PlateFdn::kDispMs2;

static double envd(const char*k,double def){ const char*v=getenv(k); return v?atof(v):def; }

int main(int argc,char**argv){
    if(argc<3){ fprintf(stderr,"usage: %s in.f32 out.f32\n",argv[0]); return 1; }
    FILE*fi=fopen(argv[1],"rb"); if(!fi){ perror("in"); return 1; }
    fseek(fi,0,SEEK_END); long bytes=ftell(fi); fseek(fi,0,SEEK_SET);
    long n=bytes/ (long)sizeof(float)/2; // stereo interleaved
    std::vector<float> in((size_t)n*2);
    fread(in.data(),sizeof(float),(size_t)n*2,fi); fclose(fi);
    long extra=(long)envd("FDN_NFRAMES",0); // pad tail
    long N=n+extra;
    std::vector<float> L((size_t)N,0.0f), R((size_t)N,0.0f);
    for(long i=0;i<n;++i){ L[(size_t)i]=in[(size_t)i*2]; R[(size_t)i]=in[(size_t)i*2+1]; }
    PlateFdn p; p.prepare(48000.0);
    p.setDecaySeconds((float)envd("FDN_T60",2.0));
    p.setDampHz((float)envd("FDN_DAMP",6000.0));
    p.setSize((float)envd("FDN_SIZE",1.2));
    p.setPredelayMs((float)envd("FDN_PRE",0.0));
    p.process(L.data(),R.data(),(int)N);
    std::vector<float> out((size_t)N*2);
    for(long i=0;i<N;++i){ out[(size_t)i*2]=L[(size_t)i]; out[(size_t)i*2+1]=R[(size_t)i]; }
    FILE*fo=fopen(argv[2],"wb"); fwrite(out.data(),sizeof(float),(size_t)N*2,fo); fclose(fo);
    fprintf(stderr,"rendered %ld frames -> %s (T60=%.2f damp=%.0f size=%.2f)\n",
        N,argv[2],envd("FDN_T60",2.0),envd("FDN_DAMP",6000.0),envd("FDN_SIZE",1.2));
    return 0;
}
