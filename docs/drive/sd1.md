# Super Drive — the Boss SD-1 Super Overdrive (Overdrive model 2)

A worked example of the [playbook](building-drives-playbook.md), built end-to-end
the same way as [Green Drive II](circuit-accuracy.md) (TS808). The SD-1 is the
easiest *new* model because it shares the Tube Screamer's DNA — so most of the
voicing carries over, and the work is in the **one thing that makes it an SD-1**:
asymmetric clipping.

## 1. The circuit (research)

The SD-1 (1981) is Boss's update of their own OD-1 (1977), adding the tone control
that the TS-808 had borrowed from the OD-1 in the first place. All three —
OD-1 / TS-808 / SD-1 — are the **same feedback-clip overdrive**: a non-inverting
op-amp gain stage (uPC4558 first half) with the clipping diodes *in the feedback
loop* (soft clipping), a fixed ~720 Hz pre-clip HF boost, and a ~720 Hz post-stage
low-pass into the tone control. Electric Druid's side-by-side redraw puts it
bluntly: *"This comparison emphasises just how similar these pedals are. In many
places they're identical."*

Two things make the SD-1 an SD-1 rather than a TS:

1. **Asymmetric clipping.** Three silicon diodes (originally 1S2473, Vf ≈ 0.9 V):
   **two in series one way, one the other**. So one half of the wave clips at ~2×
   the threshold of the other — a **~2:1 ratio**. This produces strong even
   harmonics (a 2nd-harmonic "crunch") that the TS-808's *symmetric* pair does
   not. This is the identity. (The TS's symmetric clipping is sometimes said to
   have dodged a patent on the OD-1's asymmetry.)
2. **More HF roll-off when cranked.** The feedback HF-limit cap is larger than the
   TS's 51 pF, so the drive stage rolls off down to ~**1.56 kHz at max gain** (the
   TS only reaches ~5.6 kHz). Clean/min-drive the two are the same; *driven*, the
   SD-1 is darker and smoother on top.

Minor: the SD-1's minimum gain is a touch lower than the TS ("the TS is a little
hotter at minimum" — Electric Druid), and the stock low end is a little thinner.

Sources: [Electric Druid — Designing a classic OD-1/TS-808/SD-1 Overdrive](https://electricdruid.net/designing-a-classic-overdrive/),
[Wampler DIY — SD-1 mods (stage-by-stage stock values)](https://wamplerdiy.com/blogs/news/a-few-easy-boss-sd-1-super-overdrive-mods),
[hobby-hour SD-1 schematic](https://www.hobby-hour.com/electronics/s/sd1-super-overdrive.php) (3× 1S2473, 2+1 asymmetric),
[Fader & Knob — SD-1 mod guide](https://faderandknob.com/blog/sd1-mod-guide).

## 2. The clean voicing (derive + fit)

`docs/drive/sd1_response.py` derives the small-signal (diodes open) transfer
function from the drive-stage component values (Rg leg 1k/0.22 µF → 720 Hz boost;
feedback R5 ≈ 10k fixed + 1 M pot ∥ ~100 pF HF cap; ~720 Hz post LP) and fits our
`lowCut · midPeak · topLP` to it. As predicted, it lands almost exactly on the
TS808:

```
SD-1 small-signal (dB re 200 Hz), min drive:
 f(Hz):   50   100   200   300   500   720  1000  1500  2000  3000  5000
 SD-1:  -7.6  -4.4  +0.0  +2.5  +4.8  +5.4  +4.9  +3.2  +1.5  -1.5  -5.6
 TS808: -8.2  -4.6  +0.0  +2.6  +4.9  +5.5  +5.1  +3.4  +1.5  -1.4  -5.6   (within ~0.5 dB)
```

The HF-limit cap shows up only when cranked (this is the "darker driven" trait,
not in the clean fit — handled by the emphasis pair + a slightly lower top LP):

```
SD-1 small-signal at MAX drive: 3000 Hz -7.6 dB, 5000 Hz -15.5 dB (vs -1.5 / -5.6 clean)
```

Best fit (RMS **0.63 dB**): `lowCut 160, midHz 900, midQ 0.5, midDb 5.0, lpHz 2245`.
Shipped voicing rounds these and sets `lpHz 2000` (a touch lower than the clean fit
to lean toward the SD-1's usually-driven, slightly-smoother top). Same +5 dB hump
near 720–900 Hz as the TS, slightly fuller bass and a hair brighter — the SD-1's
"more open" reputation, confirmed by the circuit, not guessed.

## 3. The clip (the actual work)

Clip **type 4 — the asymmetric cubic** (built for Round Fuzz II): positive knee at
1 (rail +2/3), negative knee at `kn = 1 − bias` (rail −(2/3)·kn). Unlike a
symmetric shaper + DC `bias` (which just offsets and squares up *symmetrically*
when cranked), this keeps the asymmetry at all gains — exactly the persistent even
harmonics the 2:1 diode split produces. Polynomial → exact F1/F2 → 2nd-order ADAA
(no dilogarithm), with the same peak guard as the cubic/hard paths.

The real diode ratio is 2:1 (≡ `bias 0.5`), but the diodes are in the **feedback
loop** = a *soft* knee, so a literal hard 2:1 over-states the audible asymmetry.
We use **`bias 0.35` (kn 0.65)** — clearly asymmetric, milder than the fuzz
(`bias 0.45`), motivated by the circuit but softened for the feedback-clip knee.
The even-harmonic ladder (h2/h1, measured in `sd1_response.py`):

```
drive 1.5:  bias 0.00→0.000  0.30→0.064  0.35→0.075  0.45→0.098
drive 3.0:  bias 0.00→0.000  0.30→0.034  0.35→0.040  0.45→0.052
```

So `bias 0.35` sits well above the symmetric Green Drive II (~0) and below the
Rangemaster's strong germanium warmth (0.113) — a musical SD-1 crunch.

**Isolation-validated** before wiring in (`/tmp/asym_validate.py` method): at
kn 0.65, F1′=f and F2′=F1 to ~1e-10, kn=1 reduces *exactly* to the plain cubic,
the ADAA2 path maxabs = 0.667 (the rail — no peak spikes), the alternating-peak
guard holds, rails +0.667 / −0.433.

## 4. Range, output, tone, feel

- **Noticeably hotter than GD2** (Robbie's call): `gMin 6 / gMax 120` (GD2 is
  5/80) — the SD-1's 1 M drive pot + 0.9 V diodes give more gain and output on
  tap; pushes into light distortion when cranked. Calibration-referenced
  (`kReferenceDbu`) like the rest; THD-vs-knob verified for single-coil/humbucker.
- **A touch more output:** `outTrim 1.25` (the asym rails average lower than a
  symmetric clip, so this both compensates and gives the SD-1 a little more level).
- **Feedback-clip feel** (same as GD2/TS): pre/de-emphasis pair `emphDb 10 @ 700 Hz`
  so bass clips least and the top stays smooth; small `cleanBlend 0.15` +
  `dynDepth 0.40` for transparency/touch. Static voicing (`shapeTrack 0`), mid
  peak post-clip (`midPost 1`) — level-stable, works as an always-on shaper.
- **Tone:** TS-style treble shelf (`pivotHz 1200`, bass fixed via the soft-poly
  path) — the SD-1 tone control is the same topology as the TS.

Engine note: the pre/de-emphasis pair was previously enabled only for clip 3; it's
now enabled for clip 3 **or 4** when `emphDb > 0` (Round Fuzz II has `emphDb 0`, so
it is unaffected and stays byte-exact). No other shipped model changes.

## 5. Voicing row (DriveBlock.h `od[]`, model 2)

```
//  clip gMin  gMax  lowCut  midHz midDb midQ  lpHz   bias  pivot  outTrim shp post emphDb emphHz clean dyn  toneF adaa2 gate
{ 4, 6.0f,120.0f,160.0f,900.0f,5.0f,0.5f,2000.0f,0.35f,1200.0f,1.25f,0.0f,1.0f,10.0f,700.0f,0.15f,0.40f,0.0f,0.0f,0.0f }
```
