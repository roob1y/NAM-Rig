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

```
f(x)  = 1.5x − 0.5x³            for |x| ≤ 1
      = sign(x)                 for |x| > 1
f'(x) = 1.5 − 1.5x²            (= 0 at |x|=1, so C¹-smooth into the flat region)
```

Antiderivatives (F1 even, F2 odd; constants chosen for continuity at |x|=1):

```
|x| ≤ 1:
  F1(x) = 0.75x² − 0.125x⁴
  F2(x) = 0.25x³ − 0.025x⁵
|x| > 1   (a = |x|, s = sign x):
  F1(x) =  a − 0.375
  F2(x) =  s · (0.5a² − 0.375a + 0.1)
```

Check: F1(1)=0.625, F2(1)=0.225 from both branches. ✔

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

## Verification plan (add to `tests/drive_test.cpp`)

- **2nd-order alias**: hot 5 kHz sine through OD, fold-back at 3 k/13 k is
  lower than the 1st-order path (and far below naive). Target ≥ +6 dB vs naive.
- **Frequency-selective clipping**: at high Drive, THD/harmonic energy on a
  100 Hz tone ≪ THD on a 1 kHz tone (bass clipped least). New, key test.
- **Clean blend preserves dynamics**: small-signal gain ≈ unity-ish; output
  tracks input amplitude more linearly than `cleanBlend=0`.
- **Envelope dynamics**: a quiet burst is cleaner (less harmonic energy) than a
  loud burst at the same Drive (with `dynDepth>0`).
- Keep **T1–T10** green (Off bit-exact, mid-hump, etc.).

Build/run offline per the sandbox recipe (JUCE stub in `/tmp`, see memory
`namrig-offline-build`).

## Sources

- [Parker, Zavalishin, Le Bivic — Antiderivative Antialiasing for Memoryless Nonlinearities (DAFx 2016)](https://www.research.ed.ac.uk/portal/en/publications/antiderivative-antialiasing-for-memoryless-nonlinearities(aee5d5ad-0e2a-4587-abe9-c2a8b54725ba).html)
- [Jatin Chowdhury — Practical Considerations for Antiderivative Anti-Aliasing](https://jatinchowdhury18.medium.com/practical-considerations-for-antiderivative-anti-aliasing-d5847167f510)
- [jatinchowdhury18/ADAA — reference implementations](https://github.com/jatinchowdhury18/ADAA)
- [ElectroSmash — Tube Screamer Analysis](https://www.electrosmash.com/tube-screamer-analysis)
- [ElectroSmash — Klon Centaur Analysis](https://www.electrosmash.com/klon-centaur-analysis)
