// M5A Multi-Scale Granular Memory & Anti-Loop Decorrelation Probe
// Comprehensive qualification harness for M5A vs frozen baseline M4D.1 (ad691aecdfa4b92124943f53c43a31b684d5ced8)

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

static float calc_centroid(const float* x, int n, float sr) {
    double num = 0.0, den = 0.0;
    for (int i = 1; i < n; i++) {
        double diff = (double)x[i] - (double)x[i - 1];
        double energy = diff * diff;
        double freq = ((double)i / (double)n) * ((double)sr * 0.5);
        num += freq * energy;
        den += energy;
    }
    return (den > 1e-12) ? (float)(num / den) : 0.0f;
}

// Normalized cross-correlation between two windows of equal length
static float calc_cross_correlation(const float* x, const float* y, int n) {
    double sum_x2 = 0.0, sum_y2 = 0.0, sum_xy = 0.0;
    for (int i = 0; i < n; i++) {
        sum_x2 += (double)x[i] * (double)x[i];
        sum_y2 += (double)y[i] * (double)y[i];
        sum_xy += (double)x[i] * (double)y[i];
    }
    double denom = sqrt(sum_x2 * sum_y2);
    return (denom > 1e-12) ? (float)(sum_xy / denom) : 0.0f;
}

// Autocorrelation at specific lag samples
static float calc_autocorrelation_at_lag(const float* buffer, int total_samples, int lag_samples, int window_samples) {
    if (lag_samples + window_samples > total_samples) return 0.0f;
    const float* x = buffer;
    const float* y = buffer + lag_samples;
    return calc_cross_correlation(x, y, window_samples);
}

// ---------------------------------------------------------------------------
// 1. Multi-Tier Distribution across Phrase States & Classes
// ---------------------------------------------------------------------------
static int run_distribution(void) {
    BubbleEngineConfig_t config;
    bubble_engine_default_config(&config);
    config.sample_rate = 44100.0f;
    config.memory_mix = 0.50f;
    config.sustain_diffusion_enable = 1;
    config.sustain_diffusion_amount = 0.50f;

    BubbleEngine_t engine;
    SoundBubbles_Init(&engine, g_delay, &config);

    const int sr = 44100;
    const int block_size = 64;
    float in[64], out_l[64], out_r[64];

    // Phases:
    // 0.0 - 0.2s: Transient burst / attack
    // 0.2 - 1.0s: Sustain body
    // 1.0 - 2.5s: Decay tail
    // 2.5 - 5.0s: Silence tail with Auto-Hold
    const int total_samples = 5 * sr;

    int r_attack = 0, m_attack = 0, d_attack = 0;
    int r_sustain = 0, m_sustain = 0, d_sustain = 0;
    int r_decay = 0, m_decay = 0, d_decay = 0;
    int r_silence = 0, m_silence = 0, d_silence = 0;

#ifdef M5A_CANDIDATE_BUILD
    int prev_recent = 0, prev_mid = 0, prev_deep = 0;
#endif

    for (int i = 0; i < total_samples; i += block_size) {
        float t = (float)i / (float)sr;
        for (int k = 0; k < block_size; k++) {
            float cur_t = t + (float)k / (float)sr;
            if (cur_t < 0.2f) {
                // Sharp attack burst
                in[k] = 0.85f * sinf(2.0f * M_PI_F * 440.0f * cur_t) * (1.0f - cur_t * 2.0f);
            } else if (cur_t < 1.0f) {
                // Sustain body tone
                in[k] = 0.65f * (0.6f * sinf(2.0f * M_PI_F * 440.0f * cur_t) + 0.3f * sinf(2.0f * M_PI_F * 880.0f * cur_t));
            } else {
                in[k] = 0.0f;
            }
        }
        SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, block_size);

#ifdef M5A_CANDIDATE_BUILD
        int cur_recent, cur_mid, cur_deep;
        SoundBubbles_GetMemoryTierCounts(&engine, &cur_recent, &cur_mid, &cur_deep);
        int d_r = cur_recent - prev_recent;
        int d_m = cur_mid - prev_mid;
        int d_d = cur_deep - prev_deep;
        prev_recent = cur_recent;
        prev_mid = cur_mid;
        prev_deep = cur_deep;

        if (t < 0.2f) {
            r_attack += d_r; m_attack += d_m; d_attack += d_d;
        } else if (t < 1.0f) {
            r_sustain += d_r; m_sustain += d_m; d_sustain += d_d;
        } else if (t < 2.5f) {
            r_decay += d_r; m_decay += d_m; d_decay += d_d;
        } else {
            r_silence += d_r; m_silence += d_m; d_silence += d_d;
        }
#endif
    }

    int tot_att = r_attack + m_attack + d_attack;
    int tot_sus = r_sustain + m_sustain + d_sustain;
    int tot_dec = r_decay + m_decay + d_decay;
    int tot_sil = r_silence + m_silence + d_silence;

    printf("DISTRIBUTION_CSV\n");
    printf("State,Recent,Mid,Deep,RecentPct,MidPct,DeepPct\n");
    printf("ATTACK,%d,%d,%d,%.1f,%.1f,%.1f\n",
           r_attack, m_attack, d_attack,
           tot_att ? 100.0f * (float)r_attack / (float)tot_att : 0.0f,
           tot_att ? 100.0f * (float)m_attack / (float)tot_att : 0.0f,
           tot_att ? 100.0f * (float)d_attack / (float)tot_att : 0.0f);
    printf("SUSTAIN,%d,%d,%d,%.1f,%.1f,%.1f\n",
           r_sustain, m_sustain, d_sustain,
           tot_sus ? 100.0f * (float)r_sustain / (float)tot_sus : 0.0f,
           tot_sus ? 100.0f * (float)m_sustain / (float)tot_sus : 0.0f,
           tot_sus ? 100.0f * (float)d_sustain / (float)tot_sus : 0.0f);
    printf("SPARSE_DECAY,%d,%d,%d,%.1f,%.1f,%.1f\n",
           r_decay, m_decay, d_decay,
           tot_dec ? 100.0f * (float)r_decay / (float)tot_dec : 0.0f,
           tot_dec ? 100.0f * (float)m_decay / (float)tot_dec : 0.0f,
           tot_dec ? 100.0f * (float)d_decay / (float)tot_dec : 0.0f);
    printf("SILENCE_HOLD,%d,%d,%d,%.1f,%.1f,%.1f\n",
           r_silence, m_silence, d_silence,
           tot_sil ? 100.0f * (float)r_silence / (float)tot_sil : 0.0f,
           tot_sil ? 100.0f * (float)m_silence / (float)tot_sil : 0.0f,
           tot_sil ? 100.0f * (float)d_silence / (float)tot_sil : 0.0f);

#ifdef M5A_CANDIDATE_BUILD
    float mean_ms, p50_ms, p95_ms, anchor_frac;
    SoundBubbles_GetReadAgeTelemetry(&engine, &mean_ms, &p50_ms, &p95_ms, &anchor_frac);
    printf("TELEMETRY: mean_ms=%.1f p50_ms=%.1f p95_ms=%.1f anchor_fraction=%.3f\n",
           mean_ms, p50_ms, p95_ms, anchor_frac);
#endif
    return 0;
}

// ---------------------------------------------------------------------------
// 2. MEMORY Sweep & Loudness Invariance
// ---------------------------------------------------------------------------
static int run_memory_sweep(void) {
    const float memory_mixes[] = {0.0f, 0.25f, 0.50f, 0.75f, 1.0f};
    printf("MEMORY_SWEEP_CSV\n");
    printf("MemoryMix,RecentPct,MidPct,DeepPct,TailRMS_dB,Peak_dB\n");

    for (int m = 0; m < 5; m++) {
        BubbleEngineConfig_t config;
        bubble_engine_default_config(&config);
        config.sample_rate = 44100.0f;
        config.memory_mix = memory_mixes[m];
        config.memory_pull = memory_mixes[m] * 0.5f;
        config.sustain_diffusion_enable = 1;
        config.sustain_diffusion_amount = 0.50f;

        BubbleEngine_t engine;
        SoundBubbles_Init(&engine, g_delay, &config);
        SoundBubbles_SetFeedbackEnabled(&engine, true);

        const int sr = 44100;
        const int total_samples = (int)(6.0f * (float)sr);
        const int tone_samples = (int)(0.5f * (float)sr);
        const int block_size = 64;
        float in[64], out_l[64], out_r[64];

        float* out_accum = (float*)malloc((total_samples + block_size) * sizeof(float));
        int out_idx = 0;

        for (int i = 0; i < total_samples; i += block_size) {
            for (int k = 0; k < block_size; k++) {
                int idx = i + k;
                if (idx < tone_samples) {
                    float t = (float)idx / (float)sr;
                    in[k] = 0.85f * (0.6f * sinf(2.0f * M_PI_F * 440.0f * t) + 0.3f * sinf(2.0f * M_PI_F * 880.0f * t));
                } else {
                    in[k] = 0.0f;
                }
            }
            SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, block_size);
            for (int k = 0; k < block_size; k++) {
                if (out_idx < total_samples) {
                    out_accum[out_idx++] = 0.5f * (out_l[k] + out_r[k]);
                }
            }
        }

        // Measure tail RMS in [2.0s, 5.0s]
        int tail_start = 2 * sr;
        int tail_count = 3 * sr;
        float tail_rms = calc_rms(out_accum + tail_start, tail_count);
        float peak = calc_peak(out_accum, total_samples);
        free(out_accum);

        int rec = 0, mid = 0, deep = 0;
#ifdef M5A_CANDIDATE_BUILD
        SoundBubbles_GetMemoryTierCounts(&engine, &rec, &mid, &deep);
#endif
        int tot = rec + mid + deep;
        printf("%.2f,%.1f,%.1f,%.1f,%.2f,%.2f\n",
               memory_mixes[m],
               tot ? 100.0f * (float)rec / (float)tot : 0.0f,
               tot ? 100.0f * (float)mid / (float)tot : 0.0f,
               tot ? 100.0f * (float)deep / (float)tot : 0.0f,
               to_db(tail_rms),
               to_db(peak));
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 3. Anti-Loop Periodicity & Decorrelation vs Baseline
// ---------------------------------------------------------------------------
static int run_periodicity(void) {
    BubbleEngineConfig_t config;
    bubble_engine_default_config(&config);
    config.sample_rate = 44100.0f;
    config.memory_mix = 0.70f;
    config.memory_pull = 0.40f;
    config.sustain_diffusion_enable = 1;
    config.sustain_diffusion_amount = 0.60f;

    BubbleEngine_t engine;
    SoundBubbles_Init(&engine, g_delay, &config);
    SoundBubbles_SetFeedbackEnabled(&engine, true);

    const int sr = 44100;
    const int total_samples = 14 * sr;
    const int tone_samples = (int)(0.5f * (float)sr);
    const int block_size = 64;
    float in[64], out_l[64], out_r[64];

    float* full_out = (float*)malloc((total_samples + block_size) * sizeof(float));
    int out_idx = 0;

    for (int i = 0; i < total_samples; i += block_size) {
        for (int k = 0; k < block_size; k++) {
            int idx = i + k;
            if (idx < tone_samples) {
                float t = (float)idx / (float)sr;
                in[k] = 0.85f * (0.6f * sinf(2.0f * M_PI_F * 440.0f * t) + 0.3f * sinf(2.0f * M_PI_F * 880.0f * t));
            } else {
                in[k] = 0.0f;
            }
        }
        SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, block_size);
        for (int k = 0; k < block_size; k++) {
            if (out_idx < total_samples) {
                full_out[out_idx++] = 0.5f * (out_l[k] + out_r[k]);
            }
        }
    }

    // Autocorrelation in the tail starting at t = 2.0s
    int tail_start = 2 * sr;
    const float* tail = full_out + tail_start;
    int tail_samples = total_samples - tail_start;

    // Window = 1.0s (44100 samples)
    int win_samples = 1 * sr;
    float ac_1_0s = calc_autocorrelation_at_lag(tail, tail_samples, (int)(1.0f * (float)sr), win_samples);
    float ac_1_5s = calc_autocorrelation_at_lag(tail, tail_samples, (int)(1.5f * (float)sr), win_samples);
    float ac_2_0s = calc_autocorrelation_at_lag(tail, tail_samples, (int)(2.0f * (float)sr), win_samples);
    float ac_3_0s = calc_autocorrelation_at_lag(tail, tail_samples, (int)(3.0f * (float)sr), win_samples);
    float ac_4_0s = calc_autocorrelation_at_lag(tail, tail_samples, (int)(4.0f * (float)sr), win_samples);
    float ac_5_0s = calc_autocorrelation_at_lag(tail, tail_samples, (int)(5.0f * (float)sr), win_samples);

    // Sliding self-similarity in late tail:
    // W_A: 4-6s (2s window)
    // W_B: 6-8s
    // W_C: 8-10s
    // W_D: 10-12s
    int late_win = 2 * sr;
    float sim_4_6_vs_6_8 = calc_cross_correlation(full_out + 4 * sr, full_out + 6 * sr, late_win);
    float sim_6_8_vs_8_10 = calc_cross_correlation(full_out + 6 * sr, full_out + 8 * sr, late_win);
    float sim_8_10_vs_10_12 = calc_cross_correlation(full_out + 8 * sr, full_out + 10 * sr, late_win);

    printf("PERIODICITY_CSV\n");
    printf("Lag_1_0s,Lag_1_5s,Lag_2_0s,Lag_3_0s,Lag_4_0s,Lag_5_0s,Sim_4_6_8,Sim_6_8_10,Sim_8_10_12\n");
    printf("%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f\n",
           ac_1_0s, ac_1_5s, ac_2_0s, ac_3_0s, ac_4_0s, ac_5_0s,
           sim_4_6_vs_6_8, sim_6_8_vs_8_10, sim_8_10_vs_10_12);

    free(full_out);
    return 0;
}

// ---------------------------------------------------------------------------
// 4. Attack Integrity Check (0-100ms and 100-300ms)
// ---------------------------------------------------------------------------
static int run_attack_integrity(void) {
    BubbleEngineConfig_t config;
    bubble_engine_default_config(&config);
    config.sample_rate = 44100.0f;
    config.memory_mix = 0.50f;
    config.sustain_diffusion_enable = 1;
    config.sustain_diffusion_amount = 0.50f;

    BubbleEngine_t engine;
    SoundBubbles_Init(&engine, g_delay, &config);

    const int sr = 44100;
    const int total_samples = (int)(0.5f * (float)sr);
    const int block_size = 64;
    float in[64], out_l[64], out_r[64];

    float* out = (float*)malloc((total_samples + block_size) * sizeof(float));
    int out_idx = 0;

    for (int i = 0; i < total_samples; i += block_size) {
        for (int k = 0; k < block_size; k++) {
            float t = (float)(i + k) / (float)sr;
            in[k] = 0.95f * expf(-t * 25.0f) * sinf(2.0f * M_PI_F * 1000.0f * t);
        }
        SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, block_size);
        for (int k = 0; k < block_size; k++) {
            if (out_idx < total_samples) {
                out[out_idx++] = 0.5f * (out_l[k] + out_r[k]);
            }
        }
    }

    int n_100ms = (int)(0.100f * (float)sr);
    int n_300ms = (int)(0.200f * (float)sr);

    float rms_0_100 = calc_rms(out, n_100ms);
    float peak_0_100 = calc_peak(out, n_100ms);
    float cent_0_100 = calc_centroid(out, n_100ms, (float)sr);

    float rms_100_300 = calc_rms(out + n_100ms, n_300ms);
    float peak_100_300 = calc_peak(out + n_100ms, n_300ms);
    float cent_100_300 = calc_centroid(out + n_100ms, n_300ms, (float)sr);

    printf("ATTACK_INTEGRITY_CSV\n");
    printf("Window,RMS_dB,Peak_dB,Centroid_Hz\n");
    printf("0-100ms,%.2f,%.2f,%.1f\n", to_db(rms_0_100), to_db(peak_0_100), cent_0_100);
    printf("100-300ms,%.2f,%.2f,%.1f\n", to_db(rms_100_300), to_db(peak_100_300), cent_100_300);

    free(out);
    return 0;
}

// ---------------------------------------------------------------------------
// 5. Cross-Phrase Contamination Check
// ---------------------------------------------------------------------------
static int run_cross_phrase(void) {
    BubbleEngineConfig_t config;
    bubble_engine_default_config(&config);
    config.sample_rate = 44100.0f;
    config.memory_mix = 0.60f;
    config.memory_pull = 0.30f;
    config.sustain_diffusion_enable = 1;
    config.sustain_diffusion_amount = 0.50f;

    BubbleEngine_t engine;
    SoundBubbles_Init(&engine, g_delay, &config);

    const int sr = 44100;
    const int block_size = 64;
    float in[64], out_l[64], out_r[64];

    // Phrase A: 0.0 - 0.4s (440 Hz)
    // Silence:  0.4 - 2.0s
    // Phrase B: 2.0 - 2.3s (880 Hz)
    const int total_samples = (int)(2.5f * (float)sr);

#ifdef M5A_CANDIDATE_BUILD
    int p_b_start = 2 * sr;
    int p_b_300ms = p_b_start + (int)(0.300f * (float)sr);
    int r_b = 0, m_b = 0, d_b = 0;
    int prev_rec = 0, prev_mid = 0, prev_deep = 0;
#endif

    for (int i = 0; i < total_samples; i += block_size) {
        float t = (float)i / (float)sr;
        for (int k = 0; k < block_size; k++) {
            float cur_t = t + (float)k / (float)sr;
            if (cur_t < 0.4f) {
                in[k] = 0.85f * sinf(2.0f * M_PI_F * 440.0f * cur_t);
            } else if (cur_t >= 2.0f && cur_t < 2.3f) {
                in[k] = 0.85f * sinf(2.0f * M_PI_F * 880.0f * cur_t);
            } else {
                in[k] = 0.0f;
            }
        }
        SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, block_size);

#ifdef M5A_CANDIDATE_BUILD
        int cur_rec, cur_mid, cur_deep;
        SoundBubbles_GetMemoryTierCounts(&engine, &cur_rec, &cur_mid, &cur_deep);
        int d_r = cur_rec - prev_rec;
        int d_m = cur_mid - prev_mid;
        int d_d = cur_deep - prev_deep;
        prev_rec = cur_rec;
        prev_mid = cur_mid;
        prev_deep = cur_deep;

        if (i >= p_b_start && i < p_b_300ms) {
            r_b += d_r;
            m_b += d_m;
            d_b += d_d;
        }
#endif
    }

#ifdef M5A_CANDIDATE_BUILD
    int tot_b = r_b + m_b + d_b;
    float deep_pct_b = tot_b ? 100.0f * (float)d_b / (float)tot_b : 0.0f;
    printf("CROSS_PHRASE: Spawns=%d Recent=%d Mid=%d Deep=%d DeepPct=%.2f%%\n",
           tot_b, r_b, m_b, d_b, deep_pct_b);
#else
    printf("CROSS_PHRASE: Baseline M4D.1 phrase B processed\n");
#endif
    return 0;
}

// ---------------------------------------------------------------------------
// 6. Pitch Stress Resilience (Deep + Unison, +12, +19, Reverse with Hermite)
// ---------------------------------------------------------------------------
static int run_pitch_stress(void) {
    BubbleEngineConfig_t config;
    bubble_engine_default_config(&config);
    config.sample_rate = 44100.0f;
    config.memory_mix = 1.0f;
    config.memory_pull = 1.0f;
    config.quality_profile = BUBBLE_QUALITY_PROFILE_WEB_ULTRA; // Hermite 4-point cubic
    config.sustain_diffusion_enable = 1;
    config.sustain_diffusion_amount = 0.85f;

    BubbleEngine_t engine;
    SoundBubbles_Init(&engine, g_delay, &config);
    SoundBubbles_SetFeedbackEnabled(&engine, true);

    const int sr = 44100;
    const int total_samples = 4 * sr;
    const int block_size = 64;
    float in[64], out_l[64], out_r[64];

    int nan_inf_count = 0;
    float max_peak = 0.0f;

    for (int i = 0; i < total_samples; i += block_size) {
        for (int k = 0; k < block_size; k++) {
            float t = (float)(i + k) / (float)sr;
            in[k] = (t < 0.5f) ? 0.95f * (sinf(2.0f * M_PI_F * 440.0f * t) + sinf(2.0f * M_PI_F * 1320.0f * t)) : 0.0f;
        }
        SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, block_size);
        for (int k = 0; k < block_size; k++) {
            if (!isfinite(out_l[k]) || !isfinite(out_r[k])) nan_inf_count++;
            float a_l = fabsf(out_l[k]);
            float a_r = fabsf(out_r[k]);
            if (a_l > max_peak) max_peak = a_l;
            if (a_r > max_peak) max_peak = a_r;
        }
    }

    uint32_t softclip_cnt = 0, clamp_cnt = 0;
    SoundBubbles_GetRingSaturationCounts(&engine, &softclip_cnt, &clamp_cnt);

    printf("PITCH_STRESS: NaN_Inf=%d MaxPeak=%.3f ClampCount=%u SoftclipCount=%u\n",
           nan_inf_count, max_peak, clamp_cnt, softclip_cnt);
    return (nan_inf_count == 0 && clamp_cnt == 0) ? 0 : 1;
}

// ---------------------------------------------------------------------------
// 7. Block Invariance (32, 64, 127, 256, 512, 2048)
// ---------------------------------------------------------------------------
static int run_block_invariance(void) {
    const int block_sizes[] = {32, 64, 127, 256, 512, 2048};
    printf("BLOCK_INVARIANCE_CSV\n");
    printf("BlockSize,RMS_dB,Peak_dB,Centroid_Hz\n");

    for (int b = 0; b < 6; b++) {
        int bs = block_sizes[b];
        BubbleEngineConfig_t config;
        bubble_engine_default_config(&config);
        config.sample_rate = 44100.0f;
        config.memory_mix = 0.50f;
        config.sustain_diffusion_enable = 1;
        config.sustain_diffusion_amount = 0.50f;

        BubbleEngine_t engine;
        SoundBubbles_Init(&engine, g_delay, &config);

        const int sr = 44100;
        const int total_samples = 2 * sr;
        float* in = (float*)malloc(bs * sizeof(float));
        float* out_l = (float*)malloc(bs * sizeof(float));
        float* out_r = (float*)malloc(bs * sizeof(float));
        float* accum = (float*)malloc(total_samples * sizeof(float));
        int accum_idx = 0;

        for (int i = 0; i < total_samples; i += bs) {
            int cur_bs = (i + bs <= total_samples) ? bs : (total_samples - i);
            for (int k = 0; k < cur_bs; k++) {
                float t = (float)(i + k) / (float)sr;
                in[k] = (t < 0.3f) ? 0.85f * sinf(2.0f * M_PI_F * 440.0f * t) : 0.0f;
            }
            SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, cur_bs);
            for (int k = 0; k < cur_bs; k++) {
                accum[accum_idx++] = 0.5f * (out_l[k] + out_r[k]);
            }
        }

        float rms = calc_rms(accum, total_samples);
        float peak = calc_peak(accum, total_samples);
        float cent = calc_centroid(accum, total_samples, (float)sr);

        printf("%d,%.2f,%.2f,%.1f\n", bs, to_db(rms), to_db(peak), cent);

        free(in);
        free(out_l);
        free(out_r);
        free(accum);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 8. CPU Benchmark
// ---------------------------------------------------------------------------
static int run_cpu_benchmark(void) {
    const float sample_rates[] = {44100.0f, 48000.0f, 96000.0f};
    const int voice_limits[] = {8, 16, 24, 32};
    printf("CPU_BENCHMARK_CSV\n");
    printf("SampleRate,Voices,TimeMs,SpeedX\n");

    for (int sr_i = 0; sr_i < 3; sr_i++) {
        float sr = sample_rates[sr_i];
        for (int v_i = 0; v_i < 4; v_i++) {
            int voices = voice_limits[v_i];

            BubbleEngineConfig_t config;
            bubble_engine_default_config(&config);
            config.sample_rate = sr;
            config.active_voice_limit = voices;
            config.memory_mix = 0.70f;
            config.sustain_diffusion_enable = 1;
            config.sustain_diffusion_amount = 0.70f;

            BubbleEngine_t engine;
            SoundBubbles_Init(&engine, g_delay, &config);
            SoundBubbles_SetFeedbackEnabled(&engine, true);

            const int test_samples = (int)(2.0f * sr);
            const int block_size = 64;
            float in[64], out_l[64], out_r[64];

            clock_t start = clock();
            for (int i = 0; i < test_samples; i += block_size) {
                for (int k = 0; k < block_size; k++) {
                    float t = (float)(i + k) / sr;
                    in[k] = (t < 0.5f) ? 0.85f * sinf(2.0f * M_PI_F * 440.0f * t) : 0.0f;
                }
                SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, block_size);
            }
            clock_t end = clock();
            double time_ms = (double)(end - start) * 1000.0 / (double)CLOCKS_PER_SEC;
            double audio_time_ms = 2000.0;
            double speed_x = (time_ms > 0.0) ? (audio_time_ms / time_ms) : 999.0;

            printf("%.0f,%d,%.2f,%.1f\n", sr, voices, time_ms, speed_x);
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Main Dispatcher
// ---------------------------------------------------------------------------
int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <distribution|memory_sweep|periodicity|attack_integrity|cross_phrase|pitch_stress|block_invariance|cpu_benchmark>\n", argv[0]);
        return 1;
    }

    const char* mode = argv[1];
    if (strcmp(mode, "distribution") == 0) return run_distribution();
    if (strcmp(mode, "memory_sweep") == 0) return run_memory_sweep();
    if (strcmp(mode, "periodicity") == 0) return run_periodicity();
    if (strcmp(mode, "attack_integrity") == 0) return run_attack_integrity();
    if (strcmp(mode, "cross_phrase") == 0) return run_cross_phrase();
    if (strcmp(mode, "pitch_stress") == 0) return run_pitch_stress();
    if (strcmp(mode, "block_invariance") == 0) return run_block_invariance();
    if (strcmp(mode, "cpu_benchmark") == 0) return run_cpu_benchmark();

    fprintf(stderr, "Unknown mode: %s\n", mode);
    return 1;
}
