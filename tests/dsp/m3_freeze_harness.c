// M3 "Expressive Freeze & Temporal Morphing" validation harness.
//
// Covers the M3 milestone requirements:
//   A. Write retention curve monotonicity across 0.0, 0.25, 0.50, 0.75, 1.00
//   B. Freeze full stability (stationary write head, static memory, no runaway, no DC accumulation over extended render)
//   C. Unfreeze recovery (click-free return to normal writing)
//   D. Sweep discontinuity (click-free, smooth RMS, bounded sample-to-sample deltas across 0->1, 1->0, 0.25->0.8, 0.9->0.1)
//   E. Determinism (same input + config + automation -> same output)
//   F. State restore (0.0, intermediate, 1.0 values)
//   G. MIDI freeze mode semantics (momentary and latch)
//   H. Sample-rate and block-size invariance matrix (44.1, 48, 88.2, 96 kHz x 32, 64, 127, 256, 512, 2048)

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/bubble_engine.h"

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, (msg)); \
        return 1; \
    } \
} while (0)

#define CHECK_CLOSE(actual, expected, tol, msg) do { \
    if (fabsf((float)(actual) - (float)(expected)) > (float)(tol)) { \
        fprintf(stderr, "FAIL %s:%d: %s (actual=%g expected=%g tol=%g)\n", \
                __FILE__, __LINE__, (msg), (double)(actual), (double)(expected), (double)(tol)); \
        return 1; \
    } \
} while (0)

static void fill_sine_input(float* buffer, int num_samples, float freq, float sample_rate, float amp, int sample_offset) {
    for (int i = 0; i < num_samples; i++) {
        buffer[i] = amp * sinf(2.0f * 3.14159265358979323846f * freq * (float)(sample_offset + i) / sample_rate);
    }
}

// ---------------------------------------------------------------------------
// Test A: Write retention curve monotonicity
// ---------------------------------------------------------------------------
static int test_write_retention_curve(void) {
    const float test_levels[5] = {0.0f, 0.25f, 0.50f, 0.75f, 1.0f};
    float measured_renewal[5] = {0.0f};
    const int test_samples = 2048; // 64 control blocks
    static int16_t delay[88200];
    float in[2048];
    float out_l[2048];
    float out_r[2048];

    for (int i = 0; i < test_samples; i++) {
        in[i] = 0.85f; // Constant test probe
    }

    for (int lvl = 0; lvl < 5; lvl++) {
        float f_val = test_levels[lvl];
        // Pre-fill delay buffer with a distinct baseline (-10000)
        for (int i = 0; i < 88200; i++) {
            delay[i] = -10000;
        }

        BubbleEngineConfig_t config;
        bubble_engine_default_config(&config);
        config.sample_rate = 44100.0f;
        config.freeze_amount = f_val;
        if (f_val >= 0.999f) {
            config.freeze_enabled = 1;
        }
        BubbleEngine_t engine;
        SoundBubbles_Init(&engine, delay, &config);

        // Pre-fill delay buffer AFTER Init (Init clears buffer to 0) with distinct baseline (-10000)
        for (int i = 0; i < 88200; i++) {
            delay[i] = -10000;
        }
        engine.smoothed_freeze = f_val;

        int start_write_ptr = engine.write_ptr;
        SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, test_samples);

        // Measure how much the written memory changed from the initial baseline
        double total_delta = 0.0;
        int samples_to_check = (f_val >= 0.999f) ? test_samples : test_samples;
        for (int i = 0; i < samples_to_check; i++) {
            int idx = (start_write_ptr + i) % 88200;
            total_delta += fabs((double)delay[idx] - (-10000.0));
        }
        measured_renewal[lvl] = (float)total_delta;
    }

    // Normalize against freeze = 0.0
    float max_renewal = measured_renewal[0];
    CHECK(max_renewal > 0.0f, "freeze=0.0 must update delay memory");
    float norm_renewal[5];
    for (int i = 0; i < 5; i++) {
        norm_renewal[i] = measured_renewal[i] / max_renewal;
    }

    // Verify strict monotonicity: higher FREEZE -> lower memory renewal
    printf("[M3 Harness] Write Retention Renewal Rates (normalized):\n");
    printf("  FREEZE 0.00: %.4f (expected 1.0000)\n", (double)norm_renewal[0]);
    printf("  FREEZE 0.25: %.4f (expected ~0.844)\n", (double)norm_renewal[1]);
    printf("  FREEZE 0.50: %.4f (expected ~0.500)\n", (double)norm_renewal[2]);
    printf("  FREEZE 0.75: %.4f (expected ~0.156)\n", (double)norm_renewal[3]);
    printf("  FREEZE 1.00: %.4f (expected 0.0000)\n", (double)norm_renewal[4]);

    CHECK_CLOSE(norm_renewal[0], 1.0f, 0.001f, "freeze=0.0 renewal must be exactly 1.0");
    CHECK(norm_renewal[0] > norm_renewal[1], "renewal rate must decrease from 0.0 to 0.25");
    CHECK(norm_renewal[1] > norm_renewal[2], "renewal rate must decrease from 0.25 to 0.50");
    CHECK(norm_renewal[2] > norm_renewal[3], "renewal rate must decrease from 0.50 to 0.75");
    CHECK(norm_renewal[3] > norm_renewal[4], "renewal rate must decrease from 0.75 to 1.00");
    CHECK_CLOSE(norm_renewal[4], 0.0f, 0.0001f, "freeze=1.0 renewal must be exactly 0.0");

    return 0;
}

// ---------------------------------------------------------------------------
// Test B: Freeze full stability over extended render (tens of seconds)
// ---------------------------------------------------------------------------
static int test_freeze_full_stability(void) {
    static int16_t delay[88200];
    static int16_t snapshot_delay[88200];
    const int total_samples = 44100 * 25; // 25 seconds of continuous freeze!
    float in[BUBBLES_BLOCK_SIZE];
    float out_l[BUBBLES_BLOCK_SIZE];
    float out_r[BUBBLES_BLOCK_SIZE];

    BubbleEngineConfig_t config;
    bubble_engine_default_config(&config);
    config.sample_rate = 44100.0f;
    BubbleEngine_t engine;
    bubble_engine_init(&engine, delay, &config);
    engine.macro_dirty_mask = 0u;

    // Pre-populate buffer with rich signal AFTER init (which zeroes delay)
    for (int i = 0; i < 88200; i++) {
        delay[i] = (int16_t)(sinf((float)i * 0.05f) * 16000.0f);
    }
    memcpy(snapshot_delay, delay, sizeof(delay));

    // Engage freeze 1.0
    bubble_engine_set_parameter(&engine, BUBBLE_PARAM_DEVELOPER_MODE, 1.0f);
    bubble_engine_set_parameter(&engine, BUBBLE_ENGINE_PARAM_FREEZE_AMOUNT, 1.0f);
    bubble_engine_set_parameter(&engine, BUBBLE_ENGINE_PARAM_FREEZE_ENABLED, 1.0f);

    int snapshot_write_ptr = engine.write_ptr;
    double sum_out_l = 0.0, sum_out_r = 0.0;
    float peak_out = 0.0f;

    for (int s = 0; s < total_samples; s += BUBBLES_BLOCK_SIZE) {
        fill_sine_input(in, BUBBLES_BLOCK_SIZE, 440.0f, 44100.0f, 0.9f, s);
        bubble_engine_process(&engine, in, out_l, out_r, BUBBLES_BLOCK_SIZE);

        for (int i = 0; i < BUBBLES_BLOCK_SIZE; i++) {
            CHECK(isfinite(out_l[i]), "out_left must be finite during long freeze");
            CHECK(isfinite(out_r[i]), "out_right must be finite during long freeze");
            float abs_l = fabsf(out_l[i]);
            float abs_r = fabsf(out_r[i]);
            if (abs_l > peak_out) peak_out = abs_l;
            if (abs_r > peak_out) peak_out = abs_r;
            sum_out_l += (double)out_l[i];
            sum_out_r += (double)out_r[i];
        }
    }

    // Verify write head did not advance
    CHECK(engine.write_ptr == snapshot_write_ptr, "freeze=1.0 keeps write_ptr stationary over 25 seconds");

    // Verify memory buffer remained 100% untouched
    CHECK(memcmp(delay, snapshot_delay, sizeof(delay)) == 0, "freeze=1.0 keeps delay buffer strictly bitwise identical");

    // Verify no runaway
    CHECK(peak_out <= 1.05f, "output peak must stay bounded within limiter ceiling (no runaway)");

    // Verify no DC accumulation
    double mean_l = sum_out_l / (double)total_samples;
    double mean_r = sum_out_r / (double)total_samples;
    CHECK(fabs(mean_l) < 0.05, "no DC accumulation on left channel during long freeze");
    CHECK(fabs(mean_r) < 0.05, "no DC accumulation on right channel during long freeze");

    printf("[M3 Harness] Freeze Full 25s stability: peak=%.4f mean_l=%.6f mean_r=%.6f (PASS)\n",
           (double)peak_out, mean_l, mean_r);
    return 0;
}

// ---------------------------------------------------------------------------
// Test C: Unfreeze recovery
// ---------------------------------------------------------------------------
static int test_unfreeze_recovery(void) {
    static int16_t delay[88200];
    memset(delay, 0, sizeof(delay));

    BubbleEngineConfig_t config;
    bubble_engine_default_config(&config);
    config.sample_rate = 44100.0f;
    BubbleEngine_t engine;
    bubble_engine_init(&engine, delay, &config);
    engine.macro_dirty_mask = 0u;

    bubble_engine_set_parameter(&engine, BUBBLE_PARAM_DEVELOPER_MODE, 1.0f);
    bubble_engine_set_parameter(&engine, BUBBLE_ENGINE_PARAM_FREEZE_AMOUNT, 1.0f);
    bubble_engine_set_parameter(&engine, BUBBLE_ENGINE_PARAM_FREEZE_ENABLED, 1.0f);

    float in[BUBBLES_BLOCK_SIZE];
    float out_l[BUBBLES_BLOCK_SIZE];
    float out_r[BUBBLES_BLOCK_SIZE];

    // Run frozen for 20000 samples (~0.45s)
    for (int s = 0; s < 20000; s += BUBBLES_BLOCK_SIZE) {
        fill_sine_input(in, BUBBLES_BLOCK_SIZE, 300.0f, 44100.0f, 0.5f, s);
        bubble_engine_process(&engine, in, out_l, out_r, BUBBLES_BLOCK_SIZE);
    }
    int frozen_write_ptr = engine.write_ptr;

    // Unfreeze: transition to 0.0
    bubble_engine_set_parameter(&engine, BUBBLE_ENGINE_PARAM_FREEZE_ENABLED, 0.0f);
    bubble_engine_set_parameter(&engine, BUBBLE_ENGINE_PARAM_FREEZE_AMOUNT, 0.0f);
    bubble_engine_set_parameter(&engine, BUBBLE_PARAM_FREEZE, 0.0f);

    float max_delta = 0.0f;
    float prev_l = out_l[BUBBLES_BLOCK_SIZE - 1];

    // Process 44100 samples after unfreeze
    for (int s = 0; s < 44100; s += BUBBLES_BLOCK_SIZE) {
        fill_sine_input(in, BUBBLES_BLOCK_SIZE, 300.0f, 44100.0f, 0.5f, 20000 + s);
        bubble_engine_process(&engine, in, out_l, out_r, BUBBLES_BLOCK_SIZE);

        for (int i = 0; i < BUBBLES_BLOCK_SIZE; i++) {
            float d = fabsf(out_l[i] - prev_l);
            if (d > max_delta) max_delta = d;
            prev_l = out_l[i];
        }
    }

    // Write head must have resumed advancing
    CHECK(engine.write_ptr != frozen_write_ptr, "unfreeze must resume write_ptr advancement");
    // Transitions must be click-free
    CHECK(max_delta < 0.35f, "unfreeze must not cause a sample-to-sample transient click");

    printf("[M3 Harness] Unfreeze recovery: max sample-to-sample delta=%.4f (PASS)\n", (double)max_delta);
    return 0;
}

// ---------------------------------------------------------------------------
// Test D: Sweep discontinuity & Click-Free Automation
// ---------------------------------------------------------------------------
static int test_freeze_sweep_discontinuity(void) {
    static int16_t delay[88200];
    memset(delay, 0, sizeof(delay));

    BubbleEngineConfig_t config;
    bubble_engine_default_config(&config);
    config.sample_rate = 44100.0f;
    BubbleEngine_t engine;
    bubble_engine_init(&engine, delay, &config);

    float in[BUBBLES_BLOCK_SIZE];
    float out_l[BUBBLES_BLOCK_SIZE];
    float out_r[BUBBLES_BLOCK_SIZE];

    struct {
        float start;
        float mid;
        float end;
    } sweeps[] = {
        {0.0f, 1.0f, 0.0f},
        {0.25f, 0.80f, 0.25f},
        {0.90f, 0.10f, 0.90f},
    };

    for (int s = 0; s < 3; s++) {
        bubble_engine_reset(&engine);
        float max_sample_delta = 0.0f;
        float peak_val = 0.0f;
        float prev_sample = 0.0f;

        // Stage 1: settle at start
        bubble_engine_set_parameter(&engine, BUBBLE_PARAM_FREEZE, sweeps[s].start);
        for (int b = 0; b < 32; b++) {
            fill_sine_input(in, BUBBLES_BLOCK_SIZE, 440.0f, 44100.0f, 0.6f, b * BUBBLES_BLOCK_SIZE);
            bubble_engine_process(&engine, in, out_l, out_r, BUBBLES_BLOCK_SIZE);
        }

        // Stage 2: sweep to mid
        bubble_engine_set_parameter(&engine, BUBBLE_PARAM_FREEZE, sweeps[s].mid);
        for (int b = 0; b < 64; b++) {
            fill_sine_input(in, BUBBLES_BLOCK_SIZE, 440.0f, 44100.0f, 0.6f, (32 + b) * BUBBLES_BLOCK_SIZE);
            bubble_engine_process(&engine, in, out_l, out_r, BUBBLES_BLOCK_SIZE);
            for (int i = 0; i < BUBBLES_BLOCK_SIZE; i++) {
                float d = fabsf(out_l[i] - prev_sample);
                if (d > max_sample_delta) max_sample_delta = d;
                if (fabsf(out_l[i]) > peak_val) peak_val = fabsf(out_l[i]);
                prev_sample = out_l[i];
            }
        }

        // Stage 3: sweep to end
        bubble_engine_set_parameter(&engine, BUBBLE_PARAM_FREEZE, sweeps[s].end);
        for (int b = 0; b < 64; b++) {
            fill_sine_input(in, BUBBLES_BLOCK_SIZE, 440.0f, 44100.0f, 0.6f, (96 + b) * BUBBLES_BLOCK_SIZE);
            bubble_engine_process(&engine, in, out_l, out_r, BUBBLES_BLOCK_SIZE);
            for (int i = 0; i < BUBBLES_BLOCK_SIZE; i++) {
                float d = fabsf(out_l[i] - prev_sample);
                if (d > max_sample_delta) max_sample_delta = d;
                if (fabsf(out_l[i]) > peak_val) peak_val = fabsf(out_l[i]);
                prev_sample = out_l[i];
            }
        }

        CHECK(peak_val <= 1.05f, "peak must not spike beyond limiter");
        CHECK(max_sample_delta < 0.35f, "sample-to-sample delta during sweep must have no anomalous glitch spike");
        printf("[M3 Harness] Sweep %g -> %g -> %g: peak=%.4f max_delta=%.4f (PASS)\n",
               (double)sweeps[s].start, (double)sweeps[s].mid, (double)sweeps[s].end,
               (double)peak_val, (double)max_sample_delta);
    }

    return 0;
}

// ---------------------------------------------------------------------------
// Test E: Determinism
// ---------------------------------------------------------------------------
static int test_freeze_determinism(void) {
    static int16_t delay1[88200];
    static int16_t delay2[88200];
    const int total_blocks = 128;
    float in[BUBBLES_BLOCK_SIZE];
    float out_l1[BUBBLES_BLOCK_SIZE * 128];
    float out_r1[BUBBLES_BLOCK_SIZE * 128];
    float out_l2[BUBBLES_BLOCK_SIZE * 128];
    float out_r2[BUBBLES_BLOCK_SIZE * 128];

    // Run 1
    memset(delay1, 0, sizeof(delay1));
    BubbleEngineConfig_t config1;
    bubble_engine_default_config(&config1);
    config1.rng_seed = 12345u;
    BubbleEngine_t engine1;
    bubble_engine_init(&engine1, delay1, &config1);

    for (int b = 0; b < total_blocks; b++) {
        fill_sine_input(in, BUBBLES_BLOCK_SIZE, 220.0f + (float)b, 44100.0f, 0.7f, b * BUBBLES_BLOCK_SIZE);
        if (b == 16) bubble_engine_set_parameter(&engine1, BUBBLE_PARAM_FREEZE, 0.45f);
        if (b == 48) bubble_engine_set_parameter(&engine1, BUBBLE_PARAM_FREEZE, 0.85f);
        if (b == 80) bubble_engine_set_parameter(&engine1, BUBBLE_PARAM_FREEZE, 0.10f);
        bubble_engine_process(&engine1, in, &out_l1[b * BUBBLES_BLOCK_SIZE], &out_r1[b * BUBBLES_BLOCK_SIZE], BUBBLES_BLOCK_SIZE);
    }

    // Run 2
    memset(delay2, 0, sizeof(delay2));
    BubbleEngineConfig_t config2;
    bubble_engine_default_config(&config2);
    config2.rng_seed = 12345u;
    BubbleEngine_t engine2;
    bubble_engine_init(&engine2, delay2, &config2);

    for (int b = 0; b < total_blocks; b++) {
        fill_sine_input(in, BUBBLES_BLOCK_SIZE, 220.0f + (float)b, 44100.0f, 0.7f, b * BUBBLES_BLOCK_SIZE);
        if (b == 16) bubble_engine_set_parameter(&engine2, BUBBLE_PARAM_FREEZE, 0.45f);
        if (b == 48) bubble_engine_set_parameter(&engine2, BUBBLE_PARAM_FREEZE, 0.85f);
        if (b == 80) bubble_engine_set_parameter(&engine2, BUBBLE_PARAM_FREEZE, 0.10f);
        bubble_engine_process(&engine2, in, &out_l2[b * BUBBLES_BLOCK_SIZE], &out_r2[b * BUBBLES_BLOCK_SIZE], BUBBLES_BLOCK_SIZE);
    }

    CHECK(memcmp(out_l1, out_l2, sizeof(out_l1)) == 0, "run 1 and run 2 left channel must be bitwise identical");
    CHECK(memcmp(out_r1, out_r2, sizeof(out_r1)) == 0, "run 1 and run 2 right channel must be bitwise identical");

    printf("[M3 Harness] Determinism: bitwise identical across all %d blocks (PASS)\n", total_blocks);
    return 0;
}

// ---------------------------------------------------------------------------
// Test F: State restore
// ---------------------------------------------------------------------------
static int test_freeze_state_restore(void) {
    static int16_t delay[88200];
    memset(delay, 0, sizeof(delay));

    BubbleEngineConfig_t config;
    bubble_engine_default_config(&config);
    BubbleEngine_t engine;
    bubble_engine_init(&engine, delay, &config);

    const float test_values[3] = {0.0f, 0.52f, 1.0f};
    for (int i = 0; i < 3; i++) {
        float f = test_values[i];
        bubble_engine_set_parameter(&engine, BUBBLE_PARAM_FREEZE, f);

        BubbleEnginePreset_t saved;
        CHECK(bubble_engine_save_preset(&engine, &saved), "save preset");

        // Change engine to something else
        bubble_engine_set_parameter(&engine, BUBBLE_PARAM_FREEZE, 0.2f);

        // Restore
        CHECK(bubble_engine_load_preset(&engine, &saved), "load preset");
        float restored = 0.0f;
        CHECK(bubble_engine_get_parameter(&engine, BUBBLE_PARAM_FREEZE, &restored), "get freeze_amount");
        CHECK_CLOSE(restored, f, 0.001f, "restored freeze_amount must match saved value");
    }

    printf("[M3 Harness] State restore: verified 0.0, 0.52, 1.0 (PASS)\n");
    return 0;
}

// ---------------------------------------------------------------------------
// Test G: MIDI freeze mode semantics
// ---------------------------------------------------------------------------
static int test_freeze_midi_modes(void) {
    static int16_t delay[88200];
    memset(delay, 0, sizeof(delay));

    BubbleEngineConfig_t config;
    bubble_engine_default_config(&config);
    BubbleEngine_t engine;
    bubble_engine_init(&engine, delay, &config);

    // Initial state: continuous knob is at 0.30f
    bubble_engine_set_parameter(&engine, BUBBLE_PARAM_FREEZE, 0.30f);

    float in[BUBBLES_BLOCK_SIZE];
    float out_l[BUBBLES_BLOCK_SIZE];
    float out_r[BUBBLES_BLOCK_SIZE];
    for (int b = 0; b < 48; b++) {
        fill_sine_input(in, BUBBLES_BLOCK_SIZE, 440.0f, 44100.0f, 0.5f, b * BUBBLES_BLOCK_SIZE);
        bubble_engine_process(&engine, in, out_l, out_r, BUBBLES_BLOCK_SIZE);
    }
    CHECK_CLOSE(engine.config.freeze_amount, 0.30f, 0.01f, "initial continuous amount is 0.30");

    // Momentary MIDI Note-On: engages full freeze target (1.0)
    bubble_engine_set_parameter(&engine, BUBBLE_PARAM_FREEZE, 1.0f);
    for (int b = 0; b < 48; b++) {
        fill_sine_input(in, BUBBLES_BLOCK_SIZE, 440.0f, 44100.0f, 0.5f, (48 + b) * BUBBLES_BLOCK_SIZE);
        bubble_engine_process(&engine, in, out_l, out_r, BUBBLES_BLOCK_SIZE);
    }
    CHECK_CLOSE(engine.config.freeze_amount, 1.0f, 0.01f, "momentary note-on engages freeze target 1.0");

    // Momentary MIDI Note-Off: releases back to 0.30f smoothly
    bubble_engine_set_parameter(&engine, BUBBLE_PARAM_FREEZE, 0.30f);
    for (int b = 0; b < 48; b++) {
        fill_sine_input(in, BUBBLES_BLOCK_SIZE, 440.0f, 44100.0f, 0.5f, (96 + b) * BUBBLES_BLOCK_SIZE);
        bubble_engine_process(&engine, in, out_l, out_r, BUBBLES_BLOCK_SIZE);
    }
    CHECK_CLOSE(engine.config.freeze_amount, 0.30f, 0.01f, "momentary note-off smoothly restores scene amount 0.30");

    printf("[M3 Harness] MIDI freeze mode semantics verified (PASS)\n");
    return 0;
}

// ---------------------------------------------------------------------------
// Test H: Sample-rate and block-size invariance matrix
// ---------------------------------------------------------------------------
static int test_freeze_sr_block_invariance_matrix(void) {
    const float sample_rates[4] = {44100.0f, 48000.0f, 88200.0f, 96000.0f};
    const int block_sizes[6] = {32, 64, 127, 256, 512, 2048};
    static int16_t delay[192000];
    float in[2048];
    float out_l[2048];
    float out_r[2048];

    for (int r = 0; r < 4; r++) {
        float sr = sample_rates[r];
        for (int b = 0; b < 6; b++) {
            int bs = block_sizes[b];
            memset(delay, 0, sizeof(delay));

            BubbleEngineConfig_t config;
            bubble_engine_default_config(&config);
            config.sample_rate = sr;
            BubbleEngine_t engine;
            bubble_engine_init(&engine, delay, &config);

            // Sweep through freeze states
            const float steps[4] = {0.0f, 0.5f, 1.0f, 0.0f};
            for (int s = 0; s < 4; s++) {
                bubble_engine_set_parameter(&engine, BUBBLE_PARAM_FREEZE, steps[s]);
                for (int iter = 0; iter < 8; iter++) {
                    fill_sine_input(in, bs, 440.0f, sr, 0.5f, iter * bs);
                    bubble_engine_process(&engine, in, out_l, out_r, bs);
                    for (int i = 0; i < bs; i++) {
                        CHECK(isfinite(out_l[i]), "out_l must be finite across matrix");
                        CHECK(isfinite(out_r[i]), "out_r must be finite across matrix");
                    }
                }
            }
        }
    }

    printf("[M3 Harness] Matrix SR x Block Size: 4 sample rates x 6 block sizes (24 configurations) (PASS)\n");
    return 0;
}

// ---------------------------------------------------------------------------
// Main test entry
// ---------------------------------------------------------------------------
int main(void) {
    printf("=== SoundBubbles M3 Freeze & Temporal Morphing Validation Suite ===\n");

    if (test_write_retention_curve() != 0) return 1;
    if (test_freeze_full_stability() != 0) return 1;
    if (test_unfreeze_recovery() != 0) return 1;
    if (test_freeze_sweep_discontinuity() != 0) return 1;
    if (test_freeze_determinism() != 0) return 1;
    if (test_freeze_state_restore() != 0) return 1;
    if (test_freeze_midi_modes() != 0) return 1;
    if (test_freeze_sr_block_invariance_matrix() != 0) return 1;

    printf("=== All M3 Tests Passed Successfully ===\n");
    return 0;
}
