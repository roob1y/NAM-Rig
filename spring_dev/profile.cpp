// IR profiler: octave-band energy/tilt, per-band RT60 (Schroeder), NED (Abel-Huang),
// L/R correlation, spectral centroid (from band energies). Reads float32 / PCM16/24/32 WAV.
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>
using namespace std;
static constexpr double PI=3.14159265358979323846;

struct Wav{int sr=0,ch=0;vector<float>L,R;};
static bool readWav(const char*path,Wav&w){
    FILE*f=fopen(path,"rb"); if(!f){printf("open fail %s\n",path);return false;}
    fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET);
    vector<uint8_t> d(n); if(fread(d.data(),1,n,f)!=(size_t)n){fclose(f);return false;} fclose(f);
    if(memcmp(d.data(),"RIFF",4)||memcmp(d.data()+8,"WAVE",4))return false;
    size_t i=12; int af=1,ch=2,sr=48000,bps=16; size_t dataOff=0,dataLen=0;
    while(i+8<=(size_t)n){
        char id[5]={0}; memcpy(id,&d[i],4);
        uint32_t sz; memcpy(&sz,&d[i+4],4);
        if(!memcmp(id,"fmt ",4)){ memcpy(&af,&d[i+8],2); memcpy(&ch,&d[i+10],2);
            memcpy(&sr,&d[i+12],4); memcpy(&bps,&d[i+22],2); }
        else if(!memcmp(id,"data",4)){ dataOff=i+8; dataLen=sz; }
        i+=8+sz+(sz&1);
    }
    w.sr=sr; w.ch=ch; int bytes=bps/8; size_t frames=dataLen/(ch*bytes);
    w.L.resize(frames); w.R.resize(frames);
    const uint8_t*p=&d[dataOff];
    auto rd=[&](size_t idx)->float{
        const uint8_t*s=p+idx*bytes;
        if(af==3){ if(bps==32){float v;memcpy(&v,s,4);return v;} double v;memcpy(&v,s,8);return (float)v;}
        if(bps==16){int16_t v;memcpy(&v,s,2);return v/32768.0f;}
        if(bps==24){int32_t v=(s[0]|(s[1]<<8)|(s[2]<<16)); if(v&0x800000)v|=~0xFFFFFF; return v/8388608.0f;}
        if(bps==32){int32_t v;memcpy(&v,s,4);return v/2147483648.0f;}
        return 0;
    };
    for(size_t fr=0;fr<frames;++fr){ w.L[fr]=rd(fr*ch); w.R[fr]=ch>1?rd(fr*ch+1):w.L[fr]; }
    return true;
}

// RBJ bandpass (constant skirt gain, peak=Q)
struct BP{double b0,b1,b2,a1,a2,x1=0,x2=0,y1=0,y2=0;
  void set(double f0,double Q,double sr){ double w=2*PI*f0/sr,a=sin(w)/(2*Q),c=cos(w),a0=1+a;
    b0=Q*a/a0; b1=0; b2=-Q*a/a0; a1=-2*c/a0; a2=(1-a)/a0; }
  double proc(double x){ double y=b0*x+b1*x1+b2*x2-a1*y1-a2*y2; x2=x1;x1=x;y2=y1;y1=y; return y; }
  void reset(){x1=x2=y1=y2=0;}
};

// RT60 via Schroeder backward integration, T20 fit (-5..-25 dB)
double rt60(const vector<float>&band,int sr){
    size_t N=band.size(); vector<double> e(N);
    double s=0; for(size_t i=0;i<N;++i) s+=(double)band[i]*band[i];
    if(s<=0) return 0;
    double acc=0; for(size_t i=0;i<N;++i){ size_t k=N-1-i; acc+=(double)band[k]*band[k]; e[k]=acc; }
    // dB curve normalized to start
    double e0=e[0]; if(e0<=0)return 0;
    // find -5 and -25 dB points
    int i5=-1,i25=-1;
    for(size_t i=0;i<N;++i){ double db=10*log10(e[i]/e0+1e-300);
        if(i5<0&&db<=-5)i5=(int)i; if(i25<0&&db<=-25){i25=(int)i;break;} }
    if(i5<0||i25<0||i25<=i5) return 0;
    double t20=(double)(i25-i5)/sr; return 3.0*t20; // T20->RT60
}

// Abel-Huang NED at a given center sample, window +/- half
double nedAt(const vector<float>&x,size_t center,int win){
    long a=(long)center-win/2, b=(long)center+win/2;
    if(a<0)a=0; if(b>(long)x.size())b=x.size(); int n=b-a; if(n<8)return 0;
    double s=0; for(long i=a;i<b;++i)s+=(double)x[i]*x[i]; double sd=sqrt(s/n);
    if(sd<=0)return 0; int cnt=0; for(long i=a;i<b;++i) if(fabs(x[i])>sd)cnt++;
    return (cnt/(double)n)/0.3173; // normalize by Gaussian expectation
}

int main(int argc,char**argv){
    if(argc<2){printf("usage: profile file.wav\n");return 1;}
    Wav w; if(!readWav(argv[1],w)){printf("read fail\n");return 1;}
    int sr=w.sr; size_t N=w.L.size();
    vector<float> mono(N); for(size_t i=0;i<N;++i)mono[i]=0.5f*(w.L[i]+w.R[i]);
    // trim leading silence to the impulse onset for fair NED/RT
    double pk=0; for(float v:mono)pk=max(pk,(double)fabs(v));
    size_t on=0; for(size_t i=0;i<N;++i) if(fabs(mono[i])>0.01*pk){on=i;break;}
    printf("== %s ==\n",argv[1]);
    printf("sr=%d ch=%d len=%.2fs onset=%.1fms peak=%.3f\n",sr,w.ch,N/(double)sr,on*1000.0/sr,pk);

    // octave bands
    double fc[]={63,125,250,500,1000,2000,4000,8000};
    int nb=8; double be[8]; double cz=0,cw=0;
    printf("band   energy(dB)  RT60(s)\n");
    for(int b=0;b<nb;++b){
        BP bp; bp.set(fc[b],1.4142,sr);
        vector<float> y(N); for(size_t i=0;i<N;++i)y[i]=(float)bp.proc(mono[i]);
        double e=0; for(size_t i=on;i<N;++i)e+=(double)y[i]*y[i];
        be[b]=e; double r=rt60(y,sr);
        printf("%5.0f  %9.2f   %6.3f\n",fc[b],10*log10(e+1e-300),r);
    }
    // normalize band energy to max for tilt readout
    double emax=0; for(int b=0;b<nb;++b)emax=max(emax,be[b]);
    printf("tilt(dB vs max): ");
    for(int b=0;b<nb;++b){ printf("%.0f:%+.1f ",fc[b],10*log10(be[b]/emax+1e-300)); cz+=fc[b]*be[b]; cw+=be[b]; }
    printf("\ncentroid(energy-weighted)=%.0f Hz\n", cw>0?cz/cw:0);

    // NED over time (diffusion onset)
    int win=(int)(0.020*sr);
    printf("NED @ ms: ");
    for(double ms:{5.0,10.0,20.0,40.0,80.0,160.0}){
        size_t c=on+(size_t)(ms*0.001*sr); if(c<N) printf("%.0f:%.2f ",ms,nedAt(mono,c,win));
    }
    // mixing time: first time NED>=0.9
    double mt=-1; for(double ms=2; ms<400; ms+=2){ size_t c=on+(size_t)(ms*0.001*sr); if(c>=N)break; if(nedAt(mono,c,win)>=0.9){mt=ms;break;} }
    printf("\nmixing_time(NED>=0.9)=%.0f ms\n", mt);

    // L/R correlation over tail (after onset+20ms)
    size_t a=on+(size_t)(0.02*sr); double sl=0,sr2=0,sc=0;
    for(size_t i=a;i<N;++i){ sl+=(double)w.L[i]*w.L[i]; sr2+=(double)w.R[i]*w.R[i]; sc+=(double)w.L[i]*w.R[i]; }
    double corr=(sl>0&&sr2>0)?sc/sqrt(sl*sr2):0;
    printf("LR_corr=%.3f\n\n",corr);
    return 0;
}
