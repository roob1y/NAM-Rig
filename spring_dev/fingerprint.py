import numpy as np, struct
def rd(p):
    d=open(p,'rb').read(); i=12;af=1;ch=2;bps=16;off=0;ln=0;sr=48000
    while i+8<=len(d):
        c=d[i:i+4]; sz=struct.unpack('<I',d[i+4:i+8])[0]
        if c==b'fmt ': af,ch,sr,_,_,bps=struct.unpack('<HHIIHH',d[i+8:i+24])
        elif c==b'data': off=i+8;ln=sz
        i+=8+sz+(sz&1)
    a=np.frombuffer(d[off:off+ln],dtype='<f4').copy() if af==3 else np.frombuffer(d[off:off+ln],dtype='<i2').astype(np.float32)/32768
    a=a.reshape(-1,ch); return a,sr
BANDS=[125,250,500,1000,2000,4000]
def octband(x,sr,fc):
    X=np.fft.rfft(x); f=np.fft.rfftfreq(len(x),1/sr)
    lo,hi=fc/2**.5,fc*2**.5; H=((f>=lo)&(f<hi)).astype(float)
    return np.fft.irfft(X*H,len(x))
def smooth_env(e,sr,ms=10):
    k=max(1,int(ms*1e-3*sr)); return np.convolve(e,np.ones(k)/k,'same')
# ---- onset-averaged bloom from a (dry,wet) pair ----
def bloom_from_pair(drypath,wetpath,win_ms=220):
    dry,sr=rd(drypath); wet,_=rd(wetpath)
    dm=0.5*(dry[:,0]+dry[:,1]); wm=0.5*(wet[:,0]+wet[:,1])
    # detect note onsets in dry via energy flux
    k=int(0.005*sr); env=smooth_env(dm**2,sr,5)
    d=np.diff(env); thr=0.15*d.max()
    onsets=[]
    i=0; hold=int(0.18*sr)
    while i<len(d):
        if d[i]>thr:
            onsets.append(i); i+=hold
        else: i+=1
    W=int(win_ms*1e-3*sr)
    out={}
    for fc in BANDS:
        yb=octband(wm,sr,fc); e=smooth_env(yb**2,sr,8)
        acc=np.zeros(W); cnt=0
        for on in onsets:
            if on+W<len(e):
                seg=e[on:on+W]
                if seg.max()>1e-9: acc+=seg/seg.max(); cnt+=1
        env=acc/max(cnt,1)
        ttp=env.argmax()/sr*1000
        out[fc]=(ttp,env)
    return out,sr,len(onsets)
if __name__=='__main__':
    ref='../spring_voicing_demos/0_reference_real-studio spring/guitar_wet-only.wav'
    bl,sr,nons=bloom_from_pair('dry.wav',ref)
    print(f"studio spring onset-averaged bloom ({nons} onsets):")
    for fc in BANDS:
        print(f"  {fc:5d}Hz  time-to-peak = {bl[fc][0]:5.0f} ms")
    np.save('bx20_bloom_target.npy',{fc:bl[fc][1] for fc in BANDS},allow_pickle=True)
    # ---- IR late-field stats ----
    ir=np.load('bx20_ir.npy'); irm=0.5*(ir[:,0]+ir[:,1])
    X=np.abs(np.fft.rfft(irm)); f=np.fft.rfftfreq(len(irm),1/sr)
    cen=np.sum(f*X)/(np.sum(X)+1e-12)
    # spectral flatness of the late tail (geo/arith mean of power)
    P=X**2+1e-20; sf=np.exp(np.mean(np.log(P)))/np.mean(P)
    corr=np.corrcoef(ir[:,0],ir[:,1])[0,1]
    # per-octave RT60 via Schroeder on the IR
    def rt60(x,sr,fc):
        yb=octband(x,sr,fc); e=yb**2; sch=np.cumsum(e[::-1])[::-1]; sch/=sch[0]+1e-20
        db=10*np.log10(sch+1e-20)
        # fit -5..-25 dB
        a=np.argmax(db<-5); b=np.argmax(db<-25)
        if b<=a: return float('nan')
        t=np.arange(a,b)/sr; p=np.polyfit(t,db[a:b],1); return -60/p[0]
    print(f"\nrecovered studio spring IR: centroid={cen:.0f}Hz  flatness={sf:.3f}  L/Rcorr={corr:+.3f}")
    print("per-octave RT60:")
    for fc in [125,250,500,1000,2000,4000]:
        print(f"  {fc:5d}Hz  RT60={rt60(irm,sr,fc):.2f}s")
