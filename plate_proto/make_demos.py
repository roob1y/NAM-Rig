# Build loudness-matched A/B guitar demos: studio reference vs old single-pole
# plate vs new EDR-matched plate. Folder-per-version, identical filenames inside.
import numpy as np, os
from wavutil import read_wav, write_wav
sr=48000
def loadf(p): return np.fromfile(p,dtype='<f4').reshape(-1,2)
dry=loadf('dry.f32')
wet_new=loadf('wet_new.f32')
wet_old=loadf('wet_old.f32')
# studio reference: convolve dry with the 2.0s IR (stereo) via FFT
ir,_=read_wav('ir/NEVO - vintage plate, 2.0s.wav'); ir=ir[:, :2]
def conv(dry,ir):
    n=len(dry)+len(ir)-1; nfft=1<<int(np.ceil(np.log2(n)))
    out=np.zeros((n,2))
    for c in range(2):
        out[:,c]=np.fft.irfft(np.fft.rfft(dry[:,c],nfft)*np.fft.rfft(ir[:,c],nfft),nfft)[:n]
    return out
wet_nevo=conv(dry,ir)
# trim/pad all wet to same length as dry timeline + tail room
def fit(x,L):
    if len(x)<L: return np.vstack([x,np.zeros((L-len(x),2))])
    return x[:L]
L=len(dry)
wet_nevo=fit(wet_nevo,L); wet_new=fit(wet_new,L); wet_old=fit(wet_old,L)
# loudness-match the WET signals to a common RMS (use studio as anchor)
def rms(x): return np.sqrt(np.mean(x**2)+1e-20)
anchor=rms(wet_nevo)
for w in (wet_new,wet_old):
    w*= anchor/rms(w)
# build outputs
def norm(x,peak=0.97):
    m=np.max(np.abs(x))+1e-9; return x*(peak/m) if m>peak else x
folders={'0_reference_real-vintage plate':wet_nevo,
         '1_old-single-pole-plate':wet_old,
         '2_new-multiband-EDR-matched':wet_new}
root='plate_rebuild_demos'
os.makedirs(root,exist_ok=True)
# common gain so all three share identical scaling (preserve relative loudness match)
allpk=max(np.max(np.abs(0.62*dry+0.38*w)) for w in folders.values())
g=0.97/allpk
for name,wet in folders.items():
    d=os.path.join(root,name); os.makedirs(d,exist_ok=True)
    mix=(0.62*dry+0.38*wet)*g
    wetonly=norm(wet.copy())
    write_wav(os.path.join(d,'guitar_mix_38pct-wet.wav'),mix,sr)
    write_wav(os.path.join(d,'guitar_wet-only.wav'),wetonly,sr)
    print(f"{name}: wetRMS={rms(wet):.4f} mixpk={np.max(np.abs(mix)):.3f}")
print("done ->",root)
