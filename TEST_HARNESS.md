# Sound Bubbles DSP Core: Offline Test Harness

This document describes the current native C harness in `platform/offline/test_harness.c`. The harness is intentionally separate from ESP-IDF, WebAudio, and file-upload code so the DSP core can be exercised as a deterministic fake audio thread.

## Scope

The harness compiles `platform/offline/test_harness.c` with `core/dsp/sound_bubbles_dsp.c`, `core/engine/bubble_engine.c`, and `core/engine/bubble_macro_map.c`, then calls `SoundBubbles_ProcessBlock` directly. It uses the same core constants as the engine:

- `BUBBLES_SAMPLE_RATE` = 44100 Hz.
- `BUBBLES_BLOCK_SIZE` = 32 samples for fixed-block runs.
- `BUBBLES_MAX_VOICES` follows the compiled engine ceiling (`BUBBLE_ENGINE_MAX_VOICES`, currently 32).
- The host-owned delay buffer is a static `int16_t delay_buffer_memory[BUBBLES_BUFFER_SIZE_SAMPLES]`.

## What it validates

The harness currently covers:

1. fixed 32-sample block processing;
2. irregular chunk processing with a repeating chunk sequence;
3. silence, impulse, plucked tone, repeated transients, and sustained sine test vectors;
4. drain behavior for vectors that should eventually release all voices;
5. finite-output checks and a broad safety bound against explosive samples;
6. engine-state, pending-spawn, voice-state, and fade-counter invariants;
7. sample-continuity checks for irregular chunk output;
8. mono-center L/R crosstalk checks when wet-only output is forced to the center.

The harness writes interleaved stereo 32-bit float `.raw` files for listening/inspection. The GitHub workflow converts those raw files to WAV artifacts after the harness passes.

## Determinism notes

The harness no longer depends on C library `rand()`/`srand()`. Test determinism is driven by `EngineConfig_t.rng_seed`, currently set to `42u` in the baseline config. Reproducibility across host/toolchain combinations should be validated with the offline renderer's metrics mode when exact artifact comparison is required.

## Baseline config

`GetBaselineConfig()` sets a focused synthetic-test preset:

- envelope thresholds: `noise_floor`, `tracking_thresh`, `sustain_thresh`, and `transient_delta`;
- ducking coefficients and burst settings;
- burst/sustain/decay density values;
- semantic read regions for attack/body/memory distances behind the write head;
- class durations/window types for micro attack, short intermediate, and sustain body voices.

Individual tests may then force wet-only gain, stereo width, pan spread, diffusion, or other fields to isolate a specific invariant.

## Build and run locally

```sh
mkdir -p build/harness
cc platform/offline/test_harness.c core/dsp/sound_bubbles_dsp.c core/engine/bubble_engine.c core/engine/bubble_macro_map.c \
  -O2 -Wall -Wextra -std=c11 -lm -Icore -Icore/dsp \
  -o build/harness/test_harness
(cd build/harness && ./test_harness)
```

The generated `.raw` files are local artifacts and should stay out of version control.

## CI workflow

`.github/workflows/main.yaml` runs the same harness on pushes that touch the DSP core or harness. It uploads both raw float files and converted WAV files as workflow artifacts so audio changes can be inspected when a DSP change lands.

## When to use this harness vs. the renderer

Use the harness for low-level state-machine, voice-pool, chunking, and invariant checks. Use `build/sound_bubbles_render` for preset-driven WAV rendering, metrics CSV export, reproducibility hashes, and reference/candidate comparisons.
