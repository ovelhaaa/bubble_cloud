# Bubble Cloud - Web & Embedded DSP Architecture

This repository holds the `bubble_cloud` highly musical, performance-responsive granular guitar pedal. It is built from a core C DSP engine intended to run on the ESP32-S3, but it has been extended with WebAssembly (WASM) to offer an interactive preview running purely in the browser.

## Architecture

- `core/`: The platform-independent C DSP engine (`sound_bubbles_dsp.c`, `sound_bubbles_dsp.h`). This is strictly pure algorithm code.
- `platform/offline/`: Offline rendering utilities and test harnesses written in C.
- `platform/embedded/`: The ESP32 hardware execution layer (to be populated).
- `web/wasm/`: The C wrapper that exposes the DSP API to the JS world.
- `web/frontend/`: The static web assets for the GitHub Pages UI, including an `AudioWorklet` processor for real-time browser preview.

## Running the Web Version Locally

The web interface is composed purely of static HTML, CSS, JS, and a single `.js` file containing the WASM binary.

1. Ensure the WASM file is compiled:
   ```sh
   make wasm
   ```
2. Serve the `web/frontend/` directory. For example, using Python:
   ```sh
   cd web/frontend
   python3 -m http.server 8000
   ```
3. Open `http://localhost:8000` in your browser. Click **Start Audio** to begin the AudioContext, and **Play Test Signal** to hear simulated pluck sounds passing through the WASM-powered DSP engine in real time. Adjusting sliders will update the DSP parameters instantly.

## Limitations & Next Steps

- **Audio Inputs**: The browser demo currently only supports playing a synthesized sawtooth "pluck" sound to test transients. Live mic input (`getUserMedia`) or file upload playback are natural next steps for a full testing environment.
- **Embedded Implementation**: The `platform/embedded` module is currently a stub placeholder and needs the I2S audio drivers and UI mapping to the ESP32 hardware to be fully realized.
- **Real-Time CPU constraints**: Running a complex DSP in the browser using WASM handles well for this algorithm, but on very low-end mobile devices it might drop frames.

## Building

A standard `Makefile` is provided to compile both the offline tools and the WASM frontend.

- `make offline`: Builds the offline renderer and test harness using `gcc`.
- `make wasm`: Builds the WebAssembly output using `emcc`. You must have the [Emscripten SDK (emsdk)](https://emscripten.org/docs/getting_started/downloads.html) installed and active.

**Important Note for WASM:** We compile using `-s SINGLE_FILE=1` to embed the `.wasm` data directly into the `.js` glue code. This significantly simplifies loading inside the `AudioWorklet` scope since we don't have to battle relative path issues for fetching `.wasm` files.

## Read-region semantics (core preset model)

The core engine now models granular read positions using three semantic regions (distance behind the write head), rather than per-class ad-hoc offset+jitter pairs:

- **`attack_region`**: ~10–80 ms behind write head (captures pick/edge detail).
- **`body_region`**: ~80–250 ms behind write head (captures note core and early bloom).
- **`memory_region`**: ~250–900 ms behind write head (captures trailing texture and phrase memory).

Each region is represented as `min_offset_samples` / `max_offset_samples` in `EngineConfig_t`.
Spawned voices map from `(bubble class, engine state)` to one of these regions, then choose a deterministic PRNG position inside that range. This keeps presets musically meaningful while preserving exact repeatability for a fixed RNG seed.

## Preset schema notes (canonical v3: canonical out, tolerant in)

The browser UI now exports presets using a single canonical schema (`schema_version: 3`) with explicit metadata:

- `schema_version`, `engine_version`, `created_at`
- `preset_name`, `preset_slug`, `description`
- `ui_category`, `tags`
- `esp32_safe`, `quality_tier`
- `params`
- optional `metadata` (including preserved unknown input fields under `metadata.extra`)

Import is tolerant: legacy JSON without `schema_version` (or with old key names like `master_dry_gain` / `master_wet_gain`) is migrated to canonical form before apply. Out-of-range values are clamped, missing fields are filled with safe defaults, and warnings are surfaced in the UI.

### Export actions in UI

- **Export Preset**: saves only the current canonical preset object.
- **Export Current State**: saves a session snapshot (transport/mode + embedded canonical preset).

### Factory presets (UI)

The UI ships with six musical factory presets:

1. **Glass Bloom** — bright, wide stereo, lively attack, clean sustain.
2. **Soft Halo** — softer rounded cloud for clean chord work.
3. **Memory Pad** — darker long sustain with stronger memory/body behavior.
4. **Shimmer Dust** — sparkling granular spread with jitter + droplets.
5. **Frozen Attack** — transient-focused, more percussive attack fragmentation.
6. **Smoky Chorus** — darker pseudo-chorus texture for base layering.

Each preset declares `esp32_safe` and `quality_tier` so heavier configurations are explicitly labeled.

## New musical controls (spawn-time / bus-rate focused)

The core DSP now includes optional low-cost musical extensions designed for ESP32-safe execution:

- **Stereo**: `stereo_width`, `attack_pan_spread`, `sustain_pan_spread`
- **Texture**: `smart_start_enable`, `smart_start_range`, `envelope_variation`, `envelope_family`, droplet and attack-rate jitter controls
- **Diffusion**: wet bus soft-clip (`wet_drive`, `wet_clip_amount`, `wet_output_trim`) and sustain all-pass diffusion controls
- **Tone**: `tone_variation`, `attack_brightness`, `sustain_darkness`
- **Memory**: `memory_mix`, `memory_pull`, `memory_darkening`

All options remain deterministic for fixed `rng_seed`, avoid dynamic allocation in audio loops, and are implemented in the shared C core used by both WASM/browser and embedded targets.

## GitHub Actions & Workflows

The project contains several GitHub Actions workflows with separated responsibilities:

- **`.github/workflows/deploy-pages.yml`**: Automatically builds the WebAssembly `.js` file, creates a `dist/` directory containing the frontend code from `web/frontend/`, and deploys the web interface to GitHub Pages. It runs on push to `main` or manual trigger.
- **`.github/workflows/main.yaml` (DSP Harness)**: Runs offline C tests to ensure the core DSP engine compiles and correctly synthesizes raw audio.
- **`.github/workflows/render_all.yml`**: An offline renderer that processes all JSON preset configurations in `presets/` against an input WAV file and uploads output WAV artifacts for downloading.
- **`.github/workflows/render_diagnostics.yml`**: Runs the offline synth renderer against multiple diagnostic scenarios (sine, triangle, expressive phrase) to test the engine's response to different waveforms and dynamic tracking, providing unique WAV artifacts per matrix run.
