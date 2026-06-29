#!/usr/bin/env python3
"""Marshall Bluesbreaker (early-'90s, the King of Tone / Timmy ancestor) small-signal
frequency response, derived from the schematic, and a fit of Breaker Drive's voicing
filters to it. No physical pedal required.

Circuit blocks (AionFX "Cerulean" analysis, ElectroSmash-style derivation):

    Adjustable gain stage & filter (IC1A non-inv + IC1B inverting clip) -> tone -> vol

  * IC1A  non-inverting boost+filter.  Gain = 1 + Zf1/Zg1
        Zf1 = x*Pdrive            (drive pot in the feedback leg, x = 0..1)
        Zg1 = R3 || (R2 + 1/jwC3) (R3 4k7 full-range leg; the R2 3k3 + C3 path
                                   "provides a slightly higher gain for mids/highs
                                   only" -> a gentle HF emphasis shelf)
  * IC1B  inverting soft-clip stage.  |Gain| = Zf2/Zin2
        Zf2 = R5 || 1/jwC2        (220k fb, C2 47p -> ~15 kHz top roll)
        Zin2 = R4 + (1-x)*Pdrive  (10k + the OTHER side of the drive pot)
        The 100k pot is a divider: as resistance is ADDED to IC1A's feedback it is
        REMOVED from IC1B's input -> both stages gain together (the "interactive
        dual gain" Marshall control). x=0 -> A1~1, A2~2 (clean, full-range);
        x=1 -> A1~22, A2~22 (max gain + the HF emphasis fully bloomed).
  * Input HPF  C1 10n / R1 1M  ->  ~16 Hz (OPEN low end; far less bass cut than a TS).
  * Tone: passive treble rolloff (bass fixed) -> handled by the engine treble shelf
        (clip-3 soft-poly path), noon ~ flat, so the fit targets the gain-stage shape.

Because the BB's coloration BLOOMS with the Drive pot (the emphasis is in the gain
loop), we fit the SHAPE at max drive (its fullest bloom) and ship shapeTrack 1 so it
scales back toward flat/open at low Drive -- the real "clean till you push it" BB feel.

Run:  python3 bluesbreaker_response.py
See:  docs/drive/bluesbreaker.md
"""
import numpy as np

SR = 48000.0

# ---- Bluesbreaker stock ("standard"/Korean V2) component values (AionFX BOM) ----
R1, C1 = 1.0e6, 10e-9       # input HPF -> ~16 Hz (open low end)
R2, R3 = 3.3e3, 4.7e3       # IC1A ground leg: R3 full-range, R2 = HF-emphasis leg
C3     = 10e-9              # C3 in series with R2 (the mid/high gain lift)
R4     = 10e3              # IC1B input series R (in series with the drive-pot leg)
R5, C2 = 220e3, 47e-12      # IC1B feedback 220k + 47p soft top roll (~15 kHz)
PDRIVE = 100e3             # Drive pot (100kB), split between the two stages


def H_bb(f, x=1.0):
    """Clean (diodes open) small-signal response at drive-pot position x (0..1)."""
    w = 2j * np.pi * f
    Hin = (w * R1 * C1) / (1 + w * R1 * C1)                 # input HPF ~16 Hz
    Zg1 = 1.0 / (1.0 / R3 + 1.0 / (R2 + 1.0 / (w * C3)))    # R3 || (R2 + 1/jwC3)
    A1 = 1.0 + (x * PDRIVE) / Zg1                            # IC1A non-inverting
    Zf2 = 1.0 / (1.0 / R5 + w * C2)                         # 220k || 47p
    Zin2 = R4 + (1.0 - x) * PDRIVE
    A2 = Zf2 / Zin2                                          # IC1B inverting magnitude
    return Hin * A1 * A2


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


def fit(xfit=0.5, fhi=6000.0):
    # Fit the audible band (the gentle bright tilt is what we voice; the continued
    # rise above ~6 kHz is trimmed by the Tone treble-shelf + feedback-cap roll, so
    # we don't chase it with the bell). noon ~ max for the BB, so fit noon.
    fg = np.geomspace(40, fhi, 160)
    tgt = _norm_db(np.array([H_bb(f, xfit) for f in fg]), fg)

    def curve(p):
        return _norm_db(np.array([H_ours(f, *p) for f in fg]), fg)

    def err(p):
        return np.mean((curve(p) - tgt) ** 2)

    best = (20, 2000, 0.5, 3.0, 6000)
    be = err(best)
    grids = [np.linspace(10, 120, 30), np.linspace(1200, 4000, 30),
             np.linspace(0.25, 0.9, 28), np.linspace(1.0, 7.0, 30),
             np.linspace(3000, 16000, 34)]
    for _ in range(10):
        for i, grid in enumerate(grids):
            for v in grid:
                p = list(best); p[i] = v
                if err(p) < be:
                    be, best = err(p), tuple(p)
    return best, np.sqrt(be)


if __name__ == "__main__":
    fs = np.array([50, 100, 200, 300, 500, 720, 1000, 1500, 2000, 3000, 5000, 7000])
    print("Bluesbreaker small-signal (dB re 200 Hz):")
    print(" " + " ".join(f"{int(x):>5}" for x in fs))
    for x, lbl in [(0.0, "min "), (0.5, "noon"), (1.0, "max ")]:
        tg = _norm_db(np.array([H_bb(f, x) for f in fs]), fs)
        print(f" {lbl} " + " ".join(f"{v:>+5.1f}" for v in tg))

    p, rms = fit(0.5, 6000.0)
    print(f"\nbest-fit voicing (to NOON shape over 40-6000 Hz, shapeTrack 1):")
    print(f"  lowCut={p[0]:.0f} midHz={p[1]:.0f} midQ={p[2]:.2f} "
          f"midDb={p[3]:.1f} lpHz={p[4]:.0f}   (RMS {rms:.2f} dB)")
    # show the fitted curve vs target at the print frequencies
    tg = _norm_db(np.array([H_bb(f, 0.5) for f in fs]), fs)
    oc = _norm_db(np.array([H_ours(f, *p) for f in fs]), fs)
    print("  target: " + " ".join(f"{v:>+5.1f}" for v in tg))
    print("  ours:   " + " ".join(f"{v:>+5.1f}" for v in oc))
