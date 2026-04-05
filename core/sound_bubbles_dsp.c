#include "sound_bubbles_dsp.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

// --- Internal Implementation Constants ---
#define ENV_ATTACK_COEF  0.1f   // ~fast tracking for attacks
#define ENV_RELEASE_COEF 0.01f  // ~slow tracking for sustain/decay

// Minimum phase (age) before a voice is considered "stealable" to avoid dropping very young clicks
#define STEAL_MIN_PHASE_THRESHOLD 0.05f

// --- Internal LUTs ---
static float WindowLUT_Hann[1024];
static float WindowLUT_Tukey[1024];
static bool luts_initialized = false;
static const uint32_t RNG_STATE_FALLBACK = 0x6D2B79F5u;

// --- Static Helper Prototypes ---
static void InitWindowLUTs(void);
static uint32_t NextRandomU32(SoundBubblesEngine_t* engine);
static float RandomFloat01(SoundBubblesEngine_t* engine);
static inline int32_t WrapIntIndex(int32_t index, int32_t size);
static inline float WrapFloatIndex(float index, float size);
static inline float LinearInterpolate(const int16_t* buffer, float index_float);
static inline bool CheckGuardZoneDirectional(int32_t write_ptr, float read_ptr_float);

static void CalculateFilterCoeffsLPF(Filter1Pole_t* f, float cutoff_hz);
static inline float Filter1Pole_ProcessLPF(Filter1Pole_t* f, float input);
static inline float Filter1Pole_ProcessHPF(Filter1Pole_t* f, float input);

static float UpdateEnvelope(float prev_state, float input_peak, float attack_coef, float release_coef);
static void UpdateStateAndDensity(SoundBubblesEngine_t* engine, float block_abs_peak);
static void Scheduler_SpawnImmediateBurst(SoundBubblesEngine_t* engine);
static void Scheduler_RunTick(SoundBubblesEngine_t* engine);
static int Voice_Allocate(SoundBubblesEngine_t* engine);
static void Voice_SpawnInit(SoundBubblesEngine_t* engine, int voice_idx, BubbleClass_t b_class);
static float LookupWindow(float phase, WindowType_t type);

// --- Initialization & Config ---

void SoundBubbles_Init(SoundBubblesEngine_t* engine, int16_t* delay_buffer_memory, const EngineConfig_t* initial_config) {
    InitWindowLUTs();

    engine->delay_buffer = delay_buffer_memory;
    engine->config = *initial_config;
    SoundBubbles_SetRngSeed(engine, engine->config.rng_seed);

    engine->write_ptr = 0;
    engine->block_counter = 0;
    engine->engine_state = ENGINE_STATE_SILENCE;

    engine->env_follower_state = 0.0f;
    engine->env_derivative = 0.0f;
    engine->burst_timer_ticks = 0;

    engine->target_density = 0.0f;
    engine->spawn_accumulator = 0.0f;

    engine->internal_ducking_target = 1.0f;
    engine->smoothed_ducking_gain = 1.0f;

    engine->master_dry_gain = 1.0f;
    engine->master_wet_gain = 1.0f;

    for (int i = 0; i < BUBBLES_BUFFER_SIZE_SAMPLES; i++) {
        engine->delay_buffer[i] = 0;
    }

    for (int i = 0; i < BUBBLES_MAX_VOICES; i++) {
        engine->voices[i].state = VOICE_STATE_INACTIVE;
    }

    // Attack HPF (implemented internally as input - LPF)
    CalculateFilterCoeffsLPF(&engine->attack_hpf, 1500.0f);
    // Sustain LPF
    CalculateFilterCoeffsLPF(&engine->sustain_lpf, 2000.0f);

    engine->ducking_lpf.b0 = engine->config.duck_attack_coef;
    engine->ducking_lpf.a1 = 1.0f - engine->config.duck_attack_coef;
    engine->ducking_lpf.z1 = 1.0f;
}

void SoundBubbles_UpdateConfig(SoundBubblesEngine_t* engine, const EngineConfig_t* new_config) {
    bool rng_seed_changed = (engine->config.rng_seed != new_config->rng_seed);
    engine->config = *new_config;
    if (rng_seed_changed) {
        SoundBubbles_SetRngSeed(engine, engine->config.rng_seed);
    }
}

void SoundBubbles_SetRngSeed(SoundBubblesEngine_t* engine, uint32_t seed) {
    engine->config.rng_seed = seed;
    engine->rng_state = (seed == 0u) ? RNG_STATE_FALLBACK : seed;
}

// --- Audio-Rate Processing Loop ---

void SoundBubbles_ProcessBlock(SoundBubblesEngine_t* engine, const float* in_mono, float* out_left, float* out_right, int num_samples) {
    float block_peak = 0.0f;

    for (int i = 0; i < num_samples; i++) {
        float dry_sample = in_mono[i];

        // Track peak for control block envelope
        float in_abs = fabsf(dry_sample);
        if (in_abs > block_peak) {
            block_peak = in_abs;
        }

        // Clamp input to [-1.0f, 1.0f] before conversion
        float clamped_sample = fmaxf(-1.0f, fminf(1.0f, dry_sample));
        engine->delay_buffer[engine->write_ptr] = (int16_t)(clamped_sample * 32767.0f);

        // Zero audio busses
        float bus_attack = 0.0f;
        float bus_flat = 0.0f;
        float bus_sustain = 0.0f;

        // Process active voices
        for (int v_idx = 0; v_idx < BUBBLES_MAX_VOICES; v_idx++) {
            BubbleVoice_t* v = &engine->voices[v_idx];
            if (v->state == VOICE_STATE_INACTIVE) continue;

            // Handle preemption and forced release fading
            if (v->state == VOICE_STATE_PREEMPT_FADING) {
                v->fade_counter--;
                v->amp = (float)v->fade_counter * (1.0f / (float)BUBBLES_FADE_SAMPLES);
                if (v->fade_counter <= 0) {
                    v->state = VOICE_STATE_INACTIVE;
                    continue;
                }
            } else {
                v->amp = 1.0f;
            }

            // Advance phase
            v->phase += v->phase_inc;
            if (v->phase >= 1.0f) {
                v->state = VOICE_STATE_INACTIVE;
                continue;
            }

            // Advance read_ptr strictly at 1.0x playback
            v->read_ptr_float += 1.0f;
            v->read_ptr_float = WrapFloatIndex(v->read_ptr_float, (float)BUBBLES_BUFFER_SIZE_SAMPLES);

            // Directional write-head guard
            if (v->state == VOICE_STATE_PLAYING && CheckGuardZoneDirectional(engine->write_ptr, v->read_ptr_float)) {
                v->state = VOICE_STATE_PREEMPT_FADING;
                v->fade_counter = BUBBLES_FADE_SAMPLES;
            }

            // Interpolate and apply window
            float sample_val = LinearInterpolate(engine->delay_buffer, v->read_ptr_float);
            float window_val = LookupWindow(v->phase, engine->config.class_configs[v->bubble_class].window_type);
            float voice_out = sample_val * window_val * v->amp;

            // Accumulate into designated bus
            if (v->bubble_class == BUBBLE_CLASS_MICRO_ATTACK) {
                bus_attack += voice_out;
            } else if (v->bubble_class == BUBBLE_CLASS_SHORT_INTERMEDIATE) {
                bus_flat += voice_out;
            } else {
                bus_sustain += voice_out;
            }
        }

        // Bus Filters
        float attack_filtered = Filter1Pole_ProcessHPF(&engine->attack_hpf, bus_attack);
        float sustain_filtered = Filter1Pole_ProcessLPF(&engine->sustain_lpf, bus_sustain);

        // Final Output Mix (DSP core owns dry/wet policy)
        float wet_mix = (attack_filtered + bus_flat + sustain_filtered) * engine->smoothed_ducking_gain * engine->master_wet_gain;
        float dry_mix = dry_sample * engine->master_dry_gain;

        out_left[i] = dry_mix + wet_mix;
        out_right[i] = dry_mix + wet_mix;

        // Advance write pointer
        engine->write_ptr = WrapIntIndex(engine->write_ptr + 1, BUBBLES_BUFFER_SIZE_SAMPLES);

        // Execute Control-Rate Tick
        if (++engine->block_counter >= BUBBLES_BLOCK_SIZE) {
            engine->block_counter = 0;
            UpdateStateAndDensity(engine, block_peak);
            Scheduler_RunTick(engine);
            block_peak = 0.0f;
        }
    }
}

// --- Internal Helper Implementations ---

static void UpdateStateAndDensity(SoundBubblesEngine_t* engine, float block_abs_peak) {
    float prev_env = engine->env_follower_state;

    // Update envelope
    engine->env_follower_state = UpdateEnvelope(prev_env, block_abs_peak, ENV_ATTACK_COEF, ENV_RELEASE_COEF);

    // Hard noise floor gating
    if (engine->env_follower_state < engine->config.noise_floor) {
        engine->env_follower_state = 0.0f;
    }

    engine->env_derivative = engine->env_follower_state - prev_env;

    // Transient Detection & State Transitions
    if (engine->env_derivative > engine->config.transient_delta) {
        engine->engine_state = ENGINE_STATE_TRANSIENT_BURST;
        engine->burst_timer_ticks = engine->config.burst_duration_ticks;
        Scheduler_SpawnImmediateBurst(engine);

    } else if (engine->burst_timer_ticks > 0) {
        engine->burst_timer_ticks--;
        if (engine->engine_state == ENGINE_STATE_TRANSIENT_BURST && engine->burst_timer_ticks < (engine->config.burst_duration_ticks - 2)) {
            engine->engine_state = ENGINE_STATE_ATTACK_ONGOING;
        }
    } else {
        if (engine->env_follower_state > engine->config.sustain_thresh) {
            engine->engine_state = ENGINE_STATE_SUSTAIN_BODY;
        } else if (engine->env_follower_state > engine->config.tracking_thresh) {
            engine->engine_state = ENGINE_STATE_SPARSE_DECAY;
        } else {
            engine->engine_state = ENGINE_STATE_SILENCE;
        }
    }

    // Ducking target logic (driven primarily by transient/burst, recovers on sustain/decay)
    if (engine->engine_state == ENGINE_STATE_TRANSIENT_BURST || engine->engine_state == ENGINE_STATE_ATTACK_ONGOING) {
        engine->internal_ducking_target = engine->config.duck_burst_level;
        engine->ducking_lpf.b0 = engine->config.duck_attack_coef;
        engine->ducking_lpf.a1 = 1.0f - engine->config.duck_attack_coef;
    } else {
        // Recover ducking during sustain, decay, or silence
        engine->internal_ducking_target = 1.0f;
        engine->ducking_lpf.b0 = engine->config.duck_release_coef;
        engine->ducking_lpf.a1 = 1.0f - engine->config.duck_release_coef;
    }

    // Control-rate ducking smoothing
    engine->smoothed_ducking_gain = Filter1Pole_ProcessLPF(&engine->ducking_lpf, engine->internal_ducking_target);

    // Target Density updates (Spawns per second)
    switch (engine->engine_state) {
        case ENGINE_STATE_TRANSIENT_BURST:
        case ENGINE_STATE_ATTACK_ONGOING:
            engine->target_density = engine->config.density_burst;
            break;
        case ENGINE_STATE_SUSTAIN_BODY:
            engine->target_density = engine->config.density_sustain;
            break;
        case ENGINE_STATE_SPARSE_DECAY:
            engine->target_density = engine->config.density_decay;
            break;
        case ENGINE_STATE_SILENCE:
        default:
            engine->target_density = 0.0f;
            break;
    }
}

static void Scheduler_SpawnImmediateBurst(SoundBubblesEngine_t* engine) {
    // Defensively clamp immediate burst count
    int burst_count = engine->config.burst_immediate_count;
    if (burst_count < 0) burst_count = 0;
    if (burst_count > BUBBLES_MAX_VOICES) burst_count = BUBBLES_MAX_VOICES;

    for (int i = 0; i < burst_count; i++) {
        int v_idx = Voice_Allocate(engine);
        if (v_idx >= 0) {
            Voice_SpawnInit(engine, v_idx, BUBBLE_CLASS_MICRO_ATTACK);
        }
    }
}

static void Scheduler_RunTick(SoundBubblesEngine_t* engine) {
    if (engine->engine_state == ENGINE_STATE_SILENCE) {
        engine->spawn_accumulator = 0.0f;
        return;
    }

    // Convert target_density (spawns/sec) to fractional spawns per block
    float spawns_per_tick = engine->target_density * ((float)BUBBLES_BLOCK_SIZE / (float)BUBBLES_SAMPLE_RATE);
    engine->spawn_accumulator += spawns_per_tick;

    int spawns_this_tick = 0;
    while (engine->spawn_accumulator >= 1.0f && spawns_this_tick < SCHED_MAX_SPAWNS_PER_TICK) {
        BubbleClass_t selected_class;
        float r = RandomFloat01(engine);

        switch (engine->engine_state) {
            case ENGINE_STATE_TRANSIENT_BURST:
                selected_class = BUBBLE_CLASS_MICRO_ATTACK;
                break;
            case ENGINE_STATE_ATTACK_ONGOING:
                selected_class = (r < 0.8f) ? BUBBLE_CLASS_MICRO_ATTACK : BUBBLE_CLASS_SHORT_INTERMEDIATE;
                break;
            case ENGINE_STATE_SUSTAIN_BODY:
                selected_class = (r < 0.7f) ? BUBBLE_CLASS_SUSTAIN_BODY : BUBBLE_CLASS_SHORT_INTERMEDIATE;
                break;
            case ENGINE_STATE_SPARSE_DECAY:
                selected_class = BUBBLE_CLASS_SUSTAIN_BODY;
                break;
            default:
                selected_class = BUBBLE_CLASS_SHORT_INTERMEDIATE;
                break;
        }

        int voice_idx = Voice_Allocate(engine);
        if (voice_idx >= 0) {
            Voice_SpawnInit(engine, voice_idx, selected_class);
        }

        engine->spawn_accumulator -= 1.0f;
        spawns_this_tick++;
    }

    if (engine->spawn_accumulator > 1.0f) {
        engine->spawn_accumulator = 1.0f;
    }
}

static int Voice_Allocate(SoundBubblesEngine_t* engine) {
    // 1. Return an inactive slot immediately if available
    for (int i = 0; i < BUBBLES_MAX_VOICES; i++) {
        if (engine->voices[i].state == VOICE_STATE_INACTIVE) return i;
    }

    // 2. Stealing required. Priority: Sustain > Short > Micro.
    // Must avoid stealing voices that are too young.
    int victim_idx = -1;
    float max_phase = -1.0f;
    int current_priority = -1; // 2=Sustain, 1=Short, 0=Micro

    for (int i = 0; i < BUBBLES_MAX_VOICES; i++) {
        BubbleVoice_t* v = &engine->voices[i];

        if (v->state == VOICE_STATE_PLAYING && v->phase > STEAL_MIN_PHASE_THRESHOLD) {
            int v_priority = 0;
            if (v->bubble_class == BUBBLE_CLASS_SUSTAIN_BODY) v_priority = 2;
            else if (v->bubble_class == BUBBLE_CLASS_SHORT_INTERMEDIATE) v_priority = 1;

            // If we found a higher priority class, or an older voice of same priority
            if (v_priority > current_priority || (v_priority == current_priority && v->phase > max_phase)) {
                current_priority = v_priority;
                max_phase = v->phase;
                victim_idx = i;
            }
        }
    }

    // Initiate preemption fade on victim
    if (victim_idx >= 0) {
        engine->voices[victim_idx].state = VOICE_STATE_PREEMPT_FADING;
        engine->voices[victim_idx].fade_counter = BUBBLES_FADE_SAMPLES;
    }

    return -1; // Wait for fade out
}

static void Voice_SpawnInit(SoundBubblesEngine_t* engine, int voice_idx, BubbleClass_t b_class) {
    BubbleVoice_t* v = &engine->voices[voice_idx];
    BubbleClassConfig_t* class_cfg = &engine->config.class_configs[b_class];

    v->state = VOICE_STATE_PLAYING;
    v->bubble_class = b_class;
    v->phase = 0.0f;
    v->amp = 1.0f;

    float duration_ms = class_cfg->duration_ms_min + (RandomFloat01(engine) * (class_cfg->duration_ms_max - class_cfg->duration_ms_min));
    float duration_samples = duration_ms * ((float)BUBBLES_SAMPLE_RATE / 1000.0f);

    // Defensively clamp duration_samples to avoid div-by-zero or extremely rapid phase_inc
    if (duration_samples < 10.0f) {
        duration_samples = 10.0f;
    }
    v->phase_inc = 1.0f / duration_samples;

    int32_t base_offset = class_cfg->offset_samples;
    if (b_class == BUBBLE_CLASS_SUSTAIN_BODY) {
        base_offset += engine->config.sustain_read_center_offset_samples;
    }

    int32_t half_jitter = class_cfg->jitter_samples / 2;
    int32_t jitter_val = (int32_t)(RandomFloat01(engine) * (float)class_cfg->jitter_samples) - half_jitter;

    v->read_ptr_float = (float)WrapIntIndex(engine->write_ptr - base_offset + jitter_val, BUBBLES_BUFFER_SIZE_SAMPLES);
}

// --- Mathematics and Filter Helpers ---

static void InitWindowLUTs(void) {
    if (luts_initialized) return;

    for (int i = 0; i < 1024; i++) {
        float phase = (float)i / 1023.0f;
        // Hann Window: 0.5 * (1 - cos(2*pi*phase))
        WindowLUT_Hann[i] = 0.5f * (1.0f - cosf(2.0f * M_PI * phase));

        // Tukey-like Window: flat top, cosine tapers
        float alpha = 0.2f; // 20% taper each side
        if (phase < alpha) {
            WindowLUT_Tukey[i] = 0.5f * (1.0f - cosf(M_PI * phase / alpha));
        } else if (phase > (1.0f - alpha)) {
            WindowLUT_Tukey[i] = 0.5f * (1.0f - cosf(M_PI * (1.0f - phase) / alpha));
        } else {
            WindowLUT_Tukey[i] = 1.0f;
        }
    }
    luts_initialized = true;
}

// Tiny deterministic xorshift32 PRNG.
static uint32_t NextRandomU32(SoundBubblesEngine_t* engine) {
    uint32_t x = engine->rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    engine->rng_state = x;
    return x;
}

// Convert to deterministic float in [0, 1) by mapping top 24 bits into mantissa range.
static float RandomFloat01(SoundBubblesEngine_t* engine) {
    const float kInv24Bit = 1.0f / 16777216.0f; // 2^24
    uint32_t rnd = NextRandomU32(engine);
    return (float)(rnd >> 8) * kInv24Bit;
}

static inline int32_t WrapIntIndex(int32_t index, int32_t size) {
    while (index >= size) index -= size;
    while (index < 0) index += size;
    return index;
}

static inline float WrapFloatIndex(float index, float size) {
    while (index >= size) index -= size;
    while (index < 0.0f) index += size;
    return index;
}

static inline float LinearInterpolate(const int16_t* buffer, float index_float) {
    int32_t idx_int = (int32_t)index_float;
    float frac = index_float - (float)idx_int;
    int32_t idx_next = (idx_int + 1 == BUBBLES_BUFFER_SIZE_SAMPLES) ? 0 : idx_int + 1;

    float val1 = (float)buffer[idx_int] * (1.0f / 32768.0f);
    float val2 = (float)buffer[idx_next] * (1.0f / 32768.0f);
    return val1 + frac * (val2 - val1);
}

static inline bool CheckGuardZoneDirectional(int32_t write_ptr, float read_ptr_float) {
    int32_t read_ptr = (int32_t)read_ptr_float;
    int32_t dist_to_write = write_ptr - read_ptr;
    if (dist_to_write < 0) dist_to_write += BUBBLES_BUFFER_SIZE_SAMPLES;
    return (dist_to_write > 0 && dist_to_write < BUBBLES_GUARD_ZONE_SAMPLES);
}

static float UpdateEnvelope(float prev_state, float input_peak, float attack_coef, float release_coef) {
    if (input_peak > prev_state) {
        return prev_state + attack_coef * (input_peak - prev_state);
    } else {
        return prev_state + release_coef * (input_peak - prev_state);
    }
}

// Basic 1-pole Lowpass coefficient calculation (explicit exponential approximation)
static void CalculateFilterCoeffsLPF(Filter1Pole_t* f, float cutoff_hz) {
    float a1 = expf(-2.0f * M_PI * cutoff_hz / (float)BUBBLES_SAMPLE_RATE);

    f->a1 = a1;
    f->b0 = 1.0f - a1;
    f->z1 = 0.0f;
}

static inline float Filter1Pole_ProcessLPF(Filter1Pole_t* f, float input) {
    f->z1 = (input * f->b0) + (f->z1 * f->a1);
    return f->z1;
}

// Attack HPF formulated correctly as: HPF(x) = x - LPF(x)
static inline float Filter1Pole_ProcessHPF(Filter1Pole_t* f, float input) {
    float lpf_out = Filter1Pole_ProcessLPF(f, input);
    return input - lpf_out;
}

static float LookupWindow(float phase, WindowType_t type) {
    int idx = (int)(phase * 1023.0f);
    if (idx < 0) idx = 0;
    if (idx > 1023) idx = 1023;

    if (type == WINDOW_TYPE_HANN) {
        return WindowLUT_Hann[idx];
    } else {
        return WindowLUT_Tukey[idx];
    }
}
