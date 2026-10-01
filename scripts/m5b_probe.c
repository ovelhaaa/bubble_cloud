// M5B Sparse Late-Tail Diffusion Probe
// Comprehensive qualification harness for M5B vs frozen baseline M5A.1 (6334512784d91e2fb27617ceef3f024bf95b7b3d)

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
    // Hann window
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

// Spectral Flatness: Geometric mean of power / Arithmetic mean of power
static float calc_spectral_flatness(const float* x, int n) {
    if (n <= 1) return 0.0f;
    int nfft = 4096;
    if (n < 4096) {
        nfft = 128;
        while (nfft < n) nfft <<= 1;
    }

    float* mag = (float*)malloc(((nfft >> 1) + 1) * sizeof(float));
    if (!mag) return 0.0f;
    compute_real_fft_mag(x, n, nfft, mag);

    int half_fft = nfft >> 1;
    double log_sum = 0.0;
    double pow_sum = 0.0;
    int bins = 0;

    for (int k = 1; k <= half_fft; k++) {
        double p = (double)mag[k] * (double)mag[k] + 1e-12;
        log_sum += log(p);
        pow_sum += p;
        bins++;
    }
    free(mag);
    if (bins == 0 || pow_sum <= 1e-12) return 0.0f;

    double geom = exp(log_sum / (double)bins);
    double arith = pow_sum / (double)bins;
    return (arith > 1e-12) ? (float)(geom / arith) : 0.0f;
}

// Spectral Occupancy: Percentage of bins above -40 dB relative to peak bin
static float calc_spectral_occupancy(const float* x, int n) {
    if (n <= 1) return 0.0f;
    int nfft = 4096;
    if (n < 4096) {
        nfft = 128;
        while (nfft < n) nfft <<= 1;
    }

    float* mag = (float*)malloc(((nfft >> 1) + 1) * sizeof(float));
    if (!mag) return 0.0f;
    compute_real_fft_mag(x, n, nfft, mag);

    int half_fft = nfft >> 1;
    float peak_mag = 0.0f;
    for (int k = 1; k <= half_fft; k++) {
        if (mag[k] > peak_mag) peak_mag = mag[k];
    }
    if (peak_mag <= 1e-9f) {
        free(mag);
        return 0.0f;
    }

    float th = peak_mag * 0.010f; // -40 dB threshold
    int active_bins = 0;
    int total_bins = half_fft;

    for (int k = 1; k <= half_fft; k++) {
        if (mag[k] >= th) active_bins++;
    }
    free(mag);
    return 100.0f * (float)active_bins / (float)total_bins;
}

// Temporal Sparsity: % of 20 ms sub-windows with RMS below threshold
static float calc_temporal_sparsity(const float* x, int n, float sr) {
    if (n <= 0 || sr <= 0.0f) return 100.0f;
    int frame_len = (int)(0.020f * sr); // 20 ms
    if (frame_len < 16) frame_len = 16;
    int frames = n / frame_len;
    if (frames <= 0) return 100.0f;

    float max_sub_rms = 0.0f;
    float* frame_rms = (float*)malloc(frames * sizeof(float));
    for (int f = 0; f < frames; f++) {
        float r = calc_rms(x + f * frame_len, frame_len);
        frame_rms[f] = r;
        if (r > max_sub_rms) max_sub_rms = r;
    }

    float threshold = fmaxf(1e-4f, max_sub_rms * 0.0316f); // -30 dB relative to local peak
    int quiet_frames = 0;
    for (int f = 0; f < frames; f++) {
        if (frame_rms[f] < threshold) quiet_frames++;
    }
    free(frame_rms);
    return 100.0f * (float)quiet_frames / (float)frames;
}

// Normalized cross-correlation
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

// Autocorrelation at lag
static float calc_autocorrelation_at_lag(const float* buffer, int total_samples, int lag_samples, int window_samples) {
    if (lag_samples + window_samples > total_samples) return 0.0f;
    const float* x = buffer;
    const float* y = buffer + lag_samples;
    return calc_cross_correlation(x, y, window_samples);
}

// ---------------------------------------------------------------------------
// Audio Sources
// ---------------------------------------------------------------------------
typedef enum {
    SOURCE_HARMONIC_PLUCK = 0,
    SOURCE_HARP_TRANSIENT,
    SOURCE_PERCUSSIVE_PULSE,
    SOURCE_TONAL_ONSET,
    SOURCE_SUSTAINED_CHORD,
    SOURCE_NOISE_RICH,
    SOURCE_COUNT
} AudioSource_t;

static const char* g_source_names[] = {
    "harmonic_pluck",
    "harp_transient",
    "percussive_pulse",
    "tonal_onset",
    "sustained_chord",
    "noise_rich"
};

static inline float GenerateSourceSample(AudioSource_t src, float t) {
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
        case SOURCE_TONAL_ONSET:
            if (t < 0.30f) {
                return 0.85f * (1.0f - expf(-t * 40.0f)) * (0.7f * sinf(2.0f * M_PI_F * 440.0f * t) +
                                                             0.3f * sinf(2.0f * M_PI_F * 880.0f * t));
            } else {
                return 0.85f * expf(-(t - 0.30f) * 20.0f) * (0.7f * sinf(2.0f * M_PI_F * 440.0f * t) +
                                                              0.3f * sinf(2.0f * M_PI_F * 880.0f * t));
            }
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
                // Deterministic pseudo-noise
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
                fwrite(out, sizeof(float), total_samples, f);
                fclose(f);
            }
        }

        struct {
            const char* name;
            int start;
            int len;
        } win_defs[] = {
            {"0-50ms", 0, (int)(0.050f * sr)},
            {"50-100ms", (int)(0.050f * sr), (int)(0.050f * sr)},
            {"100-200ms", (int)(0.100f * sr), (int)(0.100f * sr)},
            {"200-300ms", (int)(0.200f * sr), (int)(0.100f * sr)},
            {"300-500ms", (int)(0.300f * sr), (int)(0.200f * sr)},
            {"0-100ms", 0, (int)(0.100f * sr)},
            {"100-300ms", (int)(0.100f * sr), (int)(0.200f * sr)},
            {"0-300ms", 0, (int)(0.300f * sr)}
        };

        for (size_t w = 0; w < sizeof(win_defs)/sizeof(win_defs[0]); w++) {
            float rms = calc_rms(out + win_defs[w].start, win_defs[w].len);
            float peak = calc_peak(out + win_defs[w].start, win_defs[w].len);
            float centroid = calc_centroid(out + win_defs[w].start, win_defs[w].len, (float)sr);
            printf("%s,%s,%.2f,%.2f,%.1f\n", src_name, win_defs[w].name, to_db(rms), to_db(peak), centroid);
        }

        free(out);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 2. Transition Discontinuity (~230-420 ms)
// ---------------------------------------------------------------------------
static int run_transition_discontinuity(void) {
    const int sr = 44100;
    const int total_samples = (int)(0.600f * (float)sr);
    const int block_size = 64;

    printf("TRANSITION_DISCONTINUITY_CSV\n");
    printf("Source,Region,MaxDiff,RMSDiff,MedianDiff,PeakToMedian\n");

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

        struct {
            const char* name;
            int start;
            int len;
        } regions[] = {
            {"150-230ms", (int)(0.150f * sr), (int)(0.080f * sr)},
            {"230-420ms", (int)(0.230f * sr), (int)(0.190f * sr)},
            {"420-600ms", (int)(0.420f * sr), (int)(0.180f * sr)}
        };

        for (size_t r = 0; r < 3; r++) {
            int st = regions[r].start;
            int len = regions[r].len;
            float* diffs = (float*)malloc(len * sizeof(float));
            float max_diff = 0.0f;
            double sum_sq = 0.0;
            for (int j = 0; j < len; j++) {
                int idx = st + j;
                float d = (idx > 0) ? fabsf(out[idx] - out[idx - 1]) : 0.0f;
                diffs[j] = d;
                if (d > max_diff) max_diff = d;
                sum_sq += (double)d * (double)d;
            }
            float rms_diff = (float)sqrt(sum_sq / (double)len);

            for (int j = 0; j < len - 1; j++) {
                int min_idx = j;
                for (int k = j + 1; k < len; k++) {
                    if (diffs[k] < diffs[min_idx]) min_idx = k;
                }
                float tmp = diffs[j]; diffs[j] = diffs[min_idx]; diffs[min_idx] = tmp;
            }
            float median_diff = diffs[len / 2];
            float peak_to_median = (median_diff > 1e-9f) ? (max_diff / median_diff) : 0.0f;

            printf("%s,%s,%.6f,%.6f,%.6f,%.2f\n",
                   src_name, regions[r].name, max_diff, rms_diff, median_diff, peak_to_median);

            free(diffs);
        }
        free(out);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 3. Late-Tail Evolution (0-1, 1-2, 2-4, 4-6, 6-8, 8-12, 12-16 s)
// ---------------------------------------------------------------------------
static int run_tail_evolution(void) {
    const int sr = 44100;
    const int total_samples = 16 * sr;
    const int block_size = 64;

    printf("TAIL_EVOLUTION_CSV\n");
    printf("Source,Window,RMS_dB,Peak_dB,Centroid_Hz,Flatness,OccupancyPct,CrestFactor,SparsityPct,StereoCorr\n");

    for (int s = 0; s < SOURCE_COUNT; s++) {
        AudioSource_t src = (AudioSource_t)s;
        const char* src_name = g_source_names[s];

        BubbleEngineConfig_t config;
        bubble_engine_default_config(&config);
        config.sample_rate = (float)sr;
        config.memory_mix = 0.75f;
        config.sustain_diffusion_enable = 1;
        config.sustain_diffusion_amount = 0.70f;
        config.rng_seed = 42u;

        BubbleEngine_t engine;
        SoundBubbles_Init(&engine, g_delay, &config);
        SoundBubbles_SetFeedbackEnabled(&engine, true);

        float in[64], out_l[64], out_r[64];
        float* buf_l = (float*)malloc(total_samples * sizeof(float));
        float* buf_r = (float*)malloc(total_samples * sizeof(float));
        float* buf_mono = (float*)malloc(total_samples * sizeof(float));
        int out_idx = 0;

        for (int i = 0; i < total_samples; i += block_size) {
            for (int k = 0; k < block_size; k++) {
                float t = (float)(i + k) / (float)sr;
                in[k] = (t < 0.50f) ? GenerateSourceSample(src, t) : 0.0f;
            }
            SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, block_size);
            for (int k = 0; k < block_size; k++) {
                if (out_idx < total_samples) {
                    buf_l[out_idx] = out_l[k];
                    buf_r[out_idx] = out_r[k];
                    buf_mono[out_idx] = 0.5f * (out_l[k] + out_r[k]);
                    out_idx++;
                }
            }
        }

        struct {
            const char* name;
            int start;
            int len;
        } win_defs[] = {
            {"0-1s",   (int)(0.0f * sr),  (int)(1.0f * sr)},
            {"1-2s",   (int)(1.0f * sr),  (int)(1.0f * sr)},
            {"2-4s",   (int)(2.0f * sr),  (int)(2.0f * sr)},
            {"4-6s",   (int)(4.0f * sr),  (int)(2.0f * sr)},
            {"6-8s",   (int)(6.0f * sr),  (int)(2.0f * sr)},
            {"8-12s",  (int)(8.0f * sr),  (int)(4.0f * sr)},
            {"12-16s", (int)(12.0f * sr), (int)(4.0f * sr)}
        };

        for (size_t w = 0; w < sizeof(win_defs)/sizeof(win_defs[0]); w++) {
            int st = win_defs[w].start;
            int len = win_defs[w].len;

            float rms = calc_rms(buf_mono + st, len);
            float peak = calc_peak(buf_mono + st, len);
            float cent = calc_centroid(buf_mono + st, len, (float)sr);
            float flat = calc_spectral_flatness(buf_mono + st, len);
            float occ = calc_spectral_occupancy(buf_mono + st, len);
            float crest = (rms > 1e-9f) ? (peak / rms) : 1.0f;
            float spar = calc_temporal_sparsity(buf_mono + st, len, (float)sr);
            float corr = calc_cross_correlation(buf_l + st, buf_r + st, len);

            printf("%s,%s,%.2f,%.2f,%.1f,%.4f,%.1f,%.2f,%.1f,%.3f\n",
                   src_name, win_defs[w].name, to_db(rms), to_db(peak), cent, flat, occ, crest, spar, corr);
        }

        free(buf_l);
        free(buf_r);
        free(buf_mono);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 4. Periodicity & Autocorrelation
// ---------------------------------------------------------------------------
static int run_periodicity(void) {
    BubbleEngineConfig_t config;
    bubble_engine_default_config(&config);
    config.sample_rate = 44100.0f;
    config.memory_mix = 0.80f;
    config.memory_pull = 0.50f;
    config.sustain_diffusion_enable = 1;
    config.sustain_diffusion_amount = 0.75f;
    config.rng_seed = 42u;

    BubbleEngine_t engine;
    SoundBubbles_Init(&engine, g_delay, &config);
    SoundBubbles_SetFeedbackEnabled(&engine, true);

    const int sr = 44100;
    const int total_samples = 12 * sr;
    const int block_size = 64;
    float in[64], out_l[64], out_r[64];

    float* buffer = (float*)malloc(total_samples * sizeof(float));
    int b_idx = 0;

    for (int i = 0; i < total_samples; i += block_size) {
        for (int k = 0; k < block_size; k++) {
            float t = (float)(i + k) / (float)sr;
            in[k] = (t < 0.5f) ? 0.85f * (sinf(2.0f * M_PI_F * 440.0f * t) + sinf(2.0f * M_PI_F * 660.0f * t)) : 0.0f;
        }
        SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, block_size);
        for (int k = 0; k < block_size; k++) {
            if (b_idx < total_samples) {
                buffer[b_idx++] = 0.5f * (out_l[k] + out_r[k]);
            }
        }
    }

    const int win_samples = 2 * sr;
    int lag_d0 = (int)lroundf(0.0473f * sr);
    int lag_d1 = (int)lroundf(0.1071f * sr);
    int lag_d2 = (int)lroundf(0.1819f * sr);

    float ac_d0 = calc_autocorrelation_at_lag(buffer + (int)(3.0f * sr), total_samples - (int)(3.0f * sr), lag_d0, win_samples);
    float ac_d1 = calc_autocorrelation_at_lag(buffer + (int)(3.0f * sr), total_samples - (int)(3.0f * sr), lag_d1, win_samples);
    float ac_d2 = calc_autocorrelation_at_lag(buffer + (int)(3.0f * sr), total_samples - (int)(3.0f * sr), lag_d2, win_samples);

    float ac_1_0s = calc_autocorrelation_at_lag(buffer, total_samples, 1 * sr, win_samples);
    float ac_1_5s = calc_autocorrelation_at_lag(buffer, total_samples, (int)(1.5f * sr), win_samples);
    float ac_2_0s = calc_autocorrelation_at_lag(buffer, total_samples, 2 * sr, win_samples);
    float ac_3_0s = calc_autocorrelation_at_lag(buffer, total_samples, 3 * sr, win_samples);
    float ac_4_0s = calc_autocorrelation_at_lag(buffer, total_samples, 4 * sr, win_samples);
    float ac_5_0s = calc_autocorrelation_at_lag(buffer, total_samples, 5 * sr, win_samples);
    float ac_6_0s = calc_autocorrelation_at_lag(buffer, total_samples, 6 * sr, win_samples);
    float ac_8_0s = calc_autocorrelation_at_lag(buffer, total_samples, 8 * sr, win_samples);

    printf("PERIODICITY_CSV\n");
    printf("AC_D0_47ms,AC_D1_107ms,AC_D2_182ms,Lag1_0s,Lag1_5s,Lag2_0s,Lag3_0s,Lag4_0s,Lag5_0s,Lag6_0s,Lag8_0s\n");
    printf("%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f\n",
           ac_d0, ac_d1, ac_d2, ac_1_0s, ac_1_5s, ac_2_0s, ac_3_0s, ac_4_0s, ac_5_0s, ac_6_0s, ac_8_0s);

    free(buffer);
    return 0;
}

// ---------------------------------------------------------------------------
// 5. Runaway Stress & Stability Test (60s and 120s)
// ---------------------------------------------------------------------------
static int run_runaway_stress(int duration_sec) {
    const int sr = 44100;
    const int total_samples = duration_sec * sr;
    const int block_size = 64;

    BubbleEngineConfig_t config;
    bubble_engine_default_config(&config);
    config.sample_rate = (float)sr;
    config.density_sustain = 50.0f;
    config.memory_mix = 1.0f;
    config.memory_pull = 1.0f;
    config.sustain_diffusion_enable = 1;
    config.sustain_diffusion_amount = 1.0f;
    config.wet_clip_amount = 0.90f; // High Warmth
    config.pitch_mode = BUBBLE_PITCH_MODE_SHIMMER;
    config.shimmer_amount = 0.85f;
    config.reverse_probability = 0.50f;
    config.rng_seed = 999u;

    BubbleEngine_t engine;
    SoundBubbles_Init(&engine, g_delay, &config);
    SoundBubbles_SetFeedbackEnabled(&engine, true);

    float in[64], out_l[64], out_r[64];
    int nan_inf_count = 0;
    float max_peak = 0.0f;
    double dc_sum_l = 0.0, dc_sum_r = 0.0;
    float max_diff_energy = 0.0f;
    float min_limiter_gain = 1.0f;

    for (int i = 0; i < total_samples; i += block_size) {
        for (int k = 0; k < block_size; k++) {
            float t = (float)(i + k) / (float)sr;
            in[k] = (t < 1.0f) ? 0.90f * (sinf(2.0f * M_PI_F * 440.0f * t) + sinf(2.0f * M_PI_F * 880.0f * t)) : 0.0f;
        }
        SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, block_size);

        for (int k = 0; k < block_size; k++) {
            if (!isfinite(out_l[k]) || !isfinite(out_r[k])) nan_inf_count++;
            float a_l = fabsf(out_l[k]);
            float a_r = fabsf(out_r[k]);
            if (a_l > max_peak) max_peak = a_l;
            if (a_r > max_peak) max_peak = a_r;
            dc_sum_l += (double)out_l[k];
            dc_sum_r += (double)out_r[k];
        }

#ifdef M5B_CANDIDATE_BUILD
        float e = SoundBubbles_GetLateDiffuserFeedbackEnergy(&engine);
        if (e > max_diff_energy) max_diff_energy = e;
#endif
        float lim_g = SoundBubbles_GetFinalLimiterGain(&engine);
        if (lim_g < min_limiter_gain) min_limiter_gain = lim_g;
    }

    float dc_l = (float)(dc_sum_l / (double)total_samples);
    float dc_r = (float)(dc_sum_r / (double)total_samples);
    float final_limiter_max_gr_db = (min_limiter_gain > 0.0f && min_limiter_gain < 1.0f) ? -20.0f * log10f(min_limiter_gain) : 0.0f;

    printf("RUNAWAY_STRESS: Duration=%ds NaN_Inf=%d MaxPeak=%.3f MaxDiffEnergy=%.4f DC_L=%.6f DC_R=%.6f FinalLimGR_dB=%.2f\n",
           duration_sec, nan_inf_count, max_peak, max_diff_energy, dc_l, dc_r, final_limiter_max_gr_db);

    return (nan_inf_count == 0 && max_peak < 1.5f && fabsf(dc_l) < 0.01f && fabsf(dc_r) < 0.01f) ? 0 : 1;
}

// ---------------------------------------------------------------------------
// 6. Metallic Resonance Guard (Impulse test)
// ---------------------------------------------------------------------------
static int run_metallic_resonance(void) {
    const int sr = 44100;
    const int total_samples = 14 * sr;
    const int block_size = 64;

    BubbleEngineConfig_t config;
    bubble_engine_default_config(&config);
    config.sample_rate = (float)sr;
    config.memory_mix = 0.85f;
    config.sustain_diffusion_enable = 1;
    config.sustain_diffusion_amount = 0.75f;
    config.rng_seed = 123u;

    BubbleEngine_t engine;
    SoundBubbles_Init(&engine, g_delay, &config);
    SoundBubbles_SetFeedbackEnabled(&engine, true);

    float in[64], out_l[64], out_r[64];
    float* out = (float*)malloc(total_samples * sizeof(float));
    int out_idx = 0;

    for (int i = 0; i < total_samples; i += block_size) {
        for (int k = 0; k < block_size; k++) {
            in[k] = (i + k == 0) ? 0.95f : 0.0f; // Single impulse
        }
        SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, block_size);
        for (int k = 0; k < block_size; k++) {
            if (out_idx < total_samples) {
                out[out_idx++] = 0.5f * (out_l[k] + out_r[k]);
            }
        }
    }

    // Analyze late tail segment: 8s to 14s (6 seconds)
    int seg_start = 8 * sr;
    int seg_len = 6 * sr;
    int nfft = 16384;
    float* mag = (float*)malloc(((nfft >> 1) + 1) * sizeof(float));
    compute_real_fft_mag(out + seg_start, seg_len, nfft, mag);

    int half_fft = nfft >> 1;
    float max_peak_to_local_median_db = 0.0f;
    int radius = 8; // Local neighborhood +-8 bins

    for (int k = radius; k <= half_fft - radius; k++) {
        float center_val = mag[k];
        if (center_val < 1e-6f) continue;

        // Collect neighborhood
        float neighbors[32];
        int n_cnt = 0;
        for (int j = -radius; j <= radius; j++) {
            if (j == 0) continue;
            neighbors[n_cnt++] = mag[k + j];
        }

        // Median of neighbors
        for (int a = 0; a < n_cnt - 1; a++) {
            int m_idx = a;
            for (int b = a + 1; b < n_cnt; b++) {
                if (neighbors[b] < neighbors[m_idx]) m_idx = b;
            }
            float t = neighbors[a]; neighbors[a] = neighbors[m_idx]; neighbors[m_idx] = t;
        }
        float med = neighbors[n_cnt / 2];
        if (med > 1e-7f) {
            float ratio_db = 20.0f * log10f(center_val / med);
            if (ratio_db > max_peak_to_local_median_db) {
                max_peak_to_local_median_db = ratio_db;
            }
        }
    }

    free(mag);
    free(out);

    printf("METALLIC_RESONANCE: MaxPeakToLocalMedian_dB=%.2f\n", max_peak_to_local_median_db);
    return (max_peak_to_local_median_db < 15.0f) ? 0 : 1;
}

// ---------------------------------------------------------------------------
// 7. Pitch Preservation Check
// ---------------------------------------------------------------------------
static int run_pitch_preservation(void) {
    const int sr = 44100;
    const int total_samples = 10 * sr;
    const int block_size = 64;

    BubbleEngineConfig_t config;
    bubble_engine_default_config(&config);
    config.sample_rate = (float)sr;
    config.memory_mix = 0.75f;
    config.sustain_diffusion_enable = 1;
    config.sustain_diffusion_amount = 0.70f;
    config.rng_seed = 42u;

    BubbleEngine_t engine;
    SoundBubbles_Init(&engine, g_delay, &config);
    SoundBubbles_SetFeedbackEnabled(&engine, true);

    float in[64], out_l[64], out_r[64];
    float* out = (float*)malloc(total_samples * sizeof(float));
    int out_idx = 0;

    for (int i = 0; i < total_samples; i += block_size) {
        for (int k = 0; k < block_size; k++) {
            float t = (float)(i + k) / (float)sr;
            in[k] = (t < 0.4f) ? 0.85f * sinf(2.0f * M_PI_F * 440.0f * t) : 0.0f;
        }
        SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, block_size);
        for (int k = 0; k < block_size; k++) {
            if (out_idx < total_samples) {
                out[out_idx++] = 0.5f * (out_l[k] + out_r[k]);
            }
        }
    }

    // FFT of late tail (4s to 8s)
    int seg_start = 4 * sr;
    int seg_len = 4 * sr;
    int nfft = 16384;
    float* mag = (float*)malloc(((nfft >> 1) + 1) * sizeof(float));
    compute_real_fft_mag(out + seg_start, seg_len, nfft, mag);

    int half_fft = nfft >> 1;
    float peak_mag = 0.0f;
    int peak_bin = 0;
    for (int k = 1; k <= half_fft; k++) {
        if (mag[k] > peak_mag) {
            peak_mag = mag[k];
            peak_bin = k;
        }
    }
    float peak_freq = (float)peak_bin * (sr / (float)nfft);
    float delta_hz = fabsf(peak_freq - 440.0f);

    free(mag);
    free(out);

    printf("PITCH_PRESERVATION: Target=440.0Hz Observed=%.1fHz DeltaHz=%.1f\n", peak_freq, delta_hz);
    return (delta_hz <= 10.0f) ? 0 : 1;
}

// ---------------------------------------------------------------------------
// 8. MEMORY & BLOOM Sweep
// ---------------------------------------------------------------------------
static int run_memory_bloom_sweep(void) {
    const float memory_mixes[] = {0.0f, 0.25f, 0.50f, 0.75f, 1.0f};
    const float blooms[] = {0.0f, 0.50f, 1.0f};

    printf("MEMORY_BLOOM_SWEEP_CSV\n");
    printf("Memory,Bloom,LateSend,LateReturnRMS_dB,TailRMS_dB\n");

    const int sr = 44100;
    const int total_samples = 12 * sr;
    const int block_size = 64;

    for (int m = 0; m < 5; m++) {
        for (int b = 0; b < 3; b++) {
            BubbleEngineConfig_t config;
            bubble_engine_default_config(&config);
            config.sample_rate = (float)sr;
            config.memory_mix = memory_mixes[m];
            config.sustain_diffusion_enable = 1;
            config.sustain_diffusion_amount = blooms[b];
            config.rng_seed = 42u;

            BubbleEngine_t engine;
            SoundBubbles_Init(&engine, g_delay, &config);
            SoundBubbles_SetFeedbackEnabled(&engine, true);

            float in[64], out_l[64], out_r[64];
            double tail_energy = 0.0;
            int tail_count = 0;

            for (int i = 0; i < total_samples; i += block_size) {
                float t = (float)i / (float)sr;
                for (int k = 0; k < block_size; k++) {
                    float cur_t = t + (float)k / (float)sr;
                    in[k] = (cur_t < 0.5f) ? 0.85f * sinf(2.0f * M_PI_F * 440.0f * cur_t) : 0.0f;
                }
                SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, block_size);

                if (t >= 6.0f) {
                    for (int k = 0; k < block_size; k++) {
                        float m_s = 0.5f * (out_l[k] + out_r[k]);
                        tail_energy += (double)m_s * (double)m_s;
                        tail_count++;
                    }
                }
            }

            float tail_rms = (tail_count > 0) ? (float)sqrt(tail_energy / (double)tail_count) : 0.0f;
            float send = 0.0f;
            float ret_rms = 0.0f;
#ifdef M5B_CANDIDATE_BUILD
            send = SoundBubbles_GetLateDiffuserSend(&engine);
            ret_rms = SoundBubbles_GetLateDiffuserReturnRms(&engine);
#endif
            printf("%.2f,%.2f,%.4f,%.2f,%.2f\n",
                   memory_mixes[m], blooms[b], send, to_db(ret_rms), to_db(tail_rms));
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 9. Silence Startup & Finite Decay
// ---------------------------------------------------------------------------
static int run_silence_startup(void) {
    const int sr = 44100;
    const int total_samples = 30 * sr;
    const int block_size = 64;

    BubbleEngineConfig_t config;
    bubble_engine_default_config(&config);
    config.sample_rate = (float)sr;
    config.memory_mix = 1.0f;
    config.sustain_diffusion_enable = 1;
    config.sustain_diffusion_amount = 1.0f;

    BubbleEngine_t engine;
    SoundBubbles_Init(&engine, g_delay, &config);
    SoundBubbles_SetFeedbackEnabled(&engine, true);

    float in[64] = {0};
    float out_l[64], out_r[64];
    float max_peak = 0.0f;

    for (int i = 0; i < total_samples; i += block_size) {
        SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, block_size);
        for (int k = 0; k < block_size; k++) {
            float al = fabsf(out_l[k]);
            float ar = fabsf(out_r[k]);
            if (al > max_peak) max_peak = al;
            if (ar > max_peak) max_peak = ar;
        }
    }

    printf("SILENCE_STARTUP: MaxPeak=%.9e\n", max_peak);
    return (max_peak < 1e-12f) ? 0 : 1;
}

static int run_tail_decay(void) {
    const int sr = 44100;
    const int total_samples = 35 * sr;
    const int block_size = 64;

    BubbleEngineConfig_t config;
    bubble_engine_default_config(&config);
    config.sample_rate = (float)sr;
    config.memory_mix = 0.80f;
    config.sustain_diffusion_enable = 1;
    config.sustain_diffusion_amount = 0.70f;
    config.rng_seed = 42u;

    BubbleEngine_t engine;
    SoundBubbles_Init(&engine, g_delay, &config);
    SoundBubbles_SetFeedbackEnabled(&engine, true);

    float in[64], out_l[64], out_r[64];
    double late_tail_energy = 0.0;
    int late_count = 0;

    for (int i = 0; i < total_samples; i += block_size) {
        float t = (float)i / (float)sr;
        for (int k = 0; k < block_size; k++) {
            float cur_t = t + (float)k / (float)sr;
            in[k] = (cur_t < 0.5f) ? 0.85f * sinf(2.0f * M_PI_F * 440.0f * cur_t) : 0.0f;
        }
        SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, block_size);

        if (t >= 30.0f) {
            for (int k = 0; k < block_size; k++) {
                float m = 0.5f * (out_l[k] + out_r[k]);
                late_tail_energy += (double)m * (double)m;
                late_count++;
            }
        }
    }

    float late_rms = (late_count > 0) ? (float)sqrt(late_tail_energy / (double)late_count) : 0.0f;
    printf("TAIL_DECAY: 30-35s RMS_dB=%.2f\n", to_db(late_rms));
    return (late_rms < 1e-4f) ? 0 : 1;
}

// ---------------------------------------------------------------------------
// 10. Block Invariance
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
        config.memory_mix = 0.70f;
        config.sustain_diffusion_enable = 1;
        config.sustain_diffusion_amount = 0.70f;
        config.rng_seed = 42u;

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
// 11. CPU Benchmark
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
        fprintf(stderr, "Usage: %s <attack_parity|transition_discontinuity|tail_evolution|periodicity|runaway_stress_60|runaway_stress_120|metallic_resonance|pitch_preservation|memory_bloom_sweep|silence_startup|tail_decay|block_invariance|cpu_benchmark>\n", argv[0]);
        return 1;
    }

    const char* mode = argv[1];
    if (strcmp(mode, "attack_parity") == 0) return run_attack_parity(argc, argv);
    if (strcmp(mode, "transition_discontinuity") == 0) return run_transition_discontinuity();
    if (strcmp(mode, "tail_evolution") == 0) return run_tail_evolution();
    if (strcmp(mode, "periodicity") == 0) return run_periodicity();
    if (strcmp(mode, "runaway_stress_60") == 0) return run_runaway_stress(60);
    if (strcmp(mode, "runaway_stress_120") == 0) return run_runaway_stress(120);
    if (strcmp(mode, "metallic_resonance") == 0) return run_metallic_resonance();
    if (strcmp(mode, "pitch_preservation") == 0) return run_pitch_preservation();
    if (strcmp(mode, "memory_bloom_sweep") == 0) return run_memory_bloom_sweep();
    if (strcmp(mode, "silence_startup") == 0) return run_silence_startup();
    if (strcmp(mode, "tail_decay") == 0) return run_tail_decay();
    if (strcmp(mode, "block_invariance") == 0) return run_block_invariance();
    if (strcmp(mode, "cpu_benchmark") == 0) return run_cpu_benchmark();

    fprintf(stderr, "Unknown mode: %s\n", mode);
    return 1;
}
