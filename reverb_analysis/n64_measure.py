#!/usr/bin/env python3
# N32 vs N64 vs reference: centroid trajectory + 2-5 kHz modal-line metric.
import sys, os
sys.path.insert(0, '.')
import numpy as np
import reverb_battery as rb
SR = rb.sr

def load_any(path):
    if path.endswith('.wav'):
        L, R = rb.load_ref(path)
    else:
        d = np.fromfile(path, dtype='<f4').reshape(-1, 2)
        L, R = d[:, 0], d[:, 1]
    m = (L + R) / 2.0
    o = rb.onset(m)
    return L[o:], R[o:], m[o:]

def running_centroid(m, win=512, hop=128, tmax=0.6):
    cents, times = [], []
    w = np.hanning(win)
    f = np.fft.rfftfreq(win, 1/SR)
    s = f > 40
    for i in range(0, int(tmax*SR) - win, hop):
        fr = np.abs(np.fft.rfft(m[i:i+win]*w))
        cents.append(np.sum(f[s]*fr[s])/(np.sum(fr[s])+1e-20))
        times.append(i/SR)
    return np.array(times), np.array(cents)

def cent_at(times, cents, ms):
    idx = np.searchsorted(times, ms/1000.0)
    idx = min(idx, len(cents)-1)
    return cents[idx]

def modal_25k(m, t0=0.20, t1=1.0):
    # fine magnitude spectrum of the late tail, restricted to 2-5 kHz.
    seg = m[int(t0*SR):int(t1*SR)]
    nfft = 1 << 17
    P = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), nfft))
    f = np.fft.rfftfreq(nfft, 1/SR)
    band = (f >= 2000) & (f <= 5000)
    Pb = P[band]
    PbdB = 20*np.log10(Pb + 1e-20)
    # smooth median (continuum level) over the band; peak prominence above it.
    med = np.median(PbdB)
    peak = np.max(PbdB)
    # "modal line" prominence: how far the strongest line stands over the band median (dB)
    prom = peak - med
    # also the 95th-pct over median = robust "how spiky overall"
    p95 = np.percentile(PbdB, 95) - med
    return prom, p95

labels = {'reference':'ir/vintage-plate-1.5s.wav', 'N32':'/tmp/n32.f32', 'N64':'/tmp/n64.f32'}
print(f"{'':10s} | {'cen@100':>8s} {'cen@200':>8s} {'cen@350':>8s} {'cen@500':>8s} | {'2-5k peak-med':>13s} {'2-5k p95-med':>12s}")
results = {}
for name, path in labels.items():
    if not os.path.exists(path):
        print(f"{name:10s} | (missing {path})"); continue
    L, R, m = load_any(path)
    t, c = running_centroid(m)
    c100, c200, c350, c500 = (cent_at(t,c,x) for x in (100,200,350,500))
    prom, p95 = modal_25k(m)
    results[name] = dict(traj=(c100,c200,c350,c500), t=t, c=c, prom=prom, p95=p95)
    print(f"{name:10s} | {c100:8.0f} {c200:8.0f} {c350:8.0f} {c500:8.0f} | {prom:13.1f} {p95:12.1f}")

np.save('/tmp/n64_results.npy', results, allow_pickle=True)
