#!/usr/bin/env python3
# Dallas-Arbiter Fuzz Face (germanium AC128) small-signal voicing, for "Round
# Fuzz II" (Fuzz model 1). Fifth worked run of the building-drives-playbook.
# Source: ElectroSmash "Fuzz Face Analysis".
#
# The Fuzz Face EQ is deliberately minimal -- "it just removes some bass and keeps
# all the highs." Two cascaded high-passes, NO low-pass (bright, raspy):
#   C1 (2.2uF) into the very low input impedance (~5k, the pickup-loading that
#      gives the volume-knob cleanup):  fc = 1/(2*pi*5k*2.2u)  ~= 14 Hz
#   C3 (10nF) into the Volume pot (500k at full):  fc = 1/(2*pi*500k*10n) ~= 31 Hz
#      (goes UP as Volume drops -> brighter/thinner at low volume; EJ uses 100k -> 160 Hz)
# The 20uF emitter-bypass corner is ~8 Hz (out of band). So the clean voicing is
# just a gentle bass trim below ~30 Hz, flat and bright above -- the character is
# the heavily ASYMMETRIC germanium clipping (Q1 biased cold at ~-1.6V, not the
# -4.5V centre) + the input-impedance touch/volume cleanup, NOT an EQ.
#
# We model it with a single one-pole low-cut fit to the cascade (the audible bass
# trim), no mid peak, no top roll. The clipping/dynamics/gate carry the identity.
import numpy as np

FS = 48000.0
TwoPi = 2.0 * np.pi
Zin, C1 = 5.0e3, 2.2e-6      # input HP ~14 Hz
Rvol, C3 = 500e3, 10e-9      # output HP ~31 Hz (Volume at full)


def H_ff(f):
    w = TwoPi * f; jw = 1j * w
    Hin = (jw * Zin * C1) / (1.0 + jw * Zin * C1)
    Hout = (jw * Rvol * C3) / (1.0 + jw * Rvol * C3)
    return Hin * Hout


def _onepole_lp(f, fc):
    a = 1.0 - np.exp(-TwoPi * fc / FS)
    z = np.exp(-1j * TwoPi * f / FS)
    return a / (1.0 - (1.0 - a) * z)


def _hp(f, fc):
    return 1.0 - _onepole_lp(f, fc)


def _norm_db(mag, fg, fref=1000.0):
    db = 20 * np.log10(np.abs(mag))
    return db - db[np.argmin(np.abs(fg - fref))]


def fit():
    fg = np.geomspace(20, 12000, 200)
    tgt = _norm_db(H_ff(fg), fg)

    def err(fc):
        return np.mean((_norm_db(_hp(fg, fc), fg) - tgt) ** 2)

    best, be = 45.0, None
    be = err(best)
    for fc in np.linspace(20, 120, 201):
        if err(fc) < be:
            be, best = err(fc), fc
    return best, np.sqrt(be)


if __name__ == "__main__":
    fs = np.array([30, 50, 80, 100, 150, 200, 400, 1000, 3000, 6000, 10000])
    tg = _norm_db(H_ff(fs), fs)
    print("Fuzz Face target (dB re 1 kHz) -- bass trim, flat & bright above:")
    print(" " + " ".join(f"{int(x):>6}" for x in fs))
    print(" " + " ".join(f"{v:>+6.1f}" for v in tg))
    fc, rms = fit()
    print(f"\nbest-fit one-pole low-cut = {fc:.0f} Hz (no mid, no top roll)  RMS {rms:.2f} dB")
    print(" ours: " + " ".join(f"{v:>+6.1f}" for v in _norm_db(_hp(fs, fc), fs)))
    print("\nClipping (the identity, NOT in this curve): heavily asymmetric germanium")
    print("  soft->hard with level (Q1 cold-biased), touch/volume cleanup (low Zin),")
    print("  bias-starved gate/splat on decay. Modeled in the engine, not the EQ.")
