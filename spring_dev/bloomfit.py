import numpy as np, struct, subprocess, os, sys
def rd(p):
    d=open(p,'rb').read(); i=12;af=1;ch=2;bps=16;off=0;ln=0;sr=48000
    while i+8<=len(d):
        c=d[i:i+4]; sz=struct.unpack('<I',d[i+4:i+8])[0]
        if c==b'fmt ': af,ch,sr,_,_,bps=struct.unpack('<HHIIHH',d[i+8:i+8+16])
        elif c==b'data': off=i+8;ln=sz
        i+=8+sz+(sz&1)
    a=np.frombuffer(d[off:off+ln],dtype='<f4') if af==3 else np.frombuffer(d[off:off+ln],dtype='<i2').astype(np.float32)/32768
    a=a.reshape(-1,ch); return 0.5*(a[:,0]+(a[:,1] if ch>1 else a[:,0])),sr
def trim(x,sr):pk=np.max(np.abs(x));return x[np.argmax(np.abs(x)>0.02*pk):]
def fingerprint(x,sr):
    n=len(x);X=np.fft.rfft(x);f=np.fft.rfftfreq(n,1/sr)
    ttp={};growth={}
    for fc in [250,500,1000,2000]:
        lo,hi=fc/2**.5,fc*2**.5;H=((f>=lo)&(f<hi)).astype(float)
        yb=np.fft.irfft(X*H,n);e=yb**2;k=int(0.012*sr);env=np.convolve(e,np.ones(k)/k,'same')
        w=int(0.4*sr); ttp[fc]=env[:w].argmax()/sr*1000
    # low-mid growth relative to 2k: late(.4-.9s) - early(0-.06s)
    def band(seg,fc):
        Y=np.abs(np.fft.rfft(seg*np.hanning(len(seg))))**2;ff=np.fft.rfftfreq(len(seg),1/sr)
        lo,hi=fc/2**.5,fc*2**.5;m=(ff>=lo)&(ff<hi);return 10*np.log10(np.sum(Y[m])+1e-30)
    eearly=x[:int(.06*sr)];elate=x[int(.4*sr):int(.9*sr)]
    for fc in [250,500,1000]:
        growth[fc]=(band(elate,fc)-band(elate,2000))-(band(eearly,fc)-band(eearly,2000))
    return ttp,growth
# studio spring reference fingerprint
U="/sessions/admiring-inspiring-cerf/mnt/uploads/NEVO - studio spring Stereo 3.0s.wav"
xb,sr=rd(U);xb=trim(xb,sr);tb,gb=fingerprint(xb,sr)
print("studio spring target:  ttp",{k:round(v) for k,v in tb.items()}," growth",{k:round(v,1) for k,v in gb.items()})
def run(**kw):
    base=dict(IMPLEN='1.5',DISP='120',APA='0.62',K='2',EARLY='4',EARLYG='0.72',SIZE='1.3',FDAMP='7000',DIFF='0.8',PRE='6',DARK='9000',HICUT='5800',MOD='0.4',EARLYTAP='0',SWELL='0',EARLYHP='0')
    base.update({k:str(v) for k,v in kw.items()})
    env=dict(os.environ,**base)
    subprocess.run(['./fdn_proto','4.5','/tmp/spring/bf.wav'],env=env,stdout=subprocess.DEVNULL)
    x,_=rd('/tmp/spring/bf.wav');x=trim(x,sr);return fingerprint(x,sr)
print("\nSIZE x EARLYTAP -> 500Hz & 1k time-to-peak (target ~83-88ms):")
for size in [0.8,1.0,1.3]:
    for tap in [0,0.3,0.6]:
        t,g=run(SIZE=size,EARLYTAP=tap)
        print(f"  size={size} tap={tap}: ttp500={t[500]:.0f} ttp1k={t[1000]:.0f}  growth500={g[500]:+.1f}")

if __name__=='__main__' and len(sys.argv)>1 and sys.argv[1]=='fine':
    print("\nfine: size=0.8, tiny taps, FDAMP for growth (target ttp~85, growth500~+5.7):")
    for fdamp in [4000,5500]:
        for tap in [0.05,0.10,0.16]:
            t,g=run(SIZE=0.8,EARLYTAP=tap,EARLYHP=900,FDAMP=fdamp)
            print(f"  fdamp={fdamp} tap={tap}: ttp500={t[500]:.0f} ttp1k={t[1000]:.0f}  growth250/500/1k={g[250]:+.1f}/{g[500]:+.1f}/{g[1000]:+.1f}")

if __name__=='__main__' and len(sys.argv)>1 and sys.argv[1]=='sz2':
    print("\npush size down (HP tap 0.1, fdamp 6500); target ttp~85, growth 3.6/5.7/4.0:")
    for size in [0.55,0.65,0.75]:
        t,g=run(SIZE=size,EARLYTAP=0.1,EARLYHP=900,FDAMP=6500)
        print(f"  size={size}: ttp250/500/1k/2k={t[250]:.0f}/{t[500]:.0f}/{t[1000]:.0f}/{t[2000]:.0f}  growth250/500/1k={g[250]:+.1f}/{g[500]:+.1f}/{g[1000]:+.1f}")
