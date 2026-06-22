import sys,os,subprocess; sys.path.insert(0,'.')
import numpy as np, matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
import reverb_battery as rb
SR=rb.sr
VT0=[2.45*1.878,2.45*2.204,2.45*2.571]
def align(LR):L,R=LR;m=(L+R)/2;o=rb.onset(m);return L[o:],R[o:]
def render(v,tm,inp,out):
    e={**os.environ,"RV_T60":"2.45","HFM":"0","EARLY":"0","VLV":"1","DARKG":"0.85","DARKHZ":"5000","VLVLV":str(v)}
    for i,t in enumerate(VT0,1): e[f"V{i}T"]=str(t*tm)
    subprocess.run(["../plate_proto/render_proto","plate",os.path.abspath(inp),out],env=e,check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
def rc(m,win=512,hop=96,tmax=1.1):
    o=rb.onset(m);m=m[o:];w=np.hanning(win);f=np.fft.rfftfreq(win,1/SR);s=f>40;c=[];t=[]
    for i in range(0,int(tmax*SR)-win,hop):
        fr=np.abs(np.fft.rfft(m[i:i+win]*w));c.append(np.sum(f[s]*fr[s])/(np.sum(fr[s])+1e-20));t.append(i/SR*1000)
    return np.array(t),np.array(c)
IMP="../plate_proto/impulse.f32"
refL,refR=align(rb.load_ref("ir/vintage-plate-1.5s.wav"));ref=(refL+refR)/2
cfgs=[("OLD 1.4 / t1.0",1.4,1.0,'tab:red'),("FIT 0.6 / t2.0",0.6,2.0,'tab:green'),("SIMPLE 0.45 / t1.0",0.45,1.0,'tab:blue')]
fig,ax=plt.subplots(figsize=(11,6))
t,c=rc(ref);ax.plot(t,c,'k-',lw=2.8,label="reference")
print("cen@300/500/800  (ref 4443/3863/3316)")
for nm,v,tm,col in cfgs:
    render(v,tm,IMP,f"/tmp/wrap_{v}_{tm}.f32");L,R=align(rb.load_ours(f"/tmp/wrap_{v}_{tm}.f32"));m=(L+R)/2
    t,c=rc(m);ax.plot(t,c,color=col,lw=1.7,label=nm)
    at=lambda ms:int(c[min(np.searchsorted(t,ms),len(c)-1)])
    print(f"  {nm:20} {[at(300),at(500),at(800)]}")
ax.axhline(3863,color='grey',ls=':',alpha=.4);ax.set_xlim(0,1100);ax.set_title("Velvet crossover fit — centroid trajectory vs reference (body+DARKG fixed)");ax.set_xlabel("ms");ax.set_ylabel("Hz");ax.legend();ax.grid(alpha=.3)
fig.tight_layout();fig.savefig("velvet_wrap.png",dpi=120);print("saved velvet_wrap.png")
