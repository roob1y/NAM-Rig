# RESULTS — Spring (SpringReverb) vs the studio spring reference IR set

Goal: match the committed `SpringReverb` (`src/rig/ReverbBlock.h`) to Robbie's
studio spring IRs. Offline driver: `plate_proto/spring_driver.cpp` (extracted verbatim,
no JUCE; env `SPR_T60` / `SPR_TONE` / `SPR_TENSION`).

## First profile — committed Spring vs studio spring 2.0s
At `SPR_T60=3.5` the Spring hits the studio spring 2.0s mid-decay (1k ≈ 3.6 s). Gaps:

| trait | studio spring ref | committed Spring | gap |
|---|---|---|---|
| 125/1k decay ratio (low bloom) | 2.26 | 1.02 | no bloom — flat |
| spec @125 Hz | −2 dB | −7 dB | lows too thin |
| 8k/1k decay ratio | 0.59 | 0.75 | HF rings too long |
| C80 @2–8 kHz | −1…+1 dB | −3…−4 dB | mids/highs too washy |
| side/mid (width) | ~0 dB | −0.8…−1.7 dB | too narrow |
| L/R corr | −0.02 | +0.07 | too correlated |
| NED @40 ms (echo density) | 0.94 | 0.66 | sparser early onset |

## Direction (to match)
1. Low-mid **bloom**: extend low DECAY (not just the Tension swell-injection) so
   lows ring ~2× the mids — e.g. frequency-dependent feedback gain (low-shelf) as
   prototyped for the plate driver, plus a level trim to keep lows from booming.
2. **Thicken lows**: ease the 110 Hz low-cut / input darkening.
3. **Clarity up top**: faster HF decay in the feedback (more HF damping) so the
   mid/high tail isn't washy.
4. **Width**: more decorrelation (the engine already has per-channel allpasses —
   widen them / add side gain).
5. **Density**: denser early diffusion to raise NED toward 0.94.

## Reference caveat
The studio spring is historically a plate-type unit (dense, big low bloom). A real spring
is sparse/dispersive — our NED@40ms 0.66 is actually spring-like. Confirm studio spring is
the intended spring reference vs the separate studio-spring IRs before chasing all
the way to studio spring's density.
