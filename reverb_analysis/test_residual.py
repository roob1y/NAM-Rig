"""Critique tests on the 6-12k residual:
  (1) noise-floor margin: is the -55dB band point above the capture floor?
  (2) Rayleigh/diffuse test: envelope CV (Rayleigh=0.523), signal excess-kurtosis
      (Gaussian=0, sinusoid=-1.5), envelope autocorr (beating?), late spectrum (continuum vs lines).
Controls: velvet hybrid (should be diffuse) + multiband FDN.
Run from plate_proto/ :  python3 ../reverb_analysis/test_residual.py
"""
import os, sys, subprocess
sys.path.insert(0, os.path.dirname(__file__))
import numpy as np, matplotlib; matplotlib.use("Agg")
import matplotlib.pyplot as plt
import reverb_battery as rb
SR = rb.sr

def analytic(x):
    n=len(x); X=np.fft.fft(x); h=np.zeros(n)
    h[0]=1
    if n%2==0: h[n//2]=1; h[1:n//2]=2
    else: h[1:(n+1)//2]=2
    return np.fft.ifft(X*h)

def db(x): return 20*np.log10(np.abs(x)+1e-20)
def kurt(x): x=x-np.mean(x); s=np.std(x)+1e-20; return np.mean((x/s)**4)-3.0

def floor_margin(m, on, fc, pre):
    b=rb.bp(m,fc)
    env=np.abs(analytic(b[on:]))
    pk=np.max(env[:int(0.2*SR)])
    sch=rb.schroeder_db(b[on:])
    def tcross(d):
        idx=np.where(sch<=d)[0]; return idx[0] if len(idx) else len(sch)-1
    i55=tcross(-55.0)
    lvl55_abs = db(env[i55]) if i55<len(env) else -120  # absolute band level at -55 schroeder pt
    if pre is not None and len(pre)>30:
        fl = db(np.sqrt(np.mean(rb.bp(pre,fc)**2)))
    else:
        fl = db(np.sqrt(np.mean(env[int(0.96*len(env)):]**2)))  # fallback: deepest tail
    return dict(bandpeak=db(pk), lvl55=lvl55_abs, floor=fl, margin=lvl55_abs-fl,
                t55=i55/SR)

def rayleigh_stats(m, on, fc, floor_db):
    b=rb.bp(m,fc); seg=b[on:]; env=np.abs(analytic(seg))
    sch=rb.schroeder_db(seg)
    def tcross(d):
        idx=np.where(sch<=d)[0]; return idx[0] if len(idx) else len(sch)-1
    i0=tcross(-10.0); i1=tcross(-45.0)
    # don't go below floor+10dB
    pk=np.max(env[:int(0.2*SR)]); stop=np.where(db(env)< (floor_db+10))[0]
    if len(stop): i1=min(i1, stop[stop>i0][0]) if np.any(stop>i0) else i1
    i1=max(i1,i0+SR//4)
    i0=int(i0); i1=int(i1)
    w=env[i0:i1]; sg=seg[i0:i1]
    # detrend exponential (linear in dB)
    t=np.arange(len(w)); A=np.polyfit(t, np.log(w+1e-20),1); fit=np.exp(np.polyval(A,t))
    r=w/(fit+1e-20)
    cv=np.std(r)/ (np.mean(r)+1e-20)
    ek=kurt(sg/(fit+1e-20))   # whitened -> stationary, so kurtosis tests Gaussianity not the decay
    # envelope autocorr (detrended) -> beating periodicity
    rd=r-np.mean(r); ac=np.correlate(rd,rd,'full')[len(rd)-1:]; ac/=ac[0]+1e-20
    # strongest secondary peak beyond 2ms lag
    lag0=int(0.002*SR); sec=np.max(ac[lag0:lag0+int(0.05*SR)]) if len(ac)>lag0+10 else 0
    return dict(cv=cv, kurt=ek, ac_secondary=sec, r=r, seg=sg, i0=i0,i1=i1)

def late_spectrum(m,on,fc):
    b=rb.bp(m,fc); seg=b[on:]; sch=rb.schroeder_db(seg)
    idx=np.where(sch<=-20)[0]; i0=idx[0] if len(idx) else 0
    w=seg[i0:i0+int(0.6*SR)]; w=w*np.hanning(len(w))
    nfft=1<<18; S=np.abs(np.fft.rfft(w,nfft)); f=np.fft.rfftfreq(nfft,1/SR)
    sel=(f>fc*0.85)&(f<fc*1.15); ss=S[sel]
    prom = db(np.max(ss)) - db(np.median(ss))   # tall lines -> high; smooth continuum -> low
    return prom

def run(label, m, pre):
    on=rb.onset(m); out={}
    print(f"\n=== {label} ===")
    for fc in (8000,11000):
        fm=floor_margin(m,on,fc,pre)
        rs=rayleigh_stats(m,on,fc,fm['floor'])
        sp=late_spectrum(m,on,fc)
        print(f"  {fc:5d}Hz | margin {fm['margin']:5.1f}dB (-55 pt {fm['lvl55']:.0f} vs floor {fm['floor']:.0f}) "
              f"| CV {rs['cv']:.3f}(Rayl .523) kurt {rs['kurt']:+.2f}(Gauss 0) "
              f"ac2 {rs['ac_secondary']:.2f} specProm {sp:.1f}dB")
        out[fc]=(fm,rs,sp)
    return out

def main():
    rL,rR=rb.load_ref("../reverb_analysis/ir/vintage-plate-1.5s.wav"); ref15=(rL+rR)/2
    qL,qR=rb.load_ref("../reverb_analysis/ir/vintage-plate-4.5s.wav"); ref45=(qL+qR)/2
    on45=rb.onset(ref45); pre45=ref45[:on45-20]
    # controls
    e={**os.environ,"RV_T60":"2.45","HFM":"1"}
    subprocess.run(["./render_proto","plate","impulse.f32","/tmp/tr_mb.f32"],env=e,check=True,
                   stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    subprocess.run(["python3","../reverb_analysis/velvet_proof.py"],check=True,
                   stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    mL,mR=rb.load_ours("/tmp/tr_mb.f32"); mb=(mL+mR)/2
    hL,hR=rb.load_ours("/tmp/vp_hybrid.f32"); hy=(hL+hR)/2

    R15=run("REFERENCE 1.5 (anchor)", ref15, None)
    R45=run("REFERENCE 4.5 (long, has pre-onset floor)", ref45, pre45)
    Cmb=run("CONTROL multiband-only (FDN)", mb, None)
    Chy=run("CONTROL velvet hybrid", hy, None)

    # figure: envelope histograms vs Rayleigh + late spectra
    fig,ax=plt.subplots(2,2,figsize=(13,8))
    def rayl_pdf(x,mean): sig=mean/np.sqrt(np.pi/2); return (x/sig**2)*np.exp(-x**2/(2*sig**2))
    for col,fc in enumerate((8000,11000)):
        for lbl,R,c in [("ref 4.5",R45,"k"),("velvet hybrid",Chy,"tab:green"),("multiband FDN",Cmb,"tab:blue")]:
            r=R[fc][1]['r']; h,edges=np.histogram(r,bins=60,range=(0,4),density=True)
            ctr=0.5*(edges[1:]+edges[:-1]); ax[0,col].plot(ctr,h,color=c,lw=1.5,label=lbl)
        xx=np.linspace(0,4,200); ax[0,col].plot(xx,rayl_pdf(xx,1.0),"r--",lw=1.5,label="Rayleigh")
        ax[0,col].set_title(f"{fc} Hz  normalized-envelope histogram"); ax[0,col].set_xlabel("env / fit"); ax[0,col].legend(fontsize=8)
    # autocorr 11k
    for lbl,R,c in [("ref 4.5",R45,"k"),("velvet",Chy,"tab:green"),("multiband",Cmb,"tab:blue")]:
        rs=R[11000][1]; rd=rs['r']-np.mean(rs['r']); ac=np.correlate(rd,rd,'full')[len(rd)-1:]; ac/=ac[0]+1e-20
        lag=np.arange(len(ac))/SR*1000; ax[1,0].plot(lag[:int(0.04*SR)],ac[:int(0.04*SR)],color=c,lw=1.2,label=lbl)
    ax[1,0].set_title("11 kHz envelope autocorrelation (beating?)"); ax[1,0].set_xlabel("lag (ms)"); ax[1,0].axhline(0,color='gray',lw=.5); ax[1,0].legend(fontsize=8)
    # margins bar
    labels=["ref1.5 8k","ref1.5 11k","ref4.5 8k","ref4.5 11k"]
    marg=[R15[8000][0]['margin'],R15[11000][0]['margin'],R45[8000][0]['margin'],R45[11000][0]['margin']]
    ax[1,1].bar(labels,marg,color=['tab:blue','tab:cyan','tab:blue','tab:cyan'])
    ax[1,1].axhline(15,color='r',ls='--',label="15 dB 'real signal' line"); ax[1,1].set_ylabel("dB above floor at -55pt"); ax[1,1].set_title("Noise-floor margin"); ax[1,1].legend(fontsize=8); ax[1,1].tick_params(axis='x',labelrotation=20)
    fig.suptitle("Residual diagnosis: is the 6-12k tail real, and is it Rayleigh-diffuse?",fontsize=13)
    plt.tight_layout(rect=[0,0,1,0.96]); plt.savefig(os.path.join(os.path.dirname(__file__),"residual_test.png"),dpi=120); plt.close()
    print("\nwrote residual_test.png")

if __name__=="__main__": main()
