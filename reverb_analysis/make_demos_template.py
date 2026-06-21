# make_demos_template.py — loudness-matched A/B guitar demos:
#   0_reference  = dry convolved with the reference IR
#   1..n_ours    = dry through our engine render(s)
# Folder-per-version, identical simple filenames inside (the demo convention).
#
# usage: python3 make_demos_template.py --dry dry.f32 --ref "ir/<ref>.wav" \
#          --ours wet_a.f32:decay2.0 wet_b.f32:decay4.0 --root my_demos
import numpy as np, os, argparse
from wavutil import read_wav, write_wav
sr=48000
def loadf(p): return np.fromfile(p,dtype='<f4').reshape(-1,2)
def conv(dry,ir):
    n=len(dry)+len(ir)-1; nfft=1<<int(np.ceil(np.log2(n))); out=np.zeros((n,2))
    for c in range(2): out[:,c]=np.fft.irfft(np.fft.rfft(dry[:,c],nfft)*np.fft.rfft(ir[:,c],nfft),nfft)[:n]
    return out
def rms(x): return np.sqrt(np.mean(x**2)+1e-20)
def norm(x,pk=0.97): m=np.max(np.abs(x))+1e-9; return x*(pk/m) if m>pk else x
def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--dry',required=True); ap.add_argument('--ref',required=True)
    ap.add_argument('--ours',nargs='+',required=True, help='f32:label pairs')
    ap.add_argument('--root',default='demos'); ap.add_argument('--mix',type=float,default=0.38)
    a=ap.parse_args()
    dry=loadf(a.dry); L=len(dry)
    ir,_=read_wav(a.ref); ir=ir[:, :2]
    def fit(x): return np.vstack([x,np.zeros((L-len(x),2))]) if len(x)<L else x[:L]
    versions={'0_reference-convolved':fit(conv(dry,ir))}
    for i,spec in enumerate(a.ours,1):
        p,lab=spec.split(':'); versions[f'{i}_ours-{lab}']=fit(loadf(p))
    anchor=rms(versions['0_reference-convolved'])
    for k in versions:
        if k!='0_reference-convolved': versions[k]*=anchor/rms(versions[k])
    os.makedirs(a.root,exist_ok=True)
    w=a.mix; d=1.0-w
    allpk=max(np.max(np.abs(d*dry+w*v)) for v in versions.values()); g=0.97/allpk
    for name,wet in versions.items():
        o=os.path.join(a.root,name); os.makedirs(o,exist_ok=True)
        write_wav(os.path.join(o,f'guitar_mix_{int(w*100)}pct-wet.wav'),(d*dry+w*wet)*g,sr)
        write_wav(os.path.join(o,'guitar_wet-only.wav'),norm(wet.copy()),sr)
        print(f"{name}: wetRMS={rms(wet):.4f}")
    print("done ->",a.root)
if __name__=='__main__': main()
