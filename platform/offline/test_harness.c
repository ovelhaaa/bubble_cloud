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
#define DRAIN_DURATION_SEC 1
#define NUM_SAMPLES (SAMPLE_RATE * TEST_DURATION_SEC)
#define NUM_DRAIN_SAMPLES (SAMPLE_RATE * DRAIN_DURATION_SEC)
#define SAFETY_BOUND 5.0f

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

static void GenerateTestVector(TestVectorType_t type, float* buffer, int num_samples) {
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
static EngineConfig_t GetBaselineConfig() {
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

    // Positive distance behind write head
    cfg.sustain_read_center_offset_samples = 22050; // 0.5s

    // Class Configs (Positive offsets)
    cfg.class_configs[BUBBLE_CLASS_MICRO_ATTACK].duration_ms_min = 5.0f;
    cfg.class_configs[BUBBLE_CLASS_MICRO_ATTACK].duration_ms_max = 15.0f;
    cfg.class_configs[BUBBLE_CLASS_MICRO_ATTACK].offset_samples = 441; // 10ms
    cfg.class_configs[BUBBLE_CLASS_MICRO_ATTACK].jitter_samples = 100;
    cfg.class_configs[BUBBLE_CLASS_MICRO_ATTACK].window_type = WINDOW_TYPE_HANN;

    cfg.class_configs[BUBBLE_CLASS_SHORT_INTERMEDIATE].duration_ms_min = 20.0f;
    cfg.class_configs[BUBBLE_CLASS_SHORT_INTERMEDIATE].duration_ms_max = 50.0f;
    cfg.class_configs[BUBBLE_CLASS_SHORT_INTERMEDIATE].offset_samples = 4410; // 100ms
    cfg.class_configs[BUBBLE_CLASS_SHORT_INTERMEDIATE].jitter_samples = 500;
    cfg.class_configs[BUBBLE_CLASS_SHORT_INTERMEDIATE].window_type = WINDOW_TYPE_HANN;

    cfg.class_configs[BUBBLE_CLASS_SUSTAIN_BODY].duration_ms_min = 80.0f;
    cfg.class_configs[BUBBLE_CLASS_SUSTAIN_BODY].duration_ms_max = 200.0f;
    cfg.class_configs[BUBBLE_CLASS_SUSTAIN_BODY].offset_samples = 22050; // 500ms
    cfg.class_configs[BUBBLE_CLASS_SUSTAIN_BODY].jitter_samples = 4410;
    cfg.class_configs[BUBBLE_CLASS_SUSTAIN_BODY].window_type = WINDOW_TYPE_TUKEY_LIKE;

    return cfg;
}

// --- Verification & Assertions ---
static void ValidateEngineState(SoundBubblesEngine_t* e) {
    assert(e->engine_state >= ENGINE_STATE_SILENCE && e->engine_state <= ENGINE_STATE_SPARSE_DECAY);
    assert(e->smoothed_ducking_gain >= 0.0f && e->smoothed_ducking_gain <= 1.0f);
}

static void ValidateVoicesNotStuck(SoundBubblesEngine_t* e) {
    for (int i = 0; i < BUBBLES_MAX_VOICES; i++) {
        assert(e->voices[i].state == VOICE_STATE_INACTIVE);
    }
}

static void ValidateAndTrackOutput(const float* out_l, const float* out_r, int num_samples, float* peak) {
    for (int i = 0; i < num_samples; i++) {
        assert(!isnan(out_l[i]) && !isinf(out_l[i]));
        assert(!isnan(out_r[i]) && !isinf(out_r[i]));

        assert(out_l[i] >= -SAFETY_BOUND && out_l[i] <= SAFETY_BOUND);
        assert(out_r[i] >= -SAFETY_BOUND && out_r[i] <= SAFETY_BOUND);

        float abs_l = fabsf(out_l[i]);
        float abs_r = fabsf(out_r[i]);
        if (abs_l > *peak) *peak = abs_l;
        if (abs_r > *peak) *peak = abs_r;
    }
}

static void WriteRawFile(const char* filename, const float* out_l, const float* out_r, int num_samples) {
    FILE* f = fopen(filename, "wb");
    if (f) {
        for (int i = 0; i < num_samples; i++) {
            fwrite(&out_l[i], sizeof(float), 1, f);
            fwrite(&out_r[i], sizeof(float), 1, f);
        }
        fclose(f);
    } else {
        printf("Failed to write %s\n", filename);
    }
}

// --- Main Execution Runners ---

static void RunTest(TestVectorType_t type, const char* out_filename) {
    printf("Running fixed-block test vector %d...\n", type);
    srand(42);

    EngineConfig_t config = GetBaselineConfig();
    SoundBubbles_Init(&engine, delay_buffer_memory, &config);
    engine.master_dry_gain = 0.5f;
    engine.master_wet_gain = 0.5f;

    int total_samples = NUM_SAMPLES;
    bool needs_drain = (type == TEST_VECTOR_SILENCE || type == TEST_VECTOR_IMPULSE || type == TEST_VECTOR_PLUCKED_TONE);
    if (needs_drain) {
        total_samples += NUM_DRAIN_SAMPLES;
    }

    float* in_buffer = (float*)malloc(total_samples * sizeof(float));
    float* out_l_buffer = (float*)malloc(total_samples * sizeof(float));
    float* out_r_buffer = (float*)malloc(total_samples * sizeof(float));

    if (!in_buffer || !out_l_buffer || !out_r_buffer) {
        printf("Error: Malloc failed for fixed-block test.\n");
        free(in_buffer); free(out_l_buffer); free(out_r_buffer);
        exit(1);
    }

    GenerateTestVector(type, in_buffer, NUM_SAMPLES);
    if (needs_drain) {
        for (int i = NUM_SAMPLES; i < total_samples; i++) {
            in_buffer[i] = 0.0f; // Drain period
        }
    }

    float peak_val = 0.0f;
    int num_blocks = total_samples / BLOCK_SIZE;

    for (int i = 0; i < num_blocks; i++) {
        int offset = i * BLOCK_SIZE;
        SoundBubbles_ProcessBlock(&engine, &in_buffer[offset], &out_l_buffer[offset], &out_r_buffer[offset], BLOCK_SIZE);
        ValidateEngineState(&engine);
    }

    int remaining = total_samples % BLOCK_SIZE;
    if (remaining > 0) {
        int offset = num_blocks * BLOCK_SIZE;
        SoundBubbles_ProcessBlock(&engine, &in_buffer[offset], &out_l_buffer[offset], &out_r_buffer[offset], remaining);
        ValidateEngineState(&engine);
    }

    ValidateAndTrackOutput(out_l_buffer, out_r_buffer, total_samples, &peak_val);

    if (needs_drain) {
        ValidateVoicesNotStuck(&engine);
    }

    printf("  Peak Output: %f\n", peak_val);
    WriteRawFile(out_filename, out_l_buffer, out_r_buffer, total_samples);

    free(in_buffer);
    free(out_l_buffer);
    free(out_r_buffer);
}

static void RunTestIrregularChunks(TestVectorType_t type, const char* out_filename) {
    printf("Running irregular-chunk test vector %d...\n", type);
    srand(42);

    EngineConfig_t config = GetBaselineConfig();
    SoundBubbles_Init(&engine, delay_buffer_memory, &config);
    engine.master_dry_gain = 0.5f;
    engine.master_wet_gain = 0.5f;

    int total_samples = NUM_SAMPLES;
    bool needs_drain = (type == TEST_VECTOR_SILENCE || type == TEST_VECTOR_IMPULSE || type == TEST_VECTOR_PLUCKED_TONE);
    if (needs_drain) {
        total_samples += NUM_DRAIN_SAMPLES;
    }

    float* in_buffer = (float*)malloc(total_samples * sizeof(float));
    float* out_l_buffer = (float*)malloc(total_samples * sizeof(float));
    float* out_r_buffer = (float*)malloc(total_samples * sizeof(float));

    if (!in_buffer || !out_l_buffer || !out_r_buffer) {
        printf("Error: Malloc failed for irregular-chunk test.\n");
        free(in_buffer); free(out_l_buffer); free(out_r_buffer);
        exit(1);
    }

    GenerateTestVector(type, in_buffer, NUM_SAMPLES);
    if (needs_drain) {
        for (int i = NUM_SAMPLES; i < total_samples; i++) {
            in_buffer[i] = 0.0f;
        }
    }

    float peak_val = 0.0f;
    int chunk_sequence[] = {17, 48, 31, 127, 9, 64};
    int num_sequence_items = sizeof(chunk_sequence) / sizeof(chunk_sequence[0]);
    int seq_idx = 0;
    int processed = 0;

    while (processed < total_samples) {
        int chunk = chunk_sequence[seq_idx];
        if (processed + chunk > total_samples) {
            chunk = total_samples - processed;
        }

        SoundBubbles_ProcessBlock(&engine, &in_buffer[processed], &out_l_buffer[processed], &out_r_buffer[processed], chunk);
        ValidateEngineState(&engine);

        processed += chunk;
        seq_idx = (seq_idx + 1) % num_sequence_items;
    }

    ValidateAndTrackOutput(out_l_buffer, out_r_buffer, total_samples, &peak_val);

    if (needs_drain) {
        ValidateVoicesNotStuck(&engine);
    }

    printf("  Peak Output: %f\n", peak_val);
    WriteRawFile(out_filename, out_l_buffer, out_r_buffer, total_samples);

    free(in_buffer);
    free(out_l_buffer);
    free(out_r_buffer);
}

int main(void) {
    printf("Starting Sound Bubbles DSP Offline Test Harness...\n");

    // Standard fixed-block tests
    RunTest(TEST_VECTOR_SILENCE, "test_out_silence.raw");
    RunTest(TEST_VECTOR_IMPULSE, "test_out_impulse.raw");
    RunTest(TEST_VECTOR_PLUCKED_TONE, "test_out_pluck.raw");
    RunTest(TEST_VECTOR_REPEATED_TRANSIENTS, "test_out_transients.raw");
    RunTest(TEST_VECTOR_SUSTAINED_SINE, "test_out_sustain.raw");

    // Irregular chunk size validation
    RunTestIrregularChunks(TEST_VECTOR_PLUCKED_TONE, "test_out_pluck_irregular.raw");

    printf("All tests completed successfully. No assertions failed.\n");
    return 0;
}
