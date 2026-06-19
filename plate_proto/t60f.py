# Fine T60(f) measurement on the documented band grid, for any .f32 or ir/*.wav
# Usage: python3 t60f.py out.f32 [more.f32 ...]   (always also prints REF = studio 2.0s)
import numpy as np, sys, os
from wavutil import read_wav
sr=48000
BANDS=[125,180,250,355,500,710,1000,1400,2000,2800,4000,5600,8000,11000]
TARGET={125:4.41,180:4.17,250:3.97,355:3.57,500:3.34,710:3.09,1000:2.93,
        1400:2.67,2000:2.38,2800:2.27,4000:2.06,5600:1.80,8000:1.66,11000:1.63}
def biquad_bp(x,fc,Q=2.0):
    w0=2*np.pi*fc/sr; al=np.sin(w0)/(2*Q); c=np.cos(w0)
    b=np.array([al,0,-al])/(1+al); a=np.array([-2*c,1-al])/(1+al)
    y=np.zeros_like(x); x1=x2=y1=y2=0.0
    for n in range(len(x)):
        xn=x[n]; yn=b[0]*xn+b[1]*x1+b[2]*x2-a[0]*y1-a[1]*y2
        x2=x1;x1=xn;y2=y1;y1=yn;y[n]=yn
    return y
def t60clean(x,lo=-5,hi=-35):
    e=x.astype(np.float64)**2; sch=np.cumsum(e[::-1])[::-1]; sch/=sch[0]+1e-20
    db=10*np.log10(sch+1e-20)
    i1=np.argmax(db<=lo); i2=np.argmax(db<=hi)
    if i2<=i1: return float('nan')
    return (i2-i1)/sr*(60.0/(hi-lo)*-1)
def loadf(p):
    if p.endswith('.wav'): a,_=read_wav(p); return (a[:,0]+a[:,1])/2
    a=np.fromfile(p,dtype='<f4').reshape(-1,2); return (a[:,0]+a[:,1])/2
def measure(m):
    on=np.argmax(np.abs(m)>0.01*np.max(np.abs(m))); m=m[on:on+int(sr*6)]
    return {fc:t60clean(biquad_bp(m,fc)) for fc in BANDS}
def show(name,d):
    print(f"{name:18s} "+" ".join(f"{d[fc]:4.2f}" for fc in BANDS))
print("T60(f) seconds | bands: "+" ".join(f"{fc/1000:g}k" if fc>=1000 else str(fc) for fc in BANDS))
show("TARGET(doc)",TARGET)
ref,_=read_wav('ir/NEVO - vintage plate, 2.0s.wav'); show("REF-measured",measure((ref[:,0]+ref[:,1])/2))
for p in sys.argv[1:]:
    nm=os.path.basename(p).replace('.f32','').replace('.wav','')
    show(nm,measure(loadf(p)))
