import sys,os,subprocess;sys.path.insert(0,'.')
import numpy as np, matplotlib; matplotlib.use("Agg")
import matplotlib.pyplot as plt
import reverb_battery as rb
SR=rb.sr
def ir_render(cmd,cwd,env):
    out=f"/tmp/mu_{abs(hash((cmd,tuple(sorted(env.items())))))%99999}.f32"
    subprocess.run([cmd,"plate",os.path.abspath("impulse.f32"),out],cwd=cwd,
                   env={**os.environ,"RV_T60":"2.45",**env},check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    return rb.load_ours(out)
ref=rb.load_ref("ir/vintage-plate-1.5s.wav")
shipped=ir_render("./render_character",".",{})
clean=ir_render("../plate_proto/render_proto","../plate_proto",{"HFM":"1","VLV":"1","DIFF":"0"})
dense=ir_render("../plate_proto/render_proto","../plate_proto",{"HFM":"1","VLV":"1","DIFF":"1"})
cands=[("reference","k",ref),("shipped-v3","tab:gray",shipped),("clean-hybrid (DIFF off)","tab:green",clean),("dense (DIFF on)","tab:red",dense)]
def mono(c):L,R=c;return (L+R)/2
def smooth_spec(m,frac=1/12):
    on=rb.onset(m);seg=m[on:on+int(2.0*SR)]*np.hanning(int(2.0*SR)) if len(m[on:])>=int(2.0*SR) else m[on:]*np.hanning(len(m[on:]))
    X=np.abs(np.fft.rfft(seg));f=np.fft.rfftfreq(len(seg),1/SR)
    # log-smooth
    out=np.zeros_like(X)
    lf=np.log(f+1e-9)
    for i in range(len(f)):
        lo=lf[i]-frac; hi=lf[i]+frac; sel=(lf>=lo)&(lf<=hi); out[i]=np.sqrt(np.mean(X[sel]**2)+1e-20)
    return f,20*np.log10(out+1e-20)
fig,ax=plt.subplots(1,2,figsize=(17,6))
for nm,col,c in cands:
    f,S=smooth_spec(mono(c));ref_idx=np.argmin(np.abs(f-1000));S=S-S[ref_idx]
    ax[0].semilogx(f,S,color=col,label=nm,lw=1.3)
ax[0].set_xlim(100,20000);ax[0].set_ylim(-40,10);ax[0].axvline(6000,color='gray',ls=':',lw=.8)
ax[0].set_title("Wet magnitude spectrum (1/12-oct, norm @1k) -- HF extension / 'muffle'");ax[0].set_xlabel("Hz");ax[0].legend();ax[0].grid(alpha=.3,which='both')
# HF detail / number
print("HF energy (>8kHz vs 1-4kHz, dB):")
for nm,col,c in cands:
    f,S=smooth_spec(mono(c))
    hf=np.mean(S[(f>=8000)&(f<=14000)]); mid=np.mean(S[(f>=1000)&(f<=4000)])
    print(f"  {nm:24s} HF-mid = {hf-mid:+.1f} dB")
# onset transient 0-8ms (sharpness)
N=int(0.008*SR)
for nm,col,c in cands:
    m=mono(c);on=rb.onset(m);seg=np.abs(m[on:on+N])
    ax[1].plot(np.arange(N)/SR*1000,seg/ (np.max(seg)+1e-9),color=col,label=nm,lw=1)
ax[1].set_title("Onset transient 0-8ms (|x|, peak-norm) -- sharpness/'quality'");ax[1].set_xlabel("ms");ax[1].legend();ax[1].grid(alpha=.3)
fig.suptitle("'Muffled / bad quality' diagnosis: HF extension + transient sharpness",fontsize=13)
plt.tight_layout();plt.savefig("muffle_diag.png",dpi=120);plt.close();print("wrote muffle_diag.png")
