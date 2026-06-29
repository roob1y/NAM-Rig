#!/usr/bin/env python3
"""Klon Centaur voicing derivation + fit for "Gold Horse" (Overdrive model 3).

The Klon is NOT a Tube Screamer. Its op-amp gain stage is a band-pass that peaks
~1 kHz (40 dB max) and rolls off below and above (ElectroSmash). The clipped
signal (symmetric germanium 1N34A to GROUND, VF 0.35 -- a hard clip that only
bites at high gain; most distortion is the TL072 itself) is SUMMED with parallel
CLEAN feedforward paths in a high-headroom (27 V) summing stage. That clean+dirty
sum is the famous "transparent overdrive": a mid-humped, hard-clipped core under a
big, full-range clean foundation that restores the low end and dynamics.

Engine mapping (DriveBlock.h):
  * clip 1 (hard sym) + 2nd-order ADAA (adaa2) -- like Black Rodent II, clean.
  * pre-clip EQ = the ~1 kHz band-pass (so mids hit the diodes; bass clips least).
  * HEAVY clean blend taken from the RAW input (full-range) -> restores lows +
    dynamics = transparency. (Engine change: clean blend now works on the hard-clip
    path too; Black Rodent II has cleanBlend 0 so it is unaffected / byte-exact.)
  * modest gMin (genuinely clean at minimum -- the Klon clean-boost reputation),
    moderate gMax (~the real 40 dB), lots of output (it is also a boost).
  * tone = treble tilt around ~450 Hz (the Klon active treble shelf corner is
    ~408 Hz; the engine tilt approximates it -- bass mostly fixed near noon).

The gain-stage caps are gooped/"tricky values"; we fit our filters to the
PUBLISHED band-pass response (peak ~1 kHz) rather than guessed component values.

Refs: electrosmash.com/klon-centaur-analysis , coda-effects.com Klon analysis.
Run:  python3 klon_response.py
"""
import numpy as np

SR = 48000.0

# ---- Klon op-amp gain-stage band-pass (clipped path), published response ----
# Peak ~1 kHz, rolling off both sides. Modelled as a 1st-order HP x 1st-order LP
# (a gentle, broad band-pass -- the Klon hump is broader/softer than the TS).
HP_HZ, LP_HZ = 330.0, 3000.0   # geometric centre ~995 Hz


def H_klon_clip(f):
    w = 2j * np.pi * f
    hp = (w / (2 * np.pi * HP_HZ)) / (1 + w / (2 * np.pi * HP_HZ))
    lp = 1 / (1 + w / (2 * np.pi * LP_HZ))
    return hp * lp


# ---- our digital voicing blocks (match DriveBlock.h exactly, 48 kHz) ----
def _onepole_lp(f, fc):
    a = 1 - np.exp(-2 * np.pi * fc / SR); z = np.exp(-2j * np.pi * f / SR)
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
    return db - db[np.argmin(np.abs(fg - 1000))]   # normalise at the 1 kHz peak


def fit():
    fg = np.geomspace(40, 9000, 160)
    tgt = _norm_db(np.array([H_klon_clip(f) for f in fg]), fg)

    def curve(p):
        return _norm_db(np.array([H_ours(f, *p) for f in fg]), fg)

    def err(p):
        return np.mean((curve(p) - tgt) ** 2)

    best = (150, 1000, 0.5, 4.0, 3000); be = err(best)
    grids = [np.linspace(20, 400, 39), np.linspace(700, 1400, 36),
             np.linspace(0.3, 1.0, 29), np.linspace(1.0, 8.0, 36),
             np.linspace(1500, 6000, 46)]
    for _ in range(6):
        for i, grid in enumerate(grids):
            for v in grid:
                p = list(best); p[i] = v
                if err(p) < be:
                    be, best = err(p), tuple(p)
    return best, np.sqrt(be)


if __name__ == "__main__":
    fs = np.array([50, 100, 200, 400, 700, 1000, 1500, 2000, 3000, 5000])
    tg = _norm_db(np.array([H_klon_clip(f) for f in fs]), fs)
    print("Klon clipped-path band-pass (dB re 1 kHz):")
    print(" " + " ".join(f"{int(x):>5}" for x in fs))
    print(" " + " ".join(f"{v:>+5.1f}" for v in tg))

    p, rms = fit()
    print(f"\nbest-fit clipped EQ  lowCut={p[0]:.0f} midHz={p[1]:.0f} "
          f"midQ={p[2]:.2f} midDb={p[3]:.1f} lpHz={p[4]:.0f}  (RMS {rms:.2f} dB)")

    # The TRANSPARENT check: net = (1-b)*clippedEQ + b*flatClean. With a heavy
    # clean blend the deep response should flatten out (open, full low end).
    ours = _norm_db(np.array([H_ours(f, *p) for f in fs]), fs)
    for b in (0.0, 0.3, 0.5):
        lin = (1 - b) * 10 ** (ours / 20) + b * 1.0   # clean is flat (0 dB) full-range
        net = 20 * np.log10(lin); net -= net[np.argmin(np.abs(fs - 1000))]
        print(f"\nnet (clipEQ {int((1-b)*100)}% + clean {int(b*100)}%), dB re 1 kHz:")
        print(" " + " ".join(f"{v:>+5.1f}" for v in net))
