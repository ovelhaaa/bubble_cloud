# Changelog

## 1.1.0-rc.2 — 2026-09-26

- Defined an explicit stereo wet summing law: the two decorrelated engine wet fields are summed with an equal-power `1/sqrt(2)` factor (`BubbleCloudEngineWrapper::sumStereoBus`), keeping mono, dual-mono and stereo wet levels predictable and preserving the M1 stereo width without using the limiter as a gain compensator. Dry stays channel-local and unscaled.
- Reworked final-bus telemetry: `peakLeft`, `peakRight`, `limiterGain` and the new `clipCount` are now measured after the shared limiter, so they report the signal delivered to the host instead of per-engine pre-limiter wet metrics.
- `SoundBubbles_ApplyFinalLimiter`/`bubble_engine_apply_final_limiter` now return the minimum limiter gain of the block for lock-free final-bus telemetry.
- Strengthened the BPM/PPQ fallback test with a scripted host playhead: 90 BPM with valid PPQ, a temporary BPM loss with PPQ still advancing, BPM reappearance, and a genuine transport jump, asserting `lastValidHostBpm`, `expectedNextPpq` and `syncRhythmPhase()` call counts.
- Added stereo architecture regressions for dry locality, wet crossing, SPACE width, dual-mono loudness/limiter bounds, and a 44.1/48/88.2/96 kHz by 32/64/127/256/512/2048 sample-rate/block-size matrix.
- Added `docs/STEREO_SUMMING_LAW.md` documenting the bus law, its justification and the telemetry contract. No changes to memory distribution, sparkle voicing, microdetune, reverse probability, parameters, UI or presets.

## 1.1.0-rc.1 — 2026-08-01

- Added 20 curated JUCE factory presets with tempo-synced rhythm, burst, pitch, and motion settings.
- Added true dual-engine stereo processing, Freeze/Capture MIDI performance controls, persistent A/B scenes, and automatable Morph.
- Added the Cloud Alive real-engine voice visualization and synchronized rhythm playhead.
- Calibrated Morph with perceptual curves for Density, Space, and Mix, plus hysteresis for Freeze and discrete scene parameters.
- Made core macro smoothing sample-rate invariant and removed dynamic parameter storage from the JUCE audio path.
- Added JUCE audio calibration across 44.1–96 kHz, irregular block sizes, all factory presets, mono compatibility, state restore, and editor rendering.
- Added pinned pluginval strictness-5 validation to the Windows GitHub Actions workflow.
- Replaced JUCE's placeholder manufacturer metadata with `Bubbles Audio`.
