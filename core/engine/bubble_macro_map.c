#include "bubble_engine.h"

#include <math.h>
#include <stddef.h>

#define BUBBLE_NEUTRAL_MACRO_VALUE 0.5f

typedef struct BubbleMacroTarget {
    BubbleParameterId macro;
    BubbleParameterId parameter;
    float min_value;
    float max_value;
    BubbleParameterCurve curve;
    int invert;
    int integer_value;
} BubbleMacroTarget;

static float Clamp01(float value) {
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

static float Lerp(float a, float b, float t) {
    return a + (b - a) * Clamp01(t);
}

static float ApplyCurve(float value, BubbleParameterCurve curve) {
    const float v = Clamp01(value);
    switch (curve) {
        case BUBBLE_PARAMETER_CURVE_EXP:
            return (expf(v) - 1.0f) / 1.71828182845904523536f;
        case BUBBLE_PARAMETER_CURVE_LOG:
            return log1pf(v * 9.0f) / 2.30258509299404568402f;
        case BUBBLE_PARAMETER_CURVE_LINEAR:
        default:
            return v;
    }
}

static int MacroIndex(BubbleParameterId macro) {
    if (macro >= BUBBLE_PARAM_DENSITY && macro <= BUBBLE_PARAM_MIX) {
        return (int)macro - (int)BUBBLE_PARAM_DENSITY;
    }
    return -1;
}

static float MacroValue(const float macro_values[BUBBLES_MACRO_COUNT], BubbleParameterId macro) {
    const int index = MacroIndex(macro);
    if (index < 0 || index >= BUBBLES_MACRO_COUNT || macro_values == NULL) return BUBBLE_NEUTRAL_MACRO_VALUE;
    return Clamp01(macro_values[index]);
}

static void SetMappedParameter(EngineConfig_t* config, float* master_dry_gain, float* master_wet_gain, BubbleParameterId parameter, float value) {
    switch (parameter) {
        case BUBBLE_ENGINE_PARAM_TRACKING_THRESH: config->tracking_thresh = value; break;
        case BUBBLE_ENGINE_PARAM_SUSTAIN_THRESH: config->sustain_thresh = value; break;
        case BUBBLE_ENGINE_PARAM_TRANSIENT_DELTA: config->transient_delta = value; break;
        case BUBBLE_ENGINE_PARAM_DUCK_BURST_LEVEL: config->duck_burst_level = value; break;
        case BUBBLE_ENGINE_PARAM_DUCK_ATTACK_COEF: config->duck_attack_coef = value; break;
        case BUBBLE_ENGINE_PARAM_DUCK_RELEASE_COEF: config->duck_release_coef = value; break;
        case BUBBLE_ENGINE_PARAM_BURST_IMMEDIATE_COUNT: config->burst_immediate_count = (int32_t)value; break;
        case BUBBLE_ENGINE_PARAM_DENSITY_BURST: config->density_burst = value; break;
        case BUBBLE_ENGINE_PARAM_DENSITY_SUSTAIN: config->density_sustain = value; break;
        case BUBBLE_ENGINE_PARAM_DENSITY_DECAY: config->density_decay = value; break;
        case BUBBLE_ENGINE_PARAM_MEMORY_REGION_MIN_OFFSET_SAMPLES: config->memory_region.min_offset_samples = (int32_t)value; break;
        case BUBBLE_ENGINE_PARAM_MEMORY_REGION_MAX_OFFSET_SAMPLES: config->memory_region.max_offset_samples = (int32_t)value; break;
        case BUBBLE_ENGINE_PARAM_MICRO_DURATION_MS_MIN: config->class_configs[BUBBLE_CLASS_MICRO_ATTACK].duration_ms_min = value; break;
        case BUBBLE_ENGINE_PARAM_MICRO_DURATION_MS_MAX: config->class_configs[BUBBLE_CLASS_MICRO_ATTACK].duration_ms_max = value; break;
        case BUBBLE_ENGINE_PARAM_SHORT_DURATION_MS_MIN: config->class_configs[BUBBLE_CLASS_SHORT_INTERMEDIATE].duration_ms_min = value; break;
        case BUBBLE_ENGINE_PARAM_SHORT_DURATION_MS_MAX: config->class_configs[BUBBLE_CLASS_SHORT_INTERMEDIATE].duration_ms_max = value; break;
        case BUBBLE_ENGINE_PARAM_BODY_DURATION_MS_MIN: config->class_configs[BUBBLE_CLASS_SUSTAIN_BODY].duration_ms_min = value; break;
        case BUBBLE_ENGINE_PARAM_BODY_DURATION_MS_MAX: config->class_configs[BUBBLE_CLASS_SUSTAIN_BODY].duration_ms_max = value; break;
        case BUBBLE_ENGINE_PARAM_MIX_DRY_GAIN: if (master_dry_gain != NULL) *master_dry_gain = value; break;
        case BUBBLE_ENGINE_PARAM_MIX_WET_GAIN: if (master_wet_gain != NULL) *master_wet_gain = value; break;
        case BUBBLE_ENGINE_PARAM_STEREO_WIDTH: config->stereo_width = value; break;
        case BUBBLE_ENGINE_PARAM_ATTACK_PAN_SPREAD: config->attack_pan_spread = value; break;
        case BUBBLE_ENGINE_PARAM_SUSTAIN_PAN_SPREAD: config->sustain_pan_spread = value; break;
        case BUBBLE_ENGINE_PARAM_ENVELOPE_VARIATION: config->envelope_variation = value; break;
        case BUBBLE_ENGINE_PARAM_WET_DRIVE: config->wet_drive = value; break;
        case BUBBLE_ENGINE_PARAM_WET_CLIP_AMOUNT: config->wet_clip_amount = value; break;
        case BUBBLE_ENGINE_PARAM_WET_OUTPUT_TRIM: config->wet_output_trim = value; break;
        case BUBBLE_ENGINE_PARAM_SUSTAIN_DIFFUSION_AMOUNT: config->sustain_diffusion_amount = value; break;
        case BUBBLE_ENGINE_PARAM_SUSTAIN_DIFFUSION_DELAY: config->sustain_diffusion_delay = (int32_t)value; break;
        case BUBBLE_ENGINE_PARAM_SUSTAIN_DIFFUSION_FEEDBACK: config->sustain_diffusion_feedback = value; break;
        case BUBBLE_ENGINE_PARAM_DROPLET_PROBABILITY: config->droplet_probability = value; break;
        case BUBBLE_ENGINE_PARAM_DROPLET_GAIN: config->droplet_gain = value; break;
        case BUBBLE_ENGINE_PARAM_DROPLET_LENGTH_SCALE: config->droplet_length_scale = value; break;
        case BUBBLE_ENGINE_PARAM_MEMORY_MIX: config->memory_mix = value; break;
        case BUBBLE_ENGINE_PARAM_MEMORY_PULL: config->memory_pull = value; break;
        case BUBBLE_ENGINE_PARAM_MEMORY_DARKENING: config->memory_darkening = value; break;
        case BUBBLE_ENGINE_PARAM_TONE_VARIATION: config->tone_variation = value; break;
        case BUBBLE_ENGINE_PARAM_ATTACK_BRIGHTNESS: config->attack_brightness = value; break;
        case BUBBLE_ENGINE_PARAM_SUSTAIN_DARKNESS: config->sustain_darkness = value; break;
        case BUBBLE_ENGINE_PARAM_ATTACK_RATE_JITTER_DEPTH: config->attack_rate_jitter_depth = value; break;
        case BUBBLE_ENGINE_PARAM_FREEZE_AMOUNT: config->freeze_amount = value; break;
        case BUBBLE_ENGINE_PARAM_REVERSE_PROBABILITY: config->reverse_probability = value; break;
        case BUBBLE_ENGINE_PARAM_SHIMMER_AMOUNT: config->shimmer_amount = value; break;
        case BUBBLE_ENGINE_PARAM_MOTION_RATE: config->motion_rate = value; break;
        case BUBBLE_ENGINE_PARAM_MOTION_DEPTH: config->motion_depth = value; break;
        case BUBBLE_ENGINE_PARAM_MOTION_SHAPE: config->motion_shape = (int32_t)value; break;
        default: break;
    }
}

static const BubbleMacroTarget BUBBLE_MACRO_TARGETS[] = {
    { BUBBLE_PARAM_DENSITY, BUBBLE_ENGINE_PARAM_DENSITY_BURST, 20.0f, 140.0f, BUBBLE_PARAMETER_CURVE_LOG, 0, 0 },
    { BUBBLE_PARAM_DENSITY, BUBBLE_ENGINE_PARAM_DENSITY_SUSTAIN, 8.0f, 55.0f, BUBBLE_PARAMETER_CURVE_LOG, 0, 0 },
    { BUBBLE_PARAM_DENSITY, BUBBLE_ENGINE_PARAM_DENSITY_DECAY, 1.0f, 18.0f, BUBBLE_PARAMETER_CURVE_LOG, 0, 0 },
    { BUBBLE_PARAM_DENSITY, BUBBLE_ENGINE_PARAM_BURST_IMMEDIATE_COUNT, 2.0f, 9.0f, BUBBLE_PARAMETER_CURVE_LINEAR, 0, 1 },

    { BUBBLE_PARAM_BLOOM, BUBBLE_ENGINE_PARAM_SUSTAIN_DIFFUSION_AMOUNT, 0.0f, 0.72f, BUBBLE_PARAMETER_CURVE_EXP, 0, 0 },
    { BUBBLE_PARAM_BLOOM, BUBBLE_ENGINE_PARAM_SUSTAIN_DIFFUSION_DELAY, 8.0f, 42.0f, BUBBLE_PARAMETER_CURVE_LOG, 0, 1 },
    { BUBBLE_PARAM_BLOOM, BUBBLE_ENGINE_PARAM_SUSTAIN_DIFFUSION_FEEDBACK, 0.20f, 0.68f, BUBBLE_PARAMETER_CURVE_EXP, 0, 0 },
    { BUBBLE_PARAM_BLOOM, BUBBLE_ENGINE_PARAM_WET_DRIVE, 0.75f, 1.45f, BUBBLE_PARAMETER_CURVE_EXP, 0, 0 },
    { BUBBLE_PARAM_BLOOM, BUBBLE_ENGINE_PARAM_WET_OUTPUT_TRIM, 0.70f, 1.15f, BUBBLE_PARAMETER_CURVE_EXP, 0, 0 },

    { BUBBLE_PARAM_MOTION, BUBBLE_ENGINE_PARAM_MICRO_DURATION_MS_MIN, 5.0f, 2.0f, BUBBLE_PARAMETER_CURVE_LINEAR, 0, 0 },
    { BUBBLE_PARAM_MOTION, BUBBLE_ENGINE_PARAM_MICRO_DURATION_MS_MAX, 18.0f, 45.0f, BUBBLE_PARAMETER_CURVE_LOG, 0, 0 },
    { BUBBLE_PARAM_MOTION, BUBBLE_ENGINE_PARAM_SHORT_DURATION_MS_MIN, 12.0f, 60.0f, BUBBLE_PARAMETER_CURVE_LOG, 0, 0 },
    { BUBBLE_PARAM_MOTION, BUBBLE_ENGINE_PARAM_SHORT_DURATION_MS_MAX, 35.0f, 160.0f, BUBBLE_PARAMETER_CURVE_LOG, 0, 0 },
    { BUBBLE_PARAM_MOTION, BUBBLE_ENGINE_PARAM_BODY_DURATION_MS_MIN, 60.0f, 220.0f, BUBBLE_PARAMETER_CURVE_LOG, 0, 0 },
    { BUBBLE_PARAM_MOTION, BUBBLE_ENGINE_PARAM_BODY_DURATION_MS_MAX, 140.0f, 520.0f, BUBBLE_PARAMETER_CURVE_LOG, 0, 0 },
    { BUBBLE_PARAM_MOTION, BUBBLE_ENGINE_PARAM_ATTACK_RATE_JITTER_DEPTH, 0.002f, 0.09f, BUBBLE_PARAMETER_CURVE_EXP, 0, 0 },
    { BUBBLE_PARAM_MOTION, BUBBLE_ENGINE_PARAM_REVERSE_PROBABILITY, 0.0f, 0.35f, BUBBLE_PARAMETER_CURVE_EXP, 0, 0 },
    { BUBBLE_PARAM_MOTION, BUBBLE_ENGINE_PARAM_MOTION_RATE, 0.10f, 0.55f, BUBBLE_PARAMETER_CURVE_EXP, 0, 0 },
    { BUBBLE_PARAM_MOTION, BUBBLE_ENGINE_PARAM_MOTION_DEPTH, 0.0f, 0.78f, BUBBLE_PARAMETER_CURVE_LINEAR, 0, 0 },

    { BUBBLE_PARAM_TEXTURE, BUBBLE_ENGINE_PARAM_DROPLET_PROBABILITY, 0.02f, 0.38f, BUBBLE_PARAMETER_CURVE_EXP, 0, 0 },
    { BUBBLE_PARAM_TEXTURE, BUBBLE_ENGINE_PARAM_DROPLET_GAIN, 0.12f, 0.70f, BUBBLE_PARAMETER_CURVE_EXP, 0, 0 },
    { BUBBLE_PARAM_TEXTURE, BUBBLE_ENGINE_PARAM_DROPLET_LENGTH_SCALE, 0.28f, 0.95f, BUBBLE_PARAMETER_CURVE_LINEAR, 0, 0 },
    { BUBBLE_PARAM_TEXTURE, BUBBLE_ENGINE_PARAM_ENVELOPE_VARIATION, 0.05f, 0.85f, BUBBLE_PARAMETER_CURVE_LINEAR, 0, 0 },
    { BUBBLE_PARAM_TEXTURE, BUBBLE_ENGINE_PARAM_TONE_VARIATION, 0.05f, 0.85f, BUBBLE_PARAMETER_CURVE_LINEAR, 0, 0 },

    { BUBBLE_PARAM_SPACE, BUBBLE_ENGINE_PARAM_STEREO_WIDTH, 0.30f, 1.00f, BUBBLE_PARAMETER_CURVE_LINEAR, 0, 0 },
    { BUBBLE_PARAM_SPACE, BUBBLE_ENGINE_PARAM_ATTACK_PAN_SPREAD, 0.25f, 1.00f, BUBBLE_PARAMETER_CURVE_LINEAR, 0, 0 },
    { BUBBLE_PARAM_SPACE, BUBBLE_ENGINE_PARAM_SUSTAIN_PAN_SPREAD, 0.10f, 0.85f, BUBBLE_PARAMETER_CURVE_LINEAR, 0, 0 },

    { BUBBLE_PARAM_GRAVITY, BUBBLE_ENGINE_PARAM_TRACKING_THRESH, 0.01f, 0.12f, BUBBLE_PARAMETER_CURVE_LOG, 0, 0 },
    { BUBBLE_PARAM_GRAVITY, BUBBLE_ENGINE_PARAM_SUSTAIN_THRESH, 0.02f, 0.18f, BUBBLE_PARAMETER_CURVE_LOG, 0, 0 },
    { BUBBLE_PARAM_GRAVITY, BUBBLE_ENGINE_PARAM_TRANSIENT_DELTA, 0.02f, 0.22f, BUBBLE_PARAMETER_CURVE_LOG, 1, 0 },
    { BUBBLE_PARAM_GRAVITY, BUBBLE_ENGINE_PARAM_DUCK_BURST_LEVEL, 0.45f, 0.08f, BUBBLE_PARAMETER_CURVE_LINEAR, 0, 0 },
    { BUBBLE_PARAM_GRAVITY, BUBBLE_ENGINE_PARAM_DUCK_ATTACK_COEF, 0.55f, 0.97f, BUBBLE_PARAMETER_CURVE_EXP, 0, 0 },
    { BUBBLE_PARAM_GRAVITY, BUBBLE_ENGINE_PARAM_DUCK_RELEASE_COEF, 0.92f, 0.999f, BUBBLE_PARAMETER_CURVE_EXP, 0, 0 },

    { BUBBLE_PARAM_MEMORY, BUBBLE_ENGINE_PARAM_MEMORY_MIX, 0.12f, 0.72f, BUBBLE_PARAMETER_CURVE_LINEAR, 0, 0 },
    { BUBBLE_PARAM_MEMORY, BUBBLE_ENGINE_PARAM_MEMORY_PULL, 0.08f, 0.62f, BUBBLE_PARAMETER_CURVE_LINEAR, 0, 0 },
    { BUBBLE_PARAM_MEMORY, BUBBLE_ENGINE_PARAM_MEMORY_DARKENING, 0.08f, 0.72f, BUBBLE_PARAMETER_CURVE_LINEAR, 0, 0 },
    { BUBBLE_PARAM_MEMORY, BUBBLE_ENGINE_PARAM_MEMORY_REGION_MIN_OFFSET_SAMPLES, 8000.0f, 50000.0f, BUBBLE_PARAMETER_CURVE_LOG, 0, 1 },
    { BUBBLE_PARAM_MEMORY, BUBBLE_ENGINE_PARAM_MEMORY_REGION_MAX_OFFSET_SAMPLES, 24000.0f, 180000.0f, BUBBLE_PARAMETER_CURVE_LOG, 0, 1 },

    { BUBBLE_PARAM_CLARITY, BUBBLE_ENGINE_PARAM_ATTACK_BRIGHTNESS, 0.80f, 1.65f, BUBBLE_PARAMETER_CURVE_EXP, 0, 0 },
    { BUBBLE_PARAM_CLARITY, BUBBLE_ENGINE_PARAM_SUSTAIN_DARKNESS, 0.72f, 0.10f, BUBBLE_PARAMETER_CURVE_LINEAR, 0, 0 },
    { BUBBLE_PARAM_WARMTH, BUBBLE_ENGINE_PARAM_WET_CLIP_AMOUNT, 0.04f, 0.55f, BUBBLE_PARAMETER_CURVE_EXP, 0, 0 },

    { BUBBLE_PARAM_FREEZE, BUBBLE_ENGINE_PARAM_FREEZE_AMOUNT, 0.0f, 1.0f, BUBBLE_PARAMETER_CURVE_LINEAR, 0, 0 },
    { BUBBLE_PARAM_SPARKLE, BUBBLE_ENGINE_PARAM_SHIMMER_AMOUNT, 0.0f, 1.0f, BUBBLE_PARAMETER_CURVE_EXP, 0, 0 },
    { BUBBLE_PARAM_MIX, BUBBLE_ENGINE_PARAM_MIX_DRY_GAIN, 0.85f, 0.25f, BUBBLE_PARAMETER_CURVE_LINEAR, 0, 0 },
    { BUBBLE_PARAM_MIX, BUBBLE_ENGINE_PARAM_MIX_WET_GAIN, 0.0f, 0.90f, BUBBLE_PARAMETER_CURVE_LINEAR, 0, 0 },
};

void bubble_macro_map_default_values(float macro_values[BUBBLES_MACRO_COUNT]) {
    if (macro_values == NULL) return;
    for (int i = 0; i < BUBBLES_MACRO_COUNT; i++) {
        macro_values[i] = BUBBLE_NEUTRAL_MACRO_VALUE;
    }
    const int freeze_index = MacroIndex(BUBBLE_PARAM_FREEZE);
    if (freeze_index >= 0 && freeze_index < BUBBLES_MACRO_COUNT) {
        macro_values[freeze_index] = 0.0f;
    }
    const int sparkle_index = MacroIndex(BUBBLE_PARAM_SPARKLE);
    if (sparkle_index >= 0 && sparkle_index < BUBBLES_MACRO_COUNT) {
        macro_values[sparkle_index] = 0.0f;
    }
}

void bubble_macro_map_resolve(const float macro_values[BUBBLES_MACRO_COUNT],
                              const EngineConfig_t* base_config,
                              EngineConfig_t* out_config,
                              float* master_dry_gain,
                              float* master_wet_gain) {
    if (out_config == NULL) return;

    if (base_config != NULL) {
        *out_config = *base_config;
    } else {
        bubble_engine_default_config(out_config);
    }

    for (size_t i = 0; i < sizeof(BUBBLE_MACRO_TARGETS) / sizeof(BUBBLE_MACRO_TARGETS[0]); i++) {
        const BubbleMacroTarget* target = &BUBBLE_MACRO_TARGETS[i];
        float macro = MacroValue(macro_values, target->macro);
        if (target->invert) macro = 1.0f - macro;
        float curved = ApplyCurve(macro, target->curve);
        float value = Lerp(target->min_value, target->max_value, curved);
        if (target->integer_value) value = floorf(value + 0.5f);
        SetMappedParameter(out_config, master_dry_gain, master_wet_gain, target->parameter, value);
    }

    const float bloom = MacroValue(macro_values, BUBBLE_PARAM_BLOOM);
    const float motion = MacroValue(macro_values, BUBBLE_PARAM_MOTION);
    const float texture = MacroValue(macro_values, BUBBLE_PARAM_TEXTURE);
    const float freeze = MacroValue(macro_values, BUBBLE_PARAM_FREEZE);
    const float sparkle = MacroValue(macro_values, BUBBLE_PARAM_SPARKLE);
    const float warmth = MacroValue(macro_values, BUBBLE_PARAM_WARMTH);

    out_config->sustain_diffusion_enable = bloom > 0.08f ? 1 : 0;
    out_config->attack_rate_jitter = motion > 0.05f ? 1 : 0;
    out_config->droplet_enable = texture > 0.10f ? 1 : 0;
    out_config->freeze_enabled = freeze >= 0.5f ? 1 : 0;
    out_config->pitch_mode = sparkle > 0.05f ? BUBBLE_PITCH_MODE_SHIMMER : BUBBLE_PITCH_MODE_UNISON;

    out_config->attack_brightness *= Lerp(1.12f, 0.82f, ApplyCurve(warmth, BUBBLE_PARAMETER_CURVE_LINEAR));
    out_config->sustain_darkness += Lerp(0.0f, 0.18f, ApplyCurve(warmth, BUBBLE_PARAMETER_CURVE_LINEAR));
    if (out_config->sustain_darkness > 1.0f) out_config->sustain_darkness = 1.0f;
}
