# Sound Bubbles: Strict v1 Baseline Specification

This document defines the normative, strict baseline specification for the first implementable version of the Sound Bubbles granular engine on the ESP32-S3. It resolves ambiguities from previous architectural drafts and provides conservative, deterministic constraints for embedded development.

## 1. Corrected and Normalized List of Constants

Constants are explicitly separated into compile-time fixed configurations and variables that should eventually be exposed as tunable UI parameters.

### Fixed Hardware & DSP Topology Constants
```c
// Audio format and topology
#define SYS_SAMPLE_RATE_HZ         44100
#define SYS_CONTROL_RATE_SAMPLES   32
#define SYS_MAX_VOICES             12
#define SYS_DELAY_BUFFER_SECONDS   2
#define SYS_DELAY_BUFFER_SAMPLES   (SYS_SAMPLE_RATE_HZ * SYS_DELAY_BUFFER_SECONDS) // 88,200

// Internal scheduler constraints
#define SCHED_MAX_SPAWNS_PER_TICK  4    // Hard limit on bubble creation per 32-sample block
#define SCHED_PREEMPT_FADE_SAMPLES 44   // Exactly 1ms at 44.1kHz
```

### Tunable Parameters (To be mapped to UI/Config)
*These parameters dictate the musical behavior and must be tunable during calibration, though they have recommended v1 defaults.*

```c
// --- Envelope & State Tracking ---
// (Values in linear amplitude, 0.0f - 1.0f)
#define TUNE_NOISE_FLOOR           0.001f
#define TUNE_TRACKING_THRESH       0.010f
#define TUNE_SUSTAIN_THRESH        0.050f
#define TUNE_TRANSIENT_DELTA       0.080f   // dE/dt required to trigger attack burst

// --- Ducking Dynamics ---
#define TUNE_DUCKING_ATTACK_TICKS  7        // ~5ms / 0.72ms per tick
#define TUNE_DUCKING_RELEASE_TICKS 138      // ~100ms / 0.72ms per tick
#define TUNE_DUCKING_TARGET_BURST  0.10f    // Wet mix level during attack
#define TUNE_DUCKING_TARGET_IDLE   1.00f    // Wet mix level during sustain

// --- Density Targets (Spawns per Second) ---
#define TUNE_DENSITY_BURST         50.0f
#define TUNE_DENSITY_SUSTAIN_MAX   25.0f
#define TUNE_DENSITY_DECAY_MIN     2.0f

// --- Burst Logic ---
#define TUNE_BURST_DURATION_TICKS  69       // ~50ms / 0.72ms per tick
#define TUNE_BURST_IMMEDIATE_COUNT 3        // Number of bubbles forced instantly on transient
```

## 2. Corrected Runtime Data Model

To eliminate ambiguity, floating-point math is restricted where integer math is safer (e.g., buffer indexing, timer countdowns). The abstract `buffer_interest_region` is replaced with `sustain_read_anchor_samples`, a deterministic integer offset relative to the write head.

### Tunable Configuration Structs
```c
typedef struct {
    float dur_min_ms;
    float dur_max_ms;
    int   offset_min_samples;
    int   offset_max_samples;
    float jitter_amount; // 0.0f to 0.05f
    // WindowType and BusRouting are implicit per class in v1, not tunable here.
} BubbleClassConfig;

typedef struct {
    float noise_floor;
    float tracking_thresh;
    float sustain_thresh;
    float transient_delta;

    float duck_attack_coef;  // Pre-calculated from TUNE_DUCKING_ATTACK_TICKS
    float duck_release_coef; // Pre-calculated from TUNE_DUCKING_RELEASE_TICKS
    float duck_burst_level;

    int   burst_duration_ticks;
    int   burst_immediate_count;

    float density_burst;
    float density_sustain;
    float density_decay;

    BubbleClassConfig class_micro;
    BubbleClassConfig class_short;
    BubbleClassConfig class_body;
} EngineConfig;
```

### Voice and Globals Structs
```c
typedef enum {
    VOICE_INACTIVE = 0,
    VOICE_ACTIVE,
    VOICE_PREEMPT_FADING
} VoiceState;

typedef struct {
    VoiceState  state;
    uint8_t     bus_routing;    // 0: Attack, 1: Flat, 2: Sustain
    uint8_t     window_type;    // 0: Trapezoidal, 1: Hann

    float       phase;          // 0.0f to 1.0f
    float       phase_inc;      // 1.0f / duration_in_samples

    float       read_ptr;       // Float for linear interpolation
    float       playback_rate;  // Delta added to read_ptr per sample

    float       amplitude;      // Static multiplier
    float       pan_l;          // Static L channel gain
    float       pan_r;          // Static R channel gain

    int         fade_samples_remaining; // Countdown for preemption fade
} BubbleVoice;

typedef struct {
    uint8_t     state;               // 0: Silence, 1: Burst, 2: Sustain, 3: Decay

    float       env_level;           // Current RMS
    float       env_prev;            // Previous RMS for derivative
    float       recent_attack_strength;

    float       wet_ducking_state;   // Current ducking multiplier (0.0 to 1.0)

    float       spawn_accumulator;   // Drives the scheduler
    float       target_density;      // Current spawns per second

    int         write_head;          // 0 to SYS_DELAY_BUFFER_SAMPLES - 1
    int         sustain_read_anchor_samples; // Fixed sample distance behind W during burst, drifts in sustain
    int         burst_timer_ticks;   // Countdown timer for STATE_ATTACK_BURST
} EngineGlobals;
```

## 3. Fixed Update-Rate Model

Timers are strictly stored and evaluated in **ticks** (for control-rate logic) or **samples** (for audio-rate logic), avoiding arbitrary float-millisecond checks in the core loops.

*   **Per-Sample (44.1 kHz):**
    *   Convert `int16_t` input to `float`, write to circular buffer.
    *   Advance W pointer (modulo wrap).
    *   For each active voice:
        *   Decrement `fade_samples_remaining` if fading. If 0, mark INACTIVE.
        *   Advance `phase += phase_inc`. If `>= 1.0f`, mark INACTIVE.
        *   Advance `read_ptr += playback_rate`. If out of bounds, wrap (via if/else, NO float modulo).
        *   Read buffer with linear interpolation (requires converting two `int16_t` samples to `float`).
        *   Apply LUT window, amplitude, fade mult, and pan. Accumulate to bus.
    *   Apply fixed 1-pole global filters to busses.
    *   Mix busses, apply `wet_ducking_state`, output `int16_t`.
*   **Per-Block / Control-Tick (Every 32 samples):**
    *   Calculate RMS (`env_level`) of the last 32 samples.
    *   Evaluate derivative vs `transient_delta`.
    *   Decrement `burst_timer_ticks` if > 0.
    *   Update State Machine (evaluating thresholds and timers).
    *   Update `sustain_read_anchor_samples` (e.g., `+= 1` to slowly drift backward during Sustain).
    *   Calculate `target_density`.
    *   Smooth `wet_ducking_state` toward its target using a 1-pole IIR equation.
    *   Run the Scheduler (`spawn_accumulator`).
*   **At Spawn Time (Invoked by Scheduler):**
    *   Linearly scan for an INACTIVE voice, or steal the lowest priority ACTIVE voice.
    *   Calculate pseudo-random jitter for duration and W offset.
    *   Calculate `phase_inc`.
    *   Initialize the `BubbleVoice` struct.
    *   (Max allowed spawns per control tick: `SCHED_MAX_SPAWNS_PER_TICK` to strictly cap CPU spikes).

## 4. v1-Safe Scheduler Model

The scheduler is tightly bounded to guarantee determinism.

*   **Spawn Accumulator:**
    At every control tick:
    `spawn_accumulator += (target_density * SYS_CONTROL_RATE_SAMPLES) / SYS_SAMPLE_RATE_HZ;`
*   **Burst-Spawn Rule:**
    If a transient occurs in this tick:
    1. Reset `spawn_accumulator = 0.0f`.
    2. Immediately spawn `TUNE_BURST_IMMEDIATE_COUNT` (e.g., 3) Micro bubbles.
    3. Transition to Burst state and set `burst_timer_ticks`.
*   **Sustain/Decay Spawn Limit:**
    If not a transient tick, while `spawn_accumulator >= 1.0f`:
    1. Spawn 1 bubble (Class determined by state probabilities).
    2. Subtract `1.0f` from `spawn_accumulator`.
    3. Abort the while loop if `spawns_this_tick >= SCHED_MAX_SPAWNS_PER_TICK`.
*   **Deterministic Voice Allocation/Preemption:**
    *   Look for `VOICE_INACTIVE`.
    *   If none, iterate all 12 voices to find the best candidate to steal.
    *   **Rule 1:** NEVER steal a voice where `bus_routing == 0` (Micro) AND `phase < 0.5f`.
    *   **Rule 2:** Find the voice with `bus_routing == 2` (Sustain) that has the HIGHEST `phase` (oldest). Steal it.
    *   **Rule 3:** If no Sustain bubbles exist, find the oldest voice overall that violates Rule 1.
    *   **Preemption Fade:** The stolen voice does NOT immediately accept new parameters. It enters `VOICE_PREEMPT_FADING`, `fade_samples_remaining = SCHED_PREEMPT_FADE_SAMPLES`, and `phase_inc` is suspended. The scheduler leaves the struct alone. The per-sample loop fades it to 0. Only when it hits 0 does it become `VOICE_INACTIVE`, making it available for the *next* control tick's scheduler.

## 5. Numeric Representation Recommendations

Given the ESP32-S3's architectural constraints (Single-precision FPU, PSRAM caching behavior):

*   **Main Circular Delay Buffer:** **Must be `int16_t`**.
    *   *Reasoning:* A 2-second float buffer takes 352KB, heavily thrashing the PSRAM cache when 12 asynchronous voices perform linear interpolation (reading 24 scattered memory addresses per sample). An `int16_t` buffer takes 176KB, halving the memory bandwidth requirement and significantly improving cache hit rates. The CPU cost of `(float)int_val / 32768.0f` during the read phase is negligible compared to a PSRAM cache miss.
*   **Voice Structs and Busses:** **`float`**.
    *   The 12 `BubbleVoice` structs and the 3 stereo accumulation busses easily fit in IRAM (fast internal SRAM). All DSP math (phase, envelopes, mixing) should remain single-precision `float` to leverage the FPU.
*   **Phase and Pointers:**
    *   `phase` (0.0 to 1.0) remains `float`.
    *   `read_ptr` remains `float`.
    *   Timers (`burst_timer_ticks`, `fade_samples_remaining`) must be `int`.
    *   Buffer offsets (`sustain_read_anchor_samples`, `write_head`) must be `int`.

## 6. Final v1 Baseline

The mandatory v1 implementation target is defined strictly by what is included and what is explicitly excluded to ensure the ESP32-S3 maintains a stable audio callback.

**Baseline Configuration:**
*   **12 Voices Maximum.** No dynamic scaling.
*   **1 Circular Buffer (`int16_t`, 88200 samples).**
*   **3 Global Busses.** (Attack: HPF, Flat: None, Sustain: LPF).
*   **32-Sample Control Tick.**
*   **Preemption Fade of 1ms.** (Deferring re-allocation until fade completes).

**Simplifications that MUST remain in place for v1:**
1.  **Linear Interpolation Only:** Do not use Hermite or Sinc interpolation.
2.  **1.0x Playback Rate for Body:** Pitch jitter is ONLY applied to Micro bubbles. Body bubbles must strictly increment `read_ptr` by `1.0f` per sample.
3.  **LUT-Based Windows:** Window envelopes must be pre-calculated arrays (e.g., 512 float values). No `sin()` or `cos()` calls in the audio loop.
4.  **Static Panning:** Panning is set once at spawn time. No LFO panning in the per-sample loop.

**Features Explicitly Postponed (DO NOT IMPLEMENT IN V1):**
*   Zero-crossing detection or periodicity alignment.
*   Recursive bubble spawning (droplets).
*   Pitch shifting or musical quantization.
*   Per-voice filter states or tilt EQs.
*   Reverse playback bubbles.
*   Stereo spread widening on Body bubbles.
*   Dynamic adjustment of `MAX_VOICES` based on CPU load.