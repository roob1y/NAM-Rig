# Green Drive — the Ibanez TS808 (Overdrive model 0)

The reworked feedback-clip Tube Screamer (originally built as "Green Drive II";
the v1 tanh stand-in was later retired, so this is model 0). Design history in
[option-a-design.md](option-a-design.md); this doc records the shipped voicing
and the 2026-07-04 authentic refit. Source: ElectroSmash "Tube Screamer Analysis".

## Shipped voicing (DriveBlock.h `od[0]`)

Cubic soft clip (type 3) on 2nd-order ADAA; pre/de-emphasis high-shelf pair
9 dB @ 700 Hz ≈ the real feedback HPF (~720 Hz: 4.7k + 0.047 µF) → bass clips
least; static mid hump +3.6 dB @ 820 Q 0.7 post-clip, low-cut 220, top LP 1900;
treble-shelf tone (bass fixed); dynDepth 0.40 touch; Io: 446 k input (gentle
3 kHz damp), ~8 Hz coupling.

## 2026-07-04 — authentic refit (UNCOMMITTED)

Two review findings, both fixed on Robbie's "don't worry about ear-approved,
make it authentic" call:

1. **Gain range is now the REAL circuit: `gMin 12 → gMax 118`** (was the
   approximate 5..80). Gv = 1 + (51k + Drive·500k) / 4k7 → 11.9 at min, 118 at
   max. The consequence the old range hid: **a TS never fully cleans up** — min
   Drive keeps +21.6 dB into the diodes, so a humbucker at the calibration
   reference has real hair at Drive 0 (measured THD 0.249) while a single-coil
   stays much cleaner (0.041). T13's touch-spread bound retuned 10×→6×
   accordingly (measured 9.1×, still clearly touch-responsive).
2. **The clean base now tracks Drive** (`cleanBlendLo 0.35 → cleanBlend 0.12`,
   reusing the Klon dual-gang mechanism, extended to the soft-poly branch and
   guarded so SD-1/Breaker/fuzzes stay byte-exact). Rationale: in the
   non-inverting TS topology the dry signal passes at **unity always** — the
   clipped component rides on top — so the dry *fraction* of the output shrinks
   in proportion as the clipped part grows. The old flat 0.20 was too small at
   low Drive and too big cranked.

**Level:** noon RMS matched the old voicing within 2 % (HB reference, 220 Hz),
so `outTrim 1.15` is untouched. Bonus: the output sweep now keeps growing to
max Drive (old voicing flat-lined past ~0.75 — the fixed clean fraction padded
the rails); THD across the sweep: 0.169 → 0.418 → 0.477, monotone (T70).

**Expect in the play-test:** Drive 0 is no longer pristine — light hair with
humbuckers (that's the pedal); low-Drive feel is bigger/cleaner-blended;
cranked is slightly dirtier and less padded. Noon ≈ unchanged. If min-Drive
hair is unwanted in practice, the honest knob is the guitar's volume (touch
cleanup still works: dynDepth is unchanged).

## Verification (`drive_test.cpp`)

T9 mid-hump · T11 count · T12 ADAA2 alias (−52 dB) · T13 touch (6× bound, 9.1×
measured) · T14 static shaper @ Drive 0 · T61 ordering intact (BB 0.350 < GD
0.459 < SD-1 0.560 noon THD) · **T70** pins the range + tracked clean base +
min-Drive hair + monotone sweep. Full battery 159/159.
