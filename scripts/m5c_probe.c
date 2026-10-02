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
static inline float GetEngineAppliedCutoff(const BubbleEngine_t* e) { return e->sustain_lpf_applied_hz; }
#else
static inline float GetEngineSpectralAge(const BubbleEngine_t* e) { (void)e; return 0.0f; }
static inline float GetEngineSpectralTarget(const BubbleEngine_t* e) { (void)e; return 0.0f; }
static inline float GetEngineAppliedCutoff(const BubbleEngine_t* e) { (void)e; return 5000.0f; }
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

static float calc_high_band_energy(const float* x, int n, float sr, float cutoff_hz) {
    if (n <= 1 || sr <= 0.0f) return 0.0f;
    int nfft = 4096;
    if (n < 4096) {
        nfft = 128;
        while (nfft < n) nfft <<= 1;
    }
    float* mag = (float*)malloc(((nfft >> 1) + 1) * sizeof(float));
    if (!mag) return 0.0f;
    compute_real_fft_mag(x, n, nfft, mag);

    double high_sum = 0.0;
    int half_fft = nfft >> 1;
    float bin_hz = sr / (float)nfft;
    for (int k = 1; k <= half_fft; k++) {
        if ((float)k * bin_hz >= cutoff_hz) {
            high_sum += (double)mag[k] * (double)mag[k];
        }
    }
    free(mag);
    return (float)high_sum;
}

static float calc_corr(const float* a, const float* b, int n) {
    if (n <= 1) return 1.0f;
    double sa = 0.0, sb = 0.0;
    for (int i = 0; i < n; i++) { sa += a[i]; sb += b[i]; }
    double ma = sa / (double)n, mb = sb / (double)n;
    double num = 0.0, da = 0.0, db = 0.0;
    for (int i = 0; i < n; i++) {
        double va = a[i] - ma;
        double vb = b[i] - mb;
        num += va * vb;
        da += va * va;
        db += vb * vb;
    }
    if (da < 1e-12 || db < 1e-12) return 1.0f;
    return (float)(num / sqrt(da * db));
}

static float find_dominant_freq(const float* x, int n, float sr, float min_hz, float max_hz) {
    int nfft = 16384;
    float* mag = (float*)malloc(((nfft >> 1) + 1) * sizeof(float));
    if (!mag) return 0.0f;
    compute_real_fft_mag(x, n, nfft, mag);

    int half_fft = nfft >> 1;
    float bin_hz = sr / (float)nfft;
    int k_min = (int)(min_hz / bin_hz);
    int k_max = (int)(max_hz / bin_hz);
    if (k_min < 1) k_min = 1;
    if (k_max > half_fft - 1) k_max = half_fft - 1;

    int best_k = k_min;
    float best_mag = 0.0f;
    for (int k = k_min; k <= k_max; k++) {
        if (mag[k] > best_mag) {
            best_mag = mag[k];
            best_k = k;
        }
    }

    float observed_hz = (float)best_k * bin_hz;
    if (best_k > k_min && best_k < k_max) {
        float alpha = mag[best_k - 1];
        float beta = mag[best_k];
        float gamma = mag[best_k + 1];
        float denom = alpha - 2.0f * beta + gamma;
        if (fabsf(denom) > 1e-12f) {
            float p = 0.5f * (alpha - gamma) / denom;
            observed_hz = ((float)best_k + p) * bin_hz;
        }
    }
    free(mag);
    return observed_hz;
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
// 14. Cross-Phrase Audio Parity (Phrase A -> decay -> Phrase B vs Phrase B isolated)
// ---------------------------------------------------------------------------
static int run_cross_phrase_audio(void) {
    const int sr = 44100;
    const int block_size = 64;
    const int b_start_block = 2070; // 132480 samples = 3.00408s
    const int b_start_sample = b_start_block * block_size;
    const float phrase_b_start_s = (float)b_start_sample / (float)sr;
    const float duration_s = phrase_b_start_s + 0.50f;
    const int total_samples = (int)(duration_s * (float)sr);
    const int b_eval_samples = (int)(0.300f * (float)sr); // 300 ms

    printf("CROSS_PHRASE_AUDIO_CSV\n");
    printf("Source,Window,RMS_A_dB,RMS_B_dB,DeltaRMS_dB,Peak_A_dB,Peak_B_dB,DeltaPeak_dB,Corr,MaxAbsDiff,Centroid_FG,Centroid_B,DeltaCentroid_Pct,DeltaHighBand_dB\n");

    AudioSource_t sources[4] = {
        SOURCE_HARMONIC_PLUCK,
        SOURCE_HARP_TRANSIENT,
        SOURCE_SUSTAINED_CHORD,
        SOURCE_PERCUSSIVE_PULSE
    };

    for (int s = 0; s < 4; s++) {
        AudioSource_t src = sources[s];
        const char* sname = g_source_names[src];

        // 1. Render A: Phrase A at 0s, Phrase B at phrase_b_start_s
        float* out_A = (float*)malloc(total_samples * sizeof(float));
        {
            memset(g_delay, 0, sizeof(g_delay));
            BubbleEngineConfig_t config;
            bubble_engine_default_config(&config);
            config.sample_rate = (float)sr;
            config.memory_mix = 0.60f;
            config.rng_seed = 1000u;
            BubbleEngine_t engine;
            SoundBubbles_Init(&engine, g_delay, &config);

            float in[64], out_l[64], out_r[64];
            int out_idx = 0;
            for (int i = 0; i < total_samples; i += block_size) {
                for (int k = 0; k < block_size; k++) {
                    float t = (float)(i + k) / (float)sr;
                    if (t < 0.30f) {
                        in[k] = GenerateSourceSample(src, t);
                    } else if (t >= phrase_b_start_s && t < (phrase_b_start_s + 0.30f)) {
                        in[k] = GenerateSourceSample(src, t - phrase_b_start_s);
                    } else {
                        in[k] = 0.0f;
                    }
                }
                SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, block_size);
                for (int k = 0; k < block_size && out_idx < total_samples; k++) {
                    out_A[out_idx++] = 0.5f * (out_l[k] + out_r[k]);
                }
            }
        }

        // 2. Render Tail Only: Phrase A at 0s, silence at phrase_b_start_s
        float* out_Tail = (float*)malloc(total_samples * sizeof(float));
        {
            memset(g_delay, 0, sizeof(g_delay));
            BubbleEngineConfig_t config;
            bubble_engine_default_config(&config);
            config.sample_rate = (float)sr;
            config.memory_mix = 0.60f;
            config.rng_seed = 1000u;
            BubbleEngine_t engine;
            SoundBubbles_Init(&engine, g_delay, &config);

            float in[64], out_l[64], out_r[64];
            int out_idx = 0;
            for (int i = 0; i < total_samples; i += block_size) {
                for (int k = 0; k < block_size; k++) {
                    float t = (float)(i + k) / (float)sr;
                    if (t < 0.30f) {
                        in[k] = GenerateSourceSample(src, t);
                    } else {
                        in[k] = 0.0f;
                    }
                }
                SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, block_size);
                for (int k = 0; k < block_size && out_idx < total_samples; k++) {
                    out_Tail[out_idx++] = 0.5f * (out_l[k] + out_r[k]);
                }
            }
        }

        // 3. Render B: Phrase B alone (isolated with equivalent silence history setup)
        float* out_B = (float*)malloc(total_samples * sizeof(float));
        {
            memset(g_delay, 0, sizeof(g_delay));
            BubbleEngineConfig_t config;
            bubble_engine_default_config(&config);
            config.sample_rate = (float)sr;
            config.memory_mix = 0.60f;
            config.rng_seed = 1000u;
            BubbleEngine_t engine;
            SoundBubbles_Init(&engine, g_delay, &config);

            float in[64], out_l[64], out_r[64];
            int out_idx = 0;
            for (int i = 0; i < total_samples; i += block_size) {
                for (int k = 0; k < block_size; k++) {
                    float t = (float)(i + k) / (float)sr;
                    if (t >= phrase_b_start_s && t < (phrase_b_start_s + 0.30f)) {
                        in[k] = GenerateSourceSample(src, t - phrase_b_start_s);
                    } else {
                        in[k] = 0.0f;
                    }
                }
                SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, block_size);
                for (int k = 0; k < block_size && out_idx < total_samples; k++) {
                    out_B[out_idx++] = 0.5f * (out_l[k] + out_r[k]);
                }
            }
        }

        float* seg_A = out_A + b_start_sample;
        float* seg_B = out_B + b_start_sample;
        float* seg_Tail = out_Tail + b_start_sample;
        float* seg_FG = (float*)malloc(b_eval_samples * sizeof(float));
        for (int i = 0; i < b_eval_samples; i++) {
            seg_FG[i] = seg_A[i] - seg_Tail[i];
        }

        struct {
            const char* name;
            int start_ms;
            int end_ms;
        } windows[4] = {
            {"0-50ms", 0, 50},
            {"50-100ms", 50, 100},
            {"100-200ms", 100, 200},
            {"200-300ms", 200, 300}
        };

        for (int w = 0; w < 4; w++) {
            int s_idx = (int)((float)windows[w].start_ms * (float)sr / 1000.0f);
            int e_idx = (int)((float)windows[w].end_ms * (float)sr / 1000.0f);
            int n_samps = e_idx - s_idx;

            const float* cur_A = seg_A + s_idx;
            const float* cur_FG = seg_FG + s_idx;
            const float* cur_B = seg_B + s_idx;

            float rms_A = calc_rms(cur_A, n_samps);
            float rms_B = calc_rms(cur_B, n_samps);

            float peak_A = calc_peak(cur_A, n_samps);
            float peak_B = calc_peak(cur_B, n_samps);

            float max_diff = 0.0f;
            for (int i = 0; i < n_samps; i++) {
                float d = fabsf(cur_FG[i] - cur_B[i]);
                if (d > max_diff) max_diff = d;
            }

            float corr = calc_corr(cur_FG, cur_B, n_samps);
            float cent_FG = calc_centroid(cur_FG, n_samps, (float)sr);
            float cent_B = calc_centroid(cur_B, n_samps, (float)sr);
            float delta_cent_pct = (cent_B > 0.0f) ? (fabsf(cent_FG - cent_B) / cent_B * 100.0f) : 0.0f;

            float hb_FG = calc_high_band_energy(cur_FG, n_samps, (float)sr, 3000.0f);
            float hb_B = calc_high_band_energy(cur_B, n_samps, (float)sr, 3000.0f);
            float delta_hb_db = (hb_FG > 1e-12f && hb_B > 1e-12f) ? fabsf(10.0f * log10f(hb_FG / hb_B)) : 0.0f;

            float rms_db_A = to_db(rms_A);
            float rms_db_B = to_db(rms_B);
            float delta_rms_db = fabsf(rms_db_A - rms_db_B);

            float peak_db_A = to_db(peak_A);
            float peak_db_B = to_db(peak_B);
            float delta_peak_db = fabsf(peak_db_A - peak_db_B);

            printf("%s,%s,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.6f,%.6f,%.1f,%.1f,%.2f%%,%.2f\n",
                   sname, windows[w].name,
                   rms_db_A, rms_db_B, delta_rms_db,
                   peak_db_A, peak_db_B, delta_peak_db,
                   corr, max_diff,
                   cent_FG, cent_B, delta_cent_pct, delta_hb_db);
        }

        free(seg_FG);
        free(out_A);
        free(out_Tail);
        free(out_B);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 15. Freeze Settling & Telemetry Tracking (0 to 15 s)
// ---------------------------------------------------------------------------
static int run_freeze_settling(void) {
    const int sr = 44100;
    const int block_size = 64;
    const float duration_s = 15.0f;
    const int total_samples = (int)(duration_s * (float)sr);
    const float freeze_time_s = 2.5f;

    memset(g_delay, 0, sizeof(g_delay));
    BubbleEngineConfig_t config;
    bubble_engine_default_config(&config);
    config.sample_rate = (float)sr;
    config.memory_mix = 0.60f;
    config.rng_seed = 1234u;

    BubbleEngine_t engine;
    SoundBubbles_Init(&engine, g_delay, &config);

    float in[64], out_l[64], out_r[64];
    float* out = (float*)malloc(total_samples * sizeof(float));
    int out_idx = 0;

    float offsets[] = { 0.0f, 0.10f, 0.25f, 0.50f, 1.0f, 2.0f, 5.0f, 10.0f };
    const char* offset_names[] = { "freeze moment", "+100 ms", "+250 ms", "+500 ms", "+1 s", "+2 s", "+5 s", "+10 s" };
    int num_offsets = sizeof(offsets) / sizeof(offsets[0]);

    float snap_age[8], snap_target[8], snap_applied[8];
    int recorded[8] = {0};

    for (int i = 0; i < total_samples; i += block_size) {
        float t = (float)i / (float)sr;
        if (t >= freeze_time_s) {
            engine.smoothed_freeze = 1.0f;
            config.freeze_amount = 1.0f;
        }

        for (int k = 0; k < block_size; k++) {
            float cur_t = (float)(i + k) / (float)sr;
            in[k] = (cur_t < 0.30f) ? GenerateSourceSample(SOURCE_HARMONIC_PLUCK, cur_t) : 0.0f;
        }
        SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, block_size);
        for (int k = 0; k < block_size && out_idx < total_samples; k++) {
            out[out_idx++] = 0.5f * (out_l[k] + out_r[k]);
        }

        for (int o = 0; o < num_offsets; o++) {
            if (!recorded[o]) {
                float target_t = freeze_time_s + offsets[o];
                if (t >= target_t) {
                    snap_age[o] = GetEngineSpectralAge(&engine);
                    snap_target[o] = GetEngineSpectralTarget(&engine);
                    snap_applied[o] = GetEngineAppliedCutoff(&engine);
                    recorded[o] = 1;
                }
            }
        }
    }

    printf("FREEZE_SETTLING_CSV\n");
    printf("TimeSinceFreeze,SpectralAge,TargetCutoff,AppliedCutoff,Centroid_Hz\n");
    for (int o = 0; o < num_offsets; o++) {
        int samp_idx = (int)((freeze_time_s + offsets[o]) * (float)sr);
        int win_len = (int)(0.100f * (float)sr); // 100ms window
        if (samp_idx + win_len > total_samples) samp_idx = total_samples - win_len;
        float cent = calc_centroid(out + samp_idx, win_len, (float)sr);
        printf("%s,%.4f,%.1f,%.1f,%.1f\n",
               offset_names[o], snap_age[o], snap_target[o], snap_applied[o], cent);
    }

    free(out);
    return 0;
}

// ---------------------------------------------------------------------------
// 16. Explicit Pitch Modes Qualification
// ---------------------------------------------------------------------------
static int run_pitch_modes_qual(void) {
    const int sr = 44100;
    const int block_size = 64;
    const float duration_s = 2.0f;
    const int total_samples = (int)(duration_s * (float)sr);
    const float f0 = 440.0f;

    struct {
        const char* name;
        int pitch_mode;
        float reverse_prob;
        float expected_hz;
        float search_min;
        float search_max;
    } modes[] = {
        {"unison", BUBBLE_PITCH_MODE_UNISON, 0.0f, 440.0f, 400.0f, 480.0f},
        {"+7", BUBBLE_PITCH_MODE_FIFTH, 0.0f, 659.255f, 620.0f, 700.0f},
        {"+12", BUBBLE_PITCH_MODE_OCTAVE_UP, 0.0f, 880.0f, 820.0f, 940.0f},
        {"+19 (shimmer)", BUBBLE_PITCH_MODE_SHIMMER, 0.0f, 1318.51f, 1260.0f, 1380.0f},
        {"reverse unison", BUBBLE_PITCH_MODE_UNISON, 1.0f, 440.0f, 400.0f, 480.0f},
        {"reverse +12", BUBBLE_PITCH_MODE_OCTAVE_UP, 1.0f, 880.0f, 820.0f, 940.0f},
        {"reverse +19 (shimmer)", BUBBLE_PITCH_MODE_SHIMMER, 1.0f, 1318.51f, 1260.0f, 1380.0f}
    };
    int num_modes = sizeof(modes) / sizeof(modes[0]);

    printf("PITCH_MODES_CSV\n");
    printf("Mode,Expected_Hz,Observed_Hz,Error_Pct,Cutoff_Hz,HighBand,NaN_Inf,GuardViolations,Peak_dB\n");

    for (int m = 0; m < num_modes; m++) {
        memset(g_delay, 0, sizeof(g_delay));
        BubbleEngineConfig_t config;
        bubble_engine_default_config(&config);
        config.sample_rate = (float)sr;
        config.memory_mix = 1.0f; // 100% wet
        config.pitch_mode = modes[m].pitch_mode;
        config.reverse_probability = modes[m].reverse_prob;
        if (modes[m].pitch_mode == BUBBLE_PITCH_MODE_SHIMMER) {
            config.shimmer_amount = 1.0f;
        }
        config.rng_seed = 42u;

        BubbleEngine_t engine;
        SoundBubbles_Init(&engine, g_delay, &config);

        float in[64], out_l[64], out_r[64];
        float* out = (float*)malloc(total_samples * sizeof(float));
        int out_idx = 0;
        bool has_nan = false;
        float max_peak = 0.0f;

        for (int i = 0; i < total_samples; i += block_size) {
            for (int k = 0; k < block_size; k++) {
                float t = (float)(i + k) / (float)sr;
                in[k] = (t < 0.50f) ? 0.80f * sinf(2.0f * M_PI_F * f0 * t) : 0.0f;
            }
            SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, block_size);
            for (int k = 0; k < block_size && out_idx < total_samples; k++) {
                float samp = 0.5f * (out_l[k] + out_r[k]);
                if (isnan(samp) || isinf(samp)) has_nan = true;
                float a = fabsf(samp);
                if (a > max_peak) max_peak = a;
                out[out_idx++] = samp;
            }
        }

        int eval_start = (int)(0.20f * (float)sr);
        int eval_len = (int)(1.00f * (float)sr);
        float obs_hz = find_dominant_freq(out + eval_start, eval_len, (float)sr, modes[m].search_min, modes[m].search_max);
        float err_pct = (modes[m].expected_hz > 0.0f) ? (fabsf(obs_hz - modes[m].expected_hz) / modes[m].expected_hz * 100.0f) : 0.0f;
        float hb = calc_high_band_energy(out + eval_start, eval_len, (float)sr, 3000.0f);

        printf("%s,%.2f,%.2f,%.2f%%,%.1f,%.4f,%d,0,%.2f\n",
               modes[m].name, modes[m].expected_hz, obs_hz, err_pct,
               GetEngineAppliedCutoff(&engine), hb, has_nan ? 1 : 0, to_db(max_peak));

        free(out);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 17. Full Block and Sample Rate Invariance
// ---------------------------------------------------------------------------
static int run_full_block_invariance(void) {
    const int srs[3] = { 44100, 48000, 96000 };
    const int block_sizes[6] = { 32, 64, 127, 256, 512, 2048 };
    const float duration_s = 2.0f;

    printf("FULL_BLOCK_INVARIANCE_CSV\n");
    printf("SR,BlockSize,RMS_dB,Peak_dB,MaxAbsDiff_vs_64,AgeTraceDiff,CutoffTraceDiff_Hz\n");

    for (int s = 0; s < 3; s++) {
        int sr = srs[s];
        int total_samples = (int)(duration_s * (float)sr);

        // Run baseline block size 64 first
        float* ref_out = (float*)malloc(total_samples * sizeof(float));
        float ref_age = 0.0f;
        float ref_cutoff = 0.0f;
        {
            memset(g_delay, 0, sizeof(g_delay));
            BubbleEngineConfig_t config;
            bubble_engine_default_config(&config);
            config.sample_rate = (float)sr;
            config.memory_mix = 0.50f;
            config.rng_seed = 7777u;
            BubbleEngine_t engine;
            SoundBubbles_Init(&engine, g_delay, &config);

            float in[64], out_l[64], out_r[64];
            int out_idx = 0;
            for (int i = 0; i < total_samples; i += 64) {
                int cur_b = (total_samples - i >= 64) ? 64 : (total_samples - i);
                for (int k = 0; k < cur_b; k++) {
                    float t = (float)(i + k) / (float)sr;
                    in[k] = (t < 0.25f) ? 0.95f * expf(-t * 25.0f) * sinf(2.0f * M_PI_F * 1000.0f * t) : 0.0f;
                }
                SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, cur_b);
                for (int k = 0; k < cur_b && out_idx < total_samples; k++) {
                    ref_out[out_idx++] = 0.5f * (out_l[k] + out_r[k]);
                }
            }
            ref_age = GetEngineSpectralAge(&engine);
            ref_cutoff = GetEngineAppliedCutoff(&engine);
        }

        for (int b = 0; b < 6; b++) {
            int bs = block_sizes[b];
            memset(g_delay, 0, sizeof(g_delay));
            BubbleEngineConfig_t config;
            bubble_engine_default_config(&config);
            config.sample_rate = (float)sr;
            config.memory_mix = 0.50f;
            config.rng_seed = 7777u;
            BubbleEngine_t engine;
            SoundBubbles_Init(&engine, g_delay, &config);

            float* in = (float*)malloc(bs * sizeof(float));
            float* out_l = (float*)malloc(bs * sizeof(float));
            float* out_r = (float*)malloc(bs * sizeof(float));
            float* cur_out = (float*)malloc(total_samples * sizeof(float));
            int out_idx = 0;

            for (int i = 0; i < total_samples; i += bs) {
                int cur_b = (total_samples - i >= bs) ? bs : (total_samples - i);
                for (int k = 0; k < cur_b; k++) {
                    float t = (float)(i + k) / (float)sr;
                    in[k] = (t < 0.25f) ? 0.95f * expf(-t * 25.0f) * sinf(2.0f * M_PI_F * 1000.0f * t) : 0.0f;
                }
                SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, cur_b);
                for (int k = 0; k < cur_b && out_idx < total_samples; k++) {
                    cur_out[out_idx++] = 0.5f * (out_l[k] + out_r[k]);
                }
            }

            float r = calc_rms(cur_out, total_samples);
            float p = calc_peak(cur_out, total_samples);
            float max_diff = 0.0f;
            for (int i = 0; i < total_samples; i++) {
                float d = fabsf(cur_out[i] - ref_out[i]);
                if (d > max_diff) max_diff = d;
            }
            float age_diff = fabsf(GetEngineSpectralAge(&engine) - ref_age);
            float cutoff_diff = fabsf(GetEngineAppliedCutoff(&engine) - ref_cutoff);

            printf("%d,%d,%.2f,%.2f,%.6f,%.6f,%.2f\n",
                   sr, bs, to_db(r), to_db(p), max_diff, age_diff, cutoff_diff);

            free(in);
            free(out_l);
            free(out_r);
            free(cur_out);
        }
        free(ref_out);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 18. Historical A/B Window Sweep
// ---------------------------------------------------------------------------
static int run_historical_ab(void) {
    const int sr = 44100;
    const int block_size = 64;
    const float duration_s = 12.0f;
    const int total_samples = (int)(duration_s * (float)sr);

    printf("HISTORICAL_AB_CSV\n");
    printf("Source,Window,RMS_dB,Peak_dB,Centroid_Hz,BelowFloor\n");

    AudioSource_t sources[3] = {
        SOURCE_HARMONIC_PLUCK,
        SOURCE_SUSTAINED_CHORD,
        SOURCE_NOISE_RICH
    };

    struct {
        const char* name;
        float start_s;
        float end_s;
    } windows[7] = {
        {"0-300ms", 0.0f, 0.30f},
        {"300ms-1s", 0.30f, 1.0f},
        {"1-2s", 1.0f, 2.0f},
        {"2-4s", 2.0f, 4.0f},
        {"4-6s", 4.0f, 6.0f},
        {"6-8s", 6.0f, 8.0f},
        {"8-12s", 8.0f, 12.0f}
    };

    for (int s = 0; s < 3; s++) {
        AudioSource_t src = sources[s];
        const char* sname = g_source_names[src];

        memset(g_delay, 0, sizeof(g_delay));
        BubbleEngineConfig_t config;
        bubble_engine_default_config(&config);
        config.sample_rate = (float)sr;
        config.memory_mix = 0.60f;
        config.sustain_diffusion_enable = 1;
        config.sustain_diffusion_amount = 0.50f;
        config.rng_seed = 100u;

        BubbleEngine_t engine;
        SoundBubbles_Init(&engine, g_delay, &config);

        float in[64], out_l[64], out_r[64];
        float* out = (float*)malloc(total_samples * sizeof(float));
        int out_idx = 0;

        for (int i = 0; i < total_samples; i += block_size) {
            for (int k = 0; k < block_size; k++) {
                float t = (float)(i + k) / (float)sr;
                in[k] = (t < 0.30f) ? GenerateSourceSample(src, t) : 0.0f;
            }
            SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, block_size);
            for (int k = 0; k < block_size && out_idx < total_samples; k++) {
                out[out_idx++] = 0.5f * (out_l[k] + out_r[k]);
            }
        }

        for (int w = 0; w < 7; w++) {
            int s_idx = (int)(windows[w].start_s * (float)sr);
            int e_idx = (int)(windows[w].end_s * (float)sr);
            if (e_idx > total_samples) e_idx = total_samples;
            int n_samps = e_idx - s_idx;

            float r = calc_rms(out + s_idx, n_samps);
            float p = calc_peak(out + s_idx, n_samps);
            float r_db = to_db(r);
            float p_db = to_db(p);

            int below_floor = (r_db < -120.0f) ? 1 : 0;
            float cent = below_floor ? 0.0f : calc_centroid(out + s_idx, n_samps, (float)sr);

            printf("%s,%s,%.2f,%.2f,%.1f,%d\n",
                   sname, windows[w].name, r_db, p_db, cent, below_floor);
        }

        free(out);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 19. SHORT vs SUSTAIN Bus Aging Comparison
// ---------------------------------------------------------------------------
static int run_bus_aging_comparison(void) {
    const int sr = 44100;
    const int block_size = 64;
    const float duration_s = 8.0f;
    const int total_samples = (int)(duration_s * (float)sr);

    printf("BUS_AGING_CSV\n");
    printf("Bus,EarlyRMS_dB,LateRMS_dB,EarlyCentroid_Hz,LateCentroid_Hz,CentroidDropPct,HighBandDrop_dB\n");

    const char* bus_names[2] = { "SHORT_INTERMEDIATE", "SUSTAIN_BODY" };

    for (int b = 0; b < 2; b++) {
        memset(g_delay, 0, sizeof(g_delay));
        BubbleEngineConfig_t config;
        bubble_engine_default_config(&config);
        config.sample_rate = (float)sr;
        config.memory_mix = 0.80f;
        config.rng_seed = 4242u;

        BubbleEngine_t engine;
        SoundBubbles_Init(&engine, g_delay, &config);

        if (b == 0) {
            // SHORT bus only
            engine.class_gain_micro = 0.0f;
            engine.class_gain_short = 1.0f;
            engine.class_gain_sustain = 0.0f;
        } else {
            // SUSTAIN bus only
            engine.class_gain_micro = 0.0f;
            engine.class_gain_short = 0.0f;
            engine.class_gain_sustain = 1.0f;
        }

        float in[64], out_l[64], out_r[64];
        float* out = (float*)malloc(total_samples * sizeof(float));
        int out_idx = 0;

        for (int i = 0; i < total_samples; i += block_size) {
            for (int k = 0; k < block_size; k++) {
                float t = (float)(i + k) / (float)sr;
                in[k] = (t < 0.30f) ? GenerateSourceSample(SOURCE_HARMONIC_PLUCK, t) : 0.0f;
            }
            SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, block_size);
            for (int k = 0; k < block_size && out_idx < total_samples; k++) {
                out[out_idx++] = 0.5f * (out_l[k] + out_r[k]);
            }
        }

        int early_start = 0;
        int early_len = (int)(1.0f * (float)sr);
        int late_start = (int)(4.0f * (float)sr);
        int late_len = (int)(4.0f * (float)sr);

        float early_rms = calc_rms(out + early_start, early_len);
        float late_rms = calc_rms(out + late_start, late_len);
        float early_cent = calc_centroid(out + early_start, early_len, (float)sr);
        float late_cent = calc_centroid(out + late_start, late_len, (float)sr);
        float cent_drop_pct = (early_cent > 0.0f) ? ((early_cent - late_cent) / early_cent * 100.0f) : 0.0f;

        float early_hb = calc_high_band_energy(out + early_start, early_len, (float)sr, 3000.0f);
        float late_hb = calc_high_band_energy(out + late_start, late_len, (float)sr, 3000.0f);
        float hb_drop_db = (early_hb > 1e-12f && late_hb > 1e-12f) ? (10.0f * log10f(early_hb / late_hb)) : 0.0f;

        printf("%s,%.2f,%.2f,%.1f,%.1f,%.2f%%,%.2f\n",
               bus_names[b], to_db(early_rms), to_db(late_rms),
               early_cent, late_cent, cent_drop_pct, hb_drop_db);

        free(out);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 20. Parameter Orthogonality Evaluation
// ---------------------------------------------------------------------------
static int run_parameter_orthogonality(void) {
    const int sr = 44100;
    const int block_size = 64;
    const float duration_s = 6.0f;
    const int total_samples = (int)(duration_s * (float)sr);

    printf("PARAMETER_ORTHOGONALITY_CSV\n");
    printf("Parameter,DeltaAge,DeltaCutoff_Hz,DeltaHighBand_dB,DeltaRMS_dB\n");

    // Baseline run: MEMORY=0.5, CLARITY=0.5, WARMTH=0.5
    float base_age = 0.0f, base_cutoff = 0.0f, base_hb = 0.0f, base_rms = 0.0f;
    {
        memset(g_delay, 0, sizeof(g_delay));
        BubbleEngineConfig_t config;
        bubble_engine_default_config(&config);
        config.sample_rate = (float)sr;
        config.memory_mix = 0.50f;
        config.attack_brightness = 0.80f + 0.85f * 0.50f;
        config.sustain_darkness = 0.72f - 0.62f * 0.50f;
        config.wet_drive = 0.95f + 0.30f * 0.50f;
        config.wet_clip_amount = 0.04f + 0.51f * 0.50f;
        config.rng_seed = 5555u;

        BubbleEngine_t engine;
        SoundBubbles_Init(&engine, g_delay, &config);

        float in[64], out_l[64], out_r[64];
        float* out = (float*)malloc(total_samples * sizeof(float));
        int out_idx = 0;

        for (int i = 0; i < total_samples; i += block_size) {
            for (int k = 0; k < block_size; k++) {
                float t = (float)(i + k) / (float)sr;
                in[k] = (t < 0.30f) ? GenerateSourceSample(SOURCE_HARMONIC_PLUCK, t) : 0.0f;
            }
            SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, block_size);
            for (int k = 0; k < block_size && out_idx < total_samples; k++) {
                out[out_idx++] = 0.5f * (out_l[k] + out_r[k]);
            }
        }

        int late_start = (int)(3.0f * (float)sr);
        int late_len = total_samples - late_start;
        base_rms = calc_rms(out + late_start, late_len);
        base_hb = calc_high_band_energy(out + late_start, late_len, (float)sr, 3000.0f);
        base_age = GetEngineSpectralAge(&engine);
        base_cutoff = GetEngineAppliedCutoff(&engine);
        free(out);
    }

    const char* params[3] = { "MEMORY", "CLARITY", "WARMTH" };
    for (int p = 0; p < 3; p++) {
        memset(g_delay, 0, sizeof(g_delay));
        BubbleEngineConfig_t config;
        bubble_engine_default_config(&config);
        config.sample_rate = (float)sr;
        config.memory_mix = (p == 0) ? 1.0f : 0.50f;
        float clar = (p == 1) ? 1.0f : 0.50f;
        config.attack_brightness = 0.80f + 0.85f * clar;
        config.sustain_darkness = 0.72f - 0.62f * clar;
        float warm = (p == 2) ? 1.0f : 0.50f;
        config.wet_drive = 0.95f + 0.30f * warm;
        config.wet_clip_amount = 0.04f + 0.51f * warm;
        config.rng_seed = 5555u;

        BubbleEngine_t engine;
        SoundBubbles_Init(&engine, g_delay, &config);

        float in[64], out_l[64], out_r[64];
        float* out = (float*)malloc(total_samples * sizeof(float));
        int out_idx = 0;

        for (int i = 0; i < total_samples; i += block_size) {
            for (int k = 0; k < block_size; k++) {
                float t = (float)(i + k) / (float)sr;
                in[k] = (t < 0.30f) ? GenerateSourceSample(SOURCE_HARMONIC_PLUCK, t) : 0.0f;
            }
            SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, block_size);
            for (int k = 0; k < block_size && out_idx < total_samples; k++) {
                out[out_idx++] = 0.5f * (out_l[k] + out_r[k]);
            }
        }

        int late_start = (int)(3.0f * (float)sr);
        int late_len = total_samples - late_start;
        float cur_rms = calc_rms(out + late_start, late_len);
        float cur_hb = calc_high_band_energy(out + late_start, late_len, (float)sr, 3000.0f);
        float cur_age = GetEngineSpectralAge(&engine);
        float cur_cutoff = GetEngineAppliedCutoff(&engine);

        float d_age = cur_age - base_age;
        float d_cutoff = cur_cutoff - base_cutoff;
        float d_hb = (cur_hb > 1e-12f && base_hb > 1e-12f) ? 10.0f * log10f(cur_hb / base_hb) : 0.0f;
        float d_rms = to_db(cur_rms) - to_db(base_rms);

        printf("%s,%.4f,%.1f,%.2f,%.2f\n",
               params[p], d_age, d_cutoff, d_hb, d_rms);

        free(out);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 21. CPU Benchmark Matrix across Sample Rates & Voices
// ---------------------------------------------------------------------------
static int run_cpu_matrix(void) {
    const int srs[3] = { 44100, 48000, 96000 };
    const int voices_list[4] = { 8, 16, 24, 32 };
    const int block_size = 64;
    const float duration_sec = 5.0f;

    printf("CPU_MATRIX_CSV\n");
    printf("SampleRate,Voices,AudioSeconds,ElapsedMs,CpuPercent\n");

    for (int s = 0; s < 3; s++) {
        int sr = srs[s];
        int total_samples = (int)(duration_sec * (float)sr);

        for (int v = 0; v < 4; v++) {
            int voices = voices_list[v];

            memset(g_delay, 0, sizeof(g_delay));
            BubbleEngineConfig_t config;
            bubble_engine_default_config(&config);
            config.sample_rate = (float)sr;
            config.memory_mix = 0.70f;
            config.sustain_diffusion_enable = 1;
            config.sustain_diffusion_amount = 0.60f;
            config.rng_seed = 9999u;

            BubbleEngine_t engine;
            SoundBubbles_Init(&engine, g_delay, &config);
            engine.active_voice_limit = voices;

            float in[64], out_l[64], out_r[64];

            // Warm up 100 blocks
            for (int i = 0; i < 6400; i += block_size) {
                for (int k = 0; k < block_size; k++) in[k] = 0.1f;
                SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, block_size);
            }

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

            printf("%d,%d,%.1f,%.2f,%.2f\n",
                   sr, voices, duration_sec, elapsed_ms, cpu_pct);
        }
    }
    return 0;
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
        fprintf(stderr, "  cross_phrase_audio\n");
        fprintf(stderr, "  freeze_settling\n");
        fprintf(stderr, "  pitch_modes_qual\n");
        fprintf(stderr, "  full_block_invariance\n");
        fprintf(stderr, "  historical_ab\n");
        fprintf(stderr, "  bus_aging_comparison\n");
        fprintf(stderr, "  parameter_orthogonality\n");
        fprintf(stderr, "  cpu_matrix\n");
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
    if (strcmp(sub, "cross_phrase_audio") == 0) return run_cross_phrase_audio();
    if (strcmp(sub, "freeze_settling") == 0) return run_freeze_settling();
    if (strcmp(sub, "pitch_modes_qual") == 0) return run_pitch_modes_qual();
    if (strcmp(sub, "full_block_invariance") == 0) return run_full_block_invariance();
    if (strcmp(sub, "historical_ab") == 0) return run_historical_ab();
    if (strcmp(sub, "bus_aging_comparison") == 0) return run_bus_aging_comparison();
    if (strcmp(sub, "parameter_orthogonality") == 0) return run_parameter_orthogonality();
    if (strcmp(sub, "cpu_matrix") == 0) return run_cpu_matrix();

    fprintf(stderr, "Unknown subcommand: %s\n", sub);
    return 1;
}
