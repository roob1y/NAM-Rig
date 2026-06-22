import sys,os,subprocess; sys.path.insert(0,'.')
import numpy as np, reverb_battery as rb
SR=rb.sr
IMP=os.path.abspath("../plate_proto/impulse_2p5.f32")
VRAT=[1.878,2.204,2.571]
# knob -> (ref wav suffix, measured T60@1k)
KN=[("0.7",1.34),("1.0",1.87),("1.5",2.45),("2.5",3.16),("4.0",3.78)]
def align(LR):L,R=LR;m=(L+R)/2;o=rb.onset(m);return L[o:],R[o:]
def cen(m,msl,win=512,hop=96):
    o=rb.onset(m);m=m[o:];w=np.hanning(win);f=np.fft.rfftfreq(win,1/SR);s=f>40;c=[];t=[]
    for i in range(0,int(1.0*SR)-win,hop):
        fr=np.abs(np.fft.rfft(m[i:i+win]*w));c.append(np.sum(f[s]*fr[s])/(np.sum(fr[s])+1e-20));t.append(i/SR*1000)
    c=np.array(c);t=np.array(t);return np.array([c[min(np.searchsorted(t,x),len(c)-1)] for x in msl])
def render(T60,vlvlv,tmul):
    out=f"/tmp/kv_{T60}_{vlvlv}_{tmul}.f32"
    e={**os.environ,"RV_T60":str(T60),"HFM":"0","EARLY":"0","VLV":"1","DARKG":"0.85","DARKHZ":"5000","VLVLV":str(vlvlv)}
    if tmul is not None:
        for i,r in enumerate(VRAT,1): e[f"V{i}T"]=str(T60*r*tmul)  # knob-tracking velvet t60 x tmul
    subprocess.run(["../plate_proto/render_proto","plate",IMP,out],env=e,check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    return align(rb.load_ours(out))
MS=[300,500,800]
print(f"{'knob':>5}{'T60':>5} | {'REF 300/500/800':>17} | {'FIT err':>8}{'OLD err':>8}{'SIMPLE err':>10}")
errs={'FIT':[],'OLD':[],'SIMPLE':[]}
for kn,T60 in KN:
    rL,rR=align(rb.load_ref(f"ir/vintage-plate-{kn}s.wav")); ref=cen((rL+rR)/2,MS)
    def e(cfg):
        if cfg=='FIT': L,R=render(T60,0.6,2.0)
        elif cfg=='OLD': L,R=render(T60,1.4,None)
        else: L,R=render(T60,0.45,None)
        c=cen((L+R)/2,MS); return np.sqrt(np.mean((c-ref)**2)),c
    eF,cF=e('FIT'); eO,_=e('OLD'); eS,_=e('SIMPLE')
    for k,v in [('FIT',eF),('OLD',eO),('SIMPLE',eS)]: errs[k].append(v)
    print(f"{kn:>5}{T60:5.2f} | {str([int(x) for x in ref]):>17} | {eF:8.0f}{eO:8.0f}{eS:10.0f}   FIT->{[int(x) for x in cF]}")
print(f"\nmean err Hz:  FIT {np.mean(errs['FIT']):.0f}   OLD {np.mean(errs['OLD']):.0f}   SIMPLE {np.mean(errs['SIMPLE']):.0f}")
print(f"FIT err range: {min(errs['FIT']):.0f}..{max(errs['FIT']):.0f} (stable if flat across knobs)")
