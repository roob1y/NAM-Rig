import sys,os,subprocess;sys.path.insert(0,'.')
import numpy as np, reverb_battery as rb
SR=rb.sr
def ir_render(cmd,cwd,env):
    out=f"/tmp/ms_{abs(hash((cmd,tuple(sorted(env.items())))))%99999}.f32"
    subprocess.run([cmd,"plate",os.path.abspath("impulse.f32"),out],cwd=cwd,
        env={**os.environ,"RV_T60":"2.45",**env},check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    return rb.load_ours(out)
def mono(c):L,R=c;return (L+R)/2
def win_centroid(seg):
    X=np.abs(np.fft.rfft(seg*np.hanning(len(seg))));f=np.fft.rfftfreq(len(seg),1/SR)
    return np.sum(f*X)/(np.sum(X)+1e-20)
def measures(c):
    L,R=c; m=mono(c); on=rb.onset(m); m=m[on:]; L=L[on:]; R=R[on:]
    e=m**2; t=np.arange(len(m))/SR
    Ts=np.sum(t*e)/(np.sum(e)+1e-20)*1000  # center time ms
    k50=int(0.05*SR); D50=np.sum(e[:k50])/(np.sum(e)+1e-20)
    # brightness evolution: centroid of 0-20ms vs 150-300ms
    cen_early=win_centroid(m[:int(0.02*SR)])
    cen_late=win_centroid(m[int(0.15*SR):int(0.30*SR)])
    # high-res spectral roughness (coloration): std of unsmoothed log-mag over a 2s tail window, in 1/3-oct running
    seg=m[:int(2.0*SR)]*np.hanning(min(len(m),int(2.0*SR))) if len(m)>=int(2.0*SR) else m*np.hanning(len(m))
    X=20*np.log10(np.abs(np.fft.rfft(seg))+1e-9); f=np.fft.rfftfreq(len(seg),1/SR)
    band=(f>500)&(f<8000)
    # roughness = std of (X - local-smoothed X)
    Xs=np.convolve(X,np.ones(101)/101,mode='same')
    rough=np.std((X-Xs)[band])
    # spectral flatness (Wiener entropy) of the tail mag in 500-8k
    P=(np.abs(np.fft.rfft(seg))**2)[band]
    flat=np.exp(np.mean(np.log(P+1e-20)))/(np.mean(P)+1e-20)
    # crest factor of the IR
    crest=np.max(np.abs(m))/(np.sqrt(np.mean(m**2))+1e-20)
    # autocorrelation periodicity (metallic): strongest peak of normalized autocorr beyond 1ms, in late tail
    tail=m[int(0.1*SR):int(0.6*SR)]; ac=np.correlate(tail,tail,'full')[len(tail)-1:]; ac/=ac[0]+1e-20
    lag0=int(0.001*SR); metal=np.max(np.abs(ac[lag0:int(0.03*SR)]))
    # modal density: count spectral peaks per kHz in 1-6k of the tail mag (fine res)
    nfft=1<<18; Xf=np.abs(np.fft.rfft(m[:int(1.0*SR)],nfft)); ff=np.fft.rfftfreq(nfft,1/SR)
    sel=(ff>1000)&(ff<6000); xs=Xf[sel]
    peaks=np.sum((xs[1:-1]>xs[:-2])&(xs[1:-1]>xs[2:]))
    pk_per_khz=peaks/5.0
    return dict(Ts=Ts,D50=D50,cen_early=cen_early,cen_late=cen_late,rough=rough,flat=flat,crest=crest,metal=metal,pk_per_khz=pk_per_khz)
cands=[("reference",rb.load_ref("ir/vintage-plate-1.5s.wav")),
       ("shipped-v3",ir_render("./render_character",".",{})),
       ("clean-hybrid",ir_render("../plate_proto/render_proto","../plate_proto",{"HFM":"1","VLV":"1","DIFF":"0"}))]
res={n:measures(c) for n,c in cands}
keys=["Ts","D50","cen_early","cen_late","rough","flat","crest","metal","pk_per_khz"]
labels={"Ts":"CenterTime ms","D50":"Definition D50","cen_early":"centroid 0-20ms","cen_late":"centroid 150-300ms",
"rough":"spectral roughness dB","flat":"spectral flatness","crest":"crest factor","metal":"autocorr periodicity","pk_per_khz":"modal peaks/kHz"}
print(f"{'metric':24s}"+"".join(f"{n:>14s}" for n,_ in cands))
for k in keys:
    print(f"{labels[k]:24s}"+"".join(f"{res[n][k]:>14.3f}" for n,_ in cands))
