import numpy as np, subprocess, os, sys
import reverb_battery as rb

KNOBS = [0.5,0.7,1.0,1.5,2.0,2.5,3.0,3.5,4.0,4.5]
RVT60 = {0.5:0.81,0.7:1.34,1.0:1.87,1.5:2.45,2.0:2.93,2.5:3.16,3.0:3.42,3.5:3.59,4.0:3.78,4.5:3.90}

def ref_curve(knob):
    s,sr = rb.read_wav(f"ir/vintage-plate-{knob}s.wav")
    mono = (s[:,0]+s[:,1])/2 if s.ndim==2 else s
    return rb.per_band_decay(mono, 8.0, -5, -35)

def our_curve(knob):
    t60 = RVT60[knob]
    out = f"/tmp/r_{knob}.f32"
    env = dict(os.environ); env["RV_T60"]=str(t60)
    subprocess.run(["./render_character","plate","impulse.f32",out],env=env,
                   stderr=subprocess.DEVNULL,check=True)
    a = np.fromfile(out,dtype='<f4').reshape(-1,2)
    mono = (a[:,0]+a[:,1])/2
    if not np.all(np.isfinite(mono)): return None, float('inf'), mono
    return rb.per_band_decay(mono, 8.0, -5, -35), float(np.max(np.abs(mono))), mono

if __name__=="__main__":
    knobs = KNOBS
    if len(sys.argv)>1:
        knobs=[float(x) for x in sys.argv[1:]]
    print(f"{'knob':>5} {'T30err':>7} {'maxabs':>8}  per-band err (62..11k)")
    tot=[]
    for k in knobs:
        ref = ref_curve(k)
        our, mx, mono = our_curve(k)
        if our is None:
            print(f"{k:5} UNSTABLE/NONFINITE"); continue
        d = np.abs(our-ref)
        # mean over 15 bands
        err = float(np.mean(d))
        # also err ignoring 62Hz band (idx0)
        err125 = float(np.mean(d[1:]))
        tot.append(err)
        bands=" ".join(f"{x:4.1f}" for x in d)
        print(f"{k:5} {err:7.3f} {mx:8.2f}  {bands}")
    print(f"MEAN T30err over knobs: {np.mean(tot):.3f}")
