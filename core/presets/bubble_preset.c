#include "bubble_preset.h"
#include "../schema/bubble_preset_schema.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void SetError(char* error, size_t error_size, const char* message) {
    if (error != NULL && error_size > 0) {
        snprintf(error, error_size, "%s", message);
    }
}

static const char* SkipSpace(const char* p) {
    while (p != NULL && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
    return p;
}

static const char* FindJsonKeyInRange(const char* begin, const char* end, const char* key) {
    if (begin == NULL || key == NULL) return NULL;
    if (end == NULL) end = begin + strlen(begin);

    const size_t key_len = strlen(key);
    bool in_string = false;
    bool escaped = false;
    for (const char* p = begin; p < end; ++p) {
        const char c = *p;
        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                in_string = false;
            }
            continue;
        }
        if (c != '"') continue;

        const char* name = p + 1;
        if ((size_t)(end - name) < key_len + 1) continue;
        if (strncmp(name, key, key_len) != 0 || name[key_len] != '"') continue;

        const char* after = SkipSpace(name + key_len + 1);
        if (after < end && *after == ':') return after + 1;
        p = name + key_len;
    }
    return NULL;
}

static bool FindJsonObjectRange(const char* json, const char* key, const char** out_begin, const char** out_end) {
    const char* pos = FindJsonKeyInRange(json, NULL, key);
    if (pos == NULL) return false;
    pos = SkipSpace(pos);
    if (*pos != '{') return false;

    const char* begin = pos + 1;
    int depth = 1;
    bool in_string = false;
    bool escaped = false;
    for (++pos; *pos != '\0'; ++pos) {
        const char c = *pos;
        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                in_string = false;
            }
            continue;
        }
        if (c == '"') {
            in_string = true;
        } else if (c == '{') {
            depth++;
        } else if (c == '}') {
            depth--;
            if (depth == 0) {
                *out_begin = begin;
                *out_end = pos;
                return true;
            }
        }
    }
    return false;
}

static bool ParseJsonNumberAt(const char* pos, float* out_value) {
    if (pos == NULL || out_value == NULL) return false;
    pos = SkipSpace(pos);
    char* end = NULL;
    float value = strtof(pos, &end);
    if (end == pos || !isfinite(value)) return false;
    *out_value = value;
    return true;
}

static bool ReadParamNumber(const char* json, const char* params_begin, const char* params_end, const char* key, float* out_value) {
    const char* pos = NULL;
    if (params_begin != NULL && params_end != NULL) {
        pos = FindJsonKeyInRange(params_begin, params_end, key);
    } else {
        pos = FindJsonKeyInRange(json, NULL, key);
    }
    return ParseJsonNumberAt(pos, out_value);
}

static bool FloatToInt32(float value, int32_t* out_value) {
    if (out_value == NULL || !isfinite(value)) return false;
    if (value < -2147483648.0f || value > 2147483520.0f) return false;
    *out_value = (int32_t)value;
    return true;
}

static bool NormalizeTypedValue(const BubblePresetParamSpec_t* spec, float* value) {
    if (spec == NULL || value == NULL || !isfinite(*value)) return false;
    if (spec->type == BUBBLE_PRESET_PARAM_INT || spec->type == BUBBLE_PRESET_PARAM_BOOL || spec->type == BUBBLE_PRESET_PARAM_ENUM) {
        int32_t int_value = 0;
        if (!FloatToInt32(*value, &int_value)) return false;
        *value = (float)int_value;
    }
    return true;
}

static bool ValidateRegionOffset(BubbleEngineParameterId_t id, int32_t value, char* error, size_t error_size, const char* label) {
    const BubblePresetParamSpec_t* spec = bubble_preset_find_param_by_id(id);
    if (!bubble_preset_validate_param_value(spec, (float)value)) {
        char message[192];
        snprintf(message, sizeof(message), "Preset validation failed: %s is outside %.9g..%.9g.", label, spec != NULL ? spec->min_value : 0.0f, spec != NULL ? spec->max_value : 0.0f);
        SetError(error, error_size, message);
        return false;
    }
    return true;
}

static bool CheckedAddInt32(int32_t a, int32_t b, int32_t* out_value) {
    if (out_value == NULL) return false;
    if ((b > 0 && a > INT32_MAX - b) || (b < 0 && a < INT32_MIN - b)) return false;
    *out_value = a + b;
    return true;
}

static bool ReadLegacyInt(const char* json, const char* key, int32_t* out_value, char* error, size_t error_size) {
    float value = 0.0f;
    if (!ReadParamNumber(json, NULL, NULL, key, &value)) return true;
    if (!FloatToInt32(value, out_value)) {
        char message[160];
        snprintf(message, sizeof(message), "Preset validation failed: legacy parameter %s is outside int32 range.", key);
        SetError(error, error_size, message);
        return false;
    }
    return true;
}

static bool SetPresetParam(BubbleEnginePreset_t* preset, BubbleEngineParameterId_t id, float value) {
    BubbleEngineConfig_t* config = &preset->config;
    switch (id) {
        case BUBBLE_ENGINE_PARAM_NOISE_FLOOR: config->noise_floor = value; return true;
        case BUBBLE_ENGINE_PARAM_TRACKING_THRESH: config->tracking_thresh = value; return true;
        case BUBBLE_ENGINE_PARAM_SUSTAIN_THRESH: config->sustain_thresh = value; return true;
        case BUBBLE_ENGINE_PARAM_TRANSIENT_DELTA: config->transient_delta = value; return true;
        case BUBBLE_ENGINE_PARAM_DUCK_BURST_LEVEL: config->duck_burst_level = value; return true;
        case BUBBLE_ENGINE_PARAM_DUCK_ATTACK_COEF: config->duck_attack_coef = value; return true;
        case BUBBLE_ENGINE_PARAM_DUCK_RELEASE_COEF: config->duck_release_coef = value; return true;
        case BUBBLE_ENGINE_PARAM_BURST_DURATION_TICKS: config->burst_duration_ticks = (int32_t)value; return true;
        case BUBBLE_ENGINE_PARAM_BURST_IMMEDIATE_COUNT: config->burst_immediate_count = (int32_t)value; return true;
        case BUBBLE_ENGINE_PARAM_DENSITY_BURST: config->density_burst = value; return true;
        case BUBBLE_ENGINE_PARAM_DENSITY_SUSTAIN: config->density_sustain = value; return true;
        case BUBBLE_ENGINE_PARAM_DENSITY_DECAY: config->density_decay = value; return true;
        case BUBBLE_ENGINE_PARAM_ATTACK_REGION_MIN_OFFSET_SAMPLES: config->attack_region.min_offset_samples = (int32_t)value; return true;
        case BUBBLE_ENGINE_PARAM_ATTACK_REGION_MAX_OFFSET_SAMPLES: config->attack_region.max_offset_samples = (int32_t)value; return true;
        case BUBBLE_ENGINE_PARAM_BODY_REGION_MIN_OFFSET_SAMPLES: config->body_region.min_offset_samples = (int32_t)value; return true;
        case BUBBLE_ENGINE_PARAM_BODY_REGION_MAX_OFFSET_SAMPLES: config->body_region.max_offset_samples = (int32_t)value; return true;
        case BUBBLE_ENGINE_PARAM_MEMORY_REGION_MIN_OFFSET_SAMPLES: config->memory_region.min_offset_samples = (int32_t)value; return true;
        case BUBBLE_ENGINE_PARAM_MEMORY_REGION_MAX_OFFSET_SAMPLES: config->memory_region.max_offset_samples = (int32_t)value; return true;
        case BUBBLE_ENGINE_PARAM_MICRO_DURATION_MS_MIN: config->class_configs[BUBBLE_CLASS_MICRO_ATTACK].duration_ms_min = value; return true;
        case BUBBLE_ENGINE_PARAM_MICRO_DURATION_MS_MAX: config->class_configs[BUBBLE_CLASS_MICRO_ATTACK].duration_ms_max = value; return true;
        case BUBBLE_ENGINE_PARAM_SHORT_DURATION_MS_MIN: config->class_configs[BUBBLE_CLASS_SHORT_INTERMEDIATE].duration_ms_min = value; return true;
        case BUBBLE_ENGINE_PARAM_SHORT_DURATION_MS_MAX: config->class_configs[BUBBLE_CLASS_SHORT_INTERMEDIATE].duration_ms_max = value; return true;
        case BUBBLE_ENGINE_PARAM_BODY_DURATION_MS_MIN: config->class_configs[BUBBLE_CLASS_SUSTAIN_BODY].duration_ms_min = value; return true;
        case BUBBLE_ENGINE_PARAM_BODY_DURATION_MS_MAX: config->class_configs[BUBBLE_CLASS_SUSTAIN_BODY].duration_ms_max = value; return true;
        case BUBBLE_ENGINE_PARAM_RNG_SEED: config->rng_seed = (uint32_t)value; return true;
        case BUBBLE_ENGINE_PARAM_MIX_DRY_GAIN: preset->master_dry_gain = value; return true;
        case BUBBLE_ENGINE_PARAM_MIX_WET_GAIN: preset->master_wet_gain = value; return true;
        case BUBBLE_ENGINE_PARAM_STEREO_WIDTH: config->stereo_width = value; return true;
        case BUBBLE_ENGINE_PARAM_ATTACK_PAN_SPREAD: config->attack_pan_spread = value; return true;
        case BUBBLE_ENGINE_PARAM_SUSTAIN_PAN_SPREAD: config->sustain_pan_spread = value; return true;
        case BUBBLE_ENGINE_PARAM_SMART_START_ENABLE: config->smart_start_enable = (int32_t)value; return true;
        case BUBBLE_ENGINE_PARAM_SMART_START_RANGE: config->smart_start_range = (int32_t)value; return true;
        case BUBBLE_ENGINE_PARAM_ENVELOPE_VARIATION: config->envelope_variation = value; return true;
        case BUBBLE_ENGINE_PARAM_ENVELOPE_FAMILY: config->envelope_family = (int32_t)value; return true;
        case BUBBLE_ENGINE_PARAM_WET_DRIVE: config->wet_drive = value; return true;
        case BUBBLE_ENGINE_PARAM_WET_CLIP_AMOUNT: config->wet_clip_amount = value; return true;
        case BUBBLE_ENGINE_PARAM_WET_OUTPUT_TRIM: config->wet_output_trim = value; return true;
        case BUBBLE_ENGINE_PARAM_SUSTAIN_DIFFUSION_ENABLE: config->sustain_diffusion_enable = (int32_t)value; return true;
        case BUBBLE_ENGINE_PARAM_SUSTAIN_DIFFUSION_AMOUNT: config->sustain_diffusion_amount = value; return true;
        case BUBBLE_ENGINE_PARAM_SUSTAIN_DIFFUSION_STAGES: config->sustain_diffusion_stages = (int32_t)value; return true;
        case BUBBLE_ENGINE_PARAM_SUSTAIN_DIFFUSION_DELAY: config->sustain_diffusion_delay = (int32_t)value; return true;
        case BUBBLE_ENGINE_PARAM_SUSTAIN_DIFFUSION_FEEDBACK: config->sustain_diffusion_feedback = value; return true;
        case BUBBLE_ENGINE_PARAM_DROPLET_ENABLE: config->droplet_enable = (int32_t)value; return true;
        case BUBBLE_ENGINE_PARAM_DROPLET_PROBABILITY: config->droplet_probability = value; return true;
        case BUBBLE_ENGINE_PARAM_DROPLET_GAIN: config->droplet_gain = value; return true;
        case BUBBLE_ENGINE_PARAM_DROPLET_LENGTH_SCALE: config->droplet_length_scale = value; return true;
        case BUBBLE_ENGINE_PARAM_MEMORY_MIX: config->memory_mix = value; return true;
        case BUBBLE_ENGINE_PARAM_MEMORY_PULL: config->memory_pull = value; return true;
        case BUBBLE_ENGINE_PARAM_MEMORY_DARKENING: config->memory_darkening = value; return true;
        case BUBBLE_ENGINE_PARAM_TONE_VARIATION: config->tone_variation = value; return true;
        case BUBBLE_ENGINE_PARAM_ATTACK_BRIGHTNESS: config->attack_brightness = value; return true;
        case BUBBLE_ENGINE_PARAM_SUSTAIN_DARKNESS: config->sustain_darkness = value; return true;
        case BUBBLE_ENGINE_PARAM_ATTACK_RATE_JITTER: config->attack_rate_jitter = (int32_t)value; return true;
        case BUBBLE_ENGINE_PARAM_ATTACK_RATE_JITTER_DEPTH: config->attack_rate_jitter_depth = value; return true;
        case BUBBLE_ENGINE_PARAM_QUALITY_PROFILE: config->quality_profile = (BubbleQualityProfile)((int32_t)value); return true;
        case BUBBLE_ENGINE_PARAM_ACTIVE_VOICE_LIMIT: config->active_voice_limit = (int32_t)value; return true;
        case BUBBLE_ENGINE_PARAM_FREEZE_AMOUNT: config->freeze_amount = value; return true;
        case BUBBLE_ENGINE_PARAM_FREEZE_ENABLED: config->freeze_enabled = (int32_t)value; return true;
        case BUBBLE_ENGINE_PARAM_REVERSE_PROBABILITY: config->reverse_probability = value; return true;
        case BUBBLE_ENGINE_PARAM_PITCH_MODE: config->pitch_mode = (int32_t)value; return true;
        case BUBBLE_ENGINE_PARAM_SHIMMER_AMOUNT: config->shimmer_amount = value; return true;
        case BUBBLE_ENGINE_PARAM_FINAL_LIMITER_CEILING_DB: config->final_limiter_ceiling_db = value; return true;
        case BUBBLE_ENGINE_PARAM_FINAL_LIMITER_RELEASE_MS: config->final_limiter_release_ms = value; return true;
        case BUBBLE_ENGINE_PARAM_MOTION_RATE: config->motion_rate = value; return true;
        case BUBBLE_ENGINE_PARAM_MOTION_DEPTH: config->motion_depth = value; return true;
        case BUBBLE_ENGINE_PARAM_MOTION_SHAPE: config->motion_shape = (BubbleMotionShape_t)((int32_t)value); return true;
        case BUBBLE_ENGINE_PARAM_TEMPO_BPM: config->tempo_bpm = value; return true;
        case BUBBLE_ENGINE_PARAM_TEMPO_SYNC_ENABLED: config->tempo_sync_enabled = (int32_t)value; return true;
        case BUBBLE_ENGINE_PARAM_RHYTHM_DIVISION: config->rhythm_division = (BubbleRhythmDivision_t)((int32_t)value); return true;
        case BUBBLE_ENGINE_PARAM_BURST_MODE: config->burst_mode = (BubbleBurstMode_t)((int32_t)value); return true;
        case BUBBLE_ENGINE_PARAM_RHYTHM_PATTERN: config->rhythm_pattern = (uint32_t)value; return true;
        default: return false;
    }
}

static bool GetPresetParam(const BubbleEnginePreset_t* preset, BubbleEngineParameterId_t id, float* value) {
    const BubbleEngineConfig_t* config = &preset->config;
    switch (id) {
        case BUBBLE_ENGINE_PARAM_NOISE_FLOOR: *value = config->noise_floor; return true;
        case BUBBLE_ENGINE_PARAM_TRACKING_THRESH: *value = config->tracking_thresh; return true;
        case BUBBLE_ENGINE_PARAM_SUSTAIN_THRESH: *value = config->sustain_thresh; return true;
        case BUBBLE_ENGINE_PARAM_TRANSIENT_DELTA: *value = config->transient_delta; return true;
        case BUBBLE_ENGINE_PARAM_DUCK_BURST_LEVEL: *value = config->duck_burst_level; return true;
        case BUBBLE_ENGINE_PARAM_DUCK_ATTACK_COEF: *value = config->duck_attack_coef; return true;
        case BUBBLE_ENGINE_PARAM_DUCK_RELEASE_COEF: *value = config->duck_release_coef; return true;
        case BUBBLE_ENGINE_PARAM_BURST_DURATION_TICKS: *value = (float)config->burst_duration_ticks; return true;
        case BUBBLE_ENGINE_PARAM_BURST_IMMEDIATE_COUNT: *value = (float)config->burst_immediate_count; return true;
        case BUBBLE_ENGINE_PARAM_DENSITY_BURST: *value = config->density_burst; return true;
        case BUBBLE_ENGINE_PARAM_DENSITY_SUSTAIN: *value = config->density_sustain; return true;
        case BUBBLE_ENGINE_PARAM_DENSITY_DECAY: *value = config->density_decay; return true;
        case BUBBLE_ENGINE_PARAM_ATTACK_REGION_MIN_OFFSET_SAMPLES: *value = (float)config->attack_region.min_offset_samples; return true;
        case BUBBLE_ENGINE_PARAM_ATTACK_REGION_MAX_OFFSET_SAMPLES: *value = (float)config->attack_region.max_offset_samples; return true;
        case BUBBLE_ENGINE_PARAM_BODY_REGION_MIN_OFFSET_SAMPLES: *value = (float)config->body_region.min_offset_samples; return true;
        case BUBBLE_ENGINE_PARAM_BODY_REGION_MAX_OFFSET_SAMPLES: *value = (float)config->body_region.max_offset_samples; return true;
        case BUBBLE_ENGINE_PARAM_MEMORY_REGION_MIN_OFFSET_SAMPLES: *value = (float)config->memory_region.min_offset_samples; return true;
        case BUBBLE_ENGINE_PARAM_MEMORY_REGION_MAX_OFFSET_SAMPLES: *value = (float)config->memory_region.max_offset_samples; return true;
        case BUBBLE_ENGINE_PARAM_MICRO_DURATION_MS_MIN: *value = config->class_configs[BUBBLE_CLASS_MICRO_ATTACK].duration_ms_min; return true;
        case BUBBLE_ENGINE_PARAM_MICRO_DURATION_MS_MAX: *value = config->class_configs[BUBBLE_CLASS_MICRO_ATTACK].duration_ms_max; return true;
        case BUBBLE_ENGINE_PARAM_SHORT_DURATION_MS_MIN: *value = config->class_configs[BUBBLE_CLASS_SHORT_INTERMEDIATE].duration_ms_min; return true;
        case BUBBLE_ENGINE_PARAM_SHORT_DURATION_MS_MAX: *value = config->class_configs[BUBBLE_CLASS_SHORT_INTERMEDIATE].duration_ms_max; return true;
        case BUBBLE_ENGINE_PARAM_BODY_DURATION_MS_MIN: *value = config->class_configs[BUBBLE_CLASS_SUSTAIN_BODY].duration_ms_min; return true;
        case BUBBLE_ENGINE_PARAM_BODY_DURATION_MS_MAX: *value = config->class_configs[BUBBLE_CLASS_SUSTAIN_BODY].duration_ms_max; return true;
        case BUBBLE_ENGINE_PARAM_RNG_SEED: *value = (float)config->rng_seed; return true;
        case BUBBLE_ENGINE_PARAM_MIX_DRY_GAIN: *value = preset->master_dry_gain; return true;
        case BUBBLE_ENGINE_PARAM_MIX_WET_GAIN: *value = preset->master_wet_gain; return true;
        case BUBBLE_ENGINE_PARAM_STEREO_WIDTH: *value = config->stereo_width; return true;
        case BUBBLE_ENGINE_PARAM_ATTACK_PAN_SPREAD: *value = config->attack_pan_spread; return true;
        case BUBBLE_ENGINE_PARAM_SUSTAIN_PAN_SPREAD: *value = config->sustain_pan_spread; return true;
        case BUBBLE_ENGINE_PARAM_SMART_START_ENABLE: *value = (float)config->smart_start_enable; return true;
        case BUBBLE_ENGINE_PARAM_SMART_START_RANGE: *value = (float)config->smart_start_range; return true;
        case BUBBLE_ENGINE_PARAM_ENVELOPE_VARIATION: *value = config->envelope_variation; return true;
        case BUBBLE_ENGINE_PARAM_ENVELOPE_FAMILY: *value = (float)config->envelope_family; return true;
        case BUBBLE_ENGINE_PARAM_WET_DRIVE: *value = config->wet_drive; return true;
        case BUBBLE_ENGINE_PARAM_WET_CLIP_AMOUNT: *value = config->wet_clip_amount; return true;
        case BUBBLE_ENGINE_PARAM_WET_OUTPUT_TRIM: *value = config->wet_output_trim; return true;
        case BUBBLE_ENGINE_PARAM_SUSTAIN_DIFFUSION_ENABLE: *value = (float)config->sustain_diffusion_enable; return true;
        case BUBBLE_ENGINE_PARAM_SUSTAIN_DIFFUSION_AMOUNT: *value = config->sustain_diffusion_amount; return true;
        case BUBBLE_ENGINE_PARAM_SUSTAIN_DIFFUSION_STAGES: *value = (float)config->sustain_diffusion_stages; return true;
        case BUBBLE_ENGINE_PARAM_SUSTAIN_DIFFUSION_DELAY: *value = (float)config->sustain_diffusion_delay; return true;
        case BUBBLE_ENGINE_PARAM_SUSTAIN_DIFFUSION_FEEDBACK: *value = config->sustain_diffusion_feedback; return true;
        case BUBBLE_ENGINE_PARAM_DROPLET_ENABLE: *value = (float)config->droplet_enable; return true;
        case BUBBLE_ENGINE_PARAM_DROPLET_PROBABILITY: *value = config->droplet_probability; return true;
        case BUBBLE_ENGINE_PARAM_DROPLET_GAIN: *value = config->droplet_gain; return true;
        case BUBBLE_ENGINE_PARAM_DROPLET_LENGTH_SCALE: *value = config->droplet_length_scale; return true;
        case BUBBLE_ENGINE_PARAM_MEMORY_MIX: *value = config->memory_mix; return true;
        case BUBBLE_ENGINE_PARAM_MEMORY_PULL: *value = config->memory_pull; return true;
        case BUBBLE_ENGINE_PARAM_MEMORY_DARKENING: *value = config->memory_darkening; return true;
        case BUBBLE_ENGINE_PARAM_TONE_VARIATION: *value = config->tone_variation; return true;
        case BUBBLE_ENGINE_PARAM_ATTACK_BRIGHTNESS: *value = config->attack_brightness; return true;
        case BUBBLE_ENGINE_PARAM_SUSTAIN_DARKNESS: *value = config->sustain_darkness; return true;
        case BUBBLE_ENGINE_PARAM_ATTACK_RATE_JITTER: *value = (float)config->attack_rate_jitter; return true;
        case BUBBLE_ENGINE_PARAM_ATTACK_RATE_JITTER_DEPTH: *value = config->attack_rate_jitter_depth; return true;
        case BUBBLE_ENGINE_PARAM_QUALITY_PROFILE: *value = (float)config->quality_profile; return true;
        case BUBBLE_ENGINE_PARAM_ACTIVE_VOICE_LIMIT: *value = (float)config->active_voice_limit; return true;
        case BUBBLE_ENGINE_PARAM_FREEZE_AMOUNT: *value = config->freeze_amount; return true;
        case BUBBLE_ENGINE_PARAM_FREEZE_ENABLED: *value = (float)config->freeze_enabled; return true;
        case BUBBLE_ENGINE_PARAM_REVERSE_PROBABILITY: *value = config->reverse_probability; return true;
        case BUBBLE_ENGINE_PARAM_PITCH_MODE: *value = (float)config->pitch_mode; return true;
        case BUBBLE_ENGINE_PARAM_SHIMMER_AMOUNT: *value = config->shimmer_amount; return true;
        case BUBBLE_ENGINE_PARAM_FINAL_LIMITER_CEILING_DB: *value = config->final_limiter_ceiling_db; return true;
        case BUBBLE_ENGINE_PARAM_FINAL_LIMITER_RELEASE_MS: *value = config->final_limiter_release_ms; return true;
        case BUBBLE_ENGINE_PARAM_MOTION_RATE: *value = config->motion_rate; return true;
        case BUBBLE_ENGINE_PARAM_MOTION_DEPTH: *value = config->motion_depth; return true;
        case BUBBLE_ENGINE_PARAM_MOTION_SHAPE: *value = (float)config->motion_shape; return true;
        case BUBBLE_ENGINE_PARAM_TEMPO_BPM: *value = config->tempo_bpm; return true;
        case BUBBLE_ENGINE_PARAM_TEMPO_SYNC_ENABLED: *value = (float)config->tempo_sync_enabled; return true;
        case BUBBLE_ENGINE_PARAM_RHYTHM_DIVISION: *value = (float)config->rhythm_division; return true;
        case BUBBLE_ENGINE_PARAM_BURST_MODE: *value = (float)config->burst_mode; return true;
        case BUBBLE_ENGINE_PARAM_RHYTHM_PATTERN: *value = (float)config->rhythm_pattern; return true;
        default: return false;
    }
}

static bool ValidatePresetRelations(const BubbleEnginePreset_t* preset, char* error, size_t error_size) {
    const BubbleEngineConfig_t* c = &preset->config;
    if (c->attack_region.min_offset_samples > c->attack_region.max_offset_samples ||
        c->body_region.min_offset_samples > c->body_region.max_offset_samples ||
        c->memory_region.min_offset_samples > c->memory_region.max_offset_samples) {
        SetError(error, error_size, "Preset validation failed: region min offsets must be <= max offsets.");
        return false;
    }
    for (int i = 0; i < BUBBLE_CLASS_COUNT; ++i) {
        if (c->class_configs[i].duration_ms_min > c->class_configs[i].duration_ms_max) {
            SetError(error, error_size, "Preset validation failed: class minimum durations must be <= maximum durations.");
            return false;
        }
    }
    return true;
}

bool bubble_preset_load_json(const char* json, BubbleEnginePreset_t* preset, char* error, size_t error_size) {
    if (json == NULL || preset == NULL) {
        SetError(error, error_size, "Invalid preset load arguments.");
        return false;
    }

    bubble_engine_default_config(&preset->config);
    preset->master_dry_gain = 1.0f;
    preset->master_wet_gain = 1.0f;
    for (int i = 0; i < BUBBLES_MACRO_COUNT; i++) {
        preset->macro_values[i] = 0.5f;
        preset->macro_targets[i] = 0.5f;
    }
    preset->macro_values[((int)BUBBLE_PARAM_FREEZE - (int)BUBBLE_PARAM_DENSITY)] = 0.0f;
    preset->macro_targets[((int)BUBBLE_PARAM_FREEZE - (int)BUBBLE_PARAM_DENSITY)] = 0.0f;
    preset->macro_values[((int)BUBBLE_PARAM_SPARKLE - (int)BUBBLE_PARAM_DENSITY)] = 0.0f;
    preset->macro_targets[((int)BUBBLE_PARAM_SPARKLE - (int)BUBBLE_PARAM_DENSITY)] = 0.0f;
    preset->macro_dirty_mask = 0u;
    preset->developer_mode = 0;

    const char* params_begin = NULL;
    const char* params_end = NULL;
    (void)FindJsonObjectRange(json, "params", &params_begin, &params_end);

    for (size_t i = 0; i < BUBBLE_PRESET_PARAM_COUNT; ++i) {
        const BubblePresetParamSpec_t* spec = &BUBBLE_PRESET_PARAM_SPECS[i];
        float value = 0.0f;
        if (!ReadParamNumber(json, params_begin, params_end, spec->canonical_name, &value)) {
            if (spec->id == BUBBLE_ENGINE_PARAM_MIX_DRY_GAIN && ReadParamNumber(json, params_begin, params_end, "master_dry_gain", &value)) {
                /* legacy key accepted */
            } else if (spec->id == BUBBLE_ENGINE_PARAM_MIX_WET_GAIN && ReadParamNumber(json, params_begin, params_end, "master_wet_gain", &value)) {
                /* legacy key accepted */
            } else {
                continue;
            }
        }

        if (!NormalizeTypedValue(spec, &value)) {
            char message[192];
            snprintf(message, sizeof(message), "Preset validation failed: %s must be a finite int32-compatible value.", spec->canonical_name);
            SetError(error, error_size, message);
            return false;
        }
        if (!bubble_preset_validate_param_value(spec, value)) {
            char message[192];
            snprintf(message, sizeof(message), "Preset validation failed: %s is outside %.9g..%.9g.", spec->canonical_name, spec->min_value, spec->max_value);
            SetError(error, error_size, message);
            return false;
        }
        if (!SetPresetParam(preset, spec->id, value)) {
            SetError(error, error_size, "Preset validation failed: unsupported parameter id.");
            return false;
        }
    }

    if (params_begin == NULL || params_end == NULL) {
        int32_t micro_offset = 441;
        int32_t micro_jitter = 3087;
        int32_t short_offset = 3528;
        int32_t short_jitter = 7497;
        int32_t body_offset = 11025;
        int32_t body_jitter = 28665;
        float v = 0.0f;
        if (!ReadLegacyInt(json, "micro_offset_samples", &micro_offset, error, error_size) ||
            !ReadLegacyInt(json, "micro_jitter_samples", &micro_jitter, error, error_size) ||
            !ReadLegacyInt(json, "short_offset_samples", &short_offset, error, error_size) ||
            !ReadLegacyInt(json, "short_jitter_samples", &short_jitter, error, error_size) ||
            !ReadLegacyInt(json, "body_offset_samples", &body_offset, error, error_size) ||
            !ReadLegacyInt(json, "body_jitter_samples", &body_jitter, error, error_size)) {
            return false;
        }

        int32_t legacy_attack_max = 0;
        int32_t legacy_body_max = 0;
        int32_t legacy_memory_max = 0;
        if (!CheckedAddInt32(micro_offset, micro_jitter, &legacy_attack_max) ||
            !CheckedAddInt32(short_offset, short_jitter, &legacy_body_max) ||
            !CheckedAddInt32(body_offset, body_jitter, &legacy_memory_max)) {
            SetError(error, error_size, "Preset validation failed: legacy offset + jitter overflowed int32 range.");
            return false;
        }

        if (!ReadParamNumber(json, NULL, NULL, "attack_region_min_offset_samples", &v)) preset->config.attack_region.min_offset_samples = micro_offset;
        if (!ReadParamNumber(json, NULL, NULL, "attack_region_max_offset_samples", &v)) preset->config.attack_region.max_offset_samples = legacy_attack_max;
        if (!ReadParamNumber(json, NULL, NULL, "body_region_min_offset_samples", &v)) preset->config.body_region.min_offset_samples = short_offset;
        if (!ReadParamNumber(json, NULL, NULL, "body_region_max_offset_samples", &v)) preset->config.body_region.max_offset_samples = legacy_body_max;
        if (!ReadParamNumber(json, NULL, NULL, "memory_region_min_offset_samples", &v)) preset->config.memory_region.min_offset_samples = body_offset;
        if (!ReadParamNumber(json, NULL, NULL, "memory_region_max_offset_samples", &v)) preset->config.memory_region.max_offset_samples = legacy_memory_max;

        if (!ValidateRegionOffset(BUBBLE_ENGINE_PARAM_ATTACK_REGION_MIN_OFFSET_SAMPLES, preset->config.attack_region.min_offset_samples, error, error_size, "attack_region_min_offset_samples") ||
            !ValidateRegionOffset(BUBBLE_ENGINE_PARAM_ATTACK_REGION_MAX_OFFSET_SAMPLES, preset->config.attack_region.max_offset_samples, error, error_size, "attack_region_max_offset_samples") ||
            !ValidateRegionOffset(BUBBLE_ENGINE_PARAM_BODY_REGION_MIN_OFFSET_SAMPLES, preset->config.body_region.min_offset_samples, error, error_size, "body_region_min_offset_samples") ||
            !ValidateRegionOffset(BUBBLE_ENGINE_PARAM_BODY_REGION_MAX_OFFSET_SAMPLES, preset->config.body_region.max_offset_samples, error, error_size, "body_region_max_offset_samples") ||
            !ValidateRegionOffset(BUBBLE_ENGINE_PARAM_MEMORY_REGION_MIN_OFFSET_SAMPLES, preset->config.memory_region.min_offset_samples, error, error_size, "memory_region_min_offset_samples") ||
            !ValidateRegionOffset(BUBBLE_ENGINE_PARAM_MEMORY_REGION_MAX_OFFSET_SAMPLES, preset->config.memory_region.max_offset_samples, error, error_size, "memory_region_max_offset_samples")) {
            return false;
        }
    }

    return ValidatePresetRelations(preset, error, error_size);
}

bool bubble_preset_load_file(const char* path, BubbleEnginePreset_t* preset, char* error, size_t error_size) {
    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        SetError(error, error_size, "Could not open JSON preset file.");
        return false;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        SetError(error, error_size, "Could not seek JSON preset file.");
        return false;
    }
    long length = ftell(file);
    if (length < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        SetError(error, error_size, "Could not measure JSON preset file.");
        return false;
    }
    char* json = (char*)malloc((size_t)length + 1u);
    if (json == NULL) {
        fclose(file);
        SetError(error, error_size, "Failed to allocate memory for JSON preset.");
        return false;
    }
    if (fread(json, 1, (size_t)length, file) != (size_t)length) {
        free(json);
        fclose(file);
        SetError(error, error_size, "Failed to read JSON preset file.");
        return false;
    }
    json[length] = '\0';
    fclose(file);
    bool ok = bubble_preset_load_json(json, preset, error, error_size);
    free(json);
    return ok;
}

static bool Append(char* out, size_t out_size, size_t* used, const char* fmt, ...) {
    if (*used >= out_size) return false;
    va_list args;
    va_start(args, fmt);
    int wrote = vsnprintf(out + *used, out_size - *used, fmt, args);
    va_end(args);
    if (wrote < 0 || (size_t)wrote >= out_size - *used) return false;
    *used += (size_t)wrote;
    return true;
}

bool bubble_preset_save_json(const BubbleEnginePreset_t* preset, char* out_json, size_t out_json_size, char* error, size_t error_size) {
    if (preset == NULL || out_json == NULL || out_json_size == 0) {
        SetError(error, error_size, "Invalid preset save arguments.");
        return false;
    }
    if (!ValidatePresetRelations(preset, error, error_size)) return false;

    size_t used = 0;
    if (!Append(out_json, out_json_size, &used,
        "{\n  \"schema_version\": %d,\n  \"engine_version\": \"%s\",\n  \"params\": {\n",
        BUBBLE_PRESET_SCHEMA_VERSION,
        BUBBLE_PRESET_ENGINE_VERSION)) goto overflow;

    bool first = true;
    for (size_t i = 0; i < BUBBLE_PRESET_PARAM_COUNT; ++i) {
        const BubblePresetParamSpec_t* spec = &BUBBLE_PRESET_PARAM_SPECS[i];
        float value = 0.0f;
        if (!GetPresetParam(preset, spec->id, &value)) continue;
        if (!NormalizeTypedValue(spec, &value) || !bubble_preset_validate_param_value(spec, value)) {
            char message[192];
            snprintf(message, sizeof(message), "Preset validation failed: %s cannot be serialized outside %.9g..%.9g.", spec->canonical_name, spec->min_value, spec->max_value);
            SetError(error, error_size, message);
            return false;
        }
        if (!first) {
            if (!Append(out_json, out_json_size, &used, ",\n")) goto overflow;
        }
        first = false;
        if (spec->type == BUBBLE_PRESET_PARAM_FLOAT) {
            if (!Append(out_json, out_json_size, &used, "    \"%s\": %.9g", spec->canonical_name, value)) goto overflow;
        } else {
            int32_t int_value = 0;
            if (!FloatToInt32(value, &int_value)) {
                SetError(error, error_size, "Preset JSON integer value is outside int32 range.");
                return false;
            }
            if (!Append(out_json, out_json_size, &used, "    \"%s\": %d", spec->canonical_name, int_value)) goto overflow;
        }
    }
    if (!first) {
        if (!Append(out_json, out_json_size, &used, "\n")) goto overflow;
    }
    if (!Append(out_json, out_json_size, &used, "  }\n}\n")) goto overflow;
    return true;

overflow:
    SetError(error, error_size, "Preset JSON output buffer is too small.");
    if (out_json_size > 0) out_json[out_json_size - 1] = '\0';
    return false;
}

bool bubble_preset_save_file(const char* path, const BubbleEnginePreset_t* preset, char* error, size_t error_size) {
    const size_t json_size = 8192u;
    char* json = (char*)malloc(json_size);
    if (json == NULL) {
        SetError(error, error_size, "Failed to allocate memory for saving preset.");
        return false;
    }
    if (!bubble_preset_save_json(preset, json, json_size, error, error_size)) {
        free(json);
        return false;
    }
    FILE* file = fopen(path, "wb");
    if (file == NULL) {
        free(json);
        SetError(error, error_size, "Could not open preset file for writing.");
        return false;
    }
    size_t len = strlen(json);
    bool ok = fwrite(json, 1, len, file) == len;
    fclose(file);
    free(json);
    if (!ok) SetError(error, error_size, "Failed to write preset file.");
    return ok;
}
