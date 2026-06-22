import sys,os,subprocess,itertools; sys.path.insert(0,'.')
import numpy as np, reverb_battery as rb
SR=rb.sr
IMP=os.path.abspath("../plate_proto/impulse_2p5.f32")
VT0=[2.45*1.878,2.45*2.204,2.45*2.571]
REF=np.array([4443.,3863.,3316.])  # cen@300,500,800
def align(LR):L,R=LR;m=(L+R)/2;o=rb.onset(m);return L[o:],R[o:]
def render(v,tm):
    out=f"/tmp/v2_{v}_{tm}.f32"
    e={**os.environ,"RV_T60":"2.45","HFM":"0","EARLY":"0","VLV":"1","DARKG":"0.85","DARKHZ":"5000","VLVLV":str(v)}
    for i,t in enumerate(VT0,1): e[f"V{i}T"]=str(t*tm)
    subprocess.run(["../plate_proto/render_proto","plate",IMP,out],env=e,check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    return align(rb.load_ours(out))
def cen(m,msl,win=512,hop=96):
    o=rb.onset(m);m=m[o:];w=np.hanning(win);f=np.fft.rfftfreq(win,1/SR);s=f>40;c=[];t=[]
    for i in range(0,int(1.0*SR)-win,hop):
        fr=np.abs(np.fft.rfft(m[i:i+win]*w));c.append(np.sum(f[s]*fr[s])/(np.sum(fr[s])+1e-20));t.append(i/SR*1000)
    c=np.array(c);t=np.array(t);return np.array([c[min(np.searchsorted(t,x),len(c)-1)] for x in msl])
print(f"{'VLVLV':>6}{'tMul':>5} | {'cen@300/500/800':>20} | {'err Hz':>7}")
best=None
for v,tm in itertools.product([0.3,0.45,0.6,0.8],[1.0,1.6,2.4]):
    L,R=render(v,tm);m=(L+R)/2;c=cen(m,[300,500,800])
    err=np.sqrt(np.mean((c-REF)**2))
    print(f"{v:6.2f}{tm:5.1f} | {str([int(x) for x in c]):>20} | {err:7.0f}")
    if best is None or err<best[0]: best=(err,v,tm,c)
print(f"\nREF cen@300/500/800 = [4443,3863,3316]")
print(f"BEST: VLVLV={best[1]} tMul={best[2]} -> {[int(x) for x in best[3]]} err={best[0]:.0f}Hz")
