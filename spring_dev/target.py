import numpy as np
def octband(x,sr,fc):
    X=np.fft.rfft(x); f=np.fft.rfftfreq(len(x),1/sr)
    lo,hi=fc/2**.5,fc*2**.5; H=((f>=lo)&(f<hi)).astype(float)
    return np.fft.irfft(X*H,len(x))
def smooth(e,sr,ms): k=max(1,int(ms*1e-3*sr)); return np.convolve(e,np.ones(k)/k,'same')
BANDS=[125,250,500,1000,2000,4000]
# sample times for early envelope (bloom) capture, ms
TS=np.array([5,10,20,35,55,85,120,170,240,340,480])
def band_env_at(x,sr,fc,ts_ms):
    e=smooth(octband(x,sr,fc)**2,sr,12)
    on=np.argmax(e>0.02*e.max())
    idx=(on+ (ts_ms*1e-3*sr)).astype(int); idx=np.clip(idx,0,len(e)-1)
    v=e[idx]; v=v/(v.max()+1e-20); return 10*np.log10(v+1e-6)  # dB rel band peak
def fingerprint(L,R,sr):
    m=0.5*(L+R)
    fp={}
    # early per-band envelope (bloom) in dB rel each band's own peak
    fp['env']={fc:band_env_at(m,sr,fc,TS) for fc in BANDS}
    # RT60 per octave
    def rt60(fc):
        yb=octband(m,sr,fc); e=yb**2; s=np.cumsum(e[::-1])[::-1]; s/=s[0]+1e-20; db=10*np.log10(s+1e-20)
        a=np.argmax(db<-5); b=np.argmax(db<-25)
        if b<=a: return float('nan')
        t=np.arange(a,b)/sr; p=np.polyfit(t,db[a:b],1); return -60/p[0]
    fp['rt60']={fc:rt60(fc) for fc in BANDS}
    X=np.abs(np.fft.rfft(m)); f=np.fft.rfftfreq(len(m),1/sr)
    fp['centroid']=float(np.sum(f*X)/(np.sum(X)+1e-12))
    fp['corr']=float(np.corrcoef(L,R)[0,1])
    return fp
if __name__=='__main__':
    ir=np.load('bx20_ir.npy'); sr=48000
    fp=fingerprint(ir[:,0],ir[:,1],sr)
    print("studio spring IR early per-band envelope (dB rel band peak), times(ms):",list(TS))
    for fc in BANDS:
        print(f"  {fc:5d}: "+" ".join(f"{v:5.1f}" for v in fp['env'][fc])+f"   peak@~{TS[np.argmax(fp['env'][fc])]}ms")
    print("RT60:",{k:round(v,2) for k,v in fp['rt60'].items()})
    print("centroid",round(fp['centroid']),"corr",round(fp['corr'],3))
    import pickle; pickle.dump({'fp':fp,'TS':TS,'BANDS':BANDS},open('bx20_target.pkl','wb'))
    print("saved bx20_target.pkl")
