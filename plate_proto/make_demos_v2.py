import numpy as np, os
from wavutil import read_wav, write_wav
sr=48000
def loadf(p): return np.fromfile(p,dtype='<f4').reshape(-1,2)
dry=loadf('dry.f32')
ir,_=read_wav('ir/NEVO - vintage plate, 2.0s.wav'); ir=ir[:,:2]
def conv(dry,ir):
    n=len(dry)+len(ir)-1; nfft=1<<int(np.ceil(np.log2(n))); out=np.zeros((n,2))
    for c in range(2): out[:,c]=np.fft.irfft(np.fft.rfft(dry[:,c],nfft)*np.fft.rfft(ir[:,c],nfft),nfft)[:n]
    return out
wet={'0_reference_real-studio':conv(dry,ir),
     '1_v1_N64-FWHT_3biquad':loadf('w_v1.f32'),
     '2_v2_N64_GEQ':loadf('w_v2n64.f32'),
     '3_v2_N32_GEQ_FWHT':loadf('w_v2n32.f32'),
     '4_v2_N32_GEQ_randortho':loadf('w_v2n32r.f32'),
     '5_v2_N16_GEQ':loadf('w_v2n16.f32')}
def rms(x): return np.sqrt(np.mean(x**2)+1e-20)
def fit(x,L): return np.vstack([x,np.zeros((L-len(x),2))]) if len(x)<L else x[:L]
L=len(dry); wet={k:fit(v,L) for k,v in wet.items()}
anchor=rms(wet['0_reference_real-studio'])
for k in wet: wet[k]=wet[k]*(anchor/rms(wet[k]))   # loudness-match all to studio
def norm(x,pk=0.97): m=np.max(np.abs(x))+1e-9; return x*(pk/m) if m>pk else x
root='plate_v2_demos'; os.makedirs(root,exist_ok=True)
allpk=max(np.max(np.abs(0.62*dry+0.38*w)) for w in wet.values()); g=0.97/allpk
for name,w in wet.items():
    d=os.path.join(root,name); os.makedirs(d,exist_ok=True)
    write_wav(os.path.join(d,'guitar_mix_38pct-wet.wav'),(0.62*dry+0.38*w)*g,sr)
    write_wav(os.path.join(d,'guitar_wet-only.wav'),norm(w.copy()),sr)
    print("%-26s wetRMS=%.4f"%(name,rms(w)))
print("done ->",root)
