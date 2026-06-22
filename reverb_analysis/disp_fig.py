import sys,os,subprocess;sys.path.insert(0,'.')
import numpy as np, matplotlib; matplotlib.use("Agg")
import matplotlib.pyplot as plt
import reverb_battery as rb
SR=rb.sr
def analytic(x):
    n=len(x);X=np.fft.fft(x);h=np.zeros(n);h[0]=1
    if n%2==0:h[n//2]=1;h[1:n//2]=2
    else:h[1:(n+1)//2]=2
    return np.fft.ifft(X*h)
def bp(x,f0,bw,nfft):
    X=np.fft.rfft(x,nfft);f=np.fft.rfftfreq(nfft,1/SR);g=np.exp(-0.5*((f-f0)/(bw/2))**2)
    return np.fft.irfft(X*g,nfft)[:len(x)]
def ir_render(cmd,cwd,env):
    out=f"/tmp/df_{abs(hash((cmd,tuple(sorted(env.items())))))%99999}.f32"
    subprocess.run([cmd,"plate",os.path.abspath("impulse.f32"),out],cwd=cwd,env={**os.environ,"RV_T60":"2.45",**env},check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    return rb.load_ours(out)
def mono(c):L,R=c;m=(L+R)/2;return m[rb.onset(m):]
ref=mono(rb.load_ref("ir/vintage-plate-1.5s.wav"))
hyb=mono(ir_render("../plate_proto/render_proto","../plate_proto",{"HFM":"1","VLV":"1","DIFF":"0"}))
bands=[400,1000,3000,6000,9000]; cols=plt.cm.viridis(np.linspace(0,.9,len(bands)))
fig,ax=plt.subplots(2,3,figsize=(18,9))
N=int(0.15*SR);nfft=1<<16;tt=np.arange(N)/SR*1000
for sig,axi,ttl in [(ref,ax[0,0],"REFERENCE per-band onset"),(hyb,ax[0,1],"HYBRID per-band onset")]:
    for f0,col in zip(bands,cols):
        en=np.abs(analytic(bp(sig[:N],f0,max(80,0.18*f0),nfft)));en=en/(np.max(en)+1e-12)
        axi.plot(tt,en,color=col,label=f"{f0}Hz",lw=1.2)
    axi.set_title(ttl);axi.set_xlabel("ms");axi.set_ylabel("norm env");axi.legend(fontsize=7)
# energy arrival (peak time) vs freq
fc=np.geomspace(250,11000,24)
def arr(sig):
    return [np.argmax(np.abs(analytic(bp(sig[:int(0.2*SR)],f0,max(80,0.18*f0),1<<16))))/SR*1000 for f0 in fc]
ax[0,2].semilogx(fc,arr(ref),'k-o',ms=3,label="reference");ax[0,2].semilogx(fc,arr(hyb),'g-o',ms=3,label="hybrid")
ax[0,2].set_title("DISPERSION: energy-peak arrival vs freq");ax[0,2].set_xlabel("Hz");ax[0,2].set_ylabel("peak time (ms)");ax[0,2].legend();ax[0,2].grid(alpha=.3,which='both')
# brightness over time
res=np.load('/tmp/fm_res.npy',allow_pickle=True).item()
for nm,col in [("reference","k"),("hybrid","g")]:
    ax[1,0].plot(res[nm]['_ctimes']*1000,res[nm]['_cents'],color=col,label=nm)
ax[1,0].set_title("Brightness over time (running centroid)");ax[1,0].set_xlabel("ms");ax[1,0].set_ylabel("Hz");ax[1,0].legend();ax[1,0].grid(alpha=.3);ax[1,0].set_xlim(0,400)
# modulation spectrum
for nm,col in [("reference","k"),("hybrid","g")]:
    mf=res[nm]['_modf'];ms_=res[nm]['_modspec'];s=(mf>=1)&(mf<=150)
    ax[1,1].semilogx(mf[s],20*np.log10(ms_[s]/np.max(ms_[s])+1e-9),color=col,label=nm)
ax[1,1].set_title("Modulation spectrum (temporal texture)");ax[1,1].set_xlabel("mod Hz");ax[1,1].set_ylabel("dB");ax[1,1].legend();ax[1,1].grid(alpha=.3,which='both')
# ERB EDT
for nm,col in [("reference","k"),("hybrid","g")]:
    ax[1,2].semilogx(res[nm]['_erb_fc'],res[nm]['_erb_edt'],color=col,label=nm,lw=1)
ax[1,2].set_title("Per-ERB-band EDT (s)");ax[1,2].set_xlabel("Hz");ax[1,2].set_ylabel("s");ax[1,2].legend();ax[1,2].grid(alpha=.3,which='both')
fig.suptitle("NEW measurements: dispersion / bloom-timing / brightness-evolution / modulation / per-band decay",fontsize=14)
plt.tight_layout(rect=[0,0,1,0.97]);plt.savefig("dispersion_analysis.png",dpi=110);plt.close();print("wrote dispersion_analysis.png")
print("arr_LF(ref) %.0fms arr_HF(ref) %.0fms  | arr_LF(hyb) %.0fms arr_HF(hyb) %.0fms"%(
 arr(ref)[2],arr(ref)[-1],arr(hyb)[2],arr(hyb)[-1]))
