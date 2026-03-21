# Engine Behavior Spec: "Sound Bubbles" Granular Algorithm

## 1. Engine control variables
The global engine state is driven by a control-rate analysis tick (e.g., every 32 or 64 samples). The variables that govern the behavior are:

* `env_level`: Current smoothed RMS amplitude of the input signal.
* `env_deriv`: Rate of change of the envelope ($dE/dt$).
* `transient_flag`: Boolean triggered when `env_deriv` exceeds a pre-defined positive threshold.
* `recent_attack_strength`: Peak value of `env_deriv` during the last detected transient, decaying slowly over time.
* `note_activity`: A slow-decaying envelope tracking the overall macro presence of a note, used to determine if the signal is in sustain, decay, or silence.
* `wet_ducking_state`: A fast-attack, slow-release multiplier (0.0 to 1.0) applied to the global wet mix. Driven by `transient_flag`.
* `target_density`: Desired number of overlapping grains per second, derived directly from `env_level` and `note_activity`.
* `buffer_interest_region`: A pointer relative to the write head indicating the "center of mass" of the recent transient, guiding where sustain bubbles should read from.

## 2. Formal engine states
The engine operates as a deterministic state machine updated every control tick.

* **State A: Attack Burst**
  * **Entry Conditions**: `transient_flag` goes HIGH.
  * **Exit Conditions**: A fixed timer expires (e.g., 50ms) OR a new transient occurs.
  * **Behavior Priorities**: Hard ducking of existing wet mix. Aggressive voice stealing to clear old textures. Rapid spawning of micro-attack bubbles.
  * **Allowed Bubble Types**: Micro-attack, Short-intermediate.
  * **Interaction with Wet Ducking**: `wet_ducking_state` instantly drops to ~0.1, preventing the burst from masking the dry pick attack, then begins ramping up.

* **State B: Sustain Body**
  * **Entry Conditions**: Attack Burst timer expires AND `env_level` remains above the sustain threshold.
  * **Exit Conditions**: `env_level` drops below the tracking threshold OR `transient_flag` goes HIGH.
  * **Behavior Priorities**: Maintain a steady cloud of body bubbles. Slowly drift the read pointers backward.
  * **Allowed Bubble Types**: Body/Sustain, Short-intermediate.
  * **Interaction with Wet Ducking**: `wet_ducking_state` completes its ramp up to 1.0.

* **State C: Sparse Decay**
  * **Entry Conditions**: `env_deriv` is consistently negative AND `env_level` drops below the sustain threshold.
  * **Exit Conditions**: `env_level` hits the noise floor (Silence) OR `transient_flag` goes HIGH.
  * **Behavior Priorities**: Exponentially decrease spawn rate (`target_density`). Allow existing bubbles to ring out. Spawn sparse, long, smooth bubbles.
  * **Allowed Bubble Types**: Body/Sustain.
  * **Interaction with Wet Ducking**: `wet_ducking_state` remains at 1.0.

* **State D: Silence**
  * **Entry Conditions**: `env_level` is below the noise gate threshold.
  * **Exit Conditions**: `transient_flag` goes HIGH.
  * **Behavior Priorities**: Halt all new bubble spawning. Allow active voices to finish.

## 3. Bubble classes
To minimize per-voice CPU overhead, bubbles belong to discrete classes with pre-defined spectral and windowing profiles. Per-bubble DSP filters are removed; instead, bubbles route to class-specific mix busses with global filters.

* **Class 1: Micro-attack Bubbles**
  * **Sonic Role**: Transient shattering, broad-spectrum fizz, immediate impact.
  * **Duration Range**: 8–16 ms.
  * **Amplitude**: Scaled by `recent_attack_strength`.
  * **Read-position**: Tightly clustered near the write head ($W - 5\text{ms}$ to $W - 20\text{ms}$).
  * **Playback-rate**: 1.0x with slight bounded jitter ($\pm 5\%$).
  * **Pan**: Hard left/right alternating.
  * **Brightness**: Routed to an "Attack Bus" with a global High-Pass Filter.
  * **Window**: Fast Trapezoidal (1ms tapers, flat top) for maximum energy.
  * **Aging**: Fixed duration, no morphing.

* **Class 2: Short-intermediate Bubbles**
  * **Sonic Role**: Bridging the transient burst into the body of the note. Adding texture.
  * **Duration Range**: 20–40 ms.
  * **Amplitude**: Scaled by `env_level`.
  * **Read-position**: Moderate distance behind write head ($W - 20\text{ms}$ to $W - 100\text{ms}$).
  * **Playback-rate**: 1.0x with subtle LFO modulation.
  * **Pan**: Randomized within 50% left/right.
  * **Brightness**: Routed to a global neutral/flat bus.
  * **Window**: Raised Cosine (Hann).
  * **Aging**: Fixed duration.

* **Class 3: Body/Sustain Bubbles**
  * **Sonic Role**: Smooth harmonic tails, pitch clarity, organic decay.
  * **Duration Range**: 40–120 ms.
  * **Amplitude**: Scaled inversely to density (fewer bubbles = slightly louder individual bubbles).
  * **Read-position**: Drifting backward, tracking the `buffer_interest_region` ($W - 100\text{ms}$ to $W - 500\text{ms}$).
  * **Playback-rate**: 1.0x strictly stable to preserve pitch.
  * **Pan**: Narrow, slowly drifting.
  * **Brightness**: Routed to a "Sustain Bus" with a global Low-Pass Filter.
  * **Window**: Raised Cosine (Hann).
  * **Aging**: Fixed duration.

## 4. Bubble spawn rules
Spawning is calculated per control-rate tick using a probability counter driven by `target_density`.

* **Spawn Rate Evolution**:
  * In Attack Burst: Density is forced high (e.g., equivalent to 50 spawns/sec).
  * In Sustain Body: Density tracks `env_level` linearly (e.g., 15–25 spawns/sec).
  * In Sparse Decay: Density drops exponentially with `env_level` (e.g., 2–10 spawns/sec).
* **Attack Bursts**: When `transient_flag` triggers, the scheduler forces an immediate loop that spawns 2 to 4 Micro-attack bubbles instantly, ignoring the probability counter to guarantee transient density.
* **Sustain Maintenance**: The scheduler uses a steady Poisson-like or pseudo-random interval to spawn Body bubbles, ensuring the `target_density` is met without mechanical periodicity.
* **Decay Tapering**: As time since the last attack increases and `env_level` drops, the spawn interval lengthens drastically. Bubbles become isolated "drops" rather than a continuous stream.

## 5. Bubble parameter generation
When a spawn event is triggered, parameters are assigned strictly at the moment of creation:

* **Duration**: Randomized within the limits of the chosen Bubble Class.
* **Amplitude**: Deterministic, based on current `env_level` (Sustain) or `recent_attack_strength` (Attack), scaled by global Density settings.
* **Read Position**:
  * Micro-attack: Deterministic offset from write head + bounded random jitter (1-3ms).
  * Body/Sustain: Deterministic offset tracking the `buffer_interest_region` + randomized phase offset to prevent comb filtering.
* **Playback Rate**: Deterministic base rate (1.0x) + bounded randomized jitter (Micro-attack only).
* **Filter/Brightness**: Deterministic; assigned implicitly by the Bubble Class routing (Bus 1, 2, or 3). No per-voice filter calculations.
* **Pan**:
  * Micro-attack: Deterministic toggle (L, R, L, R).
  * Body/Sustain: Correlated with recent signal behavior (e.g., driven by a slow global sine LFO).
* **Window Type**: Deterministic, strictly bound to Bubble Class (Trapezoidal vs. Hann). Lookup table driven.
* **Lifetime**: Strictly equal to Duration. No recursive spawning.

## 6. Preemption and voice stealing
A strict, bounded voice pool (array of Bubble structs) is maintained. When a spawn is requested:

* **Allocating Inactive Slots**: The scheduler scans the pool linearly for a state of `inactive`. The first found is used.
* **Stealing Voices (Pool Full)**: If all voices are active, the scheduler forces preemption.
* **Priority Policy**:
  1. Never steal a Micro-attack bubble that is less than 50% through its lifespan.
  2. Always steal the oldest active Body/Sustain bubble first.
  3. If all bubbles are Micro-attacks, steal the oldest one.
* **Avoiding Clicks**: When a voice is preempted, it does NOT immediately snap to zero. It is forced into a rapid 1ms fade-out state. The new bubble parameters are loaded only after this 1ms fade completes. This effectively takes the voice offline for 1ms.

## 7. Scheduling model
To guarantee a predictable, bounded CPU load:

* **Per Sample (Audio Rate)**:
  * Advance the global write head.
  * For each active voice: increment phase, calculate window envelope from LUT, read from delay buffer with linear interpolation, apply envelope, multiply by amplitude/pan, and accumulate into the assigned class mix bus.
  * Sum class busses, apply global class filters, apply `wet_ducking_state`, and mix with dry.
* **Per Block (e.g., 32 samples)**:
  * Advance `wet_ducking_state` envelope.
  * Update global LFOs (pan, read-drift).
* **Per Control-Rate Tick (e.g., 64 samples)**:
  * Run envelope follower and discrete derivative.
  * Evaluate `transient_flag` and State Machine transitions.
  * Calculate `target_density` and `buffer_interest_region`.
  * Run the Scheduler loop (handle spawn timers, voice stealing, and preemption fades).
* **Only at Bubble Spawn Time**:
  * Random number generation for parameter bounding.
  * Struct assignment (setting duration, base read pointer, amplitude, class routing).

## 8. Practical simplifications
Strict pragmatic rules for v1 implementation:

* **Mandatory in v1**:
  * Bounded voice pool with strict age-based stealing.
  * 3 distinct Bubble Classes with fixed durations and window types.
  * Global class-based routing instead of per-voice filters (massive CPU save).
  * Wet ducking state (crucial for musicality).
  * LUT-based windowing and linear interpolation for buffer reading.
* **Optional Refinements (Do not include in core loop yet)**:
  * Zero-crossing estimation (dropped completely).
  * Pitch-shifting/Time-stretching (locked to 1.0x playback rate for v1).
  * Recursive "droplet" spawning (dropped completely).
  * Per-voice filter states or tilt EQs.

## 9. Reference implementation profile
Targeting the ESP32-S3 context, keeping DMA and RTOS overhead in mind:

* **Lean Profile (MVP)**
  * Active Bubbles: 8–12 max.
  * Control-Rate: Every 64 samples (~1.4ms at 44.1kHz).
  * Duration Ranges: Micro (10ms), Body (60ms).
  * Modulation: None. Pan is hard-panned or center.
* **Balanced Profile (Target)**
  * Active Bubbles: 12–16 max.
  * Control-Rate: Every 32 samples (~0.7ms).
  * Duration Ranges: Micro (8-16ms), Short (20-40ms), Body (40-120ms).
  * Modulation: Global LFO for body pan drift. Jitter on micro-attack read positions.
* **Richer Profile (Stretch)**
  * Active Bubbles: 16–20 max.
  * Control-Rate: Every 16 samples.
  * Duration Ranges: Full spread.
  * Modulation: Spline-based window LUTs, fractional delay interpolation (Hermite) instead of linear.

## 10. Final recommendation
The recommended engine model for the ESP32-S3 is the **Balanced Profile (12–16 voices) utilizing Global Class Busses**.

By completely removing per-voice filtering and relying on 3 distinct Bubble Classes routed to global "Attack", "Flat", and "Sustain" busses, the per-sample CPU cost is reduced strictly to phase accumulation, LUT window lookup, linear buffer interpolation, and MAC (multiply-accumulate) panning. Musicality and transient clarity are entirely preserved by the aggressive control-rate state machine (Transient Flag -> Wet Ducking + Micro-attack Burst + Voice Stealing), ensuring the engine feels highly reactive and dynamic without relying on sheer grain density or complex per-voice DSP.