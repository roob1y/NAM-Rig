#!/usr/bin/env python3
# Maestro Echoplex EP-3 preamp + Xotic EP Booster voicing, for "EP Boost II"
# (Boost model 3). The fourth worked run of the building-drives-playbook after the
# TS808, ProCo RAT and Rangemaster. Sources: AionFX (Axion/Ares/Ephemeris tracing),
# effectslayouts/masterplex BOM.
#
# Topology: ONE JFET (TIS58 orig / 2N5457) COMMON-SOURCE stage. Canonical values:
#   Cin   = 22 nF  series into the gate
#   Rgate = 2.2 M  gate bias to ground (very high input impedance -> no tone-suck)
#   Cshunt= 220 pF gate-to-ground (a whisker of HF roll, well above audio)
#   Rd    = 22 k   drain resistor       Rs = 3.3 k  source resistor (UNBYPASSED)
#   Cout  = 100 nF into a 500 k Volume   (runs on internal 22 V for headroom)
#
#   Gain (source unbypassed): Av = Rd/(Rs + 1/gm) ~ 22k/(3.3k+~0.5k) ~ 5.8, loaded
#   by the 500k volume -> ~ +11 dB. A FIXED, high-headroom CLEAN boost.
#
#   THE KEY FINDING: the small-signal response of the pure EP-3 stage is ~FLAT
#   across the whole audio band -- Cin/Rgate HPF sits at ~3.3 Hz, the 220 pF roll is
#   >70 kHz, the source is unbypassed (no treble shelf). So the EP-3's "magic" is
#   NOT an EQ: it's clean headroom + the very high input Z (keeps the guitar's own
#   top that a lossy chain would lose) + subtle JFET 2nd-harmonic. It is FULL-RANGE
#   (the opposite of the Rangemaster's treble-only high-pass).
#
#   "EP Boost" as a pedal = the Xotic EP Booster, which adds gentle TONE-SHAPING on
#   top of the EP-3 stage: a broad presence/treble lift and a touch more low end.
#   We voice EP Boost II to THAT (full-range + a gentle high-shelf), since it is the
#   recognizable "EP Boost," and it stays distinct from Range '65 II. We fit our
#   low-Q pre-shaper peak to approximate the EP Booster's gentle high-shelf.
import numpy as np

FS = 48000.0
TwoPi = 2.0 * np.pi

# ---- canonical EP-3 JFET common-source small-signal (to show it's flat) ----
Cin, Rgate = 22e-9, 2.2e6
Cshunt, Rsrc = 220e-12, 10e3   # 220 pF sees the GUITAR source impedance (~10k), not Rgate
Rd, Rs = 22e3, 3.3e3
gm = 2.0e-3            # 2N5457-ish; only sets the gain scalar, not the SHAPE


def H_ep3(f):
    w = TwoPi * f; jw = 1j * w
    Hin = (jw * Rgate * Cin) / (1.0 + jw * Rgate * Cin)      # ~3.3 Hz HPF
    Hshunt = 1.0 / (1.0 + jw * Cshunt * Rsrc)               # ~72 kHz roll (inaudible)
    Av = (gm * Rd) / (1.0 + gm * Rs)                         # unbypassed: flat
    return Hin * Hshunt * Av


# ---- Xotic EP Booster voicing target: full-range + a gentle presence high-shelf ----
def H_target(f, shelf_db=4.0, fc=1800.0):
    w = TwoPi * f; jw = 1j * w
    Hin = (jw * Rgate * Cin) / (1.0 + jw * Rgate * Cin)      # full bass (same ~3.3 Hz)
    A = 10 ** (shelf_db / 20.0)
    Hshelf = (1.0 + jw * A / (TwoPi * fc)) / (1.0 + jw / (TwoPi * fc))  # 1st-order high-shelf
    return Hin * Hshelf


# ---- our digital voicing blocks (match DriveBlock.h exactly, 48 kHz) ----
def _onepole_lp(f, fc):
    a = 1.0 - np.exp(-TwoPi * fc / FS)
    z = np.exp(-1j * TwoPi * f / FS)
    return a / (1.0 - (1.0 - a) * z)


def _hp(f, fc):
    return 1.0 - _onepole_lp(f, fc)


def _rbj_peak(f, f0, Q, gdb):
    if gdb == 0.0:
        return np.ones_like(f, dtype=complex)
    A = 10 ** (gdb / 40.0); w = TwoPi * f0 / FS
    al = np.sin(w) / (2 * Q); cw = np.cos(w); a0 = 1 + al / A
    b0 = (1 + al * A) / a0; b1 = -2 * cw / a0; b2 = (1 - al * A) / a0
    a1 = -2 * cw / a0; a2 = (1 - al / A) / a0
    z = np.exp(-1j * TwoPi * f / FS)
    return (b0 + b1 * z + b2 * z * z) / (1 + a1 * z + a2 * z * z)


def H_ours(f, lowCut, midHz, midQ, midDb, lpHz):
    H = np.ones_like(f, dtype=complex)
    if lowCut > 0: H = H * _hp(f, lowCut)
    H = H * _rbj_peak(f, midHz, midQ, midDb)
    if lpHz > 0:   H = H * _onepole_lp(f, lpHz)
    return H


def _norm_db(mag, fg, fref=200.0):
    db = 20 * np.log10(np.abs(mag))
    return db - db[np.argmin(np.abs(fg - fref))]


def fit():
    fg = np.geomspace(40, 12000, 200)
    tgt = _norm_db(H_target(fg), fg)

    def err(p):
        return np.mean((_norm_db(H_ours(fg, *p), fg) - tgt) ** 2)

    best = (25.0, 3200.0, 0.5, 4.0, 0.0)
    be = err(best)
    grids = [np.linspace(15, 60, 20), np.linspace(2000, 5000, 30),
             np.linspace(0.35, 0.8, 20), np.linspace(2.5, 6.0, 25),
             np.array([0.0, 16000.0, 20000.0])]
    for _ in range(6):
        for i, grid in enumerate(grids):
            for v in grid:
                p = list(best); p[i] = v
                if err(p) < be:
                    be, best = err(p), tuple(p)
    return best, np.sqrt(be)


if __name__ == "__main__":
    fs = np.array([50, 80, 100, 200, 400, 800, 1500, 3000, 5000, 8000, 12000])
    flat = _norm_db(H_ep3(fs), fs)
    print("Pure EP-3 small-signal (dB re 200 Hz) -- essentially FLAT:")
    print(" " + " ".join(f"{int(x):>6}" for x in fs))
    print(" " + " ".join(f"{v:>+6.1f}" for v in flat))
    print(f"  pure-EP3 gain Av = {(gm*Rd)/(1+gm*Rs):.2f} = {20*np.log10((gm*Rd)/(1+gm*Rs)):+.1f} dB (before loading)")

    tg = _norm_db(H_target(fs), fs)
    print("\nXotic EP Booster target (full-range + gentle presence shelf):")
    print(" " + " ".join(f"{v:>+6.1f}" for v in tg))

    p, rms = fit()
    print(f"\nbest-fit  lowCut={p[0]:.0f} midHz={p[1]:.0f} midQ={p[2]:.2f} "
          f"midDb={p[3]:.1f} lpHz={p[4]:.0f}  (RMS {rms:.2f} dB)")
    print(" ours:  " + " ".join(f"{v:>+6.1f}" for v in _norm_db(H_ours(fs, *p), fs)))
