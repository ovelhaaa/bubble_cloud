#ifndef SOUND_BUBBLES_DSP_H
#define SOUND_BUBBLES_DSP_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "../engine/bubble_quality.h"

// --- Ring Buffer Storage Backend (M4C) ---
// Compile-time storage format:
//   JUCE / VST / Standalone / Desktop -> float32
//   WASM / Web                        -> float32
//   MCU_SAFE / MCU_PLUS (Embedded)    -> int16
// Build target controls backend; runtime quality profile (MCU_SAFE, MCU_PLUS,
// WEB_STANDARD, WEB_ULTRA) controls voices, interpolation, and grain budgets.
#ifndef BUBBLES_RING_FLOAT
  #if defined(BUBBLES_TARGET_MCU) || defined(ESP_PLATFORM) || defined(__XTENSA__) || defined(BUBBLES_RING_INT16)
    #define BUBBLES_RING_FLOAT 0
  #else
    #define BUBBLES_RING_FLOAT 1
  #endif
#endif

#if BUBBLES_RING_FLOAT
typedef float BubbleRingSample_t;
#define BUBBLES_RING_SAMPLE_IS_FLOAT 1
#else
typedef int16_t BubbleRingSample_t;
#define BUBBLES_RING_SAMPLE_IS_FLOAT 0
#endif

// Fast inlined storage read helpers
static inline float Ring_ReadNormalizedSample(const BubbleRingSample_t* buffer, int32_t index) {
#if BUBBLES_RING_FLOAT
    return buffer[index];
#else
    return (float)buffer[index] * (1.0f / 32767.0f);
#endif
}

static inline BubbleRingSample_t Ring_ReadSample(const BubbleRingSample_t* buffer, int32_t index) {
    return buffer[index];
}

// --- System & Algorithmic Constants ---
#define BUBBLES_BLOCK_SIZE 32
#define BUBBLES_MAX_VOICES BUBBLE_ENGINE_MAX_VOICES
// Preemption fade is a musical time (1 ms) converted to samples per engine
// sample rate. BUBBLES_FADE_SAMPLES remains the 44.1 kHz reference value used
// by legacy callers and tests; the DSP uses engine->fade_samples instead.
#define BUBBLES_FADE_MS 1.0f
#define BUBBLES_FADE_SAMPLES 44
#define BUBBLES_GUARD_ZONE_SAMPLES 64
// Reference rate used to interpret time-denominated fields (read regions,
// diffusion delay, ...) that are authored/stored as 44.1 kHz sample offsets.
#define BUBBLES_REFERENCE_SAMPLE_RATE 44100.0f
#define BUBBLES_REFERENCE_SAMPLE_RATE_INT 44100
#define SCHED_MAX_SPAWNS_PER_TICK 3
#define BUBBLES_PENDING_SPAWN_CAPACITY 4
#define BUBBLES_SUSTAIN_DIFFUSION_MAX_DELAY 96
#define BUBBLES_MACRO_COUNT 12

// Read-position interpolation quality (M3.2B). Linear is the low-cost baseline
// kept for the MCU quality profiles; WEB_STANDARD/WEB_ULTRA select 4-point cubic
// Hermite (Catmull-Rom). The selection is derived from EngineConfig_t
// .quality_profile and cached on the engine so the render loop only branches on
// a plain flag.
typedef enum {
    BUBBLES_INTERPOLATION_LINEAR = 0,
    BUBBLES_INTERPOLATION_HERMITE = 1
} BubbleInterpolationMode_t;

// --- M2 musical character constants ---

// Exact 12-TET interval ratios used by fixed pitch modes and the weighted
// Sparkle interval cloud. 2^(7/12) is the tempered fifth (not the just 3/2).
#define BUBBLES_PITCH_RATIO_UNISON        1.0f
#define BUBBLES_PITCH_RATIO_OCTAVE_UP     2.0f
#define BUBBLES_PITCH_RATIO_OCTAVE_DOWN   0.5f
#define BUBBLES_PITCH_RATIO_FIFTH_12TET   1.4983070768766815f
#define BUBBLES_PITCH_RATIO_OCTAVE_FIFTH  2.9966141537533630f

// --- M5A Multi-Scale Granular Memory Tiers & Temporal Architecture ---
typedef enum {
    BUBBLE_MEMORY_RECENT = 0,
    BUBBLE_MEMORY_MID = 1,
    BUBBLE_MEMORY_DEEP = 2,
    BUBBLE_MEMORY_TIER_COUNT = 3
} BubbleMemoryTier;
typedef BubbleMemoryTier BubbleMemoryTier_t;

#define BUBBLES_MEMORY_TIER_RECENT BUBBLE_MEMORY_RECENT
#define BUBBLES_MEMORY_TIER_MEDIUM BUBBLE_MEMORY_MID
#define BUBBLES_MEMORY_TIER_MID    BUBBLE_MEMORY_MID
#define BUBBLES_MEMORY_TIER_DEEP   BUBBLE_MEMORY_DEEP
#define BUBBLES_MEMORY_TIER_COUNT  BUBBLE_MEMORY_TIER_COUNT

// Nominal envelope limits for runtime tier resolution (Section 17-21)
#define BUBBLES_TIER_RECENT_DEFAULT_MIN_MS  35.0f
#define BUBBLES_TIER_RECENT_DEFAULT_MAX_MS  420.0f
#define BUBBLES_TIER_MICRO_DEFAULT_MIN_MS   10.0f
#define BUBBLES_TIER_MICRO_DEFAULT_MAX_MS   120.0f

#define BUBBLES_TIER_MID_DEFAULT_MIN_MS     300.0f
#define BUBBLES_TIER_MID_DEFAULT_MAX_MS     1100.0f

#define BUBBLES_TIER_DEEP_DEFAULT_MIN_MS    800.0f
#define BUBBLES_TIER_DEEP_DEFAULT_MAX_MS    1900.0f

#define BUBBLES_TIER_RECENT_MIN_MS  BUBBLES_TIER_RECENT_DEFAULT_MIN_MS
#define BUBBLES_TIER_RECENT_MAX_MS  BUBBLES_TIER_RECENT_DEFAULT_MAX_MS
#define BUBBLES_TIER_MID_MIN_MS     BUBBLES_TIER_MID_DEFAULT_MIN_MS
#define BUBBLES_TIER_MID_MAX_MS     BUBBLES_TIER_MID_DEFAULT_MAX_MS
#define BUBBLES_TIER_DEEP_MIN_MS    BUBBLES_TIER_DEEP_DEFAULT_MIN_MS
#define BUBBLES_TIER_DEEP_MAX_MS    BUBBLES_TIER_DEEP_DEFAULT_MAX_MS

#define BUBBLES_MEMORY_WEIGHT_RECENT 0.60f
#define BUBBLES_MEMORY_WEIGHT_MEDIUM 0.25f
#define BUBBLES_MEMORY_WEIGHT_DEEP   0.15f
// Upper bound on the probability of choosing the deep memory region per spawn.
#define BUBBLES_MEMORY_DEEP_MAX_SHARE 0.40f

// M5A Anti-Loop Hash Namespaces & Lanes
#define BUBBLES_SHARED_KIND_MEMORY_TIER   0x4D544952u // 'MTIR'
#define BUBBLES_SHARED_KIND_MEMORY_REGION 0x4D524547u // 'MREG'
#define BUBBLES_SHARED_KIND_MEMORY_DRIFT  0x4D445246u // 'MDRF'
#define BUBBLES_SHARED_KIND_ANCHOR_BLEND  0x414E4348u // 'ANCH'

#define BUBBLES_SHARED_LANE_TIER_ROLL     0xA5A5A5A5u
#define BUBBLES_SHARED_LANE_TIER_VALUE    0x5A5A5A5Au
#define BUBBLES_SHARED_LANE_REGION_ROLL   0xB4B4B4B4u
#define BUBBLES_SHARED_LANE_REGION_VALUE  0x4B4B4B4Bu
#define BUBBLES_SHARED_LANE_DRIFT         0xD7D7D7D7u
#define BUBBLES_SHARED_LANE_ANCHOR_ROLL   0xC3C3C3C3u
#define BUBBLES_SHARED_LANE_ANCHOR_OFFSET 0x3C3C3C3Cu

// Fixed per-grain microdetune limits in cents, chosen at spawn and held for the
// grain lifetime. Attacks stay almost pure; freeze accepts more ensemble.
#define BUBBLES_MICRODETUNE_ATTACK_CENTS  2.0f
#define BUBBLES_MICRODETUNE_SHORT_CENTS   4.0f
#define BUBBLES_MICRODETUNE_SUSTAIN_CENTS 6.0f
#define BUBBLES_MICRODETUNE_FREEZE_CENTS  8.0f

// --- M4A Auto-Hold & Phrase Anchor Tail Architecture ---
#define BUBBLES_AUTO_HOLD_ATTACK_SECONDS       0.080f
#define BUBBLES_AUTO_HOLD_BASE_RELEASE_SECONDS 2.200f
#define BUBBLES_AUTO_HOLD_MAX_RETENTION        0.965f
#define BUBBLES_AUTO_HOLD_THRESHOLD            0.015f
#define BUBBLES_AUTO_HOLD_ATTACK_PEAK          0.850f
#define BUBBLES_ANCHOR_MAX_MIX                 0.600f

// --- M4B Bounded Granular Feedback Path ---
#define BUBBLES_FEEDBACK_HPF_HZ             120.0f
#define BUBBLES_FEEDBACK_LPF_BASE_HZ        7000.0f
#define BUBBLES_FEEDBACK_SAFE_BOUND         0.650f
#define BUBBLES_FEEDBACK_MAX_GAIN           0.650f
#define BUBBLES_FEEDBACK_ENERGY_SAFETY_TH   0.280f
#define BUBBLES_FEEDBACK_HOLD_APERTURE_MAX  0.150f
#define BUBBLES_FEEDBACK_WRITE_APERTURE_MAX 0.280f

// --- M4D Wet Dynamics & Limiter Decoupling ---
#define BUBBLES_WET_NORM_ATTACK_SECONDS     0.100f
#define BUBBLES_WET_NORM_RELEASE_SECONDS    0.600f
#define BUBBLES_WET_NORM_ENERGY_ATT_SECONDS 0.080f
#define BUBBLES_WET_NORM_ENERGY_REL_SECONDS 0.500f
#define BUBBLES_WET_NORM_TARGET_ENERGY      0.200f // (~0.45 RMS)^2
#define BUBBLES_WET_NORM_GAIN_MIN           0.450f
#define BUBBLES_WET_NORM_GAIN_MAX           1.000f

#define BUBBLES_WET_LIMITER_CEILING_DB      (-2.0f)
#define BUBBLES_WET_LIMITER_RELEASE_MS      (60.0f)

// --- M5B Sparse Late-Tail Diffusion Architecture ---
#define BUBBLES_LATE_DIFFUSER_LINES         3
#define BUBBLES_LATE_DIFFUSER_DELAY_0_MS    47.3f
#define BUBBLES_LATE_DIFFUSER_DELAY_1_MS    107.1f
#define BUBBLES_LATE_DIFFUSER_DELAY_2_MS    181.9f

#define BUBBLES_LATE_DIFFUSER_CAPACITY_0    4800
#define BUBBLES_LATE_DIFFUSER_CAPACITY_1    10800
#define BUBBLES_LATE_DIFFUSER_CAPACITY_2    18500
#define BUBBLES_LATE_DIFFUSER_TOTAL_CAPACITY (BUBBLES_LATE_DIFFUSER_CAPACITY_0 + BUBBLES_LATE_DIFFUSER_CAPACITY_1 + BUBBLES_LATE_DIFFUSER_CAPACITY_2)

#define BUBBLES_LATE_DIFFUSER_HPF_HZ        150.0f
#define BUBBLES_LATE_DIFFUSER_LPF_HZ        6000.0f
#define BUBBLES_LATE_DIFFUSER_FEEDBACK_MIN  0.20f
#define BUBBLES_LATE_DIFFUSER_FEEDBACK_MAX  0.50f
#define BUBBLES_LATE_DIFFUSER_FEEDBACK_CEILING 0.75f
#define BUBBLES_LATE_DIFFUSER_SEND_CEILING  0.25f
#define BUBBLES_LATE_DIFFUSER_RETURN_CEILING 0.35f
#define BUBBLES_LATE_DIFFUSER_ENERGY_SAFETY_TH 0.30f

// --- Enums ---

typedef enum {
    ENGINE_STATE_SILENCE = 0,         // No spawning
    ENGINE_STATE_TRANSIENT_BURST,     // 100% Micro
    ENGINE_STATE_ATTACK_ONGOING,      // 80% Micro / 20% Short
    ENGINE_STATE_SUSTAIN_BODY,        // 70% Body / 30% Short
    ENGINE_STATE_SPARSE_DECAY         // 100% Body
} EngineState_t;

typedef enum {
    AUTO_HOLD_IDLE = 0,
    AUTO_HOLD_ATTACK,
    AUTO_HOLD_RELEASE
} AutoHoldState_t;

typedef enum {
    VOICE_STATE_INACTIVE = 0,
    VOICE_STATE_PLAYING,
    VOICE_STATE_PREEMPT_FADING,       // Reused for: stolen voices, and write-head-guard forced release
    VOICE_STATE_PENDING_ONSET         // Allocated, but silent until its intra-tick onset (M3.2C)
} VoiceState_t;

typedef enum {
    BUBBLE_CLASS_MICRO_ATTACK = 0,
    BUBBLE_CLASS_SHORT_INTERMEDIATE,
    BUBBLE_CLASS_SUSTAIN_BODY,
    BUBBLE_CLASS_COUNT
} BubbleClass_t;

typedef enum {
    WINDOW_TYPE_HANN = 0,
    WINDOW_TYPE_TUKEY_LIKE
} WindowType_t;

typedef enum {
    ENVELOPE_FAMILY_CLASSIC = 0,
    ENVELOPE_FAMILY_SOFT = 1
} EnvelopeFamily_t;

typedef enum {
    BUBBLE_PITCH_MODE_UNISON = 0,
    BUBBLE_PITCH_MODE_OCTAVE_UP = 1,
    BUBBLE_PITCH_MODE_OCTAVE_DOWN = 2,
    BUBBLE_PITCH_MODE_FIFTH = 3,
    BUBBLE_PITCH_MODE_SHIMMER = 4
} BubblePitchMode_t;

typedef enum {
    BUBBLE_MOTION_SHAPE_TRIANGLE = 0,
    BUBBLE_MOTION_SHAPE_SMOOTH = 1,
    BUBBLE_MOTION_SHAPE_HOLD = 2
} BubbleMotionShape_t;

typedef enum {
    BUBBLE_RHYTHM_DIVISION_QUARTER = 0,
    BUBBLE_RHYTHM_DIVISION_EIGHTH = 1,
    BUBBLE_RHYTHM_DIVISION_SIXTEENTH = 2,
    BUBBLE_RHYTHM_DIVISION_THIRTY_SECOND = 3
} BubbleRhythmDivision_t;

typedef enum {
    BUBBLE_BURST_MODE_SINGLE = 0,
    BUBBLE_BURST_MODE_SPRAY = 1,
    BUBBLE_BURST_MODE_STRUM = 2,
    BUBBLE_BURST_MODE_SWARM = 3,
    BUBBLE_BURST_MODE_REVERSE_SWELL = 4
} BubbleBurstMode_t;

// Canonical scheduler event sources (M2.4). Each real top-level spawn path owns
// an independent per-tick event index, so an extra event in one source (an extra
// burst, strum, immediate transient, ...) can never shift the shared identity of
// a common event belonging to another source. The burst variant (SPRAY / SWARM /
// REVERSE_SWELL / SINGLE) is the mode of the owning path invocation and is
// addressed by `child_index` within that invocation.
typedef enum {
    BUBBLES_SPAWN_SOURCE_DENSITY = 0,  // free-running density accumulator path
    BUBBLES_SPAWN_SOURCE_RHYTHM,       // tempo-synced rhythm-step path
    BUBBLES_SPAWN_SOURCE_STRUM,        // strum pending path
    BUBBLES_SPAWN_SOURCE_BURST,        // immediate transient burst path
    BUBBLES_SPAWN_SOURCE_DROPLET,      // derived second-generation grain
    BUBBLES_SPAWN_SOURCE_COUNT
} BubbleSpawnSource_t;

// Canonical shared stereo event identity (M2.4). Replaces the old per-channel
// sequential `tick_spawn_ordinal`: `event_index` is a per-source index within the
// scheduler tick, and `child_index` selects a grain inside a burst invocation or
// a derived (droplet) generation. `tick` captures the logical event time so a
// request that saturates the pool keeps its full identity when it is finally
// materialized. The whole struct is passed by value (no allocation, no lock).
typedef struct {
    uint32_t tick;        // scheduler tick at the logical event origin
    uint32_t source;      // BubbleSpawnSource_t
    uint32_t event_index; // per-source event/invocation index within the tick
    uint32_t child_index; // grain inside the burst / derived generation
} SharedSpawnId_t;

// --- Configuration Structs ---

typedef struct {
    float duration_ms_min;
    float duration_ms_max;
    WindowType_t window_type;
} BubbleClassConfig_t;

// Semantic read region (distance behind write head). Stored values are
// reference samples at BUBBLES_REFERENCE_SAMPLE_RATE (44.1 kHz), i.e. they are
// effectively a musical time. They are converted to the engine's actual
// sample rate at spawn time so attack/body/memory stay invariant across
// 44.1/48/88.2/96 kHz and higher. Defaults target:
//   Attack: 10-80ms, Body: 80-250ms, Memory: 250-900ms.
typedef struct {
    int32_t min_offset_samples;
    int32_t max_offset_samples;
} ReadRegionConfig_t;

// Pure DSP Engine configuration (Strictly baseline approved fields)
typedef struct {
    float sample_rate;
    float noise_floor;
    float tracking_thresh;
    float sustain_thresh;
    float transient_delta;

    float duck_burst_level;
    float duck_attack_coef;
    float duck_release_coef;

    int32_t burst_duration_ticks;
    int32_t burst_immediate_count;

    float density_burst;    // Spawns per second
    float density_sustain;  // Spawns per second
    float density_decay;    // Spawns per second

    // Shared semantic read regions used by all bubble classes.
    ReadRegionConfig_t attack_region;
    ReadRegionConfig_t body_region;
    ReadRegionConfig_t memory_region;
    uint32_t rng_seed;      // Deterministic PRNG seed for all sound-affecting random decisions

    // Stereo spawn-time controls.
    float stereo_width;
    float attack_pan_spread;
    float sustain_pan_spread;

    // Spawn alignment controls.
    int32_t smart_start_enable;
    int32_t smart_start_range;

    // Envelope variation controls.
    float envelope_variation;
    int32_t envelope_family;

    // Wet bus dynamics controls.
    float wet_drive;
    float wet_clip_amount;
    float wet_output_trim;

    // Final output limiter after dry + wet mix. Ceiling is in dBFS.
    float final_limiter_ceiling_db;
    float final_limiter_release_ms;

    // Sustain bus diffusion controls (1st-order all-pass bus stages).
    int32_t sustain_diffusion_enable;
    float sustain_diffusion_amount;
    int32_t sustain_diffusion_stages;
    int32_t sustain_diffusion_delay;
    float sustain_diffusion_feedback;

    // Second-generation droplet controls.
    int32_t droplet_enable;
    float droplet_probability;
    float droplet_gain;
    float droplet_length_scale;

    // Body -> memory morph controls.
    float memory_mix;
    float memory_pull;
    float memory_darkening;

    // Quantized tone controls.
    float tone_variation;
    float attack_brightness;
    float sustain_darkness;

    // Attack-only playback jitter controls.
    int32_t attack_rate_jitter;
    float attack_rate_jitter_depth;

    // Internal freeze/reverse/pitch extension controls.
    float freeze_amount;
    int32_t freeze_enabled;
    float reverse_probability;
    int32_t pitch_mode;
    float shimmer_amount;

    // Developer motion controls. Updated at control-rate only.
    float motion_rate;
    float motion_depth;
    BubbleMotionShape_t motion_shape;

    // Tempo/rhythm scheduler controls. When disabled, density uses free-running fractional spawning.
    float tempo_bpm;
    int32_t tempo_sync_enabled;
    BubbleRhythmDivision_t rhythm_division;
    BubbleBurstMode_t burst_mode;
    uint32_t rhythm_pattern;

    // Product/runtime quality profile. It selects an active subset of the
    // compiled voice pool and the read-position interpolator (M3.2B) without
    // changing per-voice DSP behavior.
    BubbleQualityProfile quality_profile;
    int32_t active_voice_limit;

    BubbleClassConfig_t class_configs[BUBBLE_CLASS_COUNT];
} EngineConfig_t;

// --- Runtime Structs ---

// State of a single bubble voice
typedef struct {
    VoiceState_t state;
    BubbleClass_t bubble_class;

    // Hot-path critical fields
    float read_ptr_float; // Advances by signed spawn-time rate per sample
    float rate;           // Signed playback rate after pitch and reverse decisions
    float quantized_rate; // Absolute quantized pitch rate selected at spawn
    float phase;          // Window phase (0.0 to 1.0)
    float phase_inc;      // Phase step per sample based on class duration
    float amp;            // Amplitude multiplier for preemption fade
    float gain;           // Spawn-time gain shaping (droplets + tone + memory darkening)
    float pan_l;
    float pan_r;
    uint8_t envelope_variant;
    uint8_t tone_profile;
    uint8_t source_region_id;
    uint8_t read_direction; // 0 = forward, 1 = reverse
    uint8_t generation;
    uint8_t memory_tier;    // BUBBLES_MEMORY_TIER_* chosen at spawn (M2)

    // Canonical logical spawn identity (M2.4), assigned by the scheduler and held
    // for observation/debugging. Shared stereo decisions of this grain were keyed
    // by this full provenance at initialization time.
    SharedSpawnId_t spawn_id;

    // Fixed at spawn, constant for the whole grain lifetime (M2). Never an LFO.
    float microdetune_cents;

    // M3.2C intra-tick spawn jitter. A grain whose scheduler event received a
    // deterministic onset delay is allocated on the tick but stays silent in
    // VOICE_STATE_PENDING_ONSET until `onset_delay_samples` reaches zero. During
    // the wait neither phase nor read pointer advances and no audio is produced.
    // `spawn_read_offset` is the guard-clamped distance behind the write head
    // chosen at spawn; at onset the read pointer is re-derived from the *current*
    // write head, so a write head that advanced during the delay cannot violate
    // the guard.
    int16_t onset_delay_samples; // remaining samples before a pending grain starts
    int32_t spawn_read_offset;   // read offset behind write head, re-applied at onset

    // Preemption tracking
    int32_t fade_counter; // Counts down from BUBBLES_FADE_SAMPLES
} BubbleVoice_t;

// Explicit 1-Pole IIR Filter State
typedef struct {
    float a1; // Feedback coefficient
    float b0; // Feedforward coefficient
    float z1; // State delay
} Filter1Pole_t;

typedef struct {
    BubbleClass_t bubble_class;
    uint8_t generation;
    // Full canonical logical spawn identity, captured when the request was
    // created and preserved verbatim until the voice is actually initialized
    // (saturation queue). No field is ever recomputed on materialization.
    SharedSpawnId_t spawn_id;
} PendingSpawn_t;

typedef struct {
    float phase;
    float value;
    uint32_t hold_state;
} BubbleMotionLfoState_t;

typedef struct BubbleMotionState {
    BubbleMotionLfoState_t density;
    BubbleMotionLfoState_t panorama;
    BubbleMotionLfoState_t memory_pull;
    BubbleMotionLfoState_t sparkle;
    BubbleMotionLfoState_t reverse_probability;
    BubbleMotionLfoState_t diffusion_amount;
    uint32_t seed;
} BubbleMotionState;

typedef struct {
    int32_t spawn_count;
    int32_t active_voices;
    int32_t engine_state;
    float ducking_gain;
    float envelope;
    float peak_l;
    float peak_r;
    int32_t clip_count;
    float limiter_gain;
    // M4D Wet Dynamics Telemetry
    float wet_pre_norm_peak;
    float wet_pre_norm_rms;
    float wet_normalization_gain;
    float wet_limiter_gain;
    float wet_limiter_gain_reduction_db;
    float final_limiter_gain;
    float final_limiter_gain_reduction_db;
    // M5A Multi-Scale Granular Memory Telemetry
    int32_t spawn_recent_count;
    int32_t spawn_mid_count;
    int32_t spawn_deep_count;
    float mean_read_age_ms;
    float p50_read_age_ms;
    float p95_read_age_ms;
    float anchor_read_fraction;
    // M5B Sparse Late-Tail Diffusion Telemetry
    float late_diffuser_send;
    float late_diffuser_return_rms;
    float late_diffuser_return_peak;
    float late_diffuser_feedback_energy;
    float late_diffuser_max_loop_gain;
    float late_diffuser_active_fraction;
} SoundBubblesBlockMetrics_t;

typedef void (*SoundBubblesMetricsCallback_t)(const SoundBubblesBlockMetrics_t* metrics, void* user_data);

// Full DSP Engine State (Memory is caller-owned)
typedef struct {
    // Buffers (Pointer passed in by caller)
    BubbleRingSample_t* delay_buffer;
    int32_t write_ptr;

    // Voices
    BubbleVoice_t voices[BUBBLES_MAX_VOICES];

    // Control-Rate state tracking
    PendingSpawn_t pending_spawns[BUBBLES_PENDING_SPAWN_CAPACITY];
    int32_t pending_spawn_head;
    int32_t pending_spawn_count;

    EngineState_t engine_state;
    float env_follower_state;
    float env_derivative;
    int32_t burst_timer_ticks;     // Timer for transient burst duration

    float target_density;          // Current spawns per second
    float spawn_accumulator;       // Accumulates fractional spawns per block
    float rhythm_step_accumulator; // Accumulates tempo-synced pattern steps in BUBBLES_BLOCK_SIZE ticks
    int32_t rhythm_step_index;
    int32_t strum_pending_count;
    int32_t strum_step_index;
    int32_t force_reverse_spawns;

    float internal_ducking_target; // Maintained internally by DSP core
    float smoothed_ducking_gain;   // Evaluated/smoothed exclusively in control-rate

    // Internal (non-UI) class bus gain defaults for micro/short/sustain contrast shaping.
    float class_gain_micro;
    float class_gain_short;
    float class_gain_sustain;

    // Internal (non-UI) wet presence state shaping: smoothed multiplier applied post bus-sum.
    float wet_presence_target;
    float wet_presence_smoothed;
    int32_t bloom_timer_ticks;

    // Bus & Control Filters
    Filter1Pole_t attack_hpf_l;
    Filter1Pole_t attack_hpf_r;
    Filter1Pole_t sustain_lpf_l;
    Filter1Pole_t sustain_lpf_r;
    Filter1Pole_t ducking_lpf;     // Smoothing filter for ducking gain
    Filter1Pole_t wet_presence_lpf;// Control-rate smoothing for wet presence target

    // M3.2A tonal bus rebalance: the sustain LPF cutoff is modulated at control
    // rate by phrase state and the resolved WARMTH/CLARITY darkness, then
    // smoothed so warmth automation and phrase transitions cannot cause zipper.
    float sustain_lpf_cutoff_smoothed_hz; // control-rate smoothed target cutoff
    float sustain_lpf_applied_hz;         // cutoff of the coefficients currently loaded
    float sustain_lpf_smooth_coef;        // control-rate one-pole smoothing coefficient
    float sustain_diffusion_delay_l[BUBBLES_SUSTAIN_DIFFUSION_MAX_DELAY];
    float sustain_diffusion_delay_r[BUBBLES_SUSTAIN_DIFFUSION_MAX_DELAY];
    float sustain_diffusion_delay2_l[BUBBLES_SUSTAIN_DIFFUSION_MAX_DELAY];
    float sustain_diffusion_delay2_r[BUBBLES_SUSTAIN_DIFFUSION_MAX_DELAY];
    int32_t sustain_diffusion_write_idx;

    // Global Config & Block tracking
    EngineConfig_t config;
    EngineConfig_t motion_base_config;
    BubbleMotionState motion_state;
    int32_t active_voice_limit;
    // Cached read-position interpolator selected by config.quality_profile
    // (M3.2B). Keeping it as a field avoids re-deriving the profile in the
    // per-voice render loop.
    BubbleInterpolationMode_t interpolation_mode;
#if defined(BUBBLES_INTERPOLATION_TELEMETRY) || defined(BUBBLES_BUILD_PROCESSOR_TESTS)
    // Test/telemetry-only path counters. Compiled out of production release
    // builds (the macros are only set by test targets), so the render loop
    // carries no release overhead.
    uint64_t interpolation_linear_samples;
    uint64_t interpolation_hermite_samples;
#endif
    // Preemption fade length resolved from BUBBLES_FADE_MS at config.sample_rate.
    int32_t fade_samples;
    int32_t block_counter;         // Triggers control ticks every 32 samples
    // M3.2C: envelope peak accumulated across host-process calls so a control
    // tick sees the same 32-sample window regardless of how the host splits its
    // blocks. Without this, a tick straddling two host blocks would only see the
    // trailing fragment and block-size independence would break.
    float block_peak_accum;
    uint32_t rng_state;            // Internal deterministic per-channel PRNG state
    // Stateless shared-event RNG (M2.2/M2.3/M2.4). Shared stereo decisions are
    // addressed by a canonical logical event identity
    // (shared_event_seed, tick, source, event_index, child_index, decision kind)
    // and hashed on demand, so extra top-level spawns of one source cannot shift
    // the shared decision of a common event of another source. Within a single
    // source the local ordering is deterministic: if the two channels produce
    // different counts for that source, no implicit semantic correspondence is
    // assumed for subsequent events. The scheduler owns one independent
    // per-source event index per control tick, while second-generation (droplet)
    // spawns derive a stable child identity from their parent's full provenance
    // instead of consuming a new primary event index. No mutable shared stream
    // state is kept; only the per-channel `rng_state` remains sequential.
    uint32_t shared_event_seed;    // Undecorrelated base seed for shared decisions
    uint32_t scheduler_tick;       // Monotonic control-tick counter (logical event time)
    // Per-source event index for the current tick, reset at every control tick.
    uint32_t spawn_source_event_index[BUBBLES_SPAWN_SOURCE_COUNT];
    // Channel decorrelation mask, applied on seed (re)initialization so the
    // right channel keeps its own spatial stream across preset/seed changes.
    uint32_t channel_decorrelation;

    // Final output mix gains of the DSP module (not product-layer macro controls)
    float master_dry_gain;
    float master_wet_gain;

    // Continuous Freeze & Temporal Morphing state (M3)
    float smoothed_freeze;

    // Auto-Hold & Phrase Anchor Tail Architecture (M4A)
    AutoHoldState_t auto_hold_state;
    float auto_hold_amount;
    float auto_hold_target;
    float auto_hold_attack_coef;
    float auto_hold_release_coef;
    uint8_t recent_phrase_active;
    int32_t phrase_anchor_write_ptr;
    bool phrase_anchor_valid;
    uint32_t phrase_anchor_age;
    float anchor_mix;

    // Bounded Granular Feedback Path (M4B)
    int32_t feedback_enabled;
    float feedback_sample;
    float feedback_gain;
    float feedback_gain_target;
    float feedback_gain_smooth_coef;
    float feedback_energy;
    float feedback_safety_threshold;
    float feedback_energy_att_coef;
    float feedback_energy_rel_coef;
    Filter1Pole_t feedback_hpf;
    Filter1Pole_t feedback_lpf;
    float feedback_lpf_cutoff_hz;
    float last_write_input;
    float last_write_feedback;
    float last_write_retained;
    float feedback_write_aperture;
    uint32_t ring_softclip_count;
    uint32_t ring_clamp_count;

    // Ring storage backend & dither state (M4C)
    uint32_t ring_dither_rng;
    uint8_t dither_enabled;

    // Product-facing macro state. Targets are written by bubble_engine_set_parameter();
    // current values are slewed at control-rate before being mapped to raw DSP fields.
    float macro_values[BUBBLES_MACRO_COUNT];
    float macro_targets[BUBBLES_MACRO_COUNT];
    uint32_t macro_dirty_mask;
    int32_t developer_mode;

    // MCU-safe no-lookahead final limiter state and block telemetry accumulators.
    float final_limiter_gain;
    float final_limiter_ceiling_linear;
    float final_limiter_release_coef;
    float metrics_peak_l_accum;
    float metrics_peak_r_accum;
    int32_t metrics_clip_count_accum;
    float metrics_limiter_gain_min;

    // M4D Wet Dynamics & Limiter Decoupling
    float wet_norm_energy;
    float wet_normalization_gain;
    float wet_norm_target_energy;
    float wet_norm_energy_att_coef;
    float wet_norm_energy_rel_coef;
    float wet_norm_gain_att_coef;
    float wet_norm_gain_rel_coef;
    float wet_limiter_gain;
    float wet_limiter_ceiling_linear;
    float wet_limiter_release_coef;
    float metrics_wet_pre_norm_peak_accum;
    float metrics_wet_pre_norm_energy_accum;
    int32_t metrics_wet_norm_samples_accum;
    float metrics_wet_norm_gain_min;
    float metrics_wet_limiter_gain_min;
    float last_wet_pre_norm_peak;
    float last_wet_pre_norm_rms;
    float last_wet_norm_gain;
    float last_wet_limiter_gain;

    // M5A Multi-scale Memory & Anti-Loop Telemetry
    int32_t spawn_recent_count;
    int32_t spawn_mid_count;
    int32_t spawn_deep_count;
    int32_t spawn_anchor_count;
    int32_t total_spawn_count;
    float recent_read_ages_ms[128];
    int32_t recent_read_ages_head;
    int32_t recent_read_ages_count;

    // M5B Sparse Late-Tail Diffuser
    BubbleRingSample_t late_diffuser_buf0[BUBBLES_LATE_DIFFUSER_CAPACITY_0];
    BubbleRingSample_t late_diffuser_buf1[BUBBLES_LATE_DIFFUSER_CAPACITY_1];
    BubbleRingSample_t late_diffuser_buf2[BUBBLES_LATE_DIFFUSER_CAPACITY_2];
    int32_t late_diffuser_len[BUBBLES_LATE_DIFFUSER_LINES];
    int32_t late_diffuser_write_idx[BUBBLES_LATE_DIFFUSER_LINES];
    Filter1Pole_t late_diffuser_hpf[BUBBLES_LATE_DIFFUSER_LINES];
    Filter1Pole_t late_diffuser_lpf[BUBBLES_LATE_DIFFUSER_LINES];
    float late_diffuser_amount;
    float late_diffuser_target;
    float late_diffuser_send_gain;
    float late_diffuser_return_gain;
    float late_diffuser_loop_gain;
    float late_diffuser_internal_energy;
    float late_diffuser_return_l;
    float late_diffuser_return_r;

    float metrics_diffuser_send_accum;
    float metrics_diffuser_return_energy_accum;
    float metrics_diffuser_return_peak_accum;
    float metrics_diffuser_active_samples_accum;
    int32_t metrics_diffuser_samples_accum;

    // Optional per-control-block metrics hook (for offline validation/telemetry)
    SoundBubblesMetricsCallback_t metrics_callback;
    void* metrics_user_data;
    SoundBubblesBlockMetrics_t metrics_last_block;
    int32_t metrics_tick_spawn_count;
} SoundBubblesEngine_t;

// --- Function Prototypes ---

#if defined(SOUND_BUBBLES_DSP_INTERNAL)
#define SOUND_BUBBLES_DEPRECATED
#elif defined(__GNUC__) || defined(__clang__)
#define SOUND_BUBBLES_DEPRECATED __attribute__((deprecated("Use bubble_engine_* from core/engine/bubble_engine.h")))
#else
#define SOUND_BUBBLES_DEPRECATED
#endif

// Initialization: Caller provides pre-allocated delay_buffer (allocated via SoundBubbles_RequiredBufferSamples or SoundBubbles_RequiredBufferBytes) and initial config.
// Determinism contract: same rng_seed + same input samples + same config/params => identical class/read/duration random decisions.
SOUND_BUBBLES_DEPRECATED void SoundBubbles_Init(SoundBubblesEngine_t* engine, BubbleRingSample_t* delay_buffer_memory, const EngineConfig_t* initial_config);

// Returns the number of samples required for the delay buffer based on the target sample rate.
size_t SoundBubbles_RequiredBufferSamples(float sample_rate);

// Returns the number of bytes required for the delay buffer based on the target sample rate
// and the compiled storage backend (BubbleRingSample_t).
// Note: samples != bytes. Callers allocating raw memory must use RequiredBufferBytes
// or multiply RequiredBufferSamples by sizeof(BubbleRingSample_t).
size_t SoundBubbles_RequiredBufferBytes(float sample_rate);

// Config Update: Safely copy new core engine parameters
SOUND_BUBBLES_DEPRECATED void SoundBubbles_UpdateConfig(SoundBubblesEngine_t* engine, const EngineConfig_t* new_config);
SOUND_BUBBLES_DEPRECATED void SoundBubbles_UpdateRuntimeConfig(SoundBubblesEngine_t* engine, const EngineConfig_t* new_config);
SOUND_BUBBLES_DEPRECATED void SoundBubbles_ResetMotionPhase(SoundBubblesEngine_t* engine);
uint32_t SoundBubbles_MotionHash(uint32_t state);
float SoundBubbles_MotionHashToBipolar(uint32_t state);

// Explicitly reset the deterministic PRNG state (0 maps to a fixed non-zero internal state).
SOUND_BUBBLES_DEPRECATED void SoundBubbles_SetRngSeed(SoundBubblesEngine_t* engine, uint32_t seed);

// M2 stereo coherence: set a per-channel decorrelation mask applied on top of the
// shared config seed. Shared event-addressable decisions ignore this mask, so
// event-level decisions align between the L/R engines while the sequential
// per-channel stream keeps spatial decisions distinct.
SOUND_BUBBLES_DEPRECATED void SoundBubbles_SetChannelDecorrelation(SoundBubblesEngine_t* engine, uint32_t decorrelation_mask);

// Audio Processing: Processes num_samples. DSP core owns final dry/wet output policy.
SOUND_BUBBLES_DEPRECATED void SoundBubbles_ProcessBlock(SoundBubblesEngine_t* engine, const float* in_mono, float* out_left, float* out_right, int num_samples);

// Spatial split processing: writes the wet stereo bus to out_wet_left/out_wet_right
// and the dry mono bus to out_dry_mono, deliberately skipping the final limiter.
// Wrappers combining several mono spatial instances own dry placement and apply
// SoundBubbles_ApplyFinalLimiter once on the summed stereo bus. No allocation/locks.
SOUND_BUBBLES_DEPRECATED void SoundBubbles_ProcessBlockSpatial(SoundBubblesEngine_t* engine,
                                                               const float* in_mono,
                                                               float* out_wet_left,
                                                               float* out_wet_right,
                                                               float* out_dry_mono,
                                                               int num_samples);

// Applies the engine's final-limiter policy and telemetry accumulation to an
// already sum-mixed stereo block. Uses the same state as the in-process limiter.
// Returns the minimum limiter gain observed over the block (1.0 = no limiting)
// so hosts can publish a final-bus gain-reduction reading without extra storage.
SOUND_BUBBLES_DEPRECATED float SoundBubbles_ApplyFinalLimiter(SoundBubblesEngine_t* engine,
                                                              float* out_left,
                                                              float* out_right,
                                                              int num_samples);

// Converts a 44.1 kHz reference offset into samples at the given rate. Exposed so
// host layers and tests can share the exact same sample-rate conversion.
SOUND_BUBBLES_DEPRECATED int32_t SoundBubbles_ReferenceSamplesToSamples(int32_t reference_samples, float sample_rate);

// Optional metrics callback registration. Pass NULL callback to disable export.
SOUND_BUBBLES_DEPRECATED void SoundBubbles_SetMetricsCallback(SoundBubblesEngine_t* engine, SoundBubblesMetricsCallback_t callback, void* user_data);

// M3.2B interpolation selection. The mode is a pure function of the quality
// profile: MCU_SAFE/MCU_PLUS -> LINEAR, WEB_STANDARD/WEB_ULTRA -> HERMITE.
// SoundBubbles_GetInterpolationMode() reports the mode cached on a live engine.
BubbleInterpolationMode_t SoundBubbles_InterpolationModeForProfile(BubbleQualityProfile profile);
BubbleInterpolationMode_t SoundBubbles_GetInterpolationMode(const SoundBubblesEngine_t* engine);

// M4A Auto-Hold & Phrase Anchor inspection helpers
AutoHoldState_t SoundBubbles_GetAutoHoldState(const SoundBubblesEngine_t* engine);
float SoundBubbles_GetAutoHoldAmount(const SoundBubblesEngine_t* engine);
float SoundBubbles_GetAutoHoldTarget(const SoundBubblesEngine_t* engine);
int32_t SoundBubbles_GetPhraseAnchorWritePtr(const SoundBubblesEngine_t* engine);
bool SoundBubbles_GetPhraseAnchorValid(const SoundBubblesEngine_t* engine);
uint32_t SoundBubbles_GetPhraseAnchorAge(const SoundBubblesEngine_t* engine);
float SoundBubbles_GetAnchorMix(const SoundBubblesEngine_t* engine);

// M4B Bounded Granular Feedback inspection helpers
float SoundBubbles_GetFeedbackGain(const SoundBubblesEngine_t* engine);
float SoundBubbles_GetFeedbackEnergy(const SoundBubblesEngine_t* engine);
float SoundBubbles_GetFeedbackSample(const SoundBubblesEngine_t* engine);
void SoundBubbles_SetFeedbackEnabled(SoundBubblesEngine_t* engine, bool enabled);
void SoundBubbles_GetLastWriteContributions(const SoundBubblesEngine_t* engine,
                                            float* out_input,
                                            float* out_feedback,
                                            float* out_retained);
float SoundBubbles_GetFeedbackWriteAperture(const SoundBubblesEngine_t* engine);
float SoundBubbles_GetFeedbackSafetyThreshold(const SoundBubblesEngine_t* engine);
void SoundBubbles_SetFeedbackSafetyThreshold(SoundBubblesEngine_t* engine, float threshold);
void SoundBubbles_GetRingSaturationCounts(const SoundBubblesEngine_t* engine,
                                         uint32_t* out_softclip,
                                         uint32_t* out_clamp);
void SoundBubbles_ResetRingSaturationCounts(SoundBubblesEngine_t* engine);

// M4C Int16 Dither control (test-only helper, not a public parameter)
void SoundBubbles_SetDitherEnabled(SoundBubblesEngine_t* engine, bool enabled);
bool SoundBubbles_GetDitherEnabled(const SoundBubblesEngine_t* engine);

// M4D Wet Dynamics & Limiter Decoupling inspection helpers
float SoundBubbles_GetWetPreNormPeak(const SoundBubblesEngine_t* engine);
float SoundBubbles_GetWetPreNormRms(const SoundBubblesEngine_t* engine);
float SoundBubbles_GetWetNormalizationGain(const SoundBubblesEngine_t* engine);
float SoundBubbles_GetWetLimiterGain(const SoundBubblesEngine_t* engine);
float SoundBubbles_GetWetLimiterGainReductionDb(const SoundBubblesEngine_t* engine);
float SoundBubbles_GetFinalLimiterGain(const SoundBubblesEngine_t* engine);
float SoundBubbles_GetFinalLimiterGainReductionDb(const SoundBubblesEngine_t* engine);

// M5A Multi-Scale Granular Memory inspection helpers
void SoundBubbles_GetMemoryTierCounts(const SoundBubblesEngine_t* engine,
                                      int32_t* out_recent,
                                      int32_t* out_mid,
                                      int32_t* out_deep);
void SoundBubbles_GetReadAgeTelemetry(const SoundBubblesEngine_t* engine,
                                      float* out_mean_ms,
                                      float* out_p50_ms,
                                      float* out_p95_ms,
                                      float* out_anchor_fraction);
void SoundBubbles_ResetMemoryTierTelemetry(SoundBubblesEngine_t* engine);

// M5B Sparse Late-Tail Diffusion inspection helpers
float SoundBubbles_GetLateDiffuserSend(const SoundBubblesEngine_t* engine);
float SoundBubbles_GetLateDiffuserReturnRms(const SoundBubblesEngine_t* engine);
float SoundBubbles_GetLateDiffuserReturnPeak(const SoundBubblesEngine_t* engine);
float SoundBubbles_GetLateDiffuserFeedbackEnergy(const SoundBubblesEngine_t* engine);
float SoundBubbles_GetLateDiffuserMaxLoopGain(const SoundBubblesEngine_t* engine);
float SoundBubbles_GetLateDiffuserAmount(const SoundBubblesEngine_t* engine);

// Single source of truth for runtime tier ranges (Section 17-21)
void SoundBubbles_ResolveMemoryTierRangeSamples(const SoundBubblesEngine_t* engine,
                                                BubbleMemoryTier tier,
                                                int32_t* out_min_offset,
                                                int32_t* out_max_offset);
void SoundBubbles_ResolveMemoryTierRangeMs(const SoundBubblesEngine_t* engine,
                                          BubbleMemoryTier tier,
                                          float* out_min_ms,
                                          float* out_max_ms);

// Deterministic tier trace record for regression & verification (Section 28-29)
typedef struct {
    SharedSpawnId_t spawn_id;
    int32_t phrase_anchor_age;
    EngineState_t engine_state;
    float memory_macro;
    BubbleMemoryTier memory_tier;
    int32_t read_offset_samples;
    float read_age_ms;
    bool used_anchor;
} BubbleTierTraceRecord_t;

typedef void (*BubbleTierTraceFn)(void* user, const SoundBubblesEngine_t* engine, const BubbleTierTraceRecord_t* record);
void SoundBubblesTest_SetTierTrace(BubbleTierTraceFn fn, void* user);

#if defined(BUBBLES_INTERPOLATION_TELEMETRY) || defined(BUBBLES_BUILD_PROCESSOR_TESTS)
// Test/telemetry-only: number of samples rendered through each interpolation
// path since engine init. Compiled only under the test macros.
void SoundBubbles_GetInterpolationCallCounts(const SoundBubblesEngine_t* engine,
                                             uint64_t* out_linear_samples,
                                             uint64_t* out_hermite_samples);
#endif

#undef SOUND_BUBBLES_DEPRECATED

#endif // SOUND_BUBBLES_DSP_H
