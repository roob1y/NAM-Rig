# Spring studio spring optimizer harness (2026-06-18) — pure-numpy, offline

The black-box-optimizer approach to matching the studio spring. Cracks the **bloom**
(low-mids peak ~80ms while highs are immediate) that hand-tuning could not, AND the
**lows-ring-longest** RT60 tilt — simultaneously.

## Pipeline (run from a /tmp copy; the C:\ mount is stale for reads — see namrig-offline-build)
1. `deconv_ir.py`  — recover a clean studio spring IR by regularized deconvolution of
   dry.wav -> spring_voicing_demos/0_reference_real-studio spring/guitar_wet-only.wav. Saves bx20_ir.npy.
2. `engine.py`     — free-structure spring engine, **fully linear + LENGTH-INVARIANT**.
   Single dense 8-line FWHT FDN + per-octave decay shaping (dtilt -> lows ring longest)
   + a causal delayed low-mid INPUT injection = the bloom (lows enter the tank ~50ms late)
   + tunable stereo width. All FFT filtering is zero-padded LINEAR convolution.
   KEYS = lowcut,dark,fdn_base_ms,fdn_spread,g,damp,dtilt,bloom_mix,bloom_delay_ms,
   bloom_fc,early_mix,early_hp,stereo,scoop_hz,scoop_db,cut_hz.
3. `loss.py`       — bloom-first perceptual loss vs the studio spring IR fingerprint: per-band
   time-to-peak (from a GLOBAL onset) + low/high SPLIT (weighted most), per-octave RT60
   (fixed meter: integrate past the bloom peak, fit early slope), monotonic lows-longest
   reward, centroid, L/R corr.
4. `optimize.py`   — resumable differential evolution (checkpoints ckpt.npz so it runs in
   <45s shell chunks). `python3 optimize.py <budget_s>` to grind; `optimize.py report` to print.
5. `render_demo.py`/`make_fig.py` — convolve best IR with dry.wav -> demos + analysis PNG.

## RESULT (best_theta.json, gen 11, loss 13.4; rendered with stereo=0.65)
```
            ENGINE   studio spring
 ttp 250     84      72 ms   <- bloom: low-mids peak LATE
 ttp 500     69      75 ms
 ttp 1000    76      81 ms
 ttp 2000    19      25 ms   <- highs immediate
 ttp 4000    19      38 ms
 rt60 250    4.27    4.36 s  <- lows ring longest
 rt60 4000   2.12    2.39 s
 centroid    3728    3925 Hz
 L/R corr    0.29   -0.03    <- ONLY real gap: ours narrower (Width knob widens)
```

## GOTCHAS BURNED (do not repeat)
- ttp MUST be measured from a GLOBAL onset, not each band's own onset (a pure delay
  shifts both -> masks the bloom).
- The FFT allpass-power dispersion A(z^k)^M has a LONG tail -> circular wrap made a FAKE
  bloom that vanished at longer render lengths. ALWAYS check length-invariance
  (render at 2.0 vs 3.5s, ttp must be identical). Dispersion was REMOVED; chirp should be
  a future TIME-DOMAIN feature.
- RT60 meter must skip the bloom rise (integrate from the energy peak) or it reads garbage.

## NEXT
- Port to C++ SpringReverb in ReverbBlock.h: 8-line FWHT FDN + delayed low-mid injection
  (bloom_delay, bloom_fc, bloom_mix) + per-octave decay tilt (dtilt) + stereo width + voicing.
- Cleaner decorrelation (allpass, frequency-flat) so width can reach corr~0 WITHOUT
  collapsing the bloom (current sign-pattern method couples them past stereo~0.7).
- Optionally add a real (time-domain) dispersive chirp for audible springiness.
