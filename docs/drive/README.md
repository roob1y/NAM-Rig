# Drive / Overdrive research & design

Working notes behind the `DriveBlock` overdrive rework (June 2026). Goal stated
by Robbie: *"make our overdrives better."* Three agreed directions: **lower
aliasing**, **more authentic topology**, **better dynamics/feel**.

Chosen architecture: **Option A — zero-latency ADAA** (keeps the engine's
zero-latency / bit-exact-off ethos; no oversampling). See the design doc.

| File | What's in it |
|------|--------------|
| **[building-drives-playbook.md](building-drives-playbook.md)** | **START HERE for a new pedal** — the workflow, the full toolkit, lessons learned, next targets |
| [tube-screamer-circuit.md](tube-screamer-circuit.md) | TS808 clipping-amp circuit analysis — the reference for our Green Drive |
| [overdrive-family.md](overdrive-family.md) | TS vs SD-1 vs Klon: symmetric/asymmetric clipping, clean-blend, what each contributes |
| [current-driveblock.md](current-driveblock.md) | What `DriveBlock.h` does *today* (pre-rework baseline) |
| [option-a-design.md](option-a-design.md) | The plan: cubic soft-clip + 2nd-order ADAA + pre/de-emphasis + envelope dynamics, with the math |
| [circuit-accuracy.md](circuit-accuracy.md) | Deriving the TS808 response from the schematic (no pedal needed) + fitting our voicing to it (RMS 0.66 dB) |
| [ts808_response.py](ts808_response.py) | The runnable derivation/fit script |

## The three audible ideas we're stealing from the real circuits

1. **Frequency-selective clipping** (TS): the diodes sit in the op-amp feedback,
   and a high-pass in that loop means **bass is clipped least**, mids/highs above
   ~720 Hz get the gain. Plus a tiny cap across the diodes softens the clip
   corners. Net: smooth, non-fizzy, mid-forward. → we emulate with
   **pre-emphasis into the clipper + de-emphasis after** (zero latency).
2. **Clean superimposed on dirty** (TS "secret" + Klon blend): feedback clipping
   leaves part of the clean input in the output, so the pedal keeps the guitar's
   dynamics instead of becoming pure distortion. → **clean-blend term**.
3. **Touch sensitivity**: the above two together make the pedal "clean up" when
   you pick softer and bite when you dig in. → **envelope-driven knee** so it
   reacts to playing level, not just the Drive knob.

Sources are listed at the bottom of each file.
