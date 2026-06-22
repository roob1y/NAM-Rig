import sys; sys.path.insert(0,"../reverb_analysis")
import numpy as np, subprocess, os
import reverb_battery as rb
FC=list(rb.FC)
def edt_band(m,fc):
    b=rb.bp(m,fc); on=rb.onset(b); sch=rb.schroeder_db(b[on:])
    idx=np.where(sch<=-10.0)[0]; return 6.0*idx[0]/48000.0 if len(idx) else np.nan
def run(label, binpath="./render_proto", t60=2.45):
    out=f"/tmp/mp_{os.getpid()}.f32"
    subprocess.run([binpath,"plate","impulse.f32",out],env={**os.environ,"RV_T60":str(t60)},
                   check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    Lo,Ro=rb.load_ours(out); om=(Lo+Ro)/2
    L,R=rb.load_ref("../reverb_analysis/ir/vintage-plate-1.5s.wav"); rm=(L+R)/2
    ot=rb.per_band_decay(om,8.0,-5,-35); rt=rb.per_band_decay(rm,8.0,-5,-35)
    # band selections
    lm=[250,355,500,710]; idx=[FC.index(f) for f in lm]
    edt_o=[edt_band(om,f) for f in lm]; edt_r=[edt_band(rm,f) for f in lm]
    c80o=rb.c80_band(om); c80r=rb.c80_band(rm)
    t30err=np.nanmean(np.abs(ot-rt))
    # spec err
    so=rb.oct_spectrum(om,8.0); sr_=rb.oct_spectrum(rm,8.0)
    specerr=np.nanmean(np.abs(so-sr_))
    print(f"[{label}]")
    print(f"  EDT 250-710 ours {['%.2f'%x for x in edt_o]}  ref {['%.2f'%x for x in edt_r]}")
    print(f"  T30err(allband) {t30err:.3f}  specerr {specerr:.2f}dB  centroid {rb.centroid(om):.0f}(ref {rb.centroid(rm):.0f})  modal {rb.modal_depth(om):.1f}(ref {rb.modal_depth(rm):.1f})")
    print(f"  L/R corr {np.corrcoef(Lo,Ro)[0,1]:+.2f}")
if __name__=="__main__":
    run(sys.argv[1] if len(sys.argv)>1 else "proto")
