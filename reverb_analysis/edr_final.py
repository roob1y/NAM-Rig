import sys,os;sys.path.insert(0,'.')
import numpy as np, matplotlib; matplotlib.use("Agg")
import matplotlib.pyplot as plt
import reverb_battery as rb
TMAX=3.0
def trim(m):return m[rb.onset(m):]
def mono(LR):return (LR[0]+LR[1])/2
ref=trim(mono(rb.load_ref("ir/vintage-plate-1.5s.wav")))
ship=trim(mono(rb.load_ours("/tmp/ship.f32")))
acc=trim(mono(rb.load_ours("/tmp/gl_ir.f32")))
sets=[("Reference (vintage plate)",ref),("Shipped plate (current)",ship),("Accepted (onset-fixed)",acc)]
t,f,Eref=rb.edr(ref,TMAX)
fig,ax=plt.subplots(2,3,figsize=(17,9))
for j,(nm,m) in enumerate(sets):
    t_,f_,E=rb.edr(m,TMAX)
    im=ax[0,j].pcolormesh(t_,f_/1000,E.T,vmin=-60,vmax=0,cmap="magma",shading="auto")
    ax[0,j].set_ylim(0,12);ax[0,j].set_title(nm,fontsize=11);ax[0,j].set_xlabel("s");ax[0,j].set_ylabel("kHz");fig.colorbar(im,ax=ax[0,j],label="dB")
for j,(nm,m) in enumerate([sets[1],sets[2]]):
    _,_,E=rb.edr(m,TMAX);n=min(E.shape[0],Eref.shape[0]);D=E[:n]-Eref[:n]
    axd=ax[1,j+1]
    im=axd.pcolormesh(t[:n],f/1000,D.T,vmin=-15,vmax=15,cmap="RdBu_r",shading="auto")
    axd.set_ylim(0,12);axd.set_title(f"({nm}) - reference",fontsize=11);axd.set_xlabel("s");axd.set_ylabel("kHz");fig.colorbar(im,ax=axd,label="dB diff")
# brightness over time (bottom-left)
win=512;hop=128
def br(m):
    c=[];tt=[]
    for i in range(0,int(0.5*rb.sr)-win,hop):
        fr=np.abs(np.fft.rfft(m[i:i+win]*np.hanning(win)));ff=np.fft.rfftfreq(win,1/rb.sr);s=ff>40
        c.append(np.sum(ff[s]*fr[s])/(np.sum(fr[s])+1e-20));tt.append(i/rb.sr*1000)
    return np.array(tt),np.array(c)
for nm,m,col in [("reference",ref,"k"),("shipped",ship,"tab:gray"),("accepted",acc,"tab:green")]:
    tb,cb=br(m);ax[1,0].plot(tb,cb,col,label=nm)
ax[1,0].set_title("Brightness over time 0-500ms");ax[1,0].set_xlabel("ms");ax[1,0].set_ylabel("centroid Hz");ax[1,0].legend(fontsize=8);ax[1,0].grid(alpha=.3)
fig.suptitle("Plate EDR @ anchor: reference vs shipped vs accepted (onset-fixed)",fontsize=13)
plt.tight_layout(rect=[0,0,1,0.97]);plt.savefig("edr_final.png",dpi=115);plt.close();print("wrote edr_final.png")
