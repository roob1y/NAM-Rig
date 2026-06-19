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
def trim(x,sr): pk=np.max(np.abs(x)); i=np.argmax(np.abs(x)>0.02*pk); return x[i:]
def bandenv(x,sr,fc):  # energy envelope of an octave band over time (dB, normalized to own peak)
    n=len(x);X=np.fft.rfft(x);f=np.fft.rfftfreq(n,1/sr)
    lo,hi=fc/2**0.5,fc*2**0.5;H=((f>=lo)&(f<hi)).astype(float)
    yb=np.fft.irfft(X*H,n)
    e=yb**2; k=int(0.012*sr); env=np.convolve(e,np.ones(k)/k,'same')
    env=10*np.log10(env/np.max(env)+1e-9); return env
U="/sessions/admiring-inspiring-cerf/mnt/uploads/NEVO - studio spring Stereo 3.0s.wav"
xb,sr=rd(U); xl,_=rd('loved_imp.wav'); xb=trim(xb,sr); xl=trim(xl,sr)
bands=[125,250,500,1000,2000]
plt.style.use('dark_background');fig,ax=plt.subplots(2,1,figsize=(13,9));fig.patch.set_facecolor('#243044')
cols=['#ff5a7a','#ffcf4d','#46e0c0','#7a8cff','#c98cff']
print("time-to-peak per band (ms)  [bloom = peak well after 0]:")
print(f"{'band':>6} {'studio spring':>8} {'loved':>8}")
for bi,fc in enumerate(bands):
    eb=bandenv(xb,sr,fc); el=bandenv(xl,sr,fc)
    t=np.arange(len(eb))/sr*1000; tl=np.arange(len(el))/sr*1000
    ax[0].plot(t,eb,color=cols[bi],lw=1.8,label=f'{fc}Hz')
    ax[1].plot(tl,el,color=cols[bi],lw=1.8,label=f'{fc}Hz')
    # time to peak within first 400ms
    w=int(0.4*sr); pkb=eb[:w].argmax()/sr*1000; pkl=el[:w].argmax()/sr*1000
    print(f"{fc:>6} {pkb:>8.0f} {pkl:>8.0f}")
for a,nm in zip(ax,['REAL studio spring','LOVED version (folder 24)']):
    a.set_xlim(0,600);a.set_ylim(-25,1);a.grid(True,color='#3d4f6b',lw=.4);a.legend(fontsize=9,framealpha=.2,ncol=5)
    a.set_title(nm+' — per-band energy envelope (each normalized to own peak)',color='w');a.set_ylabel('dB')
ax[1].set_xlabel('Time (ms)')
fig.suptitle('Bloom analysis: how each band swells/decays — studio spring vs the loved version',color='w',fontsize=14,y=0.96)
fig.savefig('bloom.png',dpi=110,facecolor=fig.get_facecolor(),bbox_inches='tight');print("saved bloom.png")
# early vs late spectral tilt (does low-mid grow over time = bloom?)
def tilt(x,a,b):
    seg=x[int(a*sr):int(b*sr)] if int(b*sr)<=len(x) else x[int(a*sr):]
    X=np.abs(np.fft.rfft(seg*np.hanning(len(seg))))**2;f=np.fft.rfftfreq(len(seg),1/sr)
    def bn(fc):lo,hi=fc/2**0.5,fc*2**0.5;m=(f>=lo)&(f<hi);return 10*np.log10(np.sum(X[m])+1e-30)
    ref=bn(2000);return {fc:bn(fc)-ref for fc in [250,500,1000]}
print("\nlow-mid level relative to 2k, early(0-60ms) -> late(400-900ms):")
for nm,x in [('studio spring',xb),('loved',xl)]:
    e=tilt(x,0,0.06);l=tilt(x,0.4,0.9)
    print(f" {nm}:  250Hz {e[250]:+.1f}->{l[250]:+.1f}   500Hz {e[500]:+.1f}->{l[500]:+.1f}   1k {e[1000]:+.1f}->{l[1000]:+.1f}")
