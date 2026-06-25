# The op-amp overdrive family — TS vs SD-1 vs Klon

All three are op-amp-gain-stage overdrives. They differ mainly in **how the
diodes clip** and **how much clean signal survives**. Useful because the
Overdrive category currently holds only one model (Green Drive / TS); these are
the natural future siblings, and their tricks inform the Option A core.

## Tube Screamer (TS808/TS9) — symmetric, feedback clipping

- Two anti-parallel diodes **in the op-amp feedback loop**.
- Symmetric clip → predominantly **odd** harmonics, smooth.
- Feedback HPF (~720 Hz) → frequency-selective: **bass clipped least**.
- Clean input partly preserved → dynamic.
- Character: mid-humped, smooth, "transparent-ish", classic.

## Boss SD-1 (Super Overdrive) — asymmetric

- Same feedback-clipping topology, but **asymmetric diodes**: two in one
  direction, one in the other (relative to the op-amp).
- Asymmetry → **even + odd** harmonics in a different ratio → the SD-1 "crunch",
  described as hairier/grittier than the TS.
- **Louder and more gain** than a stock TS; at high gain it's more compressed on
  one half-cycle.
- Implementation note: our existing engine already supports asymmetry via the
  `bias` field (used by Fuzz, clip type 2). A future SD-1 model = the Green Drive
  core with a small asymmetric offset.

## Klon Centaur — hard clipping + heavy clean blend

- **Hard-clipping germanium pair**, but with **two parallel clean feed-forward
  paths** summed back in:
  - one low-pass clean feed restores low end after clipping,
  - one clean feed blended by a **dual-gang Gain pot** — at low gain it's almost
    a clean boost, blending to more distortion as you turn up.
- Result: lots of gain on tap but never harsh, **full dynamic range preserved**,
  cleans up with pick attack like a valve amp on the edge of breakup.
- This is the strongest argument for our **clean-blend** term: the Klon proves
  that summing a clean path onto a hard clip is what buys "transparency" and
  touch response.

## Design takeaways for Option A

1. **Clean blend is the transparency knob.** Both the TS (incidentally, via
   feedback clipping) and the Klon (deliberately) keep clean signal in the
   output. We add an explicit `cleanBlend` term — small for Green Drive (TS-ish),
   larger would give a Klon-ish transparent voicing later.
2. **Symmetry is one parameter.** Symmetric (TS) vs asymmetric (SD-1) is just the
   `bias` we already have. The cubic soft-clip core supports both.
3. **Frequency-selective clipping** (pre/de-emphasis) is shared by the whole
   family and is the single biggest "feel real" upgrade over a flat waveshaper.

## Sources

- [TS-808 vs Boss SD-1: clipping diodes (comparison)](https://www.youtube.com/watch?v=ETu9AwWLCE4)
- [Electric Druid — Designing a classic OD-1/TS-808/SD-1 overdrive](https://electricdruid.net/designing-a-classic-overdrive/)
- [ElectroSmash — Klon Centaur Analysis](https://www.electrosmash.com/klon-centaur-analysis)
- [Coda Effects — Klon Centaur circuit analysis](https://www.coda-effects.com/p/klon-centaur-circuit-analysis.html)
