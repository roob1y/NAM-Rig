# delay_battery.py — full metric battery for the NAM Rig delay CHARACTERS:
# our render (.f32 stereo, from delay_render.cpp) vs a measured reference (.wav).
# Analogue of reverb_analysis/reverb_battery.py. Saves overlaid graphs.
#
# usage:
#   python3 delay_battery.py --char tape  --ours-dir /tmp/ours --ref-dir ../delay_ref --out ../outputs
#   python3 delay_battery.py --char space --ours-dir /tmp/ours --ref-dir ../delay_ref --out ../outputs
#
# It loads, per character, the four/ five renders (impulse, tail, levels, sustain;
# + heads for space) and the matching delay_ref/*.wav, computes the §5 metrics,
# prints ours/ref/Δ tables and saves a graph sheet.
#
# READING THE VERDICT (full detail in DELAY_CHARACTER_PLAYBOOK.md):
#   1. SINGLE-REPEAT spectrum (one isolated echo) = the tonal fingerprint: the
#      low-mid head bump + the gap-loss HF roll. Match its SHAPE (norm @1k).
#   2. PER-PASS transfer = rep[n+1]/rep[n] (from the tail) = THE in-loop metric.
#      It CANCELS the output-once stage and exposes what darkens/blooms down the
#      tail. Most important and most often gotten wrong. A single-repeat fit alone
#      gives the wrong tail -> always fit the in-loop transfer from a MULTI-repeat
#      capture.
#   3. OUTPUT tilt = single-repeat MINUS per-pass = the once-at-output stage
#      (Tape thins lows -6@480 + bright shelf; Space boosts lows +2.5@180).
#   4. GAP-LOSS slope: Tape is 2-pole (~12 dB/oct), Space is 1-pole (~6 dB/oct,
#      brighter/gentler). Read the slope in the roll-off region, not just the corner.
#   5. SATURATION: output vs input level (knee) from the level sweep.
#   6. WOW/FLUTTER: instantaneous pitch of a sustained tone -> depth % + rate,
#      slow wow (~0.5-1 Hz) vs flutter (~6 Hz). Target ~0.1 % peak (subtle).
#      (Caveat: a non-pure dry test tone inflates this; drive a clean carrier.)
#   6b. SAT HARMONICS (even vs odd): the harmonic SERIES of the hottest level burst.
#      Real tape is EVEN-dominant (2nd harmonic, warm, asymmetric record transfer);
#      a symmetric soft-clipper is ODD-dominant (3rd, hollow). This is the dimension
#      the saturation CURVE (#5) and the magnitude spectra (#1-3) are all blind to:
#      a symmetric clipper can match every one of those panels and still sound wrong.
#      Found by a null test (delay_analysis/null_probe.py); regression-locked here.
#   7. SPACE TAPE head taps land at 1 : 1.9 : 2.76.
#   8. Loudness-match BEFORE any A/B; ears are the final judge -- the metrics
#      localise problems, they don't certify a match.
import numpy as np, argparse, os, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'reverb_analysis'))
from wavutil import read_wav

SR = 48000.0
# 1/6-octave centres, 40 Hz .. 16 kHz (the delay tonal fingerprint band)
FC = 40.0 * 2.0 ** (np.arange(0, 53) / 6.0)
FC = FC[FC <= 16000.0]

# ---------- loaders ----------
def load_ours(p):
    a = np.fromfile(p, dtype='<f4').reshape(-1, 2); return a[:, 0]
def load_ref(p):
    a, _ = read_wav(p); return a[:, 0]

# ---------- echo-train detection ----------
def smooth_env(x, w=240):
    return np.convolve(np.abs(x), np.ones(w) / w, 'same')

def has_echo_train(x):
    """A usable capture has energy well AFTER the first onset. Returns (ok, msg)."""
    env = smooth_env(x)
    pk = env.max() + 1e-20
    on = int(np.argmax(env > 0.05 * pk))
    # energy after first 150 ms past onset, vs the peak
    tail = x[on + int(0.15 * SR):]
    if len(tail) == 0: return False, "no tail"
    tailpk = np.max(np.abs(tail)) / (np.max(np.abs(x)) + 1e-20)
    return (tailpk > 0.02), f"post-onset peak {20*np.log10(tailpk+1e-12):.0f} dB rel"

def echo_period(x, on, tmin=0.04, tmax=1.2):
    """Delay period (s) from the FFT autocorrelation of the post-onset signal."""
    seg = x[on:on + int(3.0 * SR)].astype(np.float64)
    n = len(seg)
    if n < 32: return 0.35
    nfft = 1 << int(np.ceil(np.log2(2 * n)))
    X = np.fft.rfft(seg, nfft)
    ac = np.fft.irfft(X * np.conj(X), nfft)[:n]
    lo, hi = int(tmin * SR), min(int(tmax * SR), n - 1)
    k = lo + int(np.argmax(ac[lo:hi]))
    return k / SR

def echo_onsets(x, n=8):
    env = smooth_env(x)
    on = int(np.argmax(env > 0.05 * (env.max() + 1e-20)))
    T = echo_period(x, on)
    return on, T, [on + int(round(i * T * SR)) for i in range(n)]

# ---------- spectra ----------
def octband(seg, fcs=FC):
    seg = seg * np.hanning(len(seg))
    X = np.abs(np.fft.rfft(seg)) ** 2
    f = np.fft.rfftfreq(len(seg), 1 / SR)
    out = []
    for c in fcs:
        lo, hi = c / 2 ** (1 / 12), c * 2 ** (1 / 12)
        s = (f >= lo) & (f < hi)
        out.append(10 * np.log10(np.mean(X[s]) + 1e-30) if np.any(s) else np.nan)
    return np.array(out)

def norm_at(curve, fcs, f0=1000.0):
    i = int(np.argmin(np.abs(fcs - f0)))
    return curve - curve[i]

def single_repeat_spectrum(x):
    on, T, ons = echo_onsets(x)
    win = int(max(min(T * 0.9, 0.30), 0.12) * SR)  # >=120 ms so low bands are resolved
    seg = x[ons[0]:ons[0] + win]
    return norm_at(octband(seg), FC), T

def per_pass_transfer(x, npairs=6, clean_floor=0.04, floor_db=70.0):
    """rep[n+1]/rep[n] band ratio (dB) -> the in-loop EQ. Gated PER BAND by a noise
    floor: a band only counts in a pair where BOTH repeats clear (pair-peak − floor_db).
    Without this, the quiet later repeats' HF sits at the capture noise floor and the
    ratio there goes to ~0 -> a FALSE plateau that makes the reference's HF look like it
    'carries on' while ours rolls off. The true in-loop HF roll is steep; only the loud
    early repeats measure it honestly (low bands keep more pairs for SNR). Mind: this is
    the in-loop transfer, NOT the single-repeat -- see the playbook."""
    on, T, ons = echo_onsets(x, n=npairs + 2)
    win = int(min(T * 0.9, 0.30) * SR)
    specs, first = [], None
    for o in ons:
        if o + win >= len(x): break
        seg = x[o:o + win]
        pk = np.max(np.abs(seg))
        if first is None: first = pk
        if pk < clean_floor * first or pk < 1e-5: break
        specs.append(octband(seg))
    if len(specs) < 2: return np.full_like(FC, np.nan), T, 0
    out = np.full_like(FC, np.nan)
    for b in range(len(FC)):
        vals = []
        for i in range(len(specs) - 1):
            a, c = specs[i][b], specs[i + 1][b]
            pkref = max(np.nanmax(specs[i]), np.nanmax(specs[i + 1]))
            if a > pkref - floor_db and c > pkref - floor_db:
                vals.append(c - a)
        if vals: out[b] = np.mean(vals)
    return out, T, len(specs) - 1

# ---------- feature fits ----------
def head_bump_fit(curve_db, fcs=FC, band=(120, 800)):
    s = (fcs >= band[0]) & (fcs <= band[1])
    if not np.any(s): return np.nan, np.nan
    i = np.where(s)[0][np.argmax(curve_db[s])]
    return fcs[i], curve_db[i]

def gap_loss(curve_db, fcs=FC):
    """-3 dB HF corner (rel passband ~500-1500 Hz) + slope dB/oct above it."""
    pb = np.nanmean(curve_db[(fcs >= 500) & (fcs <= 1500)])
    hf = fcs >= 1500
    below = np.where(hf & (curve_db <= pb - 3.0))[0]
    corner = fcs[below[0]] if len(below) else np.nan
    # slope across the octave above the corner
    slope = np.nan
    if not np.isnan(corner):
        f1, f2 = corner, min(corner * 2, fcs[-1])
        i1 = int(np.argmin(np.abs(fcs - f1))); i2 = int(np.argmin(np.abs(fcs - f2)))
        if i2 > i1: slope = (curve_db[i2] - curve_db[i1]) / np.log2(f2 / f1)
    return corner, slope

# ---------- saturation ----------
def burst_levels(x, nsteps=5):
    env = smooth_env(x, w=480)
    thr = 0.08 * env.max()
    hot = env > thr
    # segment into runs
    runs, i = [], 0
    while i < len(hot):
        if hot[i]:
            j = i
            while j < len(hot) and hot[j]: j += 1
            if j - i > int(0.05 * SR): runs.append((i, j))
            i = j
        else: i += 1
    rms = [np.sqrt(np.mean(x[a:b] ** 2)) for a, b in runs]
    return np.array(rms[:nsteps])  # first nsteps bursts (ignore trailing echo runs)

# ---------- wow / flutter ----------
def hilbert_inst_freq(seg, f0=440.0):
    n = len(seg)
    Xf = np.fft.rfft(seg)
    h = np.zeros(len(Xf)); h[0] = 1
    if n % 2 == 0: h[1:-1] = 2; h[-1] = 1
    else: h[1:] = 2
    analytic = np.fft.irfft(Xf * h, n)
    # need complex analytic; build via fft of full length
    Xfull = np.fft.fft(seg); H = np.zeros(n)
    if n % 2 == 0: H[0] = H[n // 2] = 1; H[1:n // 2] = 2
    else: H[0] = 1; H[1:(n + 1) // 2] = 2
    z = np.fft.ifft(Xfull * H)
    phase = np.unwrap(np.angle(z))
    inst = np.diff(phase) / (2 * np.pi) * SR
    return inst

def wow_flutter(x):
    """depth % (peak) + dominant rate of slow (wow) and fast (flutter) pitch wobble."""
    on, T, ons = echo_onsets(x)
    seg = x[ons[0]:]
    seg = seg[:int(2.5 * SR)] if len(seg) > int(2.5 * SR) else seg
    if len(seg) < int(0.3 * SR): return dict(depth=np.nan, wow=np.nan, flut=np.nan)
    # tight FFT bandpass around the 440 Hz carrier FIRST — drops the saturation
    # harmonics and interpolation noise floor that otherwise inflate the apparent
    # pitch wobble (a non-monochromatic signal has a jittery instantaneous freq).
    Xb = np.fft.rfft(seg); frb = np.fft.rfftfreq(len(seg), 1 / SR)
    Xb[(frb < 380) | (frb > 500)] = 0
    seg = np.fft.irfft(Xb, len(seg))
    inst = hilbert_inst_freq(seg)
    # keep where the carrier is present (amp gate)
    a = np.abs(seg[1:])
    g = a > 0.2 * a.max()
    inst = inst[g]
    if len(inst) < 100: return dict(depth=np.nan, wow=np.nan, flut=np.nan)
    inst = inst[(inst > 300) & (inst < 600)]
    if len(inst) < 100: return dict(depth=np.nan, wow=np.nan, flut=np.nan)
    f0 = np.median(inst)
    dev = (inst - f0) / f0 * 100.0  # percent
    depth = np.percentile(np.abs(dev), 99)
    # rate spectrum of the deviation
    D = np.abs(np.fft.rfft((dev - dev.mean()) * np.hanning(len(dev))))
    fr = np.fft.rfftfreq(len(dev), 1 / SR)
    def peak_in(lo, hi):
        s = (fr >= lo) & (fr <= hi)
        return fr[s][np.argmax(D[s])] if np.any(s) and np.max(D[s]) > 0 else np.nan
    return dict(depth=depth, wow=peak_in(0.2, 2.0), flut=peak_in(4.0, 9.0), f0=f0)

# ---------- saturation harmonic character (even vs odd) ----------
def harmonic_profile(x, f0=None):
    """Even-vs-odd harmonic character of the in-loop saturation, from the HOTTEST level
    burst. THE dimension the level-domain saturation curve (burst-RMS growth) and the
    magnitude spectra are both BLIND to: they see compression amount and tone, but not
    whether the distortion is 2nd-harmonic (EVEN -> warm, asymmetric tape) or 3rd (ODD
    -> hollow, a symmetric soft-clipper). A symmetric clipper nulls the magnitude/sat
    panels yet sounds wrong because its repeats are odd-distorted. Returns dB rel f0."""
    e = smooth_env(x, 480); thr = 0.08 * e.max()
    hot = e > thr; runs = []; i = 0
    while i < len(hot):
        if hot[i]:
            j = i
            while j < len(hot) and hot[j]: j += 1
            if j - i > int(0.05 * SR): runs.append((i, j))
            i = j
        else: i += 1
    if not runs: return None
    a, b = runs[-1]
    seg = x[a + 1500:b - 1500] if (b - a) > 4000 else x[a:b]   # steady middle of the burst
    w = seg * np.hanning(len(seg)); X = np.abs(np.fft.rfft(w)); fr = np.fft.rfftfreq(len(seg), 1 / SR)
    if f0 is None:  # auto-detect the carrier (ref levels = 1 kHz; tolerate others)
        lo = (fr > 200) & (fr < 4000); f0 = fr[lo][np.argmax(X[lo])]
    def amp(fh):
        k = int(np.argmin(np.abs(fr - fh))); return X[max(0, k - 3):k + 4].max()
    base = amp(f0) + 1e-12
    H = {h: 20 * np.log10(amp(h * f0) / base + 1e-12) for h in range(2, 7)}
    evn = 10 * np.log10(sum(10 ** (H[h] / 10) for h in (2, 4, 6)) + 1e-30)
    odd = 10 * np.log10(sum(10 ** (H[h] / 10) for h in (3, 5)) + 1e-30)
    return dict(f0=f0, H=H, even=evn, odd=odd, margin=evn - odd)

# ---------- space-tape head taps ----------
def head_taps(x, nheads=3, window=2.5):
    # absolute envelope peaks in the first `window` s (>=8% of peak, >=15 ms apart).
    # NOT anchored to a 5% onset (that latched onto pre-echo filter smear and read a
    # spurious ~2 ms tap-1). Returns tap times (s) and ratios to the first tap.
    e = smooth_env(x, w=120)
    pk = e.max() + 1e-20
    seg = e[:int(window * SR)]
    thr = 0.08 * pk
    sep = int(0.015 * SR)
    peaks = []
    i = 1
    while i < len(seg) - 1 and len(peaks) < 8:
        if seg[i] > thr and seg[i] >= seg[i - 1] and seg[i] > seg[i + 1]:
            if not peaks or i - peaks[-1] >= sep: peaks.append(i)
            elif seg[i] > seg[peaks[-1]]: peaks[-1] = i
        i += 1
    taps = np.array(peaks[:nheads]) / SR
    ratios = taps / taps[0] if len(taps) else taps
    return taps, ratios

# ---------- main ----------
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--char', required=True, choices=['tape', 'space'])
    ap.add_argument('--ours-dir', required=True)
    ap.add_argument('--ref-dir', default=None)
    ap.add_argument('--out', default='.')
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    pre = 'tape' if a.char == 'tape' else 'space'
    od = a.ours_dir

    # Reference layout, auto-detected:
    #   NEW = <ref-dir>/<tape_echo|space_tape>/<test>.wav  (the WET captures)
    #   OLD = <ref-dir>/<ref|se>_<test>.wav                 (legacy delay_ref names)
    def refpath(test):
        if not a.ref_dir: return None
        sub = 'tape_echo' if a.char == 'tape' else 'space_tape'
        newp = os.path.join(a.ref_dir, sub, f'{test}.wav')
        if os.path.exists(newp): return newp
        rpre = 'ref' if a.char == 'tape' else 'se'
        name = 'ref_sustain.wav' if (test == 'sustain' and a.char == 'tape') else f'{rpre}_{test}.wav'
        oldp = os.path.join(a.ref_dir, name)
        return oldp if os.path.exists(oldp) else None

    sets = {
        'impulse': (f'{od}/{pre}_impulse.f32', refpath('impulse')),
        'tail':    (f'{od}/{pre}_tail.f32',    refpath('tail')),
        'levels':  (f'{od}/{pre}_levels.f32',  refpath('levels')),
        'sustain': (f'{od}/{pre}_sustain.f32', refpath('sustain') if a.char == 'tape' else None),
    }
    if a.char == 'space':
        sets['heads'] = (f'{od}/space_heads.f32', refpath('heads'))

    print(f"\n===== DELAY BATTERY — {a.char.upper()} — ours vs reference =====")

    # reference validity gate. Only the TAIL needs a multi-repeat train; a
    # single-repeat impulse (feedback all the way down) is exactly what the
    # single-repeat spectrum wants, so don't reject it for lacking a train.
    def usable(k, x):
        pk = float(np.max(np.abs(x)))
        if pk < 1e-4: return False, "silent — unusable"
        if k == 'tail':
            ok, msg = has_echo_train(x)
            return ok, ("USABLE " + msg if ok else "NO ECHO TRAIN — unusable")
        return True, f"USABLE (single repeat / tone, peak {pk:.3f})"
    ref_ok = {}
    if a.ref_dir:
        for k, (_, rp) in sets.items():
            if rp and os.path.exists(rp):
                ok, msg = usable(k, load_ref(rp))
                ref_ok[k] = ok
                print(f"  ref {os.path.basename(rp):14s} [{k:7s}]: {msg}")
        if not any(ref_ok.values()):
            print("  !! no usable references -> overlays skipped; ours profiled standalone.")

    def ours(k): return load_ours(sets[k][0])
    def ref(k):
        rp = sets[k][1]
        return load_ref(rp) if (rp and ref_ok.get(k)) else None

    # ---- single-repeat spectrum ----
    srO, T_o = single_repeat_spectrum(ours('impulse'))
    rx = ref('impulse'); srR = single_repeat_spectrum(rx)[0] if rx is not None else None
    bumpO = head_bump_fit(srO); gapO = gap_loss(srO)
    # ---- per-pass transfer ----
    ppO, Tp_o, np_o = per_pass_transfer(ours('tail'))
    rt = ref('tail'); ppR = per_pass_transfer(rt)[0] if rt is not None else None
    # the per-pass curve is negative everywhere (fb<1 decay); report the bump
    # RELATIVE to the flat passband (~1 kHz) so it reads as the true in-loop boost.
    ppN = norm_at(ppO, FC, 1000.0)
    ppbumpO = head_bump_fit(ppN); ppgapO = gap_loss(ppN)
    # ---- output tilt = single - per-pass (BOTH normalised @1k) ----
    # The per-pass MUST be normalised here: its raw level carries the per-repeat feedback
    # decay, which differs between ours and the reference (different fb), and would inject a
    # bogus offset into the tilt. Normalising both isolates the genuine once-at-output stage.
    tiltO = srO - ppN
    tiltR = (srR - norm_at(ppR, FC, 1000.0)) if (srR is not None and ppR is not None) else None
    # ---- saturation ----
    satO = burst_levels(ours('levels'))
    sx = ref('levels'); satR = burst_levels(sx) if sx is not None else None
    # ---- saturation HARMONIC character (even vs odd) -- the dimension the curve misses ----
    shO = harmonic_profile(ours('levels'))
    shR = harmonic_profile(sx) if sx is not None else None
    # ---- wow/flutter ----
    wfO = wow_flutter(ours('sustain'))
    wx = ref('sustain'); wfR = wow_flutter(wx) if wx is not None else None
    # ---- head taps (space) ----
    tapsO = ratO = None
    if a.char == 'space':
        tapsO, ratO = head_taps(ours('heads'))

    # ---------- print tables ----------
    print(f"\n single-repeat delay period: ours {T_o*1000:.0f} ms")
    print(f" HEAD BUMP  single-repeat: ours {bumpO[1]:+.1f} dB @ {bumpO[0]:.0f} Hz   per-pass: {ppbumpO[1]:+.1f} dB @ {ppbumpO[0]:.0f} Hz")
    print(f" GAP-LOSS   single-repeat: corner {gapO[0]:.0f} Hz slope {gapO[1]:.1f} dB/oct   per-pass: corner {ppgapO[0]:.0f} Hz slope {ppgapO[1]:.1f} dB/oct")
    print(f" PER-PASS pairs averaged: {np_o}")
    print(f" WOW/FLUTTER ours: depth {wfO['depth']:.2f}%  wow {wfO['wow']:.2f} Hz  flutter {wfO['flut']:.2f} Hz  (f0 {wfO.get('f0',float('nan')):.1f} Hz)")
    if wfR: print(f" WOW/FLUTTER ref:  depth {wfR['depth']:.2f}%  wow {wfR['wow']:.2f} Hz  flutter {wfR['flut']:.2f} Hz")
    print(f" SATURATION ours burst RMS steps: {np.array2string(satO, precision=3)}")
    if satR is not None: print(f" SATURATION ref  burst RMS steps: {np.array2string(satR, precision=3)}")
    if shO:
        print(f" SAT HARMONICS ours @{shO['f0']:.0f}Hz: " + " ".join(f"H{h}{shO['H'][h]:+.0f}" for h in range(2, 7))
              + f"  EVEN {shO['even']:+.0f} / ODD {shO['odd']:+.0f} dB  (even-odd margin {shO['margin']:+.0f} dB)")
    if shR:
        print(f" SAT HARMONICS ref  @{shR['f0']:.0f}Hz: " + " ".join(f"H{h}{shR['H'][h]:+.0f}" for h in range(2, 7))
              + f"  EVEN {shR['even']:+.0f} / ODD {shR['odd']:+.0f} dB  (even-odd margin {shR['margin']:+.0f} dB)")
        print(f"   -> tape is EVEN-dominant (warm); a symmetric clipper reads ODD-dominant here"
              f" while matching every magnitude/sat panel. Δmargin {shO['margin']-shR['margin']:+.0f} dB")
    if a.char == 'space':
        print(f" HEAD TAPS ours: {np.array2string(tapsO*1000, precision=1)} ms  ratios {np.array2string(ratO, precision=3)}  (target 1 : 1.9 : 2.76)")
        rh = ref('heads')
        if rh is not None:
            tR, rR = head_taps(rh); print(f" HEAD TAPS ref:  {np.array2string(tR*1000, precision=1)} ms  ratios {np.array2string(rR, precision=3)}")

    # ---------- graphs ----------
    import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
    npan = 6
    fig = plt.figure(figsize=(15, 9))
    fig.suptitle(f"Delay battery — {a.char.upper()} character — ours vs reference", fontsize=14, weight='bold')

    ax = plt.subplot(2, 3, 1)
    ax.semilogx(FC, srO, 's-', color='#c0392b', label='ours', ms=3)
    if srR is not None: ax.semilogx(FC, srR, 'o-', color='#222', label='reference', ms=3)
    ax.set_title('single-repeat spectrum (norm @1k)'); ax.set_ylabel('dB'); ax.set_ylim(-30, 12)
    ax.axvline(bumpO[0], color='#c0392b', ls=':', alpha=.4); ax.legend(); ax.grid(alpha=.3, which='both')

    ax = plt.subplot(2, 3, 2)
    ax.semilogx(FC, ppN, 's-', color='#c0392b', label='ours', ms=3)
    if ppR is not None: ax.semilogx(FC, norm_at(ppR, FC, 1000.0), 'o-', color='#222', label='reference', ms=3)
    ax.axhline(0, color='g', ls=':', alpha=.5)
    decay = np.nanmean(ppO[(FC >= 700) & (FC <= 1500)])
    ax.set_title(f'PER-PASS transfer rep[n+1]/rep[n] norm@1k (in-loop EQ; ~{decay:.1f}dB/pass)')
    ax.set_ylabel('dB'); ax.legend(); ax.grid(alpha=.3, which='both')

    ax = plt.subplot(2, 3, 3)
    ax.semilogx(FC, tiltO, 's-', color='#c0392b', label='ours', ms=3)
    if tiltR is not None: ax.semilogx(FC, tiltR, 'o-', color='#222', label='reference', ms=3)
    ax.axhline(0, color='g', ls=':', alpha=.5)
    ax.set_title('OUTPUT tilt = single − per-pass (once stage)'); ax.set_ylabel('dB'); ax.legend(); ax.grid(alpha=.3, which='both')

    ax = plt.subplot(2, 3, 4)
    steps = np.arange(1, len(satO) + 1)
    ax.plot(steps, 20 * np.log10(satO / satO[0] + 1e-12), 's-', color='#c0392b', label='ours')
    if satR is not None and len(satR):
        ax.plot(np.arange(1, len(satR) + 1), 20 * np.log10(satR / satR[0] + 1e-12), 'o-', color='#222', label='reference')
    ax.plot(steps, 20 * np.log10(2.0 ** (steps - 1)), 'g:', label='linear (×2/step)')
    ax.set_title('saturation: output growth vs input step'); ax.set_ylabel('dB rel step1'); ax.set_xlabel('step'); ax.legend(); ax.grid(alpha=.3)

    ax = plt.subplot(2, 3, 5)
    # wow/flutter pitch trace as % pitch DEVIATION (zoomed) so the subtle wobble is visible;
    # bandpass the carrier first (the saturation harmonics otherwise jitter the estimate),
    # and overlay the reference. depth is the 99th-pctile |dev|.
    def pitch_dev(x, f0=440.0, dur=2.0):
        on, T, ons = echo_onsets(x); seg = x[ons[0]:ons[0] + int(dur * SR)]
        if len(seg) < int(0.3 * SR): return None, None
        Xb = np.fft.rfft(seg); fr = np.fft.rfftfreq(len(seg), 1 / SR)
        Xb[(fr < f0 - 60) | (fr > f0 + 60)] = 0; seg = np.fft.irfft(Xb, len(seg))
        inst = hilbert_inst_freq(seg); aa = np.abs(seg[1:]); g = aa > 0.2 * aa.max()
        tt = np.arange(len(inst))[g] / SR; iv = inst[g]
        keep = (iv > f0 - 80) & (iv < f0 + 80); tt, iv = tt[keep], iv[keep]
        return tt, (iv / np.median(iv) - 1.0) * 100.0
    tO, dO = pitch_dev(ours('sustain'))
    if tO is not None: ax.plot(tO, dO, color='#c0392b', lw=.5, label=f"ours {wfO['depth']:.2f}%")
    wxs = ref('sustain')
    if wxs is not None:
        tR, dR = pitch_dev(wxs)
        if tR is not None: ax.plot(tR, dR, color='#222', lw=.5, alpha=.7,
                                   label=f"ref {wfR['depth']:.2f}%" if wfR else 'ref')
    ax.axhline(0, color='g', ls=':', alpha=.6)
    ax.set_ylim(-1.0, 1.0)  # ±1% -> subtle analogue wobble is readable
    ax.set_title('wow/flutter pitch deviation (%) — zoomed'); ax.set_ylabel('% dev'); ax.set_xlabel('s')
    ax.legend(fontsize=8); ax.grid(alpha=.3)

    ax = plt.subplot(2, 3, 6)
    if a.char == 'space' and tapsO is not None:
        ax.stem(tapsO * 1000, np.ones_like(tapsO), linefmt='#c0392b', markerfmt='rs', basefmt=' ')
        for tt, rr in zip(tapsO, ratO):
            ax.annotate(f"{rr:.2f}", (tt * 1000, 1.02), color='#c0392b', ha='center', fontsize=9)
        tgt = tapsO[0] * np.array([1, 1.9, 2.76]) * 1000
        ax.stem(tgt, 0.8 * np.ones(3), linefmt='g:', markerfmt='g^', basefmt=' ')
        ax.set_title('head taps (red=ours, green=target 1:1.9:2.76)'); ax.set_xlabel('ms'); ax.set_ylim(0, 1.2)
    elif shO is not None:
        # SATURATION HARMONIC CHARACTER: even (2nd, warm/tape) vs odd (3rd, hollow/clipper).
        # The headline metric the level-domain saturation curve + magnitude spectra MISS.
        hs = np.arange(2, 7); wbar = 0.38
        ax.bar(hs - wbar / 2, [shO['H'][h] for h in hs], wbar, color='#c0392b', label='ours')
        if shR is not None:
            ax.bar(hs + wbar / 2, [shR['H'][h] for h in hs], wbar, color='#222', label='reference')
        ax.set_title('SAT HARMONICS rel f0 (even=warm/tape, odd=hollow)\n'
                     f"ours margin {shO['margin']:+.0f} dB" + (f" / ref {shR['margin']:+.0f} dB" if shR else ""))
        ax.set_xlabel('harmonic'); ax.set_ylabel('dB rel f0'); ax.set_xticks(hs)
        ax.legend(); ax.grid(alpha=.3, axis='y')
    else:
        tl = ours('tail'); env = smooth_env(tl, 480)
        ax.plot(np.arange(len(env)) / SR, 20 * np.log10(env / env.max() + 1e-12), color='#c0392b', lw=.6)
        ax.set_title('tail decay envelope (ours)'); ax.set_ylabel('dB'); ax.set_xlabel('s'); ax.set_ylim(-80, 2); ax.grid(alpha=.3)

    plt.tight_layout(rect=[0, 0, 1, 0.96])
    p = os.path.join(a.out, f'delay_battery_{a.char}.png')
    plt.savefig(p, dpi=110); plt.close()
    print("saved:", p)

if __name__ == '__main__':
    main()
