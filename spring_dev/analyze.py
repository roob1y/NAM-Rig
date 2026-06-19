import numpy as np, struct
import matplotlib; matplotlib.use('Agg')
import matplotlib.pyplot as plt

def rd(path):
    d=open(path,'rb').read(); i=12;af=1;ch=2;bps=16;off=0;ln=0;sr=48000
    while i+8<=len(d):
        cid=d[i:i+4]; sz=struct.unpack('<I',d[i+4:i+8])[0]
        if cid==b'fmt ': af,ch,sr,_,_,bps=struct.unpack('<HHIIHH',d[i+8:i+8+16])
        elif cid==b'data': off=i+8;ln=sz
        i+=8+sz+(sz&1)
    by=bps//8;raw=d[off:off+ln]
    a=np.frombuffer(raw,dtype='<f4') if af==3 else (np.frombuffer(raw,dtype='<i2').astype(np.float32)/32768 if bps==16 else np.frombuffer(raw,dtype='<i4').astype(np.float32)/2147483648)
    a=a.reshape(-1,ch); L=a[:,0]; R=a[:,1] if ch>1 else a[:,0]; return L,R,sr

U="/sessions/admiring-inspiring-cerf/mnt/uploads/NEVO - studio spring Stereo 3.0s.wav"
sigs={
 'Real studio spring (reference)':     (U,            '#ff5a7a'),
 'Current shipping engine':   ('imp_old.wav', '#7a8cff'),
 'New: BEFORE HF fix':        ('imp_off.wav', '#ffcf4d'),
 'New: AFTER HF fix (v3)':    ('imp_v3.wav',  '#46e0c0'),
}

# ---------- 1/12-oct smoothed magnitude spectrum ----------
def smooth_spec(path):
    L,R,sr=rd(path); x=0.5*(L+R)
    X=np.abs(np.fft.rfft(x*np.hanning(len(x)))); f=np.fft.rfftfreq(len(x),1/sr)
    fc=np.geomspace(20,20000,240); out=np.zeros_like(fc)
    for k,c in enumerate(fc):
        lo,hi=c/2**(1/24),c*2**(1/24); m=(f>=lo)&(f<hi)
        out[k]=10*np.log10(np.mean(X[m]**2)+1e-30) if m.any() else np.nan
    return fc,out
# align each curve at 500-2000 Hz
specs={}
for nm,(p,col) in sigs.items():
    fc,mag=smooth_spec(p); anchor=np.nanmean(mag[(fc>=500)&(fc<=2000)]); specs[nm]=(fc,mag-anchor,col)

# ---------- per-1/3-oct RT60 (FFT bandpass + Schroeder T20) ----------
def band_rt60(path):
    L,R,sr=rd(path); x=0.5*(L+R); n=len(x); X=np.fft.rfft(x); f=np.fft.rfftfreq(n,1/sr)
    cs=np.array([63,125,250,500,1000,2000,4000,8000]); rt=[]
    for c in cs:
        lo,hi=c/2**(1/6),c*2**(1/6); H=((f>=lo)&(f<hi)).astype(float)
        yb=np.fft.irfft(X*H,n); e=yb**2
        edc=np.cumsum(e[::-1])[::-1]; edc=10*np.log10(edc/edc[0]+1e-30)
        i5=np.argmax(edc<=-5); i25=np.argmax(edc<=-25)
        rt.append(3.0*(i25-i5)/sr if i25>i5>0 else np.nan)
    return cs,np.array(rt)

# ---------- broadband Schroeder energy decay ----------
def edc(path):
    L,R,sr=rd(path); x=0.5*(L+R); e=x**2; c=np.cumsum(e[::-1])[::-1]
    db=10*np.log10(c/c[0]+1e-30); t=np.arange(len(db))/sr; return t,db

plt.style.use('dark_background')
fig=plt.figure(figsize=(15,11)); fig.patch.set_facecolor('#243044')
gs=fig.add_gridspec(2,2,hspace=0.28,wspace=0.22)

# Panel 1: magnitude overlay
ax=fig.add_subplot(gs[0,:]); ax.set_facecolor('#2b3a52')
for nm,(fc,mag,col) in specs.items():
    ax.semilogx(fc,mag,color=col,lw=2.0,label=nm)
ax.axvspan(6000,20000,color='#ff5a7a',alpha=0.06)
ax.set_xlim(20,20000); ax.set_ylim(-30,8)
ax.set_title('Magnitude spectrum  (1/12-oct smoothed, aligned at 0.5–2 kHz)',fontsize=13,color='w')
ax.set_xlabel('Frequency (Hz)'); ax.set_ylabel('Level (dB)')
ax.grid(True,which='both',color='#3d4f6b',lw=0.5); ax.legend(loc='lower center',fontsize=11,framealpha=0.2)
ax.set_xticks([20,50,100,200,500,1000,2000,5000,10000,20000]); ax.set_xticklabels(['20','50','100','200','500','1k','2k','5k','10k','20k'])
ax.text(9000,6,'excess HF region',color='#ff9ab0',fontsize=9)

# Panel 2: per-band RT60
ax2=fig.add_subplot(gs[1,0]); ax2.set_facecolor('#2b3a52')
for nm,(p,col) in sigs.items():
    cs,rt=band_rt60(p); ax2.semilogx(cs,rt,'o-',color=col,lw=2,ms=5,label=nm)
ax2.set_xlim(50,10000); ax2.set_ylim(0,7)
ax2.set_title('Decay time RT60 vs frequency  (lows-ring-longest tilt)',fontsize=12,color='w')
ax2.set_xlabel('Frequency (Hz)'); ax2.set_ylabel('RT60 (s)')
ax2.grid(True,which='both',color='#3d4f6b',lw=0.5); ax2.legend(fontsize=9,framealpha=0.2)
ax2.set_xticks([63,125,250,500,1000,2000,4000,8000]); ax2.set_xticklabels(['63','125','250','500','1k','2k','4k','8k'])

# Panel 3: broadband decay
ax3=fig.add_subplot(gs[1,1]); ax3.set_facecolor('#2b3a52')
for nm,(p,col) in sigs.items():
    t,db=edc(p); ax3.plot(t,db,color=col,lw=2,label=nm)
ax3.set_xlim(0,6); ax3.set_ylim(-60,2)
ax3.set_title('Broadband energy decay (Schroeder)',fontsize=12,color='w')
ax3.set_xlabel('Time (s)'); ax3.set_ylabel('Level (dB)')
ax3.grid(True,color='#3d4f6b',lw=0.5); ax3.legend(fontsize=9,framealpha=0.2)

fig.suptitle('NAM Rig Spring  vs  Real studio spring  —  spectral & decay analysis (decay 3.0 s)',fontsize=15,color='w',y=0.96)
fig.savefig('spring_analysis.png',dpi=110,facecolor=fig.get_facecolor(),bbox_inches='tight')
print("saved spring_analysis.png")

# numeric HF + RT60 summary
print("\n--- HF excess vs studio spring (dB, aligned) ---")
fcR,magR,_=specs['Real studio spring (reference)']
for nm in ['Current shipping engine','New: BEFORE HF fix','New: AFTER HF fix (v3)']:
    fc,mag,_=specs[nm]
    for hz in [6300,8000,10000]:
        idx=np.argmin(np.abs(fc-hz)); print(f"{nm:22s} {hz:>6}Hz  {mag[idx]-magR[idx]:+5.1f}")
