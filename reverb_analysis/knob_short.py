import sys,os,subprocess; sys.path.insert(0,'.')
import numpy as np, reverb_battery as rb
SR=rb.sr; IMP=os.path.abspath("../plate_proto/impulse_2p5.f32"); VRAT=[1.878,2.204,2.571]
def align(LR):L,R=LR;m=(L+R)/2;o=rb.onset(m);return L[o:],R[o:]
def cen(m,msl,win=512,hop=96):
    o=rb.onset(m);m=m[o:];w=np.hanning(win);f=np.fft.rfftfreq(win,1/SR);s=f>40;c=[];t=[]
    for i in range(0,int(1.0*SR)-win,hop):
        fr=np.abs(np.fft.rfft(m[i:i+win]*w));c.append(np.sum(f[s]*fr[s])/(np.sum(fr[s])+1e-20));t.append(i/SR*1000)
    c=np.array(c);t=np.array(t);return np.array([c[min(np.searchsorted(t,x),len(c)-1)] for x in msl])
def render(T60,vlvlv,tmul):
    out=f"/tmp/ks_{T60}_{tmul}.f32"; e={**os.environ,"RV_T60":str(T60),"HFM":"0","EARLY":"0","VLV":"1","DARKG":"0.85","DARKHZ":"5000","VLVLV":str(vlvlv)}
    for i,r in enumerate(VRAT,1): e[f"V{i}T"]=str(T60*r*tmul)
    subprocess.run(["../plate_proto/render_proto","plate",IMP,out],env=e,check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL); return align(rb.load_ours(out))
# knob 0.7: ref + FIT at decreasing tmul; report 300/500 (meaningful) and 800 (degenerate)
rL,rR=align(rb.load_ref("ir/vintage-plate-0.7s.wav")); ref=cen((rL+rR)/2,[300,500,800])
print(f"knob0.7 T60=1.34  REF 300/500/800 = {[int(x) for x in ref]}  (note 800>300: body gone, noise/top-dominated)")
print(f"{'tMul':>5}{'velvet_t60_top(s)':>18} | {'300/500/800':>17} | {'err300-500':>11}")
for tm in [2.0,1.5,1.0,0.6]:
    L,R=render(1.34,0.6,tm); c=cen((L+R)/2,[300,500,800]); e2=np.sqrt(np.mean((c[:2]-ref[:2])**2))
    print(f"{tm:5.1f}{1.34*2.571*tm:18.1f} | {str([int(x) for x in c]):>17} | {e2:11.0f}")
