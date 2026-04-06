# Implementation Plan V2 — Canonical Preset Schema

This document defines the **single canonical preset/config representation** used across:

- offline renderer (`platform/offline/sound_bubbles_render.c`)
- DSP-facing JSON presets (`presets/*.json`)
- WASM/Frontend parameter model (`web/wasm/bubble_cloud_wasm.c`, `web/frontend/app.js`)

## Canonical preset keys

The canonical JSON schema is a flat object with these keys:

### Detection and dynamics
- `noise_floor`
- `tracking_thresh`
- `sustain_thresh`
- `transient_delta`
- `duck_burst_level`
- `duck_attack_coef`
- `duck_release_coef`
- `burst_duration_ticks`
- `burst_immediate_count`

### Densities
- `density_burst`
- `density_sustain`
- `density_decay`

### Semantic read regions (samples behind write head)
- `attack_region_min_offset_samples`
- `attack_region_max_offset_samples`
- `body_region_min_offset_samples`
- `body_region_max_offset_samples`
- `memory_region_min_offset_samples`
- `memory_region_max_offset_samples`

### Class durations (milliseconds)
- `micro_duration_ms_min`
- `micro_duration_ms_max`
- `short_duration_ms_min`
- `short_duration_ms_max`
- `body_duration_ms_min`
- `body_duration_ms_max`

### Random seed and final mix
- `rng_seed`
- `mix_dry_gain`
- `mix_wet_gain`

## Backward-compatible translation (offline renderer)

`platform/offline/sound_bubbles_render.c` accepts legacy keys and translates them to canonical values.

### Legacy region fields
- `micro_offset_samples` + `micro_jitter_samples` → attack region min/max
- `short_offset_samples` + `short_jitter_samples` → body region min/max
- `body_offset_samples` + `body_jitter_samples` → memory region min/max

### Legacy mix fields
- `master_dry_gain` → `mix_dry_gain`
- `master_wet_gain` → `mix_wet_gain`

When both canonical and legacy keys are present, **canonical keys win**.

## WASM/frontend parameter ID contract

The frontend sends canonical parameters to `wasm_set_param` using this stable ID map:

- 0..11: analysis / ducking / burst / density
- 12..17: attack/body/memory region min/max
- 18..23: class duration min/max (micro/short/body)
- 24: `rng_seed`
- 25..26: `mix_dry_gain` / `mix_wet_gain`

This is a naming/schema alignment only; no web-only DSP behavior is introduced.
