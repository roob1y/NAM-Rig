"""Frequency-resolved late-slope (lifetime-gradient) measurement.

The reference plate's HF tail is TWO-SLOPE and the late slope RISES with frequency
(top rings longest). This harness reports the late-slope (dB/s) in the -35..-55 dB
Schroeder window per frequency, for ours vs reference, so we can see whether the
multi-band reservoir produces the rising gradient.

Reference target (from plate_locked_geometry.txt LIFETIME-GRADIENT FINDING):
    3.5k -20.7 (dies fast)  5k -16.1   8k -11.1   11k -9.6 dB/s (rings LONGEST)

Run from within plate_proto/ :  python3 ../reverb_analysis/lateslope.py [label] [t60]
"""
import sys, os, subprocess
sys.path.insert(0, os.path.join(os.path.dirname(__file__)))
import numpy as np
import reverb_battery as rb

FREQS = [3500.0, 5000.0, 8000.0, 11000.0]
REF_TARGET = {3500.0: -20.7, 5000.0: -16.1, 8000.0: -11.1, 11000.0: -9.6}


def late_slope(mono, fc, lo=-35.0, hi=-55.0):
    """Slope (dB/s) of the Schroeder curve between lo and hi dB for band fc."""
    b = rb.bp(mono, fc)
    on = rb.onset(b)
    sch = rb.schroeder_db(b[on:])
    i_lo = np.where(sch <= lo)[0]
    i_hi = np.where(sch <= hi)[0]
    if len(i_lo) == 0 or len(i_hi) == 0:
        return np.nan
    t_lo = i_lo[0] / rb.sr
    t_hi = i_hi[0] / rb.sr
    if t_hi <= t_lo:
        return np.nan
    return (hi - lo) / (t_hi - t_lo)


def render(binpath="./render_proto", t60=2.45, out=None):
    out = out or f"/tmp/ls_{os.getpid()}.f32"
    subprocess.run([binpath, "plate", "impulse.f32", out],
                   env={**os.environ, "RV_T60": str(t60)},
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    Lo, Ro = rb.load_ours(out)
    return (Lo + Ro) / 2


def ref_mono(path="../reverb_analysis/ir/vintage-plate-1.5s.wav"):
    L, R = rb.load_ref(path)
    return (L + R) / 2


def report(label="proto", t60=2.45, binpath="./render_proto"):
    om = render(binpath, t60)
    rm = ref_mono()
    print(f"[{label}] late-slope dB/s (-35..-55 window)  | gradient should RISE with freq")
    print(f"  {'freq':>6} | {'ours':>7} | {'ref':>7} | {'target':>7} | err")
    for f in FREQS:
        so = late_slope(om, f)
        sr_ = late_slope(rm, f)
        tg = REF_TARGET[f]
        print(f"  {int(f):>6} | {so:>7.1f} | {sr_:>7.1f} | {tg:>7.1f} | {so-tg:+.1f}")
    # rising check: ours slope at 11k should be SHALLOWER (less negative) than at 3.5k
    s_lo = late_slope(om, 3500.0); s_hi = late_slope(om, 11000.0)
    direction = "RISING (good)" if s_hi > s_lo else "BACKWARDS (platform)"
    print(f"  gradient 3.5k->11k: {s_lo:.1f} -> {s_hi:.1f}  = {direction}")


if __name__ == "__main__":
    lbl = sys.argv[1] if len(sys.argv) > 1 else "proto"
    t = float(sys.argv[2]) if len(sys.argv) > 2 else 2.45
    report(lbl, t)
