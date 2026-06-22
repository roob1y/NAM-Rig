import sys,os,subprocess; sys.path.insert(0,'.')
import numpy as np, matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
import reverb_battery as rb
SR=rb.sr
IMP=os.path.abspath("../plate_proto/impulse.f32")  # 10s for full tail
def align(LR): L,R=LR; m=(L+R)/2; o=rb.onset(m); return L[o:],R[o:]
def render_tail(env):
    out="/tmp/dbtail.f32"; e={**os.environ,"RV_T60":"2.45","HFM":"0",**env}
    subprocess.run(["../plate_proto/render_proto","plate",IMP,out],env=e,check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    return align(rb.load_ours(out))
def load_kernel():
    d=np.fromfile("../plate_onset_release/plate_early_kernel.f32",dtype='<f4').reshape(-1,2)
    return d[:,0],d[:,1]
def splice(eL,tL):  # same recipe as synth_v3: handoff ~142ms, 14ms xfade, level-match
    ON=len(eL); S=ON-int(0.008*SR); xf=int(0.014*SR); N=max(len(tL),ON)+10
    pad=lambda a: np.concatenate([a,np.zeros(N-len(a))]) if len(a)<N else a[:N]
    eLp=pad(eL); tt=pad(tL)
    r_e=np.sqrt(np.mean(eLp[S-xf:S]**2)+1e-20); r_t=np.sqrt(np.mean(tt[S:S+xf]**2)+1e-20); tt=tt*(r_e/(r_t+1e-12))
    a=S-xf//2; b=S+xf//2; w=np.linspace(1,0,b-a); out=np.zeros(N)
    out[:a]=eLp[:a]; out[a:b]=eLp[a:b]*w+tt[a:b]*(1-w); out[b:]=tt[b:]; return out
def rc(m,win=512,hop=64,tmax=0.6):
    o=rb.onset(m); m=m[o:]; w=np.hanning(win); f=np.fft.rfftfreq(win,1/SR); s=f>40; c=[];t=[]
    for i in range(0,int(tmax*SR)-win,hop):
        fr=np.abs(np.fft.rfft(m[i:i+win]*w)); c.append(np.sum(f[s]*fr[s])/(np.sum(fr[s])+1e-20)); t.append(i/SR*1000)
    return np.array(t),np.array(c)

refL,refR=align(rb.load_ref("ir/vintage-plate-1.5s.wav"))
DB=dict(EARLY="0",VLV="1",DARKG="0.85",DARKHZ="5000",VLVLV="1.4")
tL,tR=render_tail(DB)
kL,kR=load_kernel()
cL=splice(kL,tL); cR=splice(kR,tR)
np.stack([cL,cR],1).astype('<f4').tofile("/tmp/kernel_composite.f32")
# also: old FULL (dark EarlyField on) and dark-body tail alone, for reference
ftL,ftR=render_tail(dict(EARLY="1",VLV="1",DARKG="0.85",DARKHZ="5000",VLVLV="1.4"))
curves={
 'reference':(refL+refR)/2,
 'KERNEL+dark-body tail (true composite)':(cL+cR)/2,
 'dark-body tail only (EARLY off)':(tL+tR)/2,
 'old FULL (dark EarlyField on)':(ftL+ftR)/2,
}
fig,ax=plt.subplots(figsize=(11,6))
print(f"{'curve':40} | {'@50':>5}{'@150':>6}{'@300':>6}{'@500':>6}  {'drop50-300':>10}")
for nm,m in curves.items():
    t,c=rc(m); at=lambda ms:c[min(np.searchsorted(t,ms),len(c)-1)]
    lw=2.6 if nm=='reference' else 1.7
    ax.plot(t,c,'k-' if nm=='reference' else '-',lw=lw,label=nm)
    print(f"{nm:40} | {at(50):5.0f}{at(150):6.0f}{at(300):6.0f}{at(500):6.0f}  {at(50)-at(300):10.0f}")
for x in (50,150,300,500): ax.axvline(x,color='grey',ls=':',alpha=.3)
ax.set_title("True composite: accepted bright kernel + current dark-body tail\nvs reference (anchor knob1.5)"); ax.set_xlabel("ms"); ax.set_ylabel("centroid Hz"); ax.set_xlim(0,600); ax.legend(fontsize=9); ax.grid(alpha=.3)
fig.tight_layout(); fig.savefig("kernel_composite.png",dpi=120); print("saved kernel_composite.png")
