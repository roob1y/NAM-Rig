"""Co-tune the 3-band HF reservoir (R1/R2/R3) to the reference rising late-slope gradient.

Renders the proto for each candidate (env-driven), measures the 4 target late-slopes
plus the anchor guards (centroid, T30err), and ranks by a combined objective.

Run from plate_proto/ :  python3 ../reverb_analysis/opt_multiband.py random 30
                         python3 ../reverb_analysis/opt_multiband.py one R1HP=4500 ...
"""
import sys, os, subprocess, random
sys.path.insert(0, os.path.dirname(__file__))
import numpy as np
import reverb_battery as rb
import lateslope as ls

TARGET = ls.REF_TARGET           # {3500:-20.7, 5000:-16.1, 8000:-11.1, 11000:-9.6}
FREQS = ls.FREQS
CENT_REF = 4560.0


def evaluate(env):
    """Render with env overrides (HFM forced on) and return metrics dict."""
    e = {**os.environ, "RV_T60": "2.45", "HFM": "1"}
    e.update({k: str(v) for k, v in env.items()})
    out = f"/tmp/opt_{os.getpid()}.f32"
    subprocess.run(["./render_proto", "plate", "impulse.f32", out], env=e,
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    Lo, Ro = rb.load_ours(out)
    om = (Lo + Ro) / 2
    slopes = {f: ls.late_slope(om, f) for f in FREQS}
    cent = rb.centroid(om)
    # anchor T30 err vs ref
    rm = ls.ref_mono()
    ot = rb.per_band_decay(om, 8.0, -5, -35); rt = rb.per_band_decay(rm, 8.0, -5, -35)
    t30 = float(np.nanmean(np.abs(ot - rt)))
    modal = rb.modal_depth(om)
    corr = float(np.corrcoef(Lo, Ro)[0, 1])
    peak = float(np.max(np.abs(np.concatenate([Lo, Ro]))))
    grad_err = float(np.mean([abs(slopes[f] - TARGET[f]) for f in FREQS]))
    obj = grad_err + 0.004 * abs(cent - CENT_REF) + 40 * max(0, t30 - 0.06)
    return dict(slopes=slopes, cent=cent, t30=t30, modal=modal, corr=corr,
                peak=peak, grad_err=grad_err, obj=obj)


def fmt(env, m):
    sl = " ".join(f"{int(f)/1000:.1f}k={m['slopes'][f]:.1f}" for f in FREQS)
    es = " ".join(f"{k}={v}" for k, v in env.items())
    return (f"obj={m['obj']:.2f} grad={m['grad_err']:.2f} | {sl} | "
            f"cent={m['cent']:.0f} t30={m['t30']:.3f} modal={m['modal']:.2f} "
            f"corr={m['corr']:+.2f} peak={m['peak']:.3f} | {es}")


def rand_env():
    return {
        "R1HP": random.choice([4000, 4500, 5000]),
        "R2HP": random.choice([6000, 6500, 7000, 7500]),
        "R3HP": random.choice([8500, 9000, 9500]),
        "R1FB": round(random.uniform(0.985, 0.993), 4),
        "R2FB": round(random.uniform(0.990, 0.996), 4),
        "R3FB": round(random.uniform(0.995, 0.9995), 5),
        "R1LV": round(random.uniform(0.015, 0.06), 4),
        "R2LV": round(random.uniform(0.03, 0.12), 4),
        "R3LV": round(random.uniform(0.08, 0.28), 4),
        "R1LHP": 50,
        "R2LHP": random.choice([50, 1500, 2000, 2500]),
        "R3LHP": random.choice([1800, 2200, 2600]),
        "R3SH": 0.7, "R2SH": 0.7,
    }


if __name__ == "__main__":
    mode = sys.argv[1] if len(sys.argv) > 1 else "random"
    if mode == "one":
        env = {}
        for a in sys.argv[2:]:
            k, v = a.split("="); env[k] = v
        m = evaluate(env)
        print(fmt(env, m))
    else:
        n = int(sys.argv[2]) if len(sys.argv) > 2 else 20
        seed = int(sys.argv[3]) if len(sys.argv) > 3 else 1
        random.seed(seed)
        results = []
        for _ in range(n):
            env = rand_env()
            try:
                m = evaluate(env)
                results.append((m["obj"], env, m))
            except Exception as ex:
                print("FAIL", env, ex)
        results.sort(key=lambda r: r[0])
        for obj, env, m in results[:8]:
            print(fmt(env, m))
