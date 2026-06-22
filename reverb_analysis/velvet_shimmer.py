import sys,os,subprocess; sys.path.insert(0,'.')
import numpy as np, reverb_battery as rb
SR=rb.sr
IMP=os.path.abspath("../plate_proto/impulse_2p5.f32")
def align(LR):L,R=LR;m=(L+R)/2;o=rb.onset(m);return L[o:],R[o:]
def render(vlvlv,vlv="1"):
    out=f"/tmp/vs_{vlvlv}_{vlv}.f32"
    e={**os.environ,"RV_T60":"2.45","HFM":"0","EARLY":"0","VLV":vlv,"DARKG":"0.85","DARKHZ":"5000","VLVLV":str(vlvlv)}
    subprocess.run(["../plate_proto/render_proto","plate",IMP,out],env=e,check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    return align(rb.load_ours(out))
def late_shimmer(m,t0=0.40,t1=1.5):
    o=rb.onset(m);m=m[o:];seg=m[int(t0*SR):int(t1*SR)]
    n=1<<int(np.ceil(np.log2(len(seg))));P=np.abs(np.fft.rfft(seg,n))**2;f=np.fft.rfftfreq(n,1/SR)
    top=10*np.log10(np.sum(P[(f>=6000)&(f<12000)])+1e-20)
    mid=10*np.log10(np.sum(P[(f>=2000)&(f<4000)])+1e-20)
    return top-mid  # top vs upper-mid in the TAIL (the audible shimmer ratio)
def cen500(m,win=512,hop=64):
    o=rb.onset(m);m=m[o:];w=np.hanning(win);f=np.fft.rfftfreq(win,1/SR);s=f>40;c=[];t=[]
    for i in range(0,int(0.6*SR)-win,hop):
        fr=np.abs(np.fft.rfft(m[i:i+win]*w));c.append(np.sum(f[s]*fr[s])/(np.sum(fr[s])+1e-20));t.append(i/SR*1000)
    c=np.array(c);t=np.array(t);return int(c[min(np.searchsorted(t,500),len(c)-1)])
refL,refR=align(rb.load_ref("ir/vintage-plate-1.5s.wav"));ref=(refL+refR)/2
print(f"reference: cen@500 {cen500(ref)}  late-shimmer(6-12k vs 2-4k) {late_shimmer(ref):+.1f} dB")
bL,bR=render(1.4,"0");print(f"body only: cen@500 {cen500((bL+bR)/2)}  late-shimmer {late_shimmer((bL+bR)/2):+.1f} dB")
print(f"\n{'VLVLV':>6} | {'cen@500':>7} | {'late-shimmer dB':>15}")
for v in [1.4,0.7,0.5,0.4,0.35,0.25]:
    L,R=render(v);m=(L+R)/2;print(f"{v:6.2f} | {cen500(m):7d} | {late_shimmer(m):+15.1f}")
