"""Offline proof: does a velvet HF late-field, added to the multi-band IR, supply the
6-12k rising-lifetime shimmer the FDN can't? Renders multi-band, synthesizes velvet
(sparse +-1 taps, per-sub-band exp envelopes with INCREASING t60, band-limited,
decorrelated L/R, low level), sums, and measures the gradient + anchor guards.

Run from plate_proto/ :  python3 ../reverb_analysis/velvet_proof.py [lvl_scale]
"""
import os, sys, subprocess
sys.path.insert(0, os.path.dirname(__file__))
import numpy as np
import reverb_battery as rb
import lateslope as ls

SR = rb.sr

def render_mb():
    e = {**os.environ, "RV_T60": "2.45", "HFM": "1"}
    out = "/tmp/vp_mb.f32"
    subprocess.run(["./render_proto","plate","impulse.f32",out], env=e, check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return rb.load_ours(out)  # L, R (onset ~ sample 10)

def bp(x, lo, hi):
    n = len(x); X = np.fft.rfft(x); fr = np.fft.rfftfreq(n, 1/SR)
    H = ((fr>=lo)&(fr<=hi)).astype(float)
    # smooth edges (raised-cosine, 1/3-oct)
    for f0,rising in ((lo,True),(hi,False)):
        w = f0*0.09
        m = (np.abs(fr-f0)<w)
        ramp = 0.5*(1+np.cos(np.pi*(fr[m]-f0)/w))
        H[m] = ramp if not rising else (1-ramp)
    return np.fft.irfft(X*H, n)

def velvet_band(N, dens, t60, seed):
    rng = np.random.default_rng(seed)
    v = np.zeros(N)
    step = SR/dens
    pos = 0.0
    while pos < N-2:
        i = int(pos + rng.uniform(-0.3,0.3)*step)
        if 0 <= i < N: v[i] += rng.choice([-1.0,1.0])
        pos += step
    env = np.exp(-6.9078*np.arange(N)/(t60*SR))
    return v*env

def make_velvet(N, seed, bands):
    out = np.zeros(N)
    for k,(lo,hi,t60,lv,dens) in enumerate(bands):
        x = velvet_band(N, dens, t60, seed*97+k)
        x = bp(x, lo, hi)
        x *= lv/ (np.sqrt(np.mean(x**2))+1e-12)   # normalize then scale by lv
        out += x
    return out

def main():
    a=sys.argv[1:]
    l1=float(a[0]) if len(a)>0 else 0.6e-5
    l2=float(a[1]) if len(a)>1 else 0.85e-5
    l3=float(a[2]) if len(a)>2 else 0.6e-5
    L,R = render_mb(); N=len(L)
    # velvet bands: (lo, hi, t60_s, level_rms, density/s) -- t60 RISES, levels graded up, steeper/less-overlap
    bands = [(6500, 8200, 4.6, l1, 3000),
             (8200,10000, 5.4, l2, 3000),
             (9800,12500, 6.3, l3, 3000)]
    vL = make_velvet(N, 1, bands)
    vR = make_velvet(N, 2, bands)
    hL, hR = L+vL, R+vR
    # measure
    ref = rb.load_ref("../reverb_analysis/ir/vintage-plate-1.5s.wav"); rm=(ref[0]+ref[1])/2
    mb_m=(L+R)/2; hy_m=(hL+hR)/2
    def grad(m): return {q: ls.late_slope(m,q) for q in ls.FREQS}
    gr=grad(rm); gmb=grad(mb_m); ghy=grad(hy_m)
    print("late-slope dB/s   freq | ref | multiband | +velvet | target")
    for q in ls.FREQS:
        print(f"  {int(q):>6} | {gr[q]:6.1f} | {gmb[q]:7.1f} | {ghy[q]:6.1f} | {ls.REF_TARGET[q]:6.1f}")
    ot=rb.per_band_decay(hy_m,8.0,-5,-35); rt=rb.per_band_decay(rm,8.0,-5,-35)
    print(f"hybrid: T30err {np.nanmean(np.abs(ot-rt)):.3f}  cent {rb.centroid(hy_m):.0f}(ref {rb.centroid(rm):.0f})  modal {rb.modal_depth(hy_m):.2f}  corr {np.corrcoef(hL,hR)[0,1]:+.2f}")
    print(f"multiband-only cent {rb.centroid(mb_m):.0f}")
    # save hybrid IR for the EDR figure
    np.stack([hL,hR],1).astype('<f4').tofile("/tmp/vp_hybrid.f32")
    print("wrote /tmp/vp_hybrid.f32")

if __name__=="__main__": main()
