# Validation Notes (Offline Parity Tooling)

## Purpose

`platform/offline/sound_bubbles_render` now supports lightweight parity validation with:

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

## Typical workflow

1. Generate the **reference** render + metrics:

   ```bash
   ./platform/offline/sound_bubbles_render in.wav preset.json ref.wav --metrics-out ref_metrics.csv --repro-check
   ```

2. Generate the **candidate** render + metrics:

   ```bash
   ./platform/offline/sound_bubbles_render in.wav preset.json cand.wav --metrics-out cand_metrics.csv --repro-check
   ```

3. Compare metrics:

   ```bash
   ./platform/offline/sound_bubbles_render compare ref_metrics.csv cand_metrics.csv --max-threshold 1e-6 --mean-threshold 1e-7
   ```

## Threshold guidance

- **Exact-deterministic expectation (same binary/platform):**
  - Repro check should pass with identical metric hash.
  - Compare thresholds can remain strict (`max <= 1e-6`, `mean <= 1e-7`).
- **Cross-toolchain / cross-architecture parity checks:**
  - Start with looser guardrails (`max <= 1e-4`, `mean <= 1e-5`).
  - Tighten once stable across CI + hardware targets.

If thresholds fail, inspect the largest-delta columns first (`ducking_gain`, `envelope`, and `out_peak_*` usually identify control vs audio drift quickly).
