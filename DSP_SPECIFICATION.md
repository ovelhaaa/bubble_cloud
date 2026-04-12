# DSP Specification: "Sound Bubbles" Granular Guitar Pedal

## 1. Core sonic thesis
The granular engine acts as a dynamic, reactive particle system synchronized with the instrument's performance. Instead of generating a static, continuous grain cloud, the algorithm spawns "bubbles" whose density, size, texture, and lifespan are deterministically bound to the input signal's envelope and transient profile. Strong transients (attacks) shatter the audio into dense, short, bright fragments. These fragments gradually decay into sparse, smooth, and elongated textures during the sustain and release phases. The effect inherently yields to new phrasing, ensuring playability, harmonic clarity, and respect for the player's intent.

## 2. High-level signal model
The system utilizes a mono input that feeds into a continuous circular delay buffer (e.g., 1–2 seconds) and a parallel envelope/transient analysis path. The analysis path drives a deterministic Bubble Scheduler running at control rate. The scheduler instantiates Bubble objects (grains) that read from the delay buffer with specific windowing, pitch/speed modulation, and filtering. The outputs of these active bubbles are summed, panned, and passed through a global stereo output stage. The dry signal path is kept entirely separate from the granular processing path to maintain zero latency and absolute phase integrity of the performance.

## 3. Bubble model
A "Bubble" is a discrete granular event within the engine. It is defined by the following explicit state parameters:
* `start_time`: System time when the bubble begins.
* `duration`: Total lifespan (window length) in milliseconds.
* `read_ptr`: Initial position in the circular buffer (relative to the write head).
* `playback_rate`: Speed and direction of reading (pitch shift/time stretch).
* `window_type`: Shape of the amplitude envelope (e.g., Hann for smooth sustain, Trapezoidal with fast tapers for percussive attacks).
* `amplitude`: Peak volume of the bubble.
* `filter_state`: Cutoff frequency for an internal 1-pole LP/HP filter (determines "brightness").
* `pan`: Stereo position.

Bubbles are strictly categorized into two types based on the input context at their time of creation:
* **Attack Bubbles**: Short duration (5–20ms), sharp window tapers, wide stereo spread, high filter cutoff (bright), slight randomized pitch micro-modulation for a "shattering" effect.
* **Sustain/Decay Bubbles**: Long duration (50–200ms), smooth Gaussian/Hann windows, narrow or slowly drifting stereo spread, low filter cutoff (darker/warmer), stable playback rate.

## 4. Engine states and transitions
The engine operates as a state machine driven by the input analysis stage.
* **State A: Transient (Attack)**: Triggered by a positive rate of change in the envelope ($dE/dt$) exceeding the transient threshold. Forces an immediate flush or rapid decay of existing Sustain bubbles (ducking) to prevent smearing. Rapidly spawns Attack Bubbles.
* **State B: Sustain**: Triggered when the envelope stabilizes post-transient. The scheduler transitions to spawning Sustain Bubbles.
* **State C: Decay**: Triggered when the envelope drops below a tracking threshold or exhibits steady negative $dE/dt$. The spawning rate of new bubbles decreases exponentially, leading to a sparse texture.
* **State D: Silence**: Triggered when the envelope is below the noise gate threshold. No new bubbles are spawned; existing bubbles complete their lifespan and the wet path goes silent.

## 5. Input analysis stage
The input signal is rectified and low-pass filtered to create a control-rate amplitude envelope $E(t)$. A leaky differentiator computes $dE/dt$.
* **Transient Detector**: A Schmitt trigger operating on $dE/dt$ identifies note onsets.
* **Energy Integrator**: A sliding window RMS calculates the local energy, scaling the global maximum amplitude for newly spawned bubbles.
* **Zero-Crossing Estimator** (Optional/Lightweight): Tracks rough periodicity to align bubble read positions with waveform cycles, minimizing destructive interference.

## 6. Bubble emission rules
Emission is governed by a stochastic-deterministic scheduler tied to the engine state.
* **During Attack**: Emission rate is high (e.g., 50–100 bubbles per second). Bubbles are spawned with deterministic timing but randomized spatial (pan) and spectral (filter) parameters to simulate a physical burst.
* **During Sustain**: Emission rate is medium and steady (e.g., 10–30 bubbles per second).
* **During Decay**: Emission rate drops proportionally to $E(t)$, creating physical separation between the remaining bubbles.

The total concurrent bubble count is strictly bounded (e.g., max 24 or 32 overlapping voices). If the pool is full, new transient events ruthlessly steal voices from the oldest active sustain bubbles to guarantee transient clarity.

## 7. Bubble lifetime and burst behavior
* **Lifespan**: Duration is assigned at emission. Attack bubbles last 5–20ms. Sustain bubbles last 50–200ms, scaling inversely with the envelope's rate of decay (a faster signal decay yields shorter, more separated bubbles).
* **Burst Behavior**: When an Attack Bubble reaches the end of its window, it can optionally trigger a micro-feedback event or spawn a secondary, quieter "droplet" bubble (a strict 1-to-1 or 1-to-0 recursion) to simulate physical scattering. This recursion is strictly bounded to a single generation to prevent runaway CPU usage.

## 8. Grain/event scheduling model
The system uses a fixed-size pool of pre-allocated Bubble objects to completely avoid dynamic memory allocation. A control-rate tick evaluates the input envelope and transient detector.
If a new bubble is scheduled, the engine scans the pool for an inactive Bubble. If none are found during a transient, it preempts the oldest active bubble. Once activated, the Bubble calculates its windowing and buffer read indices per audio-rate sample until its duration expires, at which point its state is cleared and it returns to the inactive pool.

## 9. Read-position strategy within the rolling audio buffer
The write head constantly records the mono input. Bubble read positions ($R$) are placed relative to the write head ($W$).
* **Attack Bubbles**: $R$ is placed very close to $W$ (e.g., $W - 10\text{ms}$) to capture the immediate transient. The playback rate incorporates slight random jitter.
* **Sustain Bubbles**: $R$ is placed further back (e.g., $W - 50\text{ms}$ to $W - 500\text{ms}$). The position drifts slowly backward over time to scan the decaying note, preventing the static "machine gun" artifact of looping a single waveform cycle.
Read pointers are clamped to prevent crossing the write pointer. If $R$ approaches $W$ too closely during playback, the bubble's window is forced into an early zero-crossing taper.

Current implementation codifies this strategy with semantic preset regions:
* **attack_region**: 10–80ms
* **body_region**: 80–250ms
* **memory_region**: 250–900ms

Regions are stored as sample ranges behind the write head and selected deterministically per spawn via the core PRNG, so presets remain musically interpretable and reproducible.

## 10. Dry/wet interaction philosophy
The granular effect operates purely in parallel to the dry signal. The wet path is dynamically ducked by new transients. When a strong attack is detected, the global mix of the granular engine is temporarily suppressed (e.g., a rapid 10ms fade down) and then smoothly ramps back up (e.g., 50–100ms fade up). This guarantees the dry pick attack is always the psychoacoustic focal point, and the granular texture is perceived as a resultant reaction (a bloom or tail) rather than a masking layer.

## 11. Techniques for preserving musicality and note clarity
* **Transient Ducking**: Existing textures are suppressed on new attacks to make room for new notes.
* **Harmonic Windowing**: Sustain bubbles use Hann/Gaussian windows to minimize spectral splatter, keeping the harmonic content focused and musical.
* **Intrinsic Band-limiting**: Attack bubbles are automatically high-pass filtered to keep the low end clear and punchy; Sustain bubbles are low-pass filtered to push them back in the mix and prevent high-frequency fatigue.
* **Sparse Decay**: As the input note decays, the engine explicitly reduces the density (overlap) of bubbles. Rather than sustaining a continuous drone, the texture breaks apart into discrete, audible "drops," maintaining the rhythm and decay profile of the original performance.

## 12. Control abstraction for the future user interface
Parameters are exposed via macro-level, physics-inspired labels rather than raw DSP values:
* **Density / Bubbles**: Controls the base emission rate scaling and maximum concurrent voices.
* **Viscosity / Smoothness**: Controls the ratio of Attack to Sustain bubble lifespans and window shapes (lower = shorter, sharper fragments; higher = longer, smoother blooms).
* **Current / Flow**: Controls the read-pointer drift speed and playback rate modulation (pitch drift/wobble).
* **Surface Tension / Threshold**: Adjusts the sensitivity of the transient detector and the aggressiveness of the ducking behavior.
* **Mix**: Standard parallel Dry/Wet balance.

## 13. Failure modes and anti-patterns
* **Smearing**: If transient detection fails or ducking is too slow, overlapping notes will turn into an atonal wash. *Addressed by:* Aggressive voice-stealing and ducking on new transients.
* **Machine-gunning**: If read pointers remain static during sustain, the same waveform is looped rigidly. *Addressed by:* Continuous read-pointer drift and randomized phase offsets.
* **CPU Overload**: If emission rate exceeds the processing budget. *Addressed by:* A strict fixed-size voice pool and deterministic scheduling limits.
* **DC Offset/Clicks**: If read pointers cross the write head or windows are abruptly cut. *Addressed by:* Read/write pointer bounds checking and guaranteed zero-crossing tapers on preemption.

## 14. Minimum viable version of the algorithm
* Mono in, Mono out.
* Fixed pool of 16 granular voices.
* Simple envelope follower and discrete derivative for transient detection.
* 2 deterministic states: Attack (spawns 10ms rectangular-windowed grains near the write head) and Sustain (spawns 100ms Hann-windowed grains drifting backwards).
* Hard voice stealing on new transients.
* Fixed internal filters (Attack = HPF @ 1kHz, Sustain = LPF @ 2kHz).

## 15. Scalable future extensions that do not break the core concept
* **Stereo Spread**: Panning alternating bubbles hard left and right for expansive width.
* **Pitch Quantization**: Tuning the playback rate of Sustain bubbles to exact musical intervals (e.g., octaves, fifths) using fixed read-rate ratios.
* **Reverse Bubbles**: Allowing a percentage of Sustain bubbles to read backwards through the buffer.
* **Multi-tap Burst**: Attack bubbles explicitly spawning 2–3 delayed, quieter copies of themselves to simulate bouncing on a hard surface.

## 16. Web/Embedded terminology glossary (consistency guard)
The Web UI keeps internal parameter keys unchanged for preset/WASM compatibility, but uses DSP-semantic labels:

* `duck_burst_level` → **Attack Duck Target**  
  Higher value = less ducking during transient burst (wet remains more present). Lower value = deeper ducking.
* `duck_attack_coef` → **Duck Engage Speed**  
  Higher value = duck engages faster toward the attack target. Lower value = slower engagement.
* `duck_release_coef` → **Duck Recovery Speed**  
  Higher value = recovery back to unity is faster. Lower value = slower recovery.
* `density_decay` → **Tail Density**  
  Higher value = denser spawn activity in sparse decay tail. Lower value = sparser tail.
* `sustain_thresh` → **Sustain Entry Threshold**  
  Higher value = requires stronger envelope to enter sustain. Lower value = enters sustain earlier.

---

## Design Summary
* **Transient-Driven Architecture**: Behavior is strictly divided into Attack and Sustain phases governed by input envelope derivatives.
* **Deterministic Bounding**: Concurrent grains are capped (e.g., 24 voices) via a pre-allocated memory pool to guarantee constant CPU load.
* **Dual Bubble Typology**: Fast, bright, sharp bubbles handle transients; slow, dark, smooth bubbles handle tails.
* **Ducking & Voice Stealing**: New transients aggressively suppress the wet mix and steal voices from existing sustain textures to preserve phrasing clarity.
* **Dynamic Density**: Bubble emission rate and density decay proportionally with the input envelope, preventing unmusical drones.
* **Read-Pointer Drift**: Read positions continuously scan backwards during sustain to avoid static looping artifacts.
* **Parallel Processing**: Dry path remains untouched; the wet path blooms reactively behind the dry signal.
* **Macro Controls**: UI exposes high-level physics metaphors (Viscosity, Flow) rather than granular DSP parameters.
* **Built-in Filtering**: Intrinsic high-pass on attacks and low-pass on sustains maintain spectral balance without user intervention.
* **Safe Preemption**: Forced fast-taper windowing is applied when a bubble is preempted to prevent clicks and pops.
