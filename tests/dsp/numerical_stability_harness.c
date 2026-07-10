#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/bubble_engine.h"

#define TEST_FRAMES (44100 * 2)
#define MAX_ACCEPTABLE_ABS_SAMPLE 1.01f

typedef enum {
    VECTOR_SILENCE = 0,
    VECTOR_IMPULSE,
    VECTOR_SINE,
    VECTOR_LOW_NOISE,
    VECTOR_DENSE_TRANSIENTS,
    VECTOR_COUNT
} TestVector;

typedef struct {
    const char* name;
    EngineConfig_t config;
    float dry;
    float wet;
} StabilityPreset;

static uint32_t rng_step(uint32_t* state) {
    *state = (*state * 1664525u) + 1013904223u;
    return *state;
}

static float rand_signed(uint32_t* state) {
    return ((float)(rng_step(state) >> 8) / 8388607.5f) - 1.0f;
}

static void fill_vector(TestVector vector, float* input, int frames) {
    uint32_t rng = 0x12345678u;
    memset(input, 0, (size_t)frames * sizeof(float));
    for (int i = 0; i < frames; ++i) {
        float t = (float)i / (float)44100;
        switch (vector) {
            case VECTOR_SILENCE:
                input[i] = 0.0f;
                break;
            case VECTOR_IMPULSE:
                input[i] = (i == 44100 / 10) ? 1.0f : 0.0f;
                break;
            case VECTOR_SINE:
                input[i] = 0.35f * sinf(2.0f * 3.14159265358979323846f * 440.0f * t);
                break;
            case VECTOR_LOW_NOISE:
                input[i] = 0.0005f * rand_signed(&rng);
                break;
            case VECTOR_DENSE_TRANSIENTS:
                if ((i % 113) == 0 || (i % 197) == 0) {
                    input[i] = (i & 1) ? -0.9f : 0.9f;
                } else {
                    input[i] = 0.02f * sinf(2.0f * 3.14159265358979323846f * 880.0f * t);
                }
                break;
            default:
                input[i] = 0.0f;
                break;
        }
    }
}

static StabilityPreset make_baseline(void) {
    StabilityPreset preset;
    preset.name = "baseline";
    bubble_engine_default_config(&preset.config);
    preset.dry = 1.0f;
    preset.wet = 0.8f;
    return preset;
}

static StabilityPreset make_sparse_extreme(void) {
    StabilityPreset preset = make_baseline();
    preset.name = "sparse_extreme";
    preset.config.density_burst = 0.0f;
    preset.config.density_sustain = 0.0f;
    preset.config.density_decay = 0.0f;
    preset.config.burst_immediate_count = 0;
    preset.config.duck_burst_level = 0.0f;
    preset.config.final_limiter_ceiling_db = -3.0f;
    preset.wet = 0.0f;
    return preset;
}

static StabilityPreset make_dense_extreme(void) {
    StabilityPreset preset = make_baseline();
    preset.name = "dense_extreme";
    preset.config.density_burst = 150.0f;
    preset.config.density_sustain = 120.0f;
    preset.config.density_decay = 80.0f;
    preset.config.burst_immediate_count = 3;
    preset.config.wet_drive = 2.0f;
    preset.config.wet_clip_amount = 0.8f;
    preset.config.final_limiter_ceiling_db = -0.25f;
    preset.config.active_voice_limit = BUBBLE_QUALITY_DEFAULT_VOICE_LIMIT;
    preset.dry = 1.0f;
    preset.wet = 1.2f;
    return preset;
}

static int run_case(const StabilityPreset* preset, TestVector vector) {
    static float input[TEST_FRAMES];
    static float left[TEST_FRAMES];
    static float right[TEST_FRAMES];
    static int16_t delay[88200];

    fill_vector(vector, input, TEST_FRAMES);
    memset(left, 0, sizeof(left));
    memset(right, 0, sizeof(right));
    memset(delay, 0, sizeof(delay));

    BubbleEngine_t engine;
    bubble_engine_init(&engine, delay, &preset->config);
    bubble_engine_set_parameter(&engine, BUBBLE_ENGINE_PARAM_MIX_DRY_GAIN, preset->dry);
    bubble_engine_set_parameter(&engine, BUBBLE_ENGINE_PARAM_MIX_WET_GAIN, preset->wet);

    for (int offset = 0; offset < TEST_FRAMES; offset += BUBBLES_BLOCK_SIZE) {
        int chunk = BUBBLES_BLOCK_SIZE;
        if (offset + chunk > TEST_FRAMES) chunk = TEST_FRAMES - offset;
        bubble_engine_process(&engine, &input[offset], &left[offset], &right[offset], chunk);
    }

    float peak = 0.0f;
    for (int i = 0; i < TEST_FRAMES; ++i) {
        if (!isfinite(left[i]) || !isfinite(right[i])) {
            fprintf(stderr, "non-finite sample: preset=%s vector=%d frame=%d\n", preset->name, (int)vector, i);
            return 1;
        }
        float abs_l = fabsf(left[i]);
        float abs_r = fabsf(right[i]);
        if (abs_l > peak) peak = abs_l;
        if (abs_r > peak) peak = abs_r;
    }

    float clip_count = 0.0f;
    bubble_engine_get_parameter(&engine, BUBBLE_ENGINE_PARAM_RUNTIME_CLIP_COUNT, &clip_count);
    if (clip_count > 0.0f || peak > MAX_ACCEPTABLE_ABS_SAMPLE) {
        fprintf(stderr, "peak/clipping limit exceeded: preset=%s vector=%d peak=%g clips=%g\n", preset->name, (int)vector, peak, clip_count);
        return 1;
    }
    return 0;
}

int main(void) {
    StabilityPreset presets[] = {make_baseline(), make_sparse_extreme(), make_dense_extreme()};
    for (size_t p = 0; p < sizeof(presets) / sizeof(presets[0]); ++p) {
        for (int v = 0; v < VECTOR_COUNT; ++v) {
            if (run_case(&presets[p], (TestVector)v) != 0) {
                return 1;
            }
        }
    }
    return 0;
}
