# Prompt — diagnose / fix Space Tape multi-head TIMING

Paste this whole file into a new chat. STEP 0 is what I observe; STEP 1 is the task.

---

## STEP 0 — what feels off (fill in before/at the start)

The multi-head **Space Tape** head timing seems off to me. Before diving in, here's
what I'm hearing (I'll confirm in chat — ask me if blank):
- Is it in **FREE** mode, **SYNC** mode, or both? ____
- Which **Mode(s)** (1–12 / head combos) sound wrong? ____
- Is it that the echoes land at the wrong *spacing/rhythm*, or that the **displayed
  Time (ms/Hz) doesn't match what I hear**, or that a Mode lights up the wrong heads? ____
- A reference I'm comparing against (by ear / a capture)? ____

---

## STEP 1 — the task

Continue NAM Rig delay-character work, branch `feature/delay-rework`, `C:\Dev\NAM-Rig`.
Clean + Tape Echo + Space Tape are all voiced & committed (Tape Echo was just re-voiced
via the controlled-probe method). This task is NOT a re-voice — it's **verifying and, if
needed, fixing the multi-head Space Tape head TIMING**: the tap positions per mode, the
free-vs-sync time mapping, and the leading-head sync snap.

**Read first (the timing lives in `src/rig/DelayBlock.h`):**
- `processSpaceTape(...)` — the multi-head read loop. Each active head `h` reads at
  `kHeadRatio[h] * (base + modMs)` (so head1=base, head2=1.95×, head3=2.79×); note the
  `readFrac6(d - 1.0)` 1-sample interpolation offset; wow/flutter scales with head distance.
- `kHeadRatio = {1.0, 1.95, 2.79}` — head spacing, MEASURED from the controlled-probe
  `cap_taps` (close to the documented ~1:1.9:2.76, not the idealised 1:2:3).
- `kStHeadMask[12]` + `spaceTapeReverbOn(mode)` — the Mode→active-heads table (modes 1–4
  echo-only: H1, H2, H3, H2+3; 5–11 same combos + spring; 12 reverb-only). Confirm the UI
  "Mode" selector indices line up with this table.
- `baseTarget` logic: **FREE** = the Time knob is REMAPPED onto `kStHead1MinMs..MaxMs`
  (69–177 ms head 1) — so the displayed Time is NOT head1's actual ms (a known caveat; the
  UI Hz/ms readout is nominal). **SYNC** (`mSyncIdx > 0`) = `base = currentTimeMs() /
  leadingRatio` so the **lowest active head** lands on the host division (other heads fall
  at non-integer multiples — authentic, but check it's what's intended).
- `currentTimeMs()` / `setHeadMode()` / `mHeadMode` / `mMultiHead`.
- The memory note `delay-characters-tape` (Space Tape worklog: head modes, the 69–177 ms
  free span, the leading-head sync snap matched to the reference, the measured ratios).

**Do it (diagnose first, then fix only what's wrong):**
1. Build offline from `git show HEAD:src/rig/DelayBlock.h` (the mount truncates large files;
   file tools are truth; the outputs mount doesn't truncate). Keep `delay_test` 34/34, Clean
   byte-exact.
2. **Measure the actual tap positions** for ALL 12 modes, in BOTH free and sync, by driving
   the real engine (extend the `delay_test` tap checks and/or `delay_analysis/delay_null.cpp`
   — render a click/probe through `Character::SpaceTape` at a known Time/BPM/sync, find the
   echo onsets, report ms + ratios per active head). Compare to expected: per mode, the
   active heads should sit at `kHeadRatio[h] * base`, and in sync the leading active head
   should land exactly on the division.
3. **Localize what's off** against STEP 0: e.g. (a) the FREE-mode Time-knob remap making the
   displayed time disagree with the heard time (consider exposing the real head1 ms in the UI
   readout, or rethinking the remap); (b) the SYNC leading-head snap putting later heads at
   non-musical multiples; (c) a Mode→mask mismatch; (d) the `readFrac6(d-1.0)` offset; (e) the
   ratios themselves. Decide which is the real problem before changing anything.
4. **Re-measure the head ratios if suspect**: regenerate the probes with
   `python3 delay_analysis/gen_dry_probes.py` (commits the generator; produces `dry_click.wav`
   = clicks through an all-heads mode). I run `dry_click.wav` through my reference at the
   all-heads setting → `cap_taps.wav` in `delay_references/space_tape/`; extract the head-tap
   ratios from the envelope peaks and check `kHeadRatio`.
5. Verify per change with the tap harness; confirm `delay_test` 34/34, Clean byte-exact,
   loop bounded; reconstruct from HEAD to confirm it compiles. Hand me a tap-positions
   table/graph (expected vs measured, per mode, free + sync) + the commit command.

**Hard rules:** generic names only (ships as **Space Tape**; never a hardware brand/model).
Don't commit `delay_ref/`, `delay_references/`, or `dry_probes/`. Don't touch the Clean path.
Ears + the measured tap positions are the judge — I A/B by playing guitar through the JUCE
plugin. The sandbox can't git-commit; hand me the commit command.
