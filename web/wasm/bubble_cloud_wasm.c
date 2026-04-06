#include <emscripten.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "../../core/sound_bubbles_dsp.h"

static SoundBubblesEngine_t engine;
static int16_t* delay_buffer = NULL;
static EngineConfig_t current_config;
static float master_dry_gain = 1.0f;
static float master_wet_gain = 1.0f;

enum {
    PARAM_ID_NOISE_FLOOR = 0,
    PARAM_ID_TRACKING_THRESH = 1,
    PARAM_ID_SUSTAIN_THRESH = 2,
    PARAM_ID_TRANSIENT_DELTA = 3,
    PARAM_ID_DUCK_BURST_LEVEL = 4,
    PARAM_ID_DUCK_ATTACK_COEF = 5,
    PARAM_ID_DUCK_RELEASE_COEF = 6,
    PARAM_ID_BURST_DURATION_TICKS = 7,
    PARAM_ID_BURST_IMMEDIATE_COUNT = 8,
    PARAM_ID_DENSITY_BURST = 9,
    PARAM_ID_DENSITY_SUSTAIN = 10,
    PARAM_ID_DENSITY_DECAY = 11,
    PARAM_ID_ATTACK_REGION_MIN_OFFSET_SAMPLES = 12,
    PARAM_ID_ATTACK_REGION_MAX_OFFSET_SAMPLES = 13,
    PARAM_ID_BODY_REGION_MIN_OFFSET_SAMPLES = 14,
    PARAM_ID_BODY_REGION_MAX_OFFSET_SAMPLES = 15,
    PARAM_ID_MEMORY_REGION_MIN_OFFSET_SAMPLES = 16,
    PARAM_ID_MEMORY_REGION_MAX_OFFSET_SAMPLES = 17,
    PARAM_ID_MICRO_DURATION_MS_MIN = 18,
    PARAM_ID_MICRO_DURATION_MS_MAX = 19,
    PARAM_ID_SHORT_DURATION_MS_MIN = 20,
    PARAM_ID_SHORT_DURATION_MS_MAX = 21,
    PARAM_ID_BODY_DURATION_MS_MIN = 22,
    PARAM_ID_BODY_DURATION_MS_MAX = 23,
    PARAM_ID_RNG_SEED = 24,
    PARAM_ID_MIX_DRY_GAIN = 25,
    PARAM_ID_MIX_WET_GAIN = 26
};

EMSCRIPTEN_KEEPALIVE
void wasm_init() {
    if (delay_buffer == NULL) {
        delay_buffer = (int16_t*)calloc(BUBBLES_BUFFER_SIZE_SAMPLES, sizeof(int16_t));
    }

    // Default config matching baseline
    memset(&current_config, 0, sizeof(EngineConfig_t));
    current_config.noise_floor = 0.001f;
    current_config.tracking_thresh = 0.01f;
    current_config.sustain_thresh = 0.05f;
    current_config.transient_delta = 0.05f;
    current_config.duck_burst_level = 0.2f;
    current_config.duck_attack_coef = 0.80f;
    current_config.duck_release_coef = 0.99f;
    current_config.burst_duration_ticks = 10;
    current_config.burst_immediate_count = 3;
    current_config.density_burst = 50.0f;
    current_config.density_sustain = 15.0f;
    current_config.density_decay = 5.0f;
    // Semantic read regions: attack(10-80ms), body(80-250ms), memory(250-900ms).
    current_config.attack_region.min_offset_samples = 441;
    current_config.attack_region.max_offset_samples = 3528;
    current_config.body_region.min_offset_samples = 3528;
    current_config.body_region.max_offset_samples = 11025;
    current_config.memory_region.min_offset_samples = 11025;
    current_config.memory_region.max_offset_samples = 39690;
    current_config.rng_seed = 1u;

    // Micro
    current_config.class_configs[BUBBLE_CLASS_MICRO_ATTACK].duration_ms_min = 5.0f;
    current_config.class_configs[BUBBLE_CLASS_MICRO_ATTACK].duration_ms_max = 15.0f;
    current_config.class_configs[BUBBLE_CLASS_MICRO_ATTACK].window_type = WINDOW_TYPE_HANN;

    // Short
    current_config.class_configs[BUBBLE_CLASS_SHORT_INTERMEDIATE].duration_ms_min = 20.0f;
    current_config.class_configs[BUBBLE_CLASS_SHORT_INTERMEDIATE].duration_ms_max = 50.0f;
    current_config.class_configs[BUBBLE_CLASS_SHORT_INTERMEDIATE].window_type = WINDOW_TYPE_HANN;

    // Body
    current_config.class_configs[BUBBLE_CLASS_SUSTAIN_BODY].duration_ms_min = 80.0f;
    current_config.class_configs[BUBBLE_CLASS_SUSTAIN_BODY].duration_ms_max = 200.0f;
    current_config.class_configs[BUBBLE_CLASS_SUSTAIN_BODY].window_type = WINDOW_TYPE_TUKEY_LIKE;

    SoundBubbles_Init(&engine, delay_buffer, &current_config);
    engine.master_dry_gain = master_dry_gain;
    engine.master_wet_gain = master_wet_gain;
}

EMSCRIPTEN_KEEPALIVE
void wasm_reset() {
    SoundBubbles_Init(&engine, delay_buffer, &current_config);
    engine.master_dry_gain = master_dry_gain;
    engine.master_wet_gain = master_wet_gain;
}

EMSCRIPTEN_KEEPALIVE
void wasm_process(uintptr_t in_ptr, uintptr_t out_l_ptr, uintptr_t out_r_ptr, int num_samples) {
    const float* in_mono = (const float*)in_ptr;
    float* out_left = (float*)out_l_ptr;
    float* out_right = (float*)out_r_ptr;

    SoundBubbles_ProcessBlock(&engine, in_mono, out_left, out_right, num_samples);
}

EMSCRIPTEN_KEEPALIVE
void wasm_set_param(int param_id, float value) {
    switch(param_id) {
        case PARAM_ID_NOISE_FLOOR: current_config.noise_floor = value; break;
        case PARAM_ID_TRACKING_THRESH: current_config.tracking_thresh = value; break;
        case PARAM_ID_SUSTAIN_THRESH: current_config.sustain_thresh = value; break;
        case PARAM_ID_TRANSIENT_DELTA: current_config.transient_delta = value; break;
        case PARAM_ID_DUCK_BURST_LEVEL: current_config.duck_burst_level = value; break;
        case PARAM_ID_DUCK_ATTACK_COEF: current_config.duck_attack_coef = value; break;
        case PARAM_ID_DUCK_RELEASE_COEF: current_config.duck_release_coef = value; break;
        case PARAM_ID_BURST_DURATION_TICKS: current_config.burst_duration_ticks = (int32_t)value; break;
        case PARAM_ID_BURST_IMMEDIATE_COUNT: current_config.burst_immediate_count = (int32_t)value; break;
        case PARAM_ID_DENSITY_BURST: current_config.density_burst = value; break;
        case PARAM_ID_DENSITY_SUSTAIN: current_config.density_sustain = value; break;
        case PARAM_ID_DENSITY_DECAY: current_config.density_decay = value; break;
        case PARAM_ID_ATTACK_REGION_MIN_OFFSET_SAMPLES: current_config.attack_region.min_offset_samples = (int32_t)value; break;
        case PARAM_ID_ATTACK_REGION_MAX_OFFSET_SAMPLES: current_config.attack_region.max_offset_samples = (int32_t)value; break;
        case PARAM_ID_BODY_REGION_MIN_OFFSET_SAMPLES: current_config.body_region.min_offset_samples = (int32_t)value; break;
        case PARAM_ID_BODY_REGION_MAX_OFFSET_SAMPLES: current_config.body_region.max_offset_samples = (int32_t)value; break;
        case PARAM_ID_MEMORY_REGION_MIN_OFFSET_SAMPLES: current_config.memory_region.min_offset_samples = (int32_t)value; break;
        case PARAM_ID_MEMORY_REGION_MAX_OFFSET_SAMPLES: current_config.memory_region.max_offset_samples = (int32_t)value; break;
        case PARAM_ID_MICRO_DURATION_MS_MIN: current_config.class_configs[BUBBLE_CLASS_MICRO_ATTACK].duration_ms_min = value; break;
        case PARAM_ID_MICRO_DURATION_MS_MAX: current_config.class_configs[BUBBLE_CLASS_MICRO_ATTACK].duration_ms_max = value; break;
        case PARAM_ID_SHORT_DURATION_MS_MIN: current_config.class_configs[BUBBLE_CLASS_SHORT_INTERMEDIATE].duration_ms_min = value; break;
        case PARAM_ID_SHORT_DURATION_MS_MAX: current_config.class_configs[BUBBLE_CLASS_SHORT_INTERMEDIATE].duration_ms_max = value; break;
        case PARAM_ID_BODY_DURATION_MS_MIN: current_config.class_configs[BUBBLE_CLASS_SUSTAIN_BODY].duration_ms_min = value; break;
        case PARAM_ID_BODY_DURATION_MS_MAX: current_config.class_configs[BUBBLE_CLASS_SUSTAIN_BODY].duration_ms_max = value; break;
        case PARAM_ID_RNG_SEED: current_config.rng_seed = (uint32_t)value; break;
        case PARAM_ID_MIX_DRY_GAIN: master_dry_gain = value; break;
        case PARAM_ID_MIX_WET_GAIN: master_wet_gain = value; break;
        default:
            return;
    }

    // Keep WASM as inspection/demo plumbing only per docs/SONIC_PARITY_CONTRACT.md:
    // no platform-local DSP behavior is introduced here.
    SoundBubbles_UpdateConfig(&engine, &current_config);
    engine.master_dry_gain = master_dry_gain;
    engine.master_wet_gain = master_wet_gain;
}

EMSCRIPTEN_KEEPALIVE
uintptr_t wasm_alloc(size_t size) {
    return (uintptr_t)malloc(size);
}

EMSCRIPTEN_KEEPALIVE
void wasm_free(uintptr_t ptr) {
    free((void*)ptr);
}

EMSCRIPTEN_KEEPALIVE
float wasm_get_envelope() {
    return engine.env_follower_state;
}

EMSCRIPTEN_KEEPALIVE
int wasm_get_state() {
    return (int)engine.engine_state;
}

EMSCRIPTEN_KEEPALIVE
int wasm_get_active_voices() {
    int count = 0;
    for (int i = 0; i < BUBBLES_MAX_VOICES; i++) {
        if (engine.voices[i].state != 0) count++;
    }
    return count;
}
