# Tone Stack Emulation -- Research & Design (2026-07-11)

Circuit-exact tone stacks for NAM Rig: one `TonestackBlock` per amp (A/B),
placeable pre or post the NAM capture, plus the Mark-style 5-band graphic as a
post section. DSP + offline harness are DONE (`tests/tonestack_test.cpp`,
ALL PASS); processor params / RigChain wiring / UI are NEXT SESSION.

## 1. Why this works (and what its limits are)

Passive tone stacks are linear RC networks: their transfer function is a
low-order rational H(s) whose coefficients depend on pot positions. That means
EXACT emulation, not approximation (Yeh & Smith, DAFx-06, for the Bassman
stack). What cannot be exact around a static NAM capture:

* Placement: the capture has the amp's own stack baked in at capture settings,
  sitting between its real stages. Ours goes PRE the capture (shapes what
  drives the nonlinearity -- feels most amp-like, default) or POST (pure EQ).
  Captures made with controls at noon respond most authentically.
* Presence/resonance: power-amp NFB controls, NOT tone stack -- out of scope.
* Mark graphic band-to-band bus interaction: approximated by cascading
  (centres/Q/range/taper exact; the interaction term is subtle).

## 2. The engine: netlists + runtime identification

No hardcoded per-topology formulas. Each model is a NETLIST (element table).
On control change (64-sample slices, de-zippered):

1. solve the <= 8-node complex MNA at 13 log-spaced frequencies (0.5 Hz -
   21 kHz; the LF points matter -- see 8.3);
2. fit the known-order rational exactly (column-scaled MGS QR on the
   real-stacked system, normalised s' = s/2pi*1kHz);
3. SMALLEST SUFFICIENT ORDER: ascending order search, accept the first fit
   that is Hurwitz (Routh), has a1 <= 5e3 (no sub-audio spurious poles), and
   reproduces the circuit at held-out frequencies within 0.05 dB; otherwise
   commit the best safe candidate (never observed to be needed above 0.053 dB);
4. bilinear transform, c = 2fs (Yeh mapping, DC-exact), transposed DF-II in
   doubles, states zeroed on order flips.

Adding an amp = adding a table row in `spec()`. Cost: 0.43% of one core while
a knob is gliding on the heaviest model (5E3), 0.03% idle (10 s bench, -O2).

## 3. FMV family -- ONE topology, TWO wirings, 12 amps

Topology settled by matching the DAFx-06 printed coefficients to 4e-15
(machine precision) over 120 random (t,m,l,f) points:

    in --C1-- T --(1-t)R1-- OUT --tR1-- K ; in --R4-- J ;
    J --C2-- K ; K --l*R2-- M ; M --(1-m)R3-- W ; J --C3-- W ; W --mR3-- gnd

KEY SUBTLETY the common implementations miss: the bass cap C2 sits BETWEEN the
slope junction and the treble pot bottom (Rob Robinette's analysis narrates
exactly this: the treble filter grounds "through the bass and mid CAPS around
the bass pot").

* `FmvPot` (above): mid pot as potentiometer, C3 to its wiper -- 5F6-A /
  Marshall-family wiring. Matches Yeh exactly.
* `FmvRheo`: blackface-family mid wired as VARIABLE RESISTOR (input+wiper
  jumpered): C3, bass bottom and the dialled mR3 share one node. A genuinely
  different circuit -- up to 28% response delta vs applying Yeh's formula, and
  it reproduces the documented all-controls-down DROPOUT quirk (harness T4).
  CAPS/Faust/guitarix apply the pot formula to every amp; we don't.

SOURCE AND LOAD ARE PART OF THE MODEL. Yeh idealised them ("unaffected" per
his noon-settings SPICE); at bass-heavy settings the PI's 1 M grid-leak load
alone costs 2.4 dB at 40 Hz (python MNA, section 9). CF-fed stacks get
Rs = 1.3k (tweed Bassman, Marshalls, SLO...), plate-fed get 38k (blackface
family, Mark, Vox TB), solid-state get 100R. Marshalls load into 517k
(guitarscience's PI figure), others 1M.

Component values (C1/C2/C3, R1 treble/R2 bass/R3 mid/R4 slope) follow the
Duncan-TSC/CAPS lineage cross-checked against the DAFx-06 paper and the
guitarscience Marshall page: tweed 5F6-A (250p/20n/20n, 250k/1M/25k/56k,
treble+mid LINEAR, bass log -- per the paper), blackface AB763
(250p/100n/47n, 250k/250k/10k/100k), Twin AA270 (120p treble cap variant),
Mesa Mark TMB (blackface-like w/ 25k mid), JCM800 2203/2204 (470p/22n/22n,
220k/1M/22k/33k -- 25k-mid variants exist), JTM45 (270p, 33k slope -- the
"copied the schematic, not the amp" stack), Major Lead (500p), SLO-100
(470p/20n/20n, 47k slope), Sovtek MIG-100H, Peavey C20, Roland Cube-60
(ships as the honest SS-clean flavour -- NOT claimed to be a JC-120),
Vox Top Boost (50p/22n/22n, 1M/1M pots, fixed 10k mid, m pinned = TSC's Vox
model) plus the CUT control (section 5).

Tapers: LogA = 10%-at-half (81^x-1)/80, LogB = 15%-at-half, per model in the
table. Mid pots linear everywhere; Fender treble/bass audio; Marshall bass
LogB (guitarscience anchor), treble/mid linear.

## 4. James / passive Baxandall (kJames)

Orange Graphic MkII (1972) values recovered by reconciling ampbooks' two
pages, whose R-numberings CONTRADICT each other (their DSP page's "R2" is the
analysis page's "R3"). Settled topology (validated against all four published
Bode anchors -- 59/329 Hz bass poles, ~-15 dB mid plateau, 130/593 Hz
controls-min transitions):

    IN --R1(100k)-- A ; bass pot 1M: A --(upper||C1 2n2)-- W --(lower||C2 22n)-- B --R2(22k)-- GND
    W --R3(100k)-- OUT(=treble wiper) ; IN --C3(1n5)-- T1 --(1-t)RT-- OUT --tRT-- T2 --C4(10n)-- GND

Source 49k (12AX7 with 220k plate load), load 1M. This is the stack that can
do a MID HUMP (both controls low) -- the FMV can't. Harness T6 checks the
scoop at ~228 Hz and that bass-min genuinely kills lows (-35 dB DC), which
only this wiring produces.

## 5. Vox CUT (part of kVoxTB)

The real control lives ACROSS THE PHASE INVERTER outputs: 250k lin pot in
series with 4n7 bridging the anti-phase plates. Exact 2-node MNA of the
differential bridge (plate Zs 35k/38k = 82k/100k plate loads || ra, 220k grid
leaks behind 100n coupling), solved symbolically; H(s) is 3rd order with
coefficients LINEAR in pot fraction x (hardcoded pairs in `cutAnalogCoeffs`).
Authentic behaviour: knob UP = darker; ~21 dB of 10 kHz range, lows untouched,
near-transparent fully open (harness T8).

## 6. Mark 5-band graphic (post section, any model)

Real per-band series RLC from the Mark schematics: R/L/C = 470R/1H/3.3u,
470R/0.39H/0.47u, 470R/0.22H/0.22u, 1k/68mH/0.15u, 1k/33mH/33n. THE PANEL
LABELS LIE: true centres are 87.6 / 371.7 / 723.4 / 1575.9 / 4822.9 Hz
(1/2pi*sqrt(LC); Fractal's Mark IV analysis agrees). MXR-style op-amp bus:
pot (50k) spans input<->output, wiper --RLC--> virtual ground. Closed form per
band is the classic shared-w0 boost/cut biquad:

    H(s) = (s^2 LC + sC*Rn + 1) / (s^2 LC + sC*Rd + 1)
    Rn = Rbus(1-a) + Rk + Rp*a(1-a),  Rd = Rbus*a + Rk + Rp*a(1-a)

Rbus = 2.89k FITTED to the documented +17 dB max boost (Fractal: ~+17/-18,
design +/-15); the top two bands' +/-11.8 dB asymmetry then falls out of
their real Rk = 1k. Q at full boost ~1.1-2.1 per band from the real parts
(Fractal quotes ~1.3 "actual"); most action near slider ends emerges from the
CIRCUIT with a linear slider -- no fake taper. Digital: per-band bilinear
prewarped at w0 so centres land exactly (harness T9). Band-to-band bus
interaction approximated by cascade -- the one non-exactness, documented.
Authentic Mark pairing = Mark TMB (pre) + graphic (post) + V shape.

## 7. 5E3 tweed Deluxe (kTweed5E3) -- the interactive one

The 5E3's controls ARE its tone stack; they don't divide, they LOAD:

* signal enters each 1M-A volume pot at the WIPER; CW lug -> shared V2A grid
  bus, CCW lug -> ground (Rob Robinette + forum consensus; this wiring alone
  reproduces "unused volume changes the in-use channel");
* 500p bright cap bridges wiper->CW on the bright volume;
* tone pot (1M-A) BRIDGES the two channel nodes, 5n bleed at its wiper --
  the only candidate wiring that reproduces ALL documented quirks (tone works
  from both channels; bright-vol-down turns the bright cap into a treble
  bleed for the normal channel; "bright end of the tone pot" reads literally);
* V2A Miller input capacitance ~110p on the grid bus (12AX7, gain ~63);
* the idle channel's plate Z (20k) behind its coupling cap stays in circuit.

Knobs: Volume, Tone, GHOST (the unused channel's volume -- the famous
interaction control, clintj's "unused volume on 4" trick). Quirks verified in
harness T7; full-up is near-transparent (-0.5 dB, T14) so makeup ~ 1 and the
volume keeps its authentic range. 20+ dB of ghost-loading swing is REAL.

CONFIDENCE NOTE: wiring is quirk-validated against robrobinette.com prose,
not read off the schematic PDF -- eyeball the 5E3 schematic on Windows before
commit (60 seconds; the harness pins everything else).

## 8. Numerical war stories (for future blocks)

1. MNA source stamp: an element touching the source node contributes its
   Norton current ONLY -- adding its admittance to the internal node diagonal
   twice cost a frequency-flat -6 dB and took a python replica to find.
2. ampbooks' James pages use conflicting R names; reconcile against the Bode
   anchors, not the prose.
3. Rational fitting invents SPURIOUS near-DC pole/zero pairs whenever the fit
   grid misses real LF poles (5E3's 1.6 Hz coupling poles vs a 5 Hz grid
   floor) or the order exceeds what the band needs. Hurwitz-stable junk at
   1e-4 Hz still wrecks TDF2 state (slow-integrator residues, 1e20 excursions).
   Cure = LF grid coverage + smallest-sufficient-order + a1 bound + held-out
   validation + state reset on order flips. All five are in the block.
4. Yeh's "loading is negligible" holds at noon, not at bass-heavy corners
   (2.4 dB at 40 Hz from the 1M load alone). Loading belongs in the model.

## 9. Verification map

`tests/tonestack_test.cpp` (ALL PASS, 34 checks): T1a shape vs DAFx-06 at
noon; T1b nine golden pins from an independent python/numpy loaded MNA
(0.0000 dB); T2 identification <= 0.1 dB everywhere (gate 0.05 + safe
fallback, worst seen 0.053); T3 digital==analog at 1 kHz (0.006 dB); T4
blackface dropout; T5 value-table liveness; T6 James anchors; T7 5E3 quirks;
T8 cut; T9 graphic rails/detent/reciprocity/centres; T10 400-config
stability sweep; T11 de-zipper; T12 determinism; T13 static makeup; T14 5E3
transparency; T15 44.1/48/96k.

## 10. Deferred / open

* SVT-style switched-LC mid (real values recorded: tapped toroid 100/300/800
  mH, freqs 220/450/800/1600/3000 Hz) -- needs the feedback-stage model; bass
  amp, low priority for NAM Rig.
* Hiwatt: couldn't verify values this session; FMV pool covers the territory.
* Mesa graphic Rbus is FITTED (+17 dB anchor), not schematic-read; and the
  5E3 wiring note in section 7. Both are 5-minute Windows checks.
* JCM800 mid pot 22k vs 25k variants; 250k-treble JCM clones -- add rows if
  wanted.
* Integration (next session): two instances in RigChain (per amp A/B),
  pre/post placement, ~12 params + graphic, per-model knob faces (2-knob Vox,
  3-knob 5E3 with GHOST, 5-slider graphic), brand-free names per convention.

## Sources

Yeh & Smith DAFx-06 (ccrma.stanford.edu/~dtyeh/papers/yeh06_dafx.pdf) *
guitarscience.net/tsc (Marshall values via stompboxelectronics.com) *
robrobinette.com (TMB analysis, 5E3, tapers, Zsrc guidance) * CAPS/guitarix
tonestacks.lib (grame-cncm/faustlibraries) * ampbooks.com (James analysis +
DSP pages, 5E3 circuit analysis) * boogieforum/Fractal wiki (Mark EQ RLC +
behaviour) * frontiernet.net/~jff (SVT inductor) * voxac30.org.uk + ampgarage
(cut control).
