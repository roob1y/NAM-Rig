import sys;sys.path.insert(0,'.')
import numpy as np, matplotlib; matplotlib.use("Agg")
import matplotlib.pyplot as plt
import reverb_battery as rb
SR=rb.sr
def analytic(x):
    n=len(x);X=np.fft.fft(x);h=np.zeros(n);h[0]=1
    if n%2==0:h[n//2]=1;h[1:n//2]=2
    else:h[1:(n+1)//2]=2
    return np.fft.ifft(X*h)
def env_db(x):
    e=np.abs(analytic(x));return 20*np.log10(e/ (np.max(e)+1e-20)+1e-9)
# Abel-Huang echo density buildup: fraction of |x| > local std, normalized to erfc(1/sqrt2)=0.3173
def echo_density(x,win=0.010):
    w=int(win*SR);n=len(x);out=np.full(n,np.nan)
    step=64
    for i in range(0,n-w,step):
        seg=x[i:i+w];s=np.std(seg)+1e-20
        out[i:i+step]=np.mean(np.abs(seg)>s)/0.3173
    return out
ref=rb.load_ref("ir/vintage-plate-1.5s.wav"); hyb=rb.load_ours("/tmp/hyb_ir.f32")
rL,rR=ref; hL,hR=hyb
def onset_al(L,R):
    m=(L+R)/2;o=rb.onset(m);return L[o:],R[o:],(L[o:]+R[o:])/2
rL,rR,rm=onset_al(rL,rR); hL,hR,hm=onset_al(hL,hR)

fig,ax=plt.subplots(3,2,figsize=(16,12))
t_ms=lambda n:np.arange(n)/SR*1000
# 1 early waveform 0-50ms
N1=int(0.05*SR)
ax[0,0].plot(t_ms(N1),rm[:N1],'k',lw=.6,label='reference');ax[0,0].plot(t_ms(N1),hm[:N1]+0,'g',lw=.6,alpha=.7,label='hybrid')
ax[0,0].set_title("IR waveform 0-50ms (early echo pattern)");ax[0,0].set_xlabel("ms");ax[0,0].legend()
# 2 envelope dB 0-400ms
N2=int(0.4*SR)
ax[0,1].plot(t_ms(N2),env_db(rm[:N2]),'k',lw=1,label='reference');ax[0,1].plot(t_ms(N2),env_db(hm[:N2]),'g',lw=1,label='hybrid')
ax[0,1].set_title("Broadband envelope 0-400ms dB (attack/build/early decay)");ax[0,1].set_xlabel("ms");ax[0,1].set_ylim(-60,2);ax[0,1].legend()
# 3 echo density buildup 0-150ms
N3=int(0.15*SR)
edr_=echo_density(rm[:N3]);edh=echo_density(hm[:N3])
ax[1,0].plot(t_ms(N3),edr_,'k',label='reference');ax[1,0].plot(t_ms(N3),edh,'g',label='hybrid')
ax[1,0].axhline(1,color='gray',ls='--',lw=.7);ax[1,0].set_title("Echo-density buildup (1.0 = fully diffuse/Gaussian)");ax[1,0].set_xlabel("ms");ax[1,0].legend()
# 4 broadband EDC full
def edc(x):
    e=np.cumsum(x[::-1]**2)[::-1];return 10*np.log10(e/(e[0]+1e-20)+1e-20)
N4=int(2.5*SR)
ax[1,1].plot(np.arange(N4)/SR,edc(rm)[:N4],'k',label='reference');ax[1,1].plot(np.arange(N4)/SR,edc(hm)[:N4],'g',label='hybrid')
ax[1,1].set_title("Broadband energy decay (Schroeder)");ax[1,1].set_xlabel("s");ax[1,1].set_ylim(-70,2);ax[1,1].legend()
# 5,6 spectrograms 0-500ms
def spec(ax_,x,ttl):
    N=int(0.5*SR);win=512;hop=128;frames=[]
    for i in range(0,N-win,hop):frames.append(20*np.log10(np.abs(np.fft.rfft(x[i:i+win]*np.hanning(win)))+1e-6))
    S=np.array(frames).T;f=np.fft.rfftfreq(win,1/SR)
    ax_.imshow(S,origin='lower',aspect='auto',extent=[0,N/SR*1000,0,f[-1]/1000],vmin=-50,vmax=20,cmap='magma')
    ax_.set_title(ttl);ax_.set_xlabel("ms");ax_.set_ylabel("kHz");ax_.set_ylim(0,14)
spec(ax[2,0],rm,"Spectrogram reference 0-500ms");spec(ax[2,1],hm,"Spectrogram hybrid 0-500ms")
fig.suptitle("TIME-DOMAIN diagnostics (what the energy/decay battery misses): reference vs hybrid",fontsize=14)
plt.tight_layout(rect=[0,0,1,0.97]);plt.savefig("timed_diag.png",dpi=110);plt.close()
# numbers: time to reach echo-density 0.9
def t_dense(ed):
    i=np.where(ed>=0.9)[0];return i[0]/SR*1000 if len(i) else np.nan
print("echo-density reaches 0.9 at:  ref %.1f ms   hybrid %.1f ms"%(t_dense(echo_density(rm[:int(0.2*SR)])),t_dense(echo_density(hm[:int(0.2*SR)]))))
# peak/attack: time of envelope max and value at 1ms,5ms
print("env @1ms: ref %.1f hyb %.1f dB | @5ms: ref %.1f hyb %.1f | @20ms: ref %.1f hyb %.1f"%(
 env_db(rm[:int(0.4*SR)])[int(0.001*SR)],env_db(hm[:int(0.4*SR)])[int(0.001*SR)],
 env_db(rm[:int(0.4*SR)])[int(0.005*SR)],env_db(hm[:int(0.4*SR)])[int(0.005*SR)],
 env_db(rm[:int(0.4*SR)])[int(0.02*SR)],env_db(hm[:int(0.4*SR)])[int(0.02*SR)]))
print("wrote timed_diag.png")
