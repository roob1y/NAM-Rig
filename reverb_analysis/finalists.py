import sys; sys.path.insert(0,"../reverb_analysis")
import numpy as np, subprocess, os
import reverb_battery as rb
FC=list(rb.FC)
BASE=np.array([6.5,7.9,9.2,10.6,12.1,13.7,15.2,16.8,18.5,20.1,21.8,23.6,25.3,27.1,29.0,30.8,
   32.7,34.6,36.6,38.5,40.5,42.6,44.7,46.8,49.0,51.2,53.4,55.7,58.0,60.4,62.8,65.3])
def lines_for(seed,mag): return BASE*(1+mag*np.random.default_rng(int(seed)).uniform(-1,1,32))
def edt(m,f):
    b=rb.bp(m,f); on=rb.onset(b); s=rb.schroeder_db(b[on:]); idx=np.where(s<=-10)[0]; return 6*idx[0]/48000 if len(idx) else np.nan
def render(lines,dg,dg2):
    open("/tmp/fL.txt","w").write(" ".join(f"{x:.3f}" for x in lines))
    subprocess.run(["./render_proto","plate","impulse.f32","/tmp/f.f32"],
      env={**os.environ,"RV_T60":"2.45","PLINES":"/tmp/fL.txt","PDISPG":str(dg),"PDISPG2":str(dg2)},
      check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    Lo,Ro=rb.load_ours("/tmp/f.f32"); return (Lo+Ro)/2,Lo,Ro
L,R=rb.load_ref("../reverb_analysis/ir/vintage-plate-1.5s.wav"); RM=(L+R)/2
print("ref EDT 250-710:",[round(edt(RM,f),2) for f in [250,355,500,710]])
mb,_,_=render(BASE,0.62,0.55); print("shipped EDT:    ",[round(edt(mb,f),2) for f in [250,355,500,710]])
for name,seed,mag,dg,dg2 in [("837575",837575,0.06,0.40,0.40),("957825",957825,0.06,0.50,0.55)]:
    m,Lo,Ro=render(lines_for(seed,mag),dg,dg2)
    ot=rb.per_band_decay(m,8.0,-5,-35); rt=rb.per_band_decay(RM,8.0,-5,-35)
    print(f"{name} EDT:      ",[round(edt(m,f),2) for f in [250,355,500,710]],
          f" T30err {np.nanmean(np.abs(ot-rt)):.3f} cent {rb.centroid(m):.0f} modal {rb.modal_depth(m):.2f} corr {np.corrcoef(Lo,Ro)[0,1]:+.3f}")
