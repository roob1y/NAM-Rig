# Tom DeLonge / Angels & Airwaves Rig — Build Handoff

*Last updated: 2026-07-05. Everything below is UNCOMMITTED (needs a Windows MSVC build + play-test). Read the memories `stereo-front-mod-for-dualamp`, `stereo-front-delay-for-dualamp`, and `drive-send-routing` for the deep technical detail; this doc is the human-readable summary.*

---

## 1. The goal

Recreate Tom DeLonge's Angels & Airwaves (and later blink-182) tone in NAM Rig: a **dirty Vox AC30 and a clean Fender Twin run at the same time, in stereo, hard-panned L/R**, with big rhythmic stereo delays on top.

---

## 2. What DeLonge's real rig actually is (researched, cited)

**Amps — the dual setup is confirmed from his own words.** In a 2008 Guitar World interview he said: *"I use a Vox AC30 and Fender Twin in stereo… I've tried other things but I always end up with those two."* The **AC30 provides the dirt, the Twin Reverb stays clean**, blended together in stereo — not switched between. Live he ran the power amps into **Palmer speaker sims straight into the PA** so the crowd hears the stereo delays bouncing L/R.

**His dirt comes from the amp + overdrive pedals, not a high-gain channel.** Two Fulltone Fulldrives stacked, later a Fulltone OCD to push the Vox harder. The "clean" Twin was often run loud/on the edge too.

**Guitar:** Gibson ES-333 signature — single bridge pickup (Dirty Fingers), one volume knob, no tone, no selector. Bright, hot, simple.

**Effects — delay is the whole sound.** The signature ambience is a deliberate The Edge / U2 imitation: **two stereo delays, one long (~400ms+) and one short (~50–70ms), panned wide.** His actual delay unit was a clean digital **Boss DD-6** (backed by a TC G-Force rack live). Around it: a **compressor** always on the front (Line 6 Constrictor — this matters, see §6), a subtle **chorus** (Boss CE-2), a big **reverb** wash, and **MXR flanger (EVH117) / Phase 90** for movement. Sci-fi textures came from an **EHX Micro Synth** (octave), not a shimmer pedal. By the 2023+ blink tours he'd switched to a Fractal Axe-FX III, so the pedal list is the classic/AVA-era rig.

**Caveat:** DeLonge is not a documented-settings guy — almost no exact ms/feedback numbers exist from primary sources. The most "official" reference now is his own 2025 "Adventure Box" pedal (compressor + analog-voiced delay) which ships with a card of settings for "The Adventure," "Adam's Song," etc.

Sources: Guitar World (DeLonge interview), Rock Guitar Universe, UberProAudio, Premier Guitar blink Rig Rundown, Guitar World (Adventure Box), Mesa Boogie forum ("The Adventure" tone), Wikipedia (ES-333).

---

## 3. The current working NAM Rig patch

- **Mode: Dual**, hard-panned. **Amp A = left = clean Twin.  Amp B = right = AC30.**
- **`driveSend = Amp B`** — the drive rack (a TS with gain at minimum, level to taste = a clean mid-boost that pushes the AC30 into slight breakup) hits ONLY Amp B. Amp A takes the pre-drive **clean tap**, so the Twin stays clean while you can drive the AC30 as hard as you like.
- Balance the two amps **by ear while palm-muting** (the dirty side reads louder from its density; pull Amp B's level down a touch). Don't trust the auto level-match button — it probes one level and can't track the clean-vs-dirty dynamic difference (distortion = compression).
- Brighten Amp A's EQ if the clean left sounds thin next to the grit.
- **Reverb is handled by an external plugin** (Robbie's choice — the built-in reverb block is out of scope for this tone).

---

## 4. What was built this session (all UNCOMMITTED)

**Stereo front modulation (premod).** In Dual, the front mod pedal became mono-in / stereo-out: L lane → Amp A, R lane → Amp B, one phase-locked LFO read at two phases for a chorus/flanger/vibe spread across both amps. New **Stereo** toggle + **Spread** knob (default ~90°). Runs post-split, per-voice, so it slots around `driveSend` cleanly. Mono path bit-exact. (See `stereo-front-mod-for-dualamp`.)

**Stereo front delay (predelay).** Same treatment: mono-in / stereo-out, L → Amp A at the base time, R → Amp B at `(1 − 0.5·Spread)·L` — the Edge/AVA long/short. Independent per-lane delays; runs post-split after the premod stereo pass (which also restored the intended premod→predelay order). New **Stereo** toggle + **Spread** knob. Mono path bit-exact. (See `stereo-front-delay-for-dualamp`.)

**L/R balance fix (the big one this session).** Play-test found the stereo delay was consistently "hotter in the left," on all models. Reproduced offline: with identical input the left ran up to **+7 dB** hotter. **Root cause:** two independent feedback delays at different times are two comb filters resonating at different frequencies, so a sustained note lands on one lane's peak and the other's notch. **Fix:** both lanes now recirculate the **mono sum of the two wets** — one shared comb both taps read — instead of two independent loops fighting. Result: realistic chords now balance to **~1 dB** and the residual per-note variation *flips side by note* (no consistent lean). The remaining single-held-note wander (±dB by pitch) is inherent to any different-time stereo delay and averages out over chords.

---

## 5. Test & build status

- **`predelay_test` = 50/50 offline** (compiled + ran this session). Key guards: T17 mono bit-exact, T18 R-at-half-time timing, T19 block continuity, **T20 shared-feedback chord balance (measured 1.27 dB, asserts < 3 dB).**
- **`premod_test` = 42/42 offline.**
- **`dualrig_test`** integration checks can't build offline (RigChain pulls the cab convolution + NAM) — run on Windows. A predelay-stereo integration check has NOT been added yet (mirror premod's T10 if you want it).
- **Everything is uncommitted.** Next real gate: MSVC build → confirm the test counts + no RigChain/Panels compile errors → play-test → **commit** (premod stereo, predelay stereo, and the balance fix together).
- **Sandbox gotcha:** the bash mount serves stale/truncated views of large edited files (`PreDelayBlock.h`, the test). Read/Write/Edit are truth. To compile offline, reconstruct the file (git-show or head+tail splice) into the outputs dir with an empty `juce_audio_basics` stub — the DSP headers use zero juce symbols.

---

## 6. Known behaviours (NOT bugs) — so they don't surprise you again

- **Mono predelay + `driveSend = Amp B` → only Amp B gets the delay.** The clean tap for Amp A is captured *before* the drive rack, and the mono predelay sits *after* it, so Amp A's feed never passes through the delay. **Fixes:** set the predelay position to **"Before Drive"** (then the clean tap includes it → Amp A gets the delay), or use the **Stereo** predelay (runs per-amp after the split → both get it). A single mono pedal after the drive rack physically can't be on both the driven and clean-tapped bus.
- **Right delay is "double fast."** That's the **Spread** knob: R time = `(1 − 0.5·Spread)·L`. At Spread max, R is exactly half the L time. Pull Spread back to bring them closer.
- **Left delay "rings longer."** By design — L runs the longer time, and with shared Feedback the longer-timed side takes more wall-clock time to decay. Musically sensible (long wash on the clean amp, tight delay on the dirty one).

---

## 7. Open ideas / possible next steps

- **Musical timing:** make the R side snap to its own tempo-sync subdivision (currently a continuous ratio of L) for a tighter dotted-eighth pulse.
- **Ducking** on the delay repeats (swell in the gaps) — big clarity win, especially into the dirty amp; already on the backlog.
- **Per-lane feedback** if you ever want to even out (or exaggerate) the L/R decay length independently.
- **Deeper balance** for held single notes: ping-pong topology or lower default mix — only if the per-note wander bugs you on leads.
- **Finish the core:** finalize the front compressor (OTA/Dyna voicing, Sustain ~0.65 was the starting point) and the amp level/EQ balance.
- Consider whether the default **Spread** should ship gentler than 0.5.

---

*Files: `src/rig/PreDelayBlock.h`, `src/rig/PreModBlock.h`, `src/rig/RigChain.h`, `src/PluginProcessor.cpp`, `src/ui/Panels.h`, `tests/predelay_test.cpp`, `tests/premod_test.cpp`, `tests/dualrig_test.cpp`.*
