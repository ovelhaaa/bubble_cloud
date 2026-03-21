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

// --- Configuration Structs ---

typedef struct {
    float duration_ms_min;
    float duration_ms_max;
    int32_t offset_samples; // Base read offset from write head
    int32_t jitter_samples; // Randomization applied to read offset
    WindowType_t window_type;
} BubbleClassConfig_t;

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

    int32_t sustain_read_center_offset_samples;

    BubbleClassConfig_t class_configs[BUBBLE_CLASS_COUNT];
} EngineConfig_t;

// --- Runtime Structs ---

// State of a single bubble voice (Strictly 1.0x playback, no pitch fields)
typedef struct {
    VoiceState_t state;
    BubbleClass_t bubble_class;

    // Hot-path critical fields
    float read_ptr_float; // Advances precisely 1.0f per sample
    float phase;          // Window phase (0.0 to 1.0)
    float phase_inc;      // Phase step per sample based on class duration
    float amp;            // Amplitude multiplier for preemption fade

    // Preemption tracking
    int32_t fade_counter; // Counts down from BUBBLES_FADE_SAMPLES
} BubbleVoice_t;

// Explicit 1-Pole IIR Filter State
typedef struct {
    float a1; // Feedback coefficient
    float b0; // Feedforward coefficient
    float z1; // State delay
} Filter1Pole_t;

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

    // Bus & Control Filters
    Filter1Pole_t attack_hpf;
    Filter1Pole_t sustain_lpf;
    Filter1Pole_t ducking_lpf;     // Smoothing filter for ducking gain

    // Global Config & Block tracking
    EngineConfig_t config;
    int32_t block_counter;         // Triggers control ticks every 32 samples

    // Final output mix gains of the DSP module (not product-layer macro controls)
    float master_dry_gain;
    float master_wet_gain;
} SoundBubblesEngine_t;

// --- Function Prototypes ---

// Initialization: Caller provides pre-allocated delay_buffer (88,200 elements) and initial config
void SoundBubbles_Init(SoundBubblesEngine_t* engine, int16_t* delay_buffer_memory, const EngineConfig_t* initial_config);

// Config Update: Safely copy new core engine parameters
void SoundBubbles_UpdateConfig(SoundBubblesEngine_t* engine, const EngineConfig_t* new_config);

// Audio Processing: Processes num_samples. DSP core owns final dry/wet output policy.
void SoundBubbles_ProcessBlock(SoundBubblesEngine_t* engine, const float* in_mono, float* out_left, float* out_right, int num_samples);

#endif // SOUND_BUBBLES_DSP_H