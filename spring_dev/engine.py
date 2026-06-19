# Free-structure spring engine v3 (linear, LENGTH-INVARIANT). All FFT filtering is
# zero-padded linear convolution (no circular wrap). Single dense FDN backbone:
#   - highs: dispersed (spring chirp) + injected immediately
#   - lows : low-passed, DELAYED (causal bloom), injected -> low-mids peak late
#   - per-octave decay shaping (dtilt) -> lows ring longest
#   - tunable stereo width. Bloom is a real causal delay, not a wrap artifact.
import numpy as np
SR=48000; B=256
_u=np.array([1,1,1,1,1,1,1,-1.0]); _u/=np.linalg.norm(_u)
H8=np.eye(8)-2*np.outer(_u,_u)
_sL=np.array([1,-1,1,1,-1,1,-1,1.0]); _sR=np.array([1,1,-1,1,1,-1,1,-1.0])
def _Npad(n): return int(np.ceil((n+14400)/4800.0))*4800   # +0.3s pad (all stages short-tail now)
def _applyH(x,Hfun,sr=SR):
    n=len(x);N=_Npad(n);f=np.fft.rfftfreq(N,1/sr)
    return np.fft.irfft(np.fft.rfft(x,N)*Hfun(f),N)[:n]
def _lp1(x,fc,sr=SR):
    a=np.exp(-2*np.pi*fc/sr)
    return _applyH(x,lambda f:(1-a)/(1-a*np.exp(-2j*np.pi*f/sr)),sr)
def _hp1(x,fc,sr=SR): return x-_lp1(x,fc,sr)
def _bqlp(x,fc,q,sr=SR):
    w0=2*np.pi*fc/sr;al=np.sin(w0)/(2*q);c=np.cos(w0)
    b0=(1-c)/2;b1=1-c;b2=(1-c)/2;a0=1+al;a1=-2*c;a2=1-al
    def H(f):
        z=np.exp(-2j*np.pi*f/sr);return (b0+b1*z+b2*z*z)/(a0+a1*z+a2*z*z)
    return _applyH(x,H,sr)
def _peak(x,fc,q,gdb,sr=SR):
    A=10**(gdb/40);w0=2*np.pi*fc/sr;al=np.sin(w0)/(2*q);c=np.cos(w0)
    b0=1+al*A;b1=-2*c;b2=1-al*A;a0=1+al/A;a1=-2*c;a2=1-al/A
    def H(f):
        z=np.exp(-2j*np.pi*f/sr);return (b0+b1*z+b2*z*z)/(a0+a1*z+a2*z*z)
    return _applyH(x,H,sr)
def _disperse(x,a,k,M,sr=SR):
    if M<=0:return x
    def H(f):
        zk=np.exp(-2j*np.pi*f*k);return ((a+zk)/(1+a*zk))**M
    return _applyH(x,H,sr)
def _band(x,lo,hi,sr=SR):
    return _applyH(x,lambda f:((f>=lo)&(f<hi)).astype(float),sr)
def _delay(x,d):
    d=int(max(0,d))
    if d==0:return x
    o=np.zeros_like(x);o[d:]=x[:len(x)-d];return o
def _decay_tilt(x,dtilt,sr=SR):
    # smooth one-pole octave split (short tails -> length-invariant), shorten higher bands
    edges=[350,700,1400,2800,5600]; t=np.arange(len(x))/sr
    lps=[_lp1(x,fc) for fc in edges]
    bands=[lps[0]]+[lps[k]-lps[k-1] for k in range(1,len(edges))]+[x-lps[-1]]
    out=np.zeros_like(x)
    for k,bd in enumerate(bands):
        out+=bd*np.exp(-dtilt*k*t)
    return out
def _fdn_block(inp,delays,g,damp,sr=SR):
    n=len(inp);nl=8;L=np.maximum(np.asarray(delays,int),B)
    hist=np.zeros((nl,n+B));dprev=np.zeros(nl);g0=1-damp;g1=damp;out=np.zeros((nl,n))
    ar=np.arange(nl)[:,None]
    for b in range(0,n,B):
        e=min(b+B,n);idx=(np.arange(b,e)[None,:]-L[:,None])
        D=np.where(idx>=0,hist[ar,np.clip(idx,0,None)],0.0);out[:,b:e]=D
        Dsh=np.concatenate([dprev[:,None],D[:,:-1]],axis=1)
        V=g0*D+g1*Dsh;dprev=D[:,-1]
        hist[:,b:e]=g*(H8@V)+inp[b:e][None,:]
    return out
def _bloom_inject(x,fc,delay_samp,a):
    lo=_lp1(x,fc); return _delay(lo,delay_samp)
def _stereo(lines,s):
    cmn=lines.sum(0); dcL=(_sL[:,None]*lines).sum(0); dcR=(_sR[:,None]*lines).sum(0)
    return (1-s)*cmn+s*dcL,(1-s)*cmn+s*dcR
def render_ir(theta,n_sec=2.0,sr=SR):
    t=theta;n=int(n_sec*sr);x=np.zeros(n);x[0]=1.0
    x=_hp1(x,t['lowcut']); x=_lp1(x,t['dark'])
    xhi=_hp1(x,t['bloom_fc'])
    xd_hi=xhi   # dispersion removed (FFT-allpass tail broke length-invariance; chirp = future time-domain feature)
    bloom_in=_bloom_inject(x,t['bloom_fc'],t['bloom_delay_ms']*1e-3*sr,0.5)
    fdn_in=xd_hi+t['bloom_mix']*bloom_in
    ratios=np.array([1.00,1.18,1.37,1.54,1.73,1.91,2.12,2.31])
    base=t['fdn_base_ms']*1e-3*sr
    delays=(base*(1+(ratios-1)*t['fdn_spread'])).astype(int)
    loL,loR=_stereo(_fdn_block(fdn_in,delays,t['g'],t['damp']),t['stereo'])
    wetL,wetR=loL,loR
    ev=_hp1(xd_hi,t['early_hp'])
    wetL=wetL+t['early_mix']*ev; wetR=wetR+t['early_mix']*np.roll(ev,9)
    wetL=_decay_tilt(wetL,t['dtilt']); wetR=_decay_tilt(wetR,t['dtilt'])
    wetL=_peak(wetL,t['scoop_hz'],0.8,t['scoop_db']); wetR=_peak(wetR,t['scoop_hz'],0.8,t['scoop_db'])
    wetL=_bqlp(wetL,t['cut_hz'],0.7); wetR=_bqlp(wetR,t['cut_hz'],0.7)
    m=max(np.abs(wetL).max(),np.abs(wetR).max(),1e-9)
    return (wetL/m).astype(np.float32),(wetR/m).astype(np.float32)
BOUNDS={
 'lowcut':(60,260),'dark':(2000,9000),
 'fdn_base_ms':(14,45),'fdn_spread':(0.4,1.6),
 'g':(0.94,0.994),'damp':(0.02,0.25),'dtilt':(0.0,0.9),
 'bloom_mix':(0.0,2.0),'bloom_delay_ms':(20,95),'bloom_fc':(250,700),
 'early_mix':(0.0,0.8),'early_hp':(900,4000),'stereo':(0.0,1.0),
 'scoop_hz':(300,700),'scoop_db':(-9,0),'cut_hz':(3000,7000)}
KEYS=list(BOUNDS.keys())
def vec_to_theta(v): return {k:float(v[i]) for i,k in enumerate(KEYS)}
