// M4D.1 Wet Dynamics Qualification Freeze Probe
// Single authoritative harness for exhaustive, reproducible historical A/B
// and candidate qualification across all 24 milestone requirements.

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

// Helper: safe dB conversion
static inline float to_db(float lin) {
    if (lin <= 1e-9f) return -180.0f;
    return 20.0f * log10f(lin);
}

static inline float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

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

// Generate deterministic sources
typedef enum {
    SRC_SUSTAINED_SINE = 0,
    SRC_BROADBAND_NOISE,
    SRC_HARMONIC_PLUCK,
    SRC_BURSTY_DRY_PUMPING
} SourceType_t;

static void generate_source(SourceType_t type, float* buf, int n, float sr) {
    memset(buf, 0, (size_t)n * sizeof(float));
    switch (type) {
        case SRC_SUSTAINED_SINE:
            for (int i = 0; i < n; i++) {
                float t = (float)i / sr;
                buf[i] = 0.50f * sinf(2.0f * M_PI_F * 330.0f * t) +
                         0.20f * sinf(2.0f * M_PI_F * 660.0f * t);
            }
            break;
        case SRC_BROADBAND_NOISE:
            for (int i = 0; i < n; i++) {
                // Deterministic pseudo-random sequence
                uint32_t x = (uint32_t)(i * 1664525u + 1013904223u);
                float noise = ((float)(x >> 8) / 8388608.0f) - 1.0f;
                buf[i] = 0.45f * noise;
            }
            break;
        case SRC_HARMONIC_PLUCK:
            for (int i = 0; i < n; i++) {
                float t = (float)i / sr;
                float lt = fmodf(t, 0.40f);
                float env = expf(-lt * 14.0f);
                float s = sinf(2.0f * M_PI_F * 220.0f * lt) +
                          0.50f * sinf(2.0f * M_PI_F * 440.0f * lt) +
                          0.25f * sinf(2.0f * M_PI_F * 660.0f * lt);
                buf[i] = 0.60f * s * env;
            }
            break;
        case SRC_BURSTY_DRY_PUMPING:
            // Section 3: steady dry sine (220 Hz, 0.72 amplitude)
            // PLUS periodic transients every 0.35s that excite high-density wet bursts
            for (int i = 0; i < n; i++) {
                float t = (float)i / sr;
                float steady_sine = 0.72f * sinf(2.0f * M_PI_F * 220.0f * t);
                float lt = fmodf(t, 0.35f);
                float burst = (lt < 0.05f) ? (0.26f * sinf(2.0f * M_PI_F * 880.0f * lt)) : 0.0f;
                buf[i] = steady_sine + burst;
            }
            break;
    }
}

// Common engine run context
typedef struct {
    float out_rms;
    float out_peak;
    float final_lim_min_gain;
    float final_lim_max_gr_db;
    float final_lim_act_pct;
    float wet_norm_min_gain;
    float wet_lim_min_gain;
    float wet_lim_max_gr_db;
    double process_time_sec;
} EngineRunSummary_t;

typedef struct {
    int total_blocks;
    int final_lim_act_blocks;
    float min_final_lim;
    float min_wet_norm;
    float min_wet_lim;
} Accumulator_t;

static void MetricsCallback(const BubbleEngineBlockMetrics_t* m, void* user) {
    Accumulator_t* acc = (Accumulator_t*)user;
    if (m == NULL || acc == NULL) return;
#if defined(M4D_CANDIDATE_BUILD)
    float fl = m->final_limiter_gain > 0.0f ? m->final_limiter_gain : m->limiter_gain;
#else
    float fl = m->limiter_gain;
#endif
    if (fl < 0.999f) acc->final_lim_act_blocks++;
    if (fl > 0.0f && fl < acc->min_final_lim) acc->min_final_lim = fl;

#if defined(M4D_CANDIDATE_BUILD)
    if (m->wet_normalization_gain > 0.0f && m->wet_normalization_gain < acc->min_wet_norm)
        acc->min_wet_norm = m->wet_normalization_gain;
    if (m->wet_limiter_gain > 0.0f && m->wet_limiter_gain < acc->min_wet_lim)
        acc->min_wet_lim = m->wet_limiter_gain;
#endif
}

static EngineRunSummary_t run_engine(float sr, SourceType_t src, float duration_s,
                                     float density, float bloom, float mix, float warmth,
                                     BubbleQualityProfile prof, bool shimmer, bool freeze) {
    int n = (int)(sr * duration_s);
    float* in_buf = (float*)malloc((size_t)n * sizeof(float));
    float* out_l = (float*)malloc((size_t)n * sizeof(float));
    float* out_r = (float*)malloc((size_t)n * sizeof(float));
    generate_source(src, in_buf, n, sr);

    BubbleEngine_t engine;
    BubbleEngineConfig_t cfg;
    bubble_engine_default_config(&cfg);
    cfg.sample_rate = sr;
    cfg.quality_profile = prof;
    for (int i = 0; i < BUBBLE_QUALITY_PROFILE_COUNT; i++) {
        if (BUBBLE_QUALITY_PROFILE_LIMITS[i].profile == prof) {
            cfg.active_voice_limit = BUBBLE_QUALITY_PROFILE_LIMITS[i].voice_limit;
            break;
        }
    }
    if (shimmer) {
        cfg.pitch_mode = BUBBLE_PITCH_MODE_SHIMMER;
        cfg.shimmer_amount = 0.90f;
    }
    if (freeze) {
        cfg.freeze_enabled = 1;
        cfg.freeze_amount = 1.0f;
    }

    bubble_engine_init(&engine, g_delay, &cfg);
    bubble_engine_set_parameter(&engine, BUBBLE_PARAM_DEVELOPER_MODE, 0.0f);
    bubble_engine_set_parameter(&engine, BUBBLE_PARAM_DENSITY, density);
    bubble_engine_set_parameter(&engine, BUBBLE_PARAM_BLOOM, bloom);
    bubble_engine_set_parameter(&engine, BUBBLE_PARAM_MIX, mix);
    bubble_engine_set_parameter(&engine, BUBBLE_PARAM_WARMTH, warmth);

    Accumulator_t acc = {0};
    acc.min_final_lim = 1.0f;
    acc.min_wet_norm = 1.0f;
    acc.min_wet_lim = 1.0f;
    bubble_engine_set_metrics_callback(&engine, MetricsCallback, &acc);

    clock_t t0 = clock();
    int processed = 0;
    while (processed < n) {
        int chunk = BUBBLES_BLOCK_SIZE;
        if (processed + chunk > n) chunk = n - processed;
        bubble_engine_process(&engine, &in_buf[processed], &out_l[processed], &out_r[processed], chunk);
        processed += chunk;
    }
    clock_t t1 = clock();

    float rms_l = calc_rms(out_l, n);
    float rms_r = calc_rms(out_r, n);
    float peak_l = calc_peak(out_l, n);
    float peak_r = calc_peak(out_r, n);

    EngineRunSummary_t res;
    res.out_rms = sqrtf(0.5f * (rms_l * rms_l + rms_r * rms_r));
    res.out_peak = fmaxf(peak_l, peak_r);
    res.final_lim_min_gain = acc.min_final_lim;
    res.final_lim_max_gr_db = (acc.min_final_lim < 1.0f && acc.min_final_lim > 0.0f) ? -to_db(acc.min_final_lim) : 0.0f;
    res.final_lim_act_pct = (acc.total_blocks > 0) ? (100.0f * (float)acc.final_lim_act_blocks / (float)acc.total_blocks) : 0.0f;
    res.wet_norm_min_gain = acc.min_wet_norm;
    res.wet_lim_min_gain = acc.min_wet_lim;
    res.wet_lim_max_gr_db = (acc.min_wet_lim < 1.0f && acc.min_wet_lim > 0.0f) ? -to_db(acc.min_wet_lim) : 0.0f;
    res.process_time_sec = (double)(t1 - t0) / (double)CLOCKS_PER_SEC;

    free(in_buf);
    free(out_l);
    free(out_r);
    return res;
}

// -------------------------------------------------------------
// SECTION 2: Baseline Limiter Metrics
// -------------------------------------------------------------
static void run_limiter_matrix(void) {
    const float densities[] = { 0.50f, 0.75f, 1.00f };
    const float blooms[] = { 0.00f, 1.00f };
    const float mixes[] = { 0.25f, 1.00f };
    const SourceType_t sources[] = { SRC_SUSTAINED_SINE, SRC_BROADBAND_NOISE, SRC_HARMONIC_PLUCK };
    const char* src_names[] = { "Sustained Sine", "Broadband Noise", "Harmonic Pluck" };

    printf("SOURCE,DENSITY,BLOOM,MIX,OUT_RMS,OUT_PEAK,FIN_LIM_MIN,FIN_LIM_MAX_GR_DB,FIN_LIM_ACT_PCT\n");
    for (int s = 0; s < 3; s++) {
        for (int d = 0; d < 3; d++) {
            for (int b = 0; b < 2; b++) {
                for (int m = 0; m < 2; m++) {
                    EngineRunSummary_t r = run_engine(44100.0f, sources[s], 1.0f,
                                                      densities[d], blooms[b], mixes[m], 0.50f,
                                                      BUBBLE_QUALITY_PROFILE_WEB_STANDARD, false, false);
                    printf("%s,%.2f,%.2f,%.2f,%.4f,%.4f,%.4f,%.2f,%.1f\n",
                           src_names[s], densities[d], blooms[b], mixes[m],
                           r.out_rms, r.out_peak, r.final_lim_min_gain, r.final_lim_max_gr_db, r.final_lim_act_pct);
                }
            }
        }
    }
}

// -------------------------------------------------------------
// SECTION 3-8: Direct Dry Pumping Measurement
// -------------------------------------------------------------
static void run_direct_dry_pumping(void) {
    const float sr = 44100.0f;
    const float duration_s = 3.5f;
    const int n = (int)(sr * duration_s);

    float* in_buf = (float*)malloc((size_t)n * sizeof(float));
    float* wet_l = (float*)malloc((size_t)n * sizeof(float));
    float* wet_r = (float*)malloc((size_t)n * sizeof(float));
    float* dry_mono = (float*)malloc((size_t)n * sizeof(float));
    float* full_l = (float*)malloc((size_t)n * sizeof(float));
    float* full_r = (float*)malloc((size_t)n * sizeof(float));
    float* g_lim_hist = (float*)malloc((size_t)n * sizeof(float));

    generate_source(SRC_BURSTY_DRY_PUMPING, in_buf, n, sr);

    BubbleEngine_t engine;
    BubbleEngineConfig_t cfg;
    bubble_engine_default_config(&cfg);
    cfg.sample_rate = sr;
    cfg.quality_profile = BUBBLE_QUALITY_PROFILE_WEB_ULTRA;
    cfg.burst_mode = BUBBLE_BURST_MODE_SPRAY;
    cfg.pitch_mode = BUBBLE_PITCH_MODE_SHIMMER;
    cfg.shimmer_amount = 0.80f;
    bubble_engine_init(&engine, g_delay, &cfg);
    bubble_engine_set_parameter(&engine, BUBBLE_PARAM_DEVELOPER_MODE, 0.0f);
    bubble_engine_set_parameter(&engine, BUBBLE_PARAM_DENSITY, 1.00f);
    bubble_engine_set_parameter(&engine, BUBBLE_PARAM_BLOOM, 1.00f);
    bubble_engine_set_parameter(&engine, BUBBLE_PARAM_MIX, 0.25f);
    bubble_engine_set_parameter(&engine, BUBBLE_PARAM_WARMTH, 0.85f);

    // Process spatial split to obtain dry and wet components deterministically
    int processed = 0;
    while (processed < n) {
        int chunk = BUBBLES_BLOCK_SIZE;
        if (processed + chunk > n) chunk = n - processed;
        bubble_engine_process_spatial(&engine, &in_buf[processed],
                                      &wet_l[processed], &wet_r[processed],
                                      &dry_mono[processed], chunk);
        for (int i = 0; i < chunk; i++) {
            int idx = processed + i;
            full_l[idx] = dry_mono[idx] + wet_l[idx];
            full_r[idx] = dry_mono[idx] + wet_r[idx];
            bubble_engine_apply_final_limiter(&engine, &full_l[idx], &full_r[idx], 1);
            g_lim_hist[idx] = engine.final_limiter_gain;
        }
        processed += chunk;
    }

    // Measure effective dry gain:
    // Ratio of actual output dry component to input dry reference:
    // actual_dry(t) = dry_mono(t) * g_lim(t).
    // G_eff(w) = RMS(actual_dry) / RMS(dry_ref).
    // In dB: G_dry_db(w) = 20 * log10(G_eff).
    const int win_size = (int)(sr * 0.010f); // 10 ms
    const int start_sample = (int)(sr * 0.25f); // stationary region after initial 250ms smoothing
    const int end_sample = n - win_size;

    int num_win = (end_sample - start_sample) / (win_size / 2);
    if (num_win < 1) num_win = 1;

    float* dry_env_db = (float*)malloc((size_t)num_win * sizeof(float));
    float* wet_energy_env = (float*)malloc((size_t)num_win * sizeof(float));

    float max_dry_db = -999.0f;
    float min_dry_db = 999.0f;
    double sum_dry_db = 0.0;
    double sum_dry_db_sq = 0.0;

    int idx_win = 0;
    for (int s = start_sample; s < end_sample && idx_win < num_win; s += (win_size / 2)) {
        double d_ref_energy = 0.0;
        double d_act_energy = 0.0;
        double w_energy = 0.0;
        for (int k = 0; k < win_size; k++) {
            int idx = s + k;
            double d = (double)dry_mono[idx];
            double g = (double)g_lim_hist[idx];
            double d_act = d * g;
            d_ref_energy += d * d;
            d_act_energy += d_act * d_act;
            double wl = (double)wet_l[idx];
            double wr = (double)wet_r[idx];
            w_energy += 0.5 * (wl * wl + wr * wr);
        }
        double rms_ref = sqrt(d_ref_energy / (double)win_size);
        double rms_act = sqrt(d_act_energy / (double)win_size);
        double eff_gain = (rms_ref > 1e-9) ? (rms_act / rms_ref) : 1.0;
        float d_gain_db = to_db((float)eff_gain);
        dry_env_db[idx_win] = d_gain_db;
        wet_energy_env[idx_win] = (float)(w_energy / (double)win_size);

        if (d_gain_db > max_dry_db) max_dry_db = d_gain_db;
        if (d_gain_db < min_dry_db) min_dry_db = d_gain_db;
        sum_dry_db += d_gain_db;
        sum_dry_db_sq += d_gain_db * d_gain_db;
        idx_win++;
    }

    num_win = idx_win;
    float mean_db = (float)(sum_dry_db / num_win);
    float var_db = (float)((sum_dry_db_sq / num_win) - (mean_db * mean_db));
    float stddev_db = (var_db > 0.0f) ? sqrtf(var_db) : 0.0f;
    float mod_depth_db = max_dry_db - min_dry_db;

    // Correlation between wet burst energy and negative dry gain (pumping correlation)
    // Negative dry gain = mean_db - dry_env_db[i]
    double sum_w = 0.0, sum_d_neg = 0.0;
    for (int i = 0; i < num_win; i++) {
        sum_w += wet_energy_env[i];
        sum_d_neg += (mean_db - dry_env_db[i]);
    }
    double mean_w = sum_w / num_win;
    double mean_d_neg = sum_d_neg / num_win;

    double num_corr = 0.0, den_w = 0.0, den_d = 0.0;
    for (int i = 0; i < num_win; i++) {
        double dw = wet_energy_env[i] - mean_w;
        double dd = (mean_db - dry_env_db[i]) - mean_d_neg;
        num_corr += dw * dd;
        den_w += dw * dw;
        den_d += dd * dd;
    }
    double denom = sqrt(den_w * den_d);
    float corr = (denom > 1e-12) ? (float)(num_corr / denom) : 0.0f;

    printf("dry_gain_mean_db: %.4f\n", mean_db);
    printf("dry_gain_min_db: %.4f\n", min_dry_db);
    printf("dry_gain_max_db: %.4f\n", max_dry_db);
    printf("dry_modulation_depth_db: %.4f\n", mod_depth_db);
    printf("dry_gain_stddev_db: %.4f\n", stddev_db);
    printf("wet_burst_correlation: %.4f\n", corr);

    free(in_buf);
    free(wet_l);
    free(wet_r);
    free(dry_mono);
    free(full_l);
    free(full_r);
    free(g_lim_hist);
    free(dry_env_db);
    free(wet_energy_env);
}

// -------------------------------------------------------------
// SECTION 9-11: Normalization Telemetry Distribution & Hierarchy
// -------------------------------------------------------------
static int compare_floats(const void* a, const void* b) {
    float fa = *(const float*)a;
    float fb = *(const float*)b;
    return (fa > fb) - (fa < fb);
}

static void run_normalization_stats(void) {
    const char* tiers[] = { "Normal", "Dense", "Extreme" };
    const float dens[] = { 0.35f, 0.80f, 1.00f };
    const float blooms[] = { 0.35f, 0.75f, 1.00f };
    const float warmths[] = { 0.40f, 0.60f, 0.85f };
    const BubbleQualityProfile profs[] = {
        BUBBLE_QUALITY_PROFILE_WEB_STANDARD,
        BUBBLE_QUALITY_PROFILE_WEB_STANDARD,
        BUBBLE_QUALITY_PROFILE_WEB_ULTRA
    };

    printf("TIER,MIN_NORM,MEDIAN_NORM,P95_GR_DB,MEAN_NORM,TIME_LT_095_PCT,TIME_LT_080_PCT,TIME_NEAR_045_PCT,WET_LIM_MAX_GR_DB,FIN_LIM_MAX_GR_DB,FIN_LIM_ACT_PCT\n");

    for (int t = 0; t < 3; t++) {
        const float sr = 44100.0f;
        const int n = (int)(sr * 3.0f);
        float* in_buf = (float*)malloc((size_t)n * sizeof(float));
        float* out_l = (float*)malloc((size_t)n * sizeof(float));
        float* out_r = (float*)malloc((size_t)n * sizeof(float));
        generate_source(SRC_SUSTAINED_SINE, in_buf, n, sr);

        BubbleEngine_t engine;
        BubbleEngineConfig_t cfg;
        bubble_engine_default_config(&cfg);
        cfg.sample_rate = sr;
        cfg.quality_profile = profs[t];
        for (int i = 0; i < BUBBLE_QUALITY_PROFILE_COUNT; i++) {
            if (BUBBLE_QUALITY_PROFILE_LIMITS[i].profile == profs[t]) {
                cfg.active_voice_limit = BUBBLE_QUALITY_PROFILE_LIMITS[i].voice_limit;
                break;
            }
        }
        if (t == 2) { // Extreme
            cfg.pitch_mode = BUBBLE_PITCH_MODE_SHIMMER;
            cfg.shimmer_amount = 0.90f;
            cfg.reverse_probability = 0.35f;
        }

        bubble_engine_init(&engine, g_delay, &cfg);
        bubble_engine_set_parameter(&engine, BUBBLE_PARAM_DEVELOPER_MODE, 0.0f);
        bubble_engine_set_parameter(&engine, BUBBLE_PARAM_DENSITY, dens[t]);
        bubble_engine_set_parameter(&engine, BUBBLE_PARAM_BLOOM, blooms[t]);
        bubble_engine_set_parameter(&engine, BUBBLE_PARAM_MIX, 1.0f);
        bubble_engine_set_parameter(&engine, BUBBLE_PARAM_WARMTH, warmths[t]);

        int max_blocks = (n / BUBBLES_BLOCK_SIZE) + 1;
        float* norm_samples = (float*)malloc((size_t)max_blocks * sizeof(float));
        int num_blocks = 0;
        int act_fl_blocks = 0;
        float min_fl = 1.0f;
        float min_wl = 1.0f;

        int processed = 0;
        while (processed < n) {
            int chunk = BUBBLES_BLOCK_SIZE;
            if (processed + chunk > n) chunk = n - processed;
            bubble_engine_process(&engine, &in_buf[processed], &out_l[processed], &out_r[processed], chunk);

            float ng = 1.0f;
            float wl = 1.0f;
            float fl = 1.0f;
#if defined(M4D_CANDIDATE_BUILD)
            ng = SoundBubbles_GetWetNormalizationGain(&engine);
            wl = SoundBubbles_GetWetLimiterGain(&engine);
            fl = SoundBubbles_GetFinalLimiterGain(&engine);
#else
            fl = engine.final_limiter_gain;
#endif
            if (ng < 1e-4f) ng = 1.0f;
            norm_samples[num_blocks++] = ng;
            if (fl < 0.999f) act_fl_blocks++;
            if (fl < min_fl) min_fl = fl;
            if (wl < min_wl) min_wl = wl;

            processed += chunk;
        }

        // Sort for percentiles
        qsort(norm_samples, (size_t)num_blocks, sizeof(float), compare_floats);

        float min_norm = norm_samples[0];
        float median_norm = norm_samples[num_blocks / 2];
        // p95 of gain reduction corresponds to 5th percentile of gain
        float p05_gain = norm_samples[(int)(num_blocks * 0.05f)];
        float p95_gr_db = -to_db(p05_gain);

        double sum_norm = 0.0;
        int count_lt_095 = 0;
        int count_lt_080 = 0;
        int count_near_045 = 0;
        for (int i = 0; i < num_blocks; i++) {
            float g = norm_samples[i];
            sum_norm += g;
            if (g < 0.95f) count_lt_095++;
            if (g < 0.80f) count_lt_080++;
            if (g <= 0.48f) count_near_045++;
        }
        float mean_norm = (float)(sum_norm / num_blocks);
        float pct_lt_095 = 100.0f * (float)count_lt_095 / (float)num_blocks;
        float pct_lt_080 = 100.0f * (float)count_lt_080 / (float)num_blocks;
        float pct_near_045 = 100.0f * (float)count_near_045 / (float)num_blocks;

        float wet_lim_gr = (min_wl < 1.0f) ? -to_db(min_wl) : 0.0f;
        float fin_lim_gr = (min_fl < 1.0f) ? -to_db(min_fl) : 0.0f;
        float fin_act_pct = 100.0f * (float)act_fl_blocks / (float)num_blocks;

        printf("%s,%.4f,%.4f,%.2f,%.4f,%.1f,%.1f,%.1f,%.2f,%.2f,%.1f\n",
               tiers[t], min_norm, median_norm, p95_gr_db, mean_norm,
               pct_lt_095, pct_lt_080, pct_near_045, wet_lim_gr, fin_lim_gr, fin_act_pct);

        free(in_buf);
        free(out_l);
        free(out_r);
        free(norm_samples);
    }
}

// -------------------------------------------------------------
// SECTION 13: BLOOM Historical A/B
// -------------------------------------------------------------
static void run_bloom_ab(void) {
    const float densities[] = { 0.25f, 0.50f, 0.75f, 1.00f };

    printf("DENSITY,OUT_RMS_B0,OUT_RMS_B1,DELTA_OUT_RMS_DB,FIN_LIM_GR_B0,FIN_LIM_GR_B1\n");
    for (int d = 0; d < 4; d++) {
        EngineRunSummary_t r0 = run_engine(44100.0f, SRC_HARMONIC_PLUCK, 1.5f,
                                           densities[d], 0.0f, 1.0f, 0.50f,
                                           BUBBLE_QUALITY_PROFILE_WEB_STANDARD, false, false);
        EngineRunSummary_t r1 = run_engine(44100.0f, SRC_HARMONIC_PLUCK, 1.5f,
                                           densities[d], 1.0f, 1.0f, 0.50f,
                                           BUBBLE_QUALITY_PROFILE_WEB_STANDARD, false, false);
        float delta_db = to_db(r1.out_rms) - to_db(r0.out_rms);
        printf("%.2f,%.4f,%.4f,%.2f,%.2f,%.2f\n",
               densities[d], r0.out_rms, r1.out_rms, delta_db, r0.final_lim_max_gr_db, r1.final_lim_max_gr_db);
    }
}

// -------------------------------------------------------------
// SECTION 14: Tail Preservation
// -------------------------------------------------------------
static void run_tail_windows(void) {
    const float sr = 44100.0f;
    const float total_s = 10.5f;
    const int n = (int)(sr * total_s);
    float* in_buf = (float*)calloc((size_t)n, sizeof(float));
    float* out_l = (float*)malloc((size_t)n * sizeof(float));
    float* out_r = (float*)malloc((size_t)n * sizeof(float));

    // 500 ms harmonic pluck excitation
    int n_stim = (int)(sr * 0.50f);
    for (int i = 0; i < n_stim; i++) {
        float t = (float)i / sr;
        in_buf[i] = 0.70f * sinf(2.0f * M_PI_F * 220.0f * t) * expf(-t * 8.0f);
    }

    BubbleEngine_t engine;
    BubbleEngineConfig_t cfg;
    bubble_engine_default_config(&cfg);
    cfg.sample_rate = sr;
    cfg.quality_profile = BUBBLE_QUALITY_PROFILE_WEB_STANDARD;
    bubble_engine_init(&engine, g_delay, &cfg);
    bubble_engine_set_parameter(&engine, BUBBLE_PARAM_DEVELOPER_MODE, 0.0f);
    bubble_engine_set_parameter(&engine, BUBBLE_PARAM_DENSITY, 0.75f);
    bubble_engine_set_parameter(&engine, BUBBLE_PARAM_BLOOM, 0.75f);
    bubble_engine_set_parameter(&engine, BUBBLE_PARAM_MIX, 1.0f);
    bubble_engine_set_parameter(&engine, BUBBLE_PARAM_WARMTH, 0.50f);

    int processed = 0;
    while (processed < n) {
        int chunk = BUBBLES_BLOCK_SIZE;
        if (processed + chunk > n) chunk = n - processed;
        bubble_engine_process(&engine, &in_buf[processed], &out_l[processed], &out_r[processed], chunk);
        processed += chunk;
    }

    // Windows: 0.5-1, 1-2, 2-4, 4-6, 6-8, 8-10 s
    const float w_starts[] = { 0.5f, 1.0f, 2.0f, 4.0f, 6.0f, 8.0f };
    const float w_ends[]   = { 1.0f, 2.0f, 4.0f, 6.0f, 8.0f, 10.0f };

    printf("WINDOW,RMS,RMS_DB,CENTROID_HZ\n");
    for (int w = 0; w < 6; w++) {
        int s0 = (int)(w_starts[w] * sr);
        int s1 = (int)(w_ends[w] * sr);
        int count = s1 - s0;
        float rms_l = calc_rms(&out_l[s0], count);
        float rms_r = calc_rms(&out_r[s0], count);
        float rms = sqrtf(0.5f * (rms_l * rms_l + rms_r * rms_r));
        float cent = calc_centroid(&out_l[s0], count, sr);
        printf("%.1f-%.1fs,%.5f,%.2f,%.1f\n",
               w_starts[w], w_ends[w], rms, to_db(rms), cent);
    }

    free(in_buf);
    free(out_l);
    free(out_r);
}

// -------------------------------------------------------------
// SECTION 15: Stereo Preservation
// -------------------------------------------------------------
static void run_stereo_check(void) {
    const float sr = 44100.0f;
    const int n = (int)(sr * 2.0f);
    float* in_buf = (float*)malloc((size_t)n * sizeof(float));
    float* out_l = (float*)malloc((size_t)n * sizeof(float));
    float* out_r = (float*)malloc((size_t)n * sizeof(float));
    generate_source(SRC_HARMONIC_PLUCK, in_buf, n, sr);

    BubbleEngine_t engine;
    BubbleEngineConfig_t cfg;
    bubble_engine_default_config(&cfg);
    cfg.sample_rate = sr;
    cfg.quality_profile = BUBBLE_QUALITY_PROFILE_WEB_STANDARD;
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
    float ild_db = fabsf(to_db(rms_l) - to_db(rms_r));

    printf("CORRELATION: %.4f\n", corr);
    printf("SIDE_MID_RATIO: %.4f\n", smr);
    printf("ILD_DB: %.4f\n", ild_db);

    free(in_buf);
    free(out_l);
    free(out_r);
}

// -------------------------------------------------------------
// SECTION 16: MIX=0 Historical Proof
// -------------------------------------------------------------
static void run_mix_zero_check(void) {
    const float sr = 44100.0f;
    const int n = 44100 * 2;
    float* in_buf = (float*)malloc((size_t)n * sizeof(float));
    float* out_l = (float*)malloc((size_t)n * sizeof(float));
    float* out_r = (float*)malloc((size_t)n * sizeof(float));

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

    // Steady state dry gain is 0.85
    float expected_gain = 0.85f;
    float max_delta = 0.0f;
    for (int i = 4410; i < n; i++) {
        float exp = in_buf[i] * expected_gain;
        float dl = fabsf(out_l[i] - exp);
        float dr = fabsf(out_r[i] - exp);
        if (dl > max_delta) max_delta = dl;
        if (dr > max_delta) max_delta = dr;
    }

    float fl_gain = 1.0f;
#if defined(M4D_CANDIDATE_BUILD)
    fl_gain = SoundBubbles_GetFinalLimiterGain(&engine);
#else
    fl_gain = engine.final_limiter_gain;
#endif

    printf("MIX_ZERO_MAX_DELTA: %.6f\n", max_delta);
    printf("MIX_ZERO_FINAL_LIMITER: %.4f\n", fl_gain);

    free(in_buf);
    free(out_l);
    free(out_r);
}

// -------------------------------------------------------------
// SECTION 17: Silence Invariant
// -------------------------------------------------------------
static void run_silence_check(void) {
    const float sr = 44100.0f;
    // 1. Startup silence (30 seconds)
    const int n_startup = (int)(sr * 30.0f);
    float* silent_buf = (float*)calloc((size_t)n_startup, sizeof(float));
    float* out_l = (float*)malloc((size_t)n_startup * sizeof(float));
    float* out_r = (float*)malloc((size_t)n_startup * sizeof(float));

    BubbleEngine_t engine;
    BubbleEngineConfig_t cfg;
    bubble_engine_default_config(&cfg);
    cfg.sample_rate = sr;
    bubble_engine_init(&engine, g_delay, &cfg);
    bubble_engine_set_parameter(&engine, BUBBLE_PARAM_DEVELOPER_MODE, 0.0f);
    bubble_engine_set_parameter(&engine, BUBBLE_PARAM_DENSITY, 1.0f);
    bubble_engine_set_parameter(&engine, BUBBLE_PARAM_BLOOM, 1.0f);
    bubble_engine_set_parameter(&engine, BUBBLE_PARAM_MIX, 1.0f);

    int processed = 0;
    float max_startup_peak = 0.0f;
    while (processed < n_startup) {
        int chunk = BUBBLES_BLOCK_SIZE;
        if (processed + chunk > n_startup) chunk = n_startup - processed;
        bubble_engine_process(&engine, &silent_buf[processed], &out_l[processed], &out_r[processed], chunk);
        float p = fmaxf(calc_peak(&out_l[processed], chunk), calc_peak(&out_r[processed], chunk));
        if (p > max_startup_peak) max_startup_peak = p;
        processed += chunk;
    }

    // 2. 2s signal followed by 15s silence
    const int n_sig = (int)(sr * 2.0f);
    const int n_post_silence = (int)(sr * 15.0f);
    const int n_total2 = n_sig + n_post_silence;
    float* sig_buf = (float*)calloc((size_t)n_total2, sizeof(float));
    for (int i = 0; i < n_sig; i++) {
        sig_buf[i] = 0.6f * sinf(2.0f * M_PI_F * 440.0f * (float)i / sr);
    }

    bubble_engine_reset(&engine);
    bubble_engine_set_parameter(&engine, BUBBLE_PARAM_DENSITY, 0.8f);
    bubble_engine_set_parameter(&engine, BUBBLE_PARAM_BLOOM, 0.8f);
    bubble_engine_set_parameter(&engine, BUBBLE_PARAM_MIX, 1.0f);

    float max_norm_gain_in_silence = 0.0f;
    processed = 0;
    while (processed < n_total2) {
        int chunk = BUBBLES_BLOCK_SIZE;
        if (processed + chunk > n_total2) chunk = n_total2 - processed;
        bubble_engine_process(&engine, &sig_buf[processed], &out_l[processed], &out_r[processed], chunk);
#if defined(M4D_CANDIDATE_BUILD)
        if (processed >= n_sig) {
            float g = SoundBubbles_GetWetNormalizationGain(&engine);
            if (g > max_norm_gain_in_silence) max_norm_gain_in_silence = g;
        }
#else
        max_norm_gain_in_silence = 1.0f;
#endif
        processed += chunk;
    }

    printf("STARTUP_SILENCE_PEAK: %.6f\n", max_startup_peak);
    printf("POST_SILENCE_MAX_NORM_GAIN: %.4f\n", max_norm_gain_in_silence);

    free(silent_buf);
    free(sig_buf);
    free(out_l);
    free(out_r);
}

// -------------------------------------------------------------
// SECTION 18: Parameter Automation Discontinuity & Slew
// -------------------------------------------------------------
static void run_automation_check(void) {
    const float sr = 44100.0f;
    const int n = (int)(sr * 2.0f);
    float* in_buf = (float*)malloc((size_t)n * sizeof(float));
    float* out_l = (float*)malloc((size_t)n * sizeof(float));
    float* out_r = (float*)malloc((size_t)n * sizeof(float));
    generate_source(SRC_SUSTAINED_SINE, in_buf, n, sr);

    BubbleEngine_t engine;
    BubbleEngineConfig_t cfg;
    bubble_engine_default_config(&cfg);
    cfg.sample_rate = sr;
    bubble_engine_init(&engine, g_delay, &cfg);
    bubble_engine_set_parameter(&engine, BUBBLE_PARAM_DEVELOPER_MODE, 0.0f);

    float max_sample_disc = 0.0f;
    float prev_l = 0.0f;
    float prev_r = 0.0f;
#if defined(M4D_CANDIDATE_BUILD)
    float prev_norm = 1.0f;
#endif
    float max_norm_slew = 0.0f;

    int processed = 0;
    while (processed < n) {
        float ramp = (float)processed / (float)n;
        bubble_engine_set_parameter(&engine, BUBBLE_PARAM_DENSITY, ramp);
        bubble_engine_set_parameter(&engine, BUBBLE_PARAM_BLOOM, 1.0f - ramp);
        bubble_engine_set_parameter(&engine, BUBBLE_PARAM_WARMTH, ramp);
        bubble_engine_set_parameter(&engine, BUBBLE_PARAM_MIX, 0.5f + 0.5f * sinf(ramp * 6.28f));

        int chunk = BUBBLES_BLOCK_SIZE;
        if (processed + chunk > n) chunk = n - processed;
        bubble_engine_process(&engine, &in_buf[processed], &out_l[processed], &out_r[processed], chunk);

        for (int i = 0; i < chunk; i++) {
            if (processed + i > 0) {
                float dl = fabsf(out_l[processed + i] - prev_l);
                float dr = fabsf(out_r[processed + i] - prev_r);
                if (dl > max_sample_disc) max_sample_disc = dl;
                if (dr > max_sample_disc) max_sample_disc = dr;
            }
            prev_l = out_l[processed + i];
            prev_r = out_r[processed + i];
        }

#if defined(M4D_CANDIDATE_BUILD)
        float curr_norm = SoundBubbles_GetWetNormalizationGain(&engine);
        float dnorm = fabsf(curr_norm - prev_norm);
        if (dnorm > max_norm_slew) max_norm_slew = dnorm;
        prev_norm = curr_norm;
#endif

        processed += chunk;
    }

    printf("MAX_SAMPLE_DISCONTINUITY: %.4f\n", max_sample_disc);
    printf("MAX_NORM_SLEW_PER_BLOCK: %.6f\n", max_norm_slew);

    free(in_buf);
    free(out_l);
    free(out_r);
}

// -------------------------------------------------------------
// SECTION 19: CPU Benchmark
// -------------------------------------------------------------
static void run_cpu_bench(void) {
    const float srs[] = { 44100.0f, 48000.0f, 96000.0f };
    const BubbleQualityProfile profs[] = {
        BUBBLE_QUALITY_PROFILE_WEB_STANDARD, // 24 voices
        BUBBLE_QUALITY_PROFILE_WEB_STANDARD, // 24 voices
        BUBBLE_QUALITY_PROFILE_WEB_ULTRA     // 32 voices
    };
    const int voices[] = { 24, 24, 32 };

    printf("CONFIG,SR_HZ,VOICES,PROCESS_TIME_MS,REALTIME_FACTOR\n");
    for (int i = 0; i < 3; i++) {
        float sr = srs[i];
        float duration_s = 5.0f;
        EngineRunSummary_t r = run_engine(sr, SRC_SUSTAINED_SINE, duration_s,
                                          0.85f, 0.75f, 1.0f, 0.50f,
                                          profs[i], false, false);
        double time_ms = r.process_time_sec * 1000.0;
        double rtf = r.process_time_sec / duration_s;
        printf("Benchmark_%d,%.0f,%d,%.2f,%.4f\n",
               i + 1, sr, voices[i], time_ms, rtf);
    }
}

// Main CLI dispatch
int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <mode>\nModes: --limiter-matrix, --dry-pumping, --normalization-stats, --bloom-ab, --tail, --stereo, --mix-zero, --silence, --automation, --cpu\n", argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "--limiter-matrix") == 0) {
        run_limiter_matrix();
    } else if (strcmp(argv[1], "--dry-pumping") == 0) {
        run_direct_dry_pumping();
    } else if (strcmp(argv[1], "--normalization-stats") == 0) {
        run_normalization_stats();
    } else if (strcmp(argv[1], "--bloom-ab") == 0) {
        run_bloom_ab();
    } else if (strcmp(argv[1], "--tail") == 0) {
        run_tail_windows();
    } else if (strcmp(argv[1], "--stereo") == 0) {
        run_stereo_check();
    } else if (strcmp(argv[1], "--mix-zero") == 0) {
        run_mix_zero_check();
    } else if (strcmp(argv[1], "--silence") == 0) {
        run_silence_check();
    } else if (strcmp(argv[1], "--automation") == 0) {
        run_automation_check();
    } else if (strcmp(argv[1], "--cpu") == 0) {
        run_cpu_bench();
    } else {
        fprintf(stderr, "Unknown mode: %s\n", argv[1]);
        return 1;
    }
    return 0;
}
