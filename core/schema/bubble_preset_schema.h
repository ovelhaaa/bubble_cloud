#ifndef BUBBLE_PRESET_SCHEMA_H
#define BUBBLE_PRESET_SCHEMA_H

#include <stdbool.h>
#include <stddef.h>
#include "../engine/bubble_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BUBBLE_PRESET_SCHEMA_VERSION 3
#define BUBBLE_PRESET_ENGINE_VERSION "post-diffusion-ui"

typedef enum {
    BUBBLE_PRESET_PARAM_FLOAT = 0,
    BUBBLE_PRESET_PARAM_INT = 1,
    BUBBLE_PRESET_PARAM_BOOL = 2,
    BUBBLE_PRESET_PARAM_ENUM = 3
} BubblePresetParamType_t;

typedef struct {
    BubbleEngineParameterId_t id;
    const char* canonical_name;
    float min_value;
    float max_value;
    float default_value;
    BubblePresetParamType_t type;
} BubblePresetParamSpec_t;

static const BubblePresetParamSpec_t BUBBLE_PRESET_PARAM_SPECS[] = {
    { BUBBLE_ENGINE_PARAM_NOISE_FLOOR, "noise_floor", 0.0f, 1.0f, 0.001f, BUBBLE_PRESET_PARAM_FLOAT },
    { BUBBLE_ENGINE_PARAM_TRACKING_THRESH, "tracking_thresh", 0.0f, 1.0f, 0.01f, BUBBLE_PRESET_PARAM_FLOAT },
    { BUBBLE_ENGINE_PARAM_SUSTAIN_THRESH, "sustain_thresh", 0.0f, 1.0f, 0.05f, BUBBLE_PRESET_PARAM_FLOAT },
    { BUBBLE_ENGINE_PARAM_TRANSIENT_DELTA, "transient_delta", 0.0f, 1.0f, 0.05f, BUBBLE_PRESET_PARAM_FLOAT },
    { BUBBLE_ENGINE_PARAM_DUCK_BURST_LEVEL, "duck_burst_level", 0.0f, 1.0f, 0.2f, BUBBLE_PRESET_PARAM_FLOAT },
    { BUBBLE_ENGINE_PARAM_DUCK_ATTACK_COEF, "duck_attack_coef", 0.0f, 1.0f, 0.80f, BUBBLE_PRESET_PARAM_FLOAT },
    { BUBBLE_ENGINE_PARAM_DUCK_RELEASE_COEF, "duck_release_coef", 0.0f, 1.0f, 0.99f, BUBBLE_PRESET_PARAM_FLOAT },
    { BUBBLE_ENGINE_PARAM_BURST_DURATION_TICKS, "burst_duration_ticks", 0.0f, 1000.0f, 10.0f, BUBBLE_PRESET_PARAM_INT },
    { BUBBLE_ENGINE_PARAM_BURST_IMMEDIATE_COUNT, "burst_immediate_count", 0.0f, 128.0f, 3.0f, BUBBLE_PRESET_PARAM_INT },
    { BUBBLE_ENGINE_PARAM_DENSITY_BURST, "density_burst", 0.0f, 500.0f, 50.0f, BUBBLE_PRESET_PARAM_FLOAT },
    { BUBBLE_ENGINE_PARAM_DENSITY_SUSTAIN, "density_sustain", 0.0f, 500.0f, 15.0f, BUBBLE_PRESET_PARAM_FLOAT },
    { BUBBLE_ENGINE_PARAM_DENSITY_DECAY, "density_decay", 0.0f, 500.0f, 5.0f, BUBBLE_PRESET_PARAM_FLOAT },
    { BUBBLE_ENGINE_PARAM_ATTACK_REGION_MIN_OFFSET_SAMPLES, "attack_region_min_offset_samples", 0.0f, 30000.0f, 441.0f, BUBBLE_PRESET_PARAM_INT },
    { BUBBLE_ENGINE_PARAM_ATTACK_REGION_MAX_OFFSET_SAMPLES, "attack_region_max_offset_samples", 64.0f, 60000.0f, 3528.0f, BUBBLE_PRESET_PARAM_INT },
    { BUBBLE_ENGINE_PARAM_BODY_REGION_MIN_OFFSET_SAMPLES, "body_region_min_offset_samples", 128.0f, 90000.0f, 3528.0f, BUBBLE_PRESET_PARAM_INT },
    { BUBBLE_ENGINE_PARAM_BODY_REGION_MAX_OFFSET_SAMPLES, "body_region_max_offset_samples", 512.0f, 120000.0f, 11025.0f, BUBBLE_PRESET_PARAM_INT },
    { BUBBLE_ENGINE_PARAM_MEMORY_REGION_MIN_OFFSET_SAMPLES, "memory_region_min_offset_samples", 512.0f, 150000.0f, 11025.0f, BUBBLE_PRESET_PARAM_INT },
    { BUBBLE_ENGINE_PARAM_MEMORY_REGION_MAX_OFFSET_SAMPLES, "memory_region_max_offset_samples", 1024.0f, 220500.0f, 39690.0f, BUBBLE_PRESET_PARAM_INT },
    { BUBBLE_ENGINE_PARAM_MICRO_DURATION_MS_MIN, "micro_duration_ms_min", 1.0f, 1000.0f, 5.0f, BUBBLE_PRESET_PARAM_FLOAT },
    { BUBBLE_ENGINE_PARAM_MICRO_DURATION_MS_MAX, "micro_duration_ms_max", 1.0f, 1000.0f, 15.0f, BUBBLE_PRESET_PARAM_FLOAT },
    { BUBBLE_ENGINE_PARAM_SHORT_DURATION_MS_MIN, "short_duration_ms_min", 1.0f, 2000.0f, 20.0f, BUBBLE_PRESET_PARAM_FLOAT },
    { BUBBLE_ENGINE_PARAM_SHORT_DURATION_MS_MAX, "short_duration_ms_max", 1.0f, 2000.0f, 50.0f, BUBBLE_PRESET_PARAM_FLOAT },
    { BUBBLE_ENGINE_PARAM_BODY_DURATION_MS_MIN, "body_duration_ms_min", 1.0f, 5000.0f, 80.0f, BUBBLE_PRESET_PARAM_FLOAT },
    { BUBBLE_ENGINE_PARAM_BODY_DURATION_MS_MAX, "body_duration_ms_max", 1.0f, 5000.0f, 200.0f, BUBBLE_PRESET_PARAM_FLOAT },
    { BUBBLE_ENGINE_PARAM_RNG_SEED, "rng_seed", 1.0f, 2147483520.0f, 1.0f, BUBBLE_PRESET_PARAM_INT },
    { BUBBLE_ENGINE_PARAM_MIX_DRY_GAIN, "mix_dry_gain", 0.0f, 2.0f, 1.0f, BUBBLE_PRESET_PARAM_FLOAT },
    { BUBBLE_ENGINE_PARAM_MIX_WET_GAIN, "mix_wet_gain", 0.0f, 2.0f, 1.0f, BUBBLE_PRESET_PARAM_FLOAT },
    { BUBBLE_ENGINE_PARAM_STEREO_WIDTH, "stereo_width", 0.0f, 1.0f, 0.7f, BUBBLE_PRESET_PARAM_FLOAT },
    { BUBBLE_ENGINE_PARAM_ATTACK_PAN_SPREAD, "attack_pan_spread", 0.0f, 1.0f, 0.85f, BUBBLE_PRESET_PARAM_FLOAT },
    { BUBBLE_ENGINE_PARAM_SUSTAIN_PAN_SPREAD, "sustain_pan_spread", 0.0f, 1.0f, 0.45f, BUBBLE_PRESET_PARAM_FLOAT },
    { BUBBLE_ENGINE_PARAM_SMART_START_ENABLE, "smart_start_enable", 0.0f, 1.0f, 1.0f, BUBBLE_PRESET_PARAM_BOOL },
    { BUBBLE_ENGINE_PARAM_SMART_START_RANGE, "smart_start_range", 0.0f, 256.0f, 12.0f, BUBBLE_PRESET_PARAM_INT },
    { BUBBLE_ENGINE_PARAM_ENVELOPE_VARIATION, "envelope_variation", 0.0f, 1.0f, 0.35f, BUBBLE_PRESET_PARAM_FLOAT },
    { BUBBLE_ENGINE_PARAM_ENVELOPE_FAMILY, "envelope_family", 0.0f, 1.0f, 0.0f, BUBBLE_PRESET_PARAM_ENUM },
    { BUBBLE_ENGINE_PARAM_WET_DRIVE, "wet_drive", 0.0f, 4.0f, 1.0f, BUBBLE_PRESET_PARAM_FLOAT },
    { BUBBLE_ENGINE_PARAM_WET_CLIP_AMOUNT, "wet_clip_amount", 0.0f, 1.0f, 0.2f, BUBBLE_PRESET_PARAM_FLOAT },
    { BUBBLE_ENGINE_PARAM_WET_OUTPUT_TRIM, "wet_output_trim", 0.0f, 2.0f, 1.0f, BUBBLE_PRESET_PARAM_FLOAT },
    { BUBBLE_ENGINE_PARAM_SUSTAIN_DIFFUSION_ENABLE, "sustain_diffusion_enable", 0.0f, 1.0f, 0.0f, BUBBLE_PRESET_PARAM_BOOL },
    { BUBBLE_ENGINE_PARAM_SUSTAIN_DIFFUSION_AMOUNT, "sustain_diffusion_amount", 0.0f, 1.0f, 0.35f, BUBBLE_PRESET_PARAM_FLOAT },
    { BUBBLE_ENGINE_PARAM_SUSTAIN_DIFFUSION_STAGES, "sustain_diffusion_stages", 0.0f, 2.0f, 1.0f, BUBBLE_PRESET_PARAM_INT },
    { BUBBLE_ENGINE_PARAM_SUSTAIN_DIFFUSION_DELAY, "sustain_diffusion_delay", 1.0f, (float)BUBBLES_SUSTAIN_DIFFUSION_MAX_DELAY, 18.0f, BUBBLE_PRESET_PARAM_INT },
    { BUBBLE_ENGINE_PARAM_SUSTAIN_DIFFUSION_FEEDBACK, "sustain_diffusion_feedback", 0.0f, 0.95f, 0.45f, BUBBLE_PRESET_PARAM_FLOAT },
    { BUBBLE_ENGINE_PARAM_DROPLET_ENABLE, "droplet_enable", 0.0f, 1.0f, 0.0f, BUBBLE_PRESET_PARAM_BOOL },
    { BUBBLE_ENGINE_PARAM_DROPLET_PROBABILITY, "droplet_probability", 0.0f, 1.0f, 0.12f, BUBBLE_PRESET_PARAM_FLOAT },
    { BUBBLE_ENGINE_PARAM_DROPLET_GAIN, "droplet_gain", 0.0f, 2.0f, 0.5f, BUBBLE_PRESET_PARAM_FLOAT },
    { BUBBLE_ENGINE_PARAM_DROPLET_LENGTH_SCALE, "droplet_length_scale", 0.05f, 4.0f, 0.6f, BUBBLE_PRESET_PARAM_FLOAT },
    { BUBBLE_ENGINE_PARAM_MEMORY_MIX, "memory_mix", 0.0f, 1.0f, 0.35f, BUBBLE_PRESET_PARAM_FLOAT },
    { BUBBLE_ENGINE_PARAM_MEMORY_PULL, "memory_pull", 0.0f, 1.0f, 0.25f, BUBBLE_PRESET_PARAM_FLOAT },
    { BUBBLE_ENGINE_PARAM_MEMORY_DARKENING, "memory_darkening", 0.0f, 1.0f, 0.2f, BUBBLE_PRESET_PARAM_FLOAT },
    { BUBBLE_ENGINE_PARAM_TONE_VARIATION, "tone_variation", 0.0f, 1.0f, 0.4f, BUBBLE_PRESET_PARAM_FLOAT },
    { BUBBLE_ENGINE_PARAM_ATTACK_BRIGHTNESS, "attack_brightness", 0.0f, 4.0f, 1.15f, BUBBLE_PRESET_PARAM_FLOAT },
    { BUBBLE_ENGINE_PARAM_SUSTAIN_DARKNESS, "sustain_darkness", 0.0f, 1.0f, 0.25f, BUBBLE_PRESET_PARAM_FLOAT },
    { BUBBLE_ENGINE_PARAM_ATTACK_RATE_JITTER, "attack_rate_jitter", 0.0f, 1.0f, 0.0f, BUBBLE_PRESET_PARAM_BOOL },
    { BUBBLE_ENGINE_PARAM_ATTACK_RATE_JITTER_DEPTH, "attack_rate_jitter_depth", 0.0f, 1.0f, 0.02f, BUBBLE_PRESET_PARAM_FLOAT },
    { BUBBLE_ENGINE_PARAM_QUALITY_PROFILE, "quality_profile", 0.0f, (float)(BUBBLE_QUALITY_PROFILE_COUNT - 1), (float)BUBBLE_QUALITY_PROFILE_WEB_STANDARD, BUBBLE_PRESET_PARAM_ENUM },
    { BUBBLE_ENGINE_PARAM_ACTIVE_VOICE_LIMIT, "active_voice_limit", 1.0f, (float)BUBBLE_ENGINE_MAX_VOICES, (float)BUBBLE_QUALITY_DEFAULT_VOICE_LIMIT, BUBBLE_PRESET_PARAM_INT },
    { BUBBLE_ENGINE_PARAM_FREEZE_AMOUNT, "freeze_amount", 0.0f, 1.0f, 0.0f, BUBBLE_PRESET_PARAM_FLOAT },
    { BUBBLE_ENGINE_PARAM_FREEZE_ENABLED, "freeze_enabled", 0.0f, 1.0f, 0.0f, BUBBLE_PRESET_PARAM_BOOL },
    { BUBBLE_ENGINE_PARAM_REVERSE_PROBABILITY, "reverse_probability", 0.0f, 1.0f, 0.0f, BUBBLE_PRESET_PARAM_FLOAT },
    { BUBBLE_ENGINE_PARAM_PITCH_MODE, "pitch_mode", 0.0f, 4.0f, 0.0f, BUBBLE_PRESET_PARAM_ENUM },
    { BUBBLE_ENGINE_PARAM_SHIMMER_AMOUNT, "shimmer_amount", 0.0f, 1.0f, 0.0f, BUBBLE_PRESET_PARAM_FLOAT },
    { BUBBLE_ENGINE_PARAM_FINAL_LIMITER_CEILING_DB, "final_limiter_ceiling_db", -24.0f, 0.0f, -1.0f, BUBBLE_PRESET_PARAM_FLOAT },
    { BUBBLE_ENGINE_PARAM_FINAL_LIMITER_RELEASE_MS, "final_limiter_release_ms", 5.0f, 500.0f, 50.0f, BUBBLE_PRESET_PARAM_FLOAT },
    { BUBBLE_ENGINE_PARAM_MOTION_RATE, "motion_rate", 0.0f, 1.0f, 0.18f, BUBBLE_PRESET_PARAM_FLOAT },
    { BUBBLE_ENGINE_PARAM_MOTION_DEPTH, "motion_depth", 0.0f, 1.0f, 0.0f, BUBBLE_PRESET_PARAM_FLOAT },
    { BUBBLE_ENGINE_PARAM_MOTION_SHAPE, "motion_shape", 0.0f, 2.0f, 0.0f, BUBBLE_PRESET_PARAM_ENUM }
};

#define BUBBLE_PRESET_PARAM_COUNT (sizeof(BUBBLE_PRESET_PARAM_SPECS) / sizeof(BUBBLE_PRESET_PARAM_SPECS[0]))

static inline const BubblePresetParamSpec_t* bubble_preset_find_param_by_name(const char* canonical_name) {
    if (canonical_name == NULL) return NULL;
    for (size_t i = 0; i < BUBBLE_PRESET_PARAM_COUNT; ++i) {
        const char* a = BUBBLE_PRESET_PARAM_SPECS[i].canonical_name;
        const char* b = canonical_name;
        while (*a != '\0' && *b != '\0' && *a == *b) { ++a; ++b; }
        if (*a == '\0' && *b == '\0') return &BUBBLE_PRESET_PARAM_SPECS[i];
    }
    return NULL;
}

static inline const BubblePresetParamSpec_t* bubble_preset_find_param_by_id(BubbleEngineParameterId_t id) {
    for (size_t i = 0; i < BUBBLE_PRESET_PARAM_COUNT; ++i) {
        if (BUBBLE_PRESET_PARAM_SPECS[i].id == id) return &BUBBLE_PRESET_PARAM_SPECS[i];
    }
    return NULL;
}

static inline bool bubble_preset_validate_param_value(const BubblePresetParamSpec_t* spec, float value) {
    return spec != NULL && value >= spec->min_value && value <= spec->max_value;
}

#ifdef __cplusplus
}
#endif

#endif // BUBBLE_PRESET_SCHEMA_H
