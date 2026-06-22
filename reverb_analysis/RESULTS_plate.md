# RESULTS — Plate vs vintage-plate reference (full battery, 2026-06-21)

Reference: a vintage-plate IR set, 10 decay-knob settings (0.5–4.5). **Brand-neutral**
— no product names anywhere. Filenames staged as `ir/vintage-plate-<knob>s.wav`.
Knob label is NOT measured RT60: measured T30@1k runs 0.81s (0.5 knob) → 3.90s
(4.5 knob), with a classic low-mid bloom and HF roll-off throughout.

Our render: shipped `ReverbBlock` plate via `render_character` (no drift). Bracketed
at RV_T60 = 1.9 / 2.9 / 3.4 against the reference's 1.0 / 2.0 / 3.0 knob settings
(measured 1.87 / 2.93 / 3.42 s @1k). Battery + an independent ISO-3382/Lundeby
cross-check (`validate_battery.py`).

## Verdict: strong match at mid decay, decay-dependent drift at the extremes

### What matches (and survives the strict cross-check)
- **Decay surface T30(f) at the mid setting**: near-perfect overlay. Battery
  mean|ours−ref| = 0.08s; strict (LSQ slope + Lundeby) = 0.17s. Classic shape
  reproduced: lows ring ~4.5s, 1k ~2.9s, HF ~1.8s.
- **Echo density**: ~1.0 by 40ms like the reference (dense, non-grainy onset).
- **Modal depth**: 7.0 vs 7.5 ref — lush, not metallic/digital.
- **Tonal balance**: within ~1dB mean at the mid setting.
- Cross-check confirms the battery's core-band numbers are trustworthy (250 Hz–
  2.8 kHz agree within ~0.1s between the two methods).

### Real gaps (priority order)
1. **Low-mid bloom does not scale with the decay knob.** The reference deepens its
   low bloom as decay rises: 125 Hz/1 kHz decay ratio 1.14 (short) → 1.45 (mid) →
   2.08 (long). Ours is fixed at ~1.5× regardless. So short settings over-bloom the
   lows; long settings under-bloom (3.4s render sits ~2s short at 125 Hz vs the
   3.0-knob reference). Engine has a fixed bloom multiplier where it needs a
   **decay-scaled** one.
2. **Tone is fixed where the reference darkens with decay.** Reference centroid
   5031 → 4024 → 3630 Hz across the range; ours ~4500–4800 throughout. Long settings
   read too bright/thin. Tying damping to decay would add low-mid body, pull centroid
   down, AND nudge modal depth up — three gaps with one change. (This reference is
   ~darker than the older METRICS worked-example reference, centroid ~4.0k vs ~6.7k
   @ the 2.0 setting — so the old v3 brightness target was tuned to a different capture.)
3. **Onset not front-loaded enough** (EDT longer, C80 ~3.5dB lower across all
   settings; survives the strict method). Structural FDN-vs-plate trait — a real
   plate is instantly dense (two-slope onset). METRICS flags this as partly an
   ears call: a hot early tap to chase C80 drives the lows anti-phase. Treat as
   voice-by-ear, not a metric to force.
4. Minor: ours ~1–2dB wider in the 62–125 Hz band; reference is narrower in the lows.

### Rigor notes from the cross-check (`validate_battery.py`)
- Decay-curve nonlinearity ξ = 1000(1−r²): ours clean (<1.5 in-band). Reference
  ξ high at 62 Hz (13.5) and 11 kHz (7.2) → **band-edge decays are unreliable in the
  reference itself.** Don't over-chase the sub-bloom / air-decay differences; weight
  the 250 Hz–4 kHz bands.
- Battery vs strict diverge only at the band edges (whole-window integration +
  broad RBJ BPF vs brickwall + Lundeby). Core verdict identical either way.

## Next
- Decay-scaled low-mid bloom + decay-linked damping is the one structural change
  that closes gaps 1+2 (and helps 3's modal side). Relax brittle asserts rather
  than contort the engine. Re-A/B by ear after.

## Voicing pass — graph-driven match at the all-rounder (2026-06-21)

Target locked = the **1.5 knob (2.45s @1k)**, chosen as the most versatile guitar
plate. Matched purely against the battery curves (ears out of the loop, by request),
with L/R correlation kept as a mono-safety constraint only. Iterated on the live
`PlateFdn` in `ReverbBlock.h` (diff saved). Changes:
- early-tap gain 0.30 -> 0.56 + a new `mEarlyCorr` (0.5) that blends a correlated x
  into the right early tap -> front-loads the attack AND narrows the over-wide lows
  without going anti-phase (L/R corr -0.07 -> +0.04).
- presence bell 2800/Q0.6/+3 -> 3300/Q0.5/+4.2 (lifts the dull 2-6k).
- input driver darkened (+3300 -> +2700 offset) -> pulls centroid/lushness back after
  the damping re-fit, decay untouched.
- damping re-fit kDampBb -2.4367e-4 -> -2.66e-4, kDampG1 -1.8362e-4 -> -1.61e-4,
  kDampG2 -4.1266e-4 -> -3.29e-4 -> flattens the too-steep tilt (shorten lows, lengthen
  top, 1k pinned).

Result at 2.45 vs the 1.5 reference (baseline -> matched):
- T30(f) mean err 0.19s -> **0.09s** (curve overlays top to bottom).
- C80 gap ~-3.9dB avg -> matched 500 Hz-4 kHz within +/-0.5dB (low-mids 125/250 still
  -2.9/-4.2 = the structural FDN floor METRICS says not to force).
- tonal balance: 4k/8k now dead-on; 125 Hz ~-2dB light (the only out-of-tol band).
- centroid 4671 vs 4560 (within 2%); modal depth 7.0 vs 7.4 (inside the +/-1dB window);
  echo density matched; L/R corr +0.04 (mono-safe).

Decay scaling (unchanged, still the open structural item): all-rounder 0.09s, 2.0 knob
0.12s, but short 0.26s and long 0.74s — the reference's bloom deepens with the decay
knob faster than our fixed-ratio engine. Next structural task = decay-scaled bloom.

Files: voiced render `ours_final_2.45.f32`; graphs `plate_battery_base2.45.png` ->
`plate_battery_final_2.45.png`; header diff `outputs/plate_voicing.diff` (review +
commit on Robbie's side — sandbox can't commit). Original header backed up.

## Voicing pass 2 — frequency-split early energy (2026-06-21)

Closed the per-band gaps Robbie spotted on the graphs (EDT-lows, C80-lows, side/mid)
with ONE structural change: split the early tap by frequency. LOW band (<360 Hz)
goes correlated to both channels -> instantly dense + NARROW lows (front-loads the
low-mid C80/EDT and matches the reference's narrow low end). HIGH band stays mostly
decorrelated (partial mHighCorr=0.35) -> WIDE mids/highs, mono-safe. New members:
mEarlyLowGain 1.20, mEarlyLpK (~360 Hz split), mHighCorr 0.35 (replaced mEarlyCorr).

Result at 2.45 vs the 1.5 reference:
- EDT(f) lows: was +0.95/+0.49/+0.76 (62/125/180) -> +0.45/+0.09/+0.44 (125 ~exact).
- C80 low-mids: 125 Hz was -2.9 -> +0.3; 250 -4.2 -> -2.0; broadband -1.5 vs -0.5.
- side/mid: now tracks the reference SHAPE (narrow lows climbing to wide mids);
  L/R corr +0.01 (mono-safe), was drifting to -0.10 before the high-band re-correlation.
- T30 mean err 0.08s, modal 6.96 (lush/pass), centroid within 2%, echo density matched.
- echo-density GRAPH looks jagged but that's per-frame estimator noise; scalar 1.01 vs 1.01.

Compact scorecard (Robbie's old format) now passes/close on every line. The metallic
test = modal depth (now shown in `plate_metallic_scorecard.png`). Remaining minor:
250 Hz C80 -2.0, 8k C80 -1.9, 500 Hz width slightly narrow, 125 Hz tonal -2dB, modal
-0.5 — all small/structural. Decay-SCALING across the knob range is still the open
item (short 0.25s / long 0.75s T30 err). Diff updated at `outputs/plate_voicing.diff`.

## Voicing pass 3 — C80 micro-shape (250 peak / 500 dip) (2026-06-21)

Robbie asked to chase the reference's sharp C80 resonance. Added an EARLY-PATH-ONLY
shaper (tail tonal balance untouched): HF early emphasis (mHfEarly, ~2.5kHz, width-
preserving via the freq-split) for the 8k rise + two narrow biquads on the early
excitation — mEReso1 peaking 250 Hz Q4.2 +5dB, mEReso2 peaking 500 Hz Q3.0 -3.8dB —
and eased mEarlyLowGain 1.30->1.10 so the early-clarity peak sits at 250 not 125.
Result: C80 now follows the reference SHAPE across all 7 bands, every band within ~1dB
(worst: 250 -1.0, 8k -0.8); 500 dip exact, 125/1k/2k/4k within ~0.4dB. Width preserved
(L/R corr +0.00), centroid/modal/T30/tonal all intact. The residual 250 (-1dB under the
ref's isolated spike) is the floor — pushing the narrow boost harder starts to sound
artificial. NOTE: these resonance constants are fit to THIS capture's mechanical mode;
they're the most "over-fit" part of the voicing — fine for shipping a faithful 1.5-knob
plate, but flag if re-targeting a different reference.

## C80 8k band (2026-06-21)
Pushed the HF early emphasis (corner 2.5k->5.2k so it targets 8k not 4k; mHfEarly
0.45->1.15). 8k C80 -0.8 -> -0.14 (ours +0.96 vs ref +1.09). FULL C80 curve now within
~0.4dB on ALL 7 bands (125 +0.09, 250 -0.37, 500 +0.04, 1k -0.34, 2k -0.22, 4k +0.11,
8k -0.14). Width/centroid/modal/T30/tonal intact, L/R corr +0.00. Note: 8k needed a
strong early HF boost because our 8k TAIL is bright (damping re-fit) so lots of late 8k
energy fights the clarity ratio — the early boost outweighs it. Final render `ours_w12_2.45.f32`.

## Width tightening — mid-band side sculpt (2026-06-21)
The 500-1k width was too NARROW and 2k too WIDE (reference width peaks at 500-1k then
notches at 2k; ours peaked at 2k). Added OUTPUT side-only sculpting (mid untouched, so
C80/T30/modal/tonal unchanged): a 460-1150 Hz side WIDENER (mMidWidth 0.20) + a
1500-2700 Hz side NARROWER (mNarrow2k 0.22). Result: side/mid now within ~0.5dB on ALL
7 bands (was 500 -0.91, 2k +0.78). L/R corr +0.01 (mono-safe). Because the sculpt is
+side to L / -side to R, the mid M=(L+R)/2 is untouched -> every mid-signal metric is
bit-identical. Final render `ours_x3_2.45.f32`.

## EDT-low + side/mid fine-tune (2026-06-21)
SIDE/MID: upgraded the 2k side-narrower from a broad one-pole-diff to a 2nd-order
biquad bandpass (HP 1550 + LP 2650, Q0.9) -> steep skirts isolate 2k from 4k. Now
side/mid within ~0.14dB on ALL 7 bands (was 500 -0.5, 2k +0.4). EDT-LOW: lows ran long
(EDT 62 +0.49, 180 +0.31; T30 lows +0.15) -> nudged broadband damping kDampBb
-2.66e-4->-2.82e-4 (shorten lows), relengthened mids via kDampG1 -1.61->-1.46 &
kDampG2 -3.29->-3.20, and eased low-cut 108->88 Hz to keep 125 tonal body. Result: T30
mean 0.08->0.05s, EDT 62 +0.26 / 180 +0.12 (sub-62 is noisy in the ref - xi 13.5).
Costs: modal 7.0->6.9 (still in-window), centroid +3.5%. Final render `ours_x12_2.45.f32`.

## 2k tonal notch + EDT limit (2026-06-21)
TONAL: the 5th point (2k) ran +1.5dB hot (presence-bell skirt). Tried shifting the
presence up (fixed 2k but dropped modal 6.9->6.7 + brightened) -> reverted; instead
added a NARROW tail dip biquad at 2k (peaking 2k Q2.4 -1.9, mDip2kL/R, post-presence).
2k tonal +1.46 -> +0.5; spec err mean 0.90 -> 0.69. Static EQ so C80 unchanged. Modal
6.7 (still lush, >6 = not metallic). EDT FLAT LOW-MID PLATEAU + high 11k: STRUCTURAL
LIMIT. The flat 250-710 EDT is the flip side of the matched C80-low front-loading -
easing earlyLow (1.10->0.88) sloped the EDT up but craters C80-250 (-0.2 -> -1.6) and
relengthens 62-250. Our FDN+early-tap can't reproduce the real plate's high-C80 +
gradual-early-decay at once. Left C80 matched, EDT plateau accepted. 11k EDT high is
coupled to the HF-early boost (needed for 8k C80). Final render `ours_final2_2.45.f32`.

## BANKED — plate 1.5-knob match final (2026-06-21)
Resonant-pocket EDT attempt REVERTED (parallel resonators add a t=0 spike + 6 narrowband
tonal bumps; doesn't work as an add-on). The flat low-mid EDT plateau is the one accepted
residual = our smooth FDN vs a real plate's uneven modal clusters; proper fix = make the
FDN modes uneven (detune the 32 line lengths + lighten diffusion) = a core rebuild that
re-opens the whole match. Tracked as a FUTURE structural task alongside decay-scaling.
FINAL all-rounder match (2.45 s): T30 mean 0.06s, C80 within ~0.4dB all bands, tonal err
0.69dB, side/mid <0.15dB all bands, modal 6.7 (lush), L/R corr ~0.0. Engine = final2 state,
diff `outputs/plate_voicing.diff` (107 lines, UNCOMMITTED - Robbie commits). Render
`ours_PLATE_FINAL_2.45.f32`. Graphs in outputs/plate_final/. NEXT = decay-scaling.

## Decay-scaling — cross-range bloom + tone (2026-06-21)
THE cross-range fix. Reference bloom (125/1k decay) ramps 1.03 short -> 2.63 long and tone
darkens (centroid 6242 -> 3405); ours was FLAT (ratio ~1.4, centroid ~4800) -> short over-
bloomed, long under-bloomed + too bright. FIX in updateGeometry: make the damping SPLIT
decay-dependent while holding the 1 kHz damping constant so the knob still reads true seconds.
  - kD1k = kDampBb + kDampG1*kA1k (kA1k=0.70 calibrated) = the pinned 1k damping.
  - Rspl(T60) cubic (0.12412,-0.635407,1.20743,0.392912), clamp[1.0,3.05], EXACT through the
    committed all-rounder at 2.45 (=1.362) so the banked 1.5-knob match is preserved bit-for-bit.
  - bbEff = kD1k/Rspl (lows: less damping long -> ring longer); g1Eff=(kD1k-bbEff)/kA1k (keeps 1k).
  - g2Eff = kDampG2*(T60/2.45)^1.3 (top darkens with decay -> centroid tracks).
RESULT: 1.5 anchor PRESERVED (T30 0.06s, spec err 0.69dB - identical to banked). Usable range
0.5-3.0 knob now T30 mean 0.06-0.22s (was up to 0.75 @3.0, 0.25 short). Bloom + centroid track
the reference across the knob. LIMIT: extreme long 3.5-4.5 still saturate (T30 0.4-0.5, max ~2.0)
- the ref rings ~10s @125Hz at 4.5, beyond the FDN's bloom capacity; these are special-purpose
ambient settings. Graphs: outputs/plate_decayscale/. Diff now 135 lines, UNCOMMITTED.

## Decay-scaling tone refinement — anchor air trim (2026-06-21)
The decay-scaled centroid tracked the SHAPE but ran a ~constant +300Hz (+6-9%) bright
across the knob = the banked anchor's own >8k air. Robbie chose "darken the anchor".
Added a global output high-shelf air trim (mAirL/mAirR = highshelf 10kHz, -1.7dB, Q0.7,
post-2k-dip) -> drops the whole centroid curve onto the reference. RESULT: centroid now
within +/-3.5% across ALL knobs (anchor 4539 vs 4560 = -21Hz, was +252; mid +100/+2.7%;
short -218/-3.5%). Anchor banked match held: T30 0.05s, modal 6.8 (up from 6.7), spec
err 0.77dB (vs 0.69 banked - shelf skirt grazes the 8k octave slightly). Shelf at 10k
(not 8k) to spare the octave tonal. DECAY-SCALING COMPLETE: bloom tracks 0.5-3.0 (+/-0.14),
tone within +/-3.5% across the knob, T30 0.05-0.24s usable range (was up to 0.75). LIMIT
unchanged: extreme-long 3.5-4.5 bloom saturates (engine capacity, ref rings 10s@125Hz).
Diff 138 lines, UNCOMMITTED.

## Residual A investigated — extreme-top 11k ring is MODAL, not EQ-fixable (2026-06-21)
Re-verified the whole current state reproduces the handoff first: anchor RV_T60=2.45 ->
T30 0.05 / spec 0.77 / modal 6.8 / centroid 4539 (byte-identical render across runs), and the
across-knob T30 table (allband 0.44/0.58/0.55 at 3.5/4.0/4.5; 180-8k 0.23/0.25/0.22). Good.
QUANTIFIED Residual A: ref 11k T30 ramps 1.64(1.5)->2.40(3.0)->4.30(3.5)->5.95(4.0)->6.60(4.5);
ours is FLAT ~1.65 across the whole knob. The 11k band is NOT noise-floor junk - at knob 4.5 its
RMS is only -2.7dB vs 1k, so it's a real sustained extreme-top ring (plate "sizzle").
ATTEMPT (damping path, the sanctioned half - NOT mAir; an output shelf changes a band's LEVEL not
its DECAY so the handoff's mAir idea couldn't have worked): added mHs3, a per-line decay-activated
POSITIVE high-shelf (g3Eff = A*dd + B*dd^2, ZERO at the 2.45 anchor, loss-bounded), env-tunable for
fitting. Anchor render stayed BYTE-IDENTICAL (term provably 0 at dd=0). Swept A and corner
9k/12k/14k/16k at knob 4.5.
RESULT = STRUCTURAL FAIL, two independent walls:
  1) CENTROID: even A=2e-4 (11k barely moves +0.06) overshoots centroid 3349->3946 (target 3405);
     A=6e-4 gives 11k only ~1.8-2.1 but centroid 5600-6400. A broadband top shelf lifts ALL the top
     energy, wrecking the matched tonal balance, while 11k barely responds (it's broadband-damped).
  2) STABILITY: A>=1e-3 the loop runs away - 1k balloons to 15s, centroid 16k, peak explodes. The
     FWHT cross-mixing spreads the near-unity top-band gain into 5.6k/1k. No stable operating point
     reaches 11k=6.6s while keeping centroid~3405 and the loop stable. Higher corners (16k) isolate
     11k a little but don't change the verdict.
ROOT CAUSE = SAME AS RESIDUAL B. The ref's 6.6s 11k-ring-under-a-dark-3405-centroid is a SPARSE
MODAL trait (a few high modes ringing long while the broadband top stays damped). A broadband
shared-feedback FDN + EQ damping cannot sustain ONE isolated band without moving all bands + the
centroid. Same physics as the flat low-mid EDT plateau. => Residual A FOLDED INTO Residual B
(the FDN modal-pocket rebuild). REVERTED mHs3 fully: git diff byte-matches the saved
plate_voicing.diff (138-line state), CR=LINES=1742, restored anchor render byte-identical to
pre-experiment. The shipped/matched engine is untouched. A stays correctly LOW-priority
(barely audible 11k at a 4.5 wash); the only real fix is the B rebuild.

## Residual B prototype — modal-pocket FDN: EDT PLATEAU CLOSED (proof of concept, 2026-06-21)
Recommended path = PROTOTYPE OFF THE SHIPPED HEADER (Robbie: "what is the right way?" -> don't gamble
the banked match). Built an isolated proto: forked src/rig/{ReverbBlock,Blocks,Lfo,Biquad}.h into
plate_proto/inc/, built render_character against -Iinc -Istub; baseline render BYTE-IDENTICAL to live.
(platefdn_driver.cpp is STALE - maxdiff 0.063 - don't use it as baseline.) Full recipe + winning
params persisted in reverb_analysis/PLATE_MODAL_POCKET_PROTO.md (plate_proto/ is gitignored/clears).
Target quantified: at the 1.5 anchor the ref's low-mid EDT humps (250/355/500/710 = 2.48/2.59/2.52/
2.47) while ours sat FLAT at 2.40 - the smooth monotonic 32-line spacing + heavy dispersion can't
make the uneven modal pockets.
exp1 (lighten dispersion kDispG 0.62->0.45, kDispG2 0.55->0.40): lifted the MIDDLE of the plateau
(355 2.39->2.51, 500 2.39->2.45) but not the 250/710 edges; modal dipped 6.8->6.6; T30err 0.053->0.068.
exp2 (exp1 + irregular kLineMs jitter, numpy seed7 +/-6% on the original lengths -> uneven gaps =
pockets): **EDT 250-710 now 2.42/2.54/2.52/2.44 ~ ref 2.48/2.59/2.52/2.47 - PLATEAU CLOSED.** T30err
0.058 (held), centroid 4495 (ref 4560), modal 6.8 (lush, restored), L/R corr -0.03. NO t=0 spike, NO
tonal bumps (unlike the failed parallel-resonator). This is the long-standing "accepted residual"
beaten in principle.
COST (the "re-opens the match" warning, now concrete): the detune moved voicing over-fit to the OLD
lines - C80 125 -0.77->-2.11 (~1dB regress), side/mid d 0.15->0.7dB (125/8k), NED@40ms 0.97->0.91.
All RE-FITTABLE (re-tune mEReso1/2/3 + side widener to the new modes).
RESIDUAL A NOT incidentally fixed: 11k stayed flat ~1.67 at long knobs (ref ramps to 6.6). The +/-6%
jitter doesn't make the sparse long-ringing high mode; A needs targeted SHORT-line detune or a
dedicated lightly-damped high-mode line. Remaining rebuild (multi-session): optimize the jitter
(seed/mag arbitrary), re-fit the voicing, validate all 10 knobs, ear A/B, THEN port to the live CRLF
header. Shipped engine UNTOUCHED this session (byte-matches saved plate_voicing.diff).
