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
    current_config.sustain_read_center_offset_samples = 22050;

    // Micro
    current_config.class_configs[BUBBLE_CLASS_MICRO_ATTACK].duration_ms_min = 5.0f;
    current_config.class_configs[BUBBLE_CLASS_MICRO_ATTACK].duration_ms_max = 15.0f;
    current_config.class_configs[BUBBLE_CLASS_MICRO_ATTACK].offset_samples = 441;
    current_config.class_configs[BUBBLE_CLASS_MICRO_ATTACK].jitter_samples = 100;
    current_config.class_configs[BUBBLE_CLASS_MICRO_ATTACK].window_type = WINDOW_TYPE_HANN;

    // Short
    current_config.class_configs[BUBBLE_CLASS_SHORT_INTERMEDIATE].duration_ms_min = 20.0f;
    current_config.class_configs[BUBBLE_CLASS_SHORT_INTERMEDIATE].duration_ms_max = 50.0f;
    current_config.class_configs[BUBBLE_CLASS_SHORT_INTERMEDIATE].offset_samples = 4410;
    current_config.class_configs[BUBBLE_CLASS_SHORT_INTERMEDIATE].jitter_samples = 500;
    current_config.class_configs[BUBBLE_CLASS_SHORT_INTERMEDIATE].window_type = WINDOW_TYPE_HANN;

    // Body
    current_config.class_configs[BUBBLE_CLASS_SUSTAIN_BODY].duration_ms_min = 80.0f;
    current_config.class_configs[BUBBLE_CLASS_SUSTAIN_BODY].duration_ms_max = 200.0f;
    current_config.class_configs[BUBBLE_CLASS_SUSTAIN_BODY].offset_samples = 0;
    current_config.class_configs[BUBBLE_CLASS_SUSTAIN_BODY].jitter_samples = 4410;
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
        case 0: current_config.noise_floor = value; break;
        case 1: current_config.tracking_thresh = value; break;
        case 2: current_config.sustain_thresh = value; break;
        case 3: current_config.transient_delta = value; break;
        case 4: current_config.duck_burst_level = value; break;
        case 5: current_config.duck_attack_coef = value; break;
        case 6: current_config.duck_release_coef = value; break;
        case 7: current_config.burst_duration_ticks = (int32_t)value; break;
        case 8: current_config.burst_immediate_count = (int32_t)value; break;
        case 9: current_config.density_burst = value; break;
        case 10: current_config.density_sustain = value; break;
        case 11: current_config.density_decay = value; break;
        case 12: current_config.sustain_read_center_offset_samples = (int32_t)value; break;

        // Micro
        case 13: current_config.class_configs[BUBBLE_CLASS_MICRO_ATTACK].duration_ms_min = value; break;
        case 14: current_config.class_configs[BUBBLE_CLASS_MICRO_ATTACK].duration_ms_max = value; break;
        case 15: current_config.class_configs[BUBBLE_CLASS_MICRO_ATTACK].offset_samples = (int32_t)value; break;
        case 16: current_config.class_configs[BUBBLE_CLASS_MICRO_ATTACK].jitter_samples = (int32_t)value; break;

        // Short
        case 17: current_config.class_configs[BUBBLE_CLASS_SHORT_INTERMEDIATE].duration_ms_min = value; break;
        case 18: current_config.class_configs[BUBBLE_CLASS_SHORT_INTERMEDIATE].duration_ms_max = value; break;
        case 19: current_config.class_configs[BUBBLE_CLASS_SHORT_INTERMEDIATE].offset_samples = (int32_t)value; break;
        case 20: current_config.class_configs[BUBBLE_CLASS_SHORT_INTERMEDIATE].jitter_samples = (int32_t)value; break;

        // Body
        case 21: current_config.class_configs[BUBBLE_CLASS_SUSTAIN_BODY].duration_ms_min = value; break;
        case 22: current_config.class_configs[BUBBLE_CLASS_SUSTAIN_BODY].duration_ms_max = value; break;
        case 23: current_config.class_configs[BUBBLE_CLASS_SUSTAIN_BODY].offset_samples = (int32_t)value; break;
        case 24: current_config.class_configs[BUBBLE_CLASS_SUSTAIN_BODY].jitter_samples = (int32_t)value; break;

        // Mix
        case 25: master_dry_gain = value; break;
        case 26: master_wet_gain = value; break;
    }

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
