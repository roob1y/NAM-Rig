import numpy as np, json, engine, loss
import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
sr=48000
def env_db(x,fc,sm=20):
    X=np.fft.rfft(x);f=np.fft.rfftfreq(len(x),1/sr);lo,hi=fc/2**.5,fc*2**.5
    yb=np.fft.irfft(X*((f>=lo)&(f<hi)),len(x));e=yb**2
    k=int(sm*1e-3*sr);env=np.convolve(e,np.ones(k)/k,'same');env/=env.max()+1e-20
    return 10*np.log10(env+1e-5)
ir=np.load('bx20_ir.npy'); bx=0.5*(ir[:,0]+ir[:,1])
th=json.load(open('best_theta.json')); th['stereo']=0.65
eL,eR=engine.render_ir(th,n_sec=3.5); en=0.5*(eL+eR)
fig,ax=plt.subplots(1,2,figsize=(13,4.6))
# (a) bloom: low-mid vs high band energy build, first 350ms
t=np.arange(int(0.35*sr))/sr*1000
for fc,c in [(500,'#c0392b'),(4000,'#2980b9')]:
    eb=env_db(bx,fc)[:len(t)]; ee=env_db(en,fc)[:len(t)]
    ax[0].plot(t,eb,c=c,lw=2,label=f'studio spring {fc}Hz')
    ax[0].plot(t,ee,c=c,lw=1.4,ls='--',label=f'ours {fc}Hz')
ax[0].axvline(80,color='#888',ls=':',lw=1); ax[0].text(82,-3,'~80ms bloom',color='#555',fontsize=8)
ax[0].set_xlabel('time after onset (ms)'); ax[0].set_ylabel('band energy (dB, norm)')
ax[0].set_title('The bloom: low-mids (red) build to a late peak;\nhighs (blue) are immediate'); ax[0].set_ylim(-18,1); ax[0].legend(fontsize=8); ax[0].grid(alpha=.3)
# (b) RT60 vs freq
B=[250,500,1000,2000,4000]
rb=[loss.rt60_s(bx,sr,fc) for fc in B]; re=[loss.rt60_s(en,sr,fc) for fc in B]
x=np.arange(len(B))
ax[1].plot(x,rb,'o-',c='#c0392b',lw=2,label='studio spring (recovered IR)')
ax[1].plot(x,re,'s--',c='#27ae60',lw=2,label='ours (optimized)')
ax[1].set_xticks(x); ax[1].set_xticklabels([f'{f}' for f in B]); ax[1].set_xlabel('frequency (Hz)')
ax[1].set_ylabel('RT60 (s)'); ax[1].set_title('Decay vs frequency: lows ring longest'); ax[1].legend(fontsize=9); ax[1].grid(alpha=.3)
plt.tight_layout(); plt.savefig('spring_optimizer_analysis.png',dpi=110); print('figure saved')
