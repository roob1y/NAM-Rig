# rangemaster_sat_fit.py — derives the Range '65 ADAA2 saturator (DriveBlock kSat*).
#
# WHY: the treble booster was the worst aliasing offender of all drive models —
# its 2.65 kHz input-cap high-pass leaves ONLY top-octave content, which the
# legacy 1st-order-ADAA tanh then clips at gains up to 80. tanh has no closed-form
# 2nd antiderivative (dilogarithm), so instead of oversampling (CPU) we FIT an odd
# 7th-order polynomial to tanh and get exact F1/F2 -> the same cheap Parker/Bilbao
# 2nd-order ADAA the cubic/hard clips already use.
#
# Fit: constrained weighted LSQ of p(x)=c1 x + c3 x^3 + c5 x^5 + c7 x^7 to tanh(x)
# on [0, A], A = 2.0, subject to:
#   c1 = 1        (unit small-signal gain -> level/EQ transparent)
#   p(A) = tanh(A) (continuous into the constant rails L = 0.964028)
#   p'(A) = 0     (C1 join; tanh'(2) = 0.071 is small)
# Result: max |p - tanh| = 0.6 % on [0,2], monotone. Above |x|=2 the output is the
# flat rail L vs tanh's creep to 1.0 -> at most 0.3 dB extra squash on extreme
# peaks (accepted; slightly MORE germanium-like compression).
#
# Verified in-pipeline (HP 2653 + bias 0.30 + gain 4..80, mirrors of both paths):
#   harmonic profile: h1-h3 within ~0.4 dB of the legacy tanh at drive 0.5/1.0,
#     input 0.08/0.2 (single-coil / humbucker reference levels)
#   alias (5 kHz probe, max Drive): dominant 13 kHz fold -18 dB; the 3 kHz bin
#     (9th-harmonic fold, beyond the poly's order -> rail-clamp energy) is ~2 dB
#     worse but sits ~40 dB below the 13 kHz fold in the legacy path. Net win.
# Regression: tests/drive_test.cpp T68.

import numpy as np

A = 2.0
L = np.tanh(A)
x = np.linspace(0.0, A, 4001)
t = np.tanh(x)
ks = (1, 3, 5, 7)

M = np.stack([x**k for k in ks], 1)
Ca = np.vstack([[1.0] + [0.0] * (len(ks) - 1),          # c1 = 1
                [A**k for k in ks],                      # p(A) = L
                [k * A**(k - 1) for k in ks]])           # p'(A) = 0
d = np.array([1.0, L, 0.0])
H = M.T @ M
g = M.T @ t
K = np.block([[H, Ca.T], [Ca, np.zeros((3, 3))]])
c = np.linalg.solve(K, np.concatenate([g, d]))[:len(ks)]

p = M @ c
print("coeffs c1,c3,c5,c7 =", np.array2string(c, precision=9))
print("max fit error      = %.5f" % np.max(np.abs(p - t)))
print("min slope (monotone if >=0) = %.5f" % np.min(np.gradient(p, x)))
print("rail L = p(A)      = %.9f  (tanh(2) = %.9f)" % (M[-1] @ c, L))

c1, c3, c5, c7 = c
F1A = A**2 / 2 + c3 * A**4 / 4 + c5 * A**6 / 6 + c7 * A**8 / 8
F2A = A**3 / 6 + c3 * A**5 / 20 + c5 * A**7 / 42 + c7 * A**9 / 72
print("F1(A) = %.9f   F2(A) = %.9f" % (F1A, F2A))
print("-> DriveBlock kSatC3/kSatC5/kSatC7 = %.9f / %.9f / %.9f" % (c3, c5, c7))
