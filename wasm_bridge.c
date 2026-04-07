#include "sound_bubbles_dsp.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static SoundBubblesEngine_t g_engine;
static int16_t* g_delay = NULL;
static int g_initialized = 0;

static EngineConfig_t default_config(void) {
    EngineConfig_t c;
    memset(&c, 0, sizeof(c));
    c.noise_floor = 0.01f;
    c.tracking_thresh = 0.03f;
    c.sustain_thresh = 0.10f;
    c.transient_delta = 0.02f;

    c.duck_burst_level = 0.55f;
    c.duck_attack_coef = 0.22f;
    c.duck_release_coef = 0.015f;

    c.burst_duration_ticks = 8;
    c.burst_immediate_count = 3;

    c.density_burst = 36.0f;
    c.density_sustain = 20.0f;
    c.density_decay = 7.5f;

    c.sustain_read_center_offset_samples = 3300;

    c.class_configs[BUBBLE_CLASS_MICRO_ATTACK] = (BubbleClassConfig_t){12.0f, 34.0f, 100, 140, WINDOW_TYPE_HANN};
    c.class_configs[BUBBLE_CLASS_SHORT_INTERMEDIATE] = (BubbleClassConfig_t){45.0f, 110.0f, 1100, 420, WINDOW_TYPE_TUKEY_LIKE};
    c.class_configs[BUBBLE_CLASS_SUSTAIN_BODY] = (BubbleClassConfig_t){120.0f, 260.0f, 3300, 920, WINDOW_TYPE_TUKEY_LIKE};
    return c;
}

void wasm_init(void) {
    if (!g_delay) {
        g_delay = (int16_t*)malloc(sizeof(int16_t) * BUBBLES_BUFFER_SIZE_SAMPLES);
    }
    EngineConfig_t cfg = default_config();
    SoundBubbles_Init(&g_engine, g_delay, &cfg);
    g_initialized = 1;
}

void wasm_reset(void) {
    wasm_init();
}

void wasm_process(float* input, float* output_l, float* output_r, int32_t frames) {
    if (!g_initialized) {
        wasm_init();
    }
    SoundBubbles_ProcessBlock(&g_engine, input, output_l, output_r, frames);
}

void wasm_set_param(int32_t param_id, float value) {
    if (!g_initialized) {
        wasm_init();
    }

    switch (param_id) {
        case 0: g_engine.master_dry_gain = value; break;
        case 1: g_engine.master_wet_gain = value; break;
        case 2: g_engine.config.density_burst = value; break;
        case 3: g_engine.config.density_sustain = value; break;
        case 4: g_engine.config.density_decay = value; break;
        case 5: g_engine.config.duck_burst_level = value; break;
        case 6: g_engine.config.transient_delta = value; break;
        default: break;
    }
}

float wasm_get_envelope(void) {
    return g_engine.env_follower_state;
}

int32_t wasm_get_state(void) {
    return (int32_t)g_engine.engine_state;
}

int32_t wasm_get_active_voices(void) {
    int active = 0;
    for (int i = 0; i < BUBBLES_MAX_VOICES; ++i) {
        if (g_engine.voices[i].state != VOICE_STATE_INACTIVE) {
            active++;
        }
    }
    return active;
}

void* wasm_alloc(int32_t size) {
    return malloc((size_t)size);
}

void wasm_free(void* ptr) {
    free(ptr);
}
