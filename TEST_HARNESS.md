# Sound Bubbles DSP Core: Offline Test Harness

This document describes the minimal, dependency-free C test harness designed to validate and profile the Sound Bubbles DSP core offline, before introducing any RTOS or embedded hardware complexity.

## 1. Harness design
The test harness is a single C file (`test_harness.c`) that compiles natively alongside the existing `sound_bubbles_dsp.c` and `sound_bubbles_dsp.h`. It acts as a static "fake audio thread," generating synthetic 32-bit float audio vectors and passing them block-by-block into the DSP core's `SoundBubbles_ProcessBlock` function.

**Key Design Choices:**
*   **Deterministic Execution:** A fixed random seed (`srand(42)`) ensures bit-accurate outputs across consecutive runs, allowing regression tracking.
*   **No RTOS/Hardware Drivers:** I2S, DMA, and multicore locks are excluded. The focus is purely on mathematical correctness and state-machine transitions.
*   **Raw Output:** To avoid third-party dependencies (like `dr_wav`), the harness writes raw, interleaved stereo 32-bit float files (`.raw`) easily loadable into Audacity or Python for analysis.
*   **Block-Based Processing:** The harness loops `ProcessBlock` over a predefined block size (e.g., 32 samples) exactly as an embedded audio interrupt would.

## 2. Test vector set
The harness generates five distinct 5-second synthetic test vectors internally, specifically designed to trigger every engine state:
1.  **Silence (`TEST_VECTOR_SILENCE`):** Pure 0.0f. Verifies the engine remains in `ENGINE_STATE_SILENCE`, does not spawn bubbles, and outputs clean 0.0f.
2.  **Impulse (`TEST_VECTOR_IMPULSE`):** A single 1.0f sample at 100ms. Verifies `ENGINE_STATE_TRANSIENT_BURST` triggering, immediate micro-bubble spawning, and instantaneous ducking.
3.  **Plucked Envelope (`TEST_VECTOR_PLUCKED_TONE`):** A 440Hz sine wave enveloped by a fast attack and exponential decay. Verifies smooth transitions through TRANSIENT -> ATTACK -> SUSTAIN -> DECAY -> SILENCE, and ducking recovery.
4.  **Repeated Transients (`TEST_VECTOR_REPEATED_TRANSIENTS`):** An impulse train every 200ms. Verifies retriggers, ducking hold behavior, and deterministic voice stealing when the 12-voice limit is hit.
5.  **Sustained Sine (`TEST_VECTOR_SUSTAINED_SINE`):** A continuous 220Hz sine wave. Verifies stable `ENGINE_STATE_SUSTAIN_BODY` holding and continuous control-rate scheduling without runaway spawn accumulation.

## 3. EngineConfig baseline for testing
The harness explicitly initializes a safe, curated `EngineConfig_t` baseline designed to reliably hit algorithmic thresholds:
*   **Envelope tracking:** `noise_floor` = 0.001f, `tracking_thresh` = 0.01f, `sustain_thresh` = 0.1f, `transient_delta` = 0.05f.
*   **Ducking:** `duck_burst_level` = 0.2f, with fast attack (`0.99f`) and slow release (`0.999f`).
*   **Density:** Burst = 50.0f, Sustain = 15.0f, Decay = 5.0f.
*   **Bubble Classes:** Micro Attack (5-15ms, Hann window), Short Intermediate (20-50ms, Hann), Sustain Body (80-200ms, Tukey-like).
This baseline guarantees that synthetic vectors will cleanly cross the necessary state boundaries.

## 4. Logging and assertions
To verify stability without heavy logging overhead, the harness utilizes strict C `assert()` checks during runtime:
*   **Output Validity:** Asserts that every output float is neither `NaN` nor `Inf`, and broadly bound-checks the sum to prevent explosive filter instability.
*   **State Validity:** Asserts that `engine_state` remains within defined enum bounds (0 to 4) after every block.
*   **Ducking Limits:** Asserts that `smoothed_ducking_gain` never drifts outside the `[0.0, 1.0]` range due to precision errors.
*   *Note:* If an assertion fails, the process aborts immediately, pointing directly to the offending block. Console `printf` statements are used only to indicate test progress.

## 5. Minimal file layout
The testing environment requires only three source files in the same directory:
```
.
├── sound_bubbles_dsp.h    // Frozen DSP public contract
├── sound_bubbles_dsp.c    // Approved DSP implementation
└── test_harness.c         // The offline test execution environment
```

## 6. Complete `test_harness.c`
*(The complete C code is provided in the actual `test_harness.c` file in the repository).*

## 7. Minimal `main()` flow
The execution flow is straightforward and synchronous:
1.  Print start message.
2.  For each `TestVectorType_t` (Silence to Sustained Sine):
    a. Reset `srand(42)`.
    b. Load baseline `EngineConfig_t` and call `SoundBubbles_Init`.
    c. Allocate memory and generate the specific synthetic input vector.
    d. Loop through the input array in chunks of 32 samples, calling `SoundBubbles_ProcessBlock`.
    e. Assert state and output validity inside and after the loop.
    f. Write the resulting stereo float buffer to a `.raw` file.
    g. Free memory.
3.  Print success message and exit 0.

## 8. Suggested compile command
Because the code uses standard C99/C11 features and math functions, compile it with GCC using the standard math library link (`-lm`):

```bash
gcc test_harness.c sound_bubbles_dsp.c -o test_harness -O2 -Wall -Wextra -lm
```

To run it:
```bash
./test_harness
```

## 9. What to inspect first after running it
Once the harness completes without assertion failures, you will have five `.raw` files. Import these into an audio editor (e.g., Audacity: File -> Import -> Raw Data -> 32-bit float, 2 Channels, 44100Hz).
**Inspect the following:**
1.  `test_out_silence.raw`: Must be a perfectly flat line (absolute zero).
2.  `test_out_impulse.raw`: Look exactly at the 100ms mark. You should see a dense burst of very short grains (micro-bubbles), verifying `Scheduler_SpawnImmediateBurst`.
3.  `test_out_pluck.raw`: Check the density of the granular cloud. It should be dense at the attack, thinning out to longer, sparser grains as the sine wave decays, eventually going silent.
4.  `test_out_transients.raw`: Ensure there are no harsh clicks indicating invalid voice stealing or failed write-head guard logic when the voices overlap.
5.  `test_out_sustain.raw`: Verify a continuous, rolling texture (body grains) with no runaway CPU spiking or sudden drops in density.