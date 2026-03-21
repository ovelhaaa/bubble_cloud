# Sound Bubbles DSP Core: Validation and Profiling Plan

This document outlines the validation, testing, and profiling methodology for the Sound Bubbles DSP core (`sound_bubbles_dsp.c` and `sound_bubbles_dsp.h`). The goal is to verify that the current implementation is functionally correct, stable, and performant enough for integration into an ESP32-S3 (N16R8) environment.

## 1. Validation goals
The following must be verified in the current DSP core:
*   **Functional correctness:** Verify that the core processes mono audio input into stereo audio output without producing artifacts, DC offsets, or mathematically invalid values (NaN/Inf). Ensure that linear interpolation, LUT windowing, and 1-pole IIR filters operate within expected bounds.
*   **State machine correctness:** Validate that `EngineState` transitions occur exactly as defined by the envelope tracker and timing thresholds (`transient_delta`, `sustain_thresh`, `tracking_thresh`, `noise_floor`). Ensure no invalid states are entered and timers (`burst_timer_ticks`) behave deterministically.
*   **Scheduler correctness:** Confirm that control-rate ticks (every 32 samples) accurately accumulate spawns based on `target_density` and that `Scheduler_SpawnImmediateBurst` and `Scheduler_RunTick` correctly bound spawns per tick to `SCHED_MAX_SPAWNS_PER_TICK`.
*   **Voice stealing behavior:** Ensure that when all 12 voices are active, new allocations strictly follow the deterministic stealing policy (oldest sustain -> oldest short -> oldest micro) and that young voices are protected by `STEAL_MIN_PHASE_THRESHOLD`.
*   **Write-head guard behavior:** Verify that `CheckGuardZoneDirectional` accurately detects when a read pointer approaches the write head and correctly forces a `VOICE_STATE_PREEMPT_FADING` state to prevent read/write collisions.
*   **Ducking behavior:** Validate that `smoothed_ducking_gain` reacts immediately to transient states (`duck_burst_level`, `duck_attack_coef`) and recovers smoothly during sustain/decay (`duck_release_coef`). Ensure the DSP natively owns this ducking target.
*   **Output stability:** Confirm that the final dry/wet mix in `ProcessBlock` (`master_dry_gain`, `master_wet_gain`) remains stable, bound between [-1.0, 1.0], and free of severe clipping or runaway gain.
*   **CPU/performance risk areas:** Identify operations that may exceed the ESP32-S3 processing budget, specifically the per-sample voice loop, PSRAM bandwidth for the 2-second circular buffer, and control-rate filter coefficient updates.

## 2. High-risk code paths
The hottest and riskiest parts of the current implementation include:
*   **Per-sample hot path:** The inner loop inside `ProcessBlock` where up to 12 voices are processed per sample. Operations here include `WrapFloatIndex`, `LinearInterpolate`, `LookupWindow`, and `CheckGuardZoneDirectional`. This loop dictates the baseline CPU cost.
*   **PSRAM-sensitive accesses:** The 2-second (88200 samples) `int16_t` circular buffer will likely reside in PSRAM on the ESP32-S3. Cache misses during scattered voice reads (especially for widely distributed `read_idx`) pose a significant memory bandwidth risk.
*   **Control-rate logic:** `Scheduler_RunTick` runs every 32 samples. While cheaper than per-sample processing, managing up to 12 active voices, updating ducking smoothing, and performing state transitions can introduce jitter if the tick takes longer than the remaining sample budget.
*   **Filter behavior:** The 3 global 1-pole IIR filters (Attack HPF, Flat Passthrough, Sustain LPF). The HPF is calculated as (input - LPF). Coefficient calculations (`expf`) are currently simplified but must remain strictly bounded to avoid instability.
*   **Burst spawning:** `Scheduler_SpawnImmediateBurst` is called on transient detection and can attempt to spawn multiple micro-bubbles simultaneously. The handling of `burst_immediate_count` and bounded loop execution is critical to avoid stalling the audio thread.
*   **LUT usage:** `LookupWindow` relies on explicit initialization of `grain_window_lut`. Incorrect initialization or out-of-bounds indexing (despite clamping) could lead to subtle audio artifacts or hard faults.
*   **Randomization:** The placeholder RNG (`RandomFloat()`/`rand()`) is currently used for pitch jitter, position offset jitter, and panning. If called frequently in the hot path, `rand()` can be slow and non-deterministic. It needs to be evaluated for performance impact.
*   **Edge conditions:** Buffer wrap-around logic (`WrapIntIndex`, `WrapFloatIndex`), exact zero-crossing behavior during voice preemption fading, and behavior when input completely saturates the envelope tracker.

## 3. Test harness architecture
To validate the C implementation offline, a minimal desktop test harness should be constructed:
*   **Input feed:** A simple C program reads a raw mono 32-bit float audio file (or generates synthetic signals) and feeds it to `ProcessBlock` in chunks (e.g., 64 or 128 samples, simulating an audio buffer).
*   **Output capture:** The harness writes the resulting stereo 32-bit float output from `ProcessBlock` to a raw audio file or a standard WAV file using a lightweight library (e.g., `dr_wav`).
*   **EngineConfig initialization:** The harness explicitly populates an `EngineConfig` struct with safe, curated defaults (e.g., defined tracking thresholds, ducking coefficients, and density targets) before calling `SoundBubbles_Init()`.
*   **Fixed test vectors:** The harness should accept command-line arguments to select predefined synthetic vectors (e.g., silence, Dirac impulse, sine burst with exponential decay) generated internally or loaded from disk.
*   **Internal state logging:** Optional logging hooks (enabled via `#ifdef DSP_PROFILING`) can print state transitions and voice allocation events to `stdout` or a CSV file for offline analysis.
*   **Repeatable tests:** The harness should be deterministic. The RNG seed should be fixed (e.g., `srand(42)`) during testing to ensure bit-accurate repeatable runs.

## 4. Functional test plan
Concrete tests to verify functional behavior:
*   **Silence input:**
    *   *Input:* Array of exactly 0.0f.
    *   *Expected behavior:* `smoothed_env` stays 0, state remains `ENGINE_STATE_SILENCE`, 0 voices active, output is 0.0f.
    *   *Inspection:* Verify `target_density` is 0 and no voices transition out of `VOICE_STATE_IDLE`.
*   **Single impulse:**
    *   *Input:* A single sample of 1.0f, followed by silence.
    *   *Expected behavior:* Immediate transition to `ENGINE_STATE_TRANSIENT_BURST`, triggering `Scheduler_SpawnImmediateBurst`.
    *   *Inspection:* Check that exactly `burst_immediate_count` micro-bubbles are spawned and ducking gain drops to `duck_burst_level`.
*   **Single plucked-note envelope:**
    *   *Input:* Fast attack (1-2ms), slow exponential decay (1-2s).
    *   *Expected behavior:* Transitions through TRANSIENT -> ATTACK -> SUSTAIN -> DECAY -> SILENCE.
    *   *Inspection:* Verify ducking recovers during sustain/decay. Check that bubble class mix shifts from Micro to Body as the envelope decays.
*   **Repeated transients:**
    *   *Input:* A train of impulses spaced 50ms apart.
    *   *Expected behavior:* Repeated retriggers of TRANSIENT state. Older micro-bubbles are preempted gracefully if the voice limit (12) is hit.
    *   *Inspection:* Monitor voice stealing logs. Ensure no hard clipping occurs when older voices are forcibly faded out.
*   **Sustained tone:**
    *   *Input:* Continuous sine wave at -6dBFS.
    *   *Expected behavior:* Reaches and holds `ENGINE_STATE_SUSTAIN_BODY`.
    *   *Inspection:* Check that `target_density` matches `density_sustain` and only Body/Short bubbles are spawned.
*   **Decaying tone:**
    *   *Input:* Slowly fading sine wave crossing the `sustain_thresh` and `tracking_thresh`.
    *   *Expected behavior:* State drops from SUSTAIN to SPARSE_DECAY.
    *   *Inspection:* `target_density` should match `density_decay`. Only 100% Body bubbles should spawn.
*   **High-level input near clipping:**
    *   *Input:* Signal peaking at 0.99f.
    *   *Expected behavior:* Envelope maxes out, but no internal math should overflow. Float to int16_t conversion strictly clamps.
    *   *Inspection:* Check for NaN/Inf in output and ensure `int16_t` conversion is safely bounded [-32768, 32767].
*   **Configuration edge cases:**
    *   *Input:* Valid signal, but config has `density_burst = 0` or `burst_immediate_count = 0`.
    *   *Expected behavior:* The DSP should not crash, divide by zero, or enter infinite loops. It should simply spawn nothing.
    *   *Inspection:* Verify safe handling of zero-values in scheduling calculations.

## 5. State machine test plan
Tests verifying transitions of `engine_state`:
*   **ENGINE_STATE_SILENCE:**
    *   *Entry:* `smoothed_env <= tracking_thresh`.
    *   *Exit:* `smoothed_env > tracking_thresh` (transitions to `SPARSE_DECAY` or `SUSTAIN_BODY`).
    *   *Failure symptom:* Engine gets stuck in SILENCE despite valid input, or toggles rapidly due to inadequate hysteresis.
*   **ENGINE_STATE_TRANSIENT_BURST:**
    *   *Entry:* `env_delta > transient_delta`.
    *   *Exit:* `burst_timer_ticks >= burst_duration_ticks`.
    *   *Failure symptom:* Fails to trigger on sharp attacks, or stays stuck if `burst_timer_ticks` is not incremented.
*   **ENGINE_STATE_ATTACK_ONGOING:**
    *   *Entry:* Timer expires from TRANSIENT_BURST, but `env_delta` is still moderately positive.
    *   *Exit:* `env_delta` levels off and `smoothed_env > sustain_thresh`.
    *   *Failure symptom:* Skips directly to SUSTAIN on slow swells.
*   **ENGINE_STATE_SUSTAIN_BODY:**
    *   *Entry:* Envelope is stable and above `sustain_thresh`.
    *   *Exit:* `smoothed_env < sustain_thresh` (drops to DECAY) or large `env_delta` (jumps to TRANSIENT).
    *   *Failure symptom:* Prematurely drops to DECAY during vibrato or tremolo.
*   **ENGINE_STATE_SPARSE_DECAY:**
    *   *Entry:* `smoothed_env < sustain_thresh` but `> noise_floor`.
    *   *Exit:* `smoothed_env < noise_floor` (to SILENCE).
    *   *Failure symptom:* Stays in DECAY forever due to DC offset or high noise floor configuration.

## 6. Scheduler and voice-allocation test plan
*   **Spawn accumulator behavior:** Feed a constant SUSTAIN state. Verify that `spawn_accumulator` correctly accumulates fractional spawns per tick and triggers exactly the right number of spawns over 1 second.
*   **Immediate burst spawning:** Trigger a TRANSIENT state. Verify `Scheduler_SpawnImmediateBurst` executes immediately and bypasses the slow accumulator logic.
*   **Bounded spawning:** Force a configuration where required spawns exceed `SCHED_MAX_SPAWNS_PER_TICK`. Verify the scheduler caps the spawns per tick and defers the rest, preventing CPU spikes.
*   **Dropping spawns:** Fill all 12 voices with protected young voices (`phase < STEAL_MIN_PHASE_THRESHOLD`). Attempt to spawn a new voice. Verify the spawn is cleanly dropped without attempting an invalid allocation.
*   **Deterministic stealing policy:** Fill all 12 voices (mix of Body, Short, Micro). Trigger a new spawn. Verify the allocation function correctly selects the oldest Sustain/Body bubble, then oldest Short, then oldest Micro.
*   **Protection of young voices:** Verify that `STEAL_MIN_PHASE_THRESHOLD` successfully prevents clicks by rejecting stealing candidates that have just been spawned.

## 7. Write-head guard test plan
*   **Forced early release:** Deliberately configure a long Body bubble with an offset that places its read pointer dangerously close to the `write_idx`.
*   **Prevention of read/write collision:** Verify that `CheckGuardZoneDirectional` accurately flags the collision before it happens and transitions the voice to `VOICE_STATE_PREEMPT_FADING`.
*   **False positives:** Ensure the guard zone math (handling circular buffer wrapping) does not accidentally trigger fading when the read pointer is safely on the opposite side of the buffer.
*   **Musical aggressiveness:** Listen to the output of forced fades. If they occur too frequently or sound abrupt, the guard zone threshold may be too large or the fade-out time too short.

## 8. Ducking test plan
*   **Transient-triggered wet suppression:** Trigger an impulse. Verify `smoothed_ducking_gain` immediately drops to `duck_burst_level`.
*   **Recovery during sustain/decay:** Verify that as the state moves to SUSTAIN, `smoothed_ducking_gain` smoothly interpolates back toward 1.0f using `duck_release_coef`.
*   **Response to repeated attacks:** Send rapid transients. Ensure ducking stays suppressed and does not flutter or pump unmusically.
*   **Stability of smoothed_ducking_gain:** Ensure the smoothing math does not overshoot 1.0f or undershoot 0.0f due to floating-point drift.
*   **Possible pumping or over-ducking:** Evaluate offline with real guitar DI tracks to confirm `duck_attack_coef` and `duck_release_coef` ranges provide a musical response, not a jarring "gate" effect.

## 9. Profiling plan
Desktop instrumentation should establish baseline metrics, which will guide the embedded profiling strategy:
*   **Average CPU cost per sample/block:** Measure the execution time of `ProcessBlock` over a 10-second test vector. Calculate the average time spent per audio sample.
*   **Worst-case burst cost:** Measure the max execution time of `ProcessBlock` specifically during a TRANSIENT state when `Scheduler_SpawnImmediateBurst` and voice allocation logic run simultaneously.
*   **Active voice count over time:** Track the number of active voices. Correlate CPU cost directly with the active voice count.
*   **Scheduler load:** Measure the execution time of `Scheduler_RunTick` in isolation to ensure control-rate logic is cheap.
*   **Memory bandwidth sensitivity:** On desktop, cache effects will hide PSRAM latency. However, profiling the number of non-contiguous array accesses (due to randomized `read_idx`) will highlight the memory bandwidth requirement.
*   **Translation to ESP32-S3:** Once desktop metrics are stable, implement ESP-IDF hardware timers (e.g., `esp_timer_get_time()`) around `ProcessBlock` and `Scheduler_RunTick` on the target hardware. Pay close attention to cache miss penalties when reading from the 2-second PSRAM buffer.

## 10. Instrumentation hooks
To support the profiling plan, minimal and removable instrumentation should be added, ideally guarded by `#ifdef DSP_PROFILING`:
*   `uint32_t active_voice_count`: Tracked per-tick.
*   `uint32_t steal_count`: Incremented every time a voice is forcibly preempted.
*   `uint32_t guard_release_count`: Incremented when `CheckGuardZoneDirectional` triggers a fade.
*   `uint32_t state_transition_counts[5]`: Histogram of how often the engine enters each state.
*   `float peak_wet_level`: Track the maximum absolute float value of the wet mix output before the final dry/wet combination.
*   `uint32_t spawn_dropped_count`: Incremented when a spawn is requested but all voices are protected by `STEAL_MIN_PHASE_THRESHOLD`.
*   *Implementation Note:* These variables should exist in a separate `DSP_Profiler` struct, passed optionally to `ProcessBlock`, keeping the core API clean.

## 11. Pass/fail criteria
The current baseline implementation passes validation if:
*   **No invalid state transitions:** The state machine strictly follows the defined paths; timers always expire correctly.
*   **No runaway spawning:** The spawn accumulator never spirals out of control; bounds checks (`SCHED_MAX_SPAWNS_PER_TICK`) hold.
*   **No voices stuck active forever:** Every spawned voice eventually reaches `VOICE_STATE_IDLE`, either naturally or via preemption.
*   **No NaN/Inf behavior:** The output stream contains 100% valid finite floats.
*   **No guard-zone runaway behavior:** The guard zone successfully prevents audio glitches without causing a chain reaction of infinite preemption loops.
*   **Acceptable ducking recovery:** Ducking recovers smoothly to 1.0f without overshooting.
*   **Acceptable scheduler boundedness:** The worst-case control tick execution time is bounded and predictable.

## 12. Recommended first fixes if tests fail
If the validation tests expose bugs, follow this prioritized diagnostic checklist (do not redesign the algorithm):
1.  **NaN/Inf in Output:** Check the 1-pole filter coefficients. Ensure `expf` inputs are sane. Check for division by zero in `LinearInterpolate` or window phase calculations.
2.  **Voices Not Spawning:** Check the envelope tracker constants. Ensure the input signal is actually crossing `noise_floor` and `tracking_thresh`. Verify `burst_immediate_count` is not zero.
3.  **Harsh Clicks/Pops:** Investigate voice stealing. Ensure `VOICE_STATE_PREEMPT_FADING` actually executes a fast fade-out and doesn't immediately jump to IDLE. Verify `CheckGuardZoneDirectional` is functioning.
4.  **Audio Stutter/Dropouts:** This indicates `ProcessBlock` is taking too long. Check the complexity of the RNG (`rand()`) in the hot loop. Check if `Scheduler_SpawnImmediateBurst` is trying to spawn too many voices synchronously.
5.  **State Machine Stuck:** Check `burst_timer_ticks` incrementation in the control-rate tick. Ensure hysteresis between `sustain_thresh` and `tracking_thresh` is wide enough to prevent rapid oscillation.

## 13. Final recommendation
The exact next engineering steps should follow this sequence:
1.  **Desktop harness first:** Build the C test harness described in Section 3 and run the functional and state machine tests. *Why:* Desktop debugging allows rapid iteration, easy inspection of memory (Valgrind/ASAN), and bit-accurate repeatable runs without the complexity of flashing hardware or dealing with RTOS tasks.
2.  **Then embedded profiling:** Once the desktop tests pass, wrap `ProcessBlock` in an ESP-IDF FreeRTOS task. Feed it a dummy buffer and use hardware timers to measure exact CPU cycles and PSRAM read latency. *Why:* The ESP32-S3's cache and PSRAM behavior cannot be accurately simulated on a desktop. You must know if the 12-voice per-sample loop fits in the available audio interrupt budget before building the rest of the pedal.
3.  **Then hardware integration:** Only after the DSP core proves it meets the embedded processing budget should you introduce I2S, DMA, codec drivers, and UI mapping. *Why:* Premature integration masks DSP bugs behind hardware configuration issues. Proving the core first guarantees that subsequent audio glitches are strictly hardware/driver issues, not algorithm flaws.