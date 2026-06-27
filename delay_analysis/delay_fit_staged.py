# delay_fit_staged.py — staged Tape Echo voicing fit in DEPENDENCY order:
#   Stage 1  SATURATION (satDrive)  -> match the reference level-sweep compression
#   Stage 2  PER-PASS / in-loop EQ  (hbDb,hbHz,hbQ,gapHz) with satDrive fixed
#   Stage 3  OUTPUT-once shaping    (obDb,obHz,ppDb,ppHz) with in-loop+sat fixed
#
# Each stage renders through the REAL engine (delay_render with --override args /
# setTapeVoicingOverride) and is pinned to its own clean target before the next,
# matching the tape architecture (saturation couples into the bloom via loop level;
# output-tilt = single-repeat minus per-pass).
#
# KEY LESSONS BAKED IN (see DELAY_CHARACTER_PLAYBOOK.md):
#  - per-pass: use the battery's CLEAN-pair transfer (early, above-noise echoes); the
#    averaged/late pairs are noise-floor and corrupt the HF.
#  - fit the gap-loss from the RELIABLE single-repeat HF, NOT the noisy per-pass HF.
#  - render the single-repeat at LOW level (--impAmp ~0.12, near-linear) so it isn't
#    fighting its own saturation -- a unit impulse over-saturates the bump.
#  - keep the output bass-cut corner BELOW 1 kHz so it doesn't skew the @1k norm.
#  - satDrive is PINNED here (matches the saturation top; the bloom is carried by the
#    bump). NOTE the loop couples them: a bigger bump re-hardens the saturation top.
#
# usage: python3 delay_fit_staged.py --render /tmp/delay_render \
#          --ref-dir ../delay_references/tape_echo --out /tmp/fs
import numpy as np, subprocess, os, argparse, importlib.util
spec = importlib.util.spec_from_file_location("db", os.path.join(os.path.dirname(__file__), "delay_battery.py"))
db = importlib.util.module_from_spec(spec); spec.loader.exec_module(db)
FC = db.FC

# start from the committed Tape voicing
START = {'hbDb':9.5,'hbHz':260,'hbQ':0.50,'gapHz':1950,'obDb':-8.5,'obHz':560,'ppDb':6.0,'ppHz':2200,'sat':1.20}

def render(rb, test, p, out, impAmp=None):
    args = [rb,'--char','tape','--test',test,'--out',out,
            '--hbDb',f"{p['hbDb']:.3f}",'--hbHz',f"{p['hbHz']:.1f}",'--hbQ',f"{p['hbQ']:.3f}",
            '--gapHz',f"{p['gapHz']:.1f}",'--obDb',f"{p['obDb']:.3f}",'--obHz',f"{p['obHz']:.1f}",
            '--ppDb',f"{p['ppDb']:.3f}",'--ppHz',f"{p['ppHz']:.1f}",'--sat',f"{p['sat']:.3f}"]
    if impAmp is not None: args += ['--impAmp', f"{impAmp}"]
    subprocess.run(args, stderr=subprocess.DEVNULL, check=True)

def our_sat(rb,p,tmp): render(rb,'levels',p,f'{tmp}/lv.f32'); return db.burst_levels(db.load_ours(f'{tmp}/lv.f32'))
def our_pp(rb,p,tmp):  render(rb,'tail',p,f'{tmp}/tl.f32');   return db.norm_at(db.per_pass_transfer(db.load_ours(f'{tmp}/tl.f32'))[0],FC,1000.0)
def our_sr(rb,p,tmp):  render(rb,'impulse',p,f'{tmp}/im.f32',impAmp=0.12); return db.single_repeat_spectrum(db.load_ours(f'{tmp}/im.f32'))[0]

def descend(cost, p, keys, bounds, passes=3):
    best=cost(p)
    for _ in range(passes):
        for k in keys:
            lo,hi=bounds[k]; span=hi-lo; cur=p[k]; bv,bc=cur,best
            for v in sorted(set([cur,np.clip(cur-span*0.18,lo,hi),np.clip(cur+span*0.18,lo,hi),
                                 np.clip(cur-span*0.07,lo,hi),np.clip(cur+span*0.07,lo,hi)])):
                p[k]=v; c=cost(p)
                if c<bc-1e-4: bc,bv=c,v
            p[k]=bv; best=bc
    return best

def main():
    ap=argparse.ArgumentParser(); ap.add_argument('--render',required=True); ap.add_argument('--ref-dir',required=True)
    ap.add_argument('--out',default='/tmp/fs'); ap.add_argument('--pin-sat',type=float,default=1.20)
    a=ap.parse_args(); os.makedirs(a.out,exist_ok=True); R=a.ref_dir
    SRref=db.single_repeat_spectrum(db.load_ref(f'{R}/impulse.wav'))[0]
    PPref=db.norm_at(db.per_pass_transfer(db.load_ref(f'{R}/tail.wav'))[0],FC,1000.0)
    SATref=db.burst_levels(db.load_ref(f'{R}/levels.wav')); nref=len(SATref)
    refdb=20*np.log10(SATref/SATref[0]+1e-12)
    p=dict(START); octs=[125,250,500,1000,2000,4000]; at=lambda c,h:c[np.argmin(np.abs(FC-h))]

    # Stage 1: saturation (pinned; the curve fit picks the clamp-edge value, so we pin
    # the known-good 1.20 -- raise/lower only if the loop level changes a lot).
    p['sat']=a.pin_sat
    print(f"Stage1 SAT: sat={p['sat']:.2f} (pinned)")

    # Stage 2: per-pass / in-loop (bump broad to match the reference's broad bloom)
    w_pp=((FC>=60)&(FC<=2500)).astype(float); w_pp[FC>1500]*=0.6
    def cost_pp(q): pp=our_pp(a.render,q,a.out); return float(np.nansum(w_pp*(pp-PPref)**2)/np.sum(w_pp))
    descend(cost_pp,p,['hbDb','hbHz','hbQ','gapHz'],
            {'hbDb':(1.5,12),'hbHz':(190,360),'hbQ':(0.30,0.9),'gapHz':(1600,2400)},passes=3)
    print(f"Stage2 PER-PASS: hbDb={p['hbDb']:.2f} hbHz={p['hbHz']:.0f} hbQ={p['hbQ']:.2f} gapHz={p['gapHz']:.0f}  cost={cost_pp(p):.2f}")

    # Stage 3: output-once, fit to the near-linear (low-level) single repeat
    w_sr=((FC>=40)&(FC<=5200)).astype(float); w_sr[FC>3500]*=0.5
    def cost_sr(q): sr=our_sr(a.render,q,a.out); return float(np.nansum(w_sr*(sr-SRref)**2)/np.sum(w_sr))
    descend(cost_sr,p,['obDb','obHz','ppDb','ppHz'],
            {'obDb':(-12,-1),'obHz':(300,560),'ppDb':(-2,9),'ppHz':(1200,3200)},passes=3)
    print(f"Stage3 once: obDb={p['obDb']:.2f} obHz={p['obHz']:.0f} ppDb={p['ppDb']:.2f} ppHz={p['ppHz']:.0f}  cost={cost_sr(p):.2f}")

    srF=our_sr(a.render,p,a.out); ppF=our_pp(a.render,p,a.out); satF=our_sat(a.render,p,a.out)
    print("\nFINAL voicing:", " ".join(f"{k}={p[k]:.2f}" for k in ['sat','hbDb','hbHz','hbQ','gapHz','obDb','obHz','ppDb','ppHz']))
    def row(nm,o,r): print(f"{nm:11s} d"," ".join(f"{at(o,h)-at(r,h):+5.1f}" for h in octs))
    print("              Hz:    125   250   500    1k    2k    4k")
    row("PER-PASS",ppF,PPref); row("SINGLE-REP",srF,SRref); row("OUTPUT TILT",srF-ppF,SRref-PPref)
    n=min(nref,len(satF)); od=20*np.log10(satF[:n]/satF[0]+1e-12)
    print("SATURATION ours",' '.join(f"{v:+5.1f}" for v in od)," | ref",' '.join(f"{v:+5.1f}" for v in refdb[:n]))

if __name__=='__main__': main()
