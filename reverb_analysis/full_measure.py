"""EXHAUSTIVE IR measurement battery (numpy-only) — every domain, especially the
perceptual/temporal ones the energy-decay battery was blind to.

Categories:
  A energy-decay (context): EDT/T30 per band, C50/C80, D50, Ts, broadband T60
  B spectral: centroid/spread/skew/rolloff/flatness/crest/tilt
  C brightness-over-time: running centroid, spread, spectral flux
  D auditory/perceptual: ERB-band energies, Zwicker sharpness, loudness, per-ERB decay
  E temporal modulation: modulation spectrum (envelope FFT) per ERB band
  F phase/dispersion: group delay vs freq, phase linearity, onset HF/LF arrival
  G spatial: IACC broadband + per band, inter-channel coherence
  H texture/coloration: fine-spectrum roughness, flatness, comb index, modal density,
     envelope Rayleigh CV, autocorr periodicity, echo-density buildup time

Run from reverb_analysis/:  python3 full_measure.py
"""
import sys, os, subprocess
sys.path.insert(0, '.')
import numpy as np
import reverb_battery as rb
SR = rb.sr


# ---------- helpers ----------
def analytic(x):
    n = len(x); X = np.fft.fft(x); h = np.zeros(n); h[0] = 1
    if n % 2 == 0: h[n // 2] = 1; h[1:n // 2] = 2
    else: h[1:(n + 1) // 2] = 2
    return np.fft.ifft(X * h)


def hz2erb(f): return 21.4 * np.log10(4.37e-3 * f + 1)
def erb2hz(e): return (10 ** (e / 21.4) - 1) / 4.37e-3
def hz2bark(f): return 13 * np.arctan(0.00076 * f) + 3.5 * np.arctan((f / 7500.0) ** 2)


def erb_bands(lo=80, hi=16000, n=32):
    e = np.linspace(hz2erb(lo), hz2erb(hi), n)
    return erb2hz(e)


def fft_bandpass(x, f0, bw, nfft):
    X = np.fft.rfft(x, nfft); f = np.fft.rfftfreq(nfft, 1 / SR)
    g = np.exp(-0.5 * ((f - f0) / (bw / 2.0)) ** 2)  # gaussian ERB-ish
    return np.fft.irfft(X * g, nfft)[:len(x)]


# ---------- per-IR measurement ----------
def measure(c):
    L, R = c; m = (L + R) / 2
    on = rb.onset(m); m = m[on:]; L = L[on:]; R = R[on:]
    out = {}
    e = m ** 2; t = np.arange(len(m)) / SR
    Etot = np.sum(e) + 1e-20

    # ---- A energy/decay ----
    out['Ts_ms'] = np.sum(t * e) / Etot * 1000
    out['D50'] = np.sum(e[:int(0.05 * SR)]) / Etot
    out['C80_dB'] = 10 * np.log10(np.sum(e[:int(0.08 * SR)]) / (np.sum(e[int(0.08 * SR):]) + 1e-20))

    # ---- B spectral (2s tail window) ----
    W = min(len(m), int(2.0 * SR)); seg = m[:W] * np.hanning(W)
    X = np.abs(np.fft.rfft(seg)); f = np.fft.rfftfreq(W, 1 / SR); P = X ** 2
    sel = f > 40
    cen = np.sum(f[sel] * X[sel]) / (np.sum(X[sel]) + 1e-20)
    out['centroid_Hz'] = cen
    out['spread_Hz'] = np.sqrt(np.sum(((f[sel] - cen) ** 2) * X[sel]) / (np.sum(X[sel]) + 1e-20))
    cum = np.cumsum(P[sel]); out['rolloff85_Hz'] = f[sel][np.searchsorted(cum, 0.85 * cum[-1])]
    bandsel = (f > 500) & (f < 10000)
    out['flatness'] = np.exp(np.mean(np.log(P[bandsel] + 1e-20))) / (np.mean(P[bandsel]) + 1e-20)
    out['crest'] = np.max(np.abs(m)) / (np.sqrt(np.mean(m ** 2)) + 1e-20)

    # ---- C brightness over time (STFT centroid) ----
    win = 1024; hop = 256; cents = []; times = []
    wnd = np.hanning(win)
    for i in range(0, min(len(m), int(1.0 * SR)) - win, hop):
        fr = np.abs(np.fft.rfft(m[i:i + win] * wnd)); ff = np.fft.rfftfreq(win, 1 / SR)
        s = ff > 40; cents.append(np.sum(ff[s] * fr[s]) / (np.sum(fr[s]) + 1e-20)); times.append(i / SR)
    cents = np.array(cents); times = np.array(times)
    out['cen_t0_Hz'] = cents[0] if len(cents) else np.nan          # onset brightness
    out['cen_300ms_Hz'] = cents[np.searchsorted(times, 0.3)] if len(cents) else np.nan
    out['cen_slope_Hz_s'] = np.polyfit(times[:int(0.5 / (hop / SR))], cents[:int(0.5 / (hop / SR))], 1)[0] if len(cents) > 5 else np.nan
    out['_cents'] = cents; out['_ctimes'] = times

    # ---- D auditory: ERB energies, Zwicker sharpness, loudness ----
    fc = erb_bands(80, 15000, 28); nfft = 1 << int(np.ceil(np.log2(W + 8)))
    erb_e = np.array([np.mean(fft_bandpass(seg, f0, max(50, 0.18 * f0), nfft) ** 2) for f0 in fc])
    Nspec = (erb_e + 1e-20) ** 0.23      # specific loudness (compressive exponent ~0.23)
    z = hz2bark(fc)
    gz = np.where(z < 16, 1.0, 0.066 * np.exp(0.171 * z))   # Zwicker sharpness weighting
    out['sharpness_acum'] = 0.11 * np.sum(Nspec * gz * z) / (np.sum(Nspec) + 1e-20)
    out['loudness_rel'] = np.sum(Nspec)
    out['_erb_fc'] = fc; out['_erb_e'] = erb_e
    # per-ERB decay (EDT-ish, 0..-10 over the band-filtered signal), a few bands
    def band_edt(f0):
        b = fft_bandpass(m, f0, max(50, 0.18 * f0), nfft); en = np.abs(analytic(b))
        sch = rb.schroeder_db(b); idx = np.where(sch <= -10)[0]
        return 6.0 * idx[0] / SR if len(idx) else np.nan
    out['_erb_edt'] = np.array([band_edt(f0) for f0 in fc])

    # ---- E temporal modulation spectrum (envelope FFT per ERB band, averaged) ----
    modf = None; modspec = []
    for f0 in fc:
        b = fft_bandpass(m[:int(1.5 * SR)], f0, max(50, 0.18 * f0), nfft)
        env = np.abs(analytic(b)); env = env - np.mean(env)
        E = np.abs(np.fft.rfft(env * np.hanning(len(env))))
        mf = np.fft.rfftfreq(len(env), 1 / SR); modspec.append(E); modf = mf
    modspec = np.mean(np.array(modspec), axis=0)
    # power in modulation-frequency bands (perceptual roughness/clarity region)
    def modpow(a, b): s = (modf >= a) & (modf < b); return np.sum(modspec[s] ** 2)
    tot = modpow(1, 200) + 1e-20
    out['mod_lo_2-8Hz'] = modpow(2, 8) / tot
    out['mod_mid_8-30Hz'] = modpow(8, 30) / tot
    out['mod_hi_30-120Hz'] = modpow(30, 120) / tot   # high mod = fine texture / roughness
    out['_modf'] = modf; out['_modspec'] = modspec

    # ---- F phase / dispersion ----
    Wd = min(len(m), int(0.3 * SR)); xd = m[:Wd]
    Xf = np.fft.rfft(xd); ph = np.unwrap(np.angle(Xf)); ff = np.fft.rfftfreq(Wd, 1 / SR)
    gd = -np.gradient(ph, 2 * np.pi * (ff[1] - ff[0]))   # group delay (s)
    bs = (ff > 200) & (ff < 12000)
    out['groupdelay_std_ms'] = np.std(gd[bs]) * 1000     # dispersion spread
    # onset HF vs LF arrival (dispersion direction): time of energy peak in low vs high band
    def t_peak(f0, bw):
        b = fft_bandpass(m[:int(0.1 * SR)], f0, bw, 1 << 15); en = np.abs(analytic(b))
        return np.argmax(en) / SR * 1000
    out['arr_LF_ms'] = t_peak(400, 200); out['arr_HF_ms'] = t_peak(9000, 3000)
    out['disp_HF-LF_ms'] = out['arr_HF_ms'] - out['arr_LF_ms']

    # ---- G spatial ----
    n1 = min(len(L), int(2.0 * SR)); ll = L[:n1]; rr = R[:n1]
    def iacc(a, b):
        a = a - np.mean(a); b = b - np.mean(b)
        cc = np.correlate(a, b, 'full'); cc /= (np.sqrt(np.sum(a ** 2) * np.sum(b ** 2)) + 1e-20)
        mid = len(a) - 1; w = int(0.001 * SR); return np.max(np.abs(cc[mid - w:mid + w]))
    out['IACC'] = iacc(ll, rr)
    nfftI = 1 << int(np.ceil(np.log2(n1)))
    out['IACC_lowband'] = iacc(fft_bandpass(ll, 300, 200, nfftI), fft_bandpass(rr, 300, 200, nfftI))
    out['IACC_hiband'] = iacc(fft_bandpass(ll, 8000, 3000, nfftI), fft_bandpass(rr, 8000, 3000, nfftI))

    # ---- H texture / coloration ----
    Xc = 20 * np.log10(np.abs(np.fft.rfft(seg)) + 1e-9)
    Xs = np.convolve(Xc, np.ones(151) / 151, mode='same')
    cb = (f > 500) & (f < 8000)
    out['coloration_dB'] = np.std((Xc - Xs)[cb])
    # echo density buildup time
    def ed90():
        w = int(0.010 * SR)
        for i in range(0, int(0.2 * SR), 64):
            sg = m[i:i + w]; s = np.std(sg) + 1e-20
            if np.mean(np.abs(sg) > s) / 0.3173 >= 0.9: return i / SR * 1000
        return 99.0
    out['echo_dense_ms'] = ed90()
    # autocorr periodicity (metallic)
    tail = m[int(0.1 * SR):int(0.6 * SR)]; ac = np.correlate(tail, tail, 'full')[len(tail) - 1:]; ac /= ac[0] + 1e-20
    out['metallic'] = np.max(np.abs(ac[int(0.001 * SR):int(0.03 * SR)]))
    # modal density (peaks/kHz, 1-6k)
    Xm = np.abs(np.fft.rfft(m[:int(1.0 * SR)], 1 << 18)); fm = np.fft.rfftfreq(1 << 18, 1 / SR)
    s = (fm > 1000) & (fm < 6000); xs = Xm[s]
    out['modal_pk_per_kHz'] = np.sum((xs[1:-1] > xs[:-2]) & (xs[1:-1] > xs[2:])) / 5.0
    out['modal_depth'] = rb.modal_depth(m)
    return out


def ir_render(cmd, cwd, env):
    out = f"/tmp/fm_{abs(hash((cmd, tuple(sorted(env.items())))))%99999}.f32"
    subprocess.run([cmd, "plate", os.path.abspath("impulse.f32"), out], cwd=cwd,
                   env={**os.environ, "RV_T60": "2.45", **env}, check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return rb.load_ours(out)


if __name__ == "__main__":
    cands = [("reference", rb.load_ref("ir/vintage-plate-1.5s.wav")),
             ("OLD", ir_render("./render_character", ".", {})),
             ("NEW", rb.load_ours("/tmp/gl_ir.f32"))]
    res = {n: measure(c) for n, c in cands}
    scalar_keys = [k for k in res['reference'] if not k.startswith('_')]
    print(f"{'metric':22s}{'reference':>11s}{'OLD':>11s}{'NEW':>11s}{'|OLD-ref|':>11s}{'|NEW-ref|':>11s}")
    rows = []
    for k in scalar_keys:
        rv=res['reference'][k]; ov=res['OLD'][k]; nv=res['NEW'][k]
        od=abs(ov-rv)/(abs(rv)+1e-9); nd=abs(nv-rv)/(abs(rv)+1e-9)
        rows.append((nd, k, rv, ov, nv, od))
    for nd,k,rv,ov,nv,od in sorted(rows, reverse=True):
        flag = " <=WORSE" if nd>od+0.05 else (" *better" if nd<od-0.05 else "")
        print(f"{k:22s}{rv:>11.2f}{ov:>11.2f}{nv:>11.2f}{od:>10.0%}{nd:>10.0%}{flag}")
    np.save('/tmp/fm_res.npy', res, allow_pickle=True)
    print("\nsaved arrays for plotting -> /tmp/fm_res.npy")
