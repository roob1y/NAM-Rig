#!/usr/bin/env python3
"""TS808 small-signal frequency response, derived from the schematic, and a fit
of Green Drive II's voicing filters to it. No physical pedal required.

Run:  python3 ts808_response.py
See:  docs/drive/circuit-accuracy.md
"""
import numpy as np

SR = 48000.0

# ---- TS808 component values (ElectroSmash BOM) ----
R4, C3 = 4.7e3, 0.047e-6     # Z1: input leg -> HF-boost corner ~720 Hz
R6, C4 = 51e3, 51e-12        # Z2: fixed feedback R + soft-corner cap
R5, C2 = 10e3, 1e-6          # non-inv input coupling HPF (~16 Hz)
R7, C5 = 1e3, 0.22e-6        # post-clip tone-stack pole (~723 Hz)


def H_ts(f, Rdrive=0.0):
    """Clean (diodes open) small-signal response of clip-amp + tone pole."""
    w = 2j * np.pi * f
    Z1 = R4 + 1 / (w * C3)
    Z2 = 1 / (1 / (R6 + Rdrive) + w * C4)
    A = 1 + Z2 / Z1                                  # non-inverting gain
    Hin = (w * R5 * C2) / (1 + w * R5 * C2)          # input HPF
    fp = 1 / (2 * np.pi * R7 * C5)
    Hlp = 1 / (1 + w / (2 * np.pi * fp))             # 723 Hz LP
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
    tgt = _norm_db(np.array([H_ts(f) for f in fg]), fg)

    def curve(p):
        return _norm_db(np.array([H_ours(f, *p) for f in fg]), fg)

    def err(p):
        return np.mean((curve(p) - tgt) ** 2)

    best, be = (60, 718, 0.62, 5.5, 1500), None
    be = err(best)
    grids = [np.linspace(20, 300, 30), np.linspace(640, 820, 25),
             np.linspace(0.4, 1.0, 25), np.linspace(3.5, 7.0, 25),
             np.linspace(900, 3500, 30)]
    for _ in range(6):
        for i, grid in enumerate(grids):
            for v in grid:
                p = list(best); p[i] = v
                if err(p) < be:
                    be, best = err(p), tuple(p)
    return best, np.sqrt(be)


if __name__ == "__main__":
    fs = [50, 100, 200, 300, 500, 720, 1000, 1500, 2000, 3000, 5000]
    tg = _norm_db(np.array([H_ts(f) for f in fs]), np.array(fs))
    print("TS808 target (dB re 200 Hz):")
    print(" " + " ".join(f"{int(x):>5}" for x in fs))
    print(" " + " ".join(f"{v:>+5.1f}" for v in tg))
    p, rms = fit()
    print(f"\nbest-fit voicing  lowCut={p[0]:.0f} midHz={p[1]:.0f} "
          f"midQ={p[2]:.2f} midDb={p[3]:.1f} lpHz={p[4]:.0f}  (RMS {rms:.2f} dB)")
