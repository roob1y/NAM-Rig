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
    a=a.reshape(-1,ch); return a[:,0],(a[:,1] if ch>1 else a[:,0]),sr
def mono(p): L,R,sr=rd(p); return 0.5*(L+R),sr
def trim(x,sr): pk=np.max(np.abs(x)); i=np.argmax(np.abs(x)>0.01*pk); return x[i:]
U="/sessions/admiring-inspiring-cerf/mnt/uploads/NEVO - studio spring Stereo 3.0s.wav"
xb,sr=mono(U); xo,_=mono('bare_imp.wav')
# spectra (1/12-oct), aligned 0.5-2k
N=max(len(xb),len(xo)); F=np.fft.rfftfreq(N,1/sr)
def smooth(x):
    X=np.abs(np.fft.rfft(np.pad(x,(0,N-len(x)))*np.pad(np.hanning(len(x)),(0,N-len(x)))))
    fc=np.geomspace(20,20000,260); o=np.full_like(fc,np.nan)
    for k,c in enumerate(fc):
        lo,hi=c/2**(1/24),c*2**(1/24); m=(F>=lo)&(F<hi)
        if m.any(): o[k]=10*np.log10(np.mean(X[m]**2)+1e-30)
    return fc,o
fcb,mb=smooth(xb); fco,mo=smooth(xo)
ab=np.nanmean(mb[(fcb>=500)&(fcb<=2000)]); ao=np.nanmean(mo[(fco>=500)&(fco<=2000)])
mb-=ab; mo-=ao
# RT60 per band (FFT bandpass + Schroeder T20)
def rt60(x):
    n=len(x);X=np.fft.rfft(x);f=np.fft.rfftfreq(n,1/sr);cs=[63,125,250,500,1000,2000,4000,8000];out=[]
    for c in cs:
        lo,hi=c/2**(1/6),c*2**(1/6);H=((f>=lo)&(f<hi)).astype(float)
        e=np.fft.irfft(X*H,n)**2;edc=np.cumsum(e[::-1])[::-1];edc=10*np.log10(edc/edc[0]+1e-30)
        i5=np.argmax(edc<=-5);i25=np.argmax(edc<=-25);out.append(3*(i25-i5)/sr if i25>i5>0 else np.nan)
    return cs,out
def ned(x,win=int(0.02*48000)):
    t=[];o=[]
    for c in range(0,int(0.3*sr),int(0.003*sr)):
        a=max(0,c-win//2);b=min(len(x),c+win//2);seg=x[a:b]
        if len(seg)<8:continue
        sd=np.sqrt(np.mean(seg**2));o.append((np.mean(np.abs(seg)>sd)/0.3173) if sd>0 else 0);t.append(c/sr*1000)
    return np.array(t),np.array(o)
def flat(x):
    seg=x[int(0.5*sr):int(2*sr)];X=np.abs(np.fft.rfft(seg*np.hanning(len(seg))))**2
    f=np.fft.rfftfreq(len(seg),1/sr);m=(f>=200)&(f<=6000);X=X[m]+1e-12
    return np.exp(np.mean(np.log(X)))/np.mean(X)
def stft(x,nfft=1024,hop=128):
    x=trim(x,sr);w=np.hanning(nfft);Fr=[]
    for s in range(0,min(len(x),int(0.35*sr))-nfft,hop):Fr.append(np.abs(np.fft.rfft(x[s:s+nfft]*w)))
    S=np.array(Fr).T;S/=S.max()+1e-12;return 20*np.log10(S+1e-6),hop/sr
plt.style.use('dark_background');fig=plt.figure(figsize=(15,12));fig.patch.set_facecolor('#243044')
gs=fig.add_gridspec(3,2,hspace=0.33,wspace=0.2)
ax=fig.add_subplot(gs[0,:]);ax.semilogx(fcb,mb,color='#ff5a7a',lw=2,label='Real studio spring IR');ax.semilogx(fco,mo,color='#46e0c0',lw=2,label='Bare engine (no filters)')
ax.set_xlim(20,20000);ax.set_ylim(-35,8);ax.grid(True,which='both',color='#3d4f6b',lw=0.4);ax.legend(fontsize=11,framealpha=.2)
ax.set_title('Magnitude spectrum (1/12-oct, aligned 0.5-2k)',color='w');ax.set_xlabel('Hz');ax.set_ylabel('dB')
ax.set_xticks([20,50,100,200,500,1000,2000,5000,10000,20000]);ax.set_xticklabels(['20','50','100','200','500','1k','2k','5k','10k','20k'])
ax2=fig.add_subplot(gs[1,0]);cb,rb=rt60(xb);co,ro=rt60(xo)
ax2.semilogx(cb,rb,'o-',color='#ff5a7a',lw=2,label='studio spring');ax2.semilogx(co,ro,'o-',color='#46e0c0',lw=2,label='bare')
ax2.set_xlim(50,10000);ax2.set_ylim(0,7);ax2.grid(True,which='both',color='#3d4f6b',lw=.4);ax2.legend(fontsize=9,framealpha=.2)
ax2.set_title('RT60 vs frequency',color='w');ax2.set_xticks([63,125,250,500,1000,2000,4000,8000]);ax2.set_xticklabels(['63','125','250','500','1k','2k','4k','8k'])
ax3=fig.add_subplot(gs[1,1]);tb,nb=ned(xb);to,no=ned(xo)
ax3.plot(tb,nb,color='#ff5a7a',lw=2,label='studio spring');ax3.plot(to,no,color='#46e0c0',lw=2,label='bare');ax3.axhline(0.9,color='#aaa',ls='--',lw=.7)
ax3.set_xlim(0,120);ax3.set_ylim(0,1.2);ax3.grid(True,color='#3d4f6b',lw=.4);ax3.legend(fontsize=9,framealpha=.2)
ax3.set_title('Echo-density buildup (NED) — openness',color='w');ax3.set_xlabel('ms')
Sb,db=stft(xb);So,do=stft(xo)
for col,(S,dt,nm) in enumerate([(Sb,db,'studio spring'),(So,do,'bare engine')]):
    a=fig.add_subplot(gs[2,col]);a.imshow(S,origin='lower',aspect='auto',extent=[0,S.shape[1]*dt*1000,0,sr/2/1000],cmap='magma',vmin=-65,vmax=0)
    a.set_ylim(0,10);a.set_title(f'{nm} spectrogram',color='w');a.set_xlabel('ms');a.set_ylabel('kHz')
fig.suptitle('Bare dispersive-FDN engine  vs  Real studio spring IR  (impulse, decay 3.0s)',color='w',fontsize=15,y=0.95)
fig.savefig('bare_vs_bx20.png',dpi=105,facecolor=fig.get_facecolor(),bbox_inches='tight')
print(f"centroid: studio spring ~2224 | bare differs by spectrum above")
print(f"tail flatness: studio spring={flat(xb):.3f}  bare={flat(xo):.3f}")
print("NED 0.9 reached: studio spring ~{:.0f}ms  bare ~{:.0f}ms".format(tb[np.argmax(nb>=0.9)] if (nb>=0.9).any() else -1, to[np.argmax(no>=0.9)] if (no>=0.9).any() else -1))
print("saved bare_vs_bx20.png")
