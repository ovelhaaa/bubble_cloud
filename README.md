# Bubble Cloud

Bubble Cloud is a shared C granular/micro-looping engine for a performance-responsive guitar effect, with four active host targets:

- a static browser editor/player that runs the engine through WebAssembly and an `AudioWorklet`;
- an offline command-line renderer and validation harness for deterministic WAV/metrics checks;
- a JUCE VST3/AU/Standalone plug-in for DAW use;
- an ESP-IDF integration layer for ESP32-style embedded hardware.

The repository is organized around one audio contract: hosts configure `bubble_engine`, pass mono audio into the shared DSP core, and receive stereo output. UI, storage, file I/O, WebAudio, I2S, and validation code live outside the real-time DSP path.

## Current repository layout

| Path | Purpose |
| --- | --- |
| `core/dsp/` | Platform-independent granular DSP implementation, voice pool, scheduling, diffusion, limiter, runtime metrics, and fixed audio constants. |
| `core/engine/` | Public `bubble_engine_*` API, parameter metadata, macro resolution, quality profiles, and preset snapshots. |
| `core/presets/` | C preset parser plus canonical factory preset JSON files. |
| `core/schema/` | Canonical preset parameter names, ranges, defaults, and types shared by C/JS validation. |
| `platform/wasm/` | Thin Emscripten wrapper exposing the engine to JavaScript. |
| `platform/offline/` | Native renderers, WAV I/O, metrics comparison, and the synthetic C test harness. |
| `platform/juce/` | JUCE VST3/AU/Standalone wrapper, host state, factory-preset selector, and DAW transport integration. |
| `platform/esp32/` | ESP-IDF application glue for codec/I2S, controls, OLED display, and preset storage. |
| `ui/web/` | Static browser UI, `AudioWorklet`, visualizer, preset import/export, factory preset cards, and generated WASM/macro assets. |
| `docs/` | Architecture, preset format, validation, macro, WASM, embedded, and sonic-parity documentation. |
| `tests/` | Python/MJS/C regression tests, audio fixtures, preset validators, and performance checks. |

## Engine state at a glance

- **Audio model:** mono input, shared 44.1 kHz DSP constants, stereo output, fixed two-second delay buffer, and a bounded 32-voice compiled pool with runtime quality profiles controlling the active voice limit.
- **Public controls:** 12 normalized musical macros (`density`, `bloom`, `motion`, `texture`, `space`, `gravity`, `memory`, `clarity`, `freeze`, `sparkle`, `warmth`, `mix`) plus developer/raw parameters when developer mode is enabled.
- **JUCE plug-in:** 20 factory presets, persistent advanced rhythm/pitch settings, and DAW tempo/PPQ synchronization for rhythmic presets.
- **Musical features:** semantic read regions (`attack_region`, `body_region`, `memory_region`), stereo spread, smart starts, envelope families, droplets, sustain diffusion, tone/memory shaping, freeze, reverse probability, shimmer/pitch modes, tempo/rhythm controls, and final limiter metrics.
- **Determinism:** `rng_seed`, quality profile, preset values, input audio, and block order are part of the reproducibility contract.
- **Real-time rules:** no heap allocation, I/O, locks, or UI work in the audio callback path; hosts provide the large delay buffer and apply parameter snapshots between blocks.

## Web editor / player

The web application is static HTML/CSS/JS plus a single-file Emscripten output (`ui/web/bubble_cloud_wasm.js`). It currently supports:

- WebAudio startup from a user gesture and DSP processing in an `AudioWorklet`;
- custom audio-file upload/drop playback and a synthetic pluck mode;
- quality-profile selection (`MCU_SAFE`, `MCU_PLUS`, `WEB_STANDARD`, `WEB_ULTRA`);
- Simple/Advanced/Developer UI modes over the macro/raw parameter model;
- factory preset cards, reset/compare workflows, undo/redo, musical randomization, and commit-to-base controls;
- canonical preset JSON import/export, current-state snapshot export, and processed MP3 export when the encoder worker is available;
- UI meters/visualizer driven by throttled runtime metrics so visual updates do not block audio.

Run it locally:

```sh
make wasm
cd ui/web
python3 -m http.server 8000
```

Then open `http://localhost:8000`, click **Ativar Áudio**, and choose either **Custom File** or **Synth Pluck**.

## JUCE plug-in

Configure and build the VST3 target:

```sh
cmake -S platform/juce -B build_juce -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build_juce --target BubbleCloud_VST3 --parallel 1
```

The plug-in stores macro and advanced factory-preset settings in the JUCE parameter state. Presets with tempo sync enabled read BPM and PPQ position from the DAW playhead; standalone hosts fall back to the engine's 120 BPM default. Successful VST3 builds copy `Bubbles.vst3` to `C:\VST` by default. Override the destination with `-DBUBBLES_VST3_COPY_DIR=<path>`, or disable installation with `-DBUBBLES_COPY_PLUGIN_AFTER_BUILD=OFF`.

## Building

```sh
make offline   # build build/sound_bubbles_render with gcc
make wasm      # generate ui/web/macro_matrix.js and ui/web/bubble_cloud_wasm.js with emcc
make all       # build both targets
make clean     # remove generated build/WASM/macro artifacts
```

`make wasm` requires an active Emscripten SDK. The WASM target uses `-s SINGLE_FILE=1` so the `.wasm` payload is embedded in the generated JavaScript module, which simplifies loading from the AudioWorklet context.

## Offline rendering and validation

Build the native renderer first:

```sh
make offline
```

Render a fixture with a preset:

```sh
./build/sound_bubbles_render \
  tests/fixtures/audio/test_in.wav \
  core/presets/factory/ambient-bloom.json \
  build/ambient-bloom.wav \
  --quality-profile WEB_STANDARD \
  --metrics-out build/ambient-bloom_metrics.csv \
  --repro-check
```

Compare two metrics captures:

```sh
./build/sound_bubbles_render compare ref_metrics.csv cand_metrics.csv \
  --max-threshold 1e-6 \
  --mean-threshold 1e-7
```

The synthetic harness is still available through the `DSP Harness` GitHub workflow and can also be compiled directly with `platform/offline/test_harness.c`, `core/dsp/sound_bubbles_dsp.c`, `core/engine/bubble_engine.c`, and `core/engine/bubble_macro_map.c`.

## Presets

The canonical external preset format is JSON with `schema_version: 3`, `engine_version`, metadata, `params`, and `macro_values`.

- `macro_values` is the product-facing musical control surface and should use normalized `0.0..1.0` values.
- `params` stores raw DSP/engine values such as `rng_seed`, `quality_profile`, `active_voice_limit`, freeze/reverse/motion settings, and other developer parameters.
- The C parser and JS schema are tolerant of legacy input where supported, but generated/exported presets should stay canonical.
- `core/presets/factory/` is the canonical factory-preset corpus used by C/offline tooling; `ui/web/src/presets/factoryPresets.js` contains the curated factory set shown in the browser UI.

See `docs/PRESET_FORMAT.md`, `docs/macro_ranges.md`, and `docs/macro_matrix.yaml` for details.

## Documentation map

- `docs/ARCHITECTURE.md` — shared engine/platform boundaries and real-time rules.
- `docs/DSP_DESIGN.md` — DSP behavior, states, scheduling, and quality-profile invariants.
- `docs/PRESET_FORMAT.md` — canonical JSON preset contract and migration expectations.
- `docs/WASM_INTEGRATION.md` — browser/WASM integration notes and parity guidance.
- `docs/EMBEDDED_PORTING.md` — embedded host responsibilities and callback constraints.
- `docs/VALIDATION_NOTES.md` — offline renderer metrics and comparison workflow.
- `docs/SONIC_PARITY_CONTRACT.md` — requirements for keeping targets sonically aligned.
- Root-level historical spec files (`DSP_SPECIFICATION.md`, `SOUND_BUBBLES_V1_BASELINE_SPEC.md`, `EMBEDDED_DSP_ARCHITECTURE.md`, `ENGINE_BEHAVIOR_SPEC.md`, `VALIDATION_PLAN.md`) remain useful as design history, but `core/`, `platform/`, `ui/web/`, `tests/`, and `docs/` are the current implementation references.

## CI and GitHub Actions

- `.github/workflows/tests.yml` runs the core DSP, preset, performance, and static-guard tests first, then runs offline C/WASM parity in a separate job.
- `.github/workflows/build-vst.yml` builds and verifies the Windows VST3/Standalone artifacts without installing them into the runner's local VST directory.
- `.github/workflows/build-verify.yml` builds both WASM and offline targets.
- `.github/workflows/deploy-pages.yml` builds the single-file WASM module, copies `ui/web/` into `dist/`, and deploys GitHub Pages.
- `.github/workflows/main.yaml` builds/runs the C DSP harness and uploads raw/WAV artifacts.
- `.github/workflows/render_all.yml` renders every JSON preset against a selected fixture and uploads WAV artifacts.
- `.github/workflows/render_diagnostics.yml` is a legacy synthetic-diagnostics workflow that skips gracefully unless `platform/offline/sound_bubbles_synth.c` is restored.

## Contributing

Before changing DSP behavior, presets, macros, quality limits, or real-time boundaries, update the matching documentation and run the relevant tests. Start with `docs/CONTRIBUTING.md` for repository rules and recommended checks.
