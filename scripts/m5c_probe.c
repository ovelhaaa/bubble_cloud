// M5C Spectral Memory Evolution Probe
// Comprehensive qualification harness for M5C vs frozen baseline M5B.1 (dc9520f4ca27e4e82986ccc1013daafc802d5af3)

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

#ifndef M5C_CANDIDATE_BUILD
typedef struct {
    float mean_spectral_age;
    float p10_spectral_age;
    float p50_spectral_age;
    float p95_spectral_age;
    float recent_spectral_age_mean;
    float mid_spectral_age_mean;
    float deep_spectral_age_mean;
    float mean_cutoff_hz;
    float min_cutoff_hz;
    float p50_cutoff_hz;
    float p95_cutoff_hz;
    float sustain_applied_cutoff_hz;
} SoundBubblesSpectralMemoryMetrics_t;

static inline void SoundBubbles_ResetSpectralMemoryMetrics(BubbleEngine_t* e) { (void)e; }
static inline void SoundBubbles_GetSpectralMemoryMetrics(const BubbleEngine_t* e, SoundBubblesSpectralMemoryMetrics_t* m) {
    (void)e;
    if (m) memset(m, 0, sizeof(*m));
}
static inline float SoundBubbles_ComputeGrainSpectralAge(const BubbleEngine_t* e, BubbleClass_t c, BubbleMemoryTier t, float r) {
    (void)e; (void)c; (void)t; (void)r;
    return 0.0f;
}
static inline float SoundBubbles_ComputeGrainCutoffHz(const BubbleEngine_t* e, float a) {
    (void)e; (void)a;
    return 5000.0f;
}
#endif

#ifdef M5C_CANDIDATE_BUILD
static inline float GetEngineSpectralAge(const BubbleEngine_t* e) { return e->spectral_memory_age_smoothed; }
static inline float GetEngineSpectralTarget(const BubbleEngine_t* e) { return e->spectral_memory_age_target; }
#else
static inline float GetEngineSpectralAge(const BubbleEngine_t* e) { (void)e; return 0.0f; }
static inline float GetEngineSpectralTarget(const BubbleEngine_t* e) { (void)e; return 0.0f; }
#endif

#define MAX_RING_SAMPLES 384000
#ifndef M_PI_F
#define M_PI_F 3.14159265358979323846f
#endif

static BubbleRingSample_t g_delay[MAX_RING_SAMPLES];

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

// In-place real FFT helper for spectral analysis
static void compute_real_fft_mag(const float* x, int n, int nfft, float* out_mag) {
    if (!x || n <= 0 || nfft <= 0 || !out_mag) return;
    float* re = (float*)calloc((size_t)nfft, sizeof(float));
    float* im = (float*)calloc((size_t)nfft, sizeof(float));
    if (!re || !im) {
        if (re) free(re);
        if (im) free(im);
        return;
    }

    int copy_len = (n < nfft) ? n : nfft;
    for (int i = 0; i < copy_len; i++) {
        float w = 0.5f * (1.0f - cosf(2.0f * (float)M_PI_F * (float)i / (float)(copy_len - 1)));
        re[i] = x[i] * w;
    }

    // Cooley-Tukey Radix-2 FFT
    int j = 0;
    for (int i = 0; i < nfft - 1; i++) {
        if (i < j) {
            float tr = re[i]; re[i] = re[j]; re[j] = tr;
            float ti = im[i]; im[i] = im[j]; im[j] = ti;
        }
        int k = nfft >> 1;
        while (k <= j) {
            j -= k;
            k >>= 1;
        }
        j += k;
    }

    for (int len = 2; len <= nfft; len <<= 1) {
        float angle = -2.0f * (float)M_PI_F / (float)len;
        float wlen_r = cosf(angle);
        float wlen_i = sinf(angle);
        int half = len >> 1;
        for (int i = 0; i < nfft; i += len) {
            float wr = 1.0f;
            float wi = 0.0f;
            for (int k = 0; k < half; k++) {
                float u_r = re[i + k];
                float u_i = im[i + k];
                float v_r = re[i + k + half] * wr - im[i + k + half] * wi;
                float v_i = re[i + k + half] * wi + im[i + k + half] * wr;
                re[i + k] = u_r + v_r;
                im[i + k] = u_i + v_i;
                re[i + k + half] = u_r - v_r;
                im[i + k + half] = u_i - v_i;
                float next_wr = wr * wlen_r - wi * wlen_i;
                float next_wi = wr * wlen_i + wi * wlen_r;
                wr = next_wr;
                wi = next_wi;
            }
        }
    }

    int half_fft = nfft >> 1;
    for (int k = 0; k <= half_fft; k++) {
        out_mag[k] = (float)sqrt((double)re[k] * (double)re[k] + (double)im[k] * (double)im[k]);
    }

    free(re);
    free(im);
}

// Real FFT spectral centroid
static float calc_centroid(const float* x, int n, float sr) {
    if (n <= 1 || sr <= 0.0f) return 0.0f;
    int nfft = 4096;
    if (n < 4096) {
        nfft = 128;
        while (nfft < n) nfft <<= 1;
    }

    float* mag = (float*)malloc(((nfft >> 1) + 1) * sizeof(float));
    if (!mag) return 0.0f;
    compute_real_fft_mag(x, n, nfft, mag);

    double sum_weighted = 0.0;
    double sum_mag = 0.0;
    int half_fft = nfft >> 1;
    float bin_hz = sr / (float)nfft;

    for (int k = 1; k <= half_fft; k++) {
        double m = (double)mag[k];
        double freq = (double)k * (double)bin_hz;
        sum_weighted += freq * m;
        sum_mag += m;
    }
    free(mag);
    return (sum_mag > 1e-12) ? (float)(sum_weighted / sum_mag) : 0.0f;
}

// Standard test sources
typedef enum {
    SOURCE_HARMONIC_PLUCK = 0,
    SOURCE_HARP_TRANSIENT,
    SOURCE_SUSTAINED_CHORD,
    SOURCE_NOISE_RICH,
    SOURCE_PERCUSSIVE_PULSE,
    SOURCE_COUNT
} AudioSource_t;

static const char* g_source_names[SOURCE_COUNT] = {
    "harmonic_pluck",
    "harp_transient",
    "sustained_chord",
    "noise_rich",
    "percussive_pulse"
};

static float GenerateSourceSample(AudioSource_t src, float t) {
    switch (src) {
        case SOURCE_HARMONIC_PLUCK:
            return 0.95f * expf(-t * 25.0f) * (0.6f * sinf(2.0f * M_PI_F * 1000.0f * t) +
                                                0.3f * sinf(2.0f * M_PI_F * 2000.0f * t) +
                                                0.1f * sinf(2.0f * M_PI_F * 3000.0f * t));
        case SOURCE_HARP_TRANSIENT:
            return 0.95f * expf(-t * 40.0f) * (0.7f * sinf(2.0f * M_PI_F * 1500.0f * t) +
                                                0.3f * sinf(2.0f * M_PI_F * 3000.0f * t));
        case SOURCE_PERCUSSIVE_PULSE:
            return 0.95f * expf(-t * 120.0f) * sinf(2.0f * M_PI_F * 800.0f * t);
        case SOURCE_SUSTAINED_CHORD:
            if (t < 0.50f) {
                float env = (1.0f - expf(-t * 30.0f)) * (t < 0.40f ? 1.0f : expf(-(t - 0.40f) * 40.0f));
                return 0.80f * env * (0.4f * sinf(2.0f * M_PI_F * 440.0f * t) +
                                      0.35f * sinf(2.0f * M_PI_F * 554.37f * t) +
                                      0.25f * sinf(2.0f * M_PI_F * 659.25f * t));
            } else {
                return 0.0f;
            }
        case SOURCE_NOISE_RICH:
            if (t < 0.30f) {
                uint32_t s = (uint32_t)(t * 1000000.0f) * 1103515245u + 12345u;
                float rnd = ((float)(s & 0xFFFF) / 32768.0f) - 1.0f;
                return 0.70f * expf(-t * 15.0f) * rnd;
            } else {
                return 0.0f;
            }
        default:
            return 0.0f;
    }
}

// ---------------------------------------------------------------------------
// 1. Attack Parity (0-300 ms bit-exact & correlation comparison)
// ---------------------------------------------------------------------------
static int run_attack_parity(int argc, char** argv) {
    const char* dump_prefix = (argc >= 3) ? argv[2] : NULL;

    const int sr = 44100;
    const int total_samples = (int)(0.500f * (float)sr); // 500 ms
    const int block_size = 64;

    printf("ATTACK_PARITY_CSV\n");
    printf("Source,Window,RMS_dB,Peak_dB,Centroid_Hz\n");

    for (int s = 0; s < 4; s++) {
        AudioSource_t src = (AudioSource_t)s;
        const char* src_name = g_source_names[s];

        BubbleEngineConfig_t config;
        bubble_engine_default_config(&config);
        config.sample_rate = (float)sr;
        config.memory_mix = 0.50f;
        config.sustain_diffusion_enable = 1;
        config.sustain_diffusion_amount = 0.50f;
        config.rng_seed = 1u;

        BubbleEngine_t engine;
        SoundBubbles_Init(&engine, g_delay, &config);

        float in[64], out_l[64], out_r[64];
        float* out = (float*)malloc((total_samples + block_size) * sizeof(float));
        int out_idx = 0;

        for (int i = 0; i < total_samples; i += block_size) {
            for (int k = 0; k < block_size; k++) {
                float t = (float)(i + k) / (float)sr;
                in[k] = GenerateSourceSample(src, t);
            }
            SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, block_size);
            for (int k = 0; k < block_size; k++) {
                if (out_idx < total_samples) {
                    out[out_idx++] = 0.5f * (out_l[k] + out_r[k]);
                }
            }
        }

        if (dump_prefix != NULL) {
            char filename[512];
            snprintf(filename, sizeof(filename), "%s_%s.f32", dump_prefix, src_name);
            FILE* f = fopen(filename, "wb");
            if (f != NULL) {
                fwrite(out, sizeof(float), (size_t)total_samples, f);
                fclose(f);
            }
        }

        // Metrics for 0-300 ms
        int samps_300ms = (int)(0.300f * (float)sr);
        float r_300 = calc_rms(out, samps_300ms);
        float p_300 = calc_peak(out, samps_300ms);
        float c_300 = calc_centroid(out, samps_300ms, (float)sr);

        // Metrics for 0-500 ms
        float r_500 = calc_rms(out, total_samples);
        float p_500 = calc_peak(out, total_samples);
        float c_500 = calc_centroid(out, total_samples, (float)sr);

        printf("%s,0-300ms,%.2f,%.2f,%.1f\n", src_name, to_db(r_300), to_db(p_300), c_300);
        printf("%s,0-500ms,%.2f,%.2f,%.1f\n", src_name, to_db(r_500), to_db(p_500), c_500);

        free(out);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 2. Transition Discontinuity (230-420 ms boundary)
// ---------------------------------------------------------------------------
static int run_transition_discontinuity(void) {
    const int sr = 44100;
    const int total_samples = (int)(0.700f * (float)sr); // 700 ms
    const int block_size = 64;

    printf("TRANSITION_DISCONTINUITY_CSV\n");
    printf("Source,Region,MaxDiff,RMSDiff,RatioVsPrev\n");

    for (int s = 0; s < 4; s++) {
        AudioSource_t src = (AudioSource_t)s;
        const char* src_name = g_source_names[s];

        BubbleEngineConfig_t config;
        bubble_engine_default_config(&config);
        config.sample_rate = (float)sr;
        config.memory_mix = 0.50f;
        config.rng_seed = 100u;

        BubbleEngine_t engine;
        SoundBubbles_Init(&engine, g_delay, &config);

        float in[64], out_l[64], out_r[64];
        float* out = (float*)malloc((total_samples + block_size) * sizeof(float));
        int out_idx = 0;

        for (int i = 0; i < total_samples; i += block_size) {
            for (int k = 0; k < block_size; k++) {
                float t = (float)(i + k) / (float)sr;
                in[k] = GenerateSourceSample(src, t);
            }
            SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, block_size);
            for (int k = 0; k < block_size; k++) {
                if (out_idx < total_samples) {
                    out[out_idx++] = 0.5f * (out_l[k] + out_r[k]);
                }
            }
        }

        int s_150 = (int)(0.150f * (float)sr);
        int s_230 = (int)(0.230f * (float)sr);
        int s_420 = (int)(0.420f * (float)sr);
        int s_600 = (int)(0.600f * (float)sr);

        {
            float max_d1 = 0.0f, max_d2 = 0.0f, max_d3 = 0.0f;
            double sum_sq1 = 0.0, sum_sq2 = 0.0, sum_sq3 = 0.0;
            int c1 = 0, c2 = 0, c3 = 0;

            for (int i = s_150 + 1; i < s_230; i++) {
                float d = fabsf(out[i] - out[i - 1]);
                if (d > max_d1) max_d1 = d;
                sum_sq1 += (double)d * (double)d;
                c1++;
            }
            for (int i = s_230 + 1; i < s_420; i++) {
                float d = fabsf(out[i] - out[i - 1]);
                if (d > max_d2) max_d2 = d;
                sum_sq2 += (double)d * (double)d;
                c2++;
            }
            for (int i = s_420 + 1; i < s_600; i++) {
                float d = fabsf(out[i] - out[i - 1]);
                if (d > max_d3) max_d3 = d;
                sum_sq3 += (double)d * (double)d;
                c3++;
            }

            float rms_d1 = (c1 > 0) ? (float)sqrt(sum_sq1 / c1) : 0.0f;
            float rms_d2 = (c2 > 0) ? (float)sqrt(sum_sq2 / c2) : 0.0f;
            float rms_d3 = (c3 > 0) ? (float)sqrt(sum_sq3 / c3) : 0.0f;

            float ratio2_1 = (rms_d1 > 1e-6f) ? (rms_d2 / rms_d1) : 1.0f;
            float ratio3_2 = (rms_d2 > 1e-6f) ? (rms_d3 / rms_d2) : 1.0f;

            printf("%s,150-230ms,%.6f,%.6f,1.000\n", src_name, max_d1, rms_d1);
            printf("%s,230-420ms,%.6f,%.6f,%.3f\n", src_name, max_d2, rms_d2, ratio2_1);
            printf("%s,420-600ms,%.6f,%.6f,%.3f\n", src_name, max_d3, rms_d3, ratio3_2);
        }
        free(out);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 3. Tail Evolution (0-1s, 1-2s, 2-6s, 6-12s, 12-20s)
// ---------------------------------------------------------------------------
static int run_tail_evolution(void) {
    const int sr = 44100;
    const int block_size = 64;
    const float duration_sec = 20.0f;
    const int total_samples = (int)(duration_sec * (float)sr);

    printf("TAIL_EVOLUTION_CSV\n");
    printf("Source,Window,RMS_dB,Peak_dB,Centroid_Hz,MeanAge,P50Age,P95Age,MeanCutoff_Hz,P50Cutoff_Hz,MinCutoff_Hz,AppliedCutoff_Hz\n");

    const float win_starts[5] = { 0.0f, 1.0f, 2.0f, 6.0f, 12.0f };
    const float win_ends[5]   = { 1.0f, 2.0f, 6.0f, 12.0f, 20.0f };
    const char* win_names[5]  = { "0-1s", "1-2s", "2-6s", "6-12s", "12-20s" };

    for (int s = 0; s < 4; s++) {
        AudioSource_t src = (AudioSource_t)s;
        const char* src_name = g_source_names[s];

        BubbleEngineConfig_t config;
        bubble_engine_default_config(&config);
        config.sample_rate = (float)sr;
        config.memory_mix = 0.65f;
        config.sustain_diffusion_enable = 1;
        config.sustain_diffusion_amount = 0.50f;
        config.rng_seed = 42u;

        BubbleEngine_t engine;
        SoundBubbles_Init(&engine, g_delay, &config);

        float in[64], out_l[64], out_r[64];
        float* out = (float*)malloc((total_samples + block_size) * sizeof(float));
        int out_idx = 0;

        int current_win = 0;

        for (int i = 0; i < total_samples; i += block_size) {
            float t_block = (float)i / (float)sr;
            for (int k = 0; k < block_size; k++) {
                float t = (float)(i + k) / (float)sr;
                in[k] = GenerateSourceSample(src, t);
            }
            SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, block_size);
            for (int k = 0; k < block_size; k++) {
                if (out_idx < total_samples) {
                    out[out_idx++] = 0.5f * (out_l[k] + out_r[k]);
                }
            }

            // Check if we reached window end
            if (current_win < 5 && t_block >= win_ends[current_win] - 0.010f) {
                int start_samp = (int)(win_starts[current_win] * (float)sr);
                int end_samp = (int)(win_ends[current_win] * (float)sr);
                if (end_samp > total_samples) end_samp = total_samples;
                int len = end_samp - start_samp;

                float r = calc_rms(out + start_samp, len);
                float p = calc_peak(out + start_samp, len);
                float c = calc_centroid(out + start_samp, len, (float)sr);

                SoundBubblesSpectralMemoryMetrics_t sm;
                SoundBubbles_GetSpectralMemoryMetrics(&engine, &sm);

                printf("%s,%s,%.2f,%.2f,%.1f,%.3f,%.3f,%.3f,%.1f,%.1f,%.1f,%.1f\n",
                       src_name, win_names[current_win],
                       to_db(r), to_db(p), c,
                       sm.mean_spectral_age, sm.p50_spectral_age, sm.p95_spectral_age,
                       sm.mean_cutoff_hz, sm.p50_cutoff_hz, sm.min_cutoff_hz, sm.sustain_applied_cutoff_hz);

                current_win++;
            }
        }
        free(out);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 4. Memory Sweep (MEMORY = 0.0, 0.25, 0.50, 0.75, 1.0)
// ---------------------------------------------------------------------------
static int run_memory_sweep(void) {
    const int sr = 44100;
    const int block_size = 64;
    const float duration_sec = 8.0f;
    const int total_samples = (int)(duration_sec * (float)sr);

    printf("MEMORY_SWEEP_CSV\n");
    printf("MemoryMacro,TailRMS_dB,TailCentroid_Hz,MeanSpectralAge,P50SpectralAge,MeanCutoff_Hz,AppliedCutoff_Hz\n");

    const float mem_vals[5] = { 0.0f, 0.25f, 0.50f, 0.75f, 1.0f };

    for (int m = 0; m < 5; m++) {
        float mem = mem_vals[m];

        BubbleEngineConfig_t config;
        bubble_engine_default_config(&config);
        config.sample_rate = (float)sr;
        config.memory_mix = mem;
        config.memory_pull = mem;
        config.rng_seed = 777u;

        BubbleEngine_t engine;
        SoundBubbles_Init(&engine, g_delay, &config);

        float in[64], out_l[64], out_r[64];
        float* out = (float*)malloc((total_samples + block_size) * sizeof(float));
        int out_idx = 0;

        for (int i = 0; i < total_samples; i += block_size) {
            for (int k = 0; k < block_size; k++) {
                float t = (float)(i + k) / (float)sr;
                in[k] = GenerateSourceSample(SOURCE_HARMONIC_PLUCK, t);
            }
            SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, block_size);
            for (int k = 0; k < block_size; k++) {
                if (out_idx < total_samples) {
                    out[out_idx++] = 0.5f * (out_l[k] + out_r[k]);
                }
            }
        }

        // Measure late tail 4-8s
        int start_samp = (int)(4.0f * (float)sr);
        int len = total_samples - start_samp;
        float r = calc_rms(out + start_samp, len);
        float c = calc_centroid(out + start_samp, len, (float)sr);

        SoundBubblesSpectralMemoryMetrics_t sm;
        SoundBubbles_GetSpectralMemoryMetrics(&engine, &sm);

        printf("%.2f,%.2f,%.1f,%.3f,%.3f,%.1f,%.1f\n",
               mem, to_db(r), c,
               sm.mean_spectral_age, sm.p50_spectral_age,
               sm.mean_cutoff_hz, sm.sustain_applied_cutoff_hz);

        free(out);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 5. Clarity Sweep (CLARITY = 0.0, 0.25, 0.50, 0.75, 1.0)
// ---------------------------------------------------------------------------
static int run_clarity_sweep(void) {
    const int sr = 44100;
    const int block_size = 64;
    const float duration_sec = 8.0f;
    const int total_samples = (int)(duration_sec * (float)sr);

    printf("CLARITY_SWEEP_CSV\n");
    printf("ClarityMacro,TailRMS_dB,TailCentroid_Hz,MeanSpectralAge,MeanCutoff_Hz,AppliedCutoff_Hz\n");

    const float clarity_vals[5] = { 0.0f, 0.25f, 0.50f, 0.75f, 1.0f };

    for (int c_idx = 0; c_idx < 5; c_idx++) {
        float clarity = clarity_vals[c_idx];

        BubbleEngineConfig_t config;
        bubble_engine_default_config(&config);
        config.sample_rate = (float)sr;
        config.memory_mix = 0.60f;
        // Map clarity to attack_brightness (0.80 - 1.65) and sustain_darkness (0.72 - 0.10)
        config.attack_brightness = 0.80f + 0.85f * clarity;
        config.sustain_darkness = 0.72f - 0.62f * clarity;
        config.rng_seed = 888u;

        BubbleEngine_t engine;
        SoundBubbles_Init(&engine, g_delay, &config);

        float in[64], out_l[64], out_r[64];
        float* out = (float*)malloc((total_samples + block_size) * sizeof(float));
        int out_idx = 0;

        for (int i = 0; i < total_samples; i += block_size) {
            for (int k = 0; k < block_size; k++) {
                float t = (float)(i + k) / (float)sr;
                in[k] = GenerateSourceSample(SOURCE_HARMONIC_PLUCK, t);
            }
            SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, block_size);
            for (int k = 0; k < block_size; k++) {
                if (out_idx < total_samples) {
                    out[out_idx++] = 0.5f * (out_l[k] + out_r[k]);
                }
            }
        }

        int start_samp = (int)(4.0f * (float)sr);
        int len = total_samples - start_samp;
        float r = calc_rms(out + start_samp, len);
        float cent = calc_centroid(out + start_samp, len, (float)sr);

        SoundBubblesSpectralMemoryMetrics_t sm;
        SoundBubbles_GetSpectralMemoryMetrics(&engine, &sm);

        printf("%.2f,%.2f,%.1f,%.3f,%.1f,%.1f\n",
               clarity, to_db(r), cent,
               sm.mean_spectral_age, sm.mean_cutoff_hz, sm.sustain_applied_cutoff_hz);

        free(out);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 6. Warmth Sweep (WARMTH = 0.0, 0.25, 0.50, 0.75, 1.0)
// ---------------------------------------------------------------------------
static int run_warmth_sweep(void) {
    const int sr = 44100;
    const int block_size = 64;
    const float duration_sec = 8.0f;
    const int total_samples = (int)(duration_sec * (float)sr);

    printf("WARMTH_SWEEP_CSV\n");
    printf("WarmthMacro,TailRMS_dB,TailCentroid_Hz,MeanSpectralAge,MeanCutoff_Hz,AppliedCutoff_Hz\n");

    const float warmth_vals[5] = { 0.0f, 0.25f, 0.50f, 0.75f, 1.0f };

    for (int w_idx = 0; w_idx < 5; w_idx++) {
        float warmth = warmth_vals[w_idx];

        BubbleEngineConfig_t config;
        bubble_engine_default_config(&config);
        config.sample_rate = (float)sr;
        config.memory_mix = 0.60f;
        config.wet_drive = 0.95f + 0.30f * warmth;
        config.wet_clip_amount = 0.04f + 0.51f * warmth;
        config.rng_seed = 999u;

        BubbleEngine_t engine;
        SoundBubbles_Init(&engine, g_delay, &config);

        float in[64], out_l[64], out_r[64];
        float* out = (float*)malloc((total_samples + block_size) * sizeof(float));
        int out_idx = 0;

        for (int i = 0; i < total_samples; i += block_size) {
            for (int k = 0; k < block_size; k++) {
                float t = (float)(i + k) / (float)sr;
                in[k] = GenerateSourceSample(SOURCE_HARMONIC_PLUCK, t);
            }
            SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, block_size);
            for (int k = 0; k < block_size; k++) {
                if (out_idx < total_samples) {
                    out[out_idx++] = 0.5f * (out_l[k] + out_r[k]);
                }
            }
        }

        int start_samp = (int)(4.0f * (float)sr);
        int len = total_samples - start_samp;
        float r = calc_rms(out + start_samp, len);
        float cent = calc_centroid(out + start_samp, len, (float)sr);

        SoundBubblesSpectralMemoryMetrics_t sm;
        SoundBubbles_GetSpectralMemoryMetrics(&engine, &sm);

        printf("%.2f,%.2f,%.1f,%.3f,%.1f,%.1f\n",
               warmth, to_db(r), cent,
               sm.mean_spectral_age, sm.mean_cutoff_hz, sm.sustain_applied_cutoff_hz);

        free(out);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 7. Freeze & Auto-Hold Stability Test
// ---------------------------------------------------------------------------
static int run_freeze_autohold_test(void) {
    const int sr = 44100;
    const int block_size = 64;
    const float duration_sec = 10.0f;
    const int total_samples = (int)(duration_sec * (float)sr);

    printf("FREEZE_AUTOHOLD_CSV\n");
    printf("Time_s,FreezeActive,AutoHoldState,SpectralAge,AppliedCutoff_Hz\n");

    BubbleEngineConfig_t config;
    bubble_engine_default_config(&config);
    config.sample_rate = (float)sr;
    config.memory_mix = 0.60f;
    config.rng_seed = 1234u;

    BubbleEngine_t engine;
    SoundBubbles_Init(&engine, g_delay, &config);

    float in[64], out_l[64], out_r[64];
    int last_report_step = -1;

    for (int i = 0; i < total_samples; i += block_size) {
        float t = (float)i / (float)sr;
        // Engage freeze at t=2.5s
        if (t >= 2.5f) {
            engine.smoothed_freeze = 1.0f;
            config.freeze_amount = 1.0f;
        }

        for (int k = 0; k < block_size; k++) {
            float cur_t = (float)(i + k) / (float)sr;
            in[k] = (cur_t < 0.30f) ? GenerateSourceSample(SOURCE_HARMONIC_PLUCK, cur_t) : 0.0f;
        }

        SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, block_size);

        int report_step = (int)(t * 2.0f); // Every 0.5s
        if (report_step > last_report_step) {
            last_report_step = report_step;
            printf("%.1f,%d,%d,%.4f,%.1f\n",
                   (float)report_step * 0.5f,
                   (engine.smoothed_freeze > 0.5f) ? 1 : 0,
                   (int)engine.auto_hold_state,
                   GetEngineSpectralAge(&engine),
                   engine.sustain_lpf_applied_hz);
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 8. Cross-Phrase Reset Test
// ---------------------------------------------------------------------------
static int run_cross_phrase_test(void) {
    const int sr = 44100;
    const int block_size = 64;
    const float duration_sec = 6.0f;
    const int total_samples = (int)(duration_sec * (float)sr);

    printf("CROSS_PHRASE_CSV\n");
    printf("Time_s,Event,SpectralAge,TargetAge,AppliedCutoff_Hz\n");

    BubbleEngineConfig_t config;
    bubble_engine_default_config(&config);
    config.sample_rate = (float)sr;
    config.memory_mix = 0.70f;
    config.rng_seed = 555u;

    BubbleEngine_t engine;
    SoundBubbles_Init(&engine, g_delay, &config);

    float in[64], out_l[64], out_r[64];
    int last_report_step = -1;

    for (int i = 0; i < total_samples; i += block_size) {
        float t = (float)i / (float)sr;
        // Phrase 1: t=0.0s to 0.3s
        // Phrase 2: t=3.0s to 3.3s
        for (int k = 0; k < block_size; k++) {
            float cur_t = (float)(i + k) / (float)sr;
            if (cur_t < 0.30f) {
                in[k] = GenerateSourceSample(SOURCE_HARMONIC_PLUCK, cur_t);
            } else if (cur_t >= 3.0f && cur_t < 3.30f) {
                in[k] = GenerateSourceSample(SOURCE_HARMONIC_PLUCK, cur_t - 3.0f);
            } else {
                in[k] = 0.0f;
            }
        }

        SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, block_size);

        // Report critical transition moments
        int report_step = (int)(t * 2.0f);
        if ((t >= 2.95f && t <= 3.25f) || (report_step > last_report_step)) {
            if (report_step > last_report_step) last_report_step = report_step;
            const char* evt = "tail";
            if (t >= 0.0f && t < 0.3f) evt = "attack1";
            else if (t >= 2.95f && t < 3.25f) evt = "attack2";
            printf("%.3f,%s,%.4f,%.4f,%.1f\n",
                   t, evt,
                   GetEngineSpectralAge(&engine),
                   GetEngineSpectralTarget(&engine),
                   engine.sustain_lpf_applied_hz);
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 9. Shimmer Preservation Test
// ---------------------------------------------------------------------------
static int run_shimmer_preservation(void) {
    const int sr = 44100;
    const int block_size = 64;
    const float duration_sec = 6.0f;
    const int total_samples = (int)(duration_sec * (float)sr);

    printf("SHIMMER_PRESERVATION_CSV\n");
    printf("ShimmerAmount,MinCutoffObserved_Hz,SustainAppliedCutoff_Hz,SpectralAge,FloorPreserved\n");

    const float shimmer_vals[3] = { 0.0f, 0.50f, 1.0f };

    for (int s_idx = 0; s_idx < 3; s_idx++) {
        float shimmer = shimmer_vals[s_idx];

        BubbleEngineConfig_t config;
        bubble_engine_default_config(&config);
        config.sample_rate = (float)sr;
        config.memory_mix = 0.80f; // High memory mix to drive deep tail darkening
        config.shimmer_amount = shimmer;
        config.rng_seed = 333u;

        BubbleEngine_t engine;
        SoundBubbles_Init(&engine, g_delay, &config);

        float in[64], out_l[64], out_r[64];
        float min_cutoff_seen = 20000.0f;

        for (int i = 0; i < total_samples; i += block_size) {
            for (int k = 0; k < block_size; k++) {
                float t = (float)(i + k) / (float)sr;
                in[k] = (t < 0.30f) ? GenerateSourceSample(SOURCE_HARMONIC_PLUCK, t) : 0.0f;
            }
            SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, block_size);
            if (engine.sustain_lpf_applied_hz < min_cutoff_seen) {
                min_cutoff_seen = engine.sustain_lpf_applied_hz;
            }
        }

        float floor_target = (shimmer > 0.05f) ? 3200.0f : 2800.0f;
        int preserved = (min_cutoff_seen >= floor_target - 50.0f) ? 1 : 0;

        printf("%.2f,%.1f,%.1f,%.3f,%d\n",
               shimmer, min_cutoff_seen, engine.sustain_lpf_applied_hz,
               GetEngineSpectralAge(&engine), preserved);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 10. Block Size Invariance (32, 64, 128, 256)
// ---------------------------------------------------------------------------
static int run_block_invariance(void) {
    const int sr = 44100;
    const float duration_sec = 2.0f;
    const int total_samples = (int)(duration_sec * (float)sr);
    const int block_sizes[4] = { 32, 64, 128, 256 };

    printf("BLOCK_INVARIANCE_CSV\n");
    printf("BlockSize,RMS_dB,Peak_dB,MeanAge,AppliedCutoff_Hz\n");

    for (int b = 0; b < 4; b++) {
        int bs = block_sizes[b];

        BubbleEngineConfig_t config;
        bubble_engine_default_config(&config);
        config.sample_rate = (float)sr;
        config.memory_mix = 0.50f;
        config.rng_seed = 4444u;

        BubbleEngine_t engine;
        SoundBubbles_Init(&engine, g_delay, &config);

        float* in = (float*)malloc(bs * sizeof(float));
        float* out_l = (float*)malloc(bs * sizeof(float));
        float* out_r = (float*)malloc(bs * sizeof(float));
        float* full_out = (float*)malloc(total_samples * sizeof(float));
        int out_idx = 0;

        for (int i = 0; i < total_samples; i += bs) {
            for (int k = 0; k < bs; k++) {
                float t = (float)(i + k) / (float)sr;
                in[k] = (t < 0.25f) ? GenerateSourceSample(SOURCE_HARMONIC_PLUCK, t) : 0.0f;
            }
            SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, bs);
            for (int k = 0; k < bs; k++) {
                if (out_idx < total_samples) {
                    full_out[out_idx++] = 0.5f * (out_l[k] + out_r[k]);
                }
            }
        }

        float r = calc_rms(full_out, total_samples);
        float p = calc_peak(full_out, total_samples);

        printf("%d,%.2f,%.2f,%.4f,%.1f\n",
               bs, to_db(r), to_db(p),
               GetEngineSpectralAge(&engine),
               engine.sustain_lpf_applied_hz);

        free(in);
        free(out_l);
        free(out_r);
        free(full_out);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 11. CPU Benchmark
// ---------------------------------------------------------------------------
static int run_cpu_benchmark(void) {
    const int sr = 44100;
    const int block_size = 64;
    const float duration_sec = 10.0f;
    const int total_samples = (int)(duration_sec * (float)sr);

    printf("CPU_BENCHMARK_CSV\n");
    printf("Run,AudioSeconds,ElapsedMs,CpuPercent\n");

    BubbleEngineConfig_t config;
    bubble_engine_default_config(&config);
    config.sample_rate = (float)sr;
    config.memory_mix = 0.70f;
    config.sustain_diffusion_enable = 1;
    config.sustain_diffusion_amount = 0.60f;
    config.rng_seed = 9999u;

    BubbleEngine_t engine;
    SoundBubbles_Init(&engine, g_delay, &config);

    float in[64], out_l[64], out_r[64];

    // Warm up
    for (int i = 0; i < 6400; i += block_size) {
        for (int k = 0; k < block_size; k++) in[k] = 0.1f;
        SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, block_size);
    }

    for (int run = 0; run < 3; run++) {
        clock_t t0 = clock();
        for (int i = 0; i < total_samples; i += block_size) {
            for (int k = 0; k < block_size; k++) {
                float t = (float)(i + k) / (float)sr;
                in[k] = GenerateSourceSample(SOURCE_HARMONIC_PLUCK, t);
            }
            SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, block_size);
        }
        clock_t t1 = clock();
        double elapsed_ms = 1000.0 * (double)(t1 - t0) / (double)CLOCKS_PER_SEC;
        double cpu_pct = 100.0 * (elapsed_ms / (duration_sec * 1000.0));

        printf("%d,%.1f,%.2f,%.2f\n", run + 1, duration_sec, elapsed_ms, cpu_pct);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 12. Silence Startup
// ---------------------------------------------------------------------------
static int run_silence_startup(void) {
    const int sr = 44100;
    const int block_size = 64;
    const int total_blocks = 100;

    printf("SILENCE_STARTUP_CSV\n");
    printf("Block,MaxAbsOut,SpectralAge,IsClean\n");

    BubbleEngineConfig_t config;
    bubble_engine_default_config(&config);
    config.sample_rate = (float)sr;
    config.rng_seed = 12u;

    BubbleEngine_t engine;
    SoundBubbles_Init(&engine, g_delay, &config);

    float in[64], out_l[64], out_r[64];
    memset(in, 0, sizeof(in));

    float max_abs = 0.0f;
    for (int b = 0; b < total_blocks; b++) {
        SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, block_size);
        for (int k = 0; k < block_size; k++) {
            float al = fabsf(out_l[k]);
            float ar = fabsf(out_r[k]);
            if (al > max_abs) max_abs = al;
            if (ar > max_abs) max_abs = ar;
        }
    }

    int is_clean = (max_abs <= 1e-12f && GetEngineSpectralAge(&engine) == 0.0f) ? 1 : 0;
    printf("%d,%.12f,%.6f,%d\n", total_blocks, max_abs, GetEngineSpectralAge(&engine), is_clean);
    return (is_clean == 1) ? 0 : 1;
}

// ---------------------------------------------------------------------------
// 13. Runaway Stress Test
// ---------------------------------------------------------------------------
static int run_runaway_stress(void) {
    const int sr = 44100;
    const int block_size = 64;
    const float duration_sec = 5.0f;
    const int total_samples = (int)(duration_sec * (float)sr);

    printf("RUNAWAY_STRESS_CSV\n");
    printf("MaxPeak_dB,WetNormGainMin,FinalLimiterMaxGR_dB,IsStable\n");

    BubbleEngineConfig_t config;
    bubble_engine_default_config(&config);
    config.sample_rate = (float)sr;
    config.memory_mix = 1.0f;
    config.memory_pull = 1.0f;
    config.wet_drive = 2.0f;
    config.wet_clip_amount = 0.90f;
    config.density_burst = 140.0f;
    config.density_sustain = 55.0f;
    config.sustain_diffusion_enable = 1;
    config.sustain_diffusion_amount = 0.80f;
    config.rng_seed = 666u;

    BubbleEngine_t engine;
    SoundBubbles_Init(&engine, g_delay, &config);

    float in[64], out_l[64], out_r[64];
    float max_peak = 0.0f;
    bool has_nan = false;

    for (int i = 0; i < total_samples; i += block_size) {
        for (int k = 0; k < block_size; k++) {
            float t = (float)(i + k) / (float)sr;
            in[k] = GenerateSourceSample(SOURCE_HARMONIC_PLUCK, t) * 1.5f; // Hard input
        }
        SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, block_size);
        for (int k = 0; k < block_size; k++) {
            if (isnan(out_l[k]) || isinf(out_l[k]) || isnan(out_r[k]) || isinf(out_r[k])) {
                has_nan = true;
            }
            float al = fabsf(out_l[k]);
            float ar = fabsf(out_r[k]);
            if (al > max_peak) max_peak = al;
            if (ar > max_peak) max_peak = ar;
        }
    }

    SoundBubblesLimiterHierarchyMetrics_t lm;
    SoundBubbles_GetLimiterHierarchyMetrics(&engine, &lm);

    int stable = (!has_nan && max_peak <= 1.50f) ? 1 : 0;
    printf("%.2f,%.3f,%.2f,%d\n",
           to_db(max_peak), lm.wet_norm_gain_min, lm.final_limiter_max_gr_db, stable);

    return (stable == 1) ? 0 : 1;
}

// ---------------------------------------------------------------------------
// Main dispatcher
// ---------------------------------------------------------------------------
int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <subcommand> [options]\n", argv[0]);
        fprintf(stderr, "Subcommands:\n");
        fprintf(stderr, "  attack_parity [dump_prefix]\n");
        fprintf(stderr, "  transition_discontinuity\n");
        fprintf(stderr, "  tail_evolution\n");
        fprintf(stderr, "  memory_sweep\n");
        fprintf(stderr, "  clarity_sweep\n");
        fprintf(stderr, "  warmth_sweep\n");
        fprintf(stderr, "  freeze_autohold_test\n");
        fprintf(stderr, "  cross_phrase_test\n");
        fprintf(stderr, "  shimmer_preservation\n");
        fprintf(stderr, "  block_invariance\n");
        fprintf(stderr, "  cpu_benchmark\n");
        fprintf(stderr, "  silence_startup\n");
        fprintf(stderr, "  runaway_stress\n");
        return 1;
    }

    const char* sub = argv[1];
    if (strcmp(sub, "attack_parity") == 0) return run_attack_parity(argc, argv);
    if (strcmp(sub, "transition_discontinuity") == 0) return run_transition_discontinuity();
    if (strcmp(sub, "tail_evolution") == 0) return run_tail_evolution();
    if (strcmp(sub, "memory_sweep") == 0) return run_memory_sweep();
    if (strcmp(sub, "clarity_sweep") == 0) return run_clarity_sweep();
    if (strcmp(sub, "warmth_sweep") == 0) return run_warmth_sweep();
    if (strcmp(sub, "freeze_autohold_test") == 0) return run_freeze_autohold_test();
    if (strcmp(sub, "cross_phrase_test") == 0) return run_cross_phrase_test();
    if (strcmp(sub, "shimmer_preservation") == 0) return run_shimmer_preservation();
    if (strcmp(sub, "block_invariance") == 0) return run_block_invariance();
    if (strcmp(sub, "cpu_benchmark") == 0) return run_cpu_benchmark();
    if (strcmp(sub, "silence_startup") == 0) return run_silence_startup();
    if (strcmp(sub, "runaway_stress") == 0) return run_runaway_stress();

    fprintf(stderr, "Unknown subcommand: %s\n", sub);
    return 1;
}
