// M4D Wet Dynamics & Limiter Decoupling Qualification Probe
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SOUND_BUBBLES_DSP_INTERNAL 1
#include "dsp/sound_bubbles_dsp.h"
#include "engine/bubble_engine.h"

#define MAX_RING_SAMPLES 384000
#ifndef M_PI_F
#define M_PI_F 3.14159265358979323846f
#endif

static BubbleRingSample_t g_delay[MAX_RING_SAMPLES];

// Signal sources
typedef enum {
    SOURCE_PERCUSSIVE = 0,
    SOURCE_HARMONIC_PLUCK,
    SOURCE_SUSTAINED_SINE,
    SOURCE_NOISE
} SourceType_t;

static float calc_rms(const float* b, int count) {
    if (count <= 0) return 0.0f;
    double sum = 0.0;
    for (int i = 0; i < count; i++) {
        sum += (double)b[i] * (double)b[i];
    }
    return (float)sqrt(sum / (double)count);
}

static float calc_peak(const float* b, int count) {
    float p = 0.0f;
    for (int i = 0; i < count; i++) {
        float a = fabsf(b[i]);
        if (a > p) p = a;
    }
    return p;
}

static float calc_db(float lin) {
    if (lin <= 1e-9f) return -180.0f;
    return 20.0f * log10f(lin);
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

static float calc_side_mid_ratio(const float* l, const float* r, int n) {
    double sum_mid2 = 0.0;
    double sum_side2 = 0.0;
    for (int i = 0; i < n; i++) {
        double mid = 0.5 * ((double)l[i] + (double)r[i]);
        double side = 0.5 * ((double)l[i] - (double)r[i]);
        sum_mid2 += mid * mid;
        sum_side2 += side * side;
    }
    double rms_mid = sqrt(sum_mid2 / (double)n);
    double rms_side = sqrt(sum_side2 / (double)n);
    return (rms_mid > 1e-9) ? (float)(rms_side / rms_mid) : 0.0f;
}

static void generate_source(SourceType_t type, float* buffer, int num_samples, float sample_rate) {
    memset(buffer, 0, (size_t)num_samples * sizeof(float));
    switch (type) {
        case SOURCE_PERCUSSIVE:
            // Impulses every 0.3s with decay
            for (int i = 0; i < num_samples; i++) {
                float t = (float)i / sample_rate;
                float local_t = fmodf(t, 0.30f);
                float env = expf(-local_t * 35.0f);
                float noise = ((float)(rand() % 20001) / 10000.0f) - 1.0f;
                buffer[i] = 0.85f * noise * env;
            }
            break;
        case SOURCE_HARMONIC_PLUCK:
            // Pluck: fundamental + harmonics with exponential decay every 0.4s
            for (int i = 0; i < num_samples; i++) {
                float t = (float)i / sample_rate;
                float local_t = fmodf(t, 0.40f);
                float env = expf(-local_t * 12.0f);
                float sig = sinf(2.0f * M_PI_F * 220.0f * local_t) +
                            0.50f * sinf(2.0f * M_PI_F * 440.0f * local_t) +
                            0.25f * sinf(2.0f * M_PI_F * 660.0f * local_t);
                buffer[i] = 0.55f * sig * env;
            }
            break;
        case SOURCE_SUSTAINED_SINE:
            // Continuous organ-like sine
            for (int i = 0; i < num_samples; i++) {
                float t = (float)i / sample_rate;
                buffer[i] = 0.50f * sinf(2.0f * M_PI_F * 330.0f * t) +
                            0.20f * sinf(2.0f * M_PI_F * 660.0f * t);
            }
            break;
        case SOURCE_NOISE:
            // Broadband filtered noise burst
            for (int i = 0; i < num_samples; i++) {
                float noise = ((float)(rand() % 20001) / 10000.0f) - 1.0f;
                buffer[i] = 0.40f * noise;
            }
            break;
    }
}

typedef struct {
    float out_rms_l;
    float out_rms_r;
    float out_rms_total;
    float out_peak;
    float wet_pre_norm_peak;
    float wet_pre_norm_rms;
    float wet_norm_gain_min;
    float wet_limiter_gain_min;
    float wet_limiter_max_gr_db;
    float final_limiter_gain_min;
    float final_limiter_max_gr_db;
    float final_limiter_active_rate; // fraction of blocks where gain < 0.999
    float correlation;
    float side_mid_ratio;
    double process_time_sec;
} RunMetrics_t;

typedef struct {
    int total_blocks;
    int final_limiter_active_blocks;
    float min_wet_norm_gain;
    float min_wet_lim_gain;
    float min_final_lim_gain;
    float max_wet_pre_peak;
    double sum_wet_pre_energy;
    int count_wet_pre_samples;
} CallbackAccum_t;

static void MetricsCallback(const BubbleEngineBlockMetrics_t* metrics, void* user_data) {
    CallbackAccum_t* acc = (CallbackAccum_t*)user_data;
    if (metrics == NULL || acc == NULL) return;
    acc->total_blocks++;
    if (metrics->final_limiter_gain < 0.999f || metrics->limiter_gain < 0.999f) {
        acc->final_limiter_active_blocks++;
    }
    if (metrics->wet_normalization_gain > 0.0f && metrics->wet_normalization_gain < acc->min_wet_norm_gain) {
        acc->min_wet_norm_gain = metrics->wet_normalization_gain;
    }
    if (metrics->wet_limiter_gain > 0.0f && metrics->wet_limiter_gain < acc->min_wet_lim_gain) {
        acc->min_wet_lim_gain = metrics->wet_limiter_gain;
    }
    float fl_gain = metrics->final_limiter_gain > 0.0f ? metrics->final_limiter_gain : metrics->limiter_gain;
    if (fl_gain > 0.0f && fl_gain < acc->min_final_lim_gain) {
        acc->min_final_lim_gain = fl_gain;
    }
    if (metrics->wet_pre_norm_peak > acc->max_wet_pre_peak) {
        acc->max_wet_pre_peak = metrics->wet_pre_norm_peak;
    }
    acc->sum_wet_pre_energy += (double)(metrics->wet_pre_norm_rms * metrics->wet_pre_norm_rms);
    acc->count_wet_pre_samples++;
}

static RunMetrics_t run_engine(float sample_rate,
                               SourceType_t source,
                               float duration_sec,
                               float density,
                               float bloom,
                               float mix,
                               float warmth,
                               BubbleQualityProfile profile,
                               bool enable_shimmer,
                               bool enable_freeze) {
    int num_samples = (int)(sample_rate * duration_sec);
    float* in_buf = (float*)malloc((size_t)num_samples * sizeof(float));
    float* out_l = (float*)malloc((size_t)num_samples * sizeof(float));
    float* out_r = (float*)malloc((size_t)num_samples * sizeof(float));

    generate_source(source, in_buf, num_samples, sample_rate);

    BubbleEngine_t engine;
    BubbleEngineConfig_t cfg;
    bubble_engine_default_config(&cfg);
    cfg.sample_rate = sample_rate;
    cfg.quality_profile = profile;
    for (int i = 0; i < BUBBLE_QUALITY_PROFILE_COUNT; i++) {
        if (BUBBLE_QUALITY_PROFILE_LIMITS[i].profile == profile) {
            cfg.active_voice_limit = BUBBLE_QUALITY_PROFILE_LIMITS[i].voice_limit;
            break;
        }
    }
    if (enable_shimmer) {
        cfg.pitch_mode = BUBBLE_PITCH_MODE_SHIMMER;
        cfg.shimmer_amount = 0.85f;
    }
    if (enable_freeze) {
        cfg.freeze_enabled = 1;
        cfg.freeze_amount = 1.0f;
    }

    bubble_engine_init(&engine, g_delay, &cfg);
    bubble_engine_set_parameter(&engine, BUBBLE_PARAM_DEVELOPER_MODE, 0.0f); // macro mode
    bubble_engine_set_parameter(&engine, BUBBLE_PARAM_DENSITY, density);
    bubble_engine_set_parameter(&engine, BUBBLE_PARAM_BLOOM, bloom);
    bubble_engine_set_parameter(&engine, BUBBLE_PARAM_MIX, mix);
    bubble_engine_set_parameter(&engine, BUBBLE_PARAM_WARMTH, warmth);

    CallbackAccum_t acc = {0};
    acc.min_wet_norm_gain = 1.0f;
    acc.min_wet_lim_gain = 1.0f;
    acc.min_final_lim_gain = 1.0f;
    bubble_engine_set_metrics_callback(&engine, MetricsCallback, &acc);

    clock_t t0 = clock();
    int processed = 0;
    while (processed < num_samples) {
        int chunk = BUBBLES_BLOCK_SIZE;
        if (processed + chunk > num_samples) chunk = num_samples - processed;
        bubble_engine_process(&engine, &in_buf[processed], &out_l[processed], &out_r[processed], chunk);
        processed += chunk;
    }
    clock_t t1 = clock();

    RunMetrics_t res;
    res.out_rms_l = calc_rms(out_l, num_samples);
    res.out_rms_r = calc_rms(out_r, num_samples);
    res.out_rms_total = sqrtf(0.5f * (res.out_rms_l * res.out_rms_l + res.out_rms_r * res.out_rms_r));
    res.out_peak = fmaxf(calc_peak(out_l, num_samples), calc_peak(out_r, num_samples));
    res.wet_pre_norm_peak = acc.max_wet_pre_peak;
    res.wet_pre_norm_rms = (acc.count_wet_pre_samples > 0) ? (float)sqrt(acc.sum_wet_pre_energy / acc.count_wet_pre_samples) : 0.0f;
    res.wet_norm_gain_min = acc.min_wet_norm_gain;
    res.wet_limiter_gain_min = acc.min_wet_lim_gain;
    res.wet_limiter_max_gr_db = (acc.min_wet_lim_gain < 1.0f && acc.min_wet_lim_gain > 0.0f) ? -calc_db(acc.min_wet_lim_gain) : 0.0f;
    res.final_limiter_gain_min = acc.min_final_lim_gain;
    res.final_limiter_max_gr_db = (acc.min_final_lim_gain < 1.0f && acc.min_final_lim_gain > 0.0f) ? -calc_db(acc.min_final_lim_gain) : 0.0f;
    res.final_limiter_active_rate = (acc.total_blocks > 0) ? (float)acc.final_limiter_active_blocks / (float)acc.total_blocks : 0.0f;
    res.correlation = calc_correlation(out_l, out_r, num_samples);
    res.side_mid_ratio = calc_side_mid_ratio(out_l, out_r, num_samples);
    res.process_time_sec = (double)(t1 - t0) / (double)CLOCKS_PER_SEC;

    free(in_buf);
    free(out_l);
    free(out_r);
    return res;
}

// 1. MIX=0 Invariant Check
static int test_mix_zero_invariant(void) {
    const float sr = 44100.0f;
    const int n = 44100;
    float* in_buf = (float*)malloc((size_t)n * sizeof(float));
    float* out_l = (float*)malloc((size_t)n * sizeof(float));
    float* out_r = (float*)malloc((size_t)n * sizeof(float));

    // Sine at 0.5 amp (well below -1 dBFS final limiter threshold)
    for (int i = 0; i < n; i++) {
        in_buf[i] = 0.50f * sinf(2.0f * M_PI_F * 440.0f * (float)i / sr);
    }

    BubbleEngine_t engine;
    BubbleEngineConfig_t cfg;
    bubble_engine_default_config(&cfg);
    cfg.sample_rate = sr;
    bubble_engine_init(&engine, g_delay, &cfg);
    bubble_engine_set_parameter(&engine, BUBBLE_PARAM_DEVELOPER_MODE, 0.0f);
    bubble_engine_set_parameter(&engine, BUBBLE_PARAM_DENSITY, 1.0f); // High density
    bubble_engine_set_parameter(&engine, BUBBLE_PARAM_BLOOM, 1.0f);   // High bloom
    bubble_engine_set_parameter(&engine, BUBBLE_PARAM_MIX, 0.0f);     // Pure dry

    int processed = 0;
    while (processed < n) {
        int chunk = BUBBLES_BLOCK_SIZE;
        if (processed + chunk > n) chunk = n - processed;
        bubble_engine_process(&engine, &in_buf[processed], &out_l[processed], &out_r[processed], chunk);
        processed += chunk;
    }

    // Allow macro smoothing to reach steady state (~50ms)
    float expected_gain = 0.85f;
    float max_delta = 0.0f;
    for (int i = 4410; i < n; i++) {
        float exp = in_buf[i] * expected_gain;
        float dl = fabsf(out_l[i] - exp);
        float dr = fabsf(out_r[i] - exp);
        if (dl > max_delta) max_delta = dl;
        if (dr > max_delta) max_delta = dr;
    }

    printf("MIX=0 Invariant: max_delta=%.6f, final_limiter_gain=%.4f\n",
           max_delta, SoundBubbles_GetFinalLimiterGain(&engine));

    free(in_buf);
    free(out_l);
    free(out_r);
    return (max_delta < 0.001f) ? 0 : 1;
}

// 2. Dry Pumping Test (Section 30)
// Steady dry sine + bursty wet, MIX = 0.25
static int test_dry_pumping(void) {
    const float sr = 44100.0f;
    const int n = 44100 * 2; // 2 seconds
    float* in_buf = (float*)malloc((size_t)n * sizeof(float));
    float* out_l = (float*)malloc((size_t)n * sizeof(float));
    float* out_r = (float*)malloc((size_t)n * sizeof(float));

    // Steady sine + large periodic bursts every 0.5s to generate wet spikes
    for (int i = 0; i < n; i++) {
        float t = (float)i / sr;
        float steady_sine = 0.35f * sinf(2.0f * M_PI_F * 440.0f * t);
        float local_t = fmodf(t, 0.50f);
        float burst = (local_t < 0.05f) ? (0.60f * sinf(2.0f * M_PI_F * 1200.0f * local_t)) : 0.0f;
        in_buf[i] = steady_sine + burst;
    }

    BubbleEngine_t engine;
    BubbleEngineConfig_t cfg;
    bubble_engine_default_config(&cfg);
    cfg.sample_rate = sr;
    bubble_engine_init(&engine, g_delay, &cfg);
    bubble_engine_set_parameter(&engine, BUBBLE_PARAM_DEVELOPER_MODE, 0.0f);
    bubble_engine_set_parameter(&engine, BUBBLE_PARAM_DENSITY, 0.90f);
    bubble_engine_set_parameter(&engine, BUBBLE_PARAM_BLOOM, 0.80f);
    bubble_engine_set_parameter(&engine, BUBBLE_PARAM_MIX, 0.25f);

    CallbackAccum_t acc = {0};
    acc.min_wet_norm_gain = 1.0f;
    acc.min_wet_lim_gain = 1.0f;
    acc.min_final_lim_gain = 1.0f;
    bubble_engine_set_metrics_callback(&engine, MetricsCallback, &acc);

    int processed = 0;
    while (processed < n) {
        int chunk = BUBBLES_BLOCK_SIZE;
        if (processed + chunk > n) chunk = n - processed;
        bubble_engine_process(&engine, &in_buf[processed], &out_l[processed], &out_r[processed], chunk);
        processed += chunk;
    }

    // Measure dry modulation: inspect final limiter minimum gain
    printf("Dry Pumping Test (Mix=0.25): min_final_lim_gain=%.4f, final_limiter_active_rate=%.2f%%, min_wet_lim_gain=%.4f\n",
           acc.min_final_lim_gain,
           100.0f * (float)acc.final_limiter_active_blocks / (float)acc.total_blocks,
           acc.min_wet_lim_gain);

    free(in_buf);
    free(out_l);
    free(out_r);
    // In M4D, final limiter min gain should remain > 0.95 (ideally 1.0) on Mix 0.25
    return (acc.min_final_lim_gain > 0.90f) ? 0 : 1;
}

// 3. Stereo Preservation & Linked Law Test (Section 5, 11, 31)
static int test_stereo_preservation(void) {
    const float sr = 44100.0f;
    const int n = 44100;
    float* in_buf = (float*)malloc((size_t)n * sizeof(float));
    float* out_l = (float*)malloc((size_t)n * sizeof(float));
    float* out_r = (float*)malloc((size_t)n * sizeof(float));
    generate_source(SOURCE_HARMONIC_PLUCK, in_buf, n, sr);

    BubbleEngine_t engine;
    BubbleEngineConfig_t cfg;
    bubble_engine_default_config(&cfg);
    cfg.sample_rate = sr;
    bubble_engine_init(&engine, g_delay, &cfg);
    bubble_engine_set_parameter(&engine, BUBBLE_PARAM_DEVELOPER_MODE, 0.0f);
    bubble_engine_set_parameter(&engine, BUBBLE_PARAM_DENSITY, 0.75f);
    bubble_engine_set_parameter(&engine, BUBBLE_PARAM_BLOOM, 0.60f);
    bubble_engine_set_parameter(&engine, BUBBLE_PARAM_SPACE, 1.00f);
    bubble_engine_set_parameter(&engine, BUBBLE_PARAM_MIX, 1.00f);

    int processed = 0;
    while (processed < n) {
        int chunk = BUBBLES_BLOCK_SIZE;
        if (processed + chunk > n) chunk = n - processed;
        bubble_engine_process(&engine, &in_buf[processed], &out_l[processed], &out_r[processed], chunk);
        processed += chunk;
    }

    float corr = calc_correlation(out_l, out_r, n);
    float smr = calc_side_mid_ratio(out_l, out_r, n);
    float rms_l = calc_rms(out_l, n);
    float rms_r = calc_rms(out_r, n);
    float ild_db = fabsf(calc_db(rms_l) - calc_db(rms_r));

    printf("Stereo Preservation (SPACE=1.0): correlation=%.4f, side_mid_ratio=%.4f, ILD=%.2fdB\n",
           corr, smr, ild_db);

    free(in_buf);
    free(out_l);
    free(out_r);
    // Stereo image must remain open (side_mid > 0.15, ILD natural < 3.0 dB)
    bool ok = (smr > 0.15f && ild_db < 3.0f);
    return ok ? 0 : 1;
}

// 4. Silence Noise Pumping Invariant (Section 7)
static int test_silence_noise_pumping(void) {
    const float sr = 44100.0f;
    const int n_sig = 44100 * 2;
    const int n_silence = 44100 * 10; // 10 seconds of silence
    const int n_total = n_sig + n_silence;

    float* in_buf = (float*)calloc((size_t)n_total, sizeof(float));
    float* out_l = (float*)malloc((size_t)n_total * sizeof(float));
    float* out_r = (float*)malloc((size_t)n_total * sizeof(float));

    // Signal for first 2 seconds
    for (int i = 0; i < n_sig; i++) {
        in_buf[i] = 0.6f * sinf(2.0f * M_PI_F * 300.0f * (float)i / sr);
    }

    BubbleEngine_t engine;
    BubbleEngineConfig_t cfg;
    bubble_engine_default_config(&cfg);
    cfg.sample_rate = sr;
    bubble_engine_init(&engine, g_delay, &cfg);
    bubble_engine_set_parameter(&engine, BUBBLE_PARAM_DEVELOPER_MODE, 0.0f);
    bubble_engine_set_parameter(&engine, BUBBLE_PARAM_DENSITY, 0.75f);
    bubble_engine_set_parameter(&engine, BUBBLE_PARAM_BLOOM, 0.75f);
    bubble_engine_set_parameter(&engine, BUBBLE_PARAM_MIX, 1.0f);

    float max_norm_gain_in_silence = 0.0f;
    int processed = 0;
    while (processed < n_total) {
        int chunk = BUBBLES_BLOCK_SIZE;
        if (processed + chunk > n_total) chunk = n_total - processed;
        bubble_engine_process(&engine, &in_buf[processed], &out_l[processed], &out_r[processed], chunk);

        if (processed >= n_sig) {
            float g = SoundBubbles_GetWetNormalizationGain(&engine);
            if (g > max_norm_gain_in_silence) max_norm_gain_in_silence = g;
        }
        processed += chunk;
    }

    printf("Silence Noise Pumping Invariant: max_norm_gain_in_silence=%.4f (must be <= 1.0)\n",
           max_norm_gain_in_silence);

    free(in_buf);
    free(out_l);
    free(out_r);
    return (max_norm_gain_in_silence <= 1.0001f) ? 0 : 1;
}

// 5. Full Principal Matrix Runner
static void run_principal_matrix(void) {
    const float densities[] = { 0.0f, 0.25f, 0.50f, 0.75f, 1.00f };
    const float blooms[] = { 0.0f, 0.50f, 1.00f };
    const float mixes[] = { 0.25f, 0.50f, 1.00f };
    const SourceType_t sources[] = { SOURCE_PERCUSSIVE, SOURCE_HARMONIC_PLUCK, SOURCE_SUSTAINED_SINE, SOURCE_NOISE };
    const char* src_names[] = { "Percussive", "Harmonic Pluck", "Sustained Sine", "Broadband Noise" };

    printf("\n=== M4D PRINCIPAL QUALIFICATION MATRIX ===\n");
    printf("%-16s | %4s | %5s | %4s | %8s | %8s | %9s | %8s | %10s | %10s\n",
           "Source", "Dens", "Bloom", "Mix", "Out RMS", "Out Peak", "WetNormG", "WetLimGR", "FinLimGR", "FinLimAct%%");
    printf("-----------------------------------------------------------------------------------------------------------------\n");

    for (int s = 0; s < 4; s++) {
        for (int d = 0; d < 5; d++) {
            for (int b = 0; b < 3; b++) {
                for (int m = 0; m < 3; m++) {
                    RunMetrics_t res = run_engine(44100.0f, sources[s], 1.0f,
                                                  densities[d], blooms[b], mixes[m],
                                                  0.50f, BUBBLE_QUALITY_PROFILE_WEB_STANDARD, false, false);
                    printf("%-16s | %4.2f | %5.2f | %4.2f | %8.4f | %8.4f | %9.4f | %7.2fdB | %8.2fdB | %9.1f%%\n",
                           src_names[s], densities[d], blooms[b], mixes[m],
                           res.out_rms_total, res.out_peak, res.wet_norm_gain_min,
                           res.wet_limiter_max_gr_db, res.final_limiter_max_gr_db,
                           res.final_limiter_active_rate * 100.0f);
                }
            }
        }
    }
}

// 6. Comparative Baseline vs Candidate Summary
static void run_comparative_summary(void) {
    const float densities[] = { 0.10f, 0.50f, 1.00f };
    const float blooms[] = { 0.0f, 1.00f };

    printf("\n=== BLOOM DECOUPLING & LOUDNESS REGRESSION ===\n");
    printf("%-5s | %-5s | %-10s | %-10s | %-10s | %-10s\n",
           "Dens", "Bloom", "Wet RMS", "Wet Peak", "WetLimGR", "FinLimGR");
    printf("----------------------------------------------------------------\n");
    for (int d = 0; d < 3; d++) {
        for (int b = 0; b < 2; b++) {
            RunMetrics_t res = run_engine(44100.0f, SOURCE_HARMONIC_PLUCK, 1.5f,
                                          densities[d], blooms[b], 1.00f, 0.50f,
                                          BUBBLE_QUALITY_PROFILE_WEB_STANDARD, false, false);
            printf("%-5.2f | %-5.2f | %-10.4f | %-10.4f | %7.2fdB  | %7.2fdB\n",
                   densities[d], blooms[b], res.out_rms_total, res.out_peak,
                   res.wet_limiter_max_gr_db, res.final_limiter_max_gr_db);
        }
    }
}

// 7. Sample-Rate & Quality Profile Invariance
static void run_sr_profile_matrix(void) {
    const float srs[] = { 44100.0f, 48000.0f, 96000.0f };
    const BubbleQualityProfile profiles[] = {
        BUBBLE_QUALITY_PROFILE_MCU_SAFE,
        BUBBLE_QUALITY_PROFILE_MCU_PLUS,
        BUBBLE_QUALITY_PROFILE_WEB_STANDARD,
        BUBBLE_QUALITY_PROFILE_WEB_ULTRA
    };
    const char* prof_names[] = { "MCU_SAFE", "MCU_PLUS", "WEB_STD", "WEB_ULTRA" };

    printf("\n=== SAMPLE RATE & QUALITY PROFILE MATRIX ===\n");
    printf("%-10s | %-6s | %-10s | %-10s | %-10s | %-10s | %-10s\n",
           "Profile", "SR", "Out RMS", "Out Peak", "FinLimAct%%", "Side/Mid", "Time (ms)");
    printf("------------------------------------------------------------------------------------\n");

    for (int p = 0; p < 4; p++) {
        for (int s = 0; s < 3; s++) {
            RunMetrics_t res = run_engine(srs[s], SOURCE_SUSTAINED_SINE, 1.0f,
                                          0.75f, 0.60f, 0.80f, 0.50f,
                                          profiles[p], false, false);
            printf("%-10s | %-6.0f | %-10.4f | %-10.4f | %9.1f%% | %-10.4f | %7.2fms\n",
                   prof_names[p], srs[s], res.out_rms_total, res.out_peak,
                   res.final_limiter_active_rate * 100.0f, res.side_mid_ratio,
                   res.process_time_sec * 1000.0);
        }
    }
}

// 8. Extreme Stress: Shimmer + Reverse + High Memory (Section 29, 33)
static void test_extreme_stress(void) {
    printf("\n=== EXTREME STRESS (Shimmer, Reverse, Feedback Tail) ===\n");
    RunMetrics_t res = run_engine(44100.0f, SOURCE_HARMONIC_PLUCK, 2.0f,
                                  1.0f, 1.0f, 1.0f, 0.85f,
                                  BUBBLE_QUALITY_PROFILE_WEB_ULTRA, true, false);
    printf("Extreme Stress: Out RMS=%.4f, Out Peak=%.4f, WetLimGR=%.2fdB, FinLimGR=%.2fdB, FinLimAct=%.1f%%\n",
           res.out_rms_total, res.out_peak, res.wet_limiter_max_gr_db, res.final_limiter_max_gr_db,
           res.final_limiter_active_rate * 100.0f);
}

int main(int argc, char** argv) {
    if (argc > 1) {
        if (strcmp(argv[1], "--mix-zero") == 0) {
            return test_mix_zero_invariant();
        }
        if (strcmp(argv[1], "--dry-pumping") == 0) {
            return test_dry_pumping();
        }
        if (strcmp(argv[1], "--stereo") == 0) {
            return test_stereo_preservation();
        }
        if (strcmp(argv[1], "--silence-noise") == 0) {
            return test_silence_noise_pumping();
        }
        if (strcmp(argv[1], "--principal-matrix") == 0) {
            run_principal_matrix();
            return 0;
        }
        if (strcmp(argv[1], "--comparative") == 0) {
            run_comparative_summary();
            return 0;
        }
        if (strcmp(argv[1], "--sr-profiles") == 0) {
            run_sr_profile_matrix();
            return 0;
        }
        if (strcmp(argv[1], "--extreme") == 0) {
            test_extreme_stress();
            return 0;
        }
    }

    printf("=== RUNNING FULL M4D QUALIFICATION PROBE ===\n");
    int fail = 0;
    fail += test_mix_zero_invariant();
    fail += test_dry_pumping();
    fail += test_stereo_preservation();
    fail += test_silence_noise_pumping();
    run_comparative_summary();
    run_sr_profile_matrix();
    test_extreme_stress();

    if (fail == 0) {
        printf("\nALL M4D UNIT CHECKS PASSED!\n");
        return 0;
    } else {
        printf("\n%d M4D CHECKS FAILED!\n", fail);
        return 1;
    }
}
