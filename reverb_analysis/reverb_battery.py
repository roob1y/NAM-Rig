# reverb_battery.py — full metric battery: our render (.f32 stereo) vs a
# reference IR (.wav stereo). Reuses the proven plate_proto definitions and adds
# EDT/T30, C80, mid/side-per-band, and the EDR decay surface. Saves graphs.
#
# usage: python3 reverb_battery.py --ours wet_plate_2.0.f32 \
#          --ref "ir/<your reference>.wav" --label 2.0 --out ../../outputs
import numpy as np, argparse, os
from wavutil import read_wav

sr = 48000.0
FC = np.array([62.5,125,180,250,355,500,710,1000,1400,2000,2800,4000,5600,8000,11000.])
OCT = np.array([125,250,500,1000,2000,4000,8000.])

def load_ours(p):
    a = np.fromfile(p, dtype='<f4').reshape(-1,2); return a[:,0], a[:,1]
def load_ref(p):
    a,_ = read_wav(p); a=a[:, :2]; return a[:,0], a[:,1]

def onset(m): return int(np.argmax(np.abs(m) > 0.01*np.max(np.abs(m))))

def bp(x, fc, Q=2.0):  # zero-phase-ish band-pass via FFT of an RBJ BPF
    w0=2*np.pi*fc/sr; al=np.sin(w0)/(2*Q); c=np.cos(w0)
    b0,b2=al/(1+al),-al/(1+al); a1,a2=(-2*c)/(1+al),(1-al)/(1+al)
    nfft=1<<int(np.ceil(np.log2(len(x)+8))); X=np.fft.rfft(x,nfft)
    om=2*np.pi*np.fft.rfftfreq(nfft); z=np.exp(-1j*om)
    H=(b0+b2*z*z)/(1+a1*z+a2*z*z); return np.fft.irfft(X*H,nfft)[:len(x)]

def schroeder_db(x):
    e=x**2; sch=np.cumsum(e[::-1])[::-1]; sch/=sch[0]+1e-20
    return 10*np.log10(sch+1e-20)

def decaytime(x, lo, hi):  # seconds to fall (hi-lo) dB, extrapolated to 60
    db=schroeder_db(x); i1=np.argmax(db<=lo); i2=np.argmax(db<=hi)
    if i2<=i1: return np.nan
    return (i2-i1)/sr*(60.0/(lo-hi))

def per_band_decay(m, win, lo, hi):
    on=onset(m); seg=m[on:on+int(sr*win)]
    return np.array([decaytime(bp(seg,fc), lo, hi) for fc in FC])

def oct_spectrum(m, win):
    on=onset(m); seg=m[on:on+int(sr*win)]
    X=np.abs(np.fft.rfft(seg*np.hanning(len(seg))))**2; f=np.fft.rfftfreq(len(seg),1/sr); out=[]
    for c in OCT:
        lof,hif=c/2**0.5,c*2**0.5; s=(f>=lof)&(f<hif); out.append(10*np.log10(np.mean(X[s])+1e-30))
    return np.array(out)

def c80(m):  # clarity: early(<80ms)/late energy in dB
    on=onset(m); seg=m[on:]; k=int(0.08*sr)
    early=np.sum(seg[:k]**2); late=np.sum(seg[k:]**2)+1e-20
    return 10*np.log10(early/late)

def c80_band(m, win=6.0):
    on=onset(m); seg=m[on:on+int(sr*win)]
    return np.array([c80(bp(seg,fc)) for fc in OCT])

def ned(m, win=0.020):  # normalised echo density buildup
    on=onset(m); x=m[on:]; w=int(win*sr); n=len(x)//w; vals=[]
    for k in range(min(n,60)):
        seg=x[k*w:(k+1)*w]; s=np.std(seg)+1e-20
        vals.append(np.mean(np.abs(seg)>s)/0.3173)
    return np.array(vals)

def midside_band(L, R, win=4.0):
    M=(L+R)/2; S=(L-R)/2; on=onset(M)
    Ms=M[on:on+int(sr*win)]; Ss=S[on:on+int(sr*win)]
    out=[]
    for c in OCT:
        bm=bp(Ms,c); bs=bp(Ss,c)
        em=np.mean(bm**2)+1e-20; es=np.mean(bs**2)+1e-20
        out.append(10*np.log10(es/em))  # side-vs-mid dB (width per band)
    return np.array(out)

def centroid(m):
    on=onset(m); x=m[on:on+int(sr*3)]
    X=np.abs(np.fft.rfft(x*np.hanning(len(x)))); f=np.fft.rfftfreq(len(x),1/sr)
    return float(np.sum(f*X)/(np.sum(X)+1e-20))

def modal_depth(m):
    # std (dB) of the 2-6 kHz tail fine-spectrum = THE lush-vs-digital discriminator.
    # EDR/RT60 are BLIND to this. LOW (~5-6) = flat/featureless = "digital/metallic";
    # HIGH (~7-11) = peaky distinct modes = "lush". For an FDN: strong in-loop HF
    # damping -> sparse distinct modes -> HIGH modal depth (warm=lush); a bright/
    # undamped tail -> dense overlapping modes -> flat -> digital. Match the reference.
    on=onset(m); seg=m[on+int(0.4*sr):on+int(0.9*sr)]
    if len(seg)<256: return float('nan')
    X=20*np.log10(np.abs(np.fft.rfft(seg*np.hanning(len(seg))))+1e-7)
    fr=np.fft.rfftfreq(len(seg),1/sr)
    return float(np.std(X[(fr>=2000)&(fr<=6000)]))

def edr(m, tmax=4.0):
    win=1024; hop=256; m=m[:int(sr*tmax)]; w=np.hanning(win); fr=[]
    for i in range(0,len(m)-win,hop): fr.append(np.abs(np.fft.rfft(m[i:i+win]*w))**2)
    S=np.array(fr); E=np.cumsum(S[::-1],axis=0)[::-1]; E/=E[0:1]+1e-20
    return np.arange(S.shape[0])*hop/sr, np.fft.rfftfreq(win,1/sr), 10*np.log10(E+1e-12)

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--ours',required=True); ap.add_argument('--ref',required=True)
    ap.add_argument('--label',required=True); ap.add_argument('--out',default='.')
    a=ap.parse_args()
    oL,oR=load_ours(a.ours); rL,rR=load_ref(a.ref)
    oM=(oL+oR)/2; rM=(rL+rR)/2
    os.makedirs(a.out,exist_ok=True)

    # align onsets to t=0 for both
    print(f"\n===== PLATE BATTERY  ours vs reference  (label {a.label}s) =====")
    # broadband decay
    t30_o=decaytime(schroeder_index(oM),-5,-35) if False else None
    T30o=per_band_decay(oM,6.0,-5,-35); T30r=per_band_decay(rM,6.0,-5,-35)
    EDTo=per_band_decay(oM,6.0,0,-10);  EDTr=per_band_decay(rM,6.0,0,-10)
    So=oct_spectrum(oM,3.0); So-=So[3]; Sr=oct_spectrum(rM,3.0); Sr-=Sr[3]
    C80o=c80_band(oM); C80r=c80_band(rM)
    NEDo=ned(oM); NEDr=ned(rM)
    MSo=midside_band(oL,oR); MSr=midside_band(rL,rR)
    ceno=centroid(oM); cenr=centroid(rM)
    mdo=modal_depth(oM); mdr=modal_depth(rM)
    corro=float(np.corrcoef(oL[onset(oM):onset(oM)+int(sr)],oR[onset(oM):onset(oM)+int(sr)])[0,1])
    corrr=float(np.corrcoef(rL[onset(rM):onset(rM)+int(sr)],rR[onset(rM):onset(rM)+int(sr)])[0,1])

    def row(name,o,r):
        print(f"{name:10s} " + " ".join(f"{v:5.2f}" for v in o))
        print(f"{'  REF':10s} " + " ".join(f"{v:5.2f}" for v in r))
        print(f"{'  d':10s} " + " ".join(f"{(o[i]-r[i]):+5.2f}" for i in range(len(o))))
    print("\nFC:", " ".join(f"{int(f):5d}" for f in FC))
    print("-- T30(f) seconds --"); row("T30",T30o,T30r)
    print("-- EDT(f) seconds --"); row("EDT",EDTo,EDTr)
    print("\nOCT:", " ".join(f"{int(f):5d}" for f in OCT))
    print("-- tonal balance dB (norm @1k) --"); row("spec",So,Sr)
    print("-- C80 clarity dB --");             row("C80",C80o,C80r)
    print("-- side/mid dB per band (width) --");row("M/S",MSo,MSr)
    print(f"\nmodal depth: ours {mdo:.1f}dB  ref {mdr:.1f}dB   (LUSH vs digital; higher=lusher; EDR/RT60 are blind to this. d={mdo-mdr:+.1f})")
    print(f"centroid: ours {ceno:.0f}Hz  ref {cenr:.0f}Hz   L/R corr: ours {corro:+.2f} ref {corrr:+.2f}")
    print(f"NED@40ms: ours {NEDo[1]:.2f} ref {NEDr[1]:.2f}   NED@200ms: ours {NEDo[9]:.2f} ref {NEDr[9]:.2f}")
    print(f"|T30 err| mean {np.nanmean(np.abs(T30o-T30r)):.2f}s max {np.nanmax(np.abs(T30o-T30r)):.2f}s")
    print(f"|spec err| mean {np.mean(np.abs(So-Sr)):.2f}dB max {np.max(np.abs(So-Sr)):.2f}dB")

    # ---- graphs ----
    import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
    fig=plt.figure(figsize=(15,11)); fig.suptitle(f"Plate (PlateFdn) vs reference IR  —  {a.label}s",fontsize=14,weight='bold')
    ax=plt.subplot(2,3,1); ax.semilogx(FC,T30r,'o-',label='reference',color='#222')
    ax.semilogx(FC,T30o,'s--',label='ours',color='#c0392b'); ax.set_title('T30(f) decay'); ax.set_ylabel('s'); ax.legend(); ax.grid(alpha=.3,which='both')
    ax=plt.subplot(2,3,2); ax.semilogx(FC,EDTr,'o-',label='reference',color='#222')
    ax.semilogx(FC,EDTo,'s--',label='ours',color='#c0392b'); ax.set_title('EDT(f) early decay'); ax.set_ylabel('s'); ax.legend(); ax.grid(alpha=.3,which='both')
    ax=plt.subplot(2,3,3); ax.semilogx(OCT,Sr,'o-',label='reference',color='#222')
    ax.semilogx(OCT,So,'s--',label='ours',color='#c0392b'); ax.set_title('tonal balance (norm @1k)'); ax.set_ylabel('dB'); ax.legend(); ax.grid(alpha=.3,which='both')
    ax=plt.subplot(2,3,4); ax.semilogx(OCT,C80r,'o-',label='reference',color='#222')
    ax.semilogx(OCT,C80o,'s--',label='ours',color='#c0392b'); ax.set_title('C80 clarity'); ax.set_ylabel('dB'); ax.legend(); ax.grid(alpha=.3,which='both')
    ax=plt.subplot(2,3,5); ax.semilogx(OCT,MSr,'o-',label='reference',color='#222')
    ax.semilogx(OCT,MSo,'s--',label='ours',color='#c0392b'); ax.set_title('side/mid (width) per band'); ax.set_ylabel('dB'); ax.legend(); ax.grid(alpha=.3,which='both')
    ax=plt.subplot(2,3,6); tN=np.arange(len(NEDr))*20; ax.plot(tN,NEDr,'o-',label='reference',color='#222')
    tN2=np.arange(len(NEDo))*20; ax.plot(tN2,NEDo,'s--',label='ours',color='#c0392b'); ax.axhline(1,color='g',ls=':'); ax.set_title('echo density buildup'); ax.set_xlabel('ms'); ax.set_ylabel('NED'); ax.legend(); ax.grid(alpha=.3)
    plt.tight_layout(rect=[0,0,1,0.97]); p1=os.path.join(a.out,f'plate_battery_{a.label}.png'); plt.savefig(p1,dpi=110); plt.close()

    # EDR heatmaps side by side
    fig,axs=plt.subplots(1,2,figsize=(14,5));
    for ax,(m,nm) in zip(axs,[(rM,'reference'),(oM,'ours')]):
        t,f,E=edr(m); im=ax.pcolormesh(t,f/1000,E.T,vmin=-60,vmax=0,cmap='magma',shading='auto')
        ax.set_ylim(0,12); ax.set_title(f'EDR — {nm}'); ax.set_xlabel('s'); ax.set_ylabel('kHz'); fig.colorbar(im,ax=ax,label='dB')
    fig.suptitle(f'Energy Decay Relief  —  {a.label}s',weight='bold')
    plt.tight_layout(); p2=os.path.join(a.out,f'plate_edr_{a.label}.png'); plt.savefig(p2,dpi=110); plt.close()
    print("saved:",p1,p2)

def schroeder_index(x): return x  # placeholder (unused)

if __name__=='__main__': main()
