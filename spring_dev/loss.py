import numpy as np
SR=48000
BANDS=[250,500,1000,2000,4000]
TGT_TTP={250:75.,500:77.,1000:81.,2000:28.,4000:38.}
TGT_RT60={250:4.4,500:3.7,1000:3.1,2000:2.5,4000:2.4}   # from fixed-meter studio spring read
TGT_CENTROID=2224.; TGT_CORR=0.0
def _octbands(x,sr):
    X=np.fft.rfft(x);f=np.fft.rfftfreq(len(x),1/sr);out={}
    for fc in BANDS:
        lo,hi=fc/2**.5,fc*2**.5;m=((f>=lo)&(f<hi)).astype(float);out[fc]=np.fft.irfft(X*m,len(x))
    return out
def global_onset(x,sr):
    k=int(0.003*sr);e=np.convolve(x**2,np.ones(k)/k,'same');return int(np.argmax(e>0.05*e.max()))
def _ttp(yb,sr,on,sm=35):
    e=yb**2;k=int(sm*1e-3*sr);env=np.convolve(e,np.ones(k)/k,'same')
    w=env[on:on+int(0.4*sr)];return (w.argmax())/sr*1000 if len(w) else 0.0
def _rt60(yb,sr):
    e=yb**2;k=int(0.02*sr);env=np.convolve(e,np.ones(k)/k,'same');pk=int(env.argmax())
    s=np.cumsum(e[pk:][::-1])[::-1];s/=s[0]+1e-20;db=10*np.log10(s+1e-20)
    a=np.argmax(db<-5);b=np.argmax(db<-15)
    if b<=a+10:return np.nan
    tt=np.arange(a,b)/sr;p=np.polyfit(tt,db[a:b],1);return -60/p[0] if p[0]<0 else np.nan
def centroid(x,sr):
    X=np.abs(np.fft.rfft(x));f=np.fft.rfftfreq(len(x),1/sr);return float(np.sum(f*X)/(np.sum(X)+1e-12))
def fingerprint(L,R,sr=SR):
    m=0.5*(L+R);on=global_onset(m,sr);bands=_octbands(m,sr)
    return dict(ttp={fc:_ttp(bands[fc],sr,on) for fc in BANDS},
        rt60={fc:_rt60(bands[fc],sr) for fc in BANDS},
        centroid=centroid(m,sr),
        corr=float(np.corrcoef(L,R)[0,1]) if np.std(R)>0 else 1.0)
# standalone band fns for diagnostics
def ttp_ms(x,sr,fc,on=None):
    on=global_onset(x,sr) if on is None else on
    X=np.fft.rfft(x);f=np.fft.rfftfreq(len(x),1/sr);lo,hi=fc/2**.5,fc*2**.5
    yb=np.fft.irfft(X*((f>=lo)&(f<hi)),len(x));return _ttp(yb,sr,on)
def rt60_s(x,sr,fc):
    X=np.fft.rfft(x);f=np.fft.rfftfreq(len(x),1/sr);lo,hi=fc/2**.5,fc*2**.5
    yb=np.fft.irfft(X*((f>=lo)&(f<hi)),len(x));return _rt60(yb,sr)
def loss(L,R,sr=SR,detail=False):
    fp=fingerprint(L,R,sr);lb=0.0
    for fc in BANDS:
        d=(fp['ttp'][fc]-TGT_TTP[fc])/30.0;w=2.0 if fc<=1000 else 1.0;lb+=w*d*d
    split=np.mean([fp['ttp'][f] for f in(250,500,1000)])-np.mean([fp['ttp'][f] for f in(2000,4000)])
    tgt_split=np.mean([TGT_TTP[f] for f in(250,500,1000)])-np.mean([TGT_TTP[f] for f in(2000,4000)])
    lsplit=((split-tgt_split)/25.0)**2
    lr=0.0;rts=[]
    for fc in BANDS:
        r=fp['rt60'][fc]
        if not np.isfinite(r): lr+=4.0; rts.append(np.nan); continue
        rts.append(r);lr+=((r-TGT_RT60[fc])/1.5)**2
    # reward monotonic lows-longest tilt
    mono=0.0
    for i in range(len(rts)-1):
        if np.isfinite(rts[i]) and np.isfinite(rts[i+1]) and rts[i+1]>rts[i]+0.3: mono+=((rts[i+1]-rts[i])/1.0)**2
    lc=((fp['centroid']-TGT_CENTROID)/700.)**2
    lw=((fp['corr']-TGT_CORR)/0.25)**2
    total=6.0*lb+5.0*lsplit+1.2*lr+1.0*mono+0.5*lc+1.0*lw
    if detail: return total,fp,dict(bloom=lb,split=lsplit,rt60=lr,mono=mono,centroid=lc,corr=lw,split_ms=split,tgt_split_ms=tgt_split)
    return total
