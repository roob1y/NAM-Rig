#include "SpectralShifter.h"
#include <cstdio>
#include <cmath>
#include <vector>
using namespace nam_rig;
static const double SR=48000.0, PI=3.14159265358979323846;
static std::vector<float> tone(double f0,int n){
    std::vector<float> x(n,0.0f);
    for(int h=1;h<=6;++h){double a=1.0/h; for(int i=0;i<n;++i) x[i]+=(float)(a*std::sin(2*PI*f0*h*i/SR));}
    return x;
}
static double binP(const std::vector<float>&x,double f){
    int n=(int)x.size(); double re=0,im=0; for(int i=0;i<n;++i){double a=2*PI*f*i/SR; re+=x[i]*cos(a); im+=x[i]*sin(a);} return (re*re+im*im)/((double)n*n);
}
static std::vector<float> run(bool formant,bool lock,double ratio,double f0){
    SpectralShifter s; s.prepare(SR,1024,4); s.setRatio((float)ratio);
    s.setFormantPreserve(formant); s.setPhaseLock(lock);
    int n=(int)(SR*1.0); std::vector<float> x=tone(f0,n); s.process(x.data(),n);
    return std::vector<float>(x.begin()+2000,x.end());
}
int main(){
    // determinism / off-identical: formant+lock OFF must match run-to-run exactly
    auto a=run(false,false,2.0,220), b=run(false,false,2.0,220);
    double d=0; for(size_t i=0;i<a.size();++i) d=std::max(d,(double)std::fabs(a[i]-b[i]));
    std::printf("off deterministic: max diff %.2e %s\n", d, d==0?"OK":"FAIL");
    // phase-lock finite + still shifts 220->440 (ratio 2)
    auto pl=run(false,true,2.0,220);
    bool fin=true; for(float v:pl) if(!std::isfinite(v)) fin=false;
    double e440=binP(pl,440.0), e220=binP(pl,220.0);
    std::printf("phase-lock finite: %s ; pitch shifted 220->440 (%.2e @440 >> %.2e @220): %s\n",
        fin?"OK":"FAIL", e440,e220, e440>e220*5?"OK":"FAIL");
    // combined formant+lock finite + shifted
    auto cb=run(true,true,2.0,220);
    bool fin2=true; for(float v:cb) if(!std::isfinite(v)) fin2=false;
    std::printf("formant+lock finite & shifted: %s (%.2e @440 vs %.2e @220)\n",
        (fin2 && binP(cb,440)>binP(cb,220)*5)?"OK":"FAIL", binP(cb,440), binP(cb,220));
    // phase-lock does CHANGE the output (it's doing something) vs no-lock
    auto nl=run(false,false,2.0,220);
    double ch=0; for(size_t i=0;i<pl.size();++i) ch=std::max(ch,(double)std::fabs(pl[i]-nl[i]));
    std::printf("phase-lock alters output vs plain: max diff %.3e (should be > 0)\n", ch);
    // down-octave still works with lock
    auto dn=run(false,true,0.5,220);
    std::printf("down-oct + lock: 110 %.2e >> 220 %.2e : %s\n", binP(dn,110), binP(dn,220), binP(dn,110)>binP(dn,220)*3?"OK":"FAIL");
    return 0;
}
