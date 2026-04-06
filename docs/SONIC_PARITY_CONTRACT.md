# SONIC PARITY CONTRACT

**Version:** 1.0.0  
**Last Updated:** 2026-04-06

## 1) Shared C core is the only sonic source of truth

The shared DSP implementation in `core/sound_bubbles_dsp.c` and `core/sound_bubbles_dsp.h` is the **single authoritative definition** of sonic behavior across all targets.

- All platform builds (native, web, embedded) must route audio synthesis behavior through the shared C core.
- Any change that affects audible output (oscillator behavior, envelope curves, modulation, filter behavior, randomization, spawning/class behavior, gains, clipping, etc.) must be implemented in the shared core first.
- Platform layers may adapt transport and integration details only; they must not redefine DSP behavior.

## 2) Allowed platform differences

The following differences are explicitly allowed, provided they do not intentionally alter sonic intent:

- **I/O format differences:** sample format conversions (e.g., `float32`/`int16`), channel interleaving/layout differences, and output device-specific framing.
- **Scheduling granularity:** different callback/block sizes and scheduler cadence due to runtime constraints.
- **Floating-point epsilon tolerance:** small, bounded numerical differences caused by architecture/compiler/precision behavior.

These differences must remain within validation thresholds defined in this contract.

## 3) Forbidden divergences

The following are prohibited:

- **Platform-only DSP:** introducing or altering synthesis/audio-processing behavior on one platform that is not present in `core/sound_bubbles_dsp.c/.h`.
- **Web-only widening/modulation:** any browser-specific stereo widening, modulation, enhancement, or other audible embellishment absent from the shared core.
- **Differing spawn/class logic:** platform-specific changes to event spawning, bubble/class selection, class weighting, lifecycle semantics, or timing rules that affect resulting sound behavior.

If a sonic feature is desired, it must be added to the shared core and then consumed consistently by every platform.

## 4) Deterministic seed requirements and reset semantics

All platforms must support deterministic runs based on a shared seed and reset contract.

- A fixed seed must produce equivalent event/class sequences and materially matching block-level audio behavior across platforms (within tolerance).
- Reset must restore a clean deterministic baseline, including RNG state and all DSP state needed for reproducible output.
- Seed initialization and reset semantics must be identical at the contract level, even if platform plumbing differs.
- Any API that mutates deterministic state must document whether it is included in reset and reproducibility expectations.

## 5) Validation requirements

Changes affecting audio behavior must pass parity validation:

1. **Fixed-seed reproducibility:** repeated runs on the same platform with the same seed and reset sequence must be reproducible within tolerance.
2. **Block metrics parity:** for equivalent seeds/inputs, compare block-level metrics across target platforms (for example: RMS, peak, DC offset, event counts/class counts, and voice lifecycle counters).
3. **Render-diff thresholds:** rendered output comparisons must remain within agreed thresholds (e.g., max absolute error, RMS error, and any project-defined spectral/temporal bounds).

When thresholds are exceeded, the change is non-compliant until divergence is explained and contract updates are approved.

## 6) ESP32-S3 budget rule

The ESP32-S3 target is budget-constrained. New sonic features must satisfy bounded cost requirements:

- New features must fit within defined voice-count and CPU/memory budget limits for ESP32-S3 real-time operation.
- Avoid expensive per-voice additions unless there is a clear budgeted implementation path validated on-device.
- Prefer shared, bounded-cost approaches over unbounded per-voice complexity growth.
- If a feature cannot meet ESP32-S3 constraints, it must not be enabled as a parity-breaking platform special-case.

## 7) Change management and versioning

This contract is versioned and must evolve explicitly.

- Increment the contract version when requirements, tolerances, or policy constraints change.
- Every sonic-affecting PR must state whether parity behavior changed and whether this contract requires an update.
- Any intentional parity exception must be documented with scope, rationale, validation impact, and sunset/remediation plan.
- In case of conflict, this contract governs cross-platform sonic parity expectations unless superseded by a newer approved version.
