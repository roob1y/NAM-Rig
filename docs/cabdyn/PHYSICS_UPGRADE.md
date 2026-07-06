# Dynamic Cab — PHYSICS UPGRADE (displacement, intermodulation, per-cab Fs)

Extends `docs/cabdyn/DESIGN.md`. That document still governs; this one upgrades
the block from *phenomenological* (broadband-envelope-driven) to *physics-driven*
(cone-displacement-driven) and adds the couplings a real speaker has that the
current block lacks. Written before the implementation; `CabDynamicsBlock.h`
implements exactly what is derived here.

Scope (agreed with Robbie, 2026-07-06): roadmap items 1–3 plus the two cheap
research-grade items — thermal voice-coil compression and modal shaping of the
breakup exciter. NOT in scope: full Klippel Bl(x)/Kms(x)/Le(x) state-space motor
(needs an implicit solver in the loop; cannot be expressed as a delta on top of
the IR), vented-box displacement null, LF self-distortion, port noise.

Everything here preserves the §0 contract of DESIGN.md unchanged:
**delta-on-top-of-any-IR, bit-exact bypass at rest (T1/T8), no loudness
compensation, exactly 3 macros, bounded ceilings, JUCE-free header.**

---

## 1. The excursion model (new shared primitive, pre-conv)

**Physics.** Every large-signal speaker nonlinearity — suspension stiffening
`Kms(x)`, force-factor droop `Bl(x)`, Doppler — depends on cone **displacement**
`x(t)`, not on broadband signal level. For a voltage-driven driver in a sealed
box, displacement vs voice-coil voltage is a 2nd-order resonant lowpass at the
in-box resonance `Fc` with `Q = Qtc`:

```
X(s)/U(s) = K / (s² + s·ωc/Qtc + ωc²)        ωc = 2π·Fc
```

flat below `Fc` (stiffness-controlled), peaking `Qtc` at `Fc`, −12 dB/oct above
(mass-controlled). So the estimator is one RBJ lowpass biquad:

```
xd(t)  = kXCal · LP2(x; Fc = FsEst, Q = kXQ)     applied to the pre-conv INPUT
xs(t)  = tanh(xd)                                 soft excursion bound (|xs| ≤ 1)
```

- `FsEst` = per-cab resonance from the IR analysis (§4), default **90 Hz**.
- `kXQ = 0.9` (generic sealed-ish guitar cab `Qtc`).
- **Normalization**: RBJ LP2 gain at `Fc` is `Q`, so a full-scale sine parked on
  the box resonance gives `|xd|pk ≈ 0.9·kXCal`. `kXCal = 1.25` calibrates
  `xs → ±1` ("past Xmax") only when the cab is genuinely slammed at resonance;
  a typical loud low note sits ~0.4–0.8; mids/HF produce ≈ 0. **[EAR]**

Slow displacement envelope for coefficient-driving (never audio-rate):

```
dEnv    += (|xd| > dEnv ? aAtk : aRel)·(|xd| − dEnv)    τ = 3 ms / 100 ms
dispPush = clamp01( (dEnv − kDispLo) / (kDispHi − kDispLo) )   kDispLo = 0.08
                                                               kDispHi = 0.50
```

`xs` (signed, instantaneous) drives the intermodulation stage (§3);
`dispPush` (unipolar, slow) drives coefficients (§2). The filter runs whenever
`processPre` is active and is reset on the inactive→active edge, retuned
(state-preserving `copyCoeffsFrom`) when `FsEst` changes.

**Why this is the physics win:** the current block blooms A1 and compresses B3
from the **broadband** envelope, so a loud 3 kHz bend fattens the 90 Hz
resonance — physically wrong. Displacement selectivity fixes that class of error
everywhere at once (guarded by new tests T10/T17).

---

## 2. Item 1 — re-drive the existing stages from displacement

### A1 — Fs resonance bloom (was `Thump·push`, becomes `Thump·dispPush`)

```
g1Db  = min(3.0, kDepthA1 · Thump · dispPush)                (ceiling unchanged)
f0    = FsEst · (1 − 0.12·Thump) · (1 + kA1FsShift·dispPush)   kA1FsShift = 0.06
Q     = (0.9 + 0.9·Thump) · (1 − kA1QDroop·dispPush) · (1 + kA1PromQ·promN)
        kA1QDroop = 0.20,  kA1PromQ = 0.15,  promN = clamp01(promDb / 8)
```

Physics of the two new modifiers: at large excursion the progressive suspension
**stiffens** (`Kms(x)` grows → `Fs = √(k/m)/2π` shifts **up**, `+6 %` max) and
gets **lossier** (`Rms(x)` grows faster than `√k` → effective Q **drops**,
−20 % max). `promN` couples the IR's measured box-bump prominence into the base
Q — a strongly resonant capture implies a high-`Qtc` box. All bounded; the +3 dB
gain ceiling is untouched. **[EAR** for the three new coefficients**]**

### B3 — low-band power compression (was `push`, becomes `dispPush`)

```
grDb   = −(kAgeComp·Age + kEnvComp·Age·dispPush)      clamp ≥ −2.0  (unchanged)
fShelf = clamp(2.2·FsEst, 140, 260) Hz                (was fixed 200)
```

Excursion compression *is* displacement-driven by definition; the shelf corner
now tracks the cab (default `FsEst = 90` → 198 Hz ≈ the old 200).

### B1 — breakup drive gains an excursion term

```
drive = 1 + Age·(1 + kDriveEnv·push + kDriveDisp·dispPush)
        kDriveEnv: 1.2 → 0.9 (rebalanced),  kDriveDisp = 0.8   ceiling ≤ 3.8×
```

Physics: at large LF excursion the surround/spider operate in their stiffened
region, changing the cone's edge boundary condition — bending-wave breakup
starts earlier and harder. This term also produces envelope-rate LF→mid
intermodulation for free. Midband level (`push`) remains a driver — breakup
fundamentally scales with midband excitation. Update the T7c ceiling to 3.8;
the T2b alias budget has ≥ 45 dB of measured headroom (−104 dB at 3.2×), and
tanh harmonic growth from 3.2×→3.8× is marginal.

---

## 3. Item 2 — LF→HF intermodulation delta (new, pre-conv, base rate)

**Physics.** Two mechanisms couple loud lows into the highs, and neither is in
the block (no stage currently cross-modulates):

- **Bl(x) force-factor droop (AM):** motor force is `Bl(x)·i`. At excursion
  extremes `Bl` drops ~symmetrically (`∝ x²` to first order; a worn/offset
  suspension adds an asymmetric `∝ x` term). Everything the cone radiates —
  including HF riding on the LF — is amplitude-modulated at ~2·f_LF (sym) and
  f_LF (asym).
- **Doppler (FM):** HF is radiated from a source moving with LF displacement:
  instantaneous delay `τ(t) = x(t)/c`. At 3.5 mm and 5 kHz the phase index is
  `β = 2π·f·x/c ≈ 0.32 rad` → first sidebands ≈ −16 dBc. Comparable in size to
  the AM for a pushed guitar speaker.

### The sidebands-only two-tap trick (the correctness core)

A naïve modulated delay is **not** transparent at rest: any fixed center delay
`τc > 0` on the HF band, mixed against the dry path, is a static comb — the same
class of bug the Stage B describing-function fix killed. The fix has the same
flavor as B1's "harmonics-only": make the delta **sidebands-only** by
differencing a modulated and an unmodulated tap of the *same* delay line:

```
hp   = HP800(s)                      (shared: band = LP3800(hp) feeds B1)
ring.write(hp)
clean = ring[τc]                                   integer tap, exact read
mod   = ring.hermite(τc + kDop·xs(t))              fractional, displacement-modulated
gAM   = kAMsym·xs² + kAMasym·Age·xs                Bl(x) droop, worn asymmetry
δIM   = (1 − gAM)·mod − clean
s    += Wim · δIM                                  Wim = clamp01(kImAge·Age + kImThump·Thump)
```

Properties (each one is a test):

1. `xs = 0 ⇒ mod ≡ clean` (Hermite at frac 0 must return the stored sample
   bit-exactly — implement as `y1 + frac·(…)` so frac = 0 short-circuits) and
   `gAM = 0`, so `δIM = 0` **exactly**: with macros up but no LF excursion the
   stage adds *nothing* — no static comb, no color (T12).
2. At small index, FM ≈ carrier + sidebands; `mod − clean` cancels the carrier
   and keeps the sidebands, so the dry HF level is untouched and only the
   modulation *products* are added (the τc-offset carrier residue is second
   order in β). Same cancellation the B1 delta uses, in the time domain.
3. Bypass/T1 unaffected: `Wim = 0` when both macros are 0; the whole stage sits
   inside the existing pre-active gate.

### Constants

```
kDopUs   = 10.2 µs        (= 3.5 mm / c; τ swing at |xs| = 1)   [EAR]
kDop     = fs · 10.2e-6   (samples; 0.49 @ 48k — scales with fs)
τc       = ceil(kDop) + 2 (integer; Hermite needs ±2 guard)
ring len = next pow2 ≥ τc + kDop + 4   (allocate in prepare; 16 @ 48k)
kAMsym   = 0.22           (max symmetric droop ≈ −2.2 dB at |xs| = 1)   [EAR]
kAMasym  = 0.12           (Age-scaled asymmetric term)                  [EAR]
kImAge   = 0.6,  kImThump = 0.5
```

Ceilings: instantaneous HF gain deviation `|gAM| ≤ 0.34` (−3.6/+1.0 dB extreme),
delay swing ≤ ±10.2 µs, both further scaled by `Wim ≤ 1`. The δ content rides
~τc (≈3 samples) behind the dry path — irrelevant for additive sidebands; the
dry path is untouched, `latencySamples()` stays 0.

**Aliasing analysis (why base rate is fine):** `xs` is the output of a −12
dB/oct lowpass at ~90 Hz through `tanh`/squaring — effective modulator bandwidth
≲ 500 Hz. Products are `f_HF ± n·f_LF`, confined to a few hundred Hz around
existing HF content; only content already within ~500 Hz of Nyquist can fold,
with sub-permille depth. No oversampling. Guarded by the T13 grid test.

---

## 4. Item 3 — per-cab resonance from the IR (`LfResonance.h`, JUCE-free)

New tiny header `src/rig/LfResonance.h` (no JUCE — testable offline, included
by `IrAnalysis.h`):

```cpp
struct LfEstimate { float fsHz; float promDb; bool valid; };
LfEstimate estimateLfResonance(const float* respDb, int nPts,
                               float fLo, float fHi);   // the 1/6-oct grid
```

Operates on the existing 200-pt mean-centred log-spaced curve (40–8000 Hz) from
`IrAnalysis::computeResponse`:

1. One binomial (1-2-1)/4 smoothing pass (kills residual comb hash).
2. Search **45–230 Hz** for the highest *interior* local max (strictly greater
   than both neighbors). Edge max ⇒ invalid.
3. **Prominence** `promDb` = peak − mean(260–600 Hz zone). Require the response
   to *fall below* the peak on the low side (mean of the octave below the peak
   ≥ 1.5 dB down) — distinguishes the box knee from a mic-proximity shelf.
4. Parabolic refinement of the peak position in log-f.
5. `valid` ⇔ interior peak ∧ promDb ≥ 1.0 ∧ low-side fall ≥ 1.5 dB.
   Clamp `fsHz` to [45, 200], `promDb` to [0, 12].

Honest statement of what this measures: the **captured system's** LF resonance
(driver-in-box as mic'd, proximity and all) — which is exactly the frequency
the thump/excursion behaviour should key on, even when it differs from the
driver's free-air `Fs` datasheet number.

### Consumption

```cpp
// CabDynamicsBlock (JUCE-free, <atomic> is std):
void setSpeakerResonance(float fsHz, float promDb);   // message thread, atomics
void clearSpeakerResonance();                          // back to 90 Hz / 0 dB
```

Picked up at the control-rate boundary in `processPre`; on change, retune
(state-preserving) the excursion LP2, A1 base center, B3 shelf corner, and the
modal centers (§6). Changes only happen on IR load — rare, and coincide with a
wholesale sound change.

Wiring: `CabBlock::loadIr` already computes `mResDb` on the message thread —
add the estimate there + a getter; every `loadIr` call site pushes the estimate
to the **matching lane's** `cabDyn` (A→`cabDyn`, B→`cabDynB`) via a small
`RigChain` helper so call sites can't get the pairing wrong; IR cleared/failed ⇒
`clearSpeakerResonance()`.

---

## 5. Thermal voice-coil compression (research-grade, cheap)

**Physics.** Voice-coil copper heats with dissipated power (τ ≈ seconds; the
magnet/former minutes — out of scope) and `Re` rises +0.39 %/°C, dropping
sensitivity a couple of dB. This is the multi-second "sag/settle" a static IR
cannot do, and it is compression only — never restored, so it honors the
no-compensation rule.

```
pwr    += kT · (x² − pwr)          one-pole on the INPUT, τ = 3.5 s (attack = release)
pwrN    = clamp01(pwr / 0.5)       0.5 = full-scale sine steady-state power
droopDb = −kThermDb · pwrN · Age   kThermDb = 1.5   [EAR]
```

The integrator reads the pre-delta **input** `x²` (terminal-voltage proxy), not
the block's own output — no self-feedback path by construction.

Applied as a **series broadband gain** `s *= dbToLin(droopDb)` folded into the
Stage A series section (before B1's band split, so the breakup sees the sagged
signal — physically the motor drive itself droops). Broadband series gain
cannot comb. At `Age = 0`, `droopDb = 0 ⇒ gain = 1.0f` exactly ⇒ bit-exact
bypass preserved. Rebuilt at control rate; `pwr` moves over seconds so there are
no sidebands by construction. Reset with pre-state (re-engage = cold coil).
Scaled by **Age** (worn rig = the sag feel; also keeps the macro map at 3).
Simplification, documented: the droop does not feed back into the excursion
estimate (second-order effect).

---

## 6. Modal shaping of the breakup exciter (research-grade, cheap)

**Physics.** Real cone breakup is not spectrally flat grit — bending waves hit
discrete modal resonances (12″ paper: clustered ~1–4 kHz), so the "cry" has
frequency structure. Full modal synthesis inside the nonlinearity would break
B1's describing-function fundamental cancellation. But the **δ is already
harmonics-only** — series-filtering it afterwards can shape its spectrum and
**cannot reintroduce a fundamental comb** (there is no fundamental in it):

```
δ' = HS5k( M2( M1( δ )))        applied at base rate, after mHb.down()
M1 = peaking(2050·mScale, Q 2.8, +3.5 dB)      [EAR]
M2 = peaking(3350·mScale, Q 3.5, +2.5 dB)      [EAR]
HS5k = highshelf(5 kHz, −1.5 dB)               keeps fizz polite   [EAR]
mScale = (FsEst < 65 Hz) ? 0.75 : 1.0          bass cab ⇒ bigger cone ⇒ lower modes
```

Coefficients fixed except on `FsEst` change. Filters live inside the existing
`Wb > 0` branch, reset with pre-state. T9 (comb-free) and T2b (alias bound) are
re-run over the shaped exciter as regression guards; T3 covers the +3.5 dB peak
against the stability bound.

---

## 7. Macro map delta (still exactly 3 macros, all default 0 = bit-exact)

| macro | new/changed targets |
|-------|---------------------|
| **Age** | breakup drive `+0.8·dispPush` term; IM engage `0.6·Age`; IM asymmetry `kAMasym·Age`; thermal droop `·Age`; (unchanged: B3, A2 ease, Wb) |
| **Thump** | A1 now `Thump·dispPush` (+ f0/Q excursion modifiers); IM engage `0.5·Thump`; (unchanged: Stage C couplings) |
| **Cab Size** | unchanged (Stage C only) |

Stage C is untouched this pass (it is already [EAR]-gated and post-conv has no
excursion model; its broadband-env drive stands).

## 8. Ceilings (additions to DESIGN.md §6)

| stage | quantity | max swing | driver |
|-------|----------|-----------|--------|
| A1 | f0 shift / Q droop | +6 % / −20 % | dispPush |
| B1 | drive | ≤ **3.8×** (was 3.2×) | Age·(push, dispPush) |
| IM | instantaneous HF gain | −3.6/+1.0 dB (`|gAM| ≤ 0.34`), ×Wim | xs, xs² |
| IM | Doppler delay swing | ±10.2 µs | xs |
| thermal | broadband droop | **−1.5 dB**, reduction only | pwr·Age |
| modal | exciter shaping | +3.5 dB peak, −1.5 dB shelf | static |

## 9. New verification (extend `tests/cabdyn_test.cpp`, JUCE-free)

T1–T9 must still pass unmodified in *meaning* (T7c ceiling 3.2→3.8). New:

| test | asserts |
|------|---------|
| **T10** | excursion selectivity — full-scale 60 Hz ⇒ `dispPush ≥ 0.8`; full-scale 2 kHz ⇒ `≤ 0.05` (broadband-envelope regression killed) |
| **T11** | IMD engages — 100 Hz @ 0.85 + 4.5 kHz @ 0.15, Thump 1/Age 0.6: sidebands 4.5 k ± 200 in **[−48, −14] dBc**; LF removed ⇒ < −70 dBc (no self-mod) |
| **T12** | IM adds no static color — Thump 1/Age 0, quiet 3 k + 8 k tones: pre-path response within **0.1 dB** of bypass (the two-tap zero-at-rest property) |
| **T13** | IM alias grid — Thump 1/Age 0, 95 Hz + 4.7 kHz: all energy off the `|m·f_HF ± n·f_LF|` grid (m ≤ 1, n ≤ 8) < **−55 dBc** |
| **T14** | thermal — 6 s full-scale **sine** @ Age 1 (steady `pwrN = 1`, expected ≈ 1.23 dB): droop in **[1.0, 1.5] dB**, monotonic (ripple tol); 6 s silence: recovers < 0.3 dB |
| **T15** | Fs estimator — synthetic 78 Hz guitar curve ⇒ valid, ±12 %; 52 Hz bass curve ⇒ valid, ±15 %; flat/hash curve ⇒ invalid (falls back 90) |
| **T16** | resonance plumb-through — `setSpeakerResonance(52, 8)`: stable at max settings, T9 comb sweep still < 1.2 dB ripple (modal rescale safe) |
| **T17** | A1 keys on displacement — fs 60: full-scale 60 Hz ⇒ `dbgA1Db ≥ 1.5`; full-scale 400 Hz ⇒ `< 0.3`; `dbgA1CenterHz` tracks the shifted f0 |

New debug hooks: `dbgDispPush() dbgXs() dbgThermDb() dbgFsEst() dbgA1CenterHz()
dbgImWet()`.

## 10. Explicitly out of scope (and why)

Full Klippel motor state-space (implicit solver in the audio loop; not
expressible as a delta; CPU + stability risk). Vented-box displacement null at
`Fb` (needs port model; sealed LP2 is the honest generic). LF self-distortion
from `Kms(x)` acting on the low band itself (parallel band-limited fundamental
delta ⇒ the B1 comb trap all over again; revisit only with its own
describing-function treatment). Port turbulence noise. Magnet-scale thermal
(minutes).
