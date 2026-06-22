import sys,os,subprocess; sys.path.insert(0,'.')
import numpy as np, reverb_battery as rb
SR=rb.sr
IMP=os.path.abspath("../plate_proto/impulse_2p5.f32")
LO=[6500.,8000.,9800.]; HI=[8500.,10200.,12500.]
def align(LR):L,R=LR;m=(L+R)/2;o=rb.onset(m);return L[o:],R[o:]
def render(vlvlv,k):
    out=f"/tmp/vb_{vlvlv}_{k}.f32"
    e={**os.environ,"RV_T60":"2.45","HFM":"0","EARLY":"0","VLV":"1","DARKG":"0.85","DARKHZ":"5000","VLVLV":str(vlvlv)}
    for i,(lo,hi) in enumerate(zip(LO,HI),1):
        e[f"V{i}LO"]=str(lo*k); e[f"V{i}HI"]=str(hi*k)
    subprocess.run(["../plate_proto/render_proto","plate",IMP,out],env=e,check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    return align(rb.load_ours(out))
def shim(m,lo,hi,t0=0.40,t1=1.5):
    o=rb.onset(m);m=m[o:];seg=m[int(t0*SR):int(t1*SR)];n=1<<int(np.ceil(np.log2(len(seg))))
    P=np.abs(np.fft.rfft(seg,n))**2;f=np.fft.rfftfreq(n,1/SR)
    a=10*np.log10(np.sum(P[(f>=lo)&(f<hi)])+1e-20);b=10*np.log10(np.sum(P[(f>=2000)&(f<4000)])+1e-20);return a-b
def cen500(m,win=512,hop=64):
    o=rb.onset(m);m=m[o:];w=np.hanning(win);f=np.fft.rfftfreq(win,1/SR);s=f>40;c=[];t=[]
    for i in range(0,int(0.6*SR)-win,hop):
        fr=np.abs(np.fft.rfft(m[i:i+win]*w));c.append(np.sum(f[s]*fr[s])/(np.sum(fr[s])+1e-20));t.append(i/SR*1000)
    c=np.array(c);t=np.array(t);return int(c[min(np.searchsorted(t,500),len(c)-1)])
refL,refR=align(rb.load_ref("ir/vintage-plate-1.5s.wav"));ref=(refL+refR)/2
print(f"reference: cen@500 {cen500(ref)}  shimmer6-12k {shim(ref,6000,12000):+.1f}  air8-12k {shim(ref,8000,12000):+.1f}")
print(f"\n{'k(band)':>7}{'VLVLV':>6} | {'cen@500':>7} | {'shim6-12k':>9}{'air8-12k':>9}")
for k in [1.0,1.25,1.5]:
    for v in [1.0,0.6,0.4]:
        L,R=render(v,k);m=(L+R)/2
        print(f"{k:7.2f}{v:6.2f} | {cen500(m):7d} | {shim(m,6000,12000):+9.1f}{shim(m,8000,12000):+9.1f}")
print("\nGoal: cen@500~3863, shim6-12k~-7.7 (air8-12k ref shown above)")
