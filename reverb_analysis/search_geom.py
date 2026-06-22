import sys; sys.path.insert(0,"../reverb_analysis")
import numpy as np, subprocess, os, time, csv
import reverb_battery as rb
FC=list(rb.FC)
BASE=np.array([6.5,7.9,9.2,10.6,12.1,13.7,15.2,16.8,18.5,20.1,21.8,23.6,25.3,27.1,29.0,30.8,
   32.7,34.6,36.6,38.5,40.5,42.6,44.7,46.8,49.0,51.2,53.4,55.7,58.0,60.4,62.8,65.3])
def edt_band(m,fc):
    b=rb.bp(m,fc); on=rb.onset(b); s=rb.schroeder_db(b[on:]); idx=np.where(s<=-10)[0]
    return 6.0*idx[0]/48000.0 if len(idx) else np.nan
# reference anchor curves (compute once)
L,R=rb.load_ref("../reverb_analysis/ir/vintage-plate-1.5s.wav"); RM=(L+R)/2
RT30=rb.per_band_decay(RM,8.0,-5,-35)
REDT={f:edt_band(RM,f) for f in [250,355,500,710]}
RCENT=rb.centroid(RM)
PLAT=[250,355,500,710]
def render(lines,dispg,dispg2,t60=2.45):
    lf=f"/tmp/L{os.getpid()}.txt"; open(lf,"w").write(" ".join(f"{x:.3f}" for x in lines))
    out=f"/tmp/S{os.getpid()}.f32"
    env={**os.environ,"RV_T60":str(t60),"PLINES":lf,"PDISPG":str(dispg),"PDISPG2":str(dispg2)}
    subprocess.run(["./render_proto","plate","impulse.f32",out],env=env,check=True,
                   stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    Lo,Ro=rb.load_ours(out); return (Lo+Ro)/2,Lo,Ro
def objective(m,Lo,Ro):
    ot=rb.per_band_decay(m,8.0,-5,-35)
    t30err=np.nanmean(np.abs(ot-RT30))
    edt={f:edt_band(m,f) for f in PLAT}
    edterr=np.nanmean([abs(edt[f]-REDT[f]) for f in PLAT])
    cent=rb.centroid(m); modal=rb.modal_depth(m); corr=np.corrcoef(Lo,Ro)[0,1]
    # weighted score (lower=better)
    score=3.0*edterr + 6.0*max(0,t30err-0.06) + 1.5*abs(cent-RCENT)/RCENT*10 \
          + (2.0 if modal<6.4 else 0) + 5.0*max(0,abs(corr)-0.10)
    return score,t30err,edterr,cent,modal,corr,edt
def main():
    budget=float(sys.argv[1]) if len(sys.argv)>1 else 38.0
    seed0=int(sys.argv[2]) if len(sys.argv)>2 else 0
    csvp="search_results.csv"; newf=not os.path.exists(csvp)
    f=open(csvp,"a",newline=""); w=csv.writer(f)
    if newf: w.writerow(["seed","mag","dispg","dispg2","score","t30err","edterr","cent","modal","corr"])
    t0=time.time(); n=0; master=np.random.default_rng(seed0)
    while time.time()-t0<budget:
        seed=int(master.integers(0,1_000_000)); rng=np.random.default_rng(seed)
        mag=float(master.choice([0.04,0.06,0.08,0.10,0.12,0.15]))
        dispg=float(master.choice([0.40,0.45,0.50,0.55,0.62]))
        dispg2=float(master.choice([0.35,0.40,0.45,0.50,0.55]))
        pat=rng.uniform(-1,1,32); lines=BASE*(1+mag*pat)
        try: m,Lo,Ro=render(lines,dispg,dispg2)
        except Exception: continue
        sc,t30e,edte,cent,modal,corr,edt=objective(m,Lo,Ro)
        w.writerow([seed,mag,dispg,dispg2,f"{sc:.3f}",f"{t30e:.3f}",f"{edte:.3f}",f"{cent:.0f}",f"{modal:.2f}",f"{corr:.3f}"])
        n+=1
    f.close(); print(f"evaluated {n} candidates in {time.time()-t0:.0f}s")
if __name__=="__main__": main()
