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
    cfg.rng_seed = 42u;

    // Semantic read regions (distance behind write head in samples).
    cfg.attack_region.min_offset_samples = 441;   // 10ms
    cfg.attack_region.max_offset_samples = 3528;  // 80ms
    cfg.body_region.min_offset_samples = 3528;    // 80ms
    cfg.body_region.max_offset_samples = 11025;   // 250ms
    cfg.memory_region.min_offset_samples = 11025; // 250ms
    cfg.memory_region.max_offset_samples = 39690; // 900ms

    // Class Configs
    cfg.class_configs[BUBBLE_CLASS_MICRO_ATTACK].duration_ms_min = 5.0f;
    cfg.class_configs[BUBBLE_CLASS_MICRO_ATTACK].duration_ms_max = 15.0f;
    cfg.class_configs[BUBBLE_CLASS_MICRO_ATTACK].window_type = WINDOW_TYPE_HANN;

    cfg.class_configs[BUBBLE_CLASS_SHORT_INTERMEDIATE].duration_ms_min = 20.0f;
    cfg.class_configs[BUBBLE_CLASS_SHORT_INTERMEDIATE].duration_ms_max = 50.0f;
    cfg.class_configs[BUBBLE_CLASS_SHORT_INTERMEDIATE].window_type = WINDOW_TYPE_HANN;

    cfg.class_configs[BUBBLE_CLASS_SUSTAIN_BODY].duration_ms_min = 80.0f;
    cfg.class_configs[BUBBLE_CLASS_SUSTAIN_BODY].duration_ms_max = 200.0f;
    cfg.class_configs[BUBBLE_CLASS_SUSTAIN_BODY].window_type = WINDOW_TYPE_TUKEY_LIKE;

    return cfg;
}

// --- Verification & Assertions ---
static void ValidateEngineState(SoundBubblesEngine_t* e) {
    assert(e->engine_state >= ENGINE_STATE_SILENCE && e->engine_state <= ENGINE_STATE_SPARSE_DECAY);
    assert(e->smoothed_ducking_gain >= 0.0f && e->smoothed_ducking_gain <= 1.0f);
    assert(e->pending_spawn_count >= 0 && e->pending_spawn_count <= BUBBLES_PENDING_SPAWN_CAPACITY);
    assert(e->pending_spawn_head >= 0 && e->pending_spawn_head < BUBBLES_PENDING_SPAWN_CAPACITY);

    for (int i = 0; i < BUBBLES_MAX_VOICES; i++) {
        assert(e->voices[i].state >= VOICE_STATE_INACTIVE && e->voices[i].state <= VOICE_STATE_PREEMPT_FADING);
        if (e->voices[i].state == VOICE_STATE_PREEMPT_FADING) {
            assert(e->voices[i].fade_counter >= 0 && e->voices[i].fade_counter <= BUBBLES_FADE_SAMPLES);
        }
    }
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

static void ValidateNoSampleDiscontinuities(const float* out_l, const float* out_r, int num_samples, float max_allowed_step) {
    float max_step = 0.0f;

    for (int i = 1; i < num_samples; i++) {
        float step_l = fabsf(out_l[i] - out_l[i - 1]);
        float step_r = fabsf(out_r[i] - out_r[i - 1]);
        if (step_l > max_step) max_step = step_l;
        if (step_r > max_step) max_step = step_r;
    }

    printf("  Saturation max adjacent-sample step: %.9g (limit %.9g)\n", max_step, max_allowed_step);
    assert(max_step <= max_allowed_step);
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


static void RunMonoCenterCrosstalkCase(TestVectorType_t type, const char* label) {
    EngineConfig_t config = GetBaselineConfig();
    config.stereo_width = 0.0f;
    config.attack_pan_spread = 1.0f;
    config.sustain_pan_spread = 1.0f;
    config.sustain_diffusion_enable = 0;

    SoundBubbles_Init(&engine, delay_buffer_memory, &config);
    engine.master_dry_gain = 0.0f;
    engine.master_wet_gain = 1.0f;

    int total_samples = NUM_SAMPLES + NUM_DRAIN_SAMPLES;
    float* in_buffer = (float*)malloc(total_samples * sizeof(float));
    float* out_l_buffer = (float*)malloc(total_samples * sizeof(float));
    float* out_r_buffer = (float*)malloc(total_samples * sizeof(float));

    if (!in_buffer || !out_l_buffer || !out_r_buffer) {
        printf("Error: Malloc failed for mono-center crosstalk test.\n");
        free(in_buffer); free(out_l_buffer); free(out_r_buffer);
        exit(1);
    }

    GenerateTestVector(type, in_buffer, NUM_SAMPLES);
    for (int i = NUM_SAMPLES; i < total_samples; i++) {
        in_buffer[i] = 0.0f;
    }

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

    float peak_val = 0.0f;
    float max_diff = 0.0f;
    ValidateAndTrackOutput(out_l_buffer, out_r_buffer, total_samples, &peak_val);
    for (int i = 0; i < total_samples; i++) {
        float diff = fabsf(out_l_buffer[i] - out_r_buffer[i]);
        if (diff > max_diff) max_diff = diff;
    }

    printf("  %s mono-center max L/R diff: %.9g (wet peak %.9g)\n", label, max_diff, peak_val);
    assert(peak_val > 0.0001f);
    assert(max_diff <= 0.00001f);

    free(in_buffer);
    free(out_l_buffer);
    free(out_r_buffer);
}

static void RunMonoCenterCrosstalkTest(void) {
    printf("Running mono-center L/R filter crosstalk test...\n");
    RunMonoCenterCrosstalkCase(TEST_VECTOR_PLUCKED_TONE, "Attack-biased pluck");
    RunMonoCenterCrosstalkCase(TEST_VECTOR_SUSTAINED_SINE, "Sustain-biased sine");
}


// --- Main Execution Runners ---

static void RunTest(TestVectorType_t type, const char* out_filename) {
    printf("Running fixed-block test vector %d...\n", type);

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

static void RunVoiceSaturationContinuityTest(void) {
    printf("Running saturated voice continuity test...\n");

    EngineConfig_t config = GetBaselineConfig();
    config.burst_immediate_count = BUBBLES_MAX_VOICES;
    config.density_burst = 2400.0f;
    config.density_sustain = 1800.0f;
    config.density_decay = 1200.0f;
    config.droplet_enable = 1;
    config.droplet_probability = 1.0f;
    config.droplet_gain = 0.7f;
    config.droplet_length_scale = 1.0f;
    config.class_configs[BUBBLE_CLASS_MICRO_ATTACK].duration_ms_min = 60.0f;
    config.class_configs[BUBBLE_CLASS_MICRO_ATTACK].duration_ms_max = 90.0f;
    config.class_configs[BUBBLE_CLASS_SHORT_INTERMEDIATE].duration_ms_min = 90.0f;
    config.class_configs[BUBBLE_CLASS_SHORT_INTERMEDIATE].duration_ms_max = 140.0f;
    config.class_configs[BUBBLE_CLASS_SUSTAIN_BODY].duration_ms_min = 160.0f;
    config.class_configs[BUBBLE_CLASS_SUSTAIN_BODY].duration_ms_max = 260.0f;

    SoundBubbles_Init(&engine, delay_buffer_memory, &config);
    engine.master_dry_gain = 0.0f;
    engine.master_wet_gain = 1.0f;

    int total_samples = SAMPLE_RATE * 3;
    float* in_buffer = (float*)malloc(total_samples * sizeof(float));
    float* out_l_buffer = (float*)malloc(total_samples * sizeof(float));
    float* out_r_buffer = (float*)malloc(total_samples * sizeof(float));

    if (!in_buffer || !out_l_buffer || !out_r_buffer) {
        printf("Error: Malloc failed for saturated continuity test.\n");
        free(in_buffer); free(out_l_buffer); free(out_r_buffer);
        exit(1);
    }

    for (int i = 0; i < total_samples; i++) {
        float t = (float)i / SAMPLE_RATE;
        float transient = (i % (SAMPLE_RATE / 20) == 0) ? 0.9f : 0.0f;
        in_buffer[i] = 0.45f * sinf(2.0f * 3.14159f * 330.0f * t) + transient;
    }

    int saturated_ticks = 0;
    int pending_ticks = 0;
    for (int offset = 0; offset < total_samples; offset += BLOCK_SIZE) {
        int chunk = BLOCK_SIZE;
        if (offset + chunk > total_samples) {
            chunk = total_samples - offset;
        }

        SoundBubbles_ProcessBlock(&engine, &in_buffer[offset], &out_l_buffer[offset], &out_r_buffer[offset], chunk);
        ValidateEngineState(&engine);

        if (engine.metrics_last_block.active_voices == BUBBLES_MAX_VOICES) {
            saturated_ticks++;
        }
        if (engine.pending_spawn_count > 0) {
            pending_ticks++;
        }
    }

    float peak_val = 0.0f;
    ValidateAndTrackOutput(out_l_buffer, out_r_buffer, total_samples, &peak_val);
    ValidateNoSampleDiscontinuities(out_l_buffer, out_r_buffer, total_samples, 0.85f);
    assert(saturated_ticks > 0);
    assert(pending_ticks > 0);
    printf("  Saturated ticks: %d, pending ticks: %d, wet peak %.9g\n", saturated_ticks, pending_ticks, peak_val);

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

    // Stereo bus filter state isolation validation
    RunMonoCenterCrosstalkTest();

    // Saturated voice stealing/pending-spawn continuity validation
    RunVoiceSaturationContinuityTest();

    printf("All tests completed successfully. No assertions failed.\n");
    return 0;
}
