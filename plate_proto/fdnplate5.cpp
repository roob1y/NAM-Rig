// fdnplate2 — professional-recipe plate: 32-line FWHT FDN with a per-line
// MULTI-BAND absorptive damping filter fit to the studio T60(f) curve, plus a
// dense early-reflection stage to shape the FRONT of the EDR.
//
// Key design fact (why the old single-pole missed): for every line to agree on
// ONE global T60(f), each line's per-loop loss in dB must scale with its delay
// length m_i. So we give every line the SAME absorption SHAPE (derived from the
// target curve) scaled by m_i:  loopLoss_dB(f) = -60 * m_i / (SR * T60(f)).
// We realise that shape with a broadband gain (sets DC/bloom decay) followed by
// a gentle cascade of RBJ high-shelf + peak biquads carving the rising HF loss.
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
// Transposed-direct-form-II biquad (RBJ)
struct BQ{ float b0=1,b1=0,b2=0,a1=0,a2=0,z1=0,z2=0;
  inline float proc(float x){ float y=b0*x+z1; z1=b1*x-a1*y+z2; z2=b2*x-a2*y; return y; } };
static void mkHighShelf(BQ&q,double f,double gdb){ double A=pow(10.0,gdb/40.0),w=2*M_PI*f/SR,c=cos(w),s=sin(w);
  double S=1.0, al=s/2*sqrt((A+1/A)*(1/S-1)+2); double sa=2*sqrt(A)*al;
  double a0=(A+1)-(A-1)*c+sa;
  q.b0=(float)(A*((A+1)+(A-1)*c+sa)/a0); q.b1=(float)(-2*A*((A-1)+(A+1)*c)/a0);
  q.b2=(float)(A*((A+1)+(A-1)*c-sa)/a0); q.a1=(float)(2*((A-1)-(A+1)*c)/a0);
  q.a2=(float)(((A+1)-(A-1)*c-sa)/a0); }
static void mkPeak(BQ&q,double f,double Q,double gdb){ double A=pow(10.0,gdb/40.0),w=2*M_PI*f/SR,c=cos(w),s=sin(w),al=s/(2*Q);
  double a0=1+al/A; q.b0=(float)((1+al*A)/a0); q.b1=(float)((-2*c)/a0); q.b2=(float)((1-al*A)/a0);
  q.a1=(float)((-2*c)/a0); q.a2=(float)((1-al/A)/a0); }
static void mkLP(BQ&q,double f,double Q){ double w=2*M_PI*f/SR,c=cos(w),s=sin(w),al=s/(2*Q);
  double a0=1+al; q.b0=(float)(((1-c)/2)/a0); q.b1=(float)((1-c)/a0); q.b2=q.b0;
  q.a1=(float)((-2*c)/a0); q.a2=(float)((1-al)/a0); }
struct OnePole{ float z=0,k=0; void setHz(double f){k=(float)(1.0-std::exp(-2.0*M_PI*f/SR));} inline float lp(float x){z+=k*(x-z);return z;} };
static void fwht(float*a,int n){for(int len=1;len<n;len<<=1)for(int i=0;i<n;i+=len<<1)for(int j=i;j<i+len;j++){float x=a[j],y=a[j+len];a[j]=x+y;a[j+len]=x-y;}}
static float E(const char*k,float d){const char*v=getenv(k);return v?(float)atof(v):d;}
static int primeAt(int idx){ static const int P[64]={ 337,389,431,479,523,571,619,661,709,761,811,857,907,953,1009,1061,1103,1153,1201,1259,1301,1361,1409,1459,1511,1559,1607,1657,1709,1759,1811,1867,1913,1973,2017,2069,2113,2161,2213,2267,2309,2357,2411,2467,2521,2579,2621,2677,2729,2777,2833,2887,2939,2999,3041,3089,3137,3187,3251,3301,3347,3391,3457}; return P[idx&63]; }
int main(int argc,char**argv){
  FILE*f=fopen(argv[1],"rb");fseek(f,0,SEEK_END);long by=ftell(f);fseek(f,0,SEEK_SET);
  long F=by/8;std::vector<float> in(F*2);size_t rd=fread(in.data(),4,F*2,f);(void)rd;fclose(f);
  int N=(int)E("FDN_N",64);
  int Nf=1; while(Nf<N)Nf<<=1; N=Nf;
  float SIZE=E("FDN_SIZE",1.5f); std::vector<int> dl(N); for(int i=0;i<N;i++){ int d=(int)std::round(primeAt(i)*SIZE); d|=1; dl[i]=d; }
  std::vector<Delay> line(N); for(int i=0;i<N;i++) line[i].prep(dl[i]+8);

  // --- per-line absorptive damping fit to the studio T60(f) ---
  // Broadband gain sets the DC (bloom) decay; the shelf/peak cascade adds the
  // EXTRA hf loss so the realised T60 follows the target down to 1.6s at 11k.
  // Defaults below are the FITTED solution that overlays the vintage plate 2.0s
  // T60(f) (design_damp.py, measured to within ~0.05s of the real IR per band).
  float DC60=E("FDN_DC60",4.6822f);               // T60 at DC (s) = bloom floor
  // High-shelf #1 + #2 (two corners) + a peak carve the rising HF loss. dB
  // values are EXTRA loss per 1000 samples at the HF asymptote; scaled per line
  // by m_i/1000 so all lines share one T60(f).
  float HS1_DB=E("FDN_HS1_DB",-0.2302f), HS1_F=E("FDN_HS1_F",1562.40f);
  float HS2_DB=E("FDN_HS2_DB",-0.2720f), HS2_F=E("FDN_HS2_F",4834.38f);
  float PK_DB =E("FDN_PK_DB", -0.1295f), PK_F =E("FDN_PK_F", 845.03f), PK_Q=E("FDN_PK_Q",0.40f);
  std::vector<float> g0(N);
  for(int i=0;i<N;i++) g0[i]=(float)std::pow(10.0,-3.0*dl[i]/(DC60*SR));
  std::vector<BQ> hs1(N), hs2(N), pk(N);
  for(int i=0;i<N;i++){ double sc=dl[i]/1000.0;
    mkHighShelf(hs1[i],HS1_F,HS1_DB*sc); mkHighShelf(hs2[i],HS2_F,HS2_DB*sc);
    mkPeak(pk[i],PK_F,PK_Q,PK_DB*sc); }

  // --- input / diffusion ---
  float bright=E("FDN_BRIGHT",6000.0f); BQ inA; mkLP(inA,bright,0.707);
  int nID=(int)E("FDN_INDIFF",2);
  std::vector<AP> idf(std::max(0,nID)); { double ms[6]={5.4,8.1,11.7,3.3,6.9,9.8}; for(int i=0;i<nID;i++) idf[i].prep((int)std::round(ms[i%6]*0.001*SR),0.6f); }

  // --- dense early-reflection stage (shapes FRONT of the EDR) ---
  // A short multitap (decorrelated L/R) feeds the OUTPUT directly so the first
  // ~20dB drops fast (two-slope EDR), independent of the long diffuse tail.
  int ER_N=(int)E("FDN_ER_N",22); float ER_MS=E("FDN_ER_MS",48.0f); // spread over ~48ms
  float ER_MIX=E("FDN_ER_MIX",0.30f), ER_FB=E("FDN_ER_DECAY",0.62f);
  Delay erbuf; erbuf.prep((int)(SR*0.001*ER_MS)+8);
  std::vector<int> ertap(ER_N); std::vector<float> ergL(ER_N), ergR(ER_N);
  { unsigned r=99173u; auto u=[&](){ r=r*1664525u+1013904223u; return (r>>9)/8388608.0f; };
    for(int i=0;i<ER_N;i++){ float frac=(i+u()*0.7f)/ER_N; ertap[i]=8+(int)(frac*SR*0.001*ER_MS);
      float dec=std::pow(ER_FB,frac*ER_N/4.0f); ergL[i]=((r>>3)&1?1:-1)*dec; ergR[i]=((r>>5)&1?1:-1)*dec; } }

  // --- output conditioning ---
  float bodyDb=E("FDN_BODY",0.0f), bodyF=E("FDN_BODYF",250.0f), bodyQ=E("FDN_BODYQ",0.8f);
  BQ bodyL,bodyR; if(bodyDb!=0.0f){ mkPeak(bodyL,bodyF,bodyQ,bodyDb); mkPeak(bodyR,bodyF,bodyQ,bodyDb); }
  float hcDb=E("FDN_HC_DB",0.0f), hcF=E("FDN_HC_F",6000.0f);
  BQ hcL_,hcR_; if(hcDb!=0.0f){ mkHighShelf(hcL_,hcF,hcDb); mkHighShelf(hcR_,hcF,hcDb); }
  std::vector<BQ> geqL, geqR;
  if(const char*gp=getenv("FDN_GEQ")){ FILE*gf=fopen(gp,"r"); if(gf){ double gf_,gq_,gg_;
    while(fscanf(gf,"%lf %lf %lf",&gf_,&gq_,&gg_)==3){ BQ a,b; mkPeak(a,gf_,gq_,gg_); mkPeak(b,gf_,gq_,gg_); geqL.push_back(a); geqR.push_back(b);} fclose(gf);} }
  float lcHz=E("FDN_LOWCUT",0.0f); float lcK=lcHz>0?(float)(1.0-std::exp(-2.0*M_PI*lcHz/SR)):0.0f; float hcL=0,hcR=0;

  std::vector<float> sin_(N), sL(N), sR(N);
  unsigned r=12345u; auto rnd=[&](){ r=r*1664525u+1013904223u; return (float)((r>>9)&1?1.0:-1.0); };
  for(int i=0;i<N;i++) sin_[i]=rnd(); for(int i=0;i<N;i++) sL[i]=rnd(); for(int i=0;i<N;i++) sR[i]=rnd();
  std::vector<float> o(F*2,0), s(N), fb(N);
  float invsq=1.0f/std::sqrt((float)N);
  float erMix=ER_MIX, tankMix=E("FDN_TANK_MIX",1.0f);
  for(long n=0;n<F;n++){
    float x=0.5f*(in[2*n]+in[2*n+1]); x=inA.proc(x); for(int i=0;i<nID;i++) x=idf[i].proc(x);
    // early reflections from the dry-ish input
    erbuf.write(x); float el=0,er=0; for(int i=0;i<ER_N;i++){ float t=erbuf.read(ertap[i]); el+=ergL[i]*t; er+=ergR[i]*t; }
    el*=erMix/std::sqrt((float)ER_N)*4.0f; er*=erMix/std::sqrt((float)ER_N)*4.0f;
    // tank read
    for(int i=0;i<N;i++) s[i]=line[i].read(dl[i]);
    float wl=0,wr=0; for(int i=0;i<N;i++){ wl+=sL[i]*s[i]; wr+=sR[i]*s[i]; }
    float ol=wl*invsq*tankMix, orr=wr*invsq*tankMix;
    ol+=el; orr+=er;
    if(bodyDb!=0.0f){ ol=bodyL.proc(ol); orr=bodyR.proc(orr); }
    if(hcDb!=0.0f){ ol=hcL_.proc(ol); orr=hcR_.proc(orr); }
    if(lcK>0){ hcL+=lcK*(ol-hcL); ol-=hcL; hcR+=lcK*(orr-hcR); orr-=hcR; }
    for(size_t gi=0;gi<geqL.size();gi++){ ol=geqL[gi].proc(ol); orr=geqR[gi].proc(orr); }
    o[2*n]=ol; o[2*n+1]=orr;
    // per-line damping: broadband gain then shelf/peak cascade
    for(int i=0;i<N;i++){ float v=g0[i]*s[i]; v=hs1[i].proc(v); v=hs2[i].proc(v); v=pk[i].proc(v); fb[i]=v; }
    fwht(fb.data(),N); for(int i=0;i<N;i++) fb[i]*=invsq;
    float ing=0.5f; for(int i=0;i<N;i++) line[i].write(fb[i]+sin_[i]*ing*x);
  }
  FILE*go=fopen(argv[2],"wb");fwrite(o.data(),4,F*2,go);fclose(go);
  fprintf(stderr,"FDN2 N=%d DC60=%.2f HS1=%.3f@%.0f HS2=%.3f@%.0f PK=%.3f@%.0f ER=%dx%.0fms mix=%.2f\n",
    N,DC60,HS1_DB,HS1_F,HS2_DB,HS2_F,PK_DB,PK_F,ER_N,ER_MS,erMix);
  return 0;
}
