# Validation Notes (Offline Parity Tooling)

## Purpose

`build/sound_bubbles_render` now supports lightweight parity validation with:

- Per-control-block metrics exported via `SoundBubbles_SetMetricsCallback`.
- Output-energy summaries per block (RMS + peak for left/right).
- Deterministic reproducibility checks (same input + seed => identical metric hash).
- Reference-vs-candidate metrics comparison mode.

## Metrics schema

When `--metrics-out` is supplied, the renderer writes CSV rows with:

- `block`
- `spawn_count`
- `active_voices`
- `engine_state`
- `ducking_gain`
- `envelope`
- `out_rms_l`
- `out_rms_r`
- `out_peak_l`
- `out_peak_r`
- `peak_l`
- `peak_r`
- `clip_count`
- `limiter_gain`

## Typical workflow

Build the renderer with `make offline` first; the binary is emitted under `build/` so the source tree does not receive versioned build artifacts.


1. Generate the **reference** render + metrics:

   ```bash
   ./build/sound_bubbles_render in.wav preset.json ref.wav --metrics-out ref_metrics.csv --repro-check
   ```

2. Generate the **candidate** render + metrics:

   ```bash
   ./build/sound_bubbles_render in.wav preset.json cand.wav --metrics-out cand_metrics.csv --repro-check
   ```

3. Compare metrics:

   ```bash
   ./build/sound_bubbles_render compare ref_metrics.csv cand_metrics.csv --max-threshold 1e-6 --mean-threshold 1e-7
   ```

## Threshold guidance

- **Exact-deterministic expectation (same binary/platform):**
  - Repro check should pass with identical metric hash.
  - Compare thresholds can remain strict (`max <= 1e-6`, `mean <= 1e-7`).
- **Cross-toolchain / cross-architecture parity checks:**
  - Start with looser guardrails (`max <= 1e-4`, `mean <= 1e-5`).
  - Tighten once stable across CI + hardware targets.

If thresholds fail, inspect the largest-delta columns first (`ducking_gain`, `envelope`, and `out_peak_*` usually identify control vs audio drift quickly).

## JUCE release-candidate checks

When `BUBBLES_BUILD_PROCESSOR_TESTS=ON`, `tests/juce/processor_smoke.cpp` additionally renders a deterministic musical probe through:

- 44.1, 48, 88.2, and 96 kHz;
- prepared and deliberately irregular block sizes;
- all 20 JUCE factory presets;
- stereo correlation and mono-fold compatibility checks;
- perceptual morph curves, discrete/Freeze hysteresis, MIDI Capture, state restore, and editor snapshot rendering.

The Windows VST workflow runs this executable before validating the built bundle with pluginval strictness level 5. The pluginval archive is version-pinned and SHA-256 verified before execution.
