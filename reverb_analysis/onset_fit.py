"""Auto-fit the early field to the REFERENCE ONSET (splice-proven target), no human in the loop.
Error = perceptual time-frequency match of the first 120ms: per-ERB-band log energy over 5ms frames,
level-normalized. Minimizing it makes our onset's energy-vs-time-vs-frequency = the reference's.
Run from reverb_analysis/:  python3 onset_fit.py random <N> <seed>
"""
import sys,os,subprocess,random;sys.path.insert(0,'.')
import numpy as np, reverb_battery as rb
SR=rb.sr
def erb_bands(lo,hi,n):
    h=lambda f:21.4*np.log10(4.37e-3*f+1);ih=lambda e:(10**(e/21.4)-1)/4.37e-3
    return ih(np.linspace(h(lo),h(hi),n))
FC=erb_bands(150,13000,16)
def bp(x,f0,nf):
    X=np.fft.rfft(x,nf);f=np.fft.rfftfreq(nf,1/SR);bw=max(60,0.18*f0);g=np.exp(-0.5*((f-f0)/(bw/2))**2)
    return np.fft.irfft(X*g,nf)[:len(x)]
def tf_map(m):
    m=m[rb.onset(m):]; N=int(0.12*SR); m=m[:N]; nf=1<<15
    fr=int(0.005*SR); nfr=N//fr; M=np.zeros((len(FC),nfr))
    for bi,f0 in enumerate(FC):
        b=bp(m,f0,nf)
        for j in range(nfr): M[bi,j]=np.sqrt(np.mean(b[j*fr:(j+1)*fr]**2)+1e-12)
    return 20*np.log10(M+1e-9)
def norm(M): return M-np.max(M)
REFM=norm(tf_map((lambda LR:(LR[0]+LR[1])/2)(rb.load_ref("ir/vintage-plate-1.5s.wav"))))
def score(env):
    out=f"/tmp/of_{abs(hash(tuple(sorted(env.items()))))%99999}.f32"
    subprocess.run(["../plate_proto/render_proto","plate",os.path.abspath("impulse_short.f32"),out],cwd="../plate_proto",
        env={**os.environ,"RV_T60":"2.45","HFM":"1","VLV":"1","EARLY":"1",**env},check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    L,R=rb.load_ours(out);M=norm(tf_map((L+R)/2))
    return float(np.mean(np.abs(M-REFM)))
def rnd_env(r):
    return {"EARLYLV":f"{r.uniform(0.1,0.6):.3f}","EARLYLP":f"{r.choice([6000,7500,9000,11000,16000])}",
            "EARLYPK":f"{r.choice([25,40,55,70])}","EARLYMS":f"{r.choice([60,80,100])}",
            "EARLYOLD":f"{r.uniform(0.0,1.2):.2f}"}
if __name__=="__main__":
    n=int(sys.argv[2]) if len(sys.argv)>2 else 10; seed=int(sys.argv[3]) if len(sys.argv)>3 else 1
    r=random.Random(seed); base=score({"EARLYLV":"0.30","EARLYLP":"8500","EARLYPK":"48","EARLYMS":"85","EARLYOLD":"0.6"})
    print(f"current default err={base:.3f}")
    res=[]
    for _ in range(n):
        e=rnd_env(r)
        try: res.append((score(e),e))
        except Exception as ex: pass
    res.sort(key=lambda x:x[0])
    for s,e in res[:5]: print(f"err={s:.3f}  "+" ".join(f"{k}={v}" for k,v in e.items()))
