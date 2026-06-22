# add_cloud.py — additive bright HF early cloud. Synthesize ONLY the per-ERB onset deficit
# (ref - shipped, where positive) as L/R-decorrelated band-shaped noise with the reference's
# per-band time envelope, ADD it to the shipped dense onset. Brightness comes from the added
# 3k+10-13k energy arriving from t=0; density stays high (we don't touch the existing onset);
# no smearing (added clean layer, not phase-randomised reconstruction). EULA-safe (deficit
# statistics only). Returns blended onset; measures centroid arc / crest / TF-err.
import sys,os; sys.path.insert(0,'.')
import numpy as np, reverb_battery as rb, onset_fit as of
SR=rb.sr; FC=of.FC
def mono(p,L):a=L(p);return (a[0]+a[1])/2
def alLR(LR):L,R=LR;m=(L+R)/2;o=rb.onset(m);return L[o:],R[o:]
def bp(x,f0,nf):
    X=np.fft.rfft(x,nf);f=np.fft.rfftfreq(nf,1/SR);bw=max(60,0.18*f0);g=np.exp(-0.5*((f-f0)/(bw/2))**2)
    return np.fft.irfft(X*g,nf)[:len(x)]
def band_env(ch,N,nf,fr):  # per-band RMS envelope over 5ms frames, length-N region
    nfr=N//fr; M=np.zeros((len(FC),nfr))
    for bi,f0 in enumerate(FC):
        b=bp(ch[:N],f0,nf)
        for j in range(nfr): M[bi,j]=np.sqrt(np.mean(b[j*fr:(j+1)*fr]**2)+1e-12)
    return M
def build_cloud(refL,refR,shpL,shpR, N, gain=1.0, only_hi=False, seed=7, floor_hz=0, sustain=1.0, air_w=1.0, air_hz=6000):
    nf=1<<15; fr=int(0.005*SR); nfr=N//fr
    # level-match shipped to ref over first N (broadband)
    g=np.sqrt(np.mean((refL[:N]**2+refR[:N]**2))/np.mean((shpL[:N]**2+shpR[:N]**2))); shpL=shpL*g; shpR=shpR*g
    rng=np.random.default_rng(seed)
    def one(refch,shpch,sd):
        Mr=band_env(refch,N,nf,fr); Ms=band_env(shpch,N,nf,fr)
        add=np.sqrt(np.maximum(Mr**2-Ms**2,0.0))*gain   # energy to add per band/frame
        rr=np.random.default_rng(sd); out=np.zeros(N)
        for bi,f0 in enumerate(FC):
            if only_hi and f0<2500: continue
            if f0<floor_hz: continue
            noise=rr.standard_normal(N); nb=bp(noise,f0,nf)
            # apply the per-frame target RMS envelope (upsample frames -> samples, smooth)
            aenv=add[bi].copy()
            if sustain!=1.0:
                xs=np.arange(len(aenv)); aenv=aenv*np.exp(-(xs/(len(aenv)*0.5*sustain)))*np.exp(xs*0+0)+aenv*0  # placeholder
            envs=np.repeat(add[bi],fr)[:N]
            if sustain!=1.0:
                tt=np.arange(N)/SR; envs=envs*(sustain if False else 1.0)  # sustain handled below
            envs=np.convolve(envs,np.ones(int(0.004*SR))/int(0.004*SR),'same')
            wair=air_w if f0>=air_hz else 1.0; envs=envs*wair
            cur=np.sqrt(np.convolve(nb**2,np.ones(fr)/fr,'same')+1e-12)
            out += nb*(envs/cur)
        return out
    cL=one(refL,shpL,seed+1); cR=one(refR,shpR,seed+2)  # independent seeds -> decorrelated L/R
    fullL=shpL.copy(); fullR=shpR.copy(); fullL[:N]+=cL; fullR[:N]+=cR
    return fullL, fullR, shpL, shpR

def metrics(m, ref):
    def cen_t0(x,w=1024):
        fr=np.abs(np.fft.rfft(x[:w]*np.hanning(w)));ff=np.fft.rfftfreq(w,1/SR);s=ff>40
        return np.sum(ff[s]*fr[s])/(np.sum(fr[s])+1e-20)
    def arc(x,ms):
        win=512;i=int(ms/1000*SR);seg=x[i:i+win]*np.hanning(win)
        fr=np.abs(np.fft.rfft(seg));f=np.fft.rfftfreq(win,1/SR);s=f>40
        return np.sum(f[s]*fr[s])/(np.sum(fr[s])+1e-20)
    crest=np.max(np.abs(m[:int(0.02*SR)]))/(np.sqrt(np.mean(m[:int(0.02*SR)]**2))+1e-20)
    tferr=float(np.mean(np.abs(of.norm(of.tf_map(m))-of.REFM)))
    return dict(cen_t0=cen_t0(m),c5=arc(m,5),c50=arc(m,50),c150=arc(m,150),crest=crest,tferr=tferr)

if __name__=="__main__":
    N=int(0.15*SR)
    rL,rR=alLR(rb.load_ref('ir/vintage-plate-1.5s.wav'))
    sL,sR=alLR(rb.load_ours('/tmp/plate_anchor.f32'))
    ref_m=(rL+rR)/2
    print(f"{'config':<22}{'cen_t0':>8}{'c@5':>7}{'c@50':>7}{'c@150':>7}{'crest':>7}{'TFerr':>7}")
    refmet=metrics(ref_m,ref_m); print(f"{'REFERENCE (target)':<22}{refmet['cen_t0']:>8.0f}{refmet['c5']:>7.0f}{refmet['c50']:>7.0f}{refmet['c150']:>7.0f}{refmet['crest']:>7.1f}{0.0:>7.2f}")
    bL,bR,s0L,s0R=build_cloud(rL,rR,sL,sR,N,gain=0.0)
    m0=metrics((s0L+s0R)/2,ref_m); print(f"{'shipped (gain 0)':<22}{m0['cen_t0']:>8.0f}{m0['c5']:>7.0f}{m0['c50']:>7.0f}{m0['c150']:>7.0f}{m0['crest']:>7.1f}{m0['tferr']:>7.2f}")
    for gn in (0.6,1.0,1.3):
        bL,bR,_,_=build_cloud(rL,rR,sL,sR,N,gain=gn)
        mm=metrics((bL+bR)/2,ref_m); print(f"{'+cloud all bands g'+str(gn):<22}{mm['cen_t0']:>8.0f}{mm['c5']:>7.0f}{mm['c50']:>7.0f}{mm['c150']:>7.0f}{mm['crest']:>7.1f}{mm['tferr']:>7.2f}")
    for gn in (1.0,1.3):
        bL,bR,_,_=build_cloud(rL,rR,sL,sR,N,gain=gn,only_hi=True)
        mm=metrics((bL+bR)/2,ref_m); print(f"{'+cloud >2.5k g'+str(gn):<22}{mm['cen_t0']:>8.0f}{mm['c5']:>7.0f}{mm['c50']:>7.0f}{mm['c150']:>7.0f}{mm['crest']:>7.1f}{mm['tferr']:>7.2f}")
