# Prompt — re-voice Tape Echo with the controlled-probe method

Paste this whole file into a new chat. STEP 1 is the captures I do first; STEP 2 is the task.

---

## STEP 1 — capture these first (Robbie does this in the DAW)

Run the dry probe files through my **single-head Tape Echo reference plugin** (generic
terms only in code — never the brand/model) at ONE fixed setting. The probes are in
`delay_references/dry_probes/`.

Settings (map to whatever the plugin calls them — the requirement is functional):
- **Tone / EQ** (bass/treble/tilt): flat / neutral / centred.
- **Any utility HPF / LPF filters**: OFF / fully open.
- **Mix (Dry/Wet)**: 100 % WET.
- **Tape character extras**: leave the tape's own wow/flutter at default/centred (it's
  part of the character) — but no extra chorus/mod, noise OFF, tape age centred.
- **Repeat time (delay)**: ~300–400 ms — and tell me the exact ms.
- **Input**: hot enough to register, not clipping the meter.

Two captures — only the input file and the FEEDBACK change:
1. **`dry_main.wav`** → FEEDBACK at its **lowest** → save as **`cap_low.wav`**
   (one clean echo: gives EQ + saturation + harmonics + wow/flutter + impulse)
2. **`dry_perpass.wav`** → FEEDBACK **high, just under self-oscillation** (if it clips,
   turn the input down — the per-pass doesn't care about level) → save as **`cap_high.wav`**
   (the decaying trains: gives the per-pass / in-loop EQ)

Tape Echo is single-head, so there's no taps capture (`dry_main` already ends with a click
for the delay time / onset). Put `cap_low.wav` + `cap_high.wav` in
`delay_references/tape_echo/` and tell me the repeat time + which feedback I used.

---

## STEP 2 — the task

Continue NAM Rig delay-character work, branch `feature/delay-rework`, `C:\Dev\NAM-Rig`.
The **Space Tape** character was just re-voiced accurately with a controlled steady-tone
probe method and it matched the reference far better than the old click-based captures.
Redo the single-head **Tape Echo** the same way — its committed voicing used the older,
less accurate method.

**Read first:**
- `delay_analysis/CONTROLLED_PROBE_METHOD.md` — the full method (probes, capture protocol,
  extraction, fitting, pitfalls). This is the playbook; follow it.
- `voicingFor(Character::Tape)` and `tapeSat` in `src/rig/DelayBlock.h` — the current
  committed Tape voicing to refit (cubic+cosh saturation; a big in-loop peaking head-bump
  bloom +14 @260 cancelled by an output-once peaking cut −13 @260; 2-pole gap-loss ~2.1 kHz;
  preamp peak +4 @3.5 k; wow/flutter ~0.1 %). Note Space Tape already branches `tapeSat` to
  an asymmetric `sat::tanhADAA1`; Tape currently uses the cubic+cosh path.
- The memory note `delay-characters-tape` (full worklog, incl. how Space Tape was done).

**Captures:** I'll provide `delay_references/tape_echo/cap_low.wav` (lowest feedback) and
`cap_high.wav` (high feedback) from running `delay_references/dry_probes/dry_main.wav` and
`dry_perpass.wav` through my Tape reference (see STEP 1) — ask me to capture if they're not
there yet, and I'll tell you the repeat time.

**Do it (per the method doc):**
1. Build offline from `git show HEAD:src/rig/DelayBlock.h` (the mount truncates large files;
   file tools are truth; the outputs mount doesn't truncate). Keep `delay_test` 34/34, Clean
   byte-exact.
2. Align the captures by the delay; extract the **single-repeat EQ** (steady-tone transfer —
   the reliable metric), the **saturation** growth + **harmonic series**, **wow/flutter**
   (pure carrier), and the **per-pass** (high-fb decay → in-loop vs output-once split).
3. Refit the Tape voicing: the EQ band-pass / head-bump (decide in-loop vs output-once from
   the per-pass — Tape's bloom is in-loop), the saturation (check whether Tape's reference is
   EVEN-dominant — the cubic+cosh may fit — or a smooth full asymmetric series → switch Tape
   to `sat::tanhADAA1` like Space, adding a Tape branch), wow/flutter, fb ceiling. Re-fit the
   saturation after the EQ (they couple).
4. Verify per-frequency with `delay_null` (`--sync`/`--bpm` to hit my repeat time;
   `--sat`/`--asym`/`--gapHz`/`--hbDb`/… overrides to audition). Confirm `delay_test` 34/34,
   Clean byte-exact, loop bounded; reconstruct from HEAD to confirm it compiles. Hand me a
   battery/comparison graph + the commit command.

**Hard rules:** generic names only (ships as **Tape Echo**); no hardware brand/model anywhere.
Don't commit `delay_ref/`, `delay_references/`, or `dry_probes/`. Don't touch the Clean path.
Ears + null residual are the judge — I A/B by playing guitar through the JUCE plugin. The
sandbox can't git-commit; hand me the commit command.
