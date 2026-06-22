import sys,os;sys.path.insert(0,'.')
import numpy as np, reverb_battery as rb
from wavutil import write_wav
SR=48000
def align(LR):
    L,R=LR;m=(L+R)/2;o=rb.onset(m);return np.stack([L[o:],R[o:]],1)
ref=align(rb.load_ref("ir/vintage-plate-1.5s.wav"))
our=align(rb.load_ours("/tmp/our_v2_ir.f32"))
N=max(len(ref),len(our))
def pad(a):return np.vstack([a,np.zeros((N-len(a),2))]) if len(a)<N else a[:N]
ref=pad(ref);our=pad(our)
def rms(x):return np.sqrt(np.mean(x**2)+1e-20)
def splice(onset_ir,tail_ir,Sms):
    S=int(Sms*0.001*SR);xf=int(0.004*SR)
    g=rms(onset_ir[max(0,S-xf):S])/(rms(tail_ir[S:S+xf])+1e-12)
    out=np.zeros((N,2));tail=tail_ir*g;a=max(0,S-xf//2);b=S+xf//2;w=np.linspace(1,0,b-a)[:,None]
    out[:a]=onset_ir[:a];out[a:b]=onset_ir[a:b]*w+tail[a:b]*(1-w);out[b:]=tail[b:];return out
E=splice(ref,our,15)   # ref ATTACK (0-15ms) + our rest
F=splice(our,ref,15)   # our attack + ref rest
dry=np.fromfile("dry_guitar.f32",dtype='<f4').reshape(-1,2);Ld=len(dry)
def conv(ir):
    n=Ld+len(ir)-1;nf=1<<int(np.ceil(np.log2(n)));o=np.zeros((n,2))
    for c in range(2):o[:,c]=np.fft.irfft(np.fft.rfft(dry[:,c],nf)*np.fft.rfft(ir[:,c],nf),nf)[:n]
    return o
def fit(x):return np.vstack([x,np.zeros((Ld-len(x),2))]) if len(x)<Ld else x[:Ld]
for nm,ir in [("E_REFattack15+OURrest",E),("F_OURattack15+REFrest",F)]:
    fit(conv(ir)).astype('<f4').tofile(f"/tmp/ss_{nm}.f32");print("saved",nm)
