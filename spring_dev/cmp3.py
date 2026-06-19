import numpy as np, struct
import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
def rd(p):
    d=open(p,'rb').read(); i=12;af=1;ch=2;bps=16;off=0;ln=0;sr=48000
    while i+8<=len(d):
        c=d[i:i+4]; sz=struct.unpack('<I',d[i+4:i+8])[0]
        if c==b'fmt ': af,ch,sr,_,_,bps=struct.unpack('<HHIIHH',d[i+8:i+8+16])
        elif c==b'data': off=i+8;ln=sz
        i+=8+sz+(sz&1)
    a=np.frombuffer(d[off:off+ln],dtype='<f4') if af==3 else np.frombuffer(d[off:off+ln],dtype='<i2').astype(np.float32)/32768
    a=a.reshape(-1,ch); return 0.5*(a[:,0]+(a[:,1] if ch>1 else a[:,0])),sr
def trim(x,sr): pk=np.max(np.abs(x)); i=np.argmax(np.abs(x)>0.01*pk); return x[i:]
def edc(x):
    e=x**2; c=np.cumsum(e[::-1])[::-1]; return 10*np.log10(c/c[0]+1e-30)
def rt60band(x,sr):
    n=len(x);X=np.fft.rfft(x);f=np.fft.rfftfreq(n,1/sr);cs=[63,125,250,500,1000,2000,4000,8000];out=[]
    for c in cs:
        lo,hi=c/2**(1/6),c*2**(1/6);H=((f>=lo)&(f<hi)).astype(float)
        e=np.fft.irfft(X*H,n)**2;d=np.cumsum(e[::-1])[::-1];d=10*np.log10(d/d[0]+1e-30)
        i5=np.argmax(d<=-5);i25=np.argmax(d<=-25);out.append(3*(i25-i5)/sr if i25>i5>0 else np.nan)
    return cs,out
def stft(x,sr,nfft=2048,hop=512):
    w=np.hanning(nfft);F=[]
    for s in range(0,len(x)-nfft,hop):F.append(np.abs(np.fft.rfft(x[s:s+nfft]*w)))
    S=np.array(F).T;S/=S.max()+1e-12;return 20*np.log10(S+1e-6),hop/sr
U="/sessions/admiring-inspiring-cerf/mnt/uploads/NEVO - studio spring Stereo 3.0s.wav"
xb,sr=rd(U); xo,_=rd('imp23.wav'); xb=trim(xb,sr); xo=trim(xo,sr)
plt.style.use('dark_background');fig=plt.figure(figsize=(15,11));fig.patch.set_facecolor('#243044')
gs=fig.add_gridspec(2,2,hspace=0.3,wspace=0.2)
# broadband decay overlay (ring-out)
ax=fig.add_subplot(gs[0,0]);db_b=edc(xb);db_o=edc(xo);tb=np.arange(len(db_b))/sr;to=np.arange(len(db_o))/sr
ax.plot(tb,db_b,color='#ff5a7a',lw=2,label='Real studio spring');ax.plot(to,db_o,color='#46e0c0',lw=2,label='Ours (23)')
ax.axhline(-60,color='#888',ls='--',lw=.7);ax.set_xlim(0,6);ax.set_ylim(-80,2);ax.grid(True,color='#3d4f6b',lw=.4);ax.legend(fontsize=10,framealpha=.2)
ax.set_title('Broadband energy decay (ring-out time)',color='w');ax.set_xlabel('s');ax.set_ylabel('dB')
# per-band RT60
ax2=fig.add_subplot(gs[0,1]);cb,rb=rt60band(xb,sr);co,ro=rt60band(xo,sr)
ax2.semilogx(cb,rb,'o-',color='#ff5a7a',lw=2,label='studio spring');ax2.semilogx(co,ro,'o-',color='#46e0c0',lw=2,label='ours 23')
ax2.set_xlim(50,10000);ax2.set_ylim(0,7);ax2.grid(True,which='both',color='#3d4f6b',lw=.4);ax2.legend(fontsize=9,framealpha=.2)
ax2.set_title('RT60 vs frequency (decay length per band)',color='w');ax2.set_xticks([63,125,250,500,1000,2000,4000,8000]);ax2.set_xticklabels(['63','125','250','500','1k','2k','4k','8k'])
# spectrograms 0-3s
for col,(x,nm) in enumerate([(xb,'studio spring'),(xo,'ours 23')]):
    a=fig.add_subplot(gs[1,col]);S,dt=stft(x,sr)
    a.imshow(S,origin='lower',aspect='auto',extent=[0,S.shape[1]*dt,0,sr/2/1000],cmap='magma',vmin=-70,vmax=0)
    a.set_ylim(0,8);a.set_xlim(0,3);a.set_title(f'{nm} spectrogram',color='w');a.set_xlabel('s');a.set_ylabel('kHz')
fig.suptitle('Folder 23 vs Real studio spring — decay / ring-out analysis (decay knob = 3.0)',color='w',fontsize=15,y=0.95)
fig.savefig('decay_cmp.png',dpi=105,facecolor=fig.get_facecolor(),bbox_inches='tight')
print("RT60 (s) per band:")
print("  band: 63   125  250  500  1k   2k   4k   8k")
print("  studio spring:",' '.join(f'{v:4.1f}' for v in rb))
print("  ours:",' '.join(f'{v:4.1f}' for v in ro))
# overall -60dB time
def t60pt(db,sr): i=np.argmax(db<=-60); return i/sr if i>0 else len(db)/sr
print(f"time to -60dB:  studio spring {t60pt(db_b,sr):.2f}s   ours {t60pt(db_o,sr):.2f}s")
print("saved decay_cmp.png")
