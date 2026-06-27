# delay_fit.py — metric-driven Tape Echo voicing fit. Renders candidate voicings
# through the REAL engine (proto delay_render with setTapeVoicingOverride) and
# coordinate-descends the voicing params to minimise the band error against the
# reference single-repeat spectrum + per-pass transfer (the battery graphs).
#
# Reliable bands only: single-repeat 40 Hz–5 kHz (above that the reference echo is
# into the noise floor); per-pass 60–900 Hz (the in-loop low-mid bloom — above
# ~1.5 kHz the tail HF is noise). Everything is normalised @1 kHz, like the battery.
#
#   usage: python3 delay_fit.py --render /tmp/delay_render --ref-dir <delay_references/tape_echo> --out /tmp/fit
import numpy as np, subprocess, os, argparse, importlib.util
spec = importlib.util.spec_from_file_location("db", os.path.join(os.path.dirname(__file__), "delay_battery.py"))
db = importlib.util.module_from_spec(spec); spec.loader.exec_module(db)
FC = db.FC

PARAMS = ['hbDb', 'hbHz', 'hbQ', 'gapHz', 'obDb', 'ppDb', 'sat']
BOUNDS = {'hbDb': (1.5, 9.0), 'hbHz': (180, 430), 'hbQ': (0.4, 1.1),
          'gapHz': (900, 2600), 'obDb': (-12, -1), 'ppDb': (-3, 6), 'sat': (1.0, 2.4)}
START = {'hbDb': 4.0, 'hbHz': 330, 'hbQ': 0.6, 'gapHz': 2100, 'obDb': -6.0, 'ppDb': 4.0, 'sat': 1.2}

def render(render_bin, test, p, out):
    args = [render_bin, '--char', 'tape', '--test', test, '--out', out,
            '--hbDb', f"{p['hbDb']:.3f}", '--hbHz', f"{p['hbHz']:.1f}", '--hbQ', f"{p['hbQ']:.3f}",
            '--gapHz', f"{p['gapHz']:.1f}", '--obDb', f"{p['obDb']:.3f}", '--ppDb', f"{p['ppDb']:.3f}",
            '--sat', f"{p['sat']:.3f}"]
    subprocess.run(args, stderr=subprocess.DEVNULL, check=True)

def curves(render_bin, p, tmp):
    render(render_bin, 'impulse', p, f'{tmp}/imp.f32')
    render(render_bin, 'tail', p, f'{tmp}/tail.f32')
    sr = db.single_repeat_spectrum(db.load_ours(f'{tmp}/imp.f32'))[0]
    pp = db.norm_at(db.per_pass_transfer(db.load_ours(f'{tmp}/tail.f32'))[0], FC, 1000.0)
    return sr, pp

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--render', required=True)
    ap.add_argument('--ref-dir', required=True)
    ap.add_argument('--out', default='/tmp/fit')
    a = ap.parse_args(); os.makedirs(a.out, exist_ok=True)

    ref_imp = db.load_ref(f'{a.ref_dir}/impulse.wav'); ref_tail = db.load_ref(f'{a.ref_dir}/tail.wav')
    SRref = db.single_repeat_spectrum(ref_imp)[0]
    PPref = db.norm_at(db.per_pass_transfer(ref_tail)[0], FC, 1000.0)
    # reliability weights
    w_sr = ((FC >= 40) & (FC <= 5200)).astype(float)
    w_sr[FC > 3500] *= 0.5                      # taper as the echo nears noise floor
    w_pp = ((FC >= 60) & (FC <= 2500)).astype(float)   # clean-pair band: bloom + HF roll
    w_pp[FC > 1500] *= 0.6

    def cost(p):
        sr, pp = curves(a.render, p, a.out)
        e_sr = np.nansum(w_sr * (sr - SRref) ** 2) / np.sum(w_sr)
        e_pp = np.nansum(w_pp * (pp - PPref) ** 2) / np.sum(w_pp)
        return e_sr + e_pp, e_sr, e_pp

    p = dict(START)
    base = cost(p); print(f"start  cost {base[0]:.2f}  (sr {base[1]:.2f} pp {base[2]:.2f})  {p}")
    best = base[0]
    for it in range(4):
        for k in PARAMS:
            lo, hi = BOUNDS[k]
            span = (hi - lo)
            cur = p[k]
            cands = sorted(set([cur, np.clip(cur - span*0.18, lo, hi), np.clip(cur + span*0.18, lo, hi),
                                np.clip(cur - span*0.07, lo, hi), np.clip(cur + span*0.07, lo, hi)]))
            bestv, bestc = cur, best
            for v in cands:
                p[k] = v; c = cost(p)[0]
                if c < bestc - 1e-4: bestc, bestv = c, v
            p[k] = bestv; best = bestc
        # coupled "bloom" move: more in-loop bump + more output bass cut together keeps
        # the single repeat ~flat while raising the per-pass low-mid bloom (escapes the
        # local min coordinate descent gets stuck in, since each alone hurts one curve).
        for d in (3.0, 1.5, -1.5):
            q = dict(p)
            q['hbDb'] = float(np.clip(p['hbDb'] + d, *BOUNDS['hbDb']))
            q['obDb'] = float(np.clip(p['obDb'] - d, *BOUNDS['obDb']))
            c = cost(q)[0]
            if c < best - 1e-4: best = c; p = q
        print(f"pass {it+1}: cost {best:.2f}  " + " ".join(f"{k}={p[k]:.2f}" for k in PARAMS))
    fin = cost(p)
    print(f"\nFITTED cost {fin[0]:.2f} (sr {fin[1]:.2f} pp {fin[2]:.2f})")
    print("voicing:", " ".join(f"{k}={p[k]:.2f}" for k in PARAMS))
    # report band deltas before/after
    srF, ppF = curves(a.render, p, a.out)
    srB, ppB = curves(a.render, START, a.out)
    octs = [125, 250, 500, 1000, 2000, 4000]
    def at(c, h): return c[np.argmin(np.abs(FC - h))]
    print("\nSINGLE-REPEAT  Hz:", " ".join(f"{h:>6d}" for h in octs))
    print("  ref       :", " ".join(f"{at(SRref,h):+6.1f}" for h in octs))
    print("  baseline d:", " ".join(f"{at(srB,h)-at(SRref,h):+6.1f}" for h in octs))
    print("  fitted   d:", " ".join(f"{at(srF,h)-at(SRref,h):+6.1f}" for h in octs))
    print("PER-PASS       Hz:", " ".join(f"{h:>6d}" for h in octs))
    print("  ref       :", " ".join(f"{at(PPref,h):+6.1f}" for h in octs))
    print("  baseline d:", " ".join(f"{at(ppB,h)-at(PPref,h):+6.1f}" for h in octs))
    print("  fitted   d:", " ".join(f"{at(ppF,h)-at(PPref,h):+6.1f}" for h in octs))

if __name__ == '__main__':
    main()
