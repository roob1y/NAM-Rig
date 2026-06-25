# Option A — zero-latency ADAA overdrive (design + math)

The chosen rework. Keeps the engine's **zero-latency / bit-exact-off** ethos (no
oversampling) while delivering the three goals: lower aliasing, authentic
topology, better dynamics. Everything below stays measurable in
`tests/drive_test.cpp`.

## New per-slot overdrive path (clip type 0 family)

```
drive gain
  -> pre low-cut + mid bloom (unchanged, Drive-tracked)
  -> PRE-EMPHASIS  (high-shelf +Ke dB above ~fc)      ─┐ frequency-selective
  -> cubic soft-clip, 2nd-order ADAA (double)          │  clipping
  -> DE-EMPHASIS   (high-shelf −Ke dB above ~fc)      ─┘
  -> CLEAN BLEND   (+b · band-limited clean)              transparency/dynamics
  -> post low-pass -> DC blocker -> tone -> level
```

The envelope follower modulates the clip knee + clean blend (dynamics). Boost,
Distortion and Fuzz keep their current shapers/ADAA unchanged — this only
touches the Overdrive core (and the shared ADAA helper, which other types can
opt into later).

## 1. Cubic soft-clip core (replaces `tanh` for OD)

Chosen because it is `tanh`-like but **polynomial**, so both the 1st *and* 2nd
antiderivatives are cheap closed forms → real 2nd-order ADAA (no dilogarithm).
Shipped form is **unit-slope at 0** (so small-signal gain matches the old `tanh`
voicing) and saturates at ±2/3 (level made up by `outTrim`):

```
f(x)  = x − x³/3               for |x| ≤ 1
      = sign(x) · 2/3           for |x| > 1
f'(x) = 1 − x²                 (= 0 at |x|=1, so C¹-smooth into the flat region)
```

Antiderivatives (F1 even, F2 odd; constants chosen for continuity at |x|=1,
a = |x|, s = sign x):

```
|x| ≤ 1:
  F1(x) = x²/2 − x⁴/12
  F2(x) = x³/6 − x⁵/60
|x| > 1:
  F1(x) = (2/3)·a − 1/4
  F2(x) = s · ((1/3)·a² − a/4 + 1/15)
```

Check: F1(1)=5/12≈0.4167, F2(1)=3/20=0.15 from both branches. ✔ (see
`DriveBlock::cubF/cubF1/cubF2`.)

## 2. Second-order ADAA

For nonlinearity `f` with antiderivatives `F1`, `F2`, three samples
`x2=x[n]`, `x1=x[n−1]`, `x0=x[n−2]`:

```
            2          ⎡ F2(x2) − F2(x1)     F2(x1) − F2(x0) ⎤
y[n] =  ─────────── ·  ⎢ ───────────────  −  ─────────────── ⎥
        (x2 − x0)      ⎣   (x2 − x1)            (x1 − x0)     ⎦
```

Degenerate denominators (`x2≈x1`, `x1≈x0`, `x2≈x0`) are replaced by their
L'Hôpital limits expressed via `F1` and `f` (Taylor fallbacks, as in Chowdhury's
reference implementation). All evaluated in **double** — the F2 differences lose
precision in float at small signal, same reason the existing 1st-order path is
double.

Cost: 2 samples of state (`x[n−1]`, `x[n−2]`). Group delay ~1 sample of phase
(not buffered latency); the all-Off rack stays **bit-exact**. SNR improves
markedly over 1st-order, especially the high folded harmonics.

## 3. Frequency-selective clipping — pre/de-emphasis

Emulates the TS feedback HPF (bass clipped least) + the 51 pF soft-corner cap,
without a stateful in-loop clipper:

- **Pre-emphasis**: high-shelf **+Ke dB** above `fc ≈ 700 Hz` *into* the clipper
  → mids/highs hit the nonlinearity harder, bass stays below the knee → **bass
  clipped least**, mid-forward grind.
- **De-emphasis**: complementary high-shelf **−Ke dB** above `fc` *after* the
  clipper → overall small-signal response stays ~flat, and it **low-passes the
  generated harmonics** → soft corners, no fizz (the 51 pF role).

Start `Ke ≈ 8–10 dB`, `fc ≈ 700 Hz`. One shared shelf shape (use
`Biquad::highshelf`), reused pre and post with ±gain.

## 4. Clean blend — transparency & dynamics

```
out = (1 − b)·clipped + b·cleanBL
```

`cleanBL` = the band-limited (pre-clip, post-EQ) signal at unity, so blending
restores the guitar's dynamics (the TS "secret" / the Klon feed-forward). Small
for Green Drive (`b ≈ 0.15–0.25`, TS-ish); a future transparent/Klon voicing
would push `b` higher.

## 5. Envelope dynamics (touch/feel)

One-pole envelope follower on `|input|` (fast attack ~5 ms, slow release
~120 ms) produces `env ∈ [0,1]`. It modulates, gently (depth ~0.3):

- **clean blend up** when `env` is low → soft picking cleans up;
- **effective knee/gain up** when `env` is high → digging in bites.

So the pedal reacts to *playing level*, not just the Drive knob — valve-on-the-
edge behaviour. Depth is conservative so it never pumps.

## New `Voicing` fields (proposed)

| field | meaning | Green Drive start |
|-------|---------|-------------------|
| `clip` | extend with `3 = cubic soft` (keep 0/1/2 as-is) | 3 |
| `emphDb` | pre/de-emphasis shelf depth (0 = off) | 9.0 |
| `emphHz` | emphasis corner | 700 |
| `cleanBlend` | clean mix `b` | 0.20 |
| `dynDepth` | envelope modulation depth (0 = static) | 0.30 |

Keeps full backward-compat: any voicing with `emphDb=0, cleanBlend=0,
dynDepth=0` behaves like a plain shaper, and `clip 0/1/2` are untouched, so
Boost/Dist/Fuzz are unchanged.

## Verification — implemented in `tests/drive_test.cpp`, measured results

Built/run offline per the sandbox recipe (JUCE stub in `/tmp`). The two OD
models are exposed as **model 0 "Green Drive"** (unchanged tanh) and **model 1
"Green Drive II"** (this rework), so the suite A/Bs them directly.

| Test | What it asserts | Measured |
|------|-----------------|----------|
| T11 | model 0 is byte-for-byte the legacy default; category now has 2 models | exact match ✔ |
| T12 | 2nd-order ADAA crushes alias vs a naive cubic (hot 5 kHz) | **−36.4 dB** @ 3 kHz ✔ |
| T13 | envelope dynamics: loud/quiet harmonic spread at the edge of breakup, v2 > v1 | **59×** (v2) vs 37× (v1) ✔ |
| T14 | v2 is a midrange SHAPER at Drive 0 (static voicing) | 780 Hz **+10.1 dB** vs 100, +7.8 vs 3k ✔ |
| T1–T10 | unchanged engine behaviour (Off bit-exact, mid-hump, Fuzz asym, …) | all green ✔ |

## Knob feel (the part players notice)

Tuned so the Drive and Tone *sweeps* match a real TS, not just the clipped tone:

- **Drive taper = log/audio pot.** A real TS Drive pot is a log/audio taper and the
  stage gain ≈ pot resistance, which is *exactly* the engine's existing
  `gMin·(gMax/gMin)^drive` map (equal dB per rotation; gain stays modest through
  the lower half then ramps up top). No warp added — that map is the taper.
- **Lifelike gain range, calibration-referenced.** `gMin 5 → gMax 80` ≈ the real
  TS's 12..118. The clip threshold is FIXED, so distortion tracks the actual
  input *level* — hot pickups/DI drive harder than weak ones, like the real
  pedal. It's voiced for the app's calibration reference (`CalNorm
  kReferenceDbu`); with **Calibrate Input ON** the guitar is trimmed to that
  reference, so the response is level-accurate and consistent across interfaces.
  Earlier `gMin 3 / gMax 33` was ~4× too weak — max Drive barely broke up below a
  hot DI. Touch dynamics are strongest at low-to-mid Drive (a cranked TS
  compresses), so the dynamics test (T13) measures there.
- **Static voicing = works as a shaper.** `shapeTrack = 0`: the mid hump +
  bass-cut are present at *every* Drive, including off — so "Drive off, Tone
  past noon" gives the classic always-on TS mid-shaper/boost. Only the clipping
  (gain + emphasis) scales with Drive; the EQ voicing is fixed, as in the real
  fixed tone stack. The hump itself (**+3.6 dB @ 820 Hz, low-cut 220, top LP
  1900**) is **fit to the measured TS808 transfer function** — see
  [circuit-accuracy.md](circuit-accuracy.md). (Our first pass was ~2× over-
  humped; deriving it from the schematic corrected that.)
- **Tone = treble shelf, bass fixed.** The Tone knob moves treble above ~1.2 kHz
  (±~8 dB) and leaves the bass/low-mids put — the TS tone control, *not* the
  engine's default symmetric tilt (which also see-sawed the bass). Measured: at
  the extremes, 100 Hz shifts <0.3 dB while 5 kHz swings ±~8 dB.

These are voicing-level changes to **v2 only** (clip 3); v1 "Green Drive" keeps
its original taper/tilt for the A/B. Starting values — easy to fine-tune by ear
(raise `gMin` for more grit at minimum, change `emphDb`/`cleanBlend` for more or
less of the smooth/dynamic character).

Notes from measurement:
- **Levels matched**: model 1 `outTrim` set to **1.15** so v1/v2 sit within ~5 %
  RMS across the Drive sweep — a fair A/B.
- The emphasis's audible effect is mainly **softer, anti-aliased high harmonics**
  + a tighter low end under drive; the cubic is intrinsically a touch brighter
  than `tanh`, but those harmonics are clean (2nd-order ADAA) rather than gritty.
  A simple "bass-THD ≪ mid-THD" metric is confounded by the existing low-cut +
  mid-hump EQ, so it isn't asserted directly — T14 instead proves the emphasis
  is transparent until the clipper engages.

## Sources

- [Parker, Zavalishin, Le Bivic — Antiderivative Antialiasing for Memoryless Nonlinearities (DAFx 2016)](https://www.research.ed.ac.uk/portal/en/publications/antiderivative-antialiasing-for-memoryless-nonlinearities(aee5d5ad-0e2a-4587-abe9-c2a8b54725ba).html)
- [Jatin Chowdhury — Practical Considerations for Antiderivative Anti-Aliasing](https://jatinchowdhury18.medium.com/practical-considerations-for-antiderivative-anti-aliasing-d5847167f510)
- [jatinchowdhury18/ADAA — reference implementations](https://github.com/jatinchowdhury18/ADAA)
- [ElectroSmash — Tube Screamer Analysis](https://www.electrosmash.com/tube-screamer-analysis)
- [ElectroSmash — Klon Centaur Analysis](https://www.electrosmash.com/klon-centaur-analysis)
