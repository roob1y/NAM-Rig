# room_battery.py - honest full battery for the Room rebuild (2026-06-30).
# No metric omitted: EDR (union-mask, non-floor white%), C80, Ts, EDT, centroid (capped+full),
# running-centroid trajectory, onset cen1, echo-density buildup, low-mid bloom, onset crest,
# L/R correlation (early+full) and IACC. Octave EDT reported WITH reliability flags.
import numpy as np
from numpy.fft import rfft, irfft, rfftfreq
SR = 48000

def load_ir(path, dur=None):
    """Return (L, R, mono) trimmed from onset. dur in seconds (None=full)."""
    x = np.fromfile(path, dtype='<f4').reshape(-1, 2)
    m = (x[:,0] + x[:,1]) / 2
    pk = np.max(np.abs(m)) + 1e-20
    o = int(np.argmax(np.abs(m) > 0.01 * pk))
    if dur is not None:
        sl = slice(o, o + int(dur*SR))
    else:
        sl = slice(o, None)
    return x[sl,0].copy(), x[sl,1].copy(), m[sl].copy()

def edr(m, nf=1024, hop=512):
    win = np.hanning(nf)
    F = [np.abs(rfft(m[i:i+nf]*win))**2 for i in range(0, len(m)-nf, hop)]
    P = np.array(F)
    E = np.cumsum(P[::-1], 0)[::-1]
    E = 10*np.log10(E/(E.max()+1e-20) + 1e-12)
    return rfftfreq(nf, 1/SR), E.T   # (freq, time)

def edr_white(Eref, Eours, freqs, fcap=12000, floor=-45, tol=3.0):
    """Honest white%: union mask (ref OR ours audible) AND non-floor, sub-fcap. Returns (white%, mean|diff|)."""
    sub = freqs < fcap
    T = min(Eref.shape[1], Eours.shape[1])
    Er, Eq = Eref[sub,:T], Eours[sub,:T]
    D = Eq - Er
    U = (Er > floor) | (Eq > floor)
    if U.sum() == 0: return 0.0, 0.0
    return 100*np.mean(np.abs(D[U]) < tol), float(np.mean(np.abs(D[U])))

def battery(m):
    n = len(m); e = m**2; Et = e.sum()+1e-20
    ff = rfftfreq(n, 1/SR); X = rfft(m)
    # C80 (clarity), Ts (center time)
    t80 = int(.08*SR)
    C80 = 10*np.log10(e[:t80].sum()/(e[t80:].sum()+1e-20))
    Ts  = (np.arange(n)*e).sum()/Et/SR*1000
    # EDT (0..-10 extrapolated x6) on broadband Schroeder
    sch = 10*np.log10(np.cumsum(e[::-1])[::-1]/Et + 1e-12)
    def decay_time(lo, hi):
        ia = np.argmax(sch <= lo); ib = np.argmax(sch <= hi)
        if ib <= ia: return np.nan
        return (ib-ia)/SR * (60.0/abs(hi-lo))
    EDT = decay_time(0, -10)
    # spectral centroid: capped <12k (honest perceptual) + full (airy, inflated)
    mg = np.abs(X)
    cap = ff < 12000
    cen_cap = (ff[cap]*mg[cap]).sum()/(mg[cap].sum()+1e-20)
    cen_full = (ff*mg).sum()/(mg.sum()+1e-20)
    # onset centroid (first 25ms)
    w = int(.025*SR); g = np.abs(rfft(m[:w]*np.hanning(w))); fw = rfftfreq(w,1/SR)
    cen1 = (fw*g).sum()/(g.sum()+1e-20)
    # running centroid trajectory @ 1/4/10/25/50/100/200ms windows (32ms hann)
    traj = {}
    wn = int(.032*SR); hwin = np.hanning(wn)
    for ms in (1,10,25,50,100,200):
        i = int(ms*0.001*SR)
        seg = m[i:i+wn]
        if len(seg) < wn: traj[ms]=np.nan; continue
        gg = np.abs(rfft(seg*hwin)); ftr = rfftfreq(wn,1/SR)
        c = ftr<12000
        traj[ms] = (ftr[c]*gg[c]).sum()/(gg[c].sum()+1e-20)
    # echo density buildup -> time to reach 0.9 (Abel-Huang style)
    wq = int(.02*SR); echo = np.nan
    for i in range(0, int(.18*SR), int(.002*SR)):
        s = m[i:i+wq]; sd = np.std(s)
        if sd < 1e-9: continue
        frac = np.mean(np.abs(s) > sd)/0.3173
        if frac >= 0.9: echo = i/SR*1000; break
    # low-mid bloom (200-500Hz envelope peak time)
    mm = ((ff>=200)&(ff<500)).astype(float)
    be = np.abs(irfft(X*mm, n)); k = int(.005*SR)
    be = np.convolve(be, np.ones(k)/k, 'same')
    bloom = np.argmax(be[:int(.3*SR)])/SR*1000
    # onset crest (10ms): peak vs rms
    o10 = m[:int(.01*SR)]
    crest = 20*np.log10(np.max(np.abs(o10))/(np.sqrt(np.mean(o10**2))+1e-12))
    return dict(C80=C80, Ts=Ts, EDT=EDT, cen_cap=cen_cap, cen_full=cen_full,
                cen1=cen1, echo=echo, bloom=bloom, crest=crest, traj=traj)

def stereo_metrics(L, R):
    n = min(len(L), len(R))
    L, R = L[:n], R[:n]
    def corr(a, b):
        a = a - a.mean(); b = b - b.mean()
        d = (np.std(a)*np.std(b)*len(a))
        return float((a*b).sum()/(d+1e-20))
    e80 = int(.08*SR)
    corr_early = corr(L[:e80], R[:e80])
    corr_full = corr(L, R)
    # IACC: max normalized cross-corr over +/-1ms lag
    lag = int(.001*SR)
    a = L - L.mean(); b = R - R.mean()
    na = np.sqrt((a*a).sum()); nb = np.sqrt((b*b).sum())
    best = 0.0
    for k in range(-lag, lag+1):
        if k>=0: v=(a[k:]*b[:len(b)-k]).sum()
        else: v=(a[:len(a)+k]*b[-k:]).sum()
        best = max(best, abs(v))
    iacc = best/(na*nb+1e-20)
    return dict(corr_early=corr_early, corr_full=corr_full, iacc=iacc)

def octave_edt(m, reliable_floor=-35):
    """Octave-band EDT. Flags bands where the IR can't show enough decay (unreliable)."""
    X = rfft(m); ff = rfftfreq(len(m), 1/SR); out = {}
    for fc in (250,500,1000,2000,4000,8000):
        lo,hi = fc/np.sqrt(2), fc*np.sqrt(2)
        band = ((ff>=lo)&(ff<hi)).astype(float)
        bm = irfft(X*band, len(m)); e = bm**2
        sch = 10*np.log10(np.cumsum(e[::-1])[::-1]/(e.sum()+1e-20)+1e-12)
        ia = np.argmax(sch<=0); ib = np.argmax(sch<=-10)
        reliable = sch.min() < reliable_floor and ib>ia
        edt = (ib-ia)/SR*6.0 if ib>ia else np.nan
        out[fc] = (edt, reliable)
    return out

# ---- added 2026-06-30: mid/side + per-band spatial battery (was missing) ----
def _bandpass(x, lo, hi):
    from numpy.fft import rfft, irfft, rfftfreq
    f = rfftfreq(len(x), 1/SR); return irfft(rfft(x)*((f>=lo)&(f<hi)), len(x))
def spatial_full(L, R):
    n=min(len(L),len(R)); L,R=L[:n],R[:n]
    M=(L+R)/2; S=(L-R)/2
    eM=np.mean(M**2)+1e-20; eS=np.mean(S**2)+1e-20
    out=dict(side_mid_dB=10*np.log10(eS/eM), side_frac=eS/(eM+eS))
    # per-octave side/mid + IACC
    def iacc(a,b):
        a=a-a.mean(); b=b-b.mean(); na=np.sqrt((a*a).sum()); nb=np.sqrt((b*b).sum())
        lag=int(.001*SR); best=0
        for k in range(-lag,lag+1):
            v=(a[k:]*b[:len(b)-k]).sum() if k>=0 else (a[:len(a)+k]*b[-k:]).sum()
            best=max(best,abs(v))
        return best/(na*nb+1e-20)
    for fc in (250,500,1000,2000,4000,8000):
        lo,hi=fc/np.sqrt(2),fc*np.sqrt(2)
        Lb,Rb=_bandpass(L,lo,hi),_bandpass(R,lo,hi)
        Mb=(Lb+Rb)/2; Sb=(Lb-Rb)/2
        out['sm_%d'%fc]=10*np.log10((np.mean(Sb**2)+1e-20)/(np.mean(Mb**2)+1e-20))
        out['iacc_%d'%fc]=iacc(Lb,Rb)
    out['iacc_bb']=iacc(L,R)
    return out

# ---- masking-weighted NMR null test (audibility of ours vs ref in program context) ----
def null_nmr(ref_wet, our_wet, spl=85.0):
    """ref_wet, our_wet: (N,2) loudness-matched wet convolutions of the SAME dry. Returns dict."""
    from numpy.fft import rfft, rfftfreq
    x=(ref_wet[:,0]+ref_wet[:,1])/2; y=(our_wet[:,0]+our_wet[:,1])/2
    n=min(len(x),len(y)); x,y=x[:n],y[:n]
    frame=int(.020*SR); hop=int(.010*SR); win=np.hanning(frame); f=rfftfreq(frame,1/SR)
    cs=[]; fc=50
    while fc<16000: cs.append(fc); fc*=2**(1/3)
    cs=np.array(cs); edges=np.sqrt(cs[:-1]*cs[1:]); edges=np.concatenate([[40],edges,[16000]])
    masks=[(f>=edges[i])&(f<edges[i+1]) for i in range(len(cs))]
    def bp(s):
        out=[]
        for i in range(0,len(s)-frame,hop):
            X=np.abs(rfft(s[i:i+frame]*win))**2; out.append([X[m].sum() for m in masks])
        return np.array(out)
    Pr=bp(x); err=bp(y-x)
    spl_fs=spl
    Lr=10*np.log10(Pr+1e-20)+spl_fs
    # simple spreading + ATH masking threshold (conservative)
    zc=13*np.arctan(0.00076*cs)+3.5*np.arctan((cs/7500.0)**2)
    SM=np.zeros((len(cs),len(cs)))
    for i in range(len(cs)):
        for j in range(len(cs)):
            dz=zc[i]-zc[j]
            SM[i,j]=(15.81+7.5*(dz+0.474)-17.5*np.sqrt(1+(dz+0.474)**2))  # Schroeder spreading dB
    fk=cs/1000.0; ath=3.64*fk**-0.8-6.5*np.exp(-0.6*(fk-3.3)**2)+1e-3*fk**4
    thr=np.full_like(Lr,-200.0)
    for t in range(Lr.shape[0]):
        for i in range(len(cs)):
            contrib=Lr[t]+SM[i]-14.0  # noise-masker offset
            thr[t,i]=max(np.max(contrib), ath[i])
    Eerr=10*np.log10(err+1e-20)+spl_fs
    nmr=Eerr-thr  # dB above masked threshold; >0 = audible
    return dict(nmr_global=float(np.mean(nmr[nmr>-200])),
                frac_audible=float(np.mean(nmr>0)),
                nmr_p90=float(np.percentile(nmr,90)))
