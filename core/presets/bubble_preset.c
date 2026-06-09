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

static float NormalizeTypedValue(const BubblePresetParamSpec_t* spec, float value) {
    if (spec->type == BUBBLE_PRESET_PARAM_INT || spec->type == BUBBLE_PRESET_PARAM_BOOL || spec->type == BUBBLE_PRESET_PARAM_ENUM) {
        return (float)((int32_t)value);
    }
    return value;
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

        value = NormalizeTypedValue(spec, value);
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
        if (ReadParamNumber(json, NULL, NULL, "micro_offset_samples", &v)) micro_offset = (int32_t)v;
        if (ReadParamNumber(json, NULL, NULL, "micro_jitter_samples", &v)) micro_jitter = (int32_t)v;
        if (ReadParamNumber(json, NULL, NULL, "short_offset_samples", &v)) short_offset = (int32_t)v;
        if (ReadParamNumber(json, NULL, NULL, "short_jitter_samples", &v)) short_jitter = (int32_t)v;
        if (ReadParamNumber(json, NULL, NULL, "body_offset_samples", &v)) body_offset = (int32_t)v;
        if (ReadParamNumber(json, NULL, NULL, "body_jitter_samples", &v)) body_jitter = (int32_t)v;
        if (!ReadParamNumber(json, NULL, NULL, "attack_region_min_offset_samples", &v)) preset->config.attack_region.min_offset_samples = micro_offset;
        if (!ReadParamNumber(json, NULL, NULL, "attack_region_max_offset_samples", &v)) preset->config.attack_region.max_offset_samples = micro_offset + micro_jitter;
        if (!ReadParamNumber(json, NULL, NULL, "body_region_min_offset_samples", &v)) preset->config.body_region.min_offset_samples = short_offset;
        if (!ReadParamNumber(json, NULL, NULL, "body_region_max_offset_samples", &v)) preset->config.body_region.max_offset_samples = short_offset + short_jitter;
        if (!ReadParamNumber(json, NULL, NULL, "memory_region_min_offset_samples", &v)) preset->config.memory_region.min_offset_samples = body_offset;
        if (!ReadParamNumber(json, NULL, NULL, "memory_region_max_offset_samples", &v)) preset->config.memory_region.max_offset_samples = body_offset + body_jitter;
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

    for (size_t i = 0; i < BUBBLE_PRESET_PARAM_COUNT; ++i) {
        const BubblePresetParamSpec_t* spec = &BUBBLE_PRESET_PARAM_SPECS[i];
        float value = 0.0f;
        if (!GetPresetParam(preset, spec->id, &value)) continue;
        const char* comma = (i + 1u < BUBBLE_PRESET_PARAM_COUNT) ? "," : "";
        if (spec->type == BUBBLE_PRESET_PARAM_FLOAT) {
            if (!Append(out_json, out_json_size, &used, "    \"%s\": %.9g%s\n", spec->canonical_name, value, comma)) goto overflow;
        } else {
            if (!Append(out_json, out_json_size, &used, "    \"%s\": %d%s\n", spec->canonical_name, (int32_t)value, comma)) goto overflow;
        }
    }
    if (!Append(out_json, out_json_size, &used, "  }\n}\n")) goto overflow;
    return true;

overflow:
    SetError(error, error_size, "Preset JSON output buffer is too small.");
    if (out_json_size > 0) out_json[out_json_size - 1] = '\0';
    return false;
}

bool bubble_preset_save_file(const char* path, const BubbleEnginePreset_t* preset, char* error, size_t error_size) {
    char json[8192];
    if (!bubble_preset_save_json(preset, json, sizeof(json), error, error_size)) return false;
    FILE* file = fopen(path, "wb");
    if (file == NULL) {
        SetError(error, error_size, "Could not open preset file for writing.");
        return false;
    }
    size_t len = strlen(json);
    bool ok = fwrite(json, 1, len, file) == len;
    fclose(file);
    if (!ok) SetError(error, error_size, "Failed to write preset file.");
    return ok;
}
