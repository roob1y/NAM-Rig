# decayfit.py — multi-slope (early/late) decay analysis per band.
# Two-segment piecewise-linear fit to the Schroeder energy-decay curve (EDC, dB)
# with a scanned knee. Captures the "bloom & clear" the single slope erased.
# Non-neural stand-in for DecayFitNet multi-exp+noise. Outputs early/late T60 + knee.
import numpy as np, sys, os
from wavutil import read_wav
sr=48000
BANDS=[125,180,250,355,500,710,1000,1400,2000,2800,4000,5600,8000,11000]
def biquad_bp(x,fc,Q=2.0):
    # exact RBJ bandpass magnitude applied in the FFT domain (fast, vectorised)
    w0=2*np.pi*fc/sr; al=np.sin(w0)/(2*Q); c=np.cos(w0)
    b0,b1,b2=al/(1+al),0.0,-al/(1+al); a1,a2=(-2*c)/(1+al),(1-al)/(1+al)
    nfft=1<<int(np.ceil(np.log2(len(x)+8)))
    X=np.fft.rfft(x,nfft); w=2*np.pi*np.fft.rfftfreq(nfft,1.0/sr)/sr*sr  # =omega
    om=2*np.pi*np.fft.rfftfreq(nfft)  # radians/sample
    z=np.exp(-1j*om)
    H=(b0+b1*z+b2*z*z)/(1+a1*z+a2*z*z)
    return np.fft.irfft(X*H,nfft)[:len(x)]
def edc_db(x):
    e=x.astype(np.float64)**2; sch=np.cumsum(e[::-1])[::-1]; sch/=sch[0]+1e-300
    return 10*np.log10(sch+1e-300)
def two_slope(eband, lo=-5.0):
    # eband: per-sample band energy (already onset-trimmed). Estimate noise floor
    # from the last 0.3 s (pure tail), set the reliable dynamic range, then fit
    # two line segments to the Schroeder EDC over [lo, hi] with a scanned knee.
    n_tail=int(0.3*sr); pn=eband[-n_tail:].mean()+1e-300; pk=eband[:int(0.05*sr)].max()+1e-300
    snr=10*np.log10(pk/pn)
    hi=-(snr-12.0); hi=min(-15.0,max(-35.0,hi))  # ear-relevant range, floor-safe
    sch=np.cumsum(eband[::-1])[::-1]; sch/=sch[0]+1e-300; db=10*np.log10(sch+1e-300)
    i0=np.argmax(db<=lo); i1=np.argmax(db<=hi)
    if i1<=i0+20: return (np.nan,np.nan,np.nan,np.nan,hi)
    step=max(1,int(0.002*sr)); d=db[i0:i1:step]; t=np.arange(len(d))*step/sr; n=len(d)
    if n<20: return (np.nan,np.nan,np.nan,np.nan,hi)
    best=None
    for k in range(max(3,int(0.12*n)), int(0.88*n)):
        t1,y1=t[:k],d[:k]; t2,y2=t[k:],d[k:]
        A1=np.vstack([t1,np.ones_like(t1)]).T; A2=np.vstack([t2,np.ones_like(t2)]).T
        (m1,c1),_,_,_=np.linalg.lstsq(A1,y1,rcond=None)
        (m2,c2),_,_,_=np.linalg.lstsq(A2,y2,rcond=None)
        err=np.sum((A1@[m1,c1]-y1)**2)+np.sum((A2@[m2,c2]-y2)**2)
        if best is None or err<best[0]: best=(err,m1,m2,t[k],d[k])
    _,m1,m2,kt,kdb=best
    eT60=-60.0/m1 if m1<0 else np.nan; lT60=-60.0/m2 if m2<0 else np.nan
    return (eT60,lT60,kdb,kt,hi)
def measure(m):
    on=np.argmax(np.abs(m)>0.01*np.max(np.abs(m))); m=m[on:on+int(sr*7)]
    return {fc:two_slope(biquad_bp(m,fc)**2) for fc in BANDS}
def show(name,d):
    print("\n%s"%name); print("  band   early  late   knee_dB knee_t  fit_hi")
    for fc in BANDS:
        e,l,kd,kt,hi=d[fc]; print("  %6d %6.2f %6.2f  %6.1f %6.2f  %6.1f"%(fc,e,l,kd,kt,hi))
ref,_=read_wav('ir/NEVO - vintage plate, 2.0s.wav')
show("REF vintage plate 2.0s",measure((ref[:,0]+ref[:,1])/2))
for p in sys.argv[1:]:
    if p.endswith('.wav'): a,_=read_wav(p); m=(a[:,0]+a[:,1])/2
    else: a=np.fromfile(p,dtype='<f4').reshape(-1,2); m=(a[:,0]+a[:,1])/2
    show(os.path.basename(p),measure(m))
