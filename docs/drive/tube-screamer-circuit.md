# Tube Screamer TS808 — clipping amplifier analysis

The TS808 is the reference for our **Green Drive** voicing. The whole "tube
screamer sound" lives in one stage: the **clipping amplifier** — a non-inverting
op-amp (JRC4558) with the clipping diodes *inside the feedback loop* and two
filters shaping where/how it clips.

## Signal chain (effect engaged)

```
input buffer -> clipping amplifier -> tone/volume -> output buffer
                (the part that matters)
```

## The clipping amplifier

Non-inverting op-amp gain is `1 + Z2/Z1`:

- **Z1** = `R4 (4.7k) + C3 (0.047µF)` from the (−) input to ground — an active
  **high-pass**. At DC, C3 is open → Z1 → ∞ → gain → **1** (no boost, no clip).
  At high freq, C3 shorts → Z1 → R4 → gain rises to **~12–118** (Drive pot).
- **Z2** = feedback = `(R6 51k + Drive 0–500k)` ∥ the two clipping diodes ∥
  `C4 (51pF)`.

### 1. Frequency-selective distortion (the big one)

The R4/C3 high-pass in the feedback sets a corner at

```
fc = 1 / (2π · 4.7k · 0.047µF) ≈ 720 Hz
```

Below ~720 Hz the stage gain falls toward unity, so **bass notes are clipped
least** and stay tight; mids/highs above 720 Hz get the full gain and the
distortion. This is *why* a TS doesn't get flubby on low notes and sits in a
mix. It is **not** a post-EQ scoop — the clipping itself is frequency-selective.

### 2. Diode clipping — symmetric, soft, in the loop

Two anti-parallel diodes (1N4148-ish) clip symmetrically to **≈ ±0.7–1 V**. When
a diode conducts it collapses the feedback impedance, dropping the stage gain to
~1. Because it's *feedback* clipping (not a diode-to-ground after a fixed gain),
the knee is **soft** and — crucially — **part of the clean input remains in the
output** ("The Tube Screamer's Secret", Bogac Topaktas). That preserved clean
signal is what keeps the pedal dynamic and touch-responsive.

Removing one diode → **asymmetric** clipping (this is the SD-1 idea, see
[overdrive-family.md](overdrive-family.md)).

### 3. Soft corners — the 51 pF cap

`C4 = 51pF` across the diodes is a **low-pass** that softens the sharp corners of
the clipped wave and mellows the high-harmonic fizz. Its corner moves with the
Drive pot (most audible at max Drive). This is a built-in anti-fizz that a naive
memoryless waveshaper does **not** have.

## After the clipper — the mid hump

- Passive LPF `R7 (1k) / C5 (0.22µF)` → **fc ≈ 723 Hz**, −20 dB/dec. Removes
  harsh overtones.
- Active tone control levels/accentuates that around ~720 Hz.

Combined with the feedback HPF, this produces the famous **mid hump** centred
around 720 Hz. Our current voicing already models this post-clip (`midHz≈780`,
`midPost=1`) — that part is right; what's missing is the *frequency-selective
clipping* and the *clean blend* above.

## What to take into the model

| Circuit feature | Sound | Our Option A emulation |
|---|---|---|
| HPF in feedback (720 Hz) | bass clipped least, mid-forward grind | **pre-emphasis** high-shelf into the clipper |
| 51 pF across diodes | soft corners, no fizz | **de-emphasis** low-pass after the clipper |
| Feedback clipping keeps clean | dynamics, touch | **clean-blend** term |
| Symmetric diodes | odd harmonics, smooth | cubic soft-clip (symmetric) |
| Post LPF + tone | 720 Hz mid hump | (already modelled — keep) |

## Sources

- [ElectroSmash — Tube Screamer Analysis](https://www.electrosmash.com/tube-screamer-analysis)
- [R.G. Keen — The Technology of the Tube Screamer (GeoFex)](http://www.geofex.com/article_folders/tstech/tsxtech.htm)
- [StompboxElectronics — Analysis of the Ibanez TS-9 Clipping Circuit](https://stompboxelectronics.com/2023/04/03/an-analysis-of-the-ibanez-ts-9-clipping-circuit/)
