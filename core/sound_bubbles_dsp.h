#ifndef SOUND_BUBBLES_DSP_H
#define SOUND_BUBBLES_DSP_H

#include <stdint.h>
#include <stdbool.h>

// --- System & Algorithmic Constants ---
#define BUBBLES_SAMPLE_RATE 44100
#define BUBBLES_BLOCK_SIZE 32
#define BUBBLES_MAX_VOICES 12
#define BUBBLES_BUFFER_SIZE_SAMPLES 88200 // Exactly 2 seconds at 44.1kHz
#define BUBBLES_FADE_SAMPLES 44           // ~1ms preemption fade at 44.1kHz
#define BUBBLES_GUARD_ZONE_SAMPLES 64
#define SCHED_MAX_SPAWNS_PER_TICK 3
#define BUBBLES_SUSTAIN_DIFFUSION_MAX_DELAY 96

// --- Enums ---

typedef enum {
    ENGINE_STATE_SILENCE = 0,         // No spawning
    ENGINE_STATE_TRANSIENT_BURST,     // 100% Micro
    ENGINE_STATE_ATTACK_ONGOING,      // 80% Micro / 20% Short
    ENGINE_STATE_SUSTAIN_BODY,        // 70% Body / 30% Short
    ENGINE_STATE_SPARSE_DECAY         // 100% Body
} EngineState_t;

typedef enum {
    VOICE_STATE_INACTIVE = 0,
    VOICE_STATE_PLAYING,
    VOICE_STATE_PREEMPT_FADING        // Reused for: stolen voices, and write-head-guard forced release
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

// --- Configuration Structs ---

typedef struct {
    float duration_ms_min;
    float duration_ms_max;
    WindowType_t window_type;
} BubbleClassConfig_t;

// Semantic read region (distance behind write head). Values are in samples.
// Defaults target musically meaningful temporal zones:
//   Attack: 10-80ms, Body: 80-250ms, Memory: 250-900ms.
typedef struct {
    int32_t min_offset_samples;
    int32_t max_offset_samples;
} ReadRegionConfig_t;

// Pure DSP Engine configuration (Strictly baseline approved fields)
typedef struct {
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

    BubbleClassConfig_t class_configs[BUBBLE_CLASS_COUNT];
} EngineConfig_t;

// --- Runtime Structs ---

// State of a single bubble voice (Strictly 1.0x playback, no pitch fields)
typedef struct {
    VoiceState_t state;
    BubbleClass_t bubble_class;

    // Hot-path critical fields
    float read_ptr_float; // Advances precisely 1.0f per sample
    float rate;           // Spawn-time playback rate, optional and deterministic
    float phase;          // Window phase (0.0 to 1.0)
    float phase_inc;      // Phase step per sample based on class duration
    float amp;            // Amplitude multiplier for preemption fade
    float gain;           // Spawn-time gain shaping (droplets + tone + memory darkening)
    float pan_l;
    float pan_r;
    uint8_t envelope_variant;
    uint8_t tone_profile;
    uint8_t source_region_id;
    uint8_t generation;

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
    int32_t spawn_count;
    int32_t active_voices;
    int32_t engine_state;
    float ducking_gain;
    float envelope;
} SoundBubblesBlockMetrics_t;

typedef void (*SoundBubblesMetricsCallback_t)(const SoundBubblesBlockMetrics_t* metrics, void* user_data);

// Full DSP Engine State (Memory is caller-owned)
typedef struct {
    // Buffers (Pointer passed in by caller)
    int16_t* delay_buffer;
    int32_t write_ptr;

    // Voices
    BubbleVoice_t voices[BUBBLES_MAX_VOICES];

    // Control-Rate state tracking
    EngineState_t engine_state;
    float env_follower_state;
    float env_derivative;
    int32_t burst_timer_ticks;     // Timer for transient burst duration

    float target_density;          // Current spawns per second
    float spawn_accumulator;       // Accumulates fractional spawns per block

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
    Filter1Pole_t attack_hpf;
    Filter1Pole_t sustain_lpf;
    Filter1Pole_t ducking_lpf;     // Smoothing filter for ducking gain
    Filter1Pole_t wet_presence_lpf;// Control-rate smoothing for wet presence target
    float sustain_diffusion_delay_l[BUBBLES_SUSTAIN_DIFFUSION_MAX_DELAY];
    float sustain_diffusion_delay_r[BUBBLES_SUSTAIN_DIFFUSION_MAX_DELAY];
    int32_t sustain_diffusion_write_idx;

    // Global Config & Block tracking
    EngineConfig_t config;
    int32_t block_counter;         // Triggers control ticks every 32 samples
    uint32_t rng_state;            // Internal deterministic PRNG state

    // Final output mix gains of the DSP module (not product-layer macro controls)
    float master_dry_gain;
    float master_wet_gain;

    // Optional per-control-block metrics hook (for offline validation/telemetry)
    SoundBubblesMetricsCallback_t metrics_callback;
    void* metrics_user_data;
    SoundBubblesBlockMetrics_t metrics_last_block;
    int32_t metrics_tick_spawn_count;
} SoundBubblesEngine_t;

// --- Function Prototypes ---

// Initialization: Caller provides pre-allocated delay_buffer (88,200 elements) and initial config.
// Determinism contract: same rng_seed + same input samples + same config/params => identical class/read/duration random decisions.
void SoundBubbles_Init(SoundBubblesEngine_t* engine, int16_t* delay_buffer_memory, const EngineConfig_t* initial_config);

// Config Update: Safely copy new core engine parameters
void SoundBubbles_UpdateConfig(SoundBubblesEngine_t* engine, const EngineConfig_t* new_config);

// Explicitly reset the deterministic PRNG state (0 maps to a fixed non-zero internal state).
void SoundBubbles_SetRngSeed(SoundBubblesEngine_t* engine, uint32_t seed);

// Audio Processing: Processes num_samples. DSP core owns final dry/wet output policy.
void SoundBubbles_ProcessBlock(SoundBubblesEngine_t* engine, const float* in_mono, float* out_left, float* out_right, int num_samples);

// Optional metrics callback registration. Pass NULL callback to disable export.
void SoundBubbles_SetMetricsCallback(SoundBubblesEngine_t* engine, SoundBubblesMetricsCallback_t callback, void* user_data);

#endif // SOUND_BUBBLES_DSP_H
