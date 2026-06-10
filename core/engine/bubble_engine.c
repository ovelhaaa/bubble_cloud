#define SOUND_BUBBLES_DSP_INTERNAL 1
#include "bubble_engine.h"

#include <stddef.h>
#include <string.h>

#define BUBBLE_MACRO_SMOOTH_COEF 0.18f
#define BUBBLE_MACRO_EPSILON 0.0005f
#define BUBBLE_MOTION_SHAPE_TRIANGLE 0
#define BUBBLE_MOTION_SHAPE_SMOOTH 1
#define BUBBLE_MOTION_SHAPE_HOLD 2

const BubbleParameterInfo BUBBLE_PARAMETER_INFO[] = {
    { BUBBLE_PARAM_DENSITY, "density", 0.0f, 1.0f, 0.5f, BUBBLE_PARAMETER_CURVE_LOG, "norm", BUBBLE_PARAMETER_FLAG_MACRO | BUBBLE_PARAMETER_FLAG_AUDIBLE },
    { BUBBLE_PARAM_BLOOM, "bloom", 0.0f, 1.0f, 0.5f, BUBBLE_PARAMETER_CURVE_EXP, "norm", BUBBLE_PARAMETER_FLAG_MACRO | BUBBLE_PARAMETER_FLAG_AUDIBLE },
    { BUBBLE_PARAM_MOTION, "motion", 0.0f, 1.0f, 0.5f, BUBBLE_PARAMETER_CURVE_LINEAR, "norm", BUBBLE_PARAMETER_FLAG_MACRO | BUBBLE_PARAMETER_FLAG_AUDIBLE },
    { BUBBLE_PARAM_TEXTURE, "texture", 0.0f, 1.0f, 0.5f, BUBBLE_PARAMETER_CURVE_LINEAR, "norm", BUBBLE_PARAMETER_FLAG_MACRO | BUBBLE_PARAMETER_FLAG_AUDIBLE },
    { BUBBLE_PARAM_SPACE, "space", 0.0f, 1.0f, 0.5f, BUBBLE_PARAMETER_CURVE_LINEAR, "norm", BUBBLE_PARAMETER_FLAG_MACRO | BUBBLE_PARAMETER_FLAG_AUDIBLE },
    { BUBBLE_PARAM_GRAVITY, "gravity", 0.0f, 1.0f, 0.5f, BUBBLE_PARAMETER_CURVE_LOG, "norm", BUBBLE_PARAMETER_FLAG_MACRO | BUBBLE_PARAMETER_FLAG_AUDIBLE },
    { BUBBLE_PARAM_MEMORY, "memory", 0.0f, 1.0f, 0.5f, BUBBLE_PARAMETER_CURVE_LINEAR, "norm", BUBBLE_PARAMETER_FLAG_MACRO | BUBBLE_PARAMETER_FLAG_AUDIBLE },
    { BUBBLE_PARAM_CLARITY, "clarity", 0.0f, 1.0f, 0.5f, BUBBLE_PARAMETER_CURVE_EXP, "norm", BUBBLE_PARAMETER_FLAG_MACRO | BUBBLE_PARAMETER_FLAG_AUDIBLE },
    { BUBBLE_PARAM_FREEZE, "freeze", 0.0f, 1.0f, 0.0f, BUBBLE_PARAMETER_CURVE_LINEAR, "norm", BUBBLE_PARAMETER_FLAG_MACRO | BUBBLE_PARAMETER_FLAG_AUDIBLE },
    { BUBBLE_PARAM_SPARKLE, "sparkle", 0.0f, 1.0f, 0.0f, BUBBLE_PARAMETER_CURVE_EXP, "norm", BUBBLE_PARAMETER_FLAG_MACRO | BUBBLE_PARAMETER_FLAG_AUDIBLE },
    { BUBBLE_PARAM_WARMTH, "warmth", 0.0f, 1.0f, 0.5f, BUBBLE_PARAMETER_CURVE_LINEAR, "norm", BUBBLE_PARAMETER_FLAG_MACRO | BUBBLE_PARAMETER_FLAG_AUDIBLE },
    { BUBBLE_PARAM_MIX, "mix", 0.0f, 1.0f, 0.5f, BUBBLE_PARAMETER_CURVE_LINEAR, "norm", BUBBLE_PARAMETER_FLAG_MACRO | BUBBLE_PARAMETER_FLAG_AUDIBLE },
    { BUBBLE_PARAM_DEVELOPER_MODE, "developer_mode", 0.0f, 1.0f, 0.0f, BUBBLE_PARAMETER_CURVE_TOGGLE, "bool", BUBBLE_PARAMETER_FLAG_DEVELOPER | BUBBLE_PARAMETER_FLAG_BOOLEAN },
    { BUBBLE_ENGINE_PARAM_DENSITY_BURST, "density_burst", 0.0f, 200.0f, 50.0f, BUBBLE_PARAMETER_CURVE_LOG, "sp/s", BUBBLE_PARAMETER_FLAG_DEVELOPER | BUBBLE_PARAMETER_FLAG_AUDIBLE },
    { BUBBLE_ENGINE_PARAM_DENSITY_SUSTAIN, "density_sustain", 0.0f, 100.0f, 15.0f, BUBBLE_PARAMETER_CURVE_LOG, "sp/s", BUBBLE_PARAMETER_FLAG_DEVELOPER | BUBBLE_PARAMETER_FLAG_AUDIBLE },
    { BUBBLE_ENGINE_PARAM_DENSITY_DECAY, "density_decay", 0.0f, 50.0f, 5.0f, BUBBLE_PARAMETER_CURVE_LOG, "sp/s", BUBBLE_PARAMETER_FLAG_DEVELOPER | BUBBLE_PARAMETER_FLAG_AUDIBLE },
    { BUBBLE_ENGINE_PARAM_STEREO_WIDTH, "stereo_width", 0.0f, 1.0f, 0.7f, BUBBLE_PARAMETER_CURVE_LINEAR, "norm", BUBBLE_PARAMETER_FLAG_DEVELOPER | BUBBLE_PARAMETER_FLAG_AUDIBLE },
    { BUBBLE_ENGINE_PARAM_MIX_DRY_GAIN, "mix_dry_gain", 0.0f, 1.0f, 1.0f, BUBBLE_PARAMETER_CURVE_LINEAR, "gain", BUBBLE_PARAMETER_FLAG_DEVELOPER | BUBBLE_PARAMETER_FLAG_AUDIBLE },
    { BUBBLE_ENGINE_PARAM_MIX_WET_GAIN, "mix_wet_gain", 0.0f, 1.0f, 1.0f, BUBBLE_PARAMETER_CURVE_LINEAR, "gain", BUBBLE_PARAMETER_FLAG_DEVELOPER | BUBBLE_PARAMETER_FLAG_AUDIBLE },
    { BUBBLE_ENGINE_PARAM_MOTION_RATE, "motion_rate", 0.0f, 1.0f, 0.18f, BUBBLE_PARAMETER_CURVE_EXP, "norm", BUBBLE_PARAMETER_FLAG_DEVELOPER | BUBBLE_PARAMETER_FLAG_AUDIBLE },
    { BUBBLE_ENGINE_PARAM_MOTION_DEPTH, "motion_depth", 0.0f, 1.0f, 0.0f, BUBBLE_PARAMETER_CURVE_LINEAR, "norm", BUBBLE_PARAMETER_FLAG_DEVELOPER | BUBBLE_PARAMETER_FLAG_AUDIBLE },
    { BUBBLE_ENGINE_PARAM_MOTION_SHAPE, "motion_shape", 0.0f, 2.0f, 0.0f, BUBBLE_PARAMETER_CURVE_LINEAR, "enum", BUBBLE_PARAMETER_FLAG_DEVELOPER | BUBBLE_PARAMETER_FLAG_INTEGER },
    { BUBBLE_ENGINE_PARAM_RUNTIME_ENVELOPE, "runtime_envelope", 0.0f, 1.0f, 0.0f, BUBBLE_PARAMETER_CURVE_LINEAR, "norm", BUBBLE_PARAMETER_FLAG_RUNTIME }
};

const int BUBBLE_PARAMETER_INFO_COUNT = (int)(sizeof(BUBBLE_PARAMETER_INFO) / sizeof(BUBBLE_PARAMETER_INFO[0]));

const BubbleParameterInfo* bubble_engine_get_parameter_info(BubbleParameterId parameter) {
    for (int i = 0; i < BUBBLE_PARAMETER_INFO_COUNT; i++) {
        if (BUBBLE_PARAMETER_INFO[i].id == parameter) return &BUBBLE_PARAMETER_INFO[i];
    }
    return NULL;
}

static float Clamp01f(float value) {
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

static float Clampf(float value, float lo, float hi) {
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

static uint32_t MotionHashLocal(uint32_t state) {
    state ^= state >> 16;
    state *= 0x7feb352du;
    state ^= state >> 15;
    state *= 0x846ca68bu;
    state ^= state >> 16;
    return state;
}

static float MotionHashToBipolarLocal(uint32_t state) {
    uint32_t mantissa = (MotionHashLocal(state) >> 8) & 0x00FFFFFFu;
    return ((float)mantissa * (1.0f / 8388607.5f)) - 1.0f;
}

static float MotionTickLfoLocal(BubbleMotionLfoState_t* lfo, float rate_hz, float rate_scale, int32_t shape) {
    if (lfo == NULL) return 0.0f;
    float inc = rate_hz * rate_scale * ((float)BUBBLES_BLOCK_SIZE / (float)BUBBLES_SAMPLE_RATE);
    inc = Clampf(inc, 0.0f, 0.25f);
    float next_phase = lfo->phase + inc;
    bool wrapped = false;
    if (next_phase >= 1.0f) {
        next_phase -= 1.0f;
        wrapped = true;
    }
    lfo->phase = next_phase;

    if (shape == BUBBLE_MOTION_SHAPE_HOLD) {
        if (wrapped) {
            lfo->hold_state = MotionHashLocal(lfo->hold_state + 0x9E3779B9u);
            lfo->value = MotionHashToBipolarLocal(lfo->hold_state);
        }
        return lfo->value;
    }

    float triangle = (lfo->phase < 0.5f) ? (4.0f * lfo->phase - 1.0f) : (3.0f - 4.0f * lfo->phase);
    if (shape == BUBBLE_MOTION_SHAPE_SMOOTH) {
        float magnitude = triangle < 0.0f ? -triangle : triangle;
        float smooth = magnitude * magnitude * (3.0f - 2.0f * magnitude);
        return (triangle < 0.0f) ? -smooth : smooth;
    }
    return triangle;
}

static int MacroIndex(BubbleParameterId parameter) {
    if (parameter >= BUBBLE_PARAM_DENSITY && parameter <= BUBBLE_PARAM_MIX) {
        return (int)parameter - (int)BUBBLE_PARAM_DENSITY;
    }
    return -1;
}

static bool IsDeveloperOnlyParameter(BubbleParameterId parameter) {
    return parameter >= BUBBLE_ENGINE_PARAM_NOISE_FLOOR &&
           parameter <= BUBBLE_ENGINE_PARAM_MOTION_SHAPE &&
           parameter != BUBBLE_ENGINE_PARAM_QUALITY_PROFILE &&
           parameter != BUBBLE_ENGINE_PARAM_ACTIVE_VOICE_LIMIT;
}

static void ApplyRuntimeMotionConfig(BubbleEngine_t* engine) {
    if (engine == NULL) return;

    BubbleEngineConfig_t config = engine->motion_base_config;
    float depth = Clamp01f(config.motion_depth);
    if (depth <= 0.0001f) {
        SoundBubbles_UpdateRuntimeConfig(engine, &config);
        return;
    }

    float rate_hz = 0.015f + 0.285f * Clamp01f(config.motion_rate);
    int32_t shape = config.motion_shape;
    float density_lfo = MotionTickLfoLocal(&engine->motion_state.density, rate_hz, 0.73f, shape);
    float panorama_lfo = MotionTickLfoLocal(&engine->motion_state.panorama, rate_hz, 0.49f, shape);
    float memory_lfo = MotionTickLfoLocal(&engine->motion_state.memory_pull, rate_hz, 0.37f, shape);
    float sparkle_lfo = MotionTickLfoLocal(&engine->motion_state.sparkle, rate_hz, 0.61f, shape);
    float reverse_lfo = MotionTickLfoLocal(&engine->motion_state.reverse_probability, rate_hz, 0.29f, shape);
    float diffusion_lfo = MotionTickLfoLocal(&engine->motion_state.diffusion_amount, rate_hz, 0.41f, shape);

    float density_scale = 1.0f + density_lfo * (0.35f * depth);
    config.density_burst *= density_scale;
    config.density_sustain *= density_scale;
    config.density_decay *= density_scale;

    config.stereo_width = Clamp01f(config.stereo_width + panorama_lfo * (0.18f * depth));
    config.attack_pan_spread = Clamp01f(config.attack_pan_spread + panorama_lfo * (0.22f * depth));
    config.sustain_pan_spread = Clamp01f(config.sustain_pan_spread - panorama_lfo * (0.14f * depth));
    config.memory_pull = Clamp01f(config.memory_pull + memory_lfo * (0.24f * depth));
    config.shimmer_amount = Clamp01f(config.shimmer_amount + sparkle_lfo * (0.18f * depth));
    if (config.shimmer_amount > 0.03f) {
        config.pitch_mode = BUBBLE_PITCH_MODE_SHIMMER;
    }
    config.reverse_probability = Clamp01f(config.reverse_probability + reverse_lfo * (0.18f * depth));
    config.sustain_diffusion_amount = Clamp01f(config.sustain_diffusion_amount + diffusion_lfo * (0.24f * depth));

    SoundBubbles_UpdateRuntimeConfig(engine, &config);
}

static void ApplyMacroConfig(BubbleEngine_t* engine) {
    if (engine == NULL) return;
    BubbleEngineConfig_t config;
    float master_dry_gain = engine->master_dry_gain;
    float master_wet_gain = engine->master_wet_gain;
    bubble_macro_map_resolve(engine->macro_values, &engine->motion_base_config, &config, &master_dry_gain, &master_wet_gain);
    engine->master_dry_gain = master_dry_gain;
    engine->master_wet_gain = master_wet_gain;
    SoundBubbles_UpdateConfig(engine, &config);
    ApplyRuntimeMotionConfig(engine);
}


static void ApplyMacroControlRate(BubbleEngine_t* engine) {
    if (engine == NULL) return;
    if (engine->macro_dirty_mask == 0u) {
        ApplyRuntimeMotionConfig(engine);
        return;
    }
    uint32_t still_dirty = 0u;
    for (int i = 0; i < BUBBLE_PARAM_MACRO_COUNT; i++) {
        if ((engine->macro_dirty_mask & (1u << i)) == 0u) continue;
        float target = engine->macro_targets[i];
        float current = engine->macro_values[i];
        float delta = target - current;
        if (delta < BUBBLE_MACRO_EPSILON && delta > -BUBBLE_MACRO_EPSILON) {
            engine->macro_values[i] = target;
        } else {
            engine->macro_values[i] = current + delta * BUBBLE_MACRO_SMOOTH_COEF;
            still_dirty |= (1u << i);
        }
    }
    engine->macro_dirty_mask = still_dirty;
    ApplyMacroConfig(engine);
}


static int32_t CountActiveVoices(const BubbleEngine_t* engine) {
    int32_t count = 0;
    for (int i = 0; i < engine->active_voice_limit; i++) {
        if (engine->voices[i].state != VOICE_STATE_INACTIVE) {
            count++;
        }
    }
    return count;
}

void bubble_engine_default_config(BubbleEngineConfig_t* config) {
    if (config == NULL) {
        return;
    }

    memset(config, 0, sizeof(*config));
    config->noise_floor = 0.001f;
    config->tracking_thresh = 0.01f;
    config->sustain_thresh = 0.05f;
    config->transient_delta = 0.05f;
    config->duck_burst_level = 0.2f;
    config->duck_attack_coef = 0.80f;
    config->duck_release_coef = 0.99f;
    config->burst_duration_ticks = 10;
    config->burst_immediate_count = 3;
    config->density_burst = 50.0f;
    config->density_sustain = 15.0f;
    config->density_decay = 5.0f;

    // Semantic read regions: attack(10-80ms), body(80-250ms), memory(250-900ms).
    config->attack_region.min_offset_samples = 441;
    config->attack_region.max_offset_samples = 3528;
    config->body_region.min_offset_samples = 3528;
    config->body_region.max_offset_samples = 11025;
    config->memory_region.min_offset_samples = 11025;
    config->memory_region.max_offset_samples = 39690;
    config->rng_seed = 1u;

    config->stereo_width = 0.7f;
    config->attack_pan_spread = 0.85f;
    config->sustain_pan_spread = 0.45f;
    config->smart_start_enable = 1;
    config->smart_start_range = 12;
    config->envelope_variation = 0.35f;
    config->envelope_family = ENVELOPE_FAMILY_CLASSIC;
    config->wet_drive = 1.0f;
    config->wet_clip_amount = 0.2f;
    config->wet_output_trim = 1.0f;
    config->final_limiter_ceiling_db = -1.0f;
    config->final_limiter_release_ms = 50.0f;
    config->sustain_diffusion_enable = 0;
    config->sustain_diffusion_amount = 0.35f;
    config->sustain_diffusion_stages = 1;
    config->sustain_diffusion_delay = 18;
    config->sustain_diffusion_feedback = 0.45f;
    config->droplet_enable = 0;
    config->droplet_probability = 0.12f;
    config->droplet_gain = 0.5f;
    config->droplet_length_scale = 0.6f;
    config->memory_mix = 0.35f;
    config->memory_pull = 0.25f;
    config->memory_darkening = 0.2f;
    config->tone_variation = 0.4f;
    config->attack_brightness = 1.15f;
    config->sustain_darkness = 0.25f;
    config->attack_rate_jitter = 0;
    config->attack_rate_jitter_depth = 0.02f;
    config->freeze_amount = 0.0f;
    config->freeze_enabled = 0;
    config->reverse_probability = 0.0f;
    config->pitch_mode = BUBBLE_PITCH_MODE_UNISON;
    config->shimmer_amount = 0.0f;
    config->motion_rate = 0.18f;
    config->motion_depth = 0.0f;
    config->motion_shape = BUBBLE_MOTION_SHAPE_TRIANGLE;
    config->quality_profile = BUBBLE_QUALITY_PROFILE_WEB_STANDARD;
    config->active_voice_limit = BUBBLE_QUALITY_DEFAULT_VOICE_LIMIT;

    config->class_configs[BUBBLE_CLASS_MICRO_ATTACK].duration_ms_min = 5.0f;
    config->class_configs[BUBBLE_CLASS_MICRO_ATTACK].duration_ms_max = 15.0f;
    config->class_configs[BUBBLE_CLASS_MICRO_ATTACK].window_type = WINDOW_TYPE_HANN;

    config->class_configs[BUBBLE_CLASS_SHORT_INTERMEDIATE].duration_ms_min = 20.0f;
    config->class_configs[BUBBLE_CLASS_SHORT_INTERMEDIATE].duration_ms_max = 50.0f;
    config->class_configs[BUBBLE_CLASS_SHORT_INTERMEDIATE].window_type = WINDOW_TYPE_HANN;

    config->class_configs[BUBBLE_CLASS_SUSTAIN_BODY].duration_ms_min = 80.0f;
    config->class_configs[BUBBLE_CLASS_SUSTAIN_BODY].duration_ms_max = 200.0f;
    config->class_configs[BUBBLE_CLASS_SUSTAIN_BODY].window_type = WINDOW_TYPE_TUKEY_LIKE;
}

void bubble_engine_init(BubbleEngine_t* engine, int16_t* delay_buffer_memory, const BubbleEngineConfig_t* initial_config) {
    if (engine == NULL || delay_buffer_memory == NULL) {
        return;
    }

    BubbleEngineConfig_t default_config;
    bool use_macro_defaults = false;
    if (initial_config == NULL) {
        bubble_engine_default_config(&default_config);
        initial_config = &default_config;
        use_macro_defaults = true;
    }
    SoundBubbles_Init(engine, delay_buffer_memory, initial_config);
    if (use_macro_defaults) {
        ApplyMacroConfig(engine);
        engine->macro_dirty_mask = 0u;
    }
}

void bubble_engine_reset_motion_phase(BubbleEngine_t* engine) {
    if (engine == NULL) return;
    SoundBubbles_ResetMotionPhase(engine);
}

void bubble_engine_reset(BubbleEngine_t* engine) {
    if (engine == NULL || engine->delay_buffer == NULL) {
        return;
    }

    BubbleEnginePreset_t preset;
    BubbleEngineMetricsCallback_t metrics_callback = engine->metrics_callback;
    void* metrics_user_data = engine->metrics_user_data;
    int16_t* delay_buffer = engine->delay_buffer;
    float macro_values[BUBBLE_PARAM_MACRO_COUNT];
    float macro_targets[BUBBLE_PARAM_MACRO_COUNT];
    uint32_t macro_dirty_mask = engine->macro_dirty_mask;
    int32_t developer_mode = engine->developer_mode;
    for (int i = 0; i < BUBBLE_PARAM_MACRO_COUNT; i++) {
        macro_values[i] = engine->macro_values[i];
        macro_targets[i] = engine->macro_targets[i];
    }
    if (!bubble_engine_save_preset(engine, &preset)) {
        return;
    }

    SoundBubbles_Init(engine, delay_buffer, &preset.config);
    engine->master_dry_gain = preset.master_dry_gain;
    engine->master_wet_gain = preset.master_wet_gain;
    for (int i = 0; i < BUBBLE_PARAM_MACRO_COUNT; i++) {
        engine->macro_values[i] = macro_values[i];
        engine->macro_targets[i] = macro_targets[i];
    }
    engine->macro_dirty_mask = macro_dirty_mask;
    engine->developer_mode = developer_mode;
    bubble_engine_set_metrics_callback(engine, metrics_callback, metrics_user_data);
}

void bubble_engine_process(BubbleEngine_t* engine, const float* in_mono, float* out_left, float* out_right, int num_samples) {
    if (engine == NULL || engine->delay_buffer == NULL || in_mono == NULL || out_left == NULL || out_right == NULL || num_samples <= 0) {
        return;
    }

    int processed = 0;
    while (processed < num_samples) {
        int until_control = BUBBLES_BLOCK_SIZE - engine->block_counter;
        if (until_control <= 0 || until_control > BUBBLES_BLOCK_SIZE) {
            until_control = BUBBLES_BLOCK_SIZE;
        }
        int chunk = num_samples - processed;
        if (chunk > until_control) {
            chunk = until_control;
        }
        SoundBubbles_ProcessBlock(engine, &in_mono[processed], &out_left[processed], &out_right[processed], chunk);
        processed += chunk;
        if (engine->block_counter == 0) {
            ApplyMacroControlRate(engine);
        }
    }
}

bool bubble_engine_set_parameter(BubbleEngine_t* engine, BubbleEngineParameterId_t parameter, float value) {
    if (engine == NULL) {
        return false;
    }

    if (parameter == BUBBLE_PARAM_DEVELOPER_MODE) {
        engine->developer_mode = value >= 0.5f ? 1 : 0;
        return true;
    }

    int macro_index = MacroIndex(parameter);
    if (macro_index >= 0) {
        engine->macro_targets[macro_index] = Clamp01f(value);
        engine->macro_dirty_mask |= (1u << macro_index);
        return true;
    }

    if (!engine->developer_mode && IsDeveloperOnlyParameter(parameter)) {
        return false;
    }

    BubbleEngineConfig_t config = engine->motion_base_config;
    bool config_changed = true;

    switch (parameter) {
        case BUBBLE_ENGINE_PARAM_NOISE_FLOOR: config.noise_floor = value; break;
        case BUBBLE_ENGINE_PARAM_TRACKING_THRESH: config.tracking_thresh = value; break;
        case BUBBLE_ENGINE_PARAM_SUSTAIN_THRESH: config.sustain_thresh = value; break;
        case BUBBLE_ENGINE_PARAM_TRANSIENT_DELTA: config.transient_delta = value; break;
        case BUBBLE_ENGINE_PARAM_DUCK_BURST_LEVEL: config.duck_burst_level = value; break;
        case BUBBLE_ENGINE_PARAM_DUCK_ATTACK_COEF: config.duck_attack_coef = value; break;
        case BUBBLE_ENGINE_PARAM_DUCK_RELEASE_COEF: config.duck_release_coef = value; break;
        case BUBBLE_ENGINE_PARAM_BURST_DURATION_TICKS: config.burst_duration_ticks = (int32_t)value; break;
        case BUBBLE_ENGINE_PARAM_BURST_IMMEDIATE_COUNT: config.burst_immediate_count = (int32_t)value; break;
        case BUBBLE_ENGINE_PARAM_DENSITY_BURST: config.density_burst = value; break;
        case BUBBLE_ENGINE_PARAM_DENSITY_SUSTAIN: config.density_sustain = value; break;
        case BUBBLE_ENGINE_PARAM_DENSITY_DECAY: config.density_decay = value; break;
        case BUBBLE_ENGINE_PARAM_ATTACK_REGION_MIN_OFFSET_SAMPLES: config.attack_region.min_offset_samples = (int32_t)value; break;
        case BUBBLE_ENGINE_PARAM_ATTACK_REGION_MAX_OFFSET_SAMPLES: config.attack_region.max_offset_samples = (int32_t)value; break;
        case BUBBLE_ENGINE_PARAM_BODY_REGION_MIN_OFFSET_SAMPLES: config.body_region.min_offset_samples = (int32_t)value; break;
        case BUBBLE_ENGINE_PARAM_BODY_REGION_MAX_OFFSET_SAMPLES: config.body_region.max_offset_samples = (int32_t)value; break;
        case BUBBLE_ENGINE_PARAM_MEMORY_REGION_MIN_OFFSET_SAMPLES: config.memory_region.min_offset_samples = (int32_t)value; break;
        case BUBBLE_ENGINE_PARAM_MEMORY_REGION_MAX_OFFSET_SAMPLES: config.memory_region.max_offset_samples = (int32_t)value; break;
        case BUBBLE_ENGINE_PARAM_MICRO_DURATION_MS_MIN: config.class_configs[BUBBLE_CLASS_MICRO_ATTACK].duration_ms_min = value; break;
        case BUBBLE_ENGINE_PARAM_MICRO_DURATION_MS_MAX: config.class_configs[BUBBLE_CLASS_MICRO_ATTACK].duration_ms_max = value; break;
        case BUBBLE_ENGINE_PARAM_SHORT_DURATION_MS_MIN: config.class_configs[BUBBLE_CLASS_SHORT_INTERMEDIATE].duration_ms_min = value; break;
        case BUBBLE_ENGINE_PARAM_SHORT_DURATION_MS_MAX: config.class_configs[BUBBLE_CLASS_SHORT_INTERMEDIATE].duration_ms_max = value; break;
        case BUBBLE_ENGINE_PARAM_BODY_DURATION_MS_MIN: config.class_configs[BUBBLE_CLASS_SUSTAIN_BODY].duration_ms_min = value; break;
        case BUBBLE_ENGINE_PARAM_BODY_DURATION_MS_MAX: config.class_configs[BUBBLE_CLASS_SUSTAIN_BODY].duration_ms_max = value; break;
        case BUBBLE_ENGINE_PARAM_RNG_SEED: config.rng_seed = (uint32_t)value; break;
        case BUBBLE_ENGINE_PARAM_MIX_DRY_GAIN: engine->master_dry_gain = value; config_changed = false; break;
        case BUBBLE_ENGINE_PARAM_MIX_WET_GAIN: engine->master_wet_gain = value; config_changed = false; break;
        case BUBBLE_ENGINE_PARAM_STEREO_WIDTH: config.stereo_width = value; break;
        case BUBBLE_ENGINE_PARAM_ATTACK_PAN_SPREAD: config.attack_pan_spread = value; break;
        case BUBBLE_ENGINE_PARAM_SUSTAIN_PAN_SPREAD: config.sustain_pan_spread = value; break;
        case BUBBLE_ENGINE_PARAM_SMART_START_ENABLE: config.smart_start_enable = (int32_t)value; break;
        case BUBBLE_ENGINE_PARAM_SMART_START_RANGE: config.smart_start_range = (int32_t)value; break;
        case BUBBLE_ENGINE_PARAM_ENVELOPE_VARIATION: config.envelope_variation = value; break;
        case BUBBLE_ENGINE_PARAM_ENVELOPE_FAMILY: config.envelope_family = (int32_t)value; break;
        case BUBBLE_ENGINE_PARAM_WET_DRIVE: config.wet_drive = value; break;
        case BUBBLE_ENGINE_PARAM_WET_CLIP_AMOUNT: config.wet_clip_amount = value; break;
        case BUBBLE_ENGINE_PARAM_WET_OUTPUT_TRIM: config.wet_output_trim = value; break;
        case BUBBLE_ENGINE_PARAM_SUSTAIN_DIFFUSION_ENABLE: config.sustain_diffusion_enable = (int32_t)value; break;
        case BUBBLE_ENGINE_PARAM_SUSTAIN_DIFFUSION_AMOUNT: config.sustain_diffusion_amount = value; break;
        case BUBBLE_ENGINE_PARAM_SUSTAIN_DIFFUSION_STAGES: config.sustain_diffusion_stages = (int32_t)value; break;
        case BUBBLE_ENGINE_PARAM_SUSTAIN_DIFFUSION_DELAY: config.sustain_diffusion_delay = (int32_t)value; break;
        case BUBBLE_ENGINE_PARAM_SUSTAIN_DIFFUSION_FEEDBACK: config.sustain_diffusion_feedback = value; break;
        case BUBBLE_ENGINE_PARAM_DROPLET_ENABLE: config.droplet_enable = (int32_t)value; break;
        case BUBBLE_ENGINE_PARAM_DROPLET_PROBABILITY: config.droplet_probability = value; break;
        case BUBBLE_ENGINE_PARAM_DROPLET_GAIN: config.droplet_gain = value; break;
        case BUBBLE_ENGINE_PARAM_DROPLET_LENGTH_SCALE: config.droplet_length_scale = value; break;
        case BUBBLE_ENGINE_PARAM_MEMORY_MIX: config.memory_mix = value; break;
        case BUBBLE_ENGINE_PARAM_MEMORY_PULL: config.memory_pull = value; break;
        case BUBBLE_ENGINE_PARAM_MEMORY_DARKENING: config.memory_darkening = value; break;
        case BUBBLE_ENGINE_PARAM_TONE_VARIATION: config.tone_variation = value; break;
        case BUBBLE_ENGINE_PARAM_ATTACK_BRIGHTNESS: config.attack_brightness = value; break;
        case BUBBLE_ENGINE_PARAM_SUSTAIN_DARKNESS: config.sustain_darkness = value; break;
        case BUBBLE_ENGINE_PARAM_ATTACK_RATE_JITTER: config.attack_rate_jitter = (int32_t)value; break;
        case BUBBLE_ENGINE_PARAM_ATTACK_RATE_JITTER_DEPTH: config.attack_rate_jitter_depth = value; break;
        case BUBBLE_ENGINE_PARAM_FREEZE_AMOUNT: config.freeze_amount = value; break;
        case BUBBLE_ENGINE_PARAM_FREEZE_ENABLED: config.freeze_enabled = (int32_t)value; break;
        case BUBBLE_ENGINE_PARAM_REVERSE_PROBABILITY: config.reverse_probability = value; break;
        case BUBBLE_ENGINE_PARAM_PITCH_MODE: config.pitch_mode = (int32_t)value; break;
        case BUBBLE_ENGINE_PARAM_SHIMMER_AMOUNT: config.shimmer_amount = value; break;
        case BUBBLE_ENGINE_PARAM_FINAL_LIMITER_CEILING_DB: config.final_limiter_ceiling_db = value; break;
        case BUBBLE_ENGINE_PARAM_FINAL_LIMITER_RELEASE_MS: config.final_limiter_release_ms = value; break;
        case BUBBLE_ENGINE_PARAM_MOTION_RATE: config.motion_rate = value; break;
        case BUBBLE_ENGINE_PARAM_MOTION_DEPTH: config.motion_depth = value; break;
        case BUBBLE_ENGINE_PARAM_MOTION_SHAPE: config.motion_shape = (int32_t)value; break;
        case BUBBLE_ENGINE_PARAM_QUALITY_PROFILE:
            return bubble_engine_set_quality_profile(engine, (BubbleQualityProfile)((int32_t)value));
        case BUBBLE_ENGINE_PARAM_ACTIVE_VOICE_LIMIT: config.active_voice_limit = (int32_t)value; break;
        default: return false;
    }

    if (config_changed) {
        SoundBubbles_UpdateConfig(engine, &config);
    }
    return true;
}

bool bubble_engine_get_parameter(const BubbleEngine_t* engine, BubbleEngineParameterId_t parameter, float* value) {
    if (engine == NULL || value == NULL) {
        return false;
    }

    if (parameter == BUBBLE_PARAM_DEVELOPER_MODE) {
        *value = (float)engine->developer_mode;
        return true;
    }

    int macro_index = MacroIndex(parameter);
    if (macro_index >= 0) {
        *value = engine->macro_targets[macro_index];
        return true;
    }

    if (!engine->developer_mode && IsDeveloperOnlyParameter(parameter)) {
        return false;
    }

    const BubbleEngineConfig_t* config = &engine->motion_base_config;
    switch (parameter) {
        case BUBBLE_ENGINE_PARAM_NOISE_FLOOR: *value = config->noise_floor; break;
        case BUBBLE_ENGINE_PARAM_TRACKING_THRESH: *value = config->tracking_thresh; break;
        case BUBBLE_ENGINE_PARAM_SUSTAIN_THRESH: *value = config->sustain_thresh; break;
        case BUBBLE_ENGINE_PARAM_TRANSIENT_DELTA: *value = config->transient_delta; break;
        case BUBBLE_ENGINE_PARAM_DUCK_BURST_LEVEL: *value = config->duck_burst_level; break;
        case BUBBLE_ENGINE_PARAM_DUCK_ATTACK_COEF: *value = config->duck_attack_coef; break;
        case BUBBLE_ENGINE_PARAM_DUCK_RELEASE_COEF: *value = config->duck_release_coef; break;
        case BUBBLE_ENGINE_PARAM_BURST_DURATION_TICKS: *value = (float)config->burst_duration_ticks; break;
        case BUBBLE_ENGINE_PARAM_BURST_IMMEDIATE_COUNT: *value = (float)config->burst_immediate_count; break;
        case BUBBLE_ENGINE_PARAM_DENSITY_BURST: *value = config->density_burst; break;
        case BUBBLE_ENGINE_PARAM_DENSITY_SUSTAIN: *value = config->density_sustain; break;
        case BUBBLE_ENGINE_PARAM_DENSITY_DECAY: *value = config->density_decay; break;
        case BUBBLE_ENGINE_PARAM_ATTACK_REGION_MIN_OFFSET_SAMPLES: *value = (float)config->attack_region.min_offset_samples; break;
        case BUBBLE_ENGINE_PARAM_ATTACK_REGION_MAX_OFFSET_SAMPLES: *value = (float)config->attack_region.max_offset_samples; break;
        case BUBBLE_ENGINE_PARAM_BODY_REGION_MIN_OFFSET_SAMPLES: *value = (float)config->body_region.min_offset_samples; break;
        case BUBBLE_ENGINE_PARAM_BODY_REGION_MAX_OFFSET_SAMPLES: *value = (float)config->body_region.max_offset_samples; break;
        case BUBBLE_ENGINE_PARAM_MEMORY_REGION_MIN_OFFSET_SAMPLES: *value = (float)config->memory_region.min_offset_samples; break;
        case BUBBLE_ENGINE_PARAM_MEMORY_REGION_MAX_OFFSET_SAMPLES: *value = (float)config->memory_region.max_offset_samples; break;
        case BUBBLE_ENGINE_PARAM_MICRO_DURATION_MS_MIN: *value = config->class_configs[BUBBLE_CLASS_MICRO_ATTACK].duration_ms_min; break;
        case BUBBLE_ENGINE_PARAM_MICRO_DURATION_MS_MAX: *value = config->class_configs[BUBBLE_CLASS_MICRO_ATTACK].duration_ms_max; break;
        case BUBBLE_ENGINE_PARAM_SHORT_DURATION_MS_MIN: *value = config->class_configs[BUBBLE_CLASS_SHORT_INTERMEDIATE].duration_ms_min; break;
        case BUBBLE_ENGINE_PARAM_SHORT_DURATION_MS_MAX: *value = config->class_configs[BUBBLE_CLASS_SHORT_INTERMEDIATE].duration_ms_max; break;
        case BUBBLE_ENGINE_PARAM_BODY_DURATION_MS_MIN: *value = config->class_configs[BUBBLE_CLASS_SUSTAIN_BODY].duration_ms_min; break;
        case BUBBLE_ENGINE_PARAM_BODY_DURATION_MS_MAX: *value = config->class_configs[BUBBLE_CLASS_SUSTAIN_BODY].duration_ms_max; break;
        case BUBBLE_ENGINE_PARAM_RNG_SEED: *value = (float)config->rng_seed; break;
        case BUBBLE_ENGINE_PARAM_MIX_DRY_GAIN: *value = engine->master_dry_gain; break;
        case BUBBLE_ENGINE_PARAM_MIX_WET_GAIN: *value = engine->master_wet_gain; break;
        case BUBBLE_ENGINE_PARAM_STEREO_WIDTH: *value = config->stereo_width; break;
        case BUBBLE_ENGINE_PARAM_ATTACK_PAN_SPREAD: *value = config->attack_pan_spread; break;
        case BUBBLE_ENGINE_PARAM_SUSTAIN_PAN_SPREAD: *value = config->sustain_pan_spread; break;
        case BUBBLE_ENGINE_PARAM_SMART_START_ENABLE: *value = (float)config->smart_start_enable; break;
        case BUBBLE_ENGINE_PARAM_SMART_START_RANGE: *value = (float)config->smart_start_range; break;
        case BUBBLE_ENGINE_PARAM_ENVELOPE_VARIATION: *value = config->envelope_variation; break;
        case BUBBLE_ENGINE_PARAM_ENVELOPE_FAMILY: *value = (float)config->envelope_family; break;
        case BUBBLE_ENGINE_PARAM_WET_DRIVE: *value = config->wet_drive; break;
        case BUBBLE_ENGINE_PARAM_WET_CLIP_AMOUNT: *value = config->wet_clip_amount; break;
        case BUBBLE_ENGINE_PARAM_WET_OUTPUT_TRIM: *value = config->wet_output_trim; break;
        case BUBBLE_ENGINE_PARAM_SUSTAIN_DIFFUSION_ENABLE: *value = (float)config->sustain_diffusion_enable; break;
        case BUBBLE_ENGINE_PARAM_SUSTAIN_DIFFUSION_AMOUNT: *value = config->sustain_diffusion_amount; break;
        case BUBBLE_ENGINE_PARAM_SUSTAIN_DIFFUSION_STAGES: *value = (float)config->sustain_diffusion_stages; break;
        case BUBBLE_ENGINE_PARAM_SUSTAIN_DIFFUSION_DELAY: *value = (float)config->sustain_diffusion_delay; break;
        case BUBBLE_ENGINE_PARAM_SUSTAIN_DIFFUSION_FEEDBACK: *value = config->sustain_diffusion_feedback; break;
        case BUBBLE_ENGINE_PARAM_DROPLET_ENABLE: *value = (float)config->droplet_enable; break;
        case BUBBLE_ENGINE_PARAM_DROPLET_PROBABILITY: *value = config->droplet_probability; break;
        case BUBBLE_ENGINE_PARAM_DROPLET_GAIN: *value = config->droplet_gain; break;
        case BUBBLE_ENGINE_PARAM_DROPLET_LENGTH_SCALE: *value = config->droplet_length_scale; break;
        case BUBBLE_ENGINE_PARAM_MEMORY_MIX: *value = config->memory_mix; break;
        case BUBBLE_ENGINE_PARAM_MEMORY_PULL: *value = config->memory_pull; break;
        case BUBBLE_ENGINE_PARAM_MEMORY_DARKENING: *value = config->memory_darkening; break;
        case BUBBLE_ENGINE_PARAM_TONE_VARIATION: *value = config->tone_variation; break;
        case BUBBLE_ENGINE_PARAM_ATTACK_BRIGHTNESS: *value = config->attack_brightness; break;
        case BUBBLE_ENGINE_PARAM_SUSTAIN_DARKNESS: *value = config->sustain_darkness; break;
        case BUBBLE_ENGINE_PARAM_ATTACK_RATE_JITTER: *value = (float)config->attack_rate_jitter; break;
        case BUBBLE_ENGINE_PARAM_ATTACK_RATE_JITTER_DEPTH: *value = config->attack_rate_jitter_depth; break;
        case BUBBLE_ENGINE_PARAM_FREEZE_AMOUNT: *value = config->freeze_amount; break;
        case BUBBLE_ENGINE_PARAM_FREEZE_ENABLED: *value = (float)config->freeze_enabled; break;
        case BUBBLE_ENGINE_PARAM_REVERSE_PROBABILITY: *value = config->reverse_probability; break;
        case BUBBLE_ENGINE_PARAM_PITCH_MODE: *value = (float)config->pitch_mode; break;
        case BUBBLE_ENGINE_PARAM_SHIMMER_AMOUNT: *value = config->shimmer_amount; break;
        case BUBBLE_ENGINE_PARAM_FINAL_LIMITER_CEILING_DB: *value = config->final_limiter_ceiling_db; break;
        case BUBBLE_ENGINE_PARAM_FINAL_LIMITER_RELEASE_MS: *value = config->final_limiter_release_ms; break;
        case BUBBLE_ENGINE_PARAM_MOTION_RATE: *value = config->motion_rate; break;
        case BUBBLE_ENGINE_PARAM_MOTION_DEPTH: *value = config->motion_depth; break;
        case BUBBLE_ENGINE_PARAM_MOTION_SHAPE: *value = (float)config->motion_shape; break;
        case BUBBLE_ENGINE_PARAM_QUALITY_PROFILE: *value = (float)config->quality_profile; break;
        case BUBBLE_ENGINE_PARAM_ACTIVE_VOICE_LIMIT: *value = (float)config->active_voice_limit; break;
        case BUBBLE_ENGINE_PARAM_RUNTIME_ENVELOPE: *value = engine->env_follower_state; break;
        case BUBBLE_ENGINE_PARAM_RUNTIME_STATE: *value = (float)engine->engine_state; break;
        case BUBBLE_ENGINE_PARAM_RUNTIME_ACTIVE_VOICES: *value = (float)CountActiveVoices(engine); break;
        case BUBBLE_ENGINE_PARAM_RUNTIME_PEAK_L: *value = engine->metrics_last_block.peak_l; break;
        case BUBBLE_ENGINE_PARAM_RUNTIME_PEAK_R: *value = engine->metrics_last_block.peak_r; break;
        case BUBBLE_ENGINE_PARAM_RUNTIME_CLIP_COUNT: *value = (float)engine->metrics_last_block.clip_count; break;
        case BUBBLE_ENGINE_PARAM_RUNTIME_LIMITER_GAIN: *value = engine->metrics_last_block.limiter_gain; break;
        default: return false;
    }

    return true;
}

bool bubble_engine_load_preset(BubbleEngine_t* engine, const BubbleEnginePreset_t* preset) {
    if (engine == NULL || preset == NULL) {
        return false;
    }

    SoundBubbles_UpdateConfig(engine, &preset->config);
    engine->master_dry_gain = preset->master_dry_gain;
    engine->master_wet_gain = preset->master_wet_gain;
    for (int i = 0; i < BUBBLES_MACRO_COUNT; i++) {
        engine->macro_values[i] = preset->macro_values[i];
        engine->macro_targets[i] = preset->macro_targets[i];
    }
    engine->macro_dirty_mask = preset->macro_dirty_mask;
    engine->developer_mode = preset->developer_mode;
    return true;
}

bool bubble_engine_save_preset(const BubbleEngine_t* engine, BubbleEnginePreset_t* preset) {
    if (engine == NULL || preset == NULL) {
        return false;
    }

    preset->config = engine->motion_base_config;
    preset->master_dry_gain = engine->master_dry_gain;
    preset->master_wet_gain = engine->master_wet_gain;
    for (int i = 0; i < BUBBLES_MACRO_COUNT; i++) {
        preset->macro_values[i] = engine->macro_values[i];
        preset->macro_targets[i] = engine->macro_targets[i];
    }
    preset->macro_dirty_mask = engine->macro_dirty_mask;
    preset->developer_mode = engine->developer_mode;
    return true;
}

const BubbleQualityProfileLimits_t* bubble_engine_get_quality_profile_limits(BubbleQualityProfile profile) {
    for (int i = 0; i < BUBBLE_QUALITY_PROFILE_COUNT; i++) {
        if (BUBBLE_QUALITY_PROFILE_LIMITS[i].profile == profile) {
            return &BUBBLE_QUALITY_PROFILE_LIMITS[i];
        }
    }
    return NULL;
}

bool bubble_engine_set_quality_profile(BubbleEngine_t* engine, BubbleQualityProfile profile) {
    if (engine == NULL) {
        return false;
    }

    const BubbleQualityProfileLimits_t* limits = bubble_engine_get_quality_profile_limits(profile);
    if (limits == NULL) {
        return false;
    }

    BubbleEngineConfig_t config = engine->motion_base_config;
    config.quality_profile = profile;
    config.active_voice_limit = limits->voice_limit;
    SoundBubbles_UpdateConfig(engine, &config);
    return true;
}

void bubble_engine_set_metrics_callback(BubbleEngine_t* engine, BubbleEngineMetricsCallback_t callback, void* user_data) {
    if (engine == NULL) {
        return;
    }

    SoundBubbles_SetMetricsCallback(engine, callback, user_data);
}
