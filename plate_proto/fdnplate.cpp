#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <algorithm>
static double SR=48000;
struct Delay{ std::vector<float> b; int w=0,sz=0; void prep(int n){sz=1;while(sz<n+4)sz<<=1;b.assign(sz,0);w=0;}
  inline void write(float x){b[w]=x;w=(w+1)&(sz-1);} inline float read(int d)const{return b[(w-d)&(sz-1)];} };
struct AP{ Delay d; int len; float g; void prep(int n,float gg){d.prep(n+4);len=n;g=gg;}
  inline float proc(float x){float z=d.read(len);float y=-g*x+z;d.write(x+g*y);return y;} };
struct OnePole{ float z=0,k=0; void setHz(double f){k=(float)(1.0-std::exp(-2.0*M_PI*f/SR));} inline float lp(float x){z+=k*(x-z);return z;} };
static void fwht(float*a,int n){for(int len=1;len<n;len<<=1)for(int i=0;i<n;i+=len<<1)for(int j=i;j<i+len;j++){float x=a[j],y=a[j+len];a[j]=x+y;a[j+len]=x-y;}}
static float E(const char*k,float d){const char*v=getenv(k);return v?(float)atof(v):d;}
// primes for mutually-incommensurate delays
static int primeAt(int idx){ static const int P[64]={ 337,389,431,479,523,571,619,661,709,761,811,857,907,953,1009,1061,1103,1153,1201,1259,1301,1361,1409,1459,1511,1559,1607,1657,1709,1759,1811,1867,1913,1973,2017,2069,2113,2161,2213,2267,2309,2357,2411,2467,2521,2579,2621,2677,2729,2777,2833,2887,2939,2999,3041,3089,3137,3187,3251,3301,3347,3391,3457,3511}; return P[idx&63]; }
int main(int argc,char**argv){
  FILE*f=fopen(argv[1],"rb");fseek(f,0,SEEK_END);long by=ftell(f);fseek(f,0,SEEK_SET);
  long F=by/8;std::vector<float> in(F*2);fread(in.data(),4,F*2,f);fclose(f);
  int N=(int)E("FDN_N",32); float T60=E("FDN_T60",2.0f); float dampHz=E("FDN_DAMP",5500);
  float bright=E("FDN_BRIGHT",10000); int nID=(int)E("FDN_INDIFF",2); float bloom=E("FDN_BLOOM",1.0f);
  float hfRatio=E("FDN_HFRATIO",0.5f); // HF T60 = T60*hfRatio (lower=darker tail). density wants this not too low
  int Nf=1; while(Nf<N)Nf<<=1; N=Nf;
  // line delays from primes -> samples (prime sample counts = mutually incommensurate = dense, no shared modes)
  float SIZE=E("FDN_SIZE",1.0f); std::vector<int> dl(N); for(int i=0;i<N;i++){ int d=(int)std::round(primeAt(i)*SIZE); d|=1; dl[i]=d; }
  std::vector<Delay> line(N); for(int i=0;i<N;i++) line[i].prep(dl[i]+8);
  std::vector<OnePole> damp(N); std::vector<OnePole> blo(N);
  // Jot-style frequency-dependent decay: per line a one-pole lowpass-shelf, DC gain=glo
  // (T60LO) and Nyquist gain=ghi (T60HI) -> smooth monotonic T60(f): lows ring longest
  // (bloom), highs sustain shorter (bright by sustain, NO EQ -> no harshness).
  // DC decay long (bloom floor) + one-pole damping at dampF rolls highs down.
  // T60(DC)=T60LO ; highs decay faster above dampF -> smooth monotonic tilt, no EQ.
  float T60LO=E("FDN_T60LO",4.0f), dampF=E("FDN_DAMPF",1400.0f);
  float dk=(float)(1.0-std::exp(-2.0*M_PI*dampF/SR));
  std::vector<float> glo(N), ylp(N,0.0f);
  for(int i=0;i<N;i++) glo[i]=(float)std::pow(10.0,-3.0*dl[i]/(T60LO*SR));
  std::vector<AP> idf(std::max(0,nID)); { double ms[6]={5.4,8.1,11.7,3.3,6.9,9.8}; for(int i=0;i<nID;i++) idf[i].prep((int)std::round(ms[i%6]*0.001*SR),0.6f); }
  OnePole inLP; inLP.setHz(bright);
  float bodyDb=E("FDN_BODY",0.0f), bodyF=E("FDN_BODYF",280.0f), bodyQ=E("FDN_BODYQ",1.0f);
  // peaking bell (RBJ) for low-mid warmth without sub boost
  float bA=std::pow(10.0f,bodyDb/40.0f), bw0=2.0f*(float)M_PI*bodyF/(float)SR, bal=std::sin(bw0)/(2.0f*bodyQ), bc=std::cos(bw0);
  float bb0=1+bal*bA, bb1=-2*bc, bb2=1-bal*bA, ba0=1+bal/bA, ba1=-2*bc, ba2=1-bal/bA;
  float Bb0=bb0/ba0,Bb1=bb1/ba0,Bb2=bb2/ba0,Ba1=ba1/ba0,Ba2=ba2/ba0;
  float bx1L=0,bx2L=0,by1L=0,by2L=0,bx1R=0,bx2R=0,by1R=0,by2R=0;
  float lcHz=E("FDN_LOWCUT",0.0f); float lcK=lcHz>0?(float)(1.0-std::exp(-2.0*M_PI*lcHz/SR)):0.0f; float hcL=0,hcR=0,hcL2=0,hcR2=0;
  // per-line input & output sign vectors (decorrelated). deterministic pseudo-random.
  std::vector<float> sin_(N), sL(N), sR(N);
  unsigned r=12345u; auto rnd=[&](){ r=r*1664525u+1013904223u; return (float)((r>>9)&1?1.0:-1.0); };
  for(int i=0;i<N;i++) sin_[i]=rnd(); for(int i=0;i<N;i++) sL[i]=rnd(); for(int i=0;i<N;i++) sR[i]=rnd();
  std::vector<float> o(F*2,0), s(N), fb(N);
  float invsq=1.0f/std::sqrt((float)N);
  for(long n=0;n<F;n++){
    float x=0.5f*(in[2*n]+in[2*n+1]); x=inLP.lp(x); for(int i=0;i<nID;i++) x=idf[i].proc(x);
    for(int i=0;i<N;i++) s[i]=line[i].read(dl[i]);
    float wl=0,wr=0; for(int i=0;i<N;i++){ wl+=sL[i]*s[i]; wr+=sR[i]*s[i]; }
    float ol=wl*invsq, orr=wr*invsq;
    { float xn=ol, yn=Bb0*xn+Bb1*bx1L+Bb2*bx2L-Ba1*by1L-Ba2*by2L; bx2L=bx1L;bx1L=xn;by2L=by1L;by1L=yn; ol=yn;
      float xr=orr, yr=Bb0*xr+Bb1*bx1R+Bb2*bx2R-Ba1*by1R-Ba2*by2R; bx2R=bx1R;bx1R=xr;by2R=by1R;by1R=yr; orr=yr; }
    if(lcK>0){ hcL+=lcK*(ol-hcL); ol-=hcL; hcL2+=lcK*(ol-hcL2); ol-=hcL2;
                hcR+=lcK*(orr-hcR); orr-=hcR; hcR2+=lcK*(orr-hcR2); orr-=hcR2; } o[2*n]=ol; o[2*n+1]=orr;
    for(int i=0;i<N;i++){ ylp[i]+=dk*(s[i]-ylp[i]); fb[i]=glo[i]*ylp[i]; }
    fwht(fb.data(),N); for(int i=0;i<N;i++) fb[i]*=invsq;    // orthonormal mix
    float ing=0.5f; for(int i=0;i<N;i++) line[i].write(fb[i]+sin_[i]*ing*x);
  }
  FILE*go=fopen(argv[2],"wb");fwrite(o.data(),4,F*2,go);fclose(go);
  fprintf(stderr,"FDN N=%d T60=%.1f damp=%.0f hfRatio=%.2f bright=%.0f indiff=%d\n",N,T60,dampHz,hfRatio,bright,nID);
  return 0;
}
