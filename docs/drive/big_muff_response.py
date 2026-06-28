#!/usr/bin/env python3
# Derive the EHX Big Muff Pi (Ram's Head '73) voicing from the schematic and fit
# our digital filters to it. Companion to ts808_response.py / proco_rat_response.py.
#
# The Muff is NOT a single shaper: it is FOUR cascaded common-emitter stages —
#   input booster -> CLIP stage 1 -> CLIP stage 2 -> (passive tone stack) -> recovery
# with each clip being a SOFT clip (silicon 1N914 back-to-back diodes in the
# transistor's collector->base FEEDBACK loop, ~+/-0.6 V). The two consecutive soft
# clips give the dense, compressed, square "wall" the single-shaper pedals can't.
# Component values: ElectroSmash "Big Muff Pi Analysis" (V3, extrapolated) +
# Coda Effects / Kit Rae Ram's Head '73 specifics (tone-stack 4.7 nF bass cap,
# silicon clipping, dark/bassy "Gilmour" voice).
#
# Two things to fit for our engine:
#   (A) The BAND-LIMITING around each clip (the Miller caps): the Muff's dark,
#       smooth, no-fizz voice comes from rolling the highs off BEFORE the final
#       clip (clipping a low-passed signal sounds smoother). Published poles:
#         input booster : HP ~3.8 Hz,  LP ~1.2 kHz (C1 / C10 Miller)
#         clip stage 1  : HP ~55 Hz,   LP ~1.78 kHz
#         clip stage 2  : HP ~94 Hz,   LP ~1.17 kHz
#       -> we model: a pre-clip HP (lowCutHz, the combined ~80 Hz tighten), a
#          Miller LP applied BEFORE each cubic clip (muffLpHz ~1.2-1.4 kHz), and a
#          gentle post LP (lpHz).
#   (B) The passive TONE-STACK mid SCOOP (the Muff signature). We derive the real
#       network response at the Tone-noon position by nodal analysis and fit a
#       single post-clip RBJ peaking notch (negative midDb) to the 1 kHz scoop.
import numpy as np

FS = 48000.0
TwoPi = 2.0 * np.pi

# ----------------------------------------------------------------------------
# (B) Big Muff passive tone stack — nodal analysis at the Tone pot position p.
#   Vin -[Rsrc]- S -[Ct]- T(treble lug) ----+
#                 \-[Rt]- B(bass lug) -[Cb]-GND
#   Pot 100k lin between T and B, wiper W (= Vout) loaded by Rload.
#   Ram's Head '73 values: Ct=10nF (treble), Rt=39k, Cb=4.7nF (bass; the cap the
#   "mids" switch swaps to 10nF), pot 100k. Source Z ~ clip2 collector R6=15k
#   (the "high output impedance driving it" that ElectroSmash notes rolls the
#   highs and deepens the dip). Load ~ recovery-stage input bias ~100k.
Rsrc = 15e3
Ct   = 10e-9
Rt   = 39e3
Cb   = 4.7e-9
Rpot = 100e3
Rload = 100e3

def tone_H(f, p):
    w = TwoPi * f; jw = 1j * w
    Ysrc = 1.0 / Rsrc
    Yct  = jw * Ct
    Yrt  = 1.0 / Rt
    Ycb  = jw * Cb
    Ypt  = 1.0 / max(p * Rpot, 1.0)          # T..W  (treble side of wiper)
    Ypb  = 1.0 / max((1.0 - p) * Rpot, 1.0)  # W..B  (bass side of wiper)
    Yld  = 1.0 / Rload
    H = np.zeros_like(f, dtype=complex)
    for i, _ in enumerate(f):
        Y = np.array([
            [Ysrc + Yct[i] + Yrt,        -Yct[i],              -Yrt,                 0.0      ],
            [-Yct[i],                     Yct[i] + Ypt,          0.0,                -Ypt      ],
            [-Yrt,                        0.0,                   Yrt + Ycb[i] + Ypb, -Ypb      ],
            [0.0,                        -Ypt,                  -Ypb,                 Ypt + Ypb + Yld],
        ], dtype=complex)
        b = np.array([Ysrc, 0.0, 0.0, 0.0], dtype=complex)  # Vin = 1
        v = np.linalg.solve(Y, b)
        H[i] = v[3]  # node W = wiper = Vout
    return H

# ----------------------------------------------------------------------------
# digital filter magnitudes (exactly what the C++ runs at 48 kHz)
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

farr = np.array([50,80,100,150,200,300,500,700,1000,1500,2000,3000,5000,8000], dtype=float)
i200 = int(np.argmin(np.abs(farr - 200)))

# ---- the tone-stack scoop at three Tone positions (the Muff "V") --------------
ffine = np.geomspace(40, 12000, 2000)
print("Big Muff Ram's Head tone stack (Vout/Vin), dB re 1 kHz, vs Tone pot:")
for p, lab in [(0.98, "treble (CW)"), (0.5, "noon"), (0.02, "bass (CCW)")]:
    H = np.abs(tone_H(ffine, p))
    i1k = int(np.argmin(np.abs(ffine - 1000)))
    db = 20*np.log10(H / H[i1k])
    # locate the notch (mid minimum between 300 Hz and 3 kHz)
    band = (ffine >= 300) & (ffine <= 3000)
    inmin = np.argmin(np.where(band, 20*np.log10(H), 1e9))
    print(f"  Tone={lab:>12}: notch ~ {ffine[inmin]:6.0f} Hz   "
          f"insertion@1k = {20*np.log10(H[i1k]):6.1f} dB")

# The nodal solve above confirms the scoop lives in the ~0.7-1.3 kHz region and is
# worth ~3-6 dB, but its exact interior-notch-vs-tilt depth is very sensitive to the
# (poorly-specified) source/load impedances. The GROUND TRUTH is ElectroSmash's
# MEASURED tone-noon response (their "green curve"): shelves sit at the ~7 dB overall
# insertion loss and the 1 kHz notch is ~6.5 dB BELOW the shelves (-13.5 dB absolute).
# Per the playbook, when a published MEASURED response exists we fit THAT. So our
# target (relative to the shelves; the 7 dB insertion loss is just makeup = outTrim):
#   a 1 kHz scoop, ~ -6.5 dB at the notch, ~symmetric shelves either side.
ES_NOTCH_HZ = 1000.0
ES_NOTCH_DB = -6.5   # ElectroSmash measured: notch 6.5 dB below the shelves
# Synthesise the measured-shape target: 0 dB shelves, a -6.5 dB scoop at 1 kHz.
target = np.zeros_like(farr)
# a moderately broad measured notch (their plot: ~half-depth from ~0.5 to ~2 kHz)
target = 20*np.log10(np.maximum(1e-3, 1.0 + (10**(ES_NOTCH_DB/20)-1.0) *
            np.exp(-0.5*((np.log(farr/ES_NOTCH_HZ))/0.62)**2)))

def notch_err(f0, Q, g):
    m = peak_mag(farr, f0, Q, g); db = 20*np.log10(m)
    sel = (farr >= 150) & (farr <= 4000)
    return np.sqrt(np.mean((db[sel] - target[sel])**2))

best = None
for f0 in np.arange(850, 1200, 10.0):
    for Q in np.arange(0.4, 1.4, 0.05):
        for g in np.arange(-8.0, -4.0, 0.1):
            e = notch_err(f0, Q, g)
            if best is None or e < best[0]: best = (e, f0, Q, g)
e, f0, Q, g = best
print("\nTarget = ElectroSmash MEASURED tone-noon scoop (dB re shelves):")
print(" f(Hz): " + " ".join(f"{int(x):>5d}" for x in farr))
print(" target:" + " ".join(f"{d:>5.1f}" for d in target))
print(f"\nFIT scoop notch:  midHz={f0:.0f}  midQ={Q:.2f}  midDb={g:.1f}   RMS={e:.2f} dB (150..4k)")
mfit = 20*np.log10(peak_mag(farr, f0, Q, g))
print(" ours:  " + " ".join(f"{d:>5.1f}" for d in mfit))

# ---- the clip-stage band-limiting corners (set directly from published poles) -
print("\nBand-limiting (Miller caps) — pre/inter/post around the cubic clips:")
print("  pre-clip HP (tighten lows)   lowCutHz ~ 80 Hz   (clip stages HP 55/94 Hz)")
print("  Miller LP before EACH clip   muffLpHz ~ 1300 Hz (booster 1.2k, clips 1.78k/1.17k)")
print("  gentle post LP               lpHz     ~ 1600 Hz (recovery ~flat; keep it dark)")
print("  -> shipped: clip=3 cubic x2 (muffStages=2), lowCutHz=80 muffLpHz=1300 lpHz=1600")
print("     scoop midHz=1000 midQ=0.80 midDb=-6.5 (post-clip, static; ElectroSmash measured)")
print("     Tone = the engine see-saw tilt @ pivot 1000 Hz (real Muff bass/treble blend)")
print("     bias=0 (symmetric back-to-back diode clipping); moderate gMin / high-ceiling gMax")
