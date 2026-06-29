#!/usr/bin/env python3
# Derive the Dallas Rangemaster Treble Booster small-signal (clean / pre-clip)
# transfer function from the schematic, then fit our digital PRE-clip voicing
# (one-pole low-cut * optional RBJ mid-peak * optional top-LP) to it. The boost
# companion to ts808_response.py / proco_rat_response.py. Component values:
# ElectroSmash "Dallas Rangemaster Treble Booster" analysis.
#
# Topology note (why this is the SIMPLEST of the three fits):
#   One PNP common-emitter germanium stage (OC44). The emitter resistor R3 is
#   bypassed by a big 47uF cap, so the midband voltage gain is fixed
#   Gv = gm*Rc ~= 0.008 * 10k ~= 80 (38 dB) at full Volume. The ONLY audio-band
#   shaping is the series input cap C1 forming a high-pass with the (low) input
#   impedance of the stage:
#       Zin = (R1 || R2) || rpi = 470k || 68k || 12.5k ~= 12k
#       C1 = 5nF  ->  fc = 1/(2*pi*C1*Zin) = 1/(2*pi*5n*12k) ~= 2.65 kHz
#   The two other coupling caps put their corners sub-audio:
#       C2 = 10nF into ~1M load -> 15.9 Hz ;  C3 = 47uF over R3 -> 0.8 Hz.
#   So the clean voicing is a single 1st-order high-pass, FLAT above ~2.6 kHz
#   (no resonant peak, no audio-band top roll). It is a *treble* booster: the
#   output stays bright; the "warmth" is the asymmetric germanium soft-clip
#   compressing the boosted treble transients, NOT a low-pass.
#
#   Because the high-pass sits PRE-clip (input cap, before the gain) and the gain
#   is broadband, the lows reach the clipper attenuated -> bass clips LEAST, the
#   full-gain treble clips most. We model that directly: pre-shaper low-cut
#   (midPost 0), static (shapeTrack 0, the cap network is fixed), no de-emphasis.
#
#   Input-cap MOD (our Range switch): swapping C1 moves the corner. Standard set
#   5/10/15nF (subtle) or 5/47/100nF (wide). We ship 5/10/47nF (Treble/Mid/Full):
#       5nF -> 2.65 kHz ;  10nF -> 1.33 kHz ;  47nF -> 282 Hz.
import numpy as np

FS = 48000.0
TwoPi = 2.0 * np.pi

# ---- Rangemaster small-signal high-pass (input cap + input impedance) ----
ZIN = 12.0e3       # (R1||R2||rpi) = 470k||68k||12.5k ~= 12k  (ElectroSmash)
C1_STD = 5.0e-9    # standard input cap


def H_rm(f, C1=C1_STD):
    w = TwoPi * f
    jw = 1j * w
    # series cap C1 into the stage input impedance ZIN -> 1st-order high-pass.
    Hin = (jw * ZIN * C1) / (1.0 + jw * ZIN * C1)
    return Hin


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


def _norm_db(mag, fg, fref=5000.0):
    db = 20 * np.log10(np.abs(mag))
    return db - db[np.argmin(np.abs(fg - fref))]   # normalise in the flat band


def fit():
    fg = np.geomspace(40, 12000, 200)
    tgt = _norm_db(H_rm(fg), fg)

    def curve(p):
        return _norm_db(H_ours(fg, *p), fg)

    def err(p):
        return np.mean((curve(p) - tgt) ** 2)

    # only the low-cut corner matters (no peak / no LP expected -> stay 0)
    best = (2650.0, 700.0, 0.7, 0.0, 0.0)
    be = err(best)
    for v in np.linspace(1500.0, 4000.0, 251):
        p = (v, 700.0, 0.7, 0.0, 0.0)
        if err(p) < be:
            be, best = err(p), p
    return best, np.sqrt(be)


if __name__ == "__main__":
    fs = np.array([50, 100, 200, 300, 500, 700, 1000, 1500, 2000, 2650, 3000, 5000, 8000])
    tg = _norm_db(H_rm(fs), fs)
    print("Rangemaster target (dB re 5 kHz, standard 5nF):")
    print(" " + " ".join(f"{int(x):>6}" for x in fs))
    print(" " + " ".join(f"{v:>+6.1f}" for v in tg))

    p, rms = fit()
    print(f"\nbest-fit voicing  lowCut={p[0]:.0f} (midDb={p[3]:.0f} lpHz={p[4]:.0f} = none)"
          f"  RMS {rms:.2f} dB")
    fdb = _norm_db(H_ours(fs, *p), fs)
    print(" ours:  " + " ".join(f"{v:>+6.1f}" for v in fdb))

    print("\nInput-cap MOD corners  fc = 1/(2*pi*C1*Zin),  Zin=12k:")
    for c, lab in [(5e-9, "Treble (5nF, stock)"), (10e-9, "Mid (10nF)"),
                   (15e-9, "15nF"), (47e-9, "Full (47nF)"), (100e-9, "100nF")]:
        fc = 1.0 / (TwoPi * c * ZIN)
        print(f"  C1={c*1e9:>5.0f}nF  {lab:>20}: fc = {fc:7.0f} Hz")

    print("\nVoltage gain (Volume maxed): Gv = gm*Rc = 0.008*10k = 80 (38 dB)")
    print("  -> ship gMax ~= 80 (the real Gv). gMin = a clean-ish min boost.")
