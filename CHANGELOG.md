# Changelog

## 1.2.0-rc.1 — 2026-09-26

- **M2 — Musical Cloud Character.** Internal musicality/depth pass with no new UI controls, no new effects and no public parameter changes.
- Memory: read offsets now follow a non-uniform, recent-weighted temporal distribution (~60% recent / ~25% medium / ~15% deep memory). Deep memory is a bounded, occasional ghost; the recent floor keeps the cloud connected to the current phrase. Deterministic and control-rate only.
- Stereo coherence: the two engines now share an event stream (`coherence_rng_state`) while keeping a per-channel spatial stream. Coherence is phrase-state driven (attacks ~0.80, sustain ~0.22, decay ~0.20, freeze ≤0.12), aligning important events without collapsing to mono. Added the internal `bubble_engine_set_channel_decorrelation` API; the old unconditional `rng_state` XOR is gone, so preset/seed changes can no longer accidentally synchronise the two channels.
- Sparkle: fixed 12-TET fifth `2^(7/12)` for `FIFTH`; `SHIMMER` is now a weighted interval cloud (unison, +12, +7, +19, progressively rarer) and `SPARKLE=0` stays unison. Fixed pitch-mode overrides are preserved.
- Microdetune: a fixed ±2–8 cent per-grain detune chosen at birth and held for the whole grain lifetime (attack ±2, short ±4, sustain ±6, freeze ±8). Never an LFO. Applied after the offset guard to avoid tripping the pitch-up clamp.
- Reverse probability is now context-conditioned: rare on attacks, progressively higher through sustain/decay/freeze, so the cloud unfurls backwards after the event. `MOTION` and `REVERSE_SWELL` keep working.
- Tests: new `tests/dsp/m2_character_harness.c` (+ pytest runner) covers memory distribution, Sparkle interval/FIFTH, microdetune bounds/lifetime/reproducibility, context reverse, determinism and the 44.1/48/88.2/96 kHz × 32/64/127/256/512/2048 matrix. The wrapper stereo probe gained M2 coherence/side checks and an M2-aware limiter bound. `scripts/m2_validation.py` renders comparative M1/M2 scenarios with RMS, peak, correlation, side/mid, voices and limiter metrics.
- Contracts preserved: stereo summing law, dry locality, shared final limiter, sample-rate invariance, BPM/PPQ transport, voice stealing, STRUM scheduling and offline/WASM parity. No allocation, locks or I/O in the callback.

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
