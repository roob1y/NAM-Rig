#!/usr/bin/env python3
# Derive the ProCo RAT clipper-amplifier small-signal (clean / diodes-open)
# transfer function from the schematic, then fit our digital PRE-clip voicing
# (one-pole low-cut * RBJ mid-peak * one-pole top-LP) to it. The RAT companion to
# ts808_response.py. Component values: ElectroSmash "Pro Co Rat" analysis.
#
# Topology note (why this differs from the TS fit):
#   The RAT clips with silicon diodes to GROUND *after* the LM308 gain stage (not
#   in the feedback like the TS). So the gain stage's frequency-selective response
#   IS the pre-clip EQ: whatever shape it makes is what the diodes hard-clip. Bass
#   gets little gain -> bass clips LEAST; the ~1 kHz hump clips hardest. We fit that
#   shape directly as the pre-clip EQ (midPost 0) and bloom it with Drive
#   (shapeTrack 1), because the gain (= the Distortion pot) scales the whole curve.
#
#   Gain stage (LM308, non-inverting):  Gv = 1 + Zf/Zg
#     Zf = Rdist || 1/(jwC4)        Rdist=0..100k   C4=100pF  (feedback LP ~16k @ max)
#     Zg = (R4 + 1/jwC5) || (R5 + 1/jwC6)
#                                   R4=47  C5=2.2u  -> 1539 Hz HP corner
#                                   R5=560 C6=4.7u  ->   60 Hz HP corner
#   Input HPF (coupling):           Hin = jwR2C1/(1+jwR2C1)   R2=1M C1=22nF (~7.2 Hz)
#   Op-amp finite GBW (LM308):      Gcl = Gv*Aol/(Aol+Gv),  Aol = GBW/(jw), GBW~1MHz
#                                   (the "op-amp collapsing under pressure" HF roll)
#   Voltage gain range:  Gv = 1 + Rdist/(R4||R5) = 1 (min) .. 1+100k/43.4 = 2305 (max)
#   Tone "Filter" (passive LP):     fc = 1/(2pi (Rtone+R7) C8), R7=1.5k C8=3.3nF
#                                   -> 32 kHz (CCW/bright) .. 475 Hz (CW/dark)
import numpy as np

FS = 48000.0
TwoPi = 2.0 * np.pi
R2, C1 = 1e6, 22e-9
R4, C5 = 47.0, 2.2e-6
R5, C6 = 560.0, 4.7e-6
C4 = 100e-12
GBW = 1.0e6

def gain_stage(f, Rdist):
    w = TwoPi * f; jw = 1j * w
    Hin = jw * R2 * C1 / (1.0 + jw * R2 * C1)
    Zf = Rdist / (1.0 + jw * Rdist * C4)
    Za = R4 + 1.0 / (jw * C5)
    Zb = R5 + 1.0 / (jw * C6)
    Zg = (Za * Zb) / (Za + Zb)
    Gv = 1.0 + Zf / Zg
    Aol = GBW / jw
    return Hin * (Gv * Aol / (Aol + Gv))

# ---- digital filter magnitudes (exactly what the C++ runs at 48 kHz) ----
def lp_c(f, fc):
    a = 1.0 - np.exp(-TwoPi * fc / FS)
    z = np.exp(-1j * TwoPi * f / FS)
    return a / (1.0 - (1.0 - a) * z)

def peak_mag(f, f0, Q, g):
    if g == 0: return np.ones_like(f)
    A = 10.0 ** (g / 40.0); w0 = TwoPi * f0 / FS
    al = np.sin(w0) / (2.0 * Q); cw = np.cos(w0); a0 = 1.0 + al / A
    b0 = (1.0 + al * A) / a0; b1 = (-2.0 * cw) / a0; b2 = (1.0 - al * A) / a0
    a1 = (-2.0 * cw) / a0; a2 = (1.0 - al / A) / a0
    w = TwoPi * f / FS; e1 = np.exp(-1j * w); e2 = np.exp(-2j * w)
    return np.abs((b0 + b1 * e1 + b2 * e2) / (1.0 + a1 * e1 + a2 * e2))

def vmag(f, lc, mh, mq, md, lp):
    m = np.ones_like(f)
    if lc > 0: m = m * np.abs(1.0 - lp_c(f, lc))
    if md != 0: m = m * peak_mag(f, mh, mq, md)
    if lp > 0: m = m * np.abs(lp_c(f, lp))
    return m

farr = np.array([50,80,100,150,200,300,500,700,1000,1500,2000,3000,5000,8000])
i1k = int(np.argmin(np.abs(farr - 1000)))

# ---- the gain-stage peak migrates DOWN as Distortion rises (the GBW collapse) ----
ffine = np.geomspace(40, 12000, 2000)
print("Gain-stage peak vs Distortion pot (the 'op-amp collapse' darkening):")
for rd in [2e3, 10e3, 15e3, 30e3, 100e3]:
    H = np.abs(gain_stage(ffine, rd)); i = int(np.argmax(H))
    print(f"  Rdist={rd/1e3:>5.0f}k  peak ~ {ffine[i]:6.0f} Hz   peakGain ~ {H[i]:6.0f}")

def rms_err(p, tgt):
    m = vmag(farr, *p); db = 20.0 * np.log10(m / m[i1k]); return np.sqrt(np.mean((db - tgt) ** 2))

def fit(Rdist):
    H = np.abs(gain_stage(farr, Rdist)); tgt = 20.0 * np.log10(H / H[i1k])
    p = [150.0, 1000.0, 0.7, 6.0, 4000.0]; st = [50.0, 300.0, 0.1, 1.0, 1000.0]
    for _ in range(8000):
        imp = False
        for i in range(5):
            for s in (st[i], -st[i]):
                q = list(p); q[i] = max(0.01, q[i] + s)
                if q[1] < 200: q[1] = 200
                if q[2] < 0.2: q[2] = 0.2
                if rms_err(q, tgt) < rms_err(p, tgt) - 1e-9: p = q; imp = True
        if not imp:
            st = [s * 0.5 for s in st]
            if max(st) < 1e-3: break
    return p, rms_err(p, tgt), tgt

# Fit at a representative Distortion setting whose peak is the canonical ~1 kHz RAT
# hump (ElectroSmash's published response = a moderate pot setting). shapeTrack=1
# then blooms it with Drive. Baking in the extreme max-gain 300 Hz collapse would
# make a muddy voicing; the recognizable RAT lives at the ~1 kHz hump.
RDIST_FIT = 12e3
p, e, tgt = fit(RDIST_FIT)
m = vmag(farr, *p); fdb = 20.0 * np.log10(m / m[i1k])
print(f"\nFIT (Rdist={RDIST_FIT/1e3:.0f}k):  lowCutHz={p[0]:.0f}  midHz={p[1]:.0f}  midQ={p[2]:.2f}  "
      f"midDb={p[3]:.1f}  lpHz={p[4]:.0f}   RMS={e:.2f} dB")
print("  -> shipped (rounded): lowCutHz=62 midHz=935 midQ=0.50 midDb=17.0 lpHz=4800")
print(" f(Hz): " + " ".join(f"{int(x):>5d}" for x in farr))
print(" target:" + " ".join(f"{d:>5.1f}" for d in tgt))
print(" ours:  " + " ".join(f"{d:>5.1f}" for d in fdb))

print("\nFilter tone (passive 1-pole LP): fc = 1/(2pi (Rtone+1.5k) 3.3nF)")
for rt, lab in [(0, "CCW/bright"), (50e3, "noon"), (100e3, "CW/dark")]:
    fc = 1.0 / (TwoPi * (rt + 1.5e3) * 3.3e-9)
    print(f"  Rtone={rt/1e3:>5.0f}k {lab:>10}: fc = {fc:8.0f} Hz")
