import sys,os,subprocess; sys.path.insert(0,'.')
import numpy as np, reverb_battery as rb
SR=rb.sr; IMP=os.path.abspath("../plate_proto/impulse_2p5.f32"); VRAT=[1.878,2.204,2.571]
KN=[("0.5",0.81),("0.7",1.34),("1.0",1.87),("1.5",2.45),("2.5",3.16),("4.0",3.78)]
def Mtaper(T60): return 1.0+1.0*min(1.0,max(0.0,(T60-0.81)/(1.87-0.81)))
def align(LR):L,R=LR;m=(L+R)/2;o=rb.onset(m);return L[o:],R[o:]
def cen_scaled(m,T60,win=512,hop=64):
    o=rb.onset(m);m=m[o:];w=np.hanning(win);f=np.fft.rfftfreq(win,1/SR);s=f>40;c=[];t=[]
    tmax=min(1.4,T60*0.45)
    for i in range(0,int(tmax*SR)-win,hop):
        fr=np.abs(np.fft.rfft(m[i:i+win]*w));c.append(np.sum(f[s]*fr[s])/(np.sum(fr[s])+1e-20));t.append(i/SR)
    c=np.array(c);t=np.array(t)
    pts=[T60*x for x in (0.12,0.20,0.33)]
    return np.array([c[min(np.searchsorted(t,p),len(c)-1)] for p in pts]),pts
def render(T60,vlvlv,M):
    out=f"/tmp/kt_{T60}_{vlvlv}_{M}.f32"; e={**os.environ,"RV_T60":str(T60),"HFM":"0","EARLY":"0","VLV":"1","DARKG":"0.85","DARKHZ":"5000","VLVLV":str(vlvlv)}
    if M is not None:
        for i,r in enumerate(VRAT,1): e[f"V{i}T"]=str(T60*r*M)
    subprocess.run(["../plate_proto/render_proto","plate",IMP,out],env=e,check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL); return align(rb.load_ours(out))
print("decay-scaled points T60*[0.12,0.20,0.33].  FIT_tap=M taper; FIT_2.0=flat x2; OLD=1.4 default")
print(f"{'knob':>5}{'T60':>5}{'M':>5}{'vT60top':>8} | {'REF pts':>20} | {'FIT_tap':>8}{'FIT_2.0':>8}{'OLD':>6}")
eT=[];eF=[];eO=[]
for kn,T60 in KN:
    M=Mtaper(T60)
    rL,rR=align(rb.load_ref(f"ir/vintage-plate-{kn}s.wav")); ref,_=cen_scaled((rL+rR)/2,T60)
    def err(v,Mx):
        L,R=render(T60,v,Mx); c,_=cen_scaled((L+R)/2,T60); return np.sqrt(np.mean((c-ref)**2)),c
    etap,ctap=err(0.6,M); e20,_=err(0.6,2.0); eold,_=err(1.4,None)
    eT.append(etap);eF.append(e20);eO.append(eold)
    print(f"{kn:>5}{T60:5.2f}{M:5.2f}{T60*2.571*M:8.1f} | {str([int(x) for x in ref]):>20} | {etap:8.0f}{e20:8.0f}{eold:6.0f}")
print(f"\nmean err: FIT_taper {np.mean(eT):.0f}  FIT_flat2.0 {np.mean(eF):.0f}  OLD {np.mean(eO):.0f}")
print(f"FIT_taper across knobs: {[int(x) for x in eT]}  (band-check: flat & low?)")
