import sys,os,subprocess; sys.path.insert(0,'.')
import numpy as np, reverb_battery as rb
SR=rb.sr
IMP=os.path.abspath("../plate_proto/impulse_2p5.f32")
VT0=[2.45*1.878,2.45*2.204,2.45*2.571]  # default velvet t60 (s)
def align(LR):L,R=LR;m=(L+R)/2;o=rb.onset(m);return L[o:],R[o:]
def render(vlvlv,tmul):
    out=f"/tmp/vt_{vlvlv}_{tmul}.f32"
    e={**os.environ,"RV_T60":"2.45","HFM":"0","EARLY":"0","VLV":"1","DARKG":"0.85","DARKHZ":"5000","VLVLV":str(vlvlv)}
    for i,t in enumerate(VT0,1): e[f"V{i}T"]=str(t*tmul)
    subprocess.run(["../plate_proto/render_proto","plate",IMP,out],env=e,check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    return align(rb.load_ours(out))
def shim(m,lo,hi,t0,t1):
    o=rb.onset(m);m=m[o:];seg=m[int(t0*SR):int(t1*SR)];n=1<<int(np.ceil(np.log2(max(16,len(seg)))))
    P=np.abs(np.fft.rfft(seg,n))**2;f=np.fft.rfftfreq(n,1/SR)
    a=10*np.log10(np.sum(P[(f>=lo)&(f<hi)])+1e-20);b=10*np.log10(np.sum(P[(f>=2000)&(f<4000)])+1e-20);return a-b
def cen500(m,win=512,hop=64):
    o=rb.onset(m);m=m[o:];w=np.hanning(win);f=np.fft.rfftfreq(win,1/SR);s=f>40;c=[];t=[]
    for i in range(0,int(0.6*SR)-win,hop):
        fr=np.abs(np.fft.rfft(m[i:i+win]*w));c.append(np.sum(f[s]*fr[s])/(np.sum(fr[s])+1e-20));t.append(i/SR*1000)
    c=np.array(c);t=np.array(t);return int(c[min(np.searchsorted(t,500),len(c)-1)])
refL,refR=align(rb.load_ref("ir/vintage-plate-1.5s.wav"));ref=(refL+refR)/2
print(f"reference: cen@500 {cen500(ref)} shim6-12k(.4-1.5s) {shim(ref,6000,12000,.4,1.5):+.1f} shimLATE(.7-2s) {shim(ref,6000,12000,.7,2.0):+.1f}")
print(f"\n{'VLVLV':>6}{'tMul':>5} | {'cen@500':>7} | {'shim.4-1.5':>10}{'shim.7-2':>9}")
for v in [0.4,0.5]:
    for tm in [1.0,1.5,2.2]:
        L,R=render(v,tm);m=(L+R)/2
        print(f"{v:6.2f}{tm:5.1f} | {cen500(m):7d} | {shim(m,6000,12000,.4,1.5):+10.1f}{shim(m,6000,12000,.7,2.0):+9.1f}")
print("goal cen@500~3863, shim toward ref")
