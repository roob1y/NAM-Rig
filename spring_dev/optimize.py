# Resumable differential evolution (pure numpy). Checkpoints full population so it can
# run in <45s chunks across independent shell calls. Bloom-first loss.
import numpy as np, json, time, sys, os
import engine, loss
KEYS=engine.KEYS; D=len(KEYS)
LO=np.array([engine.BOUNDS[k][0] for k in KEYS]); HI=np.array([engine.BOUNDS[k][1] for k in KEYS])
CKPT='ckpt.npz'
def evalv(v):
    v=np.clip(v,LO,HI); th=engine.vec_to_theta(v)
    try:
        L,R=engine.render_ir(th)
        if not np.isfinite(np.abs(L).max()) or np.abs(L).max()<1e-6: return 1e6
        return float(loss.loss(L,R))
    except Exception: return 1e6
def save(P,F,gen):
    np.savez(CKPT,P=P,F=F,gen=gen)
    b=F.argmin(); np.save('best_vec.npy',P[b])
    json.dump({k:float(P[b][j]) for j,k in enumerate(KEYS)},open('best_theta.json','w'),indent=1)
def main(pop=24, budget_s=38, max_gen=80):
    rng=np.random.default_rng(int(time.time()))
    if os.path.exists(CKPT):
        d=np.load(CKPT); P=d['P']; F=d['F']; gen=int(d['gen'])
    else:
        P=LO+rng.random((pop,D))*(HI-LO); F=np.array([evalv(P[i]) for i in range(len(P))]); gen=0
        save(P,F,gen)
    pop=len(P); t0=time.time()
    log=open('opt_log.txt','a')
    while gen<max_gen and time.time()-t0<budget_s:
        for i in range(pop):
            idxs=[x for x in range(pop) if x!=i]; a,b,c=P[rng.choice(idxs,3,replace=False)]
            mut=np.clip(a+(0.5+0.3*rng.random())*(b-c),LO,HI)
            mask=rng.random(D)<0.9; mask[rng.integers(D)]=True
            trial=np.where(mask,mut,P[i]); ft=evalv(trial)
            if ft<F[i]: P[i]=trial; F[i]=ft
        gen+=1; b=F.argmin()
        msg=f"gen {gen:3d}  best={F[b]:.3f}  mean={F.mean():.2f}"
        log.write(msg+"\n"); log.flush(); save(P,F,gen)
    b=F.argmin(); print(f"stopped at gen {gen}, best={F[b]:.3f}"); log.close()
def report():
    d=np.load(CKPT); P=d['P']; F=d['F']; b=F.argmin()
    th=engine.vec_to_theta(P[b]); L,R=engine.render_ir(th); tot,fp,parts=loss.loss(L,R,detail=True)
    print("best loss=%.3f at gen %d"%(tot,int(d['gen'])))
    print("ttp ",{k:round(v) for k,v in fp['ttp'].items()}," (tgt 250:75 500:77 1k:81 2k:28 4k:38)")
    print("rt60",{k:round(v,2) for k,v in fp['rt60'].items()}," (tgt 4.4/4.0/3.2/2.8/2.7)")
    print("centroid %d corr %.3f split_ms %.0f (tgt %.0f)"%(fp['centroid'],fp['corr'],parts['split_ms'],parts['tgt_split_ms']))
    print("parts",{k:round(v,2) for k,v in parts.items() if k not in('split_ms','tgt_split_ms')})
if __name__=='__main__':
    if len(sys.argv)>1 and sys.argv[1]=='report': report()
    else: main(budget_s=int(sys.argv[1]) if len(sys.argv)>1 else 38)
