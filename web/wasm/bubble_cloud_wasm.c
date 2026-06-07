#include <emscripten.h>
#include <stdint.h>
#include <stdlib.h>
#include "../../core/engine/bubble_engine.h"

static BubbleEngine_t engine;
static int16_t* delay_buffer = NULL;

EMSCRIPTEN_KEEPALIVE
void wasm_init() {
    if (delay_buffer == NULL) {
        delay_buffer = (int16_t*)calloc(BUBBLES_BUFFER_SIZE_SAMPLES, sizeof(int16_t));
        if (delay_buffer == NULL) {
            return;
        }
    }

    bubble_engine_init(&engine, delay_buffer, NULL);
}

EMSCRIPTEN_KEEPALIVE
void wasm_reset() {
    bubble_engine_reset(&engine);
}

EMSCRIPTEN_KEEPALIVE
void wasm_process(uintptr_t in_ptr, uintptr_t out_l_ptr, uintptr_t out_r_ptr, int num_samples) {
    const float* in_mono = (const float*)in_ptr;
    float* out_left = (float*)out_l_ptr;
    float* out_right = (float*)out_r_ptr;

    bubble_engine_process(&engine, in_mono, out_left, out_right, num_samples);
}

EMSCRIPTEN_KEEPALIVE
void wasm_set_param(int param_id, float value) {
    // Keep WASM as inspection/demo plumbing only per docs/SONIC_PARITY_CONTRACT.md:
    // no platform-local DSP behavior is introduced here.
    (void)bubble_engine_set_parameter(&engine, (BubbleEngineParameterId_t)param_id, value);
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
    float value = 0.0f;
    (void)bubble_engine_get_parameter(&engine, BUBBLE_ENGINE_PARAM_RUNTIME_ENVELOPE, &value);
    return value;
}

EMSCRIPTEN_KEEPALIVE
int wasm_get_state() {
    float value = 0.0f;
    (void)bubble_engine_get_parameter(&engine, BUBBLE_ENGINE_PARAM_RUNTIME_STATE, &value);
    return (int)value;
}

EMSCRIPTEN_KEEPALIVE
int wasm_get_active_voices() {
    float value = 0.0f;
    (void)bubble_engine_get_parameter(&engine, BUBBLE_ENGINE_PARAM_RUNTIME_ACTIVE_VOICES, &value);
    return (int)value;
}
