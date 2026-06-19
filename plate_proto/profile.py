import numpy as np,sys,glob,os
from wavutil import read_wav
def biquad_bp(x,fc,sr,Q=1.41):
    w0=2*np.pi*fc/sr; a=np.sin(w0)/(2*Q); c=np.cos(w0)
    b0,b1,b2=a,0,-a; a0,a1,a2=1+a,-2*c,1-a
    b=np.array([b0,b1,b2])/a0; al=np.array([a1,a2])/a0
    y=np.zeros_like(x); x1=x2=y1=y2=0.0
    for n in range(len(x)):
        xn=x[n]; yn=b[0]*xn+b[1]*x1+b[2]*x2-al[0]*y1-al[1]*y2
        x2=x1;x1=xn;y2=y1;y1=yn;y[n]=yn
    return y
def rt60(x,sr):
    e=x**2; sch=np.cumsum(e[::-1])[::-1]; sch/=sch[0]+1e-20
    db=10*np.log10(sch+1e-20)
    i1=np.argmax(db<=-5); i2=np.argmax(db<=-25)
    if i2<=i1: return float('nan')
    return (i2-i1)/sr*(60/20)
def centroid(x,sr):
    X=np.abs(np.fft.rfft(x*np.hanning(len(x)))); f=np.fft.rfftfreq(len(x),1/sr)
    return float(np.sum(f*X)/(np.sum(X)+1e-20))
def ned(x,sr,win=0.020):
    w=int(win*sr); n=len(x)//w; vals=[]
    for k in range(min(n,40)):
        seg=x[k*w:(k+1)*w]; s=np.std(seg)+1e-20
        vals.append(np.mean(np.abs(seg)>s)/0.3173)
    return vals
for p in sorted(glob.glob('ir/*.wav'),key=lambda s:float(s.split(',')[1].replace('s.wav','').strip())):
    a,sr=read_wav(p); L=a[:,0]; R=a[:,1] if a.shape[1]>1 else a[:,0]
    m=(L+R)/2
    on=np.argmax(np.abs(m)>0.01*np.max(np.abs(m)))
    mt=m[on:]
    cen=centroid(mt,sr)
    bands={'low/mid 250':250,'mid 1k':1000,'high 4k':4000,'air 8k':8000}
    rts={k:rt60(biquad_bp(mt[:int(sr*min(6,len(mt)/sr))],fc,sr),sr) for k,fc in bands.items()}
    corr=float(np.corrcoef(L[on:on+sr],R[on:on+sr])[0,1])
    nd=ned(mt,sr)
    name=os.path.basename(p)
    print(f"{name}")
    print(f"   centroid={cen:6.0f}Hz  corr={corr:+.2f}  RT60[250/1k/4k/8k]={rts['low/mid 250']:.2f}/{rts['mid 1k']:.2f}/{rts['high 4k']:.2f}/{rts['air 8k']:.2f}s")
    print(f"   NED@20ms={nd[0]:.2f} @40={nd[1]:.2f} @100={nd[4]:.2f}  (1.0=fully diffuse)")
