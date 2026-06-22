# validate_battery.py — independent, second-implementation cross-check of the
# numpy battery, using stricter ISO-3382-style estimators. PURE NUMPY (the sandbox
# has no PyPI access, so no scipy/python-acoustics). The point is rigor through
# INDEPENDENCE: if two different implementations agree, the metric is trustworthy;
# if they diverge, we found a bug or a fragile estimator.
#
# What's stricter here vs reverb_battery.py:
#   1. Band filter: zero-phase FFT BRICKWALL octave bands (exact f/√2..f√2 edges)
#      instead of a broad Q=2 RBJ band-pass — removes inter-band leakage.
#   2. Schroeder: Chu noise SUBTRACTION + Lundeby truncation point (stop integrating
#      where the decay meets the noise floor) — the battery integrates the whole
#      window, which biases long/HF decays via the noise tail.
#   3. Decay time: least-squares slope fit over the eval region (ISO 3382) instead
#      of first-threshold-crossing, and reports nonlinearity (1000*(1-r^2)) so a
#      bad/curved fit is visible rather than silently wrong.
#
# usage: python3 validate_battery.py --ours ours_plate_2.9.f32 --ref ir/vintage-plate-2.0s.wav
import numpy as np, argparse
import reverb_battery as rb           # reuse onset/per_band_decay/c80 for the A/B
from wavutil import read_wav

sr = 48000.0
FC  = rb.FC
OCT = rb.OCT

def load_ours(p): a=np.fromfile(p,dtype='<f4').reshape(-1,2); return (a[:,0]+a[:,1])/2
def load_ref(p):  a,_=read_wav(p); a=a[:,:2]; return (a[:,0]+a[:,1])/2

def onset(m): return int(np.argmax(np.abs(m) > 0.01*np.max(np.abs(m))))

def band_brickwall(x, fc):
    # zero-phase exact octave band f/√2..f√2 via rfft masking
    nfft = 1<<int(np.ceil(np.log2(len(x)+8)))
    X = np.fft.rfft(x, nfft); f = np.fft.rfftfreq(nfft, 1/sr)
    lo, hi = fc/2**0.5, fc*2**0.5
    X[(f<lo)|(f>=hi)] = 0
    return np.fft.irfft(X, nfft)[:len(x)]

def lundeby_truncate(e):
    # e = squared, band-filtered energy (1-D). Returns index to truncate at and the
    # estimated noise-floor energy (for Chu subtraction). Simplified Lundeby.
    n = len(e)
    noise = np.mean(e[int(0.9*n):])            # tail 10% = noise estimate
    # smooth energy in ~20ms blocks, find where it drops to within 5 dB of noise
    w = max(1, int(0.02*sr)); k = n//w
    if k < 3: return n, noise
    blk = np.array([np.mean(e[i*w:(i+1)*w]) for i in range(k)])
    db = 10*np.log10(blk + 1e-30)
    nf = 10*np.log10(noise + 1e-30)
    cross = np.argmax(db <= nf + 5.0)          # first block within 5 dB of floor
    idx = n if cross == 0 else min(n, cross*w)
    return idx, noise

def schroeder_db_strict(x):
    e = x**2
    idx, noise = lundeby_truncate(e)
    e = e[:idx] - noise                         # Chu noise subtraction
    e = np.clip(e, 0, None)
    sch = np.cumsum(e[::-1])[::-1]
    sch /= sch[0] + 1e-20
    return 10*np.log10(sch + 1e-20)

def decay_lsq(db, lo, hi):
    # least-squares slope over [lo,hi] dB region -> seconds to fall 60 dB; + nonlinearity
    i1 = np.argmax(db <= lo); i2 = np.argmax(db <= hi)
    if i2 <= i1 + 8: return np.nan, np.nan
    seg = db[i1:i2]; t = np.arange(len(seg))/sr
    A = np.vstack([t, np.ones_like(t)]).T
    slope, _ = np.linalg.lstsq(A, seg, rcond=None)[0]
    if slope >= 0: return np.nan, np.nan
    r = np.corrcoef(t, seg)[0,1]
    return 60.0/(-slope), 1000.0*(1 - r*r)      # T (s), nonlinearity xi (ISO 3382)

def per_band_strict(m, lo, hi, win=6.0):
    on = onset(m); seg = m[on:on+int(sr*win)]
    Ts, xis = [], []
    for fc in FC:
        db = schroeder_db_strict(band_brickwall(seg, fc))
        T, xi = decay_lsq(db, lo, hi); Ts.append(T); xis.append(xi)
    return np.array(Ts), np.array(xis)

def c80_strict(m, win=6.0):
    on = onset(m); seg = m[on:on+int(sr*win)]; out=[]
    for fc in OCT:
        b = band_brickwall(seg, fc); k = int(0.08*sr)
        early = np.sum(b[:k]**2); late = np.sum(b[k:]**2)+1e-20
        out.append(10*np.log10(early/late))
    return np.array(out)

def main():
    ap=argparse.ArgumentParser(); ap.add_argument('--ours',required=True); ap.add_argument('--ref',required=True)
    a=ap.parse_args()
    oM=load_ours(a.ours); rM=load_ref(a.ref)

    print("\n===== CROSS-CHECK: strict ISO-3382/Lundeby (numpy) vs the battery =====")
    # battery numbers
    bT30o=rb.per_band_decay(oM,6.0,-5,-35); bT30r=rb.per_band_decay(rM,6.0,-5,-35)
    bEDTo=rb.per_band_decay(oM,6.0,0,-10);  bEDTr=rb.per_band_decay(rM,6.0,0,-10)
    bC80o=rb.c80_band(oM); bC80r=rb.c80_band(rM)
    # strict numbers
    sT30o,xio=per_band_strict(oM,-5,-35); sT30r,xir=per_band_strict(rM,-5,-35)
    sEDTo,_=per_band_strict(oM,0,-10);    sEDTr,_=per_band_strict(rM,0,-10)
    sC80o=c80_strict(oM); sC80r=c80_strict(rM)

    def cmp_band(name, bands, batt, strict):
        print(f"\n-- {name} : battery vs strict --")
        print("band ", " ".join(f"{int(f):5d}" for f in bands))
        print("batt ", " ".join(f"{v:5.2f}" for v in batt))
        print("strct", " ".join(f"{v:5.2f}" for v in strict))
        print("diff ", " ".join(f"{(strict[i]-batt[i]):+5.2f}" for i in range(len(bands))))
        d=strict-batt; print(f"  -> mean|diff| {np.nanmean(np.abs(d)):.3f}  max|diff| {np.nanmax(np.abs(d)):.3f}")

    print("\n############ OURS (plate render) ############")
    cmp_band("T30(f)", FC,  bT30o, sT30o)
    cmp_band("EDT(f)", FC,  bEDTo, sEDTo)
    cmp_band("C80",    OCT, bC80o, sC80o)
    print("\n############ REFERENCE ############")
    cmp_band("T30(f)", FC,  bT30r, sT30r)
    cmp_band("EDT(f)", FC,  bEDTr, sEDTr)
    cmp_band("C80",    OCT, bC80r, sC80r)

    print("\n-- decay-curve nonlinearity xi=1000(1-r^2) (ISO 3382: <5 good, >10 curved/noisy) --")
    print("ours T30 xi:", " ".join(f"{v:4.1f}" for v in xio))
    print("ref  T30 xi:", " ".join(f"{v:4.1f}" for v in xir))

    # the verdict that matters: does the OURS-vs-REF gap survive the stricter method?
    print("\n===== does the ours-vs-ref gap hold under the strict method? =====")
    i1k=list(FC).index(1000)
    print(f"T30@1k   battery: ours {bT30o[i1k]:.2f} ref {bT30r[i1k]:.2f} d={bT30o[i1k]-bT30r[i1k]:+.2f}   strict: ours {sT30o[i1k]:.2f} ref {sT30r[i1k]:.2f} d={sT30o[i1k]-sT30r[i1k]:+.2f}")
    print(f"T30 mean|ours-ref|  battery {np.nanmean(np.abs(bT30o-bT30r)):.2f}s   strict {np.nanmean(np.abs(sT30o-sT30r)):.2f}s")
    print(f"C80 mean(ours-ref)  battery {np.nanmean(bC80o-bC80r):+.2f}dB  strict {np.nanmean(sC80o-sC80r):+.2f}dB")

if __name__=='__main__': main()
