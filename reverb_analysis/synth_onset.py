import sys,os;sys.path.insert(0,'.')
import numpy as np, reverb_battery as rb
from wavutil import write_wav
SR=rb.sr
def an(x):
    n=len(x);X=np.fft.fft(x);h=np.zeros(n);h[0]=1
    if n%2==0:h[n//2]=1;h[1:n//2]=2
    else:h[1:(n+1)//2]=2
    return np.fft.ifft(X*h)
def erb_bands(lo,hi,n):
    h=lambda f:21.4*np.log10(4.37e-3*f+1);ih=lambda e:(10**(e/21.4)-1)/4.37e-3
    return ih(np.linspace(h(lo),h(hi),n))
def bp(x,f0,nf):
    X=np.fft.rfft(x,nf);f=np.fft.rfftfreq(nf,1/SR);bw=max(60,0.16*f0);g=np.exp(-0.5*((f-f0)/(bw/2))**2)
    return np.fft.irfft(X*g,nf)[:len(x)]
def align(LR):L,R=LR;m=(L+R)/2;o=rb.onset(m);return L[o:],R[o:]
# --- reference onset (first 120ms) ---
rL,rR=align(rb.load_ref("ir/vintage-plate-1.5s.wav"))
ON=int(0.12*SR); FC=erb_bands(150,14000,28); nf=1<<15
def synth_channel(refch, seed):
    rng=np.random.default_rng(seed); out=np.zeros(ON)
    for f0 in FC:
        b=bp(refch[:ON],f0,nf); env=np.abs(an(b)); env=np.convolve(env,np.ones(48)/48,mode='same') # ~1ms smooth
        noise=rng.standard_normal(ON); nb=bp(noise,f0,nf)
        nb*= env/(np.sqrt(np.mean(nb**2)+1e-12))   # impose the band's time-envelope; unit-power carrier
        # match band energy to reference
        nb*= np.sqrt(np.mean(b**2)+1e-20)/(np.sqrt(np.mean(nb**2)+1e-20))
        out+=nb
    return out
sL=synth_channel(rL,1); sR=synth_channel(rR,2)   # independent noise -> decorrelated like the ref onset
# level-match the synth early to the reference onset
g=np.sqrt(np.mean((rL[:ON]**2+rR[:ON]**2)))/ (np.sqrt(np.mean(sL**2+sR**2))+1e-12)
sL*=g; sR*=g
early=np.stack([sL,sR],1)
# --- engine tail (EARLY off) ---
tL,tR=align(rb.load_ours("/tmp/tail_ir.f32")); N=max(ON+len(tL),len(tL))
def pad(a,n):return np.concatenate([a,np.zeros(n-len(a))]) if len(a)<n else a[:n]
N=max(len(tL), ON+1)+int(0.5*SR)
# splice: synth early (0-120ms) crossfade tank tail
S=ON-int(0.006*SR); xf=int(0.012*SR)
def splice(eL,tch):
    out=np.zeros(N); eLp=pad(eL,N); tt=pad(tch,N)
    r_e=np.sqrt(np.mean(eLp[S-xf:S]**2)+1e-20); r_t=np.sqrt(np.mean(tt[S:S+xf]**2)+1e-20); gt=r_e/(r_t+1e-12)
    tt=tt*gt; a=S-xf//2;b=S+xf//2;w=np.linspace(1,0,b-a)
    out[:a]=eLp[:a]; out[a:b]=eLp[a:b]*w+tt[a:b]*(1-w); out[b:]=tt[b:]
    return out
hL=splice(sL,tL); hR=splice(sR,tR)
np.stack([hL,hR],1).astype('<f4').tofile("/tmp/hybconv_ir.f32")
# validate TF error vs reference onset (reuse onset_fit)
import onset_fit as of
err=float(np.mean(np.abs(of.norm(of.tf_map((hL+hR)/2))-of.REFM)))
print("hybrid-conv onset TF err vs reference: %.3f   (algorithmic floor was ~4.8, match=0)"%err)
# convolve guitar
dry=np.fromfile("dry_guitar.f32",dtype='<f4').reshape(-1,2);Ld=len(dry)
def conv(irLR):
    n=Ld+N-1;nfc=1<<int(np.ceil(np.log2(n)));o=np.zeros((n,2))
    for c in range(2):o[:,c]=np.fft.irfft(np.fft.rfft(dry[:,c],nfc)*np.fft.rfft(irLR[:,c],nfc),nfc)[:n]
    return o
def fit(x):return np.vstack([x,np.zeros((Ld-len(x),2))]) if len(x)<Ld else x[:Ld]
fit(conv(np.stack([hL,hR],1))).astype('<f4').tofile("/tmp/g_hybconv.f32")
print("wrote /tmp/g_hybconv.f32")
