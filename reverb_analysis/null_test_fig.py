import numpy as np, sys; sys.path.insert(0,'.')
import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
import reverb_battery as rb, masking_abx as M
sr=rb.sr; FC=M.FC
oL,oR=rb.load_ours('/tmp/plate_anchor.f32'); rL,rR=rb.load_ref('ir/vintage-plate-1.5s.wav')
n=min(len(rL),len(oL)); oL,oR,rL,rR=oL[:n],oR[:n],rL[:n],rR[:n]
g=np.sqrt(np.sum(rL**2+rR**2)/np.sum(oL**2+oR**2)); oL*=g; oR*=g
om=(oL+oR)/2; rm=(rL+rR)/2; dL,dR=rb.load_ours('dry_guitar.f32')
def topenv(x,f1=5000,f2=12000):
    X=np.fft.rfft(x); f=np.fft.rfftfreq(len(x),1/sr); X[(f<f1)|(f>=f2)]=0
    y=np.fft.irfft(X,len(x)); w=int(0.01*sr)
    e=np.sqrt(np.convolve(y**2,np.ones(w)/w,'same')+1e-20); return 20*np.log10(e/np.max(e)+1e-12)
eo=topenv(om); er=topenv(rm); t=np.arange(n)/sr
fig,ax=plt.subplots(1,3,figsize=(17,5))
# A: 5-12k decay envelope ours vs ref
ax[0].plot(t,er,'k',lw=1.6,label='reference')
ax[0].plot(t,eo,'tab:red',lw=1.4,label='ours (shipped)')
ax[0].fill_between(t,er,eo,where=eo<er,color='tab:orange',alpha=.3)
ax[0].fill_between(t,er,eo,where=eo>=er,color='tab:blue',alpha=.3)
ax[0].set_xlim(0,2.0); ax[0].set_ylim(-60,2); ax[0].set_xlabel('s'); ax[0].set_ylabel('5-12kHz env (dB)')
ax[0].set_title('A. 5-12kHz decay SHAPE (level-matched IRs)\norange=ours too dark, blue=ours over-rings'); ax[0].legend(); ax[0].grid(alpha=.3)
ax[0].annotate('-5dB dip\n350-500ms',(0.45,-22),(0.7,-12),arrowprops=dict(arrowstyle='->'),fontsize=8)
ax[0].annotate('+5dB over-ring\n>1.2s',(1.4,-43),(1.0,-30),arrowprops=dict(arrowstyle='->'),fontsize=8)
# B: NMR calibration ladder
shifts=[0.5,1,2,3,6]; vals=[-11.3,-5.0,1.6,5.7,13.4]
ax[1].plot(shifts,vals,'o-',color='#333',label='flat top-band level error')
ax[1].axhline(0,color='g',ls=':',label='audibility threshold (NMR=0)')
ax[1].axhline(6.8,color='tab:red',ls='--',label='our STRUCTURAL residual (+6.8)')
ax[1].set_xlabel('top-band level error (dB)'); ax[1].set_ylabel('in-band NMR (dB)')
ax[1].set_title('B. Calibration: our residual ~ a +3-4 dB\ntop-band EQ error (in-band)'); ax[1].legend(fontsize=8); ax[1].grid(alpha=.3)
# C: per-band NMR of the full top-band swap vs full-band context
def band_nmr(om2,rm2,spl=85):
    Po=M.band_powers(om2);Pr=M.band_powers(rm2);k=min(len(Po),len(Pr));Po,Pr=Po[:k],Pr[:k]
    pk=np.percentile(np.abs(rm2),99.9)+1e-12;thr=M.masked_threshold(Pr,spl-20*np.log10(pk))[:k]
    gate=10*np.log10(Pr+1e-20)>-60;err=(np.sqrt(Po)-np.sqrt(Pr))**2
    out=[]
    for bi in range(len(FC)):
        gg=gate[:,bi]
        out.append(10*np.log10(err[gg,bi].sum()/(thr[gg,bi].sum()+1e-20)) if gg.sum() else np.nan)
    return np.array(out)
def topband(x,f1=5000,f2=12000):
    X=np.fft.rfft(x); f=np.fft.rfftfreq(len(x),1/sr); m=((f>=f1)&(f<f2)).astype(float)
    return np.fft.irfft(X*m,len(x))[:len(x)]
rtL,rtR=topband(rL),topband(rR); otL,otR=topband(oL),topband(oR)
scL=np.sqrt(np.sum(rtL**2)/np.sum(otL**2)); scR=np.sqrt(np.sum(rtR**2)/np.sum(otR**2))
hlmL=(rL-rtL)+otL*scL; hlmR=(rR-rtR)+otR*scR
refm=(M.fftconv(dL,rL)+M.fftconv(dR,rR)); hlmm=(M.fftconv(dL,hlmL)+M.fftconv(dR,hlmR))
k=min(len(refm),len(hlmm)); bn=band_nmr(hlmm[:k]/2,refm[:k]/2)
ax[2].axhline(0,color='g',ls=':'); ax[2].semilogx(FC,bn,'s-',color='tab:red')
ax[2].axvspan(5000,12000,color='tab:orange',alpha=.15)
ax[2].set_xlabel('Hz'); ax[2].set_ylabel('per-band NMR (dB)')
ax[2].set_title('C. Shape-only swap: audible difference is\nconfined to 5-12kHz (shaded); rest masked'); ax[2].grid(alpha=.3,which='both')
fig.suptitle('PLATE NULL TEST — is the 5-12kHz EDR residual audible?  Verdict: localized cue (in-band +6.8dB NMR, full-band -16dB), energy-matched, decay-shape only',fontsize=11,weight='bold')
plt.tight_layout(rect=[0,0,1,0.95]); plt.savefig('null_test.png',dpi=120); print('wrote null_test.png')
