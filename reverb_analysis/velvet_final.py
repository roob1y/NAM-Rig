import sys,os,subprocess; sys.path.insert(0,'.')
import numpy as np, matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
import reverb_battery as rb
SR=rb.sr
IMP=os.path.abspath("../plate_proto/impulse.f32")
def align(LR):L,R=LR;m=(L+R)/2;o=rb.onset(m);return L[o:],R[o:]
def render(vlvlv):
    out=f"/tmp/vf_{vlvlv}.f32"
    e={**os.environ,"RV_T60":"2.45","HFM":"0","EARLY":"0","VLV":"1","DARKG":"0.85","DARKHZ":"5000","VLVLV":str(vlvlv)}
    subprocess.run(["../plate_proto/render_proto","plate",IMP,out],env=e,check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    return align(rb.load_ours(out))
def rc(m,win=512,hop=96,tmax=1.0):
    o=rb.onset(m);m=m[o:];w=np.hanning(win);f=np.fft.rfftfreq(win,1/SR);s=f>40;c=[];t=[]
    for i in range(0,int(tmax*SR)-win,hop):
        fr=np.abs(np.fft.rfft(m[i:i+win]*w));c.append(np.sum(f[s]*fr[s])/(np.sum(fr[s])+1e-20));t.append(i/SR*1000)
    return np.array(t),np.array(c)
def shim_t(m,win=int(0.25*SR),hop=int(0.05*SR),tmax=1.8):
    o=rb.onset(m);m=m[o:];c=[];t=[]
    for i in range(0,int(tmax*SR)-win,hop):
        seg=m[i:i+win];n=1<<15;P=np.abs(np.fft.rfft(seg,n))**2;f=np.fft.rfftfreq(n,1/SR)
        a=np.sum(P[(f>=6000)&(f<12000)]);b=np.sum(P[(f>=2000)&(f<4000)]);c.append(10*np.log10(a/(b+1e-20)+1e-9));t.append((i+win/2)/SR*1000)
    return np.array(t),np.array(c)
refL,refR=align(rb.load_ref("ir/vintage-plate-1.5s.wav"));ref=(refL+refR)/2
oL,oR=render(1.4);old=(oL+oR)/2
nL,nR=render(0.4);new=(nL+nR)/2
fig,ax=plt.subplots(1,2,figsize=(15,5.5))
for nm,m,col,lw in [("reference",ref,'k',2.6),("OLD velvet 1.4",old,'tab:red',1.6),("NEW velvet 0.40",new,'tab:green',1.8)]:
    t,c=rc(m);ax[0].plot(t,c,color=col,label=nm,lw=lw)
    t2,c2=shim_t(m);ax[1].plot(t2,c2,color=col,label=nm,lw=lw)
ax[0].set_title("Centroid trajectory (plateau fix)");ax[0].set_xlabel("ms");ax[0].set_ylabel("Hz");ax[0].set_xlim(0,1000);ax[0].axhline(3863,color='grey',ls=':',alpha=.5);ax[0].legend();ax[0].grid(alpha=.3)
ax[1].set_title("Top shimmer over time (6-12k vs 2-4k)");ax[1].set_xlabel("ms");ax[1].set_ylabel("dB");ax[1].set_xlim(0,1800);ax[1].legend();ax[1].grid(alpha=.3)
fig.suptitle("Velvet crossover fit: VLVLV 1.4 -> 0.40 (body+DARKG untouched, anchor knob1.5)")
fig.tight_layout();fig.savefig("velvet_final.png",dpi=120)
def at(m,ms):
    t,c=rc(m);return int(c[min(np.searchsorted(t,ms),len(c)-1)])
print("cen@[100,300,500,800]:")
for nm,m in [("reference",ref),("OLD 1.4",old),("NEW 0.40",new)]:
    print(f"  {nm:12} {[at(m,x) for x in (100,300,500,800)]}")
print("saved velvet_final.png")
