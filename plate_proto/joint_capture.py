#!/usr/bin/env python3
# joint_capture.py — the derivative-free JOINT optimizer the v2 plate notes parked.
# Reuses the existing capture pipeline (decayfit + GEQ + fdnplate10) but fits the FDN
# against a COMBINED loss instead of T60(f) alone:
#     EDR-difference  +  two-slope (convex) decay (decayfit)  +  spectrum  +  echo density
# Pure numpy, CPU, no torch/GPU. Resumable (checkpoint). Built to run on your i5.
#
#   python joint_capture.py "NEVO - studio spring Stereo 4.5s.wav" 600     # grind 10 min, then save+resume
#
# Honest scope: fdnplate10 is a SINGLE FDN -> one slope per band. The two-slope target
# from decayfit pushes the ER/tail balance toward the convex shape but a single FDN can't
# fully realize fast-then-linger per band (same limit we hit hand-tuning the spring). To
# go all the way, add the parallel modal/HF-fill layers as extra optimizable components
# (see spring/capture/) — this script gets the diffuse engine as close as a single FDN can.
import numpy as np, sys, os, time, json, subprocess, struct
import capture as C            # render(), FC, sr, spectral_edges, ir_len_s
from capture_v2 import design_gunit

SR = 48000
FC = C.FC                      # GEQ band centres (16)
# --- inlined from decayfit.py (avoids its import-time demo) : two-slope convex decay ---
DBANDS = [125,180,250,355,500,710,1000,1400,2000,2800,4000,5600,8000,11000]
def _bp(x, fc, Q=2.0):
    w0 = 2*np.pi*fc/SR; al = np.sin(w0)/(2*Q); c = np.cos(w0)
    b0, b2 = al/(1+al), -al/(1+al); a1, a2 = (-2*c)/(1+al), (1-al)/(1+al)
    nfft = 1 << int(np.ceil(np.log2(len(x)+8)))
    X = np.fft.rfft(x, nfft); om = 2*np.pi*np.fft.rfftfreq(nfft); z = np.exp(-1j*om)
    H = (b0 + b2*z*z) / (1 + a1*z + a2*z*z)
    return np.fft.irfft(X*H, nfft)[:len(x)]
def _two_slope(eband, lo=-5.0):
    n_tail = int(0.3*SR); pn = eband[-n_tail:].mean()+1e-300; pk = eband[:int(0.05*SR)].max()+1e-300
    hi = -( (10*np.log10(pk/pn)) - 12.0); hi = min(-15.0, max(-35.0, hi))
    sch = np.cumsum(eband[::-1])[::-1]; sch /= sch[0]+1e-300; db = 10*np.log10(sch+1e-300)
    i0 = np.argmax(db <= lo); i1 = np.argmax(db <= hi)
    if i1 <= i0+20: return (np.nan, np.nan)
    step = max(1, int(0.002*SR)); d = db[i0:i1:step]; t = np.arange(len(d))*step/SR; n = len(d)
    if n < 20: return (np.nan, np.nan)
    best = None
    for k in range(max(3, int(0.12*n)), int(0.88*n)):
        A1 = np.vstack([t[:k], np.ones(k)]).T; A2 = np.vstack([t[k:], np.ones(n-k)]).T
        s1, _, _, _ = np.linalg.lstsq(A1, d[:k], rcond=None)
        s2, _, _, _ = np.linalg.lstsq(A2, d[k:], rcond=None)
        err = np.sum((A1@s1 - d[:k])**2) + np.sum((A2@s2 - d[k:])**2)
        if best is None or err < best[0]: best = (err, s1[0], s2[0])
    _, m1, m2 = best
    return (-60.0/m1 if m1 < 0 else np.nan, -60.0/m2 if m2 < 0 else np.nan)
DBANDS_FIT = [125, 250, 500, 1000, 2000, 4000]               # fewer bands during the fit = faster
def measure_decay(m, bands=None):
    on = np.argmax(np.abs(m) > 0.01*np.max(np.abs(m))); m = m[on:on+int(SR*5)]
    return {fc: _two_slope(_bp(m, fc)**2) for fc in (bands or DBANDS_FIT)}

def read_ir(p):
    if p.endswith('.f32'):
        a = np.fromfile(p, dtype='<f4').reshape(-1, 2)
    else:
        from wavutil import read_wav; a, _ = read_wav(p)
    return (a[:, 0] + a[:, 1]) / 2

# ---- target features (computed once) -------------------------------------------------
def edr_grid(x, nfft=4096, hop=1024, fmax=6000):              # lighter grid for speed
    pk = np.max(np.abs(x)) + 1e-12
    on = np.argmax(np.abs(x) > 0.02 * pk); x = x[on:]
    win = np.hanning(nfft); nfr = max(1, 1 + (len(x) - nfft) // hop)
    P = np.empty((nfft // 2 + 1, nfr), np.float32)
    for k in range(nfr): P[:, k] = np.abs(np.fft.rfft(x[k*hop:k*hop+nfft] * win)) ** 2
    E = np.cumsum(P[:, ::-1], axis=1)[:, ::-1]; E = E / (E[:, :1] + 1e-12)
    f = np.fft.rfftfreq(nfft, 1/SR); m = (f <= fmax) & (f >= 100)
    Edb = np.clip(10*np.log10(E[m] + 1e-12), -90, 0).astype(np.float32)   # CLAMP -> bounded loss
    return f[m], np.arange(nfr) * hop / SR, Edb

def echo_density(x, win_ms=20):
    # Abel-Huang normalized echo density profile (coarse): fraction of samples beyond 1 std
    on = np.argmax(np.abs(x) > 0.02 * np.max(np.abs(x))); x = x[on:on+SR]  # first 1s
    w = int(win_ms*1e-3*SR); ned = []
    for i in range(0, len(x)-w, w):
        seg = x[i:i+w]; s = np.std(seg) + 1e-12
        ned.append(np.mean(np.abs(seg) > s) / 0.3173)
    return np.array(ned)

def spectrum13(x):
    X = np.abs(np.fft.rfft(x)); f = np.fft.rfftfreq(len(x), 1/SR); P = X**2
    edges = 2**(np.arange(np.log2(80), np.log2(7000), 1/3)); out = []
    for a, b in zip(edges[:-1], edges[1:]):
        m = (f >= a) & (f < b); out.append(10*np.log10(P[m].sum() + 1e-12))
    out = np.array(out); return out - out.mean()

class Target:
    def __init__(self, path):
        m = read_ir(path)
        self.fe, self.te, self.Edb = edr_grid(m); self.valid = self.Edb > -55
        self.decay = measure_decay(m)                          # {fc:(early_T60, late_T60)}
        self.ned = echo_density(m); self.spec = spectrum13(m)
        self.bright, self.lowcut = C.spectral_edges(m)
        self.win = min(12.0, max(3.0, C.ir_len_s(m)))

# ---- render an FDN candidate ---------------------------------------------------------
def render(theta, tgt, N=16, dur=6.0):                        # FIT at N=16 + 6s (fast); FINAL at N=64 full
    t60 = np.clip(theta[:16], 0.1, 12.0)                       # per-band T60 target (the GEQ aims here)
    bright, lowcut, er = theta[16], theta[17], theta[18]
    DC60, gunit = design_gunit(t60)
    np.savetxt('jc_gunit.txt', gunit, fmt='%.8e')
    n = int(SR * dur)
    a = np.zeros((n, 2), '<f4'); a[10] = 1; a.tofile('jc_imp.f32')
    env = {'FDN_N':int(N),'FDN_SIZE':1.5,'FDN_GEQ_Q':0.70,'FDN_DC60':round(DC60,4),
           'FDN_BRIGHT':round(float(np.clip(bright,5000,9800)),1),'FDN_AIR':0,'FDN_AIRF':11000,
           'FDN_LOWCUT':round(float(np.clip(lowcut,40,400)),1),'FDN_DRV_LM':5.0,'FDN_DRV_LMF':220,
           'FDN_ER_MIX':round(float(np.clip(er,0,0.6)),3)}
    e = dict(os.environ); e.update({k:str(v) for k,v in env.items()}); e['FDN_GUNIT']='jc_gunit.txt'
    exe = 'fdnplate10.exe' if os.name == 'nt' else './fdnplate10'   # Windows vs Linux
    subprocess.run([exe, 'jc_imp.f32', 'jc_out.f32'], env=e, stderr=subprocess.DEVNULL, check=True)
    return read_ir('jc_out.f32')

# ---- joint loss ----------------------------------------------------------------------
LOSS_CAP = 150.0   # bounded: failed/bad candidates clamp here, keeps the float32 DE well-behaved
def joint_loss(theta, tgt, detail=False):
    try: y = render(theta, tgt).astype(np.float32)
    except Exception:
        return dict(loss=LOSS_CAP, edr_mae=0, decay_err=0, spec=0, ned=0) if detail else LOSS_CAP
    fo, to, Eo = edr_grid(y)
    nt = min(Eo.shape[1], tgt.Edb.shape[1]); v = tgt.valid[:, :nt]
    edr_mae = float(np.mean(np.abs((Eo[:, :nt] - tgt.Edb[:, :nt])[v])))
    dr = measure_decay(y); de = []                             # two-slope (convex) decay match
    for fc in dr:
        a = tgt.decay[fc]; b = dr[fc]
        for k in (0, 1):                                       # early & late T60 (clamped: tails extrapolate huge)
            if np.isfinite(a[k]) and np.isfinite(b[k]): de.append(min(abs(a[k]-b[k]), 6.0))
    decay_err = float(np.mean(de)) if de else 5.0
    spec_err = float(np.sqrt(np.mean((spectrum13(y) - tgt.spec)**2)))
    no = echo_density(y); m = min(len(no), len(tgt.ned))
    ned_err = float(np.mean(np.abs(no[:m] - tgt.ned[:m])))
    L = min(LOSS_CAP, 1.0*edr_mae + 4.0*decay_err + 0.6*spec_err + 3.0*ned_err)  # EDR + convex + tone + density
    if detail: return dict(loss=L, edr_mae=edr_mae, decay_err=decay_err, spec=spec_err, ned=ned_err)
    return float(L)

# ---- differential evolution (resumable) ----------------------------------------------
def main():
    # FAIL FAST if the engine binary isn't built (else every render silently returns the 150 cap)
    exe = 'fdnplate10.exe' if os.name == 'nt' else './fdnplate10'
    if not os.path.exists(exe):
        sys.exit(f"\nERROR: {exe} not found in {os.getcwd()}\nCompile the engine first:\n"
                 f"  clang++ -O2 fdnplate10.cpp -o fdnplate10.exe\n"
                 f"  (or in an 'x64 Native Tools' prompt:  cl /O2 /EHsc fdnplate10.cpp /Fe:fdnplate10.exe)\n")
    tgt = Target(sys.argv[1]); budget = int(sys.argv[2]) if len(sys.argv) > 2 else 600
    try:                                                       # sanity-render the seed; surface real engine errors
        _ = render(np.array([3.0]*16 + [7000, 100, 0.3], np.float32), tgt)
    except Exception as ex:
        sys.exit(f"\nERROR: {exe} exists but failed to run: {ex}\nTry running it once by hand: {exe} jc_imp.f32 jc_out.f32\n")
    MAX_GEN = int(sys.argv[3]) if len(sys.argv) > 3 else 80    # CPU-light cap (your i5)
    D = 19; F32 = np.float32
    lo = np.array([0.2]*16 + [5000, 40, 0.0], F32)
    hi = np.array([12.0]*16 + [9800, 400, 0.6], F32)
    # seed band T60s from the target's late-slope (a good starting point)
    seed = np.array([np.nan_to_num(tgt.decay[min(tgt.decay, key=lambda b:abs(b-fc))][1], nan=3.0) for fc in FC], F32)
    CK = 'joint_ckpt.npz'; rng = np.random.default_rng(0); NP = 14   # CPU-light population (i5)
    if os.path.exists(CK):
        z = np.load(CK); pop = z['pop'].astype(F32); fit = z['fit'].astype(F32); best = z['best'].astype(F32); bf = float(z['bf'])
        print(f"resumed (best loss {bf:.2f})", flush=True)
    else:
        pop = (lo + rng.random((NP, D)).astype(F32) * (hi - lo)).astype(F32)
        pop[0, :16] = np.clip(seed, lo[:16], hi[:16]); pop[0, 16:] = [tgt.bright, tgt.lowcut, 0.3]
        fit = np.array([joint_loss(p, tgt) for p in pop], F32); best = pop[fit.argmin()].copy(); bf = float(fit.min())
        print(f"seeded. start loss {bf:.2f}", flush=True)
    t0 = time.time(); gen = 0
    while gen < MAX_GEN and time.time() - t0 < budget:
        for i in range(NP):
            a, b, c = pop[rng.choice(NP, 3, replace=False)]
            tr = np.clip(a + 0.7*(b - c), lo, hi)
            cr = rng.random(D) < 0.9; cand = np.where(cr, tr, pop[i])
            cf = joint_loss(cand, tgt)
            if cf < fit[i]:
                pop[i] = cand; fit[i] = cf
                if cf < bf: best, bf = cand.copy(), cf
        gen += 1; d = joint_loss(best, tgt, detail=True)
        print(f"gen{gen:3d} loss={bf:.2f}  edr={d['edr_mae']:.1f}dB decay={d['decay_err']:.2f}s spec={d['spec']:.1f} ned={d['ned']:.2f}", flush=True)
        np.savez(CK, pop=pop, fit=fit, best=best, bf=bf)
    json.dump({'theta': best.tolist(), 'loss': bf}, open('joint_best.json', 'w'), indent=2)
    y = render(best, tgt, N=64, dur=14.0)                      # FINAL: full quality + length
    np.stack([y, y], 1).astype('<f4').tofile('joint_best.f32')
    print("DONE -> joint_best.json + joint_best.f32 (re-run to resume)", flush=True)

if __name__ == '__main__':
    main()
