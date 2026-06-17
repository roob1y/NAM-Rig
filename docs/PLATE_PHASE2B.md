# Plate — Phase 2b: vintage plate voicing on the Dattorro (ready to integrate)

Status: **prototyped + measured in /tmp, NOT yet in the live header.** The restored Dattorro plate
is committed and sounds good. This doc captures the exact vintage plate voicing changes + the real-IR profiling
targets so integration next session is quick. Engine = Dattorro figure-8 (we abandoned the FDTD mesh —
too sparse/metallic at sane CPU; mesh kept in history at f9bf4a5).

## Decisions locked
- **Decay calibration fix (do this first):** the figure-8 applies the decay term more than once per
  loop, so the knob read ~half. In `PlateReverb::recompute()` change
  `std::pow(10.0, -3.0 * loopMs / (T60*1000))` clamp `0.95` → `std::pow(10.0, -1.5 * ...)` clamp `0.997`.
  Verified: knob 0.5/1/2.5/4/5.5 s → 0.64/1.11/2.53/3.99/5.47 s measured. Now reads true seconds (vintage plate 0.5–5.5 s).
- **vintage plate decay-vs-freq:** hardwire in-loop damp ~4500 Hz → lows ring longest, ~1.5 s @10 kHz (✓ measured 1.50).
- **Low-Cut (vintage plate "Input Filter"):** one-pole HPF on the engine INPUT, bounded ~Off..360 Hz. New control.
- **Tone:** bounded bright↔dark output high-shelf (~3 kHz pivot, ±0.6 of high band ≈ ±4 dB). New control,
  reuses the existing exposed plate "tone" knob slot. NOT UAD's ±12 dB EQ (no bad sounds).
- **Predelay:** extend clamp 200 → 250 ms (vintage plate range).
- Hardwire decay-vs-freq + damping so the user can't dial metallic/boomy.

## Exact /tmp patch (apply to PlateReverb in src/rig/ReverbBlock.h)
Single-line-anchored where possible (live file is CRLF — avoid multi-line `\n` matches; use full-file
write or git-extract+python like the mesh phase). Changes:
1. recompute: decay exponent -3.0→-1.5, clamp 0.95→0.997; add
   `mLcK = mLowCutHz>20 ? onePole(mLowCutHz,mFs):0; mHsK = onePole(3000.0,mFs);`
2. setters: predelay clamp →250; add `setLowCutHz(hz){mLowCutHz=hz;mDirty=true;}` and
   `setTone(t){mTone=clamp(t,-1,1);}`
3. process input (after `mBw...; x=mBw;`): `if(mLcK>0){mLcL+=mLcK*(x-mLcL); x-=mLcL;}`
4. process output (replace `left[n]=wetL; if(stereo)right[n]=wetR;`):
   `float oL=wetL,oR=wetR; const float tg=mTone*0.6f; mHsL+=mHsK*(oL-mHsL); oL+=tg*(oL-mHsL);`
   `mHsR+=mHsK*(oR-mHsR); oR+=tg*(oR-mHsR); left[n]=oL; if(stereo)right[n]=oR;`
5. members: `float mLowCutHz=0,mTone=0,mLcL=0,mLcR=0,mHsL=0,mHsR=0,mLcK=0,mHsK=0;`
6. reset: `mLcL=mLcR=mHsL=mHsR=0;`
Then ReverbBlock: pushParams plate → setLowCutHz/setTone from new params; introspection lowCutExposed=Plate;
map the plate tone knob to setTone (damping is now hardwired). JUCE-side: add Low-Cut param + UI (review by hand).

## REAL-IR PROFILING (15 Greg Hopkins vintage plate IRs, CC-BY, attribute Greg Hopkins)
Profiled with the Abel–Huang NED + per-band Schroeder harness (/tmp/prof/profile.cpp). Key targets:
- **Spectral centroid (the big correction):** bright ≈ 1080–1210 Hz, medium ≈ 800–855 Hz, dark ≈ 615–660 Hz.
  Our /tmp voicing centroid was ~2250 Hz → **WE ARE ~1 kHz TOO BRIGHT.** Darken the default toward
  medium (~825 Hz); Tone span should cover ~dark 630 ↔ bright 1150, centred ~825. (i.e. lower the default
  damp below 4500 and/or bias the Tone shelf darker.)
- **Echo density:** real NED→0.5 at ~1 ms (instant). Ours ~20 ms — fine, but real is denser/faster.
- **Decay range:** settings 1→5 give mid T60 ≈ 1.5 s → ~6 s. (The 5k/10k band readings in the table were
  erratic = bandpass artifact on these IRs; trust low/mid + centroid.)
- **L/R correlation ≈ 0** (true stereo). Ours matches.
NEXT: re-tune damp + Tone center to hit centroid ~825 Hz (medium), re-render, A/B vs the Hopkins IRs
(in "vintage plate Plate IR's" folder), then integrate + wire UI + commit.

## Licensing (for commercial release)
Hopkins vintage plate IRs = CC-BY per oramics (secondhand tag; confirm from Hopkins' own posting for a release).
Used here only as a private measurement reference (not shipped). MUST credit Greg Hopkins. Keep "vintage plate" out
of the product name (trademark). Adventure Kid spring IRs = CC-BY (clean) for the future studio spring spring ref.
