#define SOUND_BUBBLES_DSP_INTERNAL 1
#include "bubble_engine.h"

#include <stddef.h>
#include <string.h>

static int32_t CountActiveVoices(const BubbleEngine_t* engine) {
    int32_t count = 0;
    for (int i = 0; i < BUBBLES_MAX_VOICES; i++) {
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
    if (initial_config == NULL) {
        bubble_engine_default_config(&default_config);
        initial_config = &default_config;
    }
    SoundBubbles_Init(engine, delay_buffer_memory, initial_config);
}

void bubble_engine_reset(BubbleEngine_t* engine) {
    if (engine == NULL || engine->delay_buffer == NULL) {
        return;
    }

    BubbleEnginePreset_t preset;
    BubbleEngineMetricsCallback_t metrics_callback = engine->metrics_callback;
    void* metrics_user_data = engine->metrics_user_data;
    int16_t* delay_buffer = engine->delay_buffer;
    if (!bubble_engine_save_preset(engine, &preset)) {
        return;
    }

    SoundBubbles_Init(engine, delay_buffer, &preset.config);
    engine->master_dry_gain = preset.master_dry_gain;
    engine->master_wet_gain = preset.master_wet_gain;
    bubble_engine_set_metrics_callback(engine, metrics_callback, metrics_user_data);
}

void bubble_engine_process(BubbleEngine_t* engine, const float* in_mono, float* out_left, float* out_right, int num_samples) {
    if (engine == NULL || engine->delay_buffer == NULL || in_mono == NULL || out_left == NULL || out_right == NULL || num_samples <= 0) {
        return;
    }

    SoundBubbles_ProcessBlock(engine, in_mono, out_left, out_right, num_samples);
}

bool bubble_engine_set_parameter(BubbleEngine_t* engine, BubbleEngineParameterId_t parameter, float value) {
    if (engine == NULL) {
        return false;
    }

    BubbleEngineConfig_t config = engine->config;
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

    const BubbleEngineConfig_t* config = &engine->config;
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
        case BUBBLE_ENGINE_PARAM_RUNTIME_ENVELOPE: *value = engine->env_follower_state; break;
        case BUBBLE_ENGINE_PARAM_RUNTIME_STATE: *value = (float)engine->engine_state; break;
        case BUBBLE_ENGINE_PARAM_RUNTIME_ACTIVE_VOICES: *value = (float)CountActiveVoices(engine); break;
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
    return true;
}

bool bubble_engine_save_preset(const BubbleEngine_t* engine, BubbleEnginePreset_t* preset) {
    if (engine == NULL || preset == NULL) {
        return false;
    }

    preset->config = engine->config;
    preset->master_dry_gain = engine->master_dry_gain;
    preset->master_wet_gain = engine->master_wet_gain;
    return true;
}

void bubble_engine_set_metrics_callback(BubbleEngine_t* engine, BubbleEngineMetricsCallback_t callback, void* user_data) {
    if (engine == NULL) {
        return;
    }

    SoundBubbles_SetMetricsCallback(engine, callback, user_data);
}
