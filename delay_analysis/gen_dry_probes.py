#!/usr/bin/env python3
# gen_dry_probes.py -- regenerate the CONTROLLED dry probe signals used to voice
# delay CHARACTERS (Tape Echo, Space Tape, and future ones e.g. BBD) by the
# controlled-probe method (delay_analysis/CONTROLLED_PROBE_METHOD.md).
#
# The probes are deliberately NOT committed as WAVs (they live in
# delay_references/dry_probes/, which is kept out of git like all delay_ref data),
# so THIS script is the source of truth -- run it to recreate them for a new
# character. The user runs them through a hardware/plugin reference at ONE fixed
# setting (100% wet) to make cap_low / cap_high captures; we null our engine
# against the IDENTICAL dry.
#
#   python3 delay_analysis/gen_dry_probes.py [out_dir]
#   default out_dir = delay_references/dry_probes
#
# Output (48 kHz mono float32 WAV):
#   dry_main.wav    -> run at LOWEST feedback  -> cap_low.wav
#                      5 s 440 Hz carrier (wow/flutter) | 3 s 1 kHz @0.30 (harmonics)
#                      | 18 stepped tones 31 Hz-8 kHz @0.12 (single-repeat EQ)
#                      | 7-step level sweep to 0.30 (saturation) | 1 ms click (impulse)
#   dry_perpass.wav -> run at HIGH feedback     -> cap_high.wav
#                      12 short 0.2 s bursts @0.05, ~octave-spaced 40 Hz-6 kHz, 2.7 s
#                      apart (SHORT + QUIET so nothing recirculates into a clip; the
#                      decaying train gives the per-pass / in-loop EQ)
#   dry_click.wav   -> multi-head only          -> cap_taps.wav (head tap ratios)
#
# Keep the SR / levels stable across regenerations; if the reference's operating
# level differs, scale the amplitudes here (and re-capture).

import os, sys, struct, math
import numpy as np

SR = 48000

def tone(freq, dur, amp, fade=0.005):
    n = int(round(dur * SR))
    t = np.arange(n) / SR
    x = amp * np.sin(2 * np.pi * freq * t)
    f = int(fade * SR)
    if f > 0 and n > 2 * f:                       # short raised-cosine fades (no clicks)
        w = 0.5 * (1 - np.cos(np.pi * np.arange(f) / f))
        x[:f] *= w; x[-f:] *= w[::-1]
    return x.astype(np.float64)

def silence(dur):
    return np.zeros(int(round(dur * SR)), dtype=np.float64)

def write_wav_f32_mono(path, x):
    x = np.asarray(x, dtype='<f4')
    data = x.tobytes()
    with open(path, 'wb') as f:
        f.write(b'RIFF'); f.write(struct.pack('<I', 36 + len(data))); f.write(b'WAVE')
        f.write(b'fmt '); f.write(struct.pack('<IHHIIHH', 16, 3, 1, SR, SR * 4, 4, 32))  # fmt=3 IEEE float
        f.write(b'data'); f.write(struct.pack('<I', len(data))); f.write(data)

def dry_main():
    segs = [silence(0.5)]
    segs += [tone(440.0, 5.0, 0.12)]              # pure carrier -> wow/flutter
    segs += [silence(1.0)]
    segs += [tone(1000.0, 3.0, 0.30)]             # operating-ceiling tone -> harmonic series
    segs += [silence(1.0)]
    # 18 stepped steady tones, log-spaced 31 Hz-8 kHz, 0.45 s @0.12, 1.0 s gaps
    eq_freqs = [31, 40, 55, 79, 112, 159, 224, 316, 449, 630,
                900, 1000, 1400, 2000, 2800, 4000, 5600, 8000]
    for fr in eq_freqs:
        segs += [tone(float(fr), 0.45, 0.12), silence(1.0)]
    segs += [silence(0.5)]
    # 7-step level sweep at 1 kHz -> saturation growth/compression
    for amp in [0.03, 0.05, 0.08, 0.12, 0.18, 0.25, 0.30]:
        segs += [tone(1000.0, 0.30, amp), silence(0.6)]
    segs += [silence(0.5)]
    click = np.zeros(int(0.001 * SR)); click[0] = 0.5  # 1 ms impulse -> delay time / phase
    segs += [click, silence(2.0)]
    return np.concatenate(segs)

def dry_perpass():
    # 12 short quiet bursts, ~octave-ish log spacing 40 Hz-6 kHz, 0.2 s @0.05, 2.7 s apart
    freqs = [40, 64, 98, 158, 250, 400, 632, 1000, 1600, 2500, 4000, 6000]
    segs = [silence(0.5)]
    for fr in freqs:
        segs += [tone(float(fr), 0.20, 0.05), silence(2.5)]
    return np.concatenate(segs)

def dry_click():
    # repeated clicks through an all-heads mode -> tap ratios
    segs = []
    for _ in range(4):
        c = np.zeros(int(0.001 * SR)); c[0] = 0.5
        segs += [c, silence(3.0)]
    return np.concatenate(segs)

def main():
    out = sys.argv[1] if len(sys.argv) > 1 else 'delay_references/dry_probes'
    os.makedirs(out, exist_ok=True)
    for name, sig in [('dry_main', dry_main()), ('dry_perpass', dry_perpass()), ('dry_click', dry_click())]:
        p = os.path.join(out, name + '.wav')
        write_wav_f32_mono(p, sig)
        print(f"wrote {p}  ({len(sig)/SR:.2f}s, peak {np.max(np.abs(sig)):.3f})")

if __name__ == '__main__':
    main()
