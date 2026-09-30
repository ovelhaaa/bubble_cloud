// M4C qualification probe: evaluates Float Ring Backend vs Int16 (with and without dither).
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>

#include "engine/bubble_engine.h"

#define MAX_RING_SAMPLES 192000
#define FFT_N 4096

#ifndef M_PI_F
#define M_PI_F 3.14159265358979323846f
#endif

static BubbleRingSample_t g_delay[MAX_RING_SAMPLES];

static float calc_rms(const float* b, int count) {
    if (count <= 0) return 0.0f;
    double sum = 0.0;
    for (int i = 0; i < count; i++) {
        sum += (double)b[i] * (double)b[i];
    }
    return (float)sqrt(sum / (double)count);
}

static float calc_db(float rms) {
    if (rms < 1.0e-9f) return -180.0f;
    return 20.0f * log10f(rms);
}

static float calc_centroid(const float* x, int n, float sr) {
    double num = 0.0, den = 0.0;
    for (int i = 0; i < n; i++) {
        double v = fabs((double)x[i]);
        double freq = (double)i * (double)sr / (double)n;
        num += freq * v;
        den += v;
    }
    return (den > 1e-9) ? (float)(num / den) : 0.0f;
}

static float calc_correlation(const float* l, const float* r, int n) {
    double sum_l2 = 0.0, sum_r2 = 0.0, sum_lr = 0.0;
    for (int i = 0; i < n; i++) {
        sum_l2 += (double)l[i] * (double)l[i];
        sum_r2 += (double)r[i] * (double)r[i];
        sum_lr += (double)l[i] * (double)r[i];
    }
    double denom = sqrt(sum_l2 * sum_r2);
    return (denom > 1e-12) ? (float)(sum_lr / denom) : 0.0f;
}

static uint64_t fnv1a_hash(const void* data, size_t bytes) {
    const uint8_t* ptr = (const uint8_t*)data;
    uint64_t hash = 14695981039346656037ULL;
    for (size_t i = 0; i < bytes; i++) {
        hash ^= (uint64_t)ptr[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

// Generate sound materials
static void gen_material(const char* type, float* buf, int n, float sr, float amp) {
    if (strcmp(type, "pluck") == 0) {
        // Fast attack, exponential decay
        for (int i = 0; i < n; i++) {
            float env = expf(-(float)i / (0.05f * sr));
            buf[i] = amp * env * sinf(2.0f * M_PI_F * 440.0f * (float)i / sr);
        }
    } else if (strcmp(type, "harmonic") == 0) {
        // 440 + 880 + 1320
        for (int i = 0; i < n; i++) {
            float t = (float)i / sr;
            float s = 0.6f * sinf(2.0f * M_PI_F * 440.0f * t)
                    + 0.3f * sinf(2.0f * M_PI_F * 880.0f * t)
                    + 0.1f * sinf(2.0f * M_PI_F * 1320.0f * t);
            buf[i] = amp * s;
        }
    } else if (strcmp(type, "transient") == 0) {
        // Sharp 5ms noise burst + 1kHz click
        for (int i = 0; i < n; i++) {
            if (i < (int)(0.005f * sr)) {
                float r = (float)rand() / (float)RAND_MAX * 2.0f - 1.0f;
                buf[i] = amp * (0.7f * r + 0.3f * sinf(2.0f * M_PI_F * 1000.0f * (float)i / sr));
            } else {
                buf[i] = 0.0f;
            }
        }
    } else { // "pad"
        // Smooth swell & sustain
        for (int i = 0; i < n; i++) {
            float t = (float)i / sr;
            float env = (i < (int)(0.2f * sr)) ? (float)i / (0.2f * sr) : 1.0f;
            float s = 0.5f * sinf(2.0f * M_PI_F * 220.0f * t)
                    + 0.3f * sinf(2.0f * M_PI_F * 277.18f * t)
                    + 0.2f * sinf(2.0f * M_PI_F * 329.63f * t);
            buf[i] = amp * env * s;
        }
    }
}

// Low-level quantization grid test
// Prints CSV: dbfs,ring_rms_db,tail_rms_db,feedback_survived
static void run_quantization_grid(int dither_enabled) {
    const float levels_db[] = {-48.0f, -60.0f, -72.0f, -84.0f, -90.0f, -96.0f};
    const float sr = 44100.0f;
    const int tone_len = (int)(0.5f * sr);
    const int tail_len = (int)(4.0f * sr);
    const int total_samples = tone_len + tail_len;

    printf("dbfs,ring_rms_db,tail_rms_db,feedback_survived\n");

    for (int idx = 0; idx < 6; idx++) {
        float db = levels_db[idx];
        float amp = powf(10.0f, db / 20.0f);

        BubbleEngineConfig_t config;
        bubble_engine_default_config(&config);
        config.sample_rate = sr;
        config.rng_seed = 0x12345678u;
        config.density_decay = 80.0f;

        memset(g_delay, 0, sizeof(g_delay));
        BubbleEngine_t engine;
        bubble_engine_init(&engine, g_delay, &config);
        SoundBubbles_SetFeedbackEnabled(&engine, true);
        bubble_engine_set_dither_enabled(&engine, dither_enabled);

        float in[BUBBLES_BLOCK_SIZE];
        float out_l[BUBBLES_BLOCK_SIZE];
        float out_r[BUBBLES_BLOCK_SIZE];

        double tail_energy = 0.0;
        int tail_samples = 0;
        float captured_ring_rms = 0.0f;
        float max_fb_comp = 0.0f;

        for (int s = 0; s < total_samples; s += BUBBLES_BLOCK_SIZE) {
            for (int i = 0; i < BUBBLES_BLOCK_SIZE; i++) {
                int pos = s + i;
                if (pos < tone_len) {
                    in[i] = amp * sinf(2.0f * M_PI_F * 440.0f * (float)pos / sr);
                } else {
                    in[i] = 0.0f;
                }
            }
            bubble_engine_process(&engine, in, out_l, out_r, BUBBLES_BLOCK_SIZE);

            if (fabsf(engine.last_write_feedback) > max_fb_comp) {
                max_fb_comp = fabsf(engine.last_write_feedback);
            }

            // Capture ring RMS right after the tone input ends
            if (s + BUBBLES_BLOCK_SIZE >= tone_len && captured_ring_rms == 0.0f) {
                double ring_energy = 0.0;
                for (int i = 0; i < tone_len; i++) {
                    float val = Ring_ReadNormalizedSample(g_delay, i);
                    ring_energy += (double)val * (double)val;
                }
                captured_ring_rms = (float)sqrt(ring_energy / (double)tone_len);
            }

            // Tail measurement: 0.5s to 2.0s
            if (s >= tone_len && s < tone_len + (int)(1.5f * sr)) {
                for (int i = 0; i < BUBBLES_BLOCK_SIZE; i++) {
                    tail_energy += (double)out_l[i] * (double)out_l[i];
                    tail_samples++;
                }
            }
        }

        float tail_rms = (tail_samples > 0) ? (float)sqrt(tail_energy / (double)tail_samples) : 0.0f;
        int survived = (max_fb_comp > 1.0e-7f || tail_rms > 1.0e-7f) ? 1 : 0;

        printf("%.1f,%.2f,%.2f,%d\n", db, calc_db(captured_ring_rms), calc_db(tail_rms), survived);
    }
}

// Repeated regeneration test: 500ms tone -> long Auto-Hold + feedback -> 10-15s tail
// Prints: late_tail_rms_db,spectral_centroid,broadband_noise_db,feedback_energy
static void run_regeneration_test(int dither_enabled) {
    const float sr = 44100.0f;
    const int tone_len = (int)(0.5f * sr);
    const int total_samples = (int)(15.0f * sr);

    BubbleEngineConfig_t config;
    bubble_engine_default_config(&config);
    config.sample_rate = sr;
    config.rng_seed = 0xCAFEBABEu;
    config.density_decay = 70.0f;

    memset(g_delay, 0, sizeof(g_delay));
    BubbleEngine_t engine;
    bubble_engine_init(&engine, g_delay, &config);
    SoundBubbles_SetFeedbackEnabled(&engine, true);
    bubble_engine_set_dither_enabled(&engine, dither_enabled);

    float in[BUBBLES_BLOCK_SIZE];
    float out_l[BUBBLES_BLOCK_SIZE];
    float out_r[BUBBLES_BLOCK_SIZE];

    double late_tail_energy = 0.0;
    int late_tail_samples = 0;
    double total_fb_energy = 0.0;

    float* late_buffer = (float*)malloc(sizeof(float) * (int)(5.0f * sr));
    int late_buf_idx = 0;

    for (int s = 0; s < total_samples; s += BUBBLES_BLOCK_SIZE) {
        for (int i = 0; i < BUBBLES_BLOCK_SIZE; i++) {
            int pos = s + i;
            if (pos < tone_len) {
                in[i] = 0.5f * sinf(2.0f * M_PI_F * 440.0f * (float)pos / sr);
            } else {
                in[i] = 0.0f;
            }
        }
        bubble_engine_process(&engine, in, out_l, out_r, BUBBLES_BLOCK_SIZE);

        // Feedback energy tracked
        total_fb_energy += (double)SoundBubbles_GetFeedbackEnergy(&engine);

        // Late tail window: 10s to 15s
        if (s >= (int)(10.0f * sr) && s < (int)(15.0f * sr)) {
            for (int i = 0; i < BUBBLES_BLOCK_SIZE; i++) {
                late_tail_energy += (double)out_l[i] * (double)out_l[i];
                if (late_buffer && late_buf_idx < (int)(5.0f * sr)) {
                    late_buffer[late_buf_idx++] = out_l[i];
                }
                late_tail_samples++;
            }
        }
    }

    float late_rms = (late_tail_samples > 0) ? (float)sqrt(late_tail_energy / (double)late_tail_samples) : 0.0f;
    float centroid = (late_buf_idx > 0) ? calc_centroid(late_buffer, late_buf_idx, sr) : 0.0f;

    printf("%.2f,%.1f,%.4f\n", calc_db(late_rms), centroid, (float)total_fb_energy);
    if (late_buffer) free(late_buffer);
}

// High-level audio parity across materials: pluck, harmonic, transient, pad
// Prints CSV: material,scenario,rms_db,peak,centroid,stereo_corr
static void run_material_parity(const char* material, const char* scenario, int dither_enabled) {
    const float sr = 44100.0f;
    const int total_blocks = ((int)(3.0f * sr) + BUBBLES_BLOCK_SIZE - 1) / BUBBLES_BLOCK_SIZE;
    const int total_samples = total_blocks * BUBBLES_BLOCK_SIZE;

    BubbleEngineConfig_t config;
    bubble_engine_default_config(&config);
    config.sample_rate = sr;
    config.rng_seed = 0x98765432u;

    bool fb_on = false;
    if (strcmp(scenario, "no_feedback") == 0) {
        fb_on = false;
    } else if (strcmp(scenario, "m4a_tail") == 0) {
        fb_on = false;
        config.density_decay = 80.0f;
    } else { // "m4b_feedback"
        fb_on = true;
        config.density_decay = 60.0f;
    }

    memset(g_delay, 0, sizeof(g_delay));
    BubbleEngine_t engine;
    bubble_engine_init(&engine, g_delay, &config);
    SoundBubbles_SetFeedbackEnabled(&engine, fb_on);
    bubble_engine_set_dither_enabled(&engine, dither_enabled);

    float* in_full = (float*)calloc(total_samples, sizeof(float));
    float* out_l_full = (float*)calloc(total_samples, sizeof(float));
    float* out_r_full = (float*)calloc(total_samples, sizeof(float));

    gen_material(material, in_full, (int)(0.5f * sr), sr, 0.7f);
    for (int i = (int)(0.5f * sr); i < total_samples; i++) in_full[i] = 0.0f;

    float peak = 0.0f;
    for (int s = 0; s < total_samples; s += BUBBLES_BLOCK_SIZE) {
        bubble_engine_process(&engine, &in_full[s], &out_l_full[s], &out_r_full[s], BUBBLES_BLOCK_SIZE);
        for (int i = 0; i < BUBBLES_BLOCK_SIZE; i++) {
            float al = fabsf(out_l_full[s + i]);
            float ar = fabsf(out_r_full[s + i]);
            if (al > peak) peak = al;
            if (ar > peak) peak = ar;
        }
    }

    float rms = calc_rms(out_l_full, total_samples);
    float centroid = calc_centroid(out_l_full, total_samples, sr);
    float corr = calc_correlation(out_l_full, out_r_full, total_samples);

    printf("%s,%s,%.2f,%.4f,%.1f,%.4f\n", material, scenario, calc_db(rms), peak, centroid, corr);

    free(in_full);
    free(out_l_full);
    free(out_r_full);
}

// Freeze bitwise invariance check
static void run_freeze_hash_check(void) {
    BubbleEngineConfig_t config;
    bubble_engine_default_config(&config);
    config.sample_rate = 44100.0f;
    config.freeze_enabled = 1;
    config.freeze_amount = 1.0f;

    memset(g_delay, 0, sizeof(g_delay));
    BubbleEngine_t engine;
    bubble_engine_init(&engine, g_delay, &config);
    engine.macro_dirty_mask = 0u;

    // Populate with distinctive pattern
    for (int i = 0; i < 88200; i++) {
        g_delay[i] = (BubbleRingSample_t)(BUBBLES_RING_SAMPLE_IS_FLOAT ? (sinf((float)i * 0.05f) * 0.45f) : (int16_t)(sinf((float)i * 0.05f) * 15000.0f));
    }

    bubble_engine_set_parameter(&engine, BUBBLE_PARAM_DEVELOPER_MODE, 1.0f);
    bubble_engine_set_parameter(&engine, BUBBLE_ENGINE_PARAM_FREEZE_AMOUNT, 1.0f);
    bubble_engine_set_parameter(&engine, BUBBLE_ENGINE_PARAM_FREEZE_ENABLED, 1.0f);

    size_t ring_bytes = bubble_engine_required_buffer_bytes(44100.0f);
    uint64_t hash_before = fnv1a_hash(g_delay, ring_bytes);

    float in[BUBBLES_BLOCK_SIZE];
    float out_l[BUBBLES_BLOCK_SIZE];
    float out_r[BUBBLES_BLOCK_SIZE];
    for (int i = 0; i < BUBBLES_BLOCK_SIZE; i++) in[i] = 0.5f;

    // Run 25 seconds of frozen audio
    for (int s = 0; s < 44100 * 25; s += BUBBLES_BLOCK_SIZE) {
        bubble_engine_process(&engine, in, out_l, out_r, BUBBLES_BLOCK_SIZE);
    }

    uint64_t hash_after = fnv1a_hash(g_delay, ring_bytes);
    printf("hash_before=0x%016llx,hash_after=0x%016llx,identical=%d\n",
           (unsigned long long)hash_before, (unsigned long long)hash_after, (hash_before == hash_after) ? 1 : 0);
}

// Silence dither noise test
static void run_silence_dither_test(void) {
    BubbleEngineConfig_t config;
    bubble_engine_default_config(&config);
    config.sample_rate = 44100.0f;
    config.rng_seed = 0x12345678u;

    memset(g_delay, 0, sizeof(g_delay));
    BubbleEngine_t engine;
    bubble_engine_init(&engine, g_delay, &config);
    bubble_engine_set_dither_enabled(&engine, 1);

    float in[BUBBLES_BLOCK_SIZE] = {0.0f};
    float out_l[BUBBLES_BLOCK_SIZE];
    float out_r[BUBBLES_BLOCK_SIZE];

    double sum_sq = 0.0;
    // Process 5 seconds of absolute silence
    for (int s = 0; s < 44100 * 5; s += BUBBLES_BLOCK_SIZE) {
        bubble_engine_process(&engine, in, out_l, out_r, BUBBLES_BLOCK_SIZE);
        for (int i = 0; i < BUBBLES_BLOCK_SIZE; i++) {
            sum_sq += (double)out_l[i] * (double)out_l[i] + (double)out_r[i] * (double)out_r[i];
        }
    }

    size_t ring_bytes = bubble_engine_required_buffer_bytes(44100.0f);
    int ring_clean = 1;
    const uint8_t* p = (const uint8_t*)g_delay;
    for (size_t i = 0; i < ring_bytes; i++) {
        if (p[i] != 0) { ring_clean = 0; break; }
    }

    printf("silence_energy=%.8e,ring_clean=%d\n", sum_sq, ring_clean);
}

int main(int argc, char** argv) {
    int dither = 1;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--no-dither") == 0) dither = 0;
        else if (strcmp(argv[i], "--dither") == 0) dither = 1;
    }

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--quant-grid") == 0) {
            run_quantization_grid(dither);
            return 0;
        } else if (strcmp(argv[i], "--regeneration") == 0) {
            run_regeneration_test(dither);
            return 0;
        } else if (strcmp(argv[i], "--material-parity") == 0) {
            if (i + 2 < argc) {
                run_material_parity(argv[i + 1], argv[i + 2], dither);
                return 0;
            }
        } else if (strcmp(argv[i], "--freeze-hash") == 0) {
            run_freeze_hash_check();
            return 0;
        } else if (strcmp(argv[i], "--silence-dither") == 0) {
            run_silence_dither_test();
            return 0;
        }
    }

    printf("M4C Qualification Probe (BUBBLES_RING_FLOAT=%d, sample_size=%zu)\n",
           BUBBLES_RING_FLOAT, sizeof(BubbleRingSample_t));
    return 0;
}
