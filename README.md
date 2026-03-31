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

## GitHub Actions & Workflows

The project contains several GitHub Actions workflows with separated responsibilities:

- **`.github/workflows/deploy-pages.yml`**: Automatically builds the WebAssembly `.js` file, creates a `dist/` directory containing the frontend code from `web/frontend/`, and deploys the web interface to GitHub Pages. It runs on push to `main` or manual trigger.
- **`.github/workflows/main.yaml` (DSP Harness)**: Runs offline C tests to ensure the core DSP engine compiles and correctly synthesizes raw audio.
- **`.github/workflows/render_all.yml`**: An offline renderer that processes all JSON preset configurations in `presets/` against an input WAV file and uploads output WAV artifacts for downloading.
- **`.github/workflows/render_diagnostics.yml`**: Runs the offline synth renderer against multiple diagnostic scenarios (sine, triangle, expressive phrase) to test the engine's response to different waveforms and dynamic tracking, providing unique WAV artifacts per matrix run.