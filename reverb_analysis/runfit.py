import fit, numpy as np, json, sys
# Fit each anchor knob independently for (A1,B1,A2,B2), warm start
results={}
seeds={4.5:[0.4,0.0,5e-5,2e-5],4.0:[0.35,0.0,5e-5,2e-5],3.5:[0.25,0.0,4e-5,1e-5],
       3.0:[0.18,0.0,3e-5,1e-5],2.0:[0.0,0.0,0.0,0.0]}
order=[float(x) for x in sys.argv[1:]] or [4.5,4.0,3.5,3.0]
for k in order:
    p,c=fit.optimize(k,seeds.get(k,[0.2,0,3e-5,1e-5]),[0.12,0.04,3e-5,2e-5],iters=6)
    ref=fit.ref_curve(k); our,mx=fit.curve(k,*p)
    me=float(np.mean(np.abs(our-ref)))
    results[k]={'p':p,'wcost':c,'meanerr':me,'mx':mx,'our':list(map(float,our))}
    print(f"k={k} p={[round(x,6) for x in p]} wcost={c:.3f} meanerr={me:.3f} mx={mx:.3f}",flush=True)
    json.dump(results,open("fitres.json","w"),indent=1)
print("DONE",flush=True)
