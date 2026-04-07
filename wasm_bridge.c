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
        if (!g_delay) {
            abort();
        }
    }
    EngineConfig_t cfg = default_config();
    SoundBubbles_Init(&g_engine, g_delay, &cfg);
    g_initialized = 1;
}

void wasm_reset(void) {
    if (!g_initialized) {
        abort();
    }
    SoundBubbles_Reset(&g_engine);
}

void wasm_process(float* input, float* output_l, float* output_r, int32_t frames) {
    if (!g_initialized) {
        abort();
    }
    SoundBubbles_ProcessBlock(&g_engine, input, output_l, output_r, frames);
}

void wasm_set_param(int32_t param_id, float value) {
    if (!g_initialized) {
        abort();
    }

    switch (param_id) {
        case 0:
            g_engine.master_dry_gain = value;
            break;
        case 1:
            g_engine.master_wet_gain = value;
            break;
        case 2:
        case 3:
        case 4:
        case 5:
        case 6: {
            EngineConfig_t cfg = g_engine.config;
            if (param_id == 2) cfg.density_burst = value;
            else if (param_id == 3) cfg.density_sustain = value;
            else if (param_id == 4) cfg.density_decay = value;
            else if (param_id == 5) cfg.duck_burst_level = value;
            else if (param_id == 6) cfg.transient_delta = value;
            SoundBubbles_UpdateConfig(&g_engine, &cfg);
            break;
        }
        default:
            break;
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
    void* ptr = malloc((size_t)size);
    if (!ptr) {
        abort();
    }
    return ptr;
}

void wasm_free(void* ptr) {
    free(ptr);
}
