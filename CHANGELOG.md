# Changelog

## 1.1.0-rc.1 — 2026-08-01

- Added 20 curated JUCE factory presets with tempo-synced rhythm, burst, pitch, and motion settings.
- Added true dual-engine stereo processing, Freeze/Capture MIDI performance controls, persistent A/B scenes, and automatable Morph.
- Added the Cloud Alive real-engine voice visualization and synchronized rhythm playhead.
- Calibrated Morph with perceptual curves for Density, Space, and Mix, plus hysteresis for Freeze and discrete scene parameters.
- Made core macro smoothing sample-rate invariant and removed dynamic parameter storage from the JUCE audio path.
- Added JUCE audio calibration across 44.1–96 kHz, irregular block sizes, all factory presets, mono compatibility, state restore, and editor rendering.
- Added pinned pluginval strictness-5 validation to the Windows GitHub Actions workflow.
- Replaced JUCE's placeholder manufacturer metadata with `Bubbles Audio`.
