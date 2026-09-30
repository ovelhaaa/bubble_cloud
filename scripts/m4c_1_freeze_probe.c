// M4C.1 Qualification Freeze Probe.
// Exhaustive qualification covering all 32 milestone requirements.
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define SOUND_BUBBLES_DSP_INTERNAL 1
#include "dsp/sound_bubbles_dsp.c"
#include "engine/bubble_engine.h"

#define MAX_RING_SAMPLES 384000
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

__attribute__((unused)) static void fill_sine_tone(float* buffer, int num_samples, float freq, float sample_rate, float amp) {
    for (int i = 0; i < num_samples; i++) {
        buffer[i] = amp * sinf(2.0f * M_PI_F * freq * (float)i / sample_rate);
    }
}

__attribute__((unused)) static void fill_harmonic_tone(float* buffer, int num_samples, float f0, float sample_rate, float amp) {
    for (int i = 0; i < num_samples; i++) {
        float t = (float)i / sample_rate;
        float s = 0.55f * sinf(2.0f * M_PI_F * f0 * t)
                + 0.28f * sinf(2.0f * M_PI_F * (2.0f * f0) * t)
                + 0.12f * sinf(2.0f * M_PI_F * (3.0f * f0) * t)
                + 0.05f * sinf(2.0f * M_PI_F * (4.0f * f0) * t);
        buffer[i] = amp * s;
    }
}

// ---------------------------------------------------------------------------
// 1. SR x Block Matrix
// ---------------------------------------------------------------------------
static void run_sr_block_matrix(int dither_enabled) {
    const float sample_rates[4] = {44100.0f, 48000.0f, 88200.0f, 96000.0f};
    const int block_sizes[6] = {32, 64, 127, 256, 512, 2048};

    printf("sr,block_size,samples,bytes,peak,rms_db,pass\n");

    for (int r = 0; r < 4; r++) {
        float sr = sample_rates[r];
        size_t req_samples = SoundBubbles_RequiredBufferSamples(sr);
        size_t req_bytes = SoundBubbles_RequiredBufferBytes(sr);

        for (int b = 0; b < 6; b++) {
            int bs = block_sizes[b];

            BubbleEngineConfig_t config;
            bubble_engine_default_config(&config);
            config.sample_rate = sr;
            config.rng_seed = 0x12345678u;
            config.density_decay = 60.0f;

            memset(g_delay, 0, req_bytes);
            BubbleEngine_t engine;
            bubble_engine_init(&engine, g_delay, &config);
            SoundBubbles_SetFeedbackEnabled(&engine, true);
            bubble_engine_set_dither_enabled(&engine, dither_enabled);

            const int total_samples = (int)(1.5f * sr);
            const int tone_samples = (int)(0.5f * sr);

            float* in = (float*)calloc(bs, sizeof(float));
            float* out_l = (float*)calloc(bs, sizeof(float));
            float* out_r = (float*)calloc(bs, sizeof(float));

            float max_peak = 0.0f;
            double sum_sq = 0.0;
            int pass = 1;

            int processed = 0;
            while (processed < total_samples) {
                int cur_bs = bs;
                if (processed + cur_bs > total_samples) cur_bs = total_samples - processed;

                for (int i = 0; i < cur_bs; i++) {
                    int pos = processed + i;
                    in[i] = (pos < tone_samples) ? 0.7f * sinf(2.0f * M_PI_F * 440.0f * (float)pos / sr) : 0.0f;
                }

                bubble_engine_process(&engine, in, out_l, out_r, cur_bs);

                for (int i = 0; i < cur_bs; i++) {
                    if (!isfinite(out_l[i]) || !isfinite(out_r[i])) pass = 0;
                    float al = fabsf(out_l[i]);
                    float ar = fabsf(out_r[i]);
                    if (al > max_peak) max_peak = al;
                    if (ar > max_peak) max_peak = ar;
                    sum_sq += (double)(out_l[i] * out_l[i]);
                }
                processed += cur_bs;
            }

            if (max_peak > 1.05f) pass = 0;
            float rms = (processed > 0) ? (float)sqrt(sum_sq / (double)processed) : 0.0f;

            printf("%.0f,%d,%zu,%zu,%.4f,%.2f,%d\n",
                   (double)sr, bs, req_samples, req_bytes, max_peak, calc_db(rms), pass);

            free(in);
            free(out_l);
            free(out_r);
        }
    }
}

// ---------------------------------------------------------------------------
// 2. Block Invariance
// ---------------------------------------------------------------------------
static void run_block_invariance(int dither_enabled) {
    const float sr = 48000.0f;
    const int total_samples = 48000;
    const int block_sizes[6] = {32, 64, 127, 256, 512, 2048};

    float* ref_out = (float*)calloc(total_samples, sizeof(float));

    printf("block_size,max_diff,rms_diff\n");

    for (int b = 0; b < 6; b++) {
        int bs = block_sizes[b];
        float* cur_out = (float*)calloc(total_samples, sizeof(float));
        float* cur_out_r = (float*)calloc(total_samples, sizeof(float));
        float* in_buf = (float*)calloc(bs, sizeof(float));

        BubbleEngineConfig_t config;
        bubble_engine_default_config(&config);
        config.sample_rate = sr;
        config.rng_seed = 0x5EED5EEDu;

        memset(g_delay, 0, SoundBubbles_RequiredBufferBytes(sr));
        BubbleEngine_t engine;
        bubble_engine_init(&engine, g_delay, &config);
        bubble_engine_set_dither_enabled(&engine, dither_enabled);

        int processed = 0;
        while (processed < total_samples) {
            int cur_bs = bs;
            if (processed + cur_bs > total_samples) cur_bs = total_samples - processed;

            for (int i = 0; i < cur_bs; i++) {
                int pos = processed + i;
                in_buf[i] = 0.5f * sinf(2.0f * M_PI_F * 300.0f * (float)pos / sr);
            }

            bubble_engine_process(&engine, in_buf, &cur_out[processed], &cur_out_r[processed], cur_bs);
            processed += cur_bs;
        }

        if (b == 0) {
            memcpy(ref_out, cur_out, sizeof(float) * total_samples);
            printf("%d,0.000000,0.000000\n", bs);
        } else {
            double max_d = 0.0, sum_d2 = 0.0;
            for (int i = 0; i < total_samples; i++) {
                double diff = fabs((double)cur_out[i] - (double)ref_out[i]);
                if (diff > max_d) max_d = diff;
                sum_d2 += diff * diff;
            }
            double rms_d = sqrt(sum_d2 / (double)total_samples);
            printf("%d,%.6f,%.6f\n", bs, max_d, rms_d);
        }

        free(cur_out);
        free(cur_out_r);
        free(in_buf);
    }

    free(ref_out);
}

// ---------------------------------------------------------------------------
// 3. Determinism
// ---------------------------------------------------------------------------
static void run_determinism(int dither_enabled) {
    const float sr = 44100.0f;
    const int total_blocks = (44100 * 2 + BUBBLES_BLOCK_SIZE - 1) / BUBBLES_BLOCK_SIZE;
    const int total_samples = total_blocks * BUBBLES_BLOCK_SIZE;
    size_t req_bytes = SoundBubbles_RequiredBufferBytes(sr);

    float* out_l1 = (float*)calloc(total_samples, sizeof(float));
    float* out_r1 = (float*)calloc(total_samples, sizeof(float));
    float* out_l2 = (float*)calloc(total_samples, sizeof(float));
    float* out_r2 = (float*)calloc(total_samples, sizeof(float));
    BubbleRingSample_t* ring_snap1 = (BubbleRingSample_t*)malloc(req_bytes);
    BubbleRingSample_t* ring_snap2 = (BubbleRingSample_t*)malloc(req_bytes);

    uint32_t dither_rng1 = 0, dither_rng2 = 0;

    // Run 1
    {
        BubbleEngineConfig_t config;
        bubble_engine_default_config(&config);
        config.sample_rate = sr;
        config.rng_seed = 0xABCD1234u;
        config.density_decay = 70.0f;

        memset(g_delay, 0, req_bytes);
        BubbleEngine_t engine;
        bubble_engine_init(&engine, g_delay, &config);
        SoundBubbles_SetFeedbackEnabled(&engine, true);
        bubble_engine_set_dither_enabled(&engine, dither_enabled);

        float in[BUBBLES_BLOCK_SIZE];
        for (int s = 0; s < total_samples; s += BUBBLES_BLOCK_SIZE) {
            for (int i = 0; i < BUBBLES_BLOCK_SIZE; i++) {
                int pos = s + i;
                in[i] = (pos < 44100) ? 0.6f * sinf(2.0f * M_PI_F * 440.0f * (float)pos / sr) : 0.0f;
            }
            if (s == 22050) bubble_engine_set_parameter(&engine, BUBBLE_PARAM_FREEZE, 0.5f);
            bubble_engine_process(&engine, in, &out_l1[s], &out_r1[s], BUBBLES_BLOCK_SIZE);
        }
        memcpy(ring_snap1, g_delay, req_bytes);
        dither_rng1 = engine.ring_dither_rng;
    }

    // Run 2
    {
        BubbleEngineConfig_t config;
        bubble_engine_default_config(&config);
        config.sample_rate = sr;
        config.rng_seed = 0xABCD1234u;
        config.density_decay = 70.0f;

        memset(g_delay, 0, req_bytes);
        BubbleEngine_t engine;
        bubble_engine_init(&engine, g_delay, &config);
        SoundBubbles_SetFeedbackEnabled(&engine, true);
        bubble_engine_set_dither_enabled(&engine, dither_enabled);

        float in[BUBBLES_BLOCK_SIZE];
        for (int s = 0; s < total_samples; s += BUBBLES_BLOCK_SIZE) {
            for (int i = 0; i < BUBBLES_BLOCK_SIZE; i++) {
                int pos = s + i;
                in[i] = (pos < 44100) ? 0.6f * sinf(2.0f * M_PI_F * 440.0f * (float)pos / sr) : 0.0f;
            }
            if (s == 22050) bubble_engine_set_parameter(&engine, BUBBLE_PARAM_FREEZE, 0.5f);
            bubble_engine_process(&engine, in, &out_l2[s], &out_r2[s], BUBBLES_BLOCK_SIZE);
        }
        memcpy(ring_snap2, g_delay, req_bytes);
        dither_rng2 = engine.ring_dither_rng;
    }

    int out_match = (memcmp(out_l1, out_l2, sizeof(float) * total_samples) == 0 &&
                     memcmp(out_r1, out_r2, sizeof(float) * total_samples) == 0) ? 1 : 0;
    int ring_match = (memcmp(ring_snap1, ring_snap2, req_bytes) == 0) ? 1 : 0;
    int rng_match = (dither_rng1 == dither_rng2) ? 1 : 0;

    printf("output_match=%d,ring_match=%d,dither_rng_match=%d\n", out_match, ring_match, rng_match);

    free(out_l1);
    free(out_r1);
    free(out_l2);
    free(out_r2);
    free(ring_snap1);
    free(ring_snap2);
}

// ---------------------------------------------------------------------------
// 4. Dither RNG Isolation (Section 7)
// ---------------------------------------------------------------------------
typedef struct {
    uint32_t spawn_count;
    uint32_t active_voices;
    uint32_t engine_state;
} SpawnSnapshot_t;

static SpawnSnapshot_t g_trace_buf[4096];
static int g_trace_count = 0;

static void trace_callback(const SoundBubblesBlockMetrics_t* metrics, void* user) {
    (void)user;
    if (g_trace_count < 4096) {
        g_trace_buf[g_trace_count].spawn_count = (uint32_t)metrics->spawn_count;
        g_trace_buf[g_trace_count].active_voices = (uint32_t)metrics->active_voices;
        g_trace_buf[g_trace_count].engine_state = (uint32_t)metrics->engine_state;
        g_trace_count++;
    }
}

static void run_dither_isolation(void) {
    const float sr = 44100.0f;
    const int total_blocks = 256;
    SpawnSnapshot_t trace_off[256];
    SpawnSnapshot_t trace_on[256];

    // Pass 1: Dither OFF
    {
        BubbleEngineConfig_t config;
        bubble_engine_default_config(&config);
        config.sample_rate = sr;
        config.rng_seed = 0x11223344u;
        config.density_burst = 90.0f;
        config.density_sustain = 60.0f;

        memset(g_delay, 0, SoundBubbles_RequiredBufferBytes(sr));
        BubbleEngine_t engine;
        bubble_engine_init(&engine, g_delay, &config);
        bubble_engine_set_dither_enabled(&engine, 0);
        g_trace_count = 0;
        bubble_engine_set_metrics_callback(&engine, trace_callback, NULL);

        float in[BUBBLES_BLOCK_SIZE], l[BUBBLES_BLOCK_SIZE], r[BUBBLES_BLOCK_SIZE];
        for (int b = 0; b < total_blocks; b++) {
            for (int i = 0; i < BUBBLES_BLOCK_SIZE; i++) in[i] = 0.5f * sinf(2.0f * M_PI_F * 440.0f * (float)(b * 32 + i) / sr);
            bubble_engine_process(&engine, in, l, r, BUBBLES_BLOCK_SIZE);
        }
        memcpy(trace_off, g_trace_buf, sizeof(trace_off));
    }

    // Pass 2: Dither ON
    {
        BubbleEngineConfig_t config;
        bubble_engine_default_config(&config);
        config.sample_rate = sr;
        config.rng_seed = 0x11223344u;
        config.density_burst = 90.0f;
        config.density_sustain = 60.0f;

        memset(g_delay, 0, SoundBubbles_RequiredBufferBytes(sr));
        BubbleEngine_t engine;
        bubble_engine_init(&engine, g_delay, &config);
        bubble_engine_set_dither_enabled(&engine, 1);
        g_trace_count = 0;
        bubble_engine_set_metrics_callback(&engine, trace_callback, NULL);

        float in[BUBBLES_BLOCK_SIZE], l[BUBBLES_BLOCK_SIZE], r[BUBBLES_BLOCK_SIZE];
        for (int b = 0; b < total_blocks; b++) {
            for (int i = 0; i < BUBBLES_BLOCK_SIZE; i++) in[i] = 0.5f * sinf(2.0f * M_PI_F * 440.0f * (float)(b * 32 + i) / sr);
            bubble_engine_process(&engine, in, l, r, BUBBLES_BLOCK_SIZE);
        }
        memcpy(trace_on, g_trace_buf, sizeof(trace_on));
    }

    int mismatches = 0;
    for (int i = 0; i < total_blocks; i++) {
        if (trace_off[i].spawn_count != trace_on[i].spawn_count ||
            trace_off[i].active_voices != trace_on[i].active_voices ||
            trace_off[i].engine_state != trace_on[i].engine_state) {
            mismatches++;
        }
    }

    printf("blocks=%d,mismatches=%d\n", total_blocks, mismatches);
}

// ---------------------------------------------------------------------------
// 5. Freeze Write Lock & Dither Invariance (Section 8)
// ---------------------------------------------------------------------------
static void run_freeze_lock_check(void) {
    const float sr = 44100.0f;
    size_t req_bytes = SoundBubbles_RequiredBufferBytes(sr);

    BubbleEngineConfig_t config;
    bubble_engine_default_config(&config);
    config.sample_rate = sr;
    config.freeze_enabled = 1;
    config.freeze_amount = 1.0f;

    memset(g_delay, 0, req_bytes);
    BubbleEngine_t engine;
    bubble_engine_init(&engine, g_delay, &config);
    engine.macro_dirty_mask = 0u;

    for (int i = 0; i < 88200; i++) {
        g_delay[i] = (BubbleRingSample_t)(BUBBLES_RING_SAMPLE_IS_FLOAT ? (sinf((float)i * 0.05f) * 0.45f) : (int16_t)(sinf((float)i * 0.05f) * 15000.0f));
    }

    bubble_engine_set_parameter(&engine, BUBBLE_PARAM_DEVELOPER_MODE, 1.0f);
    bubble_engine_set_parameter(&engine, BUBBLE_ENGINE_PARAM_FREEZE_AMOUNT, 1.0f);
    bubble_engine_set_parameter(&engine, BUBBLE_ENGINE_PARAM_FREEZE_ENABLED, 1.0f);

    uint32_t dither_rng_before = engine.ring_dither_rng;
    int32_t ptr_before = engine.write_ptr;
    uint64_t hash_before = fnv1a_hash(g_delay, req_bytes);

    float in[BUBBLES_BLOCK_SIZE], out_l[BUBBLES_BLOCK_SIZE], out_r[BUBBLES_BLOCK_SIZE];
    for (int i = 0; i < BUBBLES_BLOCK_SIZE; i++) in[i] = 0.5f;

    for (int s = 0; s < 44100 * 25; s += BUBBLES_BLOCK_SIZE) {
        bubble_engine_process(&engine, in, out_l, out_r, BUBBLES_BLOCK_SIZE);
    }

    uint32_t dither_rng_after = engine.ring_dither_rng;
    int32_t ptr_after = engine.write_ptr;
    uint64_t hash_after = fnv1a_hash(g_delay, req_bytes);

    int ptr_match = (ptr_before == ptr_after) ? 1 : 0;
    int hash_match = (hash_before == hash_after) ? 1 : 0;
    int dither_rng_match = (dither_rng_before == dither_rng_after) ? 1 : 0;

    printf("ptr_match=%d,hash_match=%d,dither_rng_match=%d\n", ptr_match, hash_match, dither_rng_match);
}

// ---------------------------------------------------------------------------
// 6. Pitch & Direction Stress Matrix (Section 9)
// ---------------------------------------------------------------------------
static void run_pitch_direction_stress(void) {
    const float sr = 48000.0f;
    const int total_samples = 48000 * 2;

    struct {
        const char* label;
        float pitch_mode;
        float shimmer;
        float rev_prob;
    } tests[] = {
        {"unison", 0.0f, 0.0f, 0.0f},
        {"+12", 1.0f, 0.0f, 0.0f},
        {"-12", 2.0f, 0.0f, 0.0f},
        {"fifth_+7", 3.0f, 0.0f, 0.0f},
        {"shimmer_+19", 1.0f, 0.85f, 0.0f},
        {"reverse_unison", 0.0f, 0.0f, 1.0f},
        {"reverse_+12", 1.0f, 0.0f, 1.0f},
    };

    printf("rate_label,peak,rms_db,pass\n");

    for (int t = 0; t < 7; t++) {
        BubbleEngineConfig_t config;
        bubble_engine_default_config(&config);
        config.sample_rate = sr;
        config.quality_profile = BUBBLE_QUALITY_PROFILE_WEB_ULTRA;
        config.active_voice_limit = 32;
        config.reverse_probability = tests[t].rev_prob;
        config.shimmer_amount = tests[t].shimmer;

        memset(g_delay, 0, SoundBubbles_RequiredBufferBytes(sr));
        BubbleEngine_t engine;
        bubble_engine_init(&engine, g_delay, &config);
        bubble_engine_set_parameter(&engine, BUBBLE_PARAM_DEVELOPER_MODE, 1.0f);
        bubble_engine_set_parameter(&engine, BUBBLE_ENGINE_PARAM_PITCH_MODE, tests[t].pitch_mode);

        float in[BUBBLES_BLOCK_SIZE], l[BUBBLES_BLOCK_SIZE], r[BUBBLES_BLOCK_SIZE];
        float max_p = 0.0f;
        double sum_sq = 0.0;
        int pass = 1;

        for (int s = 0; s < total_samples; s += BUBBLES_BLOCK_SIZE) {
            for (int i = 0; i < BUBBLES_BLOCK_SIZE; i++) {
                int pos = s + i;
                in[i] = (pos < 24000) ? 0.6f * sinf(2.0f * M_PI_F * 440.0f * (float)pos / sr) : 0.0f;
            }
            bubble_engine_process(&engine, in, l, r, BUBBLES_BLOCK_SIZE);
            for (int i = 0; i < BUBBLES_BLOCK_SIZE; i++) {
                if (!isfinite(l[i]) || !isfinite(r[i])) pass = 0;
                float al = fabsf(l[i]);
                float ar = fabsf(r[i]);
                if (al > max_p) max_p = al;
                if (ar > max_p) max_p = ar;
                sum_sq += (double)(l[i] * l[i]);
            }
        }

        if (max_p > 1.05f) pass = 0;
        float rms = (float)sqrt(sum_sq / (double)total_samples);
        printf("%s,%.4f,%.2f,%d\n", tests[t].label, max_p, calc_db(rms), pass);
    }
}

// ---------------------------------------------------------------------------
// 7. Hermite Mathematical Parity (Section 10)
// ---------------------------------------------------------------------------
static void run_hermite_math_parity(void) {
    printf("test_name,max_err,pass\n");

    // 1. Constant
    {
        BubbleRingSample_t buf[256];
        for (int i = 0; i < 256; i++) buf[i] = (BubbleRingSample_t)(BUBBLES_RING_SAMPLE_IS_FLOAT ? (12000.0f / 32768.0f) : 12000);
        double max_err = 0.0;
        for (int i = 0; i < 256; i++) {
            for (int f = 0; f < 10; f++) {
                float pos = (float)i + (float)f * 0.1f;
                float got = Hermite4Interpolate(buf, pos, 256);
                double expected = (double)Ring_ReadNormalizedSample(buf, 0);
                double err = fabs((double)got - expected);
                if (err > max_err) max_err = err;
            }
        }
        printf("constant,%.3e,%d\n", max_err, (max_err < 1e-6) ? 1 : 0);
    }

    // 2. Ramp
    {
        BubbleRingSample_t buf[512];
        for (int i = 0; i < 512; i++) buf[i] = (BubbleRingSample_t)(BUBBLES_RING_SAMPLE_IS_FLOAT ? ((1000.0f + 37.0f * (float)i) / 32768.0f) : (int16_t)(1000 + 37 * i));
        double max_err = 0.0;
        for (int i = 1; i < 510; i++) {
            for (int f = 0; f < 10; f++) {
                float pos = (float)i + (float)f * 0.1f;
                float got = Hermite4Interpolate(buf, pos, 512);
                double expected = (1000.0 + 37.0 * (double)pos) / (BUBBLES_RING_SAMPLE_IS_FLOAT ? 32768.0 : 32767.0);
                double err = fabs((double)got - expected);
                if (err > max_err) max_err = err;
            }
        }
        printf("ramp,%.3e,%d\n", max_err, (max_err < 2e-4) ? 1 : 0);
    }

    // 3. Wrap
    {
        BubbleRingSample_t buf[128];
        for (int i = 0; i < 128; i++) {
            double v = sin(2.0 * M_PI_F * 3.0 * (double)i / 128.0);
            buf[i] = (BubbleRingSample_t)(BUBBLES_RING_SAMPLE_IS_FLOAT ? (float)(v * (20000.0 / 32768.0)) : (int16_t)lround(20000.0 * v));
        }
        double max_err = 0.0;
        const double edges[] = {0.0, 0.25, 0.5, 0.9, 127.0, 127.25, 127.5, 127.9};
        for (int p = 0; p < 8; p++) {
            float got = Hermite4Interpolate(buf, (float)edges[p], 128);
            int idx = (int)edges[p];
            double frac = edges[p] - (double)idx;
            int im1 = (idx > 0) ? idx - 1 : 127;
            int ip1 = (idx + 1 < 128) ? idx + 1 : 0;
            int ip2 = (idx + 2 < 128) ? idx + 2 : idx + 2 - 128;
            double xm1 = (double)Ring_ReadNormalizedSample(buf, im1);
            double x0 = (double)Ring_ReadNormalizedSample(buf, idx);
            double x1 = (double)Ring_ReadNormalizedSample(buf, ip1);
            double x2 = (double)Ring_ReadNormalizedSample(buf, ip2);
            double a = -0.5 * xm1 + 1.5 * x0 - 1.5 * x1 + 0.5 * x2;
            double b = xm1 - 2.5 * x0 + 2.0 * x1 - 0.5 * x2;
            double c = -0.5 * xm1 + 0.5 * x1;
            double d = x0;
            double expected = ((a * frac + b) * frac + c) * frac + d;
            double err = fabs((double)got - expected);
            if (err > max_err) max_err = err;
        }
        printf("wrap,%.3e,%d\n", max_err, (max_err < 1e-6) ? 1 : 0);
    }
}

// ---------------------------------------------------------------------------
// 8. Normal Level Parity (Section 11)
// ---------------------------------------------------------------------------
static void run_normal_level_parity(int dither_enabled) {
    const float levels_db[] = {-6.0f, -12.0f, -24.0f, -36.0f};
    const char* scenarios[] = {"no_feedback", "autohold_tail", "m4b_feedback"};
    const float sr = 44100.0f;
    const int total_blocks = (44100 * 2 + BUBBLES_BLOCK_SIZE - 1) / BUBBLES_BLOCK_SIZE;
    const int total_samples = total_blocks * BUBBLES_BLOCK_SIZE;

    printf("dbfs,scenario,rms_db,peak,centroid,stereo_corr\n");

    for (int l = 0; l < 4; l++) {
        float db = levels_db[l];
        float amp = powf(10.0f, db / 20.0f);

        for (int sc = 0; sc < 3; sc++) {
            BubbleEngineConfig_t config;
            bubble_engine_default_config(&config);
            config.sample_rate = sr;
            config.rng_seed = 0x99887766u;

            bool fb_on = false;
            if (sc == 1) {
                config.density_decay = 80.0f;
            } else if (sc == 2) {
                fb_on = true;
                config.density_decay = 60.0f;
            }

            memset(g_delay, 0, SoundBubbles_RequiredBufferBytes(sr));
            BubbleEngine_t engine;
            bubble_engine_init(&engine, g_delay, &config);
            SoundBubbles_SetFeedbackEnabled(&engine, fb_on);
            bubble_engine_set_dither_enabled(&engine, dither_enabled);

            float in[BUBBLES_BLOCK_SIZE], out_l[BUBBLES_BLOCK_SIZE], out_r[BUBBLES_BLOCK_SIZE];
            float* full_l = (float*)malloc(sizeof(float) * total_samples);
            float* full_r = (float*)malloc(sizeof(float) * total_samples);

            float peak = 0.0f;
            for (int s = 0; s < total_samples; s += BUBBLES_BLOCK_SIZE) {
                for (int i = 0; i < BUBBLES_BLOCK_SIZE; i++) {
                    int pos = s + i;
                    in[i] = (pos < 22050) ? amp * sinf(2.0f * M_PI_F * 440.0f * (float)pos / sr) : 0.0f;
                }
                bubble_engine_process(&engine, in, out_l, out_r, BUBBLES_BLOCK_SIZE);
                for (int i = 0; i < BUBBLES_BLOCK_SIZE; i++) {
                    full_l[s + i] = out_l[i];
                    full_r[s + i] = out_r[i];
                    if (fabsf(out_l[i]) > peak) peak = fabsf(out_l[i]);
                    if (fabsf(out_r[i]) > peak) peak = fabsf(out_r[i]);
                }
            }

            float rms = calc_rms(full_l, total_samples);
            float centroid = calc_centroid(full_l, total_samples, sr);
            float corr = calc_correlation(full_l, full_r, total_samples);

            printf("%.1f,%s,%.2f,%.4f,%.1f,%.4f\n", db, scenarios[sc], calc_db(rms), peak, centroid, corr);

            free(full_l);
            free(full_r);
        }
    }
}

// ---------------------------------------------------------------------------
// 8b. Quantization Grid (Section 12)
// ---------------------------------------------------------------------------
static void run_quantization_grid(int dither_enabled) {
    const float levels_db[] = {-48.0f, -60.0f, -72.0f, -84.0f, -90.0f, -96.0f};
    const float sr = 44100.0f;
    const int tone_len = (int)(0.5f * sr);
    const int tail_len = (int)(4.0f * sr);
    const int total_blocks = ((tone_len + tail_len) + BUBBLES_BLOCK_SIZE - 1) / BUBBLES_BLOCK_SIZE;
    const int total_samples = total_blocks * BUBBLES_BLOCK_SIZE;

    printf("dbfs,ring_rms_db,tail_rms_db,survived\n");

    for (int idx = 0; idx < 6; idx++) {
        float db = levels_db[idx];
        float amp = powf(10.0f, db / 20.0f);

        BubbleEngineConfig_t config;
        bubble_engine_default_config(&config);
        config.sample_rate = sr;
        config.rng_seed = 0x12345678u;
        config.density_decay = 80.0f;

        memset(g_delay, 0, SoundBubbles_RequiredBufferBytes(sr));
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

            if (s + BUBBLES_BLOCK_SIZE >= tone_len && captured_ring_rms == 0.0f) {
                double ring_energy = 0.0;
                for (int i = 0; i < tone_len; i++) {
                    float val = Ring_ReadNormalizedSample(g_delay, i);
                    ring_energy += (double)val * (double)val;
                }
                captured_ring_rms = (float)sqrt(ring_energy / (double)tone_len);
            }

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

// ---------------------------------------------------------------------------
// 9. Long Regeneration Windows (Section 13)
// ---------------------------------------------------------------------------
static void run_long_regeneration(int dither_enabled) {
    const float sr = 44100.0f;
    const int total_blocks = ((int)(15.0f * sr) + BUBBLES_BLOCK_SIZE - 1) / BUBBLES_BLOCK_SIZE;
    const int total_samples = total_blocks * BUBBLES_BLOCK_SIZE;
    const int tone_samples = (int)(0.5f * sr);

    BubbleEngineConfig_t config;
    bubble_engine_default_config(&config);
    config.sample_rate = sr;
    config.rng_seed = 0xCAFEBABEu;
    config.density_decay = 70.0f;

    memset(g_delay, 0, SoundBubbles_RequiredBufferBytes(sr));
    BubbleEngine_t engine;
    bubble_engine_init(&engine, g_delay, &config);
    SoundBubbles_SetFeedbackEnabled(&engine, true);
    bubble_engine_set_dither_enabled(&engine, dither_enabled);

    float in[BUBBLES_BLOCK_SIZE], out_l[BUBBLES_BLOCK_SIZE], out_r[BUBBLES_BLOCK_SIZE];
    float* full_l = (float*)calloc(total_samples, sizeof(float));

    for (int s = 0; s < total_samples; s += BUBBLES_BLOCK_SIZE) {
        for (int i = 0; i < BUBBLES_BLOCK_SIZE; i++) {
            int pos = s + i;
            in[i] = (pos < tone_samples) ? 0.6f * sinf(2.0f * M_PI_F * 440.0f * (float)pos / sr) : 0.0f;
        }
        bubble_engine_process(&engine, in, out_l, out_r, BUBBLES_BLOCK_SIZE);
        for (int i = 0; i < BUBBLES_BLOCK_SIZE; i++) full_l[s + i] = out_l[i];
    }

    struct { float start; float end; } wins[8] = {
        {0.5f, 1.0f}, {1.0f, 2.0f}, {2.0f, 4.0f}, {4.0f, 6.0f},
        {6.0f, 8.0f}, {8.0f, 10.0f}, {10.0f, 12.0f}, {12.0f, 15.0f}
    };

    printf("win_start,win_end,rms_db,centroid,fb_energy\n");
    for (int w = 0; w < 8; w++) {
        int idx_s = (int)(wins[w].start * sr);
        int idx_e = (int)(wins[w].end * sr);
        int cnt = idx_e - idx_s;
        float rms = calc_rms(&full_l[idx_s], cnt);
        float centroid = calc_centroid(&full_l[idx_s], cnt, sr);
        float fb_e = SoundBubbles_GetFeedbackEnergy(&engine);
        printf("%.1f,%.1f,%.2f,%.1f,%.4f\n", wins[w].start, wins[w].end, calc_db(rms), centroid, fb_e);
    }

    free(full_l);
}

// ---------------------------------------------------------------------------
// 10. M4B Stability Regression (Section 14 & 15)
// ---------------------------------------------------------------------------
static void run_m4b_stability_regression(void) {
    const float sr = 44100.0f;

    // 1. Extreme 60s runaway test
    BubbleEngineConfig_t config;
    bubble_engine_default_config(&config);
    config.sample_rate = sr;
    config.density_burst = 200.0f;
    config.density_sustain = 100.0f;
    config.density_decay = 50.0f;

    memset(g_delay, 0, SoundBubbles_RequiredBufferBytes(sr));
    BubbleEngine_t engine;
    bubble_engine_init(&engine, g_delay, &config);
    SoundBubbles_SetFeedbackEnabled(&engine, true);

    const int total_blocks = ((int)(60.0f * sr) + BUBBLES_BLOCK_SIZE - 1) / BUBBLES_BLOCK_SIZE;
    const int total_samples = total_blocks * BUBBLES_BLOCK_SIZE;
    float in[BUBBLES_BLOCK_SIZE], out_l[BUBBLES_BLOCK_SIZE], out_r[BUBBLES_BLOCK_SIZE];
    float max_peak = 0.0f;
    int is_stable = 1;

    for (int s = 0; s < total_samples; s += BUBBLES_BLOCK_SIZE) {
        for (int i = 0; i < BUBBLES_BLOCK_SIZE; i++) {
            in[i] = (s < (int)(2.0f * sr)) ? 0.9f * sinf(2.0f * M_PI_F * 220.0f * (float)(s + i) / sr) : 0.0f;
        }
        bubble_engine_process(&engine, in, out_l, out_r, BUBBLES_BLOCK_SIZE);
        for (int i = 0; i < BUBBLES_BLOCK_SIZE; i++) {
            if (!isfinite(out_l[i]) || !isfinite(out_r[i])) is_stable = 0;
            if (fabsf(out_l[i]) > max_peak) max_peak = fabsf(out_l[i]);
            if (fabsf(out_r[i]) > max_peak) max_peak = fabsf(out_r[i]);
        }
    }
    if (max_peak > 1.05f) is_stable = 0;

    // 2. Periodicity analysis (render 8s)
    bubble_engine_reset(&engine);
    const int p_samples = (int)(8.0f * sr);
    float* p_out = (float*)calloc(p_samples, sizeof(float));
    for (int s = 0; s < p_samples; s += BUBBLES_BLOCK_SIZE) {
        for (int i = 0; i < BUBBLES_BLOCK_SIZE; i++) {
            in[i] = (s < (int)(0.5f * sr)) ? 0.7f * sinf(2.0f * M_PI_F * 440.0f * (float)(s + i) / sr) : 0.0f;
        }
        bubble_engine_process(&engine, in, out_l, out_r, BUBBLES_BLOCK_SIZE);
        for (int i = 0; i < BUBBLES_BLOCK_SIZE; i++) p_out[s + i] = out_l[i];
    }

    int p_start = (int)(1.0f * sr);
    int p_len = (int)(2.0f * sr);
    double ref_sq = 0.0, lag2_sq = 0.0, dot2 = 0.0, lag4_sq = 0.0, dot4 = 0.0;
    int lag2 = (int)(2.0f * sr);
    int lag4 = (int)(4.0f * sr);

    for (int i = 0; i < p_len; i++) {
        double r = (double)p_out[p_start + i];
        double l2 = (double)p_out[p_start + lag2 + i];
        double l4 = (double)p_out[p_start + lag4 + i];
        ref_sq += r * r;
        lag2_sq += l2 * l2;
        dot2 += r * l2;
        lag4_sq += l4 * l4;
        dot4 += r * l4;
    }
    float autocorr_2s = (ref_sq > 1e-9 && lag2_sq > 1e-9) ? (float)(dot2 / sqrt(ref_sq * lag2_sq)) : 0.0f;
    float autocorr_4s = (ref_sq > 1e-9 && lag4_sq > 1e-9) ? (float)(dot4 / sqrt(ref_sq * lag4_sq)) : 0.0f;

    printf("runaway_peak=%.4f,runaway_stable=%d,autocorr_2s=%.4f,autocorr_4s=%.4f,pass=%d\n",
           max_peak, is_stable, autocorr_2s, autocorr_4s, (is_stable && fabs(autocorr_2s) < 0.40f && fabs(autocorr_4s) < 0.40f) ? 1 : 0);

    free(p_out);
}

// ---------------------------------------------------------------------------
// 11. CPU Matrix (Section 16)
// ---------------------------------------------------------------------------
static void run_cpu_matrix(int dither_enabled) {
    const int voices[4] = {8, 16, 24, 32};
    const float rates[3] = {44100.0f, 48000.0f, 96000.0f};
    const float dur_sec = 10.0f;

    printf("voices,sr,render_sec,rt_factor\n");

    for (int v = 0; v < 4; v++) {
        for (int r = 0; r < 3; r++) {
            float sr = rates[r];
            int total_samples = (int)(dur_sec * sr);

            BubbleEngineConfig_t config;
            bubble_engine_default_config(&config);
            config.sample_rate = sr;
            config.active_voice_limit = voices[v];
            config.density_burst = 120.0f;
            config.density_sustain = 80.0f;

            memset(g_delay, 0, SoundBubbles_RequiredBufferBytes(sr));
            BubbleEngine_t engine;
            bubble_engine_init(&engine, g_delay, &config);
            SoundBubbles_SetFeedbackEnabled(&engine, true);
            bubble_engine_set_dither_enabled(&engine, dither_enabled);

            float in[BUBBLES_BLOCK_SIZE], out_l[BUBBLES_BLOCK_SIZE], out_r[BUBBLES_BLOCK_SIZE];
            for (int i = 0; i < BUBBLES_BLOCK_SIZE; i++) in[i] = 0.5f;

            // Warm up
            for (int s = 0; s < 64 * BUBBLES_BLOCK_SIZE; s += BUBBLES_BLOCK_SIZE) {
                bubble_engine_process(&engine, in, out_l, out_r, BUBBLES_BLOCK_SIZE);
            }

            clock_t t0 = clock();
            for (int s = 0; s < total_samples; s += BUBBLES_BLOCK_SIZE) {
                bubble_engine_process(&engine, in, out_l, out_r, BUBBLES_BLOCK_SIZE);
            }
            clock_t t1 = clock();
            double sec = (double)(t1 - t0) / (double)CLOCKS_PER_SEC;
            double rt = dur_sec / (sec > 1e-9 ? sec : 1e-9);

            printf("%d,%.0f,%.4f,%.1f\n", voices[v], (double)sr, sec, rt);
        }
    }
}

// ---------------------------------------------------------------------------
// 12. Memory Footprint Matrix (Section 18)
// ---------------------------------------------------------------------------
static void run_memory_matrix(void) {
    const float rates[4] = {44100.0f, 48000.0f, 88200.0f, 96000.0f};

    printf("sr,samples,bytes,kib\n");
    for (int r = 0; r < 4; r++) {
        float sr = rates[r];
        size_t samples = SoundBubbles_RequiredBufferSamples(sr);
        size_t bytes = SoundBubbles_RequiredBufferBytes(sr);
        double kib = (double)bytes / 1024.0;
        printf("%.0f,%zu,%zu,%.1f\n", (double)sr, samples, bytes, kib);
    }
}

// ---------------------------------------------------------------------------
// Main dispatcher
// ---------------------------------------------------------------------------
int main(int argc, char** argv) {
    int dither = 1;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--no-dither") == 0) dither = 0;
        else if (strcmp(argv[i], "--dither") == 0) dither = 1;
    }

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--sr-block-matrix") == 0) {
            run_sr_block_matrix(dither);
            return 0;
        } else if (strcmp(argv[i], "--block-invariance") == 0) {
            run_block_invariance(dither);
            return 0;
        } else if (strcmp(argv[i], "--determinism") == 0) {
            run_determinism(dither);
            return 0;
        } else if (strcmp(argv[i], "--dither-isolation") == 0) {
            run_dither_isolation();
            return 0;
        } else if (strcmp(argv[i], "--freeze-lock") == 0) {
            run_freeze_lock_check();
            return 0;
        } else if (strcmp(argv[i], "--pitch-direction-stress") == 0) {
            run_pitch_direction_stress();
            return 0;
        } else if (strcmp(argv[i], "--hermite-math-parity") == 0) {
            run_hermite_math_parity();
            return 0;
        } else if (strcmp(argv[i], "--normal-level-parity") == 0) {
            run_normal_level_parity(dither);
            return 0;
        } else if (strcmp(argv[i], "--quantization-grid") == 0) {
            run_quantization_grid(dither);
            return 0;
        } else if (strcmp(argv[i], "--long-regeneration") == 0) {
            run_long_regeneration(dither);
            return 0;
        } else if (strcmp(argv[i], "--m4b-stability-regression") == 0) {
            run_m4b_stability_regression();
            return 0;
        } else if (strcmp(argv[i], "--cpu-matrix") == 0) {
            run_cpu_matrix(dither);
            return 0;
        } else if (strcmp(argv[i], "--memory-matrix") == 0) {
            run_memory_matrix();
            return 0;
        }
    }

    printf("M4C.1 Freeze Probe (BUBBLES_RING_FLOAT=%d, sample_size=%zu)\n",
           BUBBLES_RING_FLOAT, sizeof(BubbleRingSample_t));
    return 0;
}
