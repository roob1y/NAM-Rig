# auto-grade a render vs the target IR on the playbook bars
import numpy as np
from wavutil import read_wav
sr=48000.0
FC=np.array([62.5,125,180,250,355,500,710,1000,1400,2000,2800,4000,5600,8000,11000.])
def bp(x,fc,Q=2.0):
    w0=2*np.pi*fc/sr; al=np.sin(w0)/(2*Q); c=np.cos(w0)
    b0,b2=al/(1+al),-al/(1+al); a1,a2=(-2*c)/(1+al),(1-al)/(1+al)
    nfft=1<<int(np.ceil(np.log2(len(x)+8))); X=np.fft.rfft(x,nfft)
    om=2*np.pi*np.fft.rfftfreq(nfft); z=np.exp(-1j*om)
    H=(b0+b2*z*z)/(1+a1*z+a2*z*z); return np.fft.irfft(X*H,nfft)[:len(x)]
def t60(x,lo=-5,hi=-35):
    e=x**2; sch=np.cumsum(e[::-1])[::-1]; sch/=sch[0]+1e-20; db=10*np.log10(sch+1e-20)
    i1=np.argmax(db<=lo); i2=np.argmax(db<=hi)
    return (i2-i1)/sr*(60.0/(lo-hi)) if i2>i1 else np.nan
def t60f(m,win):
    on=np.argmax(np.abs(m)>0.01*np.max(np.abs(m))); seg=m[on:on+int(sr*win)]
    return np.array([t60(bp(seg,fc)) for fc in FC])
def oct3(m,cf,win):
    on=np.argmax(np.abs(m)>0.01*np.max(np.abs(m))); seg=m[on:on+int(sr*win)]
    X=np.abs(np.fft.rfft(seg*np.hanning(len(seg))))**2; f=np.fft.rfftfreq(len(seg),1/sr); out=[]
    for c in cf:
        lo,hi=c/2**(1/6),c*2**(1/6); s=(f>=lo)&(f<hi); out.append(10*np.log10(np.mean(X[s])+1e-30))
    return np.array(out)
def subair(m,win):
    on=np.argmax(np.abs(m)>0.01*np.max(np.abs(m))); seg=m[on:on+int(sr*win)]
    X=np.abs(np.fft.rfft(seg*np.hanning(len(seg))))**2; f=np.fft.rfftfreq(len(seg),1/sr)
    def b(lo,hi): s=(f>=lo)&(f<hi); return 10*np.log10(np.mean(X[s])+1e-30)
    return b(25,70)-b(200,800), b(11000,16000)-b(1000,3000)   # sub-rel, air-rel
def grade(irp, renderp, win):
    ir,_=read_wav(irp); m=(ir[:,0]+ir[:,1])/2
    a=np.fromfile(renderp,dtype='<f4').reshape(-1,2); r=(a[:,0]+a[:,1])/2
    tI,tR=t60f(m,win),t60f(r,win); te=tR-tI
    cf=np.array([200,250,315,400,500,630,800,1000,1250,1600,2000,2500,3150,4000,5000,6300,8000.])
    sI=oct3(m,cf,3); sI-=sI[7]; sR=oct3(r,cf,3); sR-=sR[7]; se=sR-sI
    subI,airI=subair(m,3); subR,airR=subair(r,3)
    print("  T60(f) vs IR: mean|e|=%.3fs max=%.3fs  (bar +/-0.1)"%(np.nanmean(np.abs(te)),np.nanmax(np.abs(te))))
    print("  spectrum 200-8k: mean|e|=%.2fdB max=%.2fdB (bar <=1)"%(np.mean(np.abs(se)),np.max(np.abs(se))))
    print("  sub-rel: IR%+.1f ours%+.1f (d%+.1f)  air-rel: IR%+.1f ours%+.1f (d%+.1f)"%(subI,subR,subR-subI,airI,airR,airR-airI))
    return np.nanmean(np.abs(te)),np.max(np.abs(se))
