import sys; sys.path.insert(0,"../reverb_analysis")
import numpy as np, subprocess, os
import reverb_battery as rb
FC=list(rb.FC)
BASE=np.array([6.5,7.9,9.2,10.6,12.1,13.7,15.2,16.8,18.5,20.1,21.8,23.6,25.3,27.1,29.0,30.8,
   32.7,34.6,36.6,38.5,40.5,42.6,44.7,46.8,49.0,51.2,53.4,55.7,58.0,60.4,62.8,65.3])
def lines_for(seed,mag): return BASE*(1+mag*np.random.default_rng(int(seed)).uniform(-1,1,32))
def render(lines,dg,dg2,t60):
    lf=f"/tmp/p2L.txt"; open(lf,"w").write(" ".join(f"{x:.3f}" for x in lines)); out="/tmp/p2.f32"
    subprocess.run(["./render_proto","plate","impulse.f32",out],
        env={**os.environ,"RV_T60":str(t60),"PLINES":lf,"PDISPG":str(dg),"PDISPG2":str(dg2)},
        check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    Lo,Ro=rb.load_ours(out); return (Lo+Ro)/2,Lo,Ro
# baseline (shipped) refs
L,R=rb.load_ref("../reverb_analysis/ir/vintage-plate-1.5s.wav"); RM=(L+R)/2
c80r=rb.c80_band(RM); msr=rb.midside_band(*( (rb.load_ref("../reverb_analysis/ir/vintage-plate-1.5s.wav")[0]),(rb.load_ref("../reverb_analysis/ir/vintage-plate-1.5s.wav")[1]) ))
# shipped baseline at anchor
mb,Lb,Rb=render(BASE,0.62,0.55,2.45)
c80b=rb.c80_band(mb)
cands=[(957825,0.06,0.50,0.55),(726442,0.15,0.62,0.45),(421117,0.10,0.45,0.35),(837575,0.06,0.40,0.40),(725875,0.04,0.45,0.55)]
oct=[125,250,500,1000,2000,4000,8000]
print("C80 low (125/250) ref:",f"{c80r[0]:.1f}/{c80r[1]:.1f}","| shipped:",f"{c80b[0]:.1f}/{c80b[1]:.1f}")
i11=FC.index(11000)
for seed,mag,dg,dg2 in cands:
    m,Lo,Ro=render(lines_for(seed,mag),dg,dg2,2.45)
    c80=rb.c80_band(m)
    # side/mid err vs ref
    ms=rb.midside_band(Lo,Ro); 
    msdev=np.nanmean(np.abs(np.array(ms)-np.array(msr)))
    # 11k at knob 4.5
    m45,_,_=render(lines_for(seed,mag),dg,dg2,3.90)
    t11=rb.per_band_decay(m45,8.0,-5,-35)[i11]
    print(f"seed {seed}: C80(125/250)={c80[0]:+.1f}/{c80[1]:+.1f} (ship {c80b[0]:+.1f}/{c80b[1]:+.1f})  sideMidErr={msdev:.2f}dB  11k@4.5={t11:.2f}(ref6.60)")
