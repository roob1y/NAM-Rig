# Dynamic Cab — DESIGN

`CabDynamicsBlock` — a **delta wrapper** around the existing static IR
convolution (`CabBlock`). 3Sigma captures are full *speaker + cabinet + mic*
snapshots: box resonance, cone colour and the small-signal impedance curve are
**already baked into the IR**. This block adds **only** the behaviour a static
linear IR physically cannot contain: **level-dependent and time-variant**
deviation. Nothing here re-EQs the cab; every stage contributes a *delta on top
of the IR* and collapses to nothing at rest.

> Written before the implementation. `CabDynamicsBlock.h` implements exactly
> what is derived here. The block cannot be heard by its author — voicings
> flagged **[EAR]** are physically-motivated guesses pending Robbie's ear-test.

---

## 0. Placement, signal flow, and the bit-exact contract

```
        processPre()                              processPost()
[in] → A. Reactive Impedance Δ → B. Cone Breakup Δ → [ existing IR conv ] → C. Enclosure Air/Damping Δ → [out]
```

The block does **not** contain a convolver. It exposes two process stages that
wrap the engine already in the codebase:

- `processPre(buf, n)`  — runs **before** `CabBlock::process` (stages A + B)
- `processPost(buf, n)` — runs **after**  `CabBlock::process` (stage C)

Two instances run (Cab A / Cab B lanes); all state is per-instance.

### The DELTA principle (why every stage is bit-exact-bypassable)

Every stage is written as `y = x + (macro · envelope) · delta(x)`, never as a
filter placed *in series* with the dry path. When the controlling macro is 0 the
delta term is multiplied by 0, so the stage is skipped entirely (early-return,
buffer untouched). Consequences:

1. **All three macros at 0 ⇒ byte-identical output** (`memcmp == 0`). Asserted
   by test **T1**. Neither process stage touches the buffer at rest.
2. A stage can never impose a *fixed* EQ colour — the crossover/allpass phase
   shifts only ever ride *inside* a delta that is zero at rest, so they cannot
   leak a static comb into the dry signal (this is what lets Stage B use a
   crossover and Stage C use allpasses without breaking bypass).

### The `active` gate (click-free engage/disengage)

Each stage tracks a smoothed engage level. A stage is *active* when its macro
target > 0 **or** any of its smoothers/envelope have not yet returned to rest.
Only when everything has settled to exactly rest does it early-return. This
mirrors the house `CutFilters` pattern (ramp to the transparent edge, *then*
leave the path) so turning a macro to 0 fades out rather than clicking.

### No loudness compensation — ever

Envelope drive is permitted **only** as physical modelling (a real speaker
genuinely behaves differently when pushed). There is **no** RMS makeup, no
auto-level, no broadband gain that restores level. The one gain-*reduction* in
the block (low-band cone power compression, Stage B) drops level when pushed and
never adds it back — that is compression, not compensation.

---

## 1. Envelope follower (shared primitive)

Each process stage owns a one-pole amplitude follower on its own input
(`processPre` follows the pre-conv signal; `processPost` follows the post-conv
signal — the panels are excited by the speaker's acoustic output, so post-conv
is the physically correct driver for Stage C).

```
a_atk = 1 − exp(−1 / (τ_atk · fs))      τ_atk = 8 ms
a_rel = 1 − exp(−1 / (τ_rel · fs))      τ_rel = 160 ms
r = |x|
env += (r > env ? a_atk : a_rel) · (r − env)
```

`env` is then mapped to a bounded **push** in [0, 1] with a soft knee so quiet
playing barely modulates and loud playing pushes:

```
push = clamp( (env − LO) / (HI − LO), 0, 1 )      LO = 0.03  (≈ −30 dBFS)
                                                  HI = 0.50  (≈  −6 dBFS)
```

`τ_rel = 160 ms` guarantees the modulation is **slow / program-dependent**: the
envelope cannot follow the waveform, so it produces no audible sidebands. All
coefficient recomputation driven by `push` happens once per 32-sample control
sub-block, and because `push` moves slowly the biquad coefficients move in tiny
continuous steps (no zipper). **Modulation is bounded per stage — ceilings stated
below.**

---

## 2. Stage A — Reactive Impedance Delta (pre-conv)

**Physics.** A moving-coil speaker's electrical impedance is not flat: a
resonance peak at the driver's free-air resonance `Fs` (here ~90 Hz for a
guitar 12") and a rising voice-coil inductance `Le` reactance above ~1–2 kHz.
The **static** shape of that curve — how it interacts with the capture rig's
source impedance — is **already in the IR**. What a static IR cannot contain:

- **Suspension nonlinearity.** At large excursion the spider/surround
  compliance stiffens and warms, so the effective `Fs` interaction **grows**
  with drive — the low resonance "blooms" only when the speaker is worked.
- **Le thermal/current rise.** Voice-coil inductance rises with current and
  temperature, so the top **droops** progressively under power.

Modelled as **two biquads whose GAINS deviate from 0 dB** driven by `push` —
flat at rest, never a fixed EQ:

### A1 — Fs resonance peak (RBJ peaking)

```
f0 = 90 Hz · (1 − 0.12·Thump)          slightly lower & fatter as Thump rises
Q  = 0.9 + 0.9·Thump                    0.9 … 1.8
gainDb_A1 = min(3.0, DEPTH_A1 · Thump · push)      DEPTH_A1 = 4.0
```

Ceiling **+3 dB** (hard-clamped). At rest `push = 0 ⇒ gainDb = 0 ⇒
Biquad::peaking() returns identity ⇒ exact passthrough`. Thump both scales the
depth and widens Q (fatter, more resonant thump when driven).

### A2 — Le inductance shelf (RBJ high-shelf, 2 kHz)

```
f0 = 2000 Hz,  S = 0.7
gainDb_A2 = −( DEPTH_A2 · push  +  HF_EASE · Age )      clamp ≥ −3.0
            DEPTH_A2 = 2.0     (dynamic droop under power)
            HF_EASE  = 1.5     (static "aged/duller" tilt from Age)
```

Ceiling **−3 dB**. Negative-going (a *droop*, never a boost). The `Age` term is
the "eases the HF shelf down slightly" behaviour of the Age macro; the `push`
term is the dynamic thermal droop.

Both biquads are rebuilt once per 32-sample control block from the smoothed
`push`, using `copyCoeffsFrom` to preserve filter state across the coefficient
swap (no click on a coefficient move). Stage A is a straight series filter *only
while active*; at rest it is skipped, so the DELTA contract holds.

**Deliberately NOT modelled here** (already in the IR): the static impedance
magnitude/phase, the mic-position comb, the fixed HF roll-off of the cone. This
stage only *deviates* from those.

---

## 3. Stage B — Band-Limited Cone Breakup (pre-conv)

**Physics.** Above the piston band a paper cone stops moving as one rigid body:
concentric regions break into out-of-phase modal motion ("cone cry"), which adds
level-dependent odd-harmonic grit in the **~1–4 kHz** midrange. The IR captured
this cone at *one* small-signal level, so its breakup is frozen. This stage adds
the **level-dependent** part.

### B1 — harmonics-only parallel delta, band-limited drive

Cone breakup is a **midrange** phenomenon, not a broadband or extreme-top one.
The driven band is isolated with a Linkwitz-Riley-style crossover HP at ~800 Hz
plus a gentle LP at ~3.8 kHz (the low band stays perfectly linear, as required):

```
band = LP_3800( HP_800(x) )              LR2 sections (Butterworth, Q = 0.707)
```

The nonlinearity is applied as a **parallel delta** added to the un-filtered dry
`x`, so the low band and full-range dry are untouched and bypass is bit-exact:

```
δ  = nl(band) − G(A)·band                        HARMONICS ONLY (see below)
y  = x + Wb · δ                                   Wb = smoothed Age engage (0 at rest)
nl(u) = tanh(u·drive) / drive                     odd, unit slope at 0, compressive
```

- `tanh(band·drive)/drive` keeps **small-signal gain = 1 for any drive**, so the
  onset from Age = 0 is smooth.
- At `Age = 0`, `Wb = 0` ⇒ stage skipped ⇒ bit-exact.

#### The crossover phase-alignment trap (and why `G(A)` is there)

A naïve `δ = nl(band) − band` is **not** purely harmonic under hard drive. A
compressive nonlinearity has describing-function gain `G(A) < 1` at large
amplitude `A`, so `nl(band)` contains a *reduced* fundamental: `nl(band) ≈
G(A)·band + harmonics`. The naïve delta therefore carries a residual
fundamental-frequency term `(G(A)−1)·band` **at `band`'s crossover-shifted
phase**. Added to the un-shifted `x`, that residual **combs** near the 800 Hz and
3.8 kHz corners — a level-dependent ±3 dB / −6 dB ripple (measured on the naïve
version), which would violate "never impose a fixed EQ colour / alter the IR
tonality". Any band-limited (minimum-phase) level change combs this way; it is
intrinsic, not a tuning bug.

The fix makes the delta **literally harmonics-only** by cancelling that
fundamental. `G(A)·band` *is* the fundamental component of `nl(band)`, so:

```
δ = nl(band) − G(A)·band  =  harmonics only        (the fundamental cancels)
```

`G(A)` is the **tanh describing function** at the current band envelope `A`:

```
G(A) = g(β)/β,   β = drive·A,   g(β) = (2/π) ∫₀^π tanh(β·sin t)·sin t dt
```

`g(β)` is tabulated at `prepare()` (numerical integration, 257-point lookup) and
`A` tracked by a fast band-envelope follower (5 ms / 40 ms). With the residual
fundamental removed, adding `δ` to `x` cannot cancel `x`'s fundamental, so **there
is no comb** regardless of the crossover phase. Measured level-dependent ripple
drops from **2.9 dB → 0.30 dB**, with no gain bumps above +0.2 dB. This is a
regression-guarded invariant — test **T9**.

### B2 — oversampling and the aliasing budget

`tanh` generates odd harmonics; the driven fundamental reaches ~3.8 kHz, so the
3rd (≤11.4 kHz) and 5th (≤19 kHz) land below 24 kHz and **do not alias at
48 kHz** — but the 7th (≤26.6 kHz) and up would fold. The clipper therefore runs
at **2× oversampling** with an explicit **linear-phase halfband FIR** (derived in
code from a Kaiser-windowed sinc, even taps forced to zero, run as a two-branch
**polyphase** up/down sampler — no library).

Budget at 48 kHz, 2× (intermediate 96 kHz, Nyquist 48 kHz):

| harmonic | freq (fund ≤3.8 k) | folds at 48 k? | with 2× halfband |
|----------|--------------------|----------------|------------------|
| 3rd | ≤ 11.4 kHz | no | passes clean |
| 5th | ≤ 19.0 kHz | no | passes clean |
| 7th | ≤ 26.6 kHz | would fold to ≥21.4 k | > 24 k ⇒ hit by decimator stopband |
| 9th+ | ≥ 34 kHz | would fold | deep in stopband, removed |

The halfband is designed for ≥ ~70 dB stopband. Net worst-case aliasing at max
drive is bounded well below **−55 dBFS** relative to the fundamental. Verified by
test **T2** (swept sine through the driven clipper, alias-band energy measured).

### B3 — low-band cone power compression (part of Age)

At high power a real cone **compresses** — thermal + suspension losses reduce
low-end output a couple of dB. This is a genuine *magnitude* change, so it is
implemented as a **series dynamic low-shelf** (not a parallel delta — a parallel
low-band delta would comb near its corner exactly like the naïve B1 above). A
shelf's magnitude response is smooth, so it cannot comb:

```
grDb  = −( AGE_COMP · Age  +  ENV_COMP · Age · push )     clamp ≥ −2.0
shelf = RBJ low-shelf( 200 Hz, grDb )                     AGE_COMP = 0.6, ENV_COMP = 1.8
s     = shelf(s)                                          rebuilt at control rate
```

Ceiling **−2 dB**. `grDb = 0 ⇒` identity biquad (bit-exact); level only ever
drops, never restored (no makeup).

**Deliberately NOT modelled** (already in the IR): the static cone-breakup notch
structure, the fixed presence peak, the mic comb. Stage B adds only the
*dynamic* grit and the *dynamic* low compression.

---

## 4. Stage C — Enclosure Air / Damping Delta (post-conv)

**Physics.** The IR contains the box's **linear** resonance (port/panel modes at
the capture level). It cannot contain **level-dependent panel excitation**: at
high SPL the cabinet walls flex and re-radiate a short, diffuse, slightly
damped "air" that grows with level. This is the stage **most likely to sound
artificial** — its range is deliberately conservative and it is flagged **[EAR]**.

**Why post-conv is correct.** Panels are excited by the driver's *acoustic
output*, i.e. the signal *after* the speaker+box transfer — which is exactly the
post-conv signal. Placing this delta post-conv means it re-radiates the already
cabbed sound (physically right) and, being additive at low level, it does not
re-impose a second box EQ — it only adds diffuse energy the linear IR is missing.

### C1 — diffusion network

Each lane runs **two parallel networks** of **3 series Schroeder allpasses** with
**prime** delay lengths (< 10 ms) and a one-pole **LP damping** filter inside
each allpass feedback path. Prime lengths keep the modal echoes incommensurate
(no ringing tone). Schroeder allpass with in-loop damping:

```
d   = LP_damp( line.read() )            in-loop HF damping (one-pole)
w   = x + g·d
y   = d − g·w                           allpass (energy-preserving in g)
line.write(w)
```

Two length/damping sets voiced for the two cab archetypes:

| set | delays (samples @48 k) | ≈ ms | g | damp LP | character |
|-----|------------------------|------|-----|---------|-----------|
| **A — tight 1×12 open-back** | 113, 179, 251 | 2.4 / 3.7 / 5.2 | 0.50 | 6.0 kHz | short, airy, bright |
| **B — big 4×12 closed-back** | 211, 331, 461 | 4.4 / 6.9 / 9.6 | 0.62 | 3.0 kHz | longer, darker, boxier |

Delays are resampled to the running `fs` in `prepare` (ms → samples).

### C2 — Cab Size morph (artifact-free)

Cab Size crossfades the **outputs** of the two parallel networks, never the delay
*times* — so there is no delay-length change, hence **no pitch warble / zipper by
construction**:

```
netOut = (1 − Size)·netA(x) + Size·netB(x)        Size smoothed
```

### C3 — level and mix (bounded, default 0)

```
encMix = ENC_BASE · ( 0.5·Thump + 0.5·Thump·push  +  0.25·Age·push )   clamp ≤ 0.12
y      = x + encMix · netOut
```

Wet mix ceiling **0.12** (≈ −18 dBFS of diffuse air at absolute max). Enclosure
feedback `g` is additionally scaled by Thump (Thump "scales the enclosure
feedback"). At `Thump = 0` and `Age = 0`, `encMix = 0` ⇒ stage skipped ⇒
bit-exact. Stability at max `g`/mix verified by test **T3**; the Cab-Size sweep
artifact check is test **T4**.

**Deliberately NOT modelled** (already in the IR): the box's linear resonance,
the port tuning, the panel modal EQ. Stage C adds only the *dynamic, diffuse*
excitation on top.

---

## 5. Macro map (exactly 3, floats 0..1, all default 0 = bypass)

All three are smoothed with a ~25 ms one-pole before use.

### Age / Drive `a`
| target | mapping | ceiling |
|--------|---------|---------|
| Stage B breakup engage `Wb` | `Wb = a` | — |
| Stage B drive | `drive = 1 + a·(1.0 + 1.2·push)` | ≈ 3.2× |
| Stage B low compression | `grDb = −(0.6·a + 1.8·a·push)` | −2 dB |
| Stage A2 HF ease | `−1.5·a` dB added to shelf | part of −3 dB |
| Stage C air (minor) | `+0.25·a·push` into `encMix` | ≤ 0.12 |

### Thump `t`
| target | mapping | ceiling |
|--------|---------|---------|
| Stage A1 depth | `gainDb = min(3, 4.0·t·push)` | +3 dB |
| Stage A1 Q / f0 | `Q = 0.9 + 0.9·t`, `f0 = 90·(1−0.12·t)` | — |
| Stage C enclosure feedback | `g = g_set + 0.10·t` | g ≤ 0.72 |
| Stage C air | `0.5·t + 0.5·t·push` into `encMix` | ≤ 0.12 |

### Cab Size `s`
| target | mapping |
|--------|---------|
| Stage C network crossfade | `netOut = (1−s)·netA + s·netB` |

Cab Size only voices the enclosure; it is audible when the enclosure is engaged
(Thump or Age driving `encMix > 0`).

---

## 6. Modulation ceilings (summary — all small, all bounded)

| stage | quantity | max swing | driver |
|-------|----------|-----------|--------|
| A1 | 90 Hz resonance gain | **+3 dB** | Thump · envelope |
| A2 | 2 kHz inductance shelf | **−3 dB** | envelope + Age |
| B1 | midband harmonic drive | drive ≤ 3.2×, band 0.8–3.8 kHz | Age · envelope |
| B3 | low-band compression | **−2 dB** | Age · envelope |
| C | diffuse air wet mix | **≤ 0.12 (≈ −18 dB)** | Thump/Age · envelope |

---

## 7. What was deliberately left out (because the IR already has it)

- The **static** impedance magnitude/phase curve and its capture-rig
  interaction — Stage A only models the *deviation* under drive.
- The **cone's fixed** frequency response, presence peak, and breakup notch
  structure — Stage B adds only *dynamic* grit, not a re-EQ.
- The **box's linear** resonance, port tuning and panel modal EQ — Stage C adds
  only *dynamic diffuse* excitation, not a second cab.
- **Mic colour / position comb** — entirely the IR's job; nothing here touches
  it.

If a proposed stage mostly duplicated the IR it was cut or made subtle. The
whole block is a thin, mostly-silent delta that only wakes up when the macros and
the playing dynamics ask it to.

---

## 8. Verification map (see `tests/cabdyn_test.cpp`)

| test | asserts |
|------|---------|
| **T1** | bit-exact bypass — all macros 0 ⇒ `memcmp(out, in) == 0` (pre & post) |
| **T2** | aliasing bound — swept sine at max drive, alias-band energy < −55 dBFS |
| **T3** | stability at max settings — 10 s of full-scale noise stays finite & bounded |
| **T4** | delay-change / Cab-Size artifact — sweeping Size on a steady tone: no sample step > bound, no delay-time zipper |
| **T5** | determinism — identical input+params ⇒ identical output |
| **T6** | envelope-driven, not static EQ — a loud passage deviates, a −40 dB passage barely does |
| **T7** | modulation ceilings — measured A1/A2/C swings stay within the stated caps |
| **T8** | engage/disengage is click-free (macro 0→x→0 leaves no residual, returns to bit-exact) |
| **T9** | Stage B is comb-free — level-dependent fundamental response is smooth (no crossover phase-cancellation: no >0 dB bumps, adjacent-bin ripple < 1 dB) |
