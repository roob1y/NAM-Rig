# Dynamic Cab — EAR NOTES (Robbie → Claude)

Listening findings for the physics upgrade. One entry per observation. Claude
reads this at session start and maps symptoms to stages/constants (see
PHYSICS_UPGRADE.md §11 for what is data-anchored vs [EAR]).

**Before judging:** level-match the A/B (Match Levels in the Mix panel, or trim
the Dyn side down by the measured difference) — a hotter A/B always "wins".
Quick contract check: the added level must be LEVEL-DEPENDENT; if the gain
persists on quiet playing, report it as a BUG, not a voicing note.

**Symptom → stage cheat sheet:**
- low thump blooms too much / too little on sustained low notes → A1
- fizzy / honky / crying mids when digging in → B1 breakup + modal shaping
- highs smear / warble / thicken on loud low chugs → IM (AM/Doppler)
- slowly gets duller over a long loud section, recovers in pauses → thermal
- boxy / reverby tail, "room around the cab" → Stage C enclosure
- how hard you must play before ANY of it wakes up → kDispLo/Hi knee + kXCal

**Even better than words:** bounce the same loop Dyn-on and Dyn-off (wav) into
`docs/cabdyn/renders/` — Claude can measure the delta directly (bloom dB,
sideband levels, spectra) and tune constants against your actual playing.

Template:
```
## YYYY-MM-DD — <preset or Age/Thump/Size values> — <IR> — <playing style>
- <what you heard> — <way too much / slightly too much / right / too little>
```

---

## 2026-07-06 — settings: (fill in) — IR: (fill in)
- Dyn adds ~0.8 dB of level at these settings — definitely doing something.
  (Expected if it collapses when playing quietly; A/B verdicts need Match
  Levels engaged. If it does NOT collapse when quiet → report as bug.)
- Stage C "air": not really audible at these settings — judge via delta-null
  before deciding whether it earns its place or gets cut.

## 2026-07-06 (later) — boosted JCM800 capture
- Quiet vs loud playing doesn't change the null much; "not dynamic enough".
- Quite fizzy (possibly the boosted high-gain capture).
- Low end doesn't "thump"; not coming out of the speaker dynamically.

**Claude's diagnosis (pending renders):**
1. Age's static wear parts (HF ease, low-comp floor, drive floor 1+Age) are
   MEANT to persist at quiet — partial explanation for the constant null.
2. A boosted JCM800 is a limiter: its output level barely tracks playing
   dynamics, so the cab sees near-constant drive (true of real rigs too).
   Sanity check: guitar volume rolled off / clean capture → dynamics should
   reappear.
3. BUG (mine): the dispPush knee (kDispLo/Hi) was calibrated on FULL-SCALE
   PURE TONES (T10-style), but in program material the 80-110 Hz slice sits
   ~-15 dB under the total → dispPush barely engages on real playing → no
   thump bloom, near-silent IM. Fix by DATA, not ear:

**RENDERS REQUESTED → docs/cabdyn/renders/**
1. amp-only.wav (cab + Dyn bypassed): hard low chugs, normal riffing, quiet
   playing — used to recalibrate kXCal / kDispLo/Hi / kEnvLo/Hi to the real
   pre-cab level structure.
2. riff-dyn-on.wav + riff-dyn-off.wav (same loop): delta spectrum → re-dose
   fizz (modal gains / drive floor).

## 2026-07-06 — CORRECTION: renders were −10.1 dB trimmed
Robbie's Mix Output Gain was at −10.1 dB for the renders. It sits POST-chain
(PluginProcessor applies it after RigChain), so it never affects the Dynamic
Cab internally — but it scaled the measurement. All §12 constants rescaled
+10.1 dB to the true INTERNAL level (kXCal 8.75, kEnvLo/Hi 0.16/0.48,
kThermFull 0.041); behavior verified identical on internal-level signals;
40/40 tests. **For future calibration renders: note the Output Gain setting,
or set it to 0 dB.** For LISTENING, the knob is harmless — set it wherever.

## 2026-07-06 — RENDERS RECEIVED → rig recalibration SHIPPED (spec §12)
Measured: amp-out ~ −24 dBFS RMS; dispPush was 0.0000 on ALL files (bug
confirmed); old on/off delta = constant −14 dBc grit at 2.5–20 kHz vs −27 dB
thump. Recalibrated kXCal 28, knees 0.20/0.80 + 0.05/0.15, thermal full-point
0.004, drive dose moved static→dynamic, exciter band-limited < 6.5 kHz.
Verified on the renders: chugs pin dispPush 0.93, riffing 0.31 (peaks 0.87),
quiet exactly 0; playing hard now darkens HF −2.3 dB (A2+thermal) instead of
hissing. 40/40 tests pass. **Needs local rebuild, then re-listen.**

**What to listen for on the re-test (level-match first — Dyn now sits ~1 dB
quieter while playing hard, so use Match Levels):**
- Palm-mute chugs: low-end should bloom/breathe per hit now (A1 + IM alive).
- Digging in vs backing off: audible dynamic response (was static before).
- Fizz: should now MOVE with playing and sit darker overall; if still too
  gritty at rest, the remaining dial is kDriveBase (0.5) / modal gains [EAR].
- Long sustained loud section: gradual ~1 dB sag, recovering in pauses
  (thermal — subtle by design).
