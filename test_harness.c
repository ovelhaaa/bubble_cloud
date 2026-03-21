#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <assert.h>
#include "sound_bubbles_dsp.h"

// --- Configuration ---
#define SAMPLE_RATE 44100
#define BLOCK_SIZE 32
#define TEST_DURATION_SEC 5
#define NUM_SAMPLES (SAMPLE_RATE * TEST_DURATION_SEC)

// Memory for the 2-second delay buffer
static int16_t delay_buffer_memory[BUBBLES_BUFFER_SIZE_SAMPLES];
static SoundBubblesEngine_t engine;

// --- Test Vectors ---

typedef enum {
    TEST_VECTOR_SILENCE,
    TEST_VECTOR_IMPULSE,
    TEST_VECTOR_PLUCKED_TONE,
    TEST_VECTOR_REPEATED_TRANSIENTS,
    TEST_VECTOR_SUSTAINED_SINE
} TestVectorType_t;

void GenerateTestVector(TestVectorType_t type, float* buffer, int num_samples) {
    for (int i = 0; i < num_samples; i++) {
        buffer[i] = 0.0f; // Default silence

        switch (type) {
            case TEST_VECTOR_SILENCE:
                break;
            case TEST_VECTOR_IMPULSE:
                if (i == 4410) { // Impulse at 100ms
                    buffer[i] = 1.0f;
                }
                break;
            case TEST_VECTOR_PLUCKED_TONE: {
                // Pluck at 0.5s
                int start_sample = (int)(0.5f * SAMPLE_RATE);
                if (i >= start_sample) {
                    float t = (float)(i - start_sample) / SAMPLE_RATE;
                    float env = expf(-t * 3.0f); // Decay
                    buffer[i] = env * sinf(2.0f * 3.14159f * 440.0f * t);
                }
                break;
            }
            case TEST_VECTOR_REPEATED_TRANSIENTS: {
                // Impulse train every 200ms
                int period = (int)(0.2f * SAMPLE_RATE);
                if (i % period == 0 && i > 0) {
                    buffer[i] = 1.0f;
                }
                break;
            }
            case TEST_VECTOR_SUSTAINED_SINE: {
                float t = (float)i / SAMPLE_RATE;
                buffer[i] = 0.5f * sinf(2.0f * 3.14159f * 220.0f * t);
                break;
            }
        }
    }
}

// --- Engine Configuration Baseline ---

EngineConfig_t GetBaselineConfig() {
    EngineConfig_t cfg = {0};

    cfg.noise_floor = 0.001f;
    cfg.tracking_thresh = 0.01f;
    cfg.sustain_thresh = 0.1f;
    cfg.transient_delta = 0.05f;

    cfg.duck_burst_level = 0.2f;
    cfg.duck_attack_coef = 0.99f;
    cfg.duck_release_coef = 0.999f;

    cfg.burst_duration_ticks = 10;
    cfg.burst_immediate_count = 3;

    cfg.density_burst = 50.0f;
    cfg.density_sustain = 15.0f;
    cfg.density_decay = 5.0f;

    cfg.sustain_read_center_offset_samples = -22050; // -0.5s

    // Class Configs
    cfg.class_configs[BUBBLE_CLASS_MICRO_ATTACK].duration_ms_min = 5.0f;
    cfg.class_configs[BUBBLE_CLASS_MICRO_ATTACK].duration_ms_max = 15.0f;
    cfg.class_configs[BUBBLE_CLASS_MICRO_ATTACK].offset_samples = -441; // -10ms
    cfg.class_configs[BUBBLE_CLASS_MICRO_ATTACK].jitter_samples = 100;
    cfg.class_configs[BUBBLE_CLASS_MICRO_ATTACK].window_type = WINDOW_TYPE_HANN;

    cfg.class_configs[BUBBLE_CLASS_SHORT_INTERMEDIATE].duration_ms_min = 20.0f;
    cfg.class_configs[BUBBLE_CLASS_SHORT_INTERMEDIATE].duration_ms_max = 50.0f;
    cfg.class_configs[BUBBLE_CLASS_SHORT_INTERMEDIATE].offset_samples = -4410; // -100ms
    cfg.class_configs[BUBBLE_CLASS_SHORT_INTERMEDIATE].jitter_samples = 500;
    cfg.class_configs[BUBBLE_CLASS_SHORT_INTERMEDIATE].window_type = WINDOW_TYPE_HANN;

    cfg.class_configs[BUBBLE_CLASS_SUSTAIN_BODY].duration_ms_min = 80.0f;
    cfg.class_configs[BUBBLE_CLASS_SUSTAIN_BODY].duration_ms_max = 200.0f;
    cfg.class_configs[BUBBLE_CLASS_SUSTAIN_BODY].offset_samples = -22050; // -500ms
    cfg.class_configs[BUBBLE_CLASS_SUSTAIN_BODY].jitter_samples = 4410;
    cfg.class_configs[BUBBLE_CLASS_SUSTAIN_BODY].window_type = WINDOW_TYPE_TUKEY_LIKE;

    return cfg;
}

// --- Verification & Assertions ---

void ValidateOutput(const float* out_l, const float* out_r, int num_samples) {
    for (int i = 0; i < num_samples; i++) {
        // Assert no NaN or Inf
        assert(!isnan(out_l[i]) && !isinf(out_l[i]));
        assert(!isnan(out_r[i]) && !isinf(out_r[i]));
        // Optional bounds check, output might exceed [-1, 1] slightly before soft-clipping, but should be bounded.
        assert(out_l[i] >= -10.0f && out_l[i] <= 10.0f);
    }
}

void ValidateEngineState(SoundBubblesEngine_t* engine) {
    assert(engine->engine_state >= ENGINE_STATE_SILENCE && engine->engine_state <= ENGINE_STATE_SPARSE_DECAY);
    assert(engine->smoothed_ducking_gain >= 0.0f && engine->smoothed_ducking_gain <= 1.0f);
}

void ValidateVoicesNotStuck(SoundBubblesEngine_t* engine) {
    // Verifies that no voices are stuck playing indefinitely when they shouldn't be
    for (int i = 0; i < BUBBLES_MAX_VOICES; i++) {
        assert(engine->voices[i].state == VOICE_STATE_INACTIVE);
    }
}

// --- Main Execution Runner ---

void RunTest(TestVectorType_t type, const char* out_filename) {
    printf("Running test vector %d...\n", type);

    // Deterministic seed
    srand(42);

    EngineConfig_t config = GetBaselineConfig();
    SoundBubbles_Init(&engine, delay_buffer_memory, &config);
    engine.master_dry_gain = 0.5f;
    engine.master_wet_gain = 0.5f;

    float* in_buffer = (float*)malloc(NUM_SAMPLES * sizeof(float));
    float* out_l_buffer = (float*)malloc(NUM_SAMPLES * sizeof(float));
    float* out_r_buffer = (float*)malloc(NUM_SAMPLES * sizeof(float));

    GenerateTestVector(type, in_buffer, NUM_SAMPLES);

    // Process in blocks
    int num_blocks = NUM_SAMPLES / BLOCK_SIZE;
    for (int i = 0; i < num_blocks; i++) {
        int offset = i * BLOCK_SIZE;
        SoundBubbles_ProcessBlock(&engine, &in_buffer[offset], &out_l_buffer[offset], &out_r_buffer[offset], BLOCK_SIZE);

        // Assert state validity every block
        ValidateEngineState(&engine);
    }

    // Process remaining samples if any
    int remaining = NUM_SAMPLES % BLOCK_SIZE;
    if (remaining > 0) {
        int offset = num_blocks * BLOCK_SIZE;
        SoundBubbles_ProcessBlock(&engine, &in_buffer[offset], &out_l_buffer[offset], &out_r_buffer[offset], remaining);
    }

    ValidateOutput(out_l_buffer, out_r_buffer, NUM_SAMPLES);

    // Assert no voices stuck forever for tests that return to silence
    if (type == TEST_VECTOR_SILENCE || type == TEST_VECTOR_IMPULSE || type == TEST_VECTOR_PLUCKED_TONE) {
        ValidateVoicesNotStuck(&engine);
    }

    // Write interleaved raw float32 file
    FILE* f = fopen(out_filename, "wb");
    if (f) {
        for (int i = 0; i < NUM_SAMPLES; i++) {
            fwrite(&out_l_buffer[i], sizeof(float), 1, f);
            fwrite(&out_r_buffer[i], sizeof(float), 1, f);
        }
        fclose(f);
        printf("Wrote %s (Raw Interleaved Stereo Float32, 44100Hz)\n", out_filename);
    } else {
        printf("Failed to write %s\n", out_filename);
    }

    free(in_buffer);
    free(out_l_buffer);
    free(out_r_buffer);
}

int main() {
    printf("Starting Sound Bubbles DSP Offline Test Harness...\n");

    RunTest(TEST_VECTOR_SILENCE, "test_out_silence.raw");
    RunTest(TEST_VECTOR_IMPULSE, "test_out_impulse.raw");
    RunTest(TEST_VECTOR_PLUCKED_TONE, "test_out_pluck.raw");
    RunTest(TEST_VECTOR_REPEATED_TRANSIENTS, "test_out_transients.raw");
    RunTest(TEST_VECTOR_SUSTAINED_SINE, "test_out_sustain.raw");

    printf("All tests completed successfully. No assertions failed.\n");
    return 0;
}
