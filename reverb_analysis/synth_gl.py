import sys,os;sys.path.insert(0,'.')
import numpy as np, reverb_battery as rb
SR=rb.sr
WIN=512; HOP=128
def stft(x):
    w=np.hanning(WIN); n=len(x); frames=[]
    for i in range(0,n-WIN,HOP): frames.append(np.fft.rfft(x[i:i+WIN]*w))
    return np.array(frames)
def istft(S,n):
    w=np.hanning(WIN); x=np.zeros(n+WIN); ws=np.zeros(n+WIN)
    for k in range(len(S)):
        i=k*HOP; seg=np.fft.irfft(S[k],WIN); x[i:i+WIN]+=seg*w; ws[i:i+WIN]+=w**2
    return (x/(ws+1e-9))[:n]
def griffinlim(mag,n,iters=80,seed=0):
    rng=np.random.default_rng(seed); ph=rng.uniform(-np.pi,np.pi,mag.shape)
    x=istft(mag*np.exp(1j*ph),n)
    for _ in range(iters):
        S=stft(x)
        if S.shape[0]<mag.shape[0]: S=np.vstack([S,np.zeros((mag.shape[0]-S.shape[0],S.shape[1]))])
        ph=np.angle(S[:mag.shape[0]]); x=istft(mag*np.exp(1j*ph),n)
    return x
def align(LR):L,R=LR;m=(L+R)/2;o=rb.onset(m);return L[o:],R[o:]
rL,rR=align(rb.load_ref("ir/vintage-plate-1.5s.wav"))
ON=int(0.12*SR)
magL=np.abs(stft(rL[:ON])); magR=np.abs(stft(rR[:ON]))
sL=griffinlim(magL,ON,seed=1); sR=griffinlim(magR,ON,seed=2)
# level-match to reference onset
g=np.sqrt(np.mean(rL[:ON]**2+rR[:ON]**2))/(np.sqrt(np.mean(sL**2+sR**2))+1e-12); sL*=g; sR*=g
# splice with engine tail (EARLY off)
import subprocess
subprocess.run(["../plate_proto/render_proto","plate",os.path.abspath("impulse.f32"),"/tmp/tail_ir.f32"],cwd="../plate_proto",
    env={**os.environ,"RV_T60":"2.45","HFM":"1","VLV":"1","EARLY":"0"},check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
tL,tR=align(rb.load_ours("/tmp/tail_ir.f32"))
N=max(len(tL),ON)+10
def pad(a):return np.concatenate([a,np.zeros(N-len(a))]) if len(a)<N else a[:N]
S=ON-int(0.006*SR); xf=int(0.012*SR)
def splice(eL,tch):
    out=np.zeros(N);eLp=pad(eL);tt=pad(tch)
    r_e=np.sqrt(np.mean(eLp[S-xf:S]**2)+1e-20);r_t=np.sqrt(np.mean(tt[S:S+xf]**2)+1e-20);tt=tt*(r_e/(r_t+1e-12))
    a=S-xf//2;b=S+xf//2;w=np.linspace(1,0,b-a)
    out[:a]=eLp[:a];out[a:b]=eLp[a:b]*w+tt[a:b]*(1-w);out[b:]=tt[b:];return out
hL=splice(sL,tL);hR=splice(sR,tR)
np.stack([hL,hR],1).astype('<f4').tofile("/tmp/gl_ir.f32")
import onset_fit as of
err=float(np.mean(np.abs(of.norm(of.tf_map((hL+hR)/2))-of.REFM)))
print("Griffin-Lim onset TF err: %.3f (random-phase was 3.11, algo floor 4.8)"%err)
