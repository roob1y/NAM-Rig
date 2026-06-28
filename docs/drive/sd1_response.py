#!/usr/bin/env python3
"""Boss SD-1 Super Overdrive small-signal frequency response, derived from the
schematic, plus a fit of Super Drive's voicing filters to it, and a sweep to
choose the asymmetric-cubic knee (clip type 4) for the 2:1 diode-threshold split.
No physical pedal required.

The SD-1 is an OD-1/TS-808-lineage feedback-clip overdrive (uPC4558 first half):
same ~720 Hz pre-clip HF-boost corner (Rg leg) and ~720 Hz post-stage low-pass as
the TS, so the CLEAN voicing is ~identical to the TS808 (a modest ~+5 dB mid hump).
The SD-1's identity is elsewhere: (1) ASYMMETRIC clipping (D4/D5/D6 = two diodes
one way, one the other -> a ~2:1 forward-drop ratio -> persistent even harmonics),
and (2) a LARGER feedback HF-limit cap so the drive stage rolls off harder when
cranked (~1.56 kHz at max gain vs the TS's ~5.6 kHz) = darker/smoother cranked.

Refs:
  electricdruid.net/designing-a-classic-overdrive  (OD-1/TS-808/SD-1 side by side)
  wamplerdiy.com  "A few easy Boss SD-1 ... Mods"   (stage-by-stage stock values)
  hobby-hour.com SD-1 schematic                     (3x 1S2473, 2+1 asymmetric)

Run:  python3 sd1_response.py
See:  docs/drive/sd1.md , docs/drive/circuit-accuracy.md
"""
import numpy as np

SR = 48000.0

# ---- SD-1 drive-stage values (Wampler DIY stage-by-stage + Electric Druid) ----
# Rg leg (resistor + cap to ground): the 720 Hz HF-boost corner. SD-1 = R6 1k /
#   C3 0.22uF (= 723 Hz); the TS uses 4.7k / 0.047uF for the SAME corner.
Rg, Cg = 1.0e3, 0.22e-6
# Feedback leg: fixed min-gain R (R5) + Drive pot (1M) in parallel with the HF
#   gain-limit cap (Cf). Stock R5 ~10k (the "better low gain" mod LOWERS it to
#   4.7k, so stock is higher) -> min gain 1+10k/1k = x11 (+20.8 dB), just under
#   the TS's ~x11.9 ("the TS is a little hotter at minimum" - Electric Druid).
#   SD-1's Cf is LARGER than the TS's 51 pF: ~100 pF lands the max-gain corner at
#   ~1.56 kHz (the documented Boss value); out of band at min drive (small-sig).
Rfix, Rpot_max, Cf = 10.0e3, 1.0e6, 100e-12
# Non-inverting input coupling HPF: ~7 Hz (input cap into the 470k input Z).
Rin, Cin = 470e3, 0.047e-6
# Post-stage tone-stack input low-pass pole: ~720 Hz (R7 1k / C4 0.22uF), as TS.
Rpost, Cpost = 1.0e3, 0.22e-6


def H_sd1(f, drive=0.0):
    """Clean (diodes open) small-signal response of clip-amp + tone pole.
    drive in 0..1 sets the pot resistance (only matters for the HF-limit cap)."""
    w = 2j * np.pi * f
    Z1 = Rg + 1 / (w * Cg)                               # Rg leg (720 Hz boost)
    Rf = Rfix + drive * Rpot_max
    Z2 = 1 / (1 / Rf + w * Cf)                           # feedback leg ∥ HF cap
    A = 1 + Z2 / Z1                                      # non-inverting gain
    Hin = (w * Rin * Cin) / (1 + w * Rin * Cin)          # input HPF (~7 Hz)
    fp = 1 / (2 * np.pi * Rpost * Cpost)
    Hlp = 1 / (1 + w / (2 * np.pi * fp))                 # ~720 Hz post LP
    return Hin * A * Hlp


# ---- our digital voicing blocks (match DriveBlock.h exactly, 48 kHz) ----
def _onepole_lp(f, fc):
    a = 1 - np.exp(-2 * np.pi * fc / SR)
    z = np.exp(-2j * np.pi * f / SR)
    return a / (1 - (1 - a) * z)


def _hp(f, fc):
    return 1 - _onepole_lp(f, fc)


def _rbj_peak(f, f0, Q, gdb):
    A = 10 ** (gdb / 40); w = 2 * np.pi * f0 / SR
    al = np.sin(w) / (2 * Q); cw = np.cos(w); a0 = 1 + al / A
    b0 = (1 + al * A) / a0; b1 = -2 * cw / a0; b2 = (1 - al * A) / a0
    a1 = -2 * cw / a0; a2 = (1 - al / A) / a0
    z = np.exp(-2j * np.pi * f / SR)
    return (b0 + b1 * z + b2 * z * z) / (1 + a1 * z + a2 * z * z)


def H_ours(f, lowCut, midHz, midQ, midDb, lpHz):
    H = _rbj_peak(f, midHz, midQ, midDb)
    if lowCut > 0: H = H * _hp(f, lowCut)
    if lpHz > 0:   H = H * _onepole_lp(f, lpHz)
    return H


def _norm_db(mag, fg):
    db = 20 * np.log10(np.abs(mag))
    return db - db[np.argmin(np.abs(fg - 200))]   # normalise at 200 Hz


def fit():
    fg = np.geomspace(40, 9000, 160)
    tgt = _norm_db(np.array([H_sd1(f) for f in fg]), fg)

    def curve(p):
        return _norm_db(np.array([H_ours(f, *p) for f in fg]), fg)

    def err(p):
        return np.mean((curve(p) - tgt) ** 2)

    best = (220, 818, 0.7, 4.0, 1900)
    be = err(best)
    grids = [np.linspace(20, 320, 31), np.linspace(640, 900, 27),
             np.linspace(0.4, 1.0, 25), np.linspace(2.5, 7.0, 31),
             np.linspace(900, 3500, 30)]
    for _ in range(6):
        for i, grid in enumerate(grids):
            for v in grid:
                p = list(best); p[i] = v
                if err(p) < be:
                    be, best = err(p), tuple(p)
    return best, np.sqrt(be)


# ---- asymmetric-cubic shaper (clip type 4 in DriveBlock.h) knee selection ----
# Positive knee at 1 (rail +2/3); negative knee at kn = 1-bias (rail -(2/3)kn).
# The real SD-1 diode thresholds are ~2:1 (two diodes vs one). We measure the
# even-harmonic content (h2/h1) vs bias and pick a MILD-but-clearly-asymmetric
# knee: well above the symmetric Green Drive II (~0), below the fuzz's bias 0.45.
def asymF(x, kn):
    x = np.asarray(x, float)
    pos = np.where(x > 1.0, 2.0 / 3.0, x - x ** 3 / 3.0)
    neg = np.where(x < -kn, -(2.0 / 3.0) * kn, x - x ** 3 / (3.0 * kn * kn))
    return np.where(x >= 0.0, pos, neg)


def harmonics(kn, drive_lin):
    n = 1 << 15
    t = np.arange(n) / SR
    f0 = 220.0
    x = drive_lin * np.sin(2 * np.pi * f0 * t)
    y = asymF(x, kn)
    win = np.hanning(n)
    Y = np.abs(np.fft.rfft(y * win))
    k = int(round(f0 * n / SR))
    h1 = Y[k]
    h2 = Y[2 * k]
    h3 = Y[3 * k]
    return h2 / h1, h3 / h1


if __name__ == "__main__":
    fs = [50, 100, 200, 300, 500, 720, 1000, 1500, 2000, 3000, 5000]
    tg = _norm_db(np.array([H_sd1(f) for f in fs]), np.array(fs))
    print("SD-1 small-signal target (dB re 200 Hz), min drive:")
    print(" " + " ".join(f"{int(x):>5}" for x in fs))
    print(" " + " ".join(f"{v:>+5.1f}" for v in tg))

    # show the drive-stage HF rolloff difference (cranked) vs the TS
    tg_hi = _norm_db(np.array([H_sd1(f, drive=1.0) for f in fs]), np.array(fs))
    print("\nSD-1 small-signal at MAX drive (HF-limit cap engaged):")
    print(" " + " ".join(f"{v:>+5.1f}" for v in tg_hi))

    p, rms = fit()
    print(f"\nbest-fit voicing  lowCut={p[0]:.0f} midHz={p[1]:.0f} "
          f"midQ={p[2]:.2f} midDb={p[3]:.1f} lpHz={p[4]:.0f}  (RMS {rms:.2f} dB)")

    print("\nasym-cubic knee sweep (h2/h1, h3/h1 at a moderate drive=3.0):")
    for bias in (0.0, 0.20, 0.30, 0.35, 0.45):
        kn = 1.0 - bias
        h2, h3 = harmonics(kn, 3.0)
        print(f"  bias={bias:.2f} kn={kn:.2f}:  h2/h1={h2:.3f}  h3/h1={h3:.3f}")
