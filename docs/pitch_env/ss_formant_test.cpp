#include "SpectralShifter.h"
#include <cstdio>
#include <cmath>
#include <vector>
using namespace nam_rig;
static const double SR=48000.0, PI=3.14159265358979323846;
// voiced signal: harmonics of f0 shaped by fixed formants (F1 700, F2 1800, F3 2600)
static std::vector<float> voiced(double f0,int n){
    std::vector<float> x(n,0.0f);
    auto env=[](double f){ auto r=[&](double fc,double bw,double a){return a/std::sqrt(1+((f-fc)/bw)*((f-fc)/bw));};
        return 0.03+r(700,120,1.0)+r(1800,220,0.6)+r(2600,320,0.25); };
    for(int h=1;h<80;++h){double fh=f0*h; if(fh>=SR/2)break; double g=env(fh);
        for(int i=0;i<n;++i) x[i]+=(float)(g*std::sin(2*PI*fh*i/SR));}
    return x;
}
static double bandE(const std::vector<float>&x,double lo,double hi){
    // Goertzel-ish DFT energy sum over lo..hi via coarse bins
    int n=(int)x.size(); double e=0;
    for(double f=lo;f<=hi;f+=25.0){double re=0,im=0; for(int i=0;i<n;++i){double a=2*PI*f*i/SR; re+=x[i]*std::cos(a); im+=x[i]*std::sin(a);} e+=(re*re+im*im)/((double)n*n);}
    return e;
}
static std::vector<float> run(bool formant){
    SpectralShifter s; s.prepare(SR,1024,4); s.setRatio(2.0f); s.setFormantPreserve(formant);
    int n=(int)(SR*1.0); std::vector<float> x=voiced(150.0,n);
    s.process(x.data(),n);
    return std::vector<float>(x.begin()+2000, x.end()); // drop latency+transient
}
int main(){
    // 1) formant OFF must be byte-identical to the original algorithm (no regression).
    //    Compare against a second OFF run -> deterministic.
    auto a=run(false), b=run(false);
    double diff=0; for(size_t i=0;i<a.size();++i) diff=std::max(diff,(double)std::fabs(a[i]-b[i]));
    std::printf("determinism (off vs off): max diff %.2e %s\n", diff, diff<1e-9?"OK":"FAIL");
    // 2) finite output both modes.
    auto off=run(false), on=run(true);
    bool fin=true; for(float v:on) if(!std::isfinite(v)) fin=false;
    std::printf("formant-on finite: %s\n", fin?"OK":"FAIL");
    // 3) body retention: energy in F1 region (500-1000 Hz) relative to F2-shifted region.
    //    Naive up-oct pushes body up to ~1000-2000; formant-preserve keeps 500-1000.
    double offLo=bandE(off,500,1000), offHi=bandE(off,1200,2200);
    double onLo =bandE(on ,500,1000), onHi =bandE(on ,1200,2200);
    std::printf("low/high body ratio (500-1000 / 1200-2200):  naive %.3f   formant %.3f\n",
                offLo/(offHi+1e-12), onLo/(onHi+1e-12));
    std::printf("=> formant-preserve should have MORE low-band body (higher ratio)\n");
    return 0;
}
