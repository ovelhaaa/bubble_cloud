#define SOUND_BUBBLES_DSP_INTERNAL 1
#include "sound_bubbles_dsp.h"
#include <float.h>
#include <math.h>
#include <stddef.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

// --- Internal Implementation Constants ---
#define ENV_ATTACK_COEF  0.1f   // ~fast tracking for attacks
#define ENV_RELEASE_COEF 0.01f  // ~slow tracking for sustain/decay

// Minimum phase (age) before a voice is considered "stealable" to avoid dropping very young clicks
#define STEAL_MIN_PHASE_THRESHOLD 0.05f
#define PRESENCE_BLOOM_TICKS 32

// Internal non-UI defaults for bus and presence shaping (musical tuning constants).
#define CLASS_GAIN_MICRO_DEFAULT   1.15f
#define CLASS_GAIN_SHORT_DEFAULT   0.96f
#define CLASS_GAIN_SUSTAIN_DEFAULT 0.78f
#define DROPLET_OCCUPANCY_DISABLE 0.75f
#define DROPLET_OCCUPANCY_REDUCE 0.50f
#define SMART_START_ENERGY_RADIUS 3
#define FINAL_LIMITER_DEFAULT_CEILING_DB -1.0f
#define FINAL_LIMITER_DEFAULT_RELEASE_MS 50.0f

#if !defined(BUBBLES_QUALITY_ESP32_SAFE) && !defined(BUBBLES_QUALITY_WASM_FULL)
#define BUBBLES_QUALITY_STANDARD 1
#endif

// --- Internal LUTs ---
static float WindowLUT_Hann[1024];
static float WindowLUT_Tukey[1024];
static bool luts_initialized = false;
static const uint32_t RNG_STATE_FALLBACK = 0x6D2B79F5u;

typedef struct {
    const ReadRegionConfig_t* region;
    uint8_t region_id;
} ReadRegionChoice_t;

// --- Static Helper Prototypes ---
static void InitWindowLUTs(void);
static uint32_t NextRandomU32(SoundBubblesEngine_t* engine);
static float RandomFloat01(SoundBubblesEngine_t* engine);
static inline int32_t WrapIntIndex(int32_t index, int32_t size);
static inline float WrapFloatIndex(float index, float size);
static inline float LinearInterpolate(const int16_t* buffer, float index_float);
static inline bool CheckGuardZoneDirectional(int32_t write_ptr, float read_ptr_float, float rate);
static float ResolvePitchModeRate(SoundBubblesEngine_t* engine);

static void CalculateFilterCoeffsLPF(Filter1Pole_t* f, float cutoff_hz);
static inline float Filter1Pole_ProcessLPF(Filter1Pole_t* f, float input);
static inline float Filter1Pole_ProcessHPF(Filter1Pole_t* f, float input);

static float UpdateEnvelope(float prev_state, float input_peak, float attack_coef, float release_coef);
static void UpdateStateAndDensity(SoundBubblesEngine_t* engine, float block_abs_peak);
static void Scheduler_SpawnImmediateBurst(SoundBubblesEngine_t* engine);
static void Scheduler_RunTick(SoundBubblesEngine_t* engine);
static int Voice_FindInactiveSlot(SoundBubblesEngine_t* engine);
static int Voice_Allocate(SoundBubblesEngine_t* engine);
static bool Voice_QueuePendingSpawn(SoundBubblesEngine_t* engine, BubbleClass_t b_class, int generation);
static int32_t Voice_CountFadingVoices(const SoundBubblesEngine_t* engine);
static int Voice_FlushPendingSpawns(SoundBubblesEngine_t* engine, int max_spawns);
static bool Voice_RequestSpawn(SoundBubblesEngine_t* engine, BubbleClass_t b_class, int generation);
static void Voice_SpawnInit(SoundBubblesEngine_t* engine, int voice_idx, BubbleClass_t b_class, int generation);
static float LookupWindow(float phase, WindowType_t type);
static ReadRegionChoice_t ResolveReadRegionChoice(SoundBubblesEngine_t* engine, BubbleClass_t bubble_class, EngineState_t engine_state);
static int32_t ChooseReadOffsetSamples(SoundBubblesEngine_t* engine, const ReadRegionConfig_t* region);
static int32_t RefineReadOffsetSmartStart(const SoundBubblesEngine_t* engine, int32_t read_offset_samples, int32_t range);
static float EnvelopeVariantGain(float phase, uint8_t variant, int family);
static float SoftClip(float x, float amount);
static inline float DbToLinear(float db);
static inline void CacheFinalLimiterBlockParams(SoundBubblesEngine_t* engine);
static inline float ProcessFinalLimiterSample(SoundBubblesEngine_t* engine, float* l, float* r);
static inline float DbToLinear(float db) {
    return powf(10.0f, db * 0.05f);
}

static inline void CacheFinalLimiterBlockParams(SoundBubblesEngine_t* engine) {
    float ceiling = DbToLinear(engine->config.final_limiter_ceiling_db);
    if (!isfinite(ceiling) || ceiling <= 0.0f) ceiling = DbToLinear(FINAL_LIMITER_DEFAULT_CEILING_DB);
    if (ceiling > 1.0f) ceiling = 1.0f;
    engine->final_limiter_ceiling_linear = ceiling;

    float release_ms = engine->config.final_limiter_release_ms;
    if (!isfinite(release_ms) || release_ms <= 0.0f) release_ms = FINAL_LIMITER_DEFAULT_RELEASE_MS;
    float release_samples = release_ms * 0.001f * (float)BUBBLES_SAMPLE_RATE;
    engine->final_limiter_release_coef = 1.0f / fmaxf(1.0f, release_samples);
}

static inline float ProcessFinalLimiterSample(SoundBubblesEngine_t* engine, float* l, float* r) {
    float ceiling = engine->final_limiter_ceiling_linear;
    float peak = fmaxf(fabsf(*l), fabsf(*r));
    float target_gain = 1.0f;
    if (peak > ceiling) {
        target_gain = ceiling / peak;
        engine->metrics_clip_count_accum++;
    }

    if (target_gain < engine->final_limiter_gain) {
        engine->final_limiter_gain = target_gain;
    } else {
        engine->final_limiter_gain += (1.0f - engine->final_limiter_gain) * engine->final_limiter_release_coef;
        if (engine->final_limiter_gain > 1.0f) engine->final_limiter_gain = 1.0f;
    }

    *l *= engine->final_limiter_gain;
    *r *= engine->final_limiter_gain;
    return engine->final_limiter_gain;
}

static float ProcessSustainDiffusionSample(SoundBubblesEngine_t* engine, float in, float* delay_line, int delay_samples);
static void ApplyQualityTierDefaults(EngineConfig_t* cfg);
static int32_t ClampActiveVoiceLimit(int32_t requested_limit);
static int32_t ResolveProfileVoiceLimit(BubbleQualityProfile profile);
static void DeactivateVoicesAboveActiveLimit(SoundBubblesEngine_t* engine);
static inline float Clamp01(float x);
static inline float Clamp(float x, float lo, float hi);
static inline float Lerp(float a, float b, float t);
static int32_t CountActiveVoices(const SoundBubblesEngine_t* engine);

// --- Initialization & Config ---

void SoundBubbles_Init(SoundBubblesEngine_t* engine, int16_t* delay_buffer_memory, const EngineConfig_t* initial_config) {
    InitWindowLUTs();

    engine->delay_buffer = delay_buffer_memory;
    engine->config = *initial_config;
    ApplyQualityTierDefaults(&engine->config);
    engine->active_voice_limit = engine->config.active_voice_limit;
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
    engine->class_gain_micro = CLASS_GAIN_MICRO_DEFAULT;
    engine->class_gain_short = CLASS_GAIN_SHORT_DEFAULT;
    engine->class_gain_sustain = CLASS_GAIN_SUSTAIN_DEFAULT;
    engine->wet_presence_target = 0.0f;
    engine->wet_presence_smoothed = 0.0f;
    engine->bloom_timer_ticks = 0;

    engine->master_dry_gain = 1.0f;
    engine->master_wet_gain = 1.0f;
    engine->final_limiter_gain = 1.0f;
    engine->final_limiter_ceiling_linear = 1.0f;
    engine->final_limiter_release_coef = 0.0f;
    engine->metrics_peak_l_accum = 0.0f;
    engine->metrics_peak_r_accum = 0.0f;
    engine->metrics_clip_count_accum = 0;
    engine->metrics_limiter_gain_min = 1.0f;
    engine->metrics_callback = NULL;
    engine->metrics_user_data = NULL;
    engine->metrics_last_block.spawn_count = 0;
    engine->metrics_last_block.active_voices = 0;
    engine->metrics_last_block.engine_state = ENGINE_STATE_SILENCE;
    engine->metrics_last_block.ducking_gain = 1.0f;
    engine->metrics_last_block.envelope = 0.0f;
    engine->metrics_last_block.peak_l = 0.0f;
    engine->metrics_last_block.peak_r = 0.0f;
    engine->metrics_last_block.clip_count = 0;
    engine->metrics_last_block.limiter_gain = 1.0f;
    engine->metrics_tick_spawn_count = 0;
    engine->pending_spawn_head = 0;
    engine->pending_spawn_count = 0;

    for (int i = 0; i < BUBBLES_BUFFER_SIZE_SAMPLES; i++) {
        engine->delay_buffer[i] = 0;
    }

    for (int i = 0; i < BUBBLES_MAX_VOICES; i++) {
        engine->voices[i].state = VOICE_STATE_INACTIVE;
    }

    // Attack HPF (implemented internally as input - LPF), one state per stereo channel.
    CalculateFilterCoeffsLPF(&engine->attack_hpf_l, 1500.0f);
    CalculateFilterCoeffsLPF(&engine->attack_hpf_r, 1500.0f);
    // Sustain LPF, one state per stereo channel.
    CalculateFilterCoeffsLPF(&engine->sustain_lpf_l, 2000.0f);
    CalculateFilterCoeffsLPF(&engine->sustain_lpf_r, 2000.0f);

    engine->ducking_lpf.b0 = engine->config.duck_attack_coef;
    engine->ducking_lpf.a1 = 1.0f - engine->config.duck_attack_coef;
    engine->ducking_lpf.z1 = 1.0f;

    // Faster response when presence rises, slower response when it relaxes.
    engine->wet_presence_lpf.b0 = 0.2f;
    engine->wet_presence_lpf.a1 = 0.8f;
    engine->wet_presence_lpf.z1 = 0.0f;
    engine->sustain_diffusion_write_idx = 0;
    for (int i = 0; i < BUBBLES_SUSTAIN_DIFFUSION_MAX_DELAY; i++) {
        engine->sustain_diffusion_delay_l[i] = 0.0f;
        engine->sustain_diffusion_delay_r[i] = 0.0f;
        engine->sustain_diffusion_delay2_l[i] = 0.0f;
        engine->sustain_diffusion_delay2_r[i] = 0.0f;
    }
}

void SoundBubbles_UpdateConfig(SoundBubblesEngine_t* engine, const EngineConfig_t* new_config) {
    bool rng_seed_changed = (engine->config.rng_seed != new_config->rng_seed);
    engine->config = *new_config;
    ApplyQualityTierDefaults(&engine->config);
    engine->active_voice_limit = engine->config.active_voice_limit;
    DeactivateVoicesAboveActiveLimit(engine);
    if (rng_seed_changed) {
        SoundBubbles_SetRngSeed(engine, engine->config.rng_seed);
    }
}

void SoundBubbles_SetRngSeed(SoundBubblesEngine_t* engine, uint32_t seed) {
    engine->config.rng_seed = seed;
    engine->rng_state = (seed == 0u) ? RNG_STATE_FALLBACK : seed;
}

void SoundBubbles_SetMetricsCallback(SoundBubblesEngine_t* engine, SoundBubblesMetricsCallback_t callback, void* user_data) {
    engine->metrics_callback = callback;
    engine->metrics_user_data = user_data;
}

// --- Audio-Rate Processing Loop ---

void SoundBubbles_ProcessBlock(SoundBubblesEngine_t* engine, const float* in_mono, float* out_left, float* out_right, int num_samples) {
    float block_peak = 0.0f;
    bool freeze_write = (engine->config.freeze_enabled != 0) || (engine->config.freeze_amount >= 0.5f);

    for (int i = 0; i < num_samples; i++) {
        if (i == 0) {
            CacheFinalLimiterBlockParams(engine);
        }

        float dry_sample = in_mono[i];

        // Track peak for control block envelope
        float in_abs = fabsf(dry_sample);
        if (in_abs > block_peak) {
            block_peak = in_abs;
        }

        // Clamp input to [-1.0f, 1.0f] before conversion. Freeze keeps the
        // granular memory write head locked while voices continue processing.
        float clamped_sample = fmaxf(-1.0f, fminf(1.0f, dry_sample));
        if (!freeze_write) {
            engine->delay_buffer[engine->write_ptr] = (int16_t)(clamped_sample * 32767.0f);
        }

        // Zero audio busses
        float bus_attack_l = 0.0f, bus_attack_r = 0.0f;
        float bus_flat_l = 0.0f, bus_flat_r = 0.0f;
        float bus_sustain_l = 0.0f, bus_sustain_r = 0.0f;

        // Process active voices, plus any retired voices above the active limit
        // that are draining through the normal preempt-fade path after a profile
        // downgrade. No new voices are allocated outside active_voice_limit.
        for (int v_idx = 0; v_idx < BUBBLES_MAX_VOICES; v_idx++) {
            BubbleVoice_t* v = &engine->voices[v_idx];
            if (v->state == VOICE_STATE_INACTIVE) continue;
            if (v_idx >= engine->active_voice_limit && v->state != VOICE_STATE_PREEMPT_FADING) continue;

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

            // Advance read_ptr with optional spawn-time attack jittered rate.
            v->read_ptr_float += v->rate;
            v->read_ptr_float = WrapFloatIndex(v->read_ptr_float, (float)BUBBLES_BUFFER_SIZE_SAMPLES);

            // Directional write-head guard. Reverse voices also reject the
            // opposite-side guard band so interpolation cannot wrap into the
            // frozen/live write head neighborhood.
            if (v->state == VOICE_STATE_PLAYING && CheckGuardZoneDirectional(engine->write_ptr, v->read_ptr_float, v->rate)) {
                v->state = VOICE_STATE_PREEMPT_FADING;
                v->fade_counter = BUBBLES_FADE_SAMPLES;
            }

            // Interpolate and apply window
            float sample_val = LinearInterpolate(engine->delay_buffer, v->read_ptr_float);
            float window_val = LookupWindow(v->phase, engine->config.class_configs[v->bubble_class].window_type);
            float env_var = EnvelopeVariantGain(v->phase, v->envelope_variant, engine->config.envelope_family);
            float voice_out = sample_val * window_val * env_var * v->amp * v->gain;

            // Accumulate into designated bus
            if (v->bubble_class == BUBBLE_CLASS_MICRO_ATTACK) {
                bus_attack_l += voice_out * v->pan_l;
                bus_attack_r += voice_out * v->pan_r;
            } else if (v->bubble_class == BUBBLE_CLASS_SHORT_INTERMEDIATE) {
                bus_flat_l += voice_out * v->pan_l;
                bus_flat_r += voice_out * v->pan_r;
            } else {
                bus_sustain_l += voice_out * v->pan_l;
                bus_sustain_r += voice_out * v->pan_r;
            }
        }

        // Bus Filters
        float attack_filtered_l = Filter1Pole_ProcessHPF(&engine->attack_hpf_l, bus_attack_l);
        float attack_filtered_r = Filter1Pole_ProcessHPF(&engine->attack_hpf_r, bus_attack_r);
        float sustain_filtered_l = Filter1Pole_ProcessLPF(&engine->sustain_lpf_l, bus_sustain_l);
        float sustain_filtered_r = Filter1Pole_ProcessLPF(&engine->sustain_lpf_r, bus_sustain_r);

        if (engine->config.sustain_diffusion_enable) {
            int delay_samples = engine->config.sustain_diffusion_delay;
            if (delay_samples < 2) delay_samples = 2;
            if (delay_samples >= BUBBLES_SUSTAIN_DIFFUSION_MAX_DELAY) {
                delay_samples = BUBBLES_SUSTAIN_DIFFUSION_MAX_DELAY - 1;
            }
            int stages = engine->config.sustain_diffusion_stages;
            if (stages < 1) stages = 1;
            if (stages > 2) stages = 2;

            float d_l = sustain_filtered_l;
            float d_r = sustain_filtered_r;
            d_l = ProcessSustainDiffusionSample(engine, d_l, engine->sustain_diffusion_delay_l, delay_samples);
            d_r = ProcessSustainDiffusionSample(engine, d_r, engine->sustain_diffusion_delay_r, delay_samples);
            if (stages > 1) {
                d_l = ProcessSustainDiffusionSample(engine, d_l, engine->sustain_diffusion_delay2_l, delay_samples);
                d_r = ProcessSustainDiffusionSample(engine, d_r, engine->sustain_diffusion_delay2_r, delay_samples);
            }
            sustain_filtered_l = Lerp(sustain_filtered_l, d_l, Clamp01(engine->config.sustain_diffusion_amount));
            sustain_filtered_r = Lerp(sustain_filtered_r, d_r, Clamp01(engine->config.sustain_diffusion_amount));
        }

        // Final Output Mix (DSP core owns dry/wet policy)
        float attack_tilt = 1.0f;
        float sustain_tilt = 1.0f;
        if (engine->engine_state == ENGINE_STATE_TRANSIENT_BURST || engine->engine_state == ENGINE_STATE_ATTACK_ONGOING) {
            // Brighter onsets: emphasize HPF micro bus and slightly trim darker sustain bus.
            attack_tilt = 1.08f;
            sustain_tilt = 0.92f;
        } else if (engine->engine_state == ENGINE_STATE_SUSTAIN_BODY || engine->engine_state == ENGINE_STATE_SPARSE_DECAY) {
            // Later phrase stages: keep darker halo and avoid clicky forwardness.
            attack_tilt = 0.95f;
            sustain_tilt = 1.06f;
        }

        float wet_sum_l =
            (attack_filtered_l * attack_tilt * engine->class_gain_micro) +
            (bus_flat_l * engine->class_gain_short) +
            (sustain_filtered_l * sustain_tilt * engine->class_gain_sustain);
        float wet_sum_r =
            (attack_filtered_r * attack_tilt * engine->class_gain_micro) +
            (bus_flat_r * engine->class_gain_short) +
            (sustain_filtered_r * sustain_tilt * engine->class_gain_sustain);
        float wet_drive = fmaxf(0.1f, engine->config.wet_drive);
        float wet_clip_amt = Clamp01(engine->config.wet_clip_amount);
        float wet_trim = fmaxf(0.0f, engine->config.wet_output_trim);
        wet_sum_l = SoftClip(wet_sum_l * wet_drive, wet_clip_amt) * wet_trim;
        wet_sum_r = SoftClip(wet_sum_r * wet_drive, wet_clip_amt) * wet_trim;
        float wet_gain = engine->smoothed_ducking_gain * engine->wet_presence_smoothed * engine->master_wet_gain;
        float wet_mix_l = wet_sum_l * wet_gain;
        float wet_mix_r = wet_sum_r * wet_gain;
        float dry_mix = dry_sample * engine->master_dry_gain;

        float final_l = dry_mix + wet_mix_l;
        float final_r = dry_mix + wet_mix_r;
        float limiter_gain = ProcessFinalLimiterSample(engine, &final_l, &final_r);

        float out_abs_l = fabsf(final_l);
        float out_abs_r = fabsf(final_r);
        if (out_abs_l > engine->metrics_peak_l_accum) engine->metrics_peak_l_accum = out_abs_l;
        if (out_abs_r > engine->metrics_peak_r_accum) engine->metrics_peak_r_accum = out_abs_r;
        if (limiter_gain < engine->metrics_limiter_gain_min) engine->metrics_limiter_gain_min = limiter_gain;

        out_left[i] = final_l;
        out_right[i] = final_r;

        if (engine->config.sustain_diffusion_enable) {
            engine->sustain_diffusion_write_idx++;
            if (engine->sustain_diffusion_write_idx >= BUBBLES_SUSTAIN_DIFFUSION_MAX_DELAY) {
                engine->sustain_diffusion_write_idx = 0;
            }
        }

        // Advance write pointer unless freeze has locked the granular memory.
        if (!freeze_write) {
            engine->write_ptr = WrapIntIndex(engine->write_ptr + 1, BUBBLES_BUFFER_SIZE_SAMPLES);
        }

        // Execute Control-Rate Tick
        if (++engine->block_counter >= BUBBLES_BLOCK_SIZE) {
            engine->block_counter = 0;
            engine->metrics_tick_spawn_count = 0;
            UpdateStateAndDensity(engine, block_peak);
            Scheduler_RunTick(engine);

            engine->metrics_last_block.spawn_count = engine->metrics_tick_spawn_count;
            engine->metrics_last_block.active_voices = CountActiveVoices(engine);
            engine->metrics_last_block.engine_state = (int32_t)engine->engine_state;
            engine->metrics_last_block.ducking_gain = engine->smoothed_ducking_gain;
            engine->metrics_last_block.envelope = engine->env_follower_state;
            engine->metrics_last_block.peak_l = engine->metrics_peak_l_accum;
            engine->metrics_last_block.peak_r = engine->metrics_peak_r_accum;
            engine->metrics_last_block.clip_count = engine->metrics_clip_count_accum;
            engine->metrics_last_block.limiter_gain = engine->metrics_limiter_gain_min;
            engine->metrics_peak_l_accum = 0.0f;
            engine->metrics_peak_r_accum = 0.0f;
            engine->metrics_clip_count_accum = 0;
            engine->metrics_limiter_gain_min = engine->final_limiter_gain;
            if (engine->metrics_callback != NULL) {
                engine->metrics_callback(&engine->metrics_last_block, engine->metrics_user_data);
            }

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
        engine->bloom_timer_ticks = PRESENCE_BLOOM_TICKS;
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

    if (engine->bloom_timer_ticks > 0) {
        engine->bloom_timer_ticks--;
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

    // Wet presence macro-shape across phrase phases:
    // - transient: preserve dry attack clarity
    // - post-attack bloom: brief intentional rise in wet audibility
    // - sustain: stable perceptible halo
    // - decay/silence: thinner but still audible tail
    switch (engine->engine_state) {
        case ENGINE_STATE_TRANSIENT_BURST:
            engine->wet_presence_target = 0.58f;
            break;
        case ENGINE_STATE_ATTACK_ONGOING:
        {
            float bloom_progress = 1.0f;
            if (PRESENCE_BLOOM_TICKS > 0) {
                bloom_progress = 1.0f - ((float)engine->bloom_timer_ticks / (float)PRESENCE_BLOOM_TICKS);
            }
            bloom_progress = Clamp01(bloom_progress);
            engine->wet_presence_target = Lerp(1.28f, 1.02f, bloom_progress);
            break;
        }
        case ENGINE_STATE_SUSTAIN_BODY:
            engine->wet_presence_target = 0.88f;
            break;
        case ENGINE_STATE_SPARSE_DECAY:
            engine->wet_presence_target = 0.54f;
            break;
        case ENGINE_STATE_SILENCE:
        default:
            engine->wet_presence_target = 0.32f;
            break;
    }

    // Asymmetric smoothing keeps bloom responsive but avoids pumping on release.
    if (engine->wet_presence_target > engine->wet_presence_smoothed) {
        engine->wet_presence_lpf.b0 = 0.25f;
        engine->wet_presence_lpf.a1 = 0.75f;
    } else {
        engine->wet_presence_lpf.b0 = 0.08f;
        engine->wet_presence_lpf.a1 = 0.92f;
    }
    engine->wet_presence_smoothed = Filter1Pole_ProcessLPF(&engine->wet_presence_lpf, engine->wet_presence_target);

    // Target Density updates (Spawns per second), modulated by envelope and derivative within bounded state ranges.
    float env_norm = 0.0f;
    if (engine->config.sustain_thresh > engine->config.noise_floor) {
        env_norm = (engine->env_follower_state - engine->config.noise_floor) / (engine->config.sustain_thresh - engine->config.noise_floor);
    }
    env_norm = Clamp01(env_norm);

    float d_norm = 0.0f;
    if (engine->config.transient_delta > 1.0e-6f) {
        d_norm = engine->env_derivative / engine->config.transient_delta;
    }
    d_norm = Clamp(d_norm, -1.0f, 1.0f);

    switch (engine->engine_state) {
        case ENGINE_STATE_TRANSIENT_BURST:
        {
            float burst_max = engine->config.density_burst;
            float burst_min = burst_max * 0.7f;
            float burst_progress = 0.0f;
            if (engine->config.burst_duration_ticks > 0) {
                burst_progress = 1.0f - (float)engine->burst_timer_ticks / (float)engine->config.burst_duration_ticks;
            }
            burst_progress = Clamp01(burst_progress);
            float accent = Clamp01((1.0f - burst_progress) * 0.7f + Clamp01(d_norm) * 0.3f);
            engine->target_density = Lerp(burst_min, burst_max, accent);
            break;
        }
        case ENGINE_STATE_ATTACK_ONGOING:
        {
            float attack_max = engine->config.density_burst;
            float attack_min = attack_max * 0.55f;
            float attack_shape = Clamp01(0.65f * env_norm + 0.35f * Clamp01(d_norm));
            engine->target_density = Lerp(attack_min, attack_max, attack_shape);
            break;
        }
        case ENGINE_STATE_SUSTAIN_BODY:
        {
            float sustain_min = engine->config.density_sustain * 0.85f;
            float sustain_max = engine->config.density_sustain * 1.1f;
            float sustain_shape = Clamp01((env_norm - 0.4f) / 0.6f);
            float derivative_damp = 1.0f - 0.25f * fabsf(d_norm);
            sustain_shape *= Clamp01(derivative_damp);
            engine->target_density = Lerp(sustain_min, sustain_max, sustain_shape);
            break;
        }
        case ENGINE_STATE_SPARSE_DECAY:
        {
            float decay_max = engine->config.density_decay;
            float decay_min = decay_max * 0.1f;
            float tail = Clamp01((engine->env_follower_state - engine->config.tracking_thresh) /
                                 fmaxf(1.0e-6f, (engine->config.sustain_thresh - engine->config.tracking_thresh)));
            float release_emphasis = Clamp01(0.5f - 0.5f * d_norm);
            engine->target_density = Lerp(decay_min, decay_max, tail * release_emphasis);
            break;
        }
        case ENGINE_STATE_SILENCE:
        default:
            engine->target_density = 0.0f;
            break;
    }

    // Shimmer increases upper-octave cloud density in a bounded way while
    // preserving the existing scheduler cap.
    float shimmer = Clamp01(engine->config.shimmer_amount);
    if (engine->config.pitch_mode == BUBBLE_PITCH_MODE_SHIMMER && shimmer > 0.0f) {
        engine->target_density *= 1.0f + (0.5f * shimmer);
    }
}

static void Scheduler_SpawnImmediateBurst(SoundBubblesEngine_t* engine) {
    // Defensively clamp immediate burst count
    int burst_count = engine->config.burst_immediate_count;
    if (burst_count < 0) burst_count = 0;
    if (burst_count > engine->active_voice_limit) burst_count = engine->active_voice_limit;

    for (int i = 0; i < burst_count; i++) {
        if (Voice_RequestSpawn(engine, BUBBLE_CLASS_MICRO_ATTACK, 0)) {
            engine->metrics_tick_spawn_count++;
        }
    }
}

static void Scheduler_RunTick(SoundBubblesEngine_t* engine) {
    int spawns_this_tick = Voice_FlushPendingSpawns(engine, SCHED_MAX_SPAWNS_PER_TICK);

    if (engine->engine_state == ENGINE_STATE_SILENCE) {
        engine->spawn_accumulator = 0.0f;
        return;
    }

    // Convert target_density (spawns/sec) to fractional spawns per block
    float spawns_per_tick = engine->target_density * ((float)BUBBLES_BLOCK_SIZE / (float)BUBBLES_SAMPLE_RATE);
    engine->spawn_accumulator += spawns_per_tick;

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

        if (Voice_RequestSpawn(engine, selected_class, 0)) {
            engine->metrics_tick_spawn_count++;
        }

        engine->spawn_accumulator -= 1.0f;
        spawns_this_tick++;
    }

    if (engine->spawn_accumulator > 1.0f) {
        engine->spawn_accumulator = 1.0f;
    }
}

static int Voice_FindInactiveSlot(SoundBubblesEngine_t* engine) {
    for (int i = 0; i < engine->active_voice_limit; i++) {
        if (engine->voices[i].state == VOICE_STATE_INACTIVE) return i;
    }

    return -1;
}

static int Voice_Allocate(SoundBubblesEngine_t* engine) {
    // 1. Return an inactive slot immediately if available. Inactive slots are the
    // only allocation result that callers may initialize immediately.
    int inactive_idx = Voice_FindInactiveSlot(engine);
    if (inactive_idx >= 0) {
        return inactive_idx;
    }

    // 2. Stealing policy when fully occupied (deterministic, bounded, musically protective):
    //    - Class priority is MICRO first, then SHORT, then SUSTAIN/BODY.
    //    - Inside a class, steal the "least useful" voice: older phase and lower remaining contribution.
    //    - Young-voice protection threshold is respected first. If every voice is still young,
    //      deterministically fall back to the best global candidate so a spawn request can queue.
    //    - Tie-breaks are resolved by lower voice index for fixed-seed reproducibility.
    //
    // Active victims are never returned as ready-to-write slots. They are moved into
    // PREEMPT_FADING and keep their read pointer/phase fields intact until the fade
    // reaches INACTIVE, at which point queued spawns can consume the slot on a later tick.
    int best_protected_idx = -1;
    int best_fallback_idx = -1;
    int best_protected_rank = 99;
    int best_fallback_rank = 99;
    float best_protected_score = -1.0f;
    float best_fallback_score = -1.0f;

    for (int i = 0; i < engine->active_voice_limit; i++) {
        BubbleVoice_t* v = &engine->voices[i];
        if (v->state != VOICE_STATE_PLAYING) {
            continue;
        }

        int class_rank = 2; // lowest steal priority by default (Sustain/Body)
        if (v->bubble_class == BUBBLE_CLASS_MICRO_ATTACK) {
            class_rank = 0;
        } else if (v->bubble_class == BUBBLE_CLASS_SHORT_INTERMEDIATE) {
            class_rank = 1;
        }

        float remaining_contrib = (1.0f - Clamp01(v->phase)) * Clamp01(v->amp);
        float usefulness_score = Clamp01(v->phase) + (1.0f - Clamp01(remaining_contrib));
        bool old_enough = (v->phase > STEAL_MIN_PHASE_THRESHOLD);

        bool better_than_best_protected =
            (class_rank < best_protected_rank) ||
            (class_rank == best_protected_rank &&
             (usefulness_score > best_protected_score ||
              (usefulness_score == best_protected_score &&
               (best_protected_idx < 0 || i < best_protected_idx))));

        if (old_enough && better_than_best_protected) {
            best_protected_rank = class_rank;
            best_protected_score = usefulness_score;
            best_protected_idx = i;
        }

        bool better_than_best_fallback =
            (class_rank < best_fallback_rank) ||
            (class_rank == best_fallback_rank &&
             (usefulness_score > best_fallback_score ||
              (usefulness_score == best_fallback_score &&
               (best_fallback_idx < 0 || i < best_fallback_idx))));

        if (better_than_best_fallback) {
            best_fallback_rank = class_rank;
            best_fallback_score = usefulness_score;
            best_fallback_idx = i;
        }
    }

    int victim_idx = (best_protected_idx >= 0) ? best_protected_idx : best_fallback_idx;
    if (victim_idx >= 0) {
        BubbleVoice_t* victim = &engine->voices[victim_idx];
        victim->state = VOICE_STATE_PREEMPT_FADING;
        victim->fade_counter = BUBBLES_FADE_SAMPLES;
    }

    return -1;
}

static bool Voice_QueuePendingSpawn(SoundBubblesEngine_t* engine, BubbleClass_t b_class, int generation) {
    if (engine->pending_spawn_count >= BUBBLES_PENDING_SPAWN_CAPACITY) {
        return false;
    }

    int tail = (engine->pending_spawn_head + engine->pending_spawn_count) % BUBBLES_PENDING_SPAWN_CAPACITY;
    engine->pending_spawns[tail].bubble_class = b_class;
    engine->pending_spawns[tail].generation = (uint8_t)((generation <= 0) ? 0 : 1);
    engine->pending_spawn_count++;
    return true;
}

static int32_t Voice_CountFadingVoices(const SoundBubblesEngine_t* engine) {
    int32_t fading_count = 0;

    for (int i = 0; i < engine->active_voice_limit; i++) {
        if (engine->voices[i].state == VOICE_STATE_PREEMPT_FADING) {
            fading_count++;
        }
    }

    return fading_count;
}

static int Voice_FlushPendingSpawns(SoundBubblesEngine_t* engine, int max_spawns) {
    int spawned_count = 0;

    while (engine->pending_spawn_count > 0 && spawned_count < max_spawns) {
        int voice_idx = Voice_FindInactiveSlot(engine);
        if (voice_idx < 0) {
            break;
        }

        PendingSpawn_t request = engine->pending_spawns[engine->pending_spawn_head];
        engine->pending_spawn_head = (engine->pending_spawn_head + 1) % BUBBLES_PENDING_SPAWN_CAPACITY;
        engine->pending_spawn_count--;

        Voice_SpawnInit(engine, voice_idx, request.bubble_class, request.generation);
        engine->metrics_tick_spawn_count++;
        spawned_count++;
    }

    if (engine->pending_spawn_count == 0) {
        engine->pending_spawn_head = 0;
    }

    return spawned_count;
}

static bool Voice_RequestSpawn(SoundBubblesEngine_t* engine, BubbleClass_t b_class, int generation) {
    int inactive_idx = Voice_FindInactiveSlot(engine);
    if (inactive_idx >= 0) {
        Voice_SpawnInit(engine, inactive_idx, b_class, generation);
        return true;
    }

    if (engine->pending_spawn_count >= BUBBLES_PENDING_SPAWN_CAPACITY) {
        return false;
    }

    // Only preempt a new victim when currently fading voices cannot already cover
    // all queued spawns plus this new request. Existing PREEMPT_FADING voices are
    // guaranteed future inactive slots, so reusing their pending capacity avoids
    // unnecessary polyphony loss.
    int32_t fading_count = Voice_CountFadingVoices(engine);
    if (fading_count <= engine->pending_spawn_count) {
        (void)Voice_Allocate(engine);
    }

    Voice_QueuePendingSpawn(engine, b_class, generation);
    return false;
}

static void Voice_SpawnInit(SoundBubblesEngine_t* engine, int voice_idx, BubbleClass_t b_class, int generation) {
    BubbleVoice_t* v = &engine->voices[voice_idx];
    BubbleClassConfig_t* class_cfg = &engine->config.class_configs[b_class];

    v->state = VOICE_STATE_PLAYING;
    v->bubble_class = b_class;
    v->generation = (uint8_t)((generation <= 0) ? 0 : 1);
    v->phase = 0.0f;
    v->amp = 1.0f;
    v->quantized_rate = ResolvePitchModeRate(engine);
    v->read_direction = (RandomFloat01(engine) < Clamp01(engine->config.reverse_probability)) ? 1u : 0u;
    v->rate = (v->read_direction != 0u) ? -v->quantized_rate : v->quantized_rate;
    if (engine->config.attack_rate_jitter && b_class == BUBBLE_CLASS_MICRO_ATTACK) {
        float d = Clamp(engine->config.attack_rate_jitter_depth, 0.0f, 0.2f);
        float j = (RandomFloat01(engine) * 2.0f - 1.0f) * d;
        v->quantized_rate = Clamp(v->quantized_rate + j, 0.25f, 4.0f);
        v->rate = (v->read_direction != 0u) ? -v->quantized_rate : v->quantized_rate;
    }
    v->gain = 1.0f;

    float duration_ms = class_cfg->duration_ms_min + (RandomFloat01(engine) * (class_cfg->duration_ms_max - class_cfg->duration_ms_min));
    if (v->generation == 1) {
        duration_ms *= Clamp(engine->config.droplet_length_scale, 0.2f, 1.0f);
        v->gain *= Clamp(engine->config.droplet_gain, 0.0f, 1.0f);
    }
    float duration_samples = duration_ms * ((float)BUBBLES_SAMPLE_RATE / 1000.0f);

    // Defensively clamp duration_samples to avoid div-by-zero or extremely rapid phase_inc
    if (duration_samples < 10.0f) {
        duration_samples = 10.0f;
    }
    v->phase_inc = 1.0f / duration_samples;

    ReadRegionChoice_t region_choice = ResolveReadRegionChoice(engine, b_class, engine->engine_state);
    int32_t read_offset_samples = ChooseReadOffsetSamples(engine, region_choice.region);

    // Clamp spawn offsets so reverse and high-rate reads have a guard-banded
    // path through the ring buffer for the expected voice duration. Forward
    // voices are also kept far enough behind the write head for their pitch
    // rate when possible. Runtime guards still handle pathological parameter
    // updates and long frozen reads.
    float span_rate = (v->rate < 0.0f) ? (1.0f + fabsf(v->rate)) : fabsf(v->rate);
    int32_t projected_span = (int32_t)ceilf(duration_samples * span_rate);
    if (projected_span < 0) projected_span = 0;
    int32_t max_guarded_offset = BUBBLES_BUFFER_SIZE_SAMPLES - BUBBLES_GUARD_ZONE_SAMPLES - 1;
    if (v->rate < 0.0f) {
        int32_t reverse_max = max_guarded_offset - projected_span;
        if (reverse_max < BUBBLES_GUARD_ZONE_SAMPLES) reverse_max = BUBBLES_GUARD_ZONE_SAMPLES;
        if (read_offset_samples > reverse_max) read_offset_samples = reverse_max;
    } else if (v->rate > 1.0f) {
        int32_t forward_min = BUBBLES_GUARD_ZONE_SAMPLES + projected_span;
        if (forward_min > max_guarded_offset) forward_min = max_guarded_offset;
        if (read_offset_samples < forward_min) read_offset_samples = forward_min;
    }

    if (engine->config.smart_start_enable) {
        read_offset_samples = RefineReadOffsetSmartStart(engine, read_offset_samples, engine->config.smart_start_range);
    }
    v->read_ptr_float = (float)WrapIntIndex(engine->write_ptr - read_offset_samples, BUBBLES_BUFFER_SIZE_SAMPLES);

    float spread = (b_class == BUBBLE_CLASS_MICRO_ATTACK) ? engine->config.attack_pan_spread : engine->config.sustain_pan_spread;
    float pan = (RandomFloat01(engine) * 2.0f - 1.0f) * Clamp01(spread) * Clamp01(engine->config.stereo_width);
    float pan_pos = Clamp((pan + 1.0f) * 0.5f, 0.0f, 1.0f);
    float theta = pan_pos * (0.5f * M_PI);
    v->pan_l = cosf(theta);
    v->pan_r = sinf(theta);

    int env_count = (engine->config.envelope_variation > 0.001f) ? 3 : 1;
    v->envelope_variant = (uint8_t)(NextRandomU32(engine) % (uint32_t)env_count);

    int tone_count = (engine->config.tone_variation > 0.001f) ? 3 : 1;
    v->tone_profile = (uint8_t)(NextRandomU32(engine) % (uint32_t)tone_count);
    if (v->tone_profile == 0) {
        v->gain *= (b_class == BUBBLE_CLASS_MICRO_ATTACK) ? Clamp(engine->config.attack_brightness, 0.3f, 1.8f) : 1.0f;
    } else if (v->tone_profile == 2) {
        v->gain *= (b_class == BUBBLE_CLASS_SUSTAIN_BODY) ? Clamp(1.0f - engine->config.sustain_darkness, 0.2f, 1.0f) : 0.92f;
    }

    v->source_region_id = region_choice.region_id;
    if (region_choice.region_id == 2) {
        v->gain *= Clamp(1.0f - 0.6f * engine->config.memory_darkening, 0.2f, 1.0f);
    }

    // Optional hard-limited 2nd-generation droplet (single child, no recursive chain).
    if (engine->config.droplet_enable && v->generation == 0 && b_class == BUBBLE_CLASS_MICRO_ATTACK) {
        int active = CountActiveVoices(engine);
        float occupancy = (float)active / (float)engine->active_voice_limit;
        if (occupancy < DROPLET_OCCUPANCY_DISABLE) {
            float prob = Clamp01(engine->config.droplet_probability);
            if (occupancy > DROPLET_OCCUPANCY_REDUCE) {
                prob *= 0.35f;
            }
            if (RandomFloat01(engine) < prob) {
                if (Voice_RequestSpawn(engine, BUBBLE_CLASS_SHORT_INTERMEDIATE, 1)) {
                    engine->metrics_tick_spawn_count++;
                }
            }
        }
    }
}

static ReadRegionChoice_t ResolveReadRegionChoice(SoundBubblesEngine_t* engine, BubbleClass_t bubble_class, EngineState_t engine_state) {
    // Deterministic map from "what bubble" + "what phrase phase" => temporal memory slice.
    // Attack-oriented contexts read from attack/body. Tail-oriented contexts read from memory.
    ReadRegionChoice_t choice;

    if (bubble_class == BUBBLE_CLASS_MICRO_ATTACK) {
        choice.region = &engine->config.attack_region;
        choice.region_id = 0;
        return choice;
    }

    float mem_bias = 0.0f;
    if (bubble_class == BUBBLE_CLASS_SHORT_INTERMEDIATE) {
        mem_bias = Clamp01(engine->config.memory_mix);
        if (engine_state == ENGINE_STATE_SUSTAIN_BODY || engine_state == ENGINE_STATE_SPARSE_DECAY) {
            mem_bias = Clamp01(mem_bias + engine->config.memory_pull * 0.35f);
        }
    } else {
        mem_bias = Clamp01(engine->config.memory_mix + engine->config.memory_pull);
        if (engine_state == ENGINE_STATE_TRANSIENT_BURST || engine_state == ENGINE_STATE_ATTACK_ONGOING) {
            mem_bias *= 0.35f;
        }
    }

    if (RandomFloat01(engine) < mem_bias) {
        choice.region = &engine->config.memory_region;
        choice.region_id = 2;
    } else {
        choice.region = &engine->config.body_region;
        choice.region_id = 1;
    }

    return choice;
}

static int32_t ChooseReadOffsetSamples(SoundBubblesEngine_t* engine, const ReadRegionConfig_t* region) {
    // Clamp and normalize range so presets stay ring-buffer safe.
    const int32_t min_safe = BUBBLES_GUARD_ZONE_SAMPLES;
    const int32_t max_safe = BUBBLES_BUFFER_SIZE_SAMPLES - BUBBLES_GUARD_ZONE_SAMPLES - 1;

    int32_t min_offset = region->min_offset_samples;
    int32_t max_offset = region->max_offset_samples;

    if (min_offset < min_safe) min_offset = min_safe;
    if (max_offset < min_safe) max_offset = min_safe;
    if (min_offset > max_safe) min_offset = max_safe;
    if (max_offset > max_safe) max_offset = max_safe;
    if (max_offset < min_offset) max_offset = min_offset;

    int32_t span = max_offset - min_offset;
    if (span == 0) {
        return min_offset;
    }

    // Deterministic uniform selection over [min_offset, max_offset].
    uint32_t rnd = NextRandomU32(engine);
    uint32_t bucket = (uint32_t)(span + 1);
    return min_offset + (int32_t)(rnd % bucket);
}

static int32_t RefineReadOffsetSmartStart(const SoundBubblesEngine_t* engine, int32_t read_offset_samples, int32_t range) {
    int32_t best = read_offset_samples;
    int32_t scan = range;
    if (scan < 1) return best;
    if (scan > 64) scan = 64;

    float best_energy = FLT_MAX;
    for (int32_t delta = -scan; delta <= scan; delta++) {
        int32_t candidate = read_offset_samples + delta;
        if (candidate < BUBBLES_GUARD_ZONE_SAMPLES) continue;
        if (candidate >= (BUBBLES_BUFFER_SIZE_SAMPLES - BUBBLES_GUARD_ZONE_SAMPLES)) continue;
        int32_t idx = WrapIntIndex(engine->write_ptr - candidate, BUBBLES_BUFFER_SIZE_SAMPLES);
        int32_t prev = WrapIntIndex(idx - 1, BUBBLES_BUFFER_SIZE_SAMPLES);
        float a = (float)engine->delay_buffer[prev];
        float b = (float)engine->delay_buffer[idx];
        if ((a <= 0.0f && b >= 0.0f) || (a >= 0.0f && b <= 0.0f)) {
            return candidate;
        }

        float energy = 0.0f;
        for (int k = -SMART_START_ENERGY_RADIUS; k <= SMART_START_ENERGY_RADIUS; k++) {
            int32_t eidx = WrapIntIndex(idx + k, BUBBLES_BUFFER_SIZE_SAMPLES);
            float s = (float)engine->delay_buffer[eidx];
            energy += s * s;
        }
        if (energy < best_energy) {
            best_energy = energy;
            best = candidate;
        }
    }
    return best;
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

static inline float Clamp01(float x) {
    return Clamp(x, 0.0f, 1.0f);
}

static inline float Clamp(float x, float lo, float hi) {
    return fmaxf(lo, fminf(hi, x));
}

static inline float Lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

static int32_t CountActiveVoices(const SoundBubblesEngine_t* engine) {
    int32_t count = 0;
    for (int i = 0; i < engine->active_voice_limit; i++) {
        if (engine->voices[i].state != VOICE_STATE_INACTIVE) {
            count++;
        }
    }
    return count;
}

static inline int32_t WrapIntIndex(int32_t index, int32_t size) {
    while (index >= size) index -= size;
    while (index < 0) index += size;
    return index;
}

static inline float WrapFloatIndex(float index, float size) {
    if (index >= 0.0f && index < size) {
        return index;
    }
    if (!isfinite(index) || !isfinite(size) || size <= 0.0f) {
        return 0.0f;
    }

    index = fmodf(index, size);
    if (index < 0.0f) {
        index += size;
    }
    if (!(index >= 0.0f && index < size)) {
        return 0.0f;
    }
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

static inline bool CheckGuardZoneDirectional(int32_t write_ptr, float read_ptr_float, float rate) {
    int32_t read_ptr = (int32_t)read_ptr_float;
    if (rate >= 0.0f) {
        int32_t dist_behind_write = write_ptr - read_ptr;
        if (dist_behind_write < 0) dist_behind_write += BUBBLES_BUFFER_SIZE_SAMPLES;
        return (dist_behind_write >= 0 && dist_behind_write < BUBBLES_GUARD_ZONE_SAMPLES);
    } else {
        int32_t dist_ahead_of_write = read_ptr - write_ptr;
        if (dist_ahead_of_write < 0) dist_ahead_of_write += BUBBLES_BUFFER_SIZE_SAMPLES;
        return (dist_ahead_of_write >= 0 && dist_ahead_of_write < BUBBLES_GUARD_ZONE_SAMPLES);
    }
}

static float ResolvePitchModeRate(SoundBubblesEngine_t* engine) {
    switch (engine->config.pitch_mode) {
        case BUBBLE_PITCH_MODE_OCTAVE_UP:
            return 2.0f;
        case BUBBLE_PITCH_MODE_OCTAVE_DOWN:
            return 0.5f;
        case BUBBLE_PITCH_MODE_FIFTH:
            return 1.5f;
        case BUBBLE_PITCH_MODE_SHIMMER:
        {
            float shimmer = Clamp01(engine->config.shimmer_amount);
            return (RandomFloat01(engine) < shimmer) ? 2.0f : 1.0f;
        }
        case BUBBLE_PITCH_MODE_UNISON:
        default:
            return 1.0f;
    }
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

static float EnvelopeVariantGain(float phase, uint8_t variant, int family) {
    float p = Clamp01(phase);
    if (variant == 0) return 1.0f;
    if (family == ENVELOPE_FAMILY_SOFT) {
        // "Soft" arch without trig in the audio loop: 4p(1-p) in [0, 1].
        float arch = 4.0f * p * (1.0f - p);
        return (variant == 1) ? (0.85f + 0.15f * arch) : (0.75f + 0.25f * (1.0f - p));
    }
    return (variant == 1) ? (0.92f + 0.08f * (1.0f - p)) : (0.85f + 0.15f * p);
}

static float SoftClip(float x, float amount) {
    float a = Clamp01(amount);
    float x_c = Clamp(x, -1.0f, 1.0f);
    float cubic = x_c - 0.3333333f * x_c * x_c * x_c;
    return Lerp(x, cubic, a);
}


static float ProcessSustainDiffusionSample(SoundBubblesEngine_t* engine, float in, float* delay_line, int delay_samples) {
    int read_idx = engine->sustain_diffusion_write_idx - delay_samples;
    if (read_idx < 0) read_idx += BUBBLES_SUSTAIN_DIFFUSION_MAX_DELAY;
    float delayed = delay_line[read_idx];
    float g = Clamp(engine->config.sustain_diffusion_feedback, 0.0f, 0.95f);
    float y = -g * in + delayed;
    delay_line[engine->sustain_diffusion_write_idx] = in + g * y;
    return y;
}

static int32_t ClampActiveVoiceLimit(int32_t requested_limit) {
    if (requested_limit < 1) return 1;
    if (requested_limit > BUBBLE_ENGINE_MAX_VOICES) return BUBBLE_ENGINE_MAX_VOICES;
    return requested_limit;
}

static int32_t ResolveProfileVoiceLimit(BubbleQualityProfile profile) {
    for (int i = 0; i < BUBBLE_QUALITY_PROFILE_COUNT; i++) {
        if (BUBBLE_QUALITY_PROFILE_LIMITS[i].profile == profile) {
            return BUBBLE_QUALITY_PROFILE_LIMITS[i].voice_limit;
        }
    }
    return BUBBLE_QUALITY_DEFAULT_VOICE_LIMIT;
}

static void DeactivateVoicesAboveActiveLimit(SoundBubblesEngine_t* engine) {
    for (int i = engine->active_voice_limit; i < BUBBLES_MAX_VOICES; i++) {
        BubbleVoice_t* voice = &engine->voices[i];
        if (voice->state == VOICE_STATE_INACTIVE) continue;
        voice->state = VOICE_STATE_PREEMPT_FADING;
        if (voice->fade_counter <= 0 || voice->fade_counter > BUBBLES_FADE_SAMPLES) {
            voice->fade_counter = BUBBLES_FADE_SAMPLES;
        }
    }
}

static void ApplyQualityTierDefaults(EngineConfig_t* cfg) {
    if (cfg->quality_profile < BUBBLE_QUALITY_PROFILE_MCU_SAFE || cfg->quality_profile > BUBBLE_QUALITY_PROFILE_WEB_ULTRA) {
        cfg->quality_profile = BUBBLE_QUALITY_PROFILE_WEB_STANDARD;
    }
    if (cfg->active_voice_limit <= 0) {
        // A zero-initialized legacy config has quality_profile == MCU_SAFE because
        // that enum value is 0. Treat the absent voice limit as the legacy full
        // pool instead of silently reducing old direct-DSP callers to 8 voices.
        if (cfg->quality_profile == BUBBLE_QUALITY_PROFILE_MCU_SAFE) {
            cfg->quality_profile = BUBBLE_QUALITY_PROFILE_WEB_ULTRA;
        }
        cfg->active_voice_limit = ResolveProfileVoiceLimit(cfg->quality_profile);
    } else {
        cfg->active_voice_limit = ClampActiveVoiceLimit(cfg->active_voice_limit);
    }

    if (cfg->smart_start_range == 0) cfg->smart_start_range = 12;
    if (cfg->wet_drive <= 0.0f) cfg->wet_drive = 1.0f;
    if (cfg->wet_output_trim <= 0.0f) cfg->wet_output_trim = 1.0f;
    bool limiter_config_missing = (cfg->final_limiter_ceiling_db == 0.0f && cfg->final_limiter_release_ms <= 0.0f);
    if (!isfinite(cfg->final_limiter_ceiling_db) || limiter_config_missing) cfg->final_limiter_ceiling_db = FINAL_LIMITER_DEFAULT_CEILING_DB;
    cfg->final_limiter_ceiling_db = Clamp(cfg->final_limiter_ceiling_db, -24.0f, 0.0f);
    if (!isfinite(cfg->final_limiter_release_ms) || cfg->final_limiter_release_ms <= 0.0f) cfg->final_limiter_release_ms = FINAL_LIMITER_DEFAULT_RELEASE_MS;
    cfg->final_limiter_release_ms = Clamp(cfg->final_limiter_release_ms, 5.0f, 500.0f);
    if (cfg->sustain_diffusion_stages == 0) cfg->sustain_diffusion_stages = 1;
    if (cfg->sustain_diffusion_delay == 0) cfg->sustain_diffusion_delay = 18;
    if (cfg->droplet_length_scale <= 0.0f) cfg->droplet_length_scale = 0.6f;
    if (cfg->attack_brightness <= 0.0f) cfg->attack_brightness = 1.15f;
    cfg->freeze_amount = Clamp01(cfg->freeze_amount);
    cfg->freeze_enabled = (cfg->freeze_enabled != 0) ? 1 : 0;
    cfg->reverse_probability = Clamp01(cfg->reverse_probability);
    cfg->shimmer_amount = Clamp01(cfg->shimmer_amount);
    if (cfg->pitch_mode < BUBBLE_PITCH_MODE_UNISON || cfg->pitch_mode > BUBBLE_PITCH_MODE_SHIMMER) {
        cfg->pitch_mode = BUBBLE_PITCH_MODE_UNISON;
    }

#if defined(BUBBLES_QUALITY_ESP32_SAFE)
    cfg->sustain_diffusion_stages = 1;
    cfg->droplet_enable = 0;
    cfg->attack_rate_jitter_depth = fminf(cfg->attack_rate_jitter_depth, 0.03f);
    cfg->smart_start_range = (cfg->smart_start_range > 16) ? 16 : cfg->smart_start_range;
#elif defined(BUBBLES_QUALITY_WASM_FULL)
    // Keep caller values; wasm tier can run all optional layers.
#else
    cfg->sustain_diffusion_stages = (cfg->sustain_diffusion_stages > 2) ? 2 : cfg->sustain_diffusion_stages;
    cfg->smart_start_range = (cfg->smart_start_range > 32) ? 32 : cfg->smart_start_range;
#endif
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
