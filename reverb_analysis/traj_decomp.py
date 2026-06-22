import sys,os,subprocess; sys.path.insert(0,'.')
import numpy as np, matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
import reverb_battery as rb
SR=rb.sr
IMP=os.path.abspath("../plate_proto/impulse_2p5.f32")
def render(env,binn="render_proto"):
    out=f"/tmp/td_{abs(hash(tuple(sorted(env.items()))))%999999}.f32"
    e={**os.environ,"RV_T60":"2.45","HFM":"0",**env}
    subprocess.run([f"../plate_proto/{binn}","plate",IMP,out],env=e,check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    d=np.fromfile(out,dtype='<f4').reshape(-1,2); return (d[:,0]+d[:,1])/2
def rc(m,win=512,hop=64,tmax=0.6):
    o=rb.onset(m); m=m[o:]; w=np.hanning(win); f=np.fft.rfftfreq(win,1/SR); s=f>40; c=[];t=[]
    for i in range(0,int(tmax*SR)-win,hop):
        fr=np.abs(np.fft.rfft(m[i:i+win]*w)); c.append(np.sum(f[s]*fr[s])/(np.sum(fr[s])+1e-20)); t.append(i/SR*1000)
    return np.array(t),np.array(c)
def load_ref():
    L,R=rb.load_ref("ir/vintage-plate-1.5s.wav"); return (L+R)/2
configs={
 'reference':None,
 'FULL (early+body+velvet)':dict(EARLY="1",VLV="1",DARKG="0.85",DARKHZ="5000",VLVLV="1.4"),
 'body only (no early,no velvet)':dict(EARLY="0",VLV="0",DARKG="0.85",DARKHZ="5000",VLVLV="1.4"),
 'body, dark OFF':dict(EARLY="0",VLV="0",DARKG="1.0",DARKHZ="5000",VLVLV="1.4"),
 'body+early':dict(EARLY="1",VLV="0",DARKG="0.85",DARKHZ="5000",VLVLV="1.4"),
 'body+velvet':dict(EARLY="0",VLV="1",DARKG="0.85",DARKHZ="5000",VLVLV="1.4"),
}
fig,ax=plt.subplots(figsize=(11,6))
print(f"{'config':32} | {'cen@50':>6}{'cen@150':>7}{'cen@300':>7}{'cen@500':>7}  drop50->300")
for nm,env in configs.items():
    m=load_ref() if env is None else render(env)
    t,c=rc(m); 
    def at(ms): return c[min(np.searchsorted(t,ms),len(c)-1)]
    style='k-' if env is None else '-'
    lw=2.6 if env is None else 1.6
    ax.plot(t,c,style,lw=lw,label=nm)
    print(f"{nm:32} | {at(50):6.0f}{at(150):7.0f}{at(300):7.0f}{at(500):7.0f}  {at(50)-at(300):8.0f}")
for x in (50,150,300,500): ax.axvline(x,color='grey',ls=':',alpha=.3)
ax.set_title("Centroid-trajectory decomposition (which stage owns the darkening shape?)\nanchor knob1.5 T60=2.45"); ax.set_xlabel("ms"); ax.set_ylabel("centroid Hz"); ax.set_xlim(0,600); ax.legend(fontsize=9); ax.grid(alpha=.3)
fig.tight_layout(); fig.savefig("traj_decomp.png",dpi=120); print("saved traj_decomp.png")
