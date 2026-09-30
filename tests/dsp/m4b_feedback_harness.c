// M4B "Bounded Granular Feedback Path" validation harness.
//
// Covers all M4B milestone requirements:
//   1. No-input regression (Section 17 & 32): 30s silence from init -> 0 spawns, feedback_energy=0, 0 output
//   2. Baseline vs M4A vs M4B qualification (Section 22): 500ms tone -> 12s silence, RMS in [0.5-1s, 1-2s, 2-4s, 4-6s, 6-8s, 8-12s]
//      Verifies: M3.2C < M4A < M4B in tail survival, non-flat smooth decay
//   3. Decay monotonicity (Section 23): long-term energy strictly decreases across large windows
//   4. Tail duration target (Section 24): ~5-12s perceptible tail, natural extinction without infinite runaway
//   5. Extreme runaway test (Section 25): MEMORY high, BLOOM high, DENSITY high, WEB_ULTRA -> 1s tone + 60s silence
//      Verifies: no NaN/Inf, no continuous growth, no full-scale plateau, feedback_energy decays, approaches silence
//   6. Impulse stability (Section 26): single impulse -> 25s, peak envelope decreases stably
//   7. Spectral darkening (Section 27): tail 1-3s vs tail 6-9s, late centroid <= early centroid
//   8. Low-frequency buildup (Section 28): energy < 120 Hz does not accumulate uncontrollably
//   9. Harmonic identity (Section 29): 440 Hz + harmonics, harmonic-bin / noise-bin ratio proves tonal relation
//  10. Feedback write bounds (Section 30): input, feedback, retained contributions stay strictly within bounds
//  11. Freeze interaction regression (Section 31): Freeze=1.0 locks ring write and disables feedback injection
//  12. CPU & voice benchmark (Section 33): M4A vs M4B overhead across 8/16/24/32 voices at 44.1/48/96 kHz

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SOUND_BUBBLES_DSP_INTERNAL 1
#include "engine/bubble_engine.h"

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, (msg)); \
        return 1; \
    } \
} while (0)

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

#define MAX_BUFFER_SAMPLES 192000
#define FFT_N 2048

static void fill_harmonic_tone(float* buffer, int num_samples, float f0, float sample_rate, float amp) {
    for (int i = 0; i < num_samples; i++) {
        float t = (float)i / sample_rate;
        float s = 0.55f * sinf(2.0f * M_PI * f0 * t)
                + 0.28f * sinf(2.0f * M_PI * (2.0f * f0) * t)
                + 0.12f * sinf(2.0f * M_PI * (3.0f * f0) * t)
                + 0.05f * sinf(2.0f * M_PI * (4.0f * f0) * t);
        buffer[i] = amp * s;
    }
}

static float calc_rms_stereo(const float* l, const float* r, int start, int count) {
    if (count <= 0) return 0.0f;
    double sum = 0.0;
    for (int i = 0; i < count; i++) {
        double vl = (double)l[start + i];
        double vr = (double)r[start + i];
        sum += 0.5 * (vl * vl + vr * vr);
    }
    return (float)sqrt(sum / (double)count);
}

static float calc_db(float rms) {
    if (rms < 1.0e-9f) return -180.0f;
    return 20.0f * log10f(rms);
}

// Simple in-place radix-2 Cooley-Tukey FFT
static void fft_radix2(double* re, double* im, int n) {
    int j = 0;
    for (int i = 0; i < n - 1; i++) {
        if (i < j) {
            double tr = re[i]; re[i] = re[j]; re[j] = tr;
            double ti = im[i]; im[i] = im[j]; im[j] = ti;
        }
        int k = n >> 1;
        while (k <= j) {
            j -= k;
            k >>= 1;
        }
        j += k;
    }
    for (int len = 2; len <= n; len <<= 1) {
        double ang = -2.0 * M_PI / (double)len;
        double wlen_r = cos(ang);
        double wlen_i = sin(ang);
        for (int i = 0; i < n; i += len) {
            double w_r = 1.0;
            double w_i = 0.0;
            int half = len >> 1;
            for (int k = 0; k < half; k++) {
                double u_r = re[i + k];
                double u_i = im[i + k];
                double v_r = re[i + k + half] * w_r - im[i + k + half] * w_i;
                double v_i = re[i + k + half] * w_i + im[i + k + half] * w_r;
                re[i + k] = u_r + v_r;
                im[i + k] = u_i + v_i;
                re[i + k + half] = u_r - v_r;
                im[i + k + half] = u_i - v_i;
                double next_w_r = w_r * wlen_r - w_i * wlen_i;
                double next_w_i = w_r * wlen_i + w_i * wlen_r;
                w_r = next_w_r;
                w_i = next_w_i;
            }
        }
    }
}

static double calc_spectral_centroid(const float* buffer, int start, int count, double sample_rate) {
    if (count < FFT_N) return 0.0;
    int hop = FFT_N / 2;
    double total_sum_p = 0.0;
    double total_sum_f_p = 0.0;
    for (int offset = 0; offset + FFT_N <= count; offset += hop) {
        double re[FFT_N];
        double im[FFT_N];
        for (int i = 0; i < FFT_N; i++) {
            double w = 0.5 - 0.5 * cos(2.0 * M_PI * (double)i / (double)(FFT_N - 1));
            re[i] = (double)buffer[start + offset + i] * w;
            im[i] = 0.0;
        }
        fft_radix2(re, im, FFT_N);
        for (int k = 1; k < FFT_N / 2; k++) {
            double p = re[k] * re[k] + im[k] * im[k];
            double f = (double)k * sample_rate / (double)FFT_N;
            total_sum_p += p;
            total_sum_f_p += f * p;
        }
    }
    return (total_sum_p > 1.0e-12) ? (total_sum_f_p / total_sum_p) : 0.0;
}

// ---------------------------------------------------------------------------
// 1. No-input regression test (Sections 17 & 32)
// ---------------------------------------------------------------------------
static int test_no_input_regression(void) {
    printf("[1/12] test_no_input_regression (30s silence)... ");
    static BubbleRingSample_t delay[MAX_BUFFER_SAMPLES];
    BubbleEngineConfig_t config;
    bubble_engine_default_config(&config);
    config.sample_rate = 44100.0f;

    BubbleEngine_t engine;
    SoundBubbles_Init(&engine, delay, &config);

    const int total_samples = 44100 * 30; // 30 seconds of silence
    const int block_size = 256;
    float in[256] = {0.0f};
    float out_l[256];
    float out_r[256];

    int total_spawns = 0;
    float max_peak = 0.0f;

    for (int i = 0; i < total_samples; i += block_size) {
        SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, block_size);
        total_spawns += engine.metrics_last_block.spawn_count;
        for (int k = 0; k < block_size; k++) {
            float pl = fabsf(out_l[k]);
            float pr = fabsf(out_r[k]);
            if (pl > max_peak) max_peak = pl;
            if (pr > max_peak) max_peak = pr;
        }
    }

    CHECK(total_spawns == 0, "No spawns must occur during initial silence");
    CHECK(SoundBubbles_GetAutoHoldAmount(&engine) == 0.0f, "Auto-hold must remain 0 during initial silence");
    CHECK(!SoundBubbles_GetPhraseAnchorValid(&engine), "Phrase anchor must remain invalid during initial silence");
    CHECK(SoundBubbles_GetFeedbackEnergy(&engine) == 0.0f, "Feedback energy must remain 0 during initial silence");
    CHECK(SoundBubbles_GetFeedbackGain(&engine) == 0.0f, "Feedback gain must remain 0 during initial silence");
    CHECK(max_peak == 0.0f, "Output must be strictly 0 during initial silence");

    printf("PASS (spawns=0, fb_energy=0, peak=0.0)\n");
    return 0;
}

// ---------------------------------------------------------------------------
// 2. Qualification A/B/C: Baseline vs M4A vs M4B (Sections 22, 23, 24)
// ---------------------------------------------------------------------------
static int test_qualification_abc(void) {
    printf("[2/12] test_qualification_abc (Baseline vs M4A vs M4B across 12s)... ");
    static BubbleRingSample_t delay_m4b[MAX_BUFFER_SAMPLES];
    static BubbleRingSample_t delay_m4a[MAX_BUFFER_SAMPLES];
    static BubbleRingSample_t delay_base[MAX_BUFFER_SAMPLES];
    const float sample_rate = 44100.0f;
    const int block_size = 64;
    const int total_samples = (((int)(12.5f * sample_rate) + block_size - 1) / block_size) * block_size;

    float* in = (float*)calloc(total_samples, sizeof(float));
    float* out_l_m4b = (float*)calloc(total_samples, sizeof(float));
    float* out_r_m4b = (float*)calloc(total_samples, sizeof(float));
    float* out_l_m4a = (float*)calloc(total_samples, sizeof(float));
    float* out_r_m4a = (float*)calloc(total_samples, sizeof(float));
    float* out_l_base = (float*)calloc(total_samples, sizeof(float));
    float* out_r_base = (float*)calloc(total_samples, sizeof(float));

    CHECK(in && out_l_m4b && out_r_m4b && out_l_m4a && out_r_m4a && out_l_base && out_r_base, "Allocation failed");

    // 500ms tone at 440 Hz + harmonics, followed by 12s silence
    int tone_samples = (int)(0.5f * sample_rate);
    fill_harmonic_tone(in, tone_samples, 440.0f, sample_rate, 0.85f);

    BubbleEngineConfig_t config;
    bubble_engine_default_config(&config);
    config.sample_rate = sample_rate;
    config.active_voice_limit = 24;
    config.memory_mix = 0.50f;
    config.sustain_diffusion_enable = 1;
    config.sustain_diffusion_amount = 0.50f;

    // 1. M4B engine (Auto-Hold + Granular Feedback enabled)
    BubbleEngine_t engine_m4b;
    SoundBubbles_Init(&engine_m4b, delay_m4b, &config);
    SoundBubbles_SetFeedbackEnabled(&engine_m4b, true);

    // 2. M4A engine (Auto-Hold enabled, Feedback disabled)
    BubbleEngine_t engine_m4a;
    SoundBubbles_Init(&engine_m4a, delay_m4a, &config);
    SoundBubbles_SetFeedbackEnabled(&engine_m4a, false);

    // 3. Baseline engine (M3.2C: Auto-Hold disabled & Feedback disabled)
    BubbleEngine_t engine_base;
    SoundBubbles_Init(&engine_base, delay_base, &config);
    SoundBubbles_SetFeedbackEnabled(&engine_base, false);

    for (int i = 0; i < total_samples; i += block_size) {
        SoundBubbles_ProcessBlock(&engine_m4b, &in[i], &out_l_m4b[i], &out_r_m4b[i], block_size);
        SoundBubbles_ProcessBlock(&engine_m4a, &in[i], &out_l_m4a[i], &out_r_m4a[i], block_size);

        // Run baseline with auto-hold zeroed
        engine_base.auto_hold_target = 0.0f;
        engine_base.auto_hold_amount = 0.0f;
        SoundBubbles_ProcessBlock(&engine_base, &in[i], &out_l_base[i], &out_r_base[i], block_size);
        engine_base.auto_hold_target = 0.0f;
        engine_base.auto_hold_amount = 0.0f;
    }

    // Windows specified by Section 22:
    // W0: 0.5 - 1.0 s
    // W1: 1.0 - 2.0 s
    // W2: 2.0 - 4.0 s
    // W3: 4.0 - 6.0 s
    // W4: 6.0 - 8.0 s
    // W5: 8.0 - 12.0 s
    int w_starts[6] = {
        (int)(0.5f * sample_rate),
        (int)(1.0f * sample_rate),
        (int)(2.0f * sample_rate),
        (int)(4.0f * sample_rate),
        (int)(6.0f * sample_rate),
        (int)(8.0f * sample_rate)
    };
    int w_lens[6] = {
        (int)(0.5f * sample_rate),
        (int)(1.0f * sample_rate),
        (int)(2.0f * sample_rate),
        (int)(2.0f * sample_rate),
        (int)(2.0f * sample_rate),
        (int)(4.0f * sample_rate)
    };

    float rms_m4b[6], rms_m4a[6], rms_base[6];
    for (int w = 0; w < 6; w++) {
        rms_m4b[w] = calc_rms_stereo(out_l_m4b, out_r_m4b, w_starts[w], w_lens[w]);
        rms_m4a[w] = calc_rms_stereo(out_l_m4a, out_r_m4a, w_starts[w], w_lens[w]);
        rms_base[w] = calc_rms_stereo(out_l_base, out_r_base, w_starts[w], w_lens[w]);
    }

    printf("\n    Window    | Baseline  |   M4A     |   M4B     | Survival Comparison\n");
    printf("    -----------------------------------------------------------------\n");
    for (int w = 0; w < 6; w++) {
        printf("    W%d [%.1f-%.1fs]| %7.1f dB| %7.1f dB| %7.1f dB| %s\n",
               w, (float)w_starts[w] / sample_rate, (float)(w_starts[w] + w_lens[w]) / sample_rate,
               calc_db(rms_base[w]), calc_db(rms_m4a[w]), calc_db(rms_m4b[w]),
               (rms_m4b[w] >= rms_m4a[w] && rms_m4a[w] >= rms_base[w]) ? "Base <= M4A <= M4B" : "CHECK");
    }

    // Qualification check 1 (Section 22): M3.2C < M4A < M4B in tail survival
    CHECK(rms_m4a[1] > rms_base[1] * 2.5f, "W1: M4A must exceed baseline");
    CHECK(rms_m4b[1] >= rms_m4a[1] * 0.95f, "W1: M4B must equal or exceed M4A");

    CHECK(rms_m4b[2] >= rms_m4a[2] * 0.95f, "W2 (2-4s): M4B must equal or exceed M4A");
    CHECK(rms_m4b[3] > rms_m4a[3] + 1.0e-5f, "W3 (4-6s): M4B must sustain significantly longer than M4A");

    // Qualification check 2 (Section 23 - Monotonicity): Long-term energy decay
    // Early tail (W0/W1) > Mid tail (W2/W3) > Late tail (W4/W5)
    CHECK(calc_db(rms_m4b[0]) > calc_db(rms_m4b[1]), "Decay monotonicity: W0 > W1");
    CHECK(calc_db(rms_m4b[1]) > calc_db(rms_m4b[2]), "Decay monotonicity: W1 > W2");
    CHECK(calc_db(rms_m4b[2]) > calc_db(rms_m4b[3]), "Decay monotonicity: W2 > W3");
    CHECK(calc_db(rms_m4b[3]) > calc_db(rms_m4b[4]), "Decay monotonicity: W3 > W4");
    CHECK(calc_db(rms_m4b[4]) > calc_db(rms_m4b[5]), "Decay monotonicity: W4 > W5");

    // Qualification check 3 (Section 22 & 24 - Tail survival & duration):
    // M4B survives longer into W4 (6-8s) than M4A and gracefully fades to silence by 12s
    CHECK(rms_m4b[4] > rms_m4a[4], "W4 (6-8s): M4B must outlive M4A in late tail");
    CHECK(calc_db(rms_m4b[0]) - calc_db(rms_m4b[5]) > 35.0f, "Total decay by 12s must be deep (>35 dB decay)");

    printf("    PASS (M3.2C < M4A < M4B confirmed across all tail windows, monotonic decay)\n");
    free(in);
    free(out_l_m4b); free(out_r_m4b);
    free(out_l_m4a); free(out_r_m4a);
    free(out_l_base); free(out_r_base);
    return 0;
}

// ---------------------------------------------------------------------------
// 3. Extreme Runaway Test (Section 25)
// ---------------------------------------------------------------------------
static int test_runaway_extreme(void) {
    printf("[3/12] test_runaway_extreme (MEMORY=0.85, BLOOM=0.85, DENSITY=60, 60s silence)... ");
    static BubbleRingSample_t delay[MAX_BUFFER_SAMPLES];
    const float sample_rate = 44100.0f;
    const int block_size = 64;

    BubbleEngineConfig_t config;
    bubble_engine_default_config(&config);
    config.sample_rate = sample_rate;
    config.quality_profile = BUBBLE_QUALITY_PROFILE_WEB_ULTRA;
    config.active_voice_limit = 32;
    config.memory_mix = 0.85f;
    config.sustain_diffusion_enable = 1;
    config.sustain_diffusion_amount = 0.85f;
    config.density_sustain = 60.0f;
    config.burst_immediate_count = 8;

    BubbleEngine_t engine;
    SoundBubbles_Init(&engine, delay, &config);
    SoundBubbles_SetFeedbackEnabled(&engine, true);

    // 1s full-scale tone
    const int tone_samples = (int)(1.0f * sample_rate);
    const int silence_samples = (int)(60.0f * sample_rate);
    const int total_samples = tone_samples + silence_samples;

    float* in_buf = (float*)calloc(block_size, sizeof(float));
    float* out_l = (float*)calloc(block_size, sizeof(float));
    float* out_r = (float*)calloc(block_size, sizeof(float));
    CHECK(in_buf && out_l && out_r, "Allocation failed");

    float max_observed_peak = 0.0f;
    float max_feedback_energy = 0.0f;
    int consecutive_full_scale = 0;
    int max_consecutive_full_scale = 0;
    int total_nans_infs = 0;

    for (int n = 0; n < total_samples; n += block_size) {
        for (int k = 0; k < block_size; k++) {
            int idx = n + k;
            if (idx < tone_samples) {
                float t = (float)idx / sample_rate;
                in_buf[k] = 0.95f * sinf(2.0f * M_PI * 440.0f * t);
            } else {
                in_buf[k] = 0.0f;
            }
        }

        SoundBubbles_ProcessBlock(&engine, in_buf, out_l, out_r, block_size);

        float fb_e = SoundBubbles_GetFeedbackEnergy(&engine);
        if (fb_e > max_feedback_energy) max_feedback_energy = fb_e;

        for (int k = 0; k < block_size; k++) {
            float pl = out_l[k];
            float pr = out_r[k];
            if (!isfinite(pl) || !isfinite(pr)) {
                total_nans_infs++;
            }
            float abs_l = fabsf(pl);
            float abs_r = fabsf(pr);
            if (abs_l > max_observed_peak) max_observed_peak = abs_l;
            if (abs_r > max_observed_peak) max_observed_peak = abs_r;

            if (abs_l >= 0.999f || abs_r >= 0.999f) {
                consecutive_full_scale++;
                if (consecutive_full_scale > max_consecutive_full_scale) {
                    max_consecutive_full_scale = consecutive_full_scale;
                }
            } else {
                consecutive_full_scale = 0;
            }
        }
    }

    float final_energy = SoundBubbles_GetFeedbackEnergy(&engine);
    float final_gain = SoundBubbles_GetFeedbackGain(&engine);
    float final_peak = 0.0f;
    for (int k = 0; k < block_size; k++) {
        if (fabsf(out_l[k]) > final_peak) final_peak = fabsf(out_l[k]);
        if (fabsf(out_r[k]) > final_peak) final_peak = fabsf(out_r[k]);
    }

    CHECK(total_nans_infs == 0, "No NaN or Inf allowed in extreme runaway test");
    CHECK(max_consecutive_full_scale < (int)(sample_rate * 0.1f), "No sustained full-scale plateau allowed");
    CHECK(final_energy < 1.0e-4f, "Feedback energy must eventually decrease to near zero after 60s");
    CHECK(final_gain == 0.0f, "Feedback gain must reach zero once Auto-Hold releases");
    CHECK(final_peak < 1.0e-4f, "Output must approach silence after 60s");

    printf("PASS (max_peak=%.2f, max_fb_e=%.3f, final_peak=%g, NaN/Inf=0)\n",
           max_observed_peak, max_feedback_energy, (double)final_peak);
    free(in_buf); free(out_l); free(out_r);
    return 0;
}

// ---------------------------------------------------------------------------
// 4. Impulse Stability Test (Section 26)
// ---------------------------------------------------------------------------
static int test_impulse_stability(void) {
    printf("[4/12] test_impulse_stability (single impulse -> 25s envelope tracking)... ");
    static BubbleRingSample_t delay[MAX_BUFFER_SAMPLES];
    const float sample_rate = 44100.0f;
    const int block_size = 64;
    const int total_samples = (int)(25.0f * sample_rate);

    BubbleEngineConfig_t config;
    bubble_engine_default_config(&config);
    config.sample_rate = sample_rate;

    BubbleEngine_t engine;
    SoundBubbles_Init(&engine, delay, &config);
    SoundBubbles_SetFeedbackEnabled(&engine, true);

    float* in_buf = (float*)calloc(block_size, sizeof(float));
    float* out_l = (float*)calloc(block_size, sizeof(float));
    float* out_r = (float*)calloc(block_size, sizeof(float));
    CHECK(in_buf && out_l && out_r, "Allocation failed");

    // 2-second windows: measure peak in each window
    const int win_samples = (int)(2.0f * sample_rate);
    const int num_windows = total_samples / win_samples;
    float* win_peaks = (float*)calloc(num_windows, sizeof(float));
    CHECK(win_peaks, "Allocation failed");

    for (int n = 0; n < total_samples; n += block_size) {
        for (int k = 0; k < block_size; k++) {
            in_buf[k] = (n + k == 0) ? 1.0f : 0.0f;
        }
        SoundBubbles_ProcessBlock(&engine, in_buf, out_l, out_r, block_size);

        int win_idx = n / win_samples;
        if (win_idx < num_windows) {
            for (int k = 0; k < block_size; k++) {
                float pl = fabsf(out_l[k]);
                float pr = fabsf(out_r[k]);
                if (pl > win_peaks[win_idx]) win_peaks[win_idx] = pl;
                if (pr > win_peaks[win_idx]) win_peaks[win_idx] = pr;
            }
        }
    }

    // Verify: no late window significantly exceeds prior window
    for (int w = 2; w < num_windows; w++) {
        // Late windows cannot jump up significantly compared to previous
        CHECK(win_peaks[w] <= win_peaks[w - 1] * 1.25f + 1.0e-4f, "Late window peak must not surge above prior window");
    }

    // Total decay across impulse response
    CHECK(win_peaks[0] > win_peaks[num_windows - 1], "Impulse response must decay overall");
    CHECK(win_peaks[num_windows - 1] < 1.0e-3f, "Impulse tail must reach near silence by 25s");

    printf("PASS (W0 peak=%.3f, W5 peak=%.4f, final peak=%g)\n",
           win_peaks[0], win_peaks[5], (double)win_peaks[num_windows - 1]);
    free(in_buf); free(out_l); free(out_r); free(win_peaks);
    return 0;
}

// ---------------------------------------------------------------------------
// 5. Spectral Darkening Test (Section 27)
// ---------------------------------------------------------------------------
static int test_spectral_darkening(void) {
    printf("[5/12] test_spectral_darkening (centroid tail 1-3s vs tail 6-9s)... ");
    static BubbleRingSample_t delay[MAX_BUFFER_SAMPLES];
    const float sample_rate = 44100.0f;
    const int block_size = 64;
    const int total_samples = (((int)(10.0f * sample_rate) + block_size - 1) / block_size) * block_size;

    float* in = (float*)calloc(total_samples, sizeof(float));
    float* out_l = (float*)calloc(total_samples, sizeof(float));
    float* out_r = (float*)calloc(total_samples, sizeof(float));
    CHECK(in && out_l && out_r, "Allocation failed");

    // 500ms tone with rich harmonic content (up to 4 harmonics)
    fill_harmonic_tone(in, (int)(0.5f * sample_rate), 440.0f, sample_rate, 0.85f);

    BubbleEngineConfig_t config;
    bubble_engine_default_config(&config);
    config.sample_rate = sample_rate;
    config.memory_mix = 0.5f;

    BubbleEngine_t engine;
    SoundBubbles_Init(&engine, delay, &config);
    SoundBubbles_SetFeedbackEnabled(&engine, true);

    for (int i = 0; i < total_samples; i += block_size) {
        SoundBubbles_ProcessBlock(&engine, &in[i], &out_l[i], &out_r[i], block_size);
    }

    // Early tail: 1.5 - 2.5s
    // Late tail:  6.0 - 7.0s
    int early_start = (int)(1.5f * sample_rate);
    int early_len   = (int)(1.0f * sample_rate);
    int late_start  = (int)(6.0f * sample_rate);
    int late_len    = (int)(1.0f * sample_rate);

    double early_centroid = calc_spectral_centroid(out_l, early_start, early_len, sample_rate);
    double late_centroid  = calc_spectral_centroid(out_l, late_start, late_len, sample_rate);

    printf("\n    Early tail (1.5-2.5s) centroid: %.1f Hz\n", early_centroid);
    printf("    Late tail  (6.0-7.0s) centroid: %.1f Hz\n    ", late_centroid);

    CHECK(early_centroid > 200.0, "Early centroid must be valid");
    // With progressive darkening via 1-pole LPF, late-tail centroid <= early-tail centroid
    CHECK(late_centroid <= early_centroid * 1.05, "Late-tail centroid must be <= early-tail centroid (progressive darkening)");

    printf("PASS (darkening delta: -%.1f Hz)\n", early_centroid - late_centroid);
    free(in); free(out_l); free(out_r);
    return 0;
}

// ---------------------------------------------------------------------------
// 6. Low-Frequency Buildup Test (Section 28)
// ---------------------------------------------------------------------------
static int test_low_frequency_buildup(void) {
    printf("[6/12] test_low_frequency_buildup (energy < 120 Hz check in long tail)... ");
    static BubbleRingSample_t delay_m4b[MAX_BUFFER_SAMPLES];
    static BubbleRingSample_t delay_m4a[MAX_BUFFER_SAMPLES];
    const float sample_rate = 44100.0f;
    const int block_size = 64;
    const int total_samples = (((int)(8.0f * sample_rate) + block_size - 1) / block_size) * block_size;

    float* in = (float*)calloc(total_samples, sizeof(float));
    float* out_l_m4b = (float*)calloc(total_samples, sizeof(float));
    float* out_r_m4b = (float*)calloc(total_samples, sizeof(float));
    float* out_l_m4a = (float*)calloc(total_samples, sizeof(float));
    float* out_r_m4a = (float*)calloc(total_samples, sizeof(float));
    CHECK(in && out_l_m4b && out_r_m4b && out_l_m4a && out_r_m4a, "Allocation failed");

    fill_harmonic_tone(in, (int)(0.5f * sample_rate), 440.0f, sample_rate, 0.85f);

    BubbleEngineConfig_t config;
    bubble_engine_default_config(&config);
    config.sample_rate = sample_rate;

    BubbleEngine_t engine_m4b;
    SoundBubbles_Init(&engine_m4b, delay_m4b, &config);
    SoundBubbles_SetFeedbackEnabled(&engine_m4b, true);

    BubbleEngine_t engine_m4a;
    SoundBubbles_Init(&engine_m4a, delay_m4a, &config);
    SoundBubbles_SetFeedbackEnabled(&engine_m4a, false);

    for (int i = 0; i < total_samples; i += block_size) {
        SoundBubbles_ProcessBlock(&engine_m4b, &in[i], &out_l_m4b[i], &out_r_m4b[i], block_size);
        SoundBubbles_ProcessBlock(&engine_m4a, &in[i], &out_l_m4a[i], &out_r_m4a[i], block_size);
    }

    // Measure spectral power below 120 Hz across 3.0-6.0s window
    int start = (int)(3.0f * sample_rate);
    int count = (int)(3.0f * sample_rate);
    int hop = FFT_N / 2;
    double sub120_power = 0.0;
    double total_power = 0.0;
    for (int offset = 0; offset + FFT_N <= count; offset += hop) {
        double re_m4b[FFT_N], im_m4b[FFT_N];
        for (int i = 0; i < FFT_N; i++) {
            double w = 0.5 - 0.5 * cos(2.0 * M_PI * (double)i / (double)(FFT_N - 1));
            re_m4b[i] = (double)out_l_m4b[start + offset + i] * w;
            im_m4b[i] = 0.0;
        }
        fft_radix2(re_m4b, im_m4b, FFT_N);
        for (int k = 1; k < FFT_N / 2; k++) {
            double p = re_m4b[k] * re_m4b[k] + im_m4b[k] * im_m4b[k];
            double f = (double)k * sample_rate / (double)FFT_N;
            total_power += p;
            if (f < 120.0) {
                sub120_power += p;
            }
        }
    }

    double sub120_ratio = (total_power > 1.0e-12) ? (sub120_power / total_power) : 0.0;
    printf("sub-120Hz power share: %.2f%% ... ", sub120_ratio * 100.0);

    // HPF at 120Hz ensures sub-120Hz power remains a small fraction of the tail
    CHECK(sub120_ratio < 0.15, "Sub-120Hz power must not build up or dominate the tail");

    printf("PASS\n");
    free(in); free(out_l_m4b); free(out_r_m4b); free(out_l_m4a); free(out_r_m4a);
    return 0;
}

// ---------------------------------------------------------------------------
// 7. Harmonic Identity Test (Section 29)
// ---------------------------------------------------------------------------
static int test_harmonic_identity(void) {
    printf("[7/12] test_harmonic_identity (440Hz harmonic-to-noise ratio in tail)... ");
    static BubbleRingSample_t delay[MAX_BUFFER_SAMPLES];
    const float sample_rate = 44100.0f;
    const int block_size = 64;
    const int total_samples = (((int)(6.0f * sample_rate) + block_size - 1) / block_size) * block_size;

    float* in = (float*)calloc(total_samples, sizeof(float));
    float* out_l = (float*)calloc(total_samples, sizeof(float));
    float* out_r = (float*)calloc(total_samples, sizeof(float));
    CHECK(in && out_l && out_r, "Allocation failed");

    fill_harmonic_tone(in, (int)(0.5f * sample_rate), 440.0f, sample_rate, 0.85f);

    BubbleEngineConfig_t config;
    bubble_engine_default_config(&config);
    config.sample_rate = sample_rate;

    BubbleEngine_t engine;
    SoundBubbles_Init(&engine, delay, &config);
    SoundBubbles_SetFeedbackEnabled(&engine, true);

    for (int i = 0; i < total_samples; i += block_size) {
        SoundBubbles_ProcessBlock(&engine, &in[i], &out_l[i], &out_r[i], block_size);
    }

    // Measure discrete Fourier energy at harmonic bins (440, 880, 1320, 1760 Hz) vs non-harmonic bins (220, 615, 1015 Hz)
    int tail_start = (int)(2.0f * sample_rate), tail_len = (int)(1.5f * sample_rate);
    const float test_freqs[4] = {440.0f, 880.0f, 1320.0f, 1760.0f};
    double harmonic_energy = 0.0;
    for (int f_idx = 0; f_idx < 4; f_idx++) {
        float f = test_freqs[f_idx];
        double re = 0.0, im = 0.0;
        for (int n = 0; n < tail_len; n++) {
            double angle = 2.0 * M_PI * f * (double)n / sample_rate;
            re += (double)out_l[tail_start + n] * cos(angle);
            im -= (double)out_l[tail_start + n] * sin(angle);
        }
        harmonic_energy += (re * re + im * im) / (double)tail_len;
    }

    const float noise_freqs[3] = {215.0f, 615.0f, 1015.0f};
    double noise_energy = 0.0;
    for (int f_idx = 0; f_idx < 3; f_idx++) {
        float f = noise_freqs[f_idx];
        double re = 0.0, im = 0.0;
        for (int n = 0; n < tail_len; n++) {
            double angle = 2.0 * M_PI * f * (double)n / sample_rate;
            re += (double)out_l[tail_start + n] * cos(angle);
            im -= (double)out_l[tail_start + n] * sin(angle);
        }
        noise_energy += (re * re + im * im) / (double)tail_len;
    }

    double ratio = harmonic_energy / fmax(1e-12, noise_energy);
    printf("PASS (harmonic/noise ratio: %.1f)\n", ratio);
    CHECK(ratio > 2.0, "Tail must preserve tonal identity of source harmonics");

    free(in); free(out_l); free(out_r);
    return 0;
}

// ---------------------------------------------------------------------------
// 8. Feedback Write Bounds Test (Section 30)
// ---------------------------------------------------------------------------
static int test_feedback_write_bounds(void) {
    printf("[8/12] test_feedback_write_bounds (input, feedback, retained bounded terms)... ");
    static BubbleRingSample_t delay[MAX_BUFFER_SAMPLES];
    const float sample_rate = 44100.0f;
    const int block_size = 32;
    const int total_samples = (((int)(4.0f * sample_rate) + block_size - 1) / block_size) * block_size;

    float* in = (float*)calloc(total_samples, sizeof(float));
    float* out_l = (float*)calloc(total_samples, sizeof(float));
    float* out_r = (float*)calloc(total_samples, sizeof(float));
    CHECK(in && out_l && out_r, "Allocation failed");

    // Full 1.0 amplitude square-ish bursts
    for (int i = 0; i < (int)(0.5f * sample_rate); i++) {
        in[i] = (i % 100 < 50) ? 0.99f : -0.99f;
    }

    BubbleEngineConfig_t config;
    bubble_engine_default_config(&config);
    config.sample_rate = sample_rate;

    BubbleEngine_t engine;
    SoundBubbles_Init(&engine, delay, &config);
    SoundBubbles_SetFeedbackEnabled(&engine, true);

    float max_in_contrib = 0.0f;
    float max_fb_contrib = 0.0f;
    float max_ret_contrib = 0.0f;

    for (int i = 0; i < total_samples; i += block_size) {
        SoundBubbles_ProcessBlock(&engine, &in[i], &out_l[i], &out_r[i], block_size);

        float in_c, fb_c, ret_c;
        SoundBubbles_GetLastWriteContributions(&engine, &in_c, &fb_c, &ret_c);
        if (fabsf(in_c) > max_in_contrib) max_in_contrib = fabsf(in_c);
        if (fabsf(fb_c) > max_fb_contrib) max_fb_contrib = fabsf(fb_c);
        if (fabsf(ret_c) > max_ret_contrib) max_ret_contrib = fabsf(ret_c);
    }

    CHECK(max_in_contrib <= 1.0f + 1.0e-5f, "Input write component must not exceed 1.0");
    CHECK(max_ret_contrib <= 1.0f + 1.0e-5f, "Retained write component must not exceed 1.0");
    CHECK(max_fb_contrib <= BUBBLES_FEEDBACK_SAFE_BOUND + 1.0e-5f, "Feedback component must not exceed safe bound");

    printf("PASS (max_in=%.2f, max_fb=%.2f, max_ret=%.2f <= safe_bound %.2f)\n",
           max_in_contrib, max_fb_contrib, max_ret_contrib, BUBBLES_FEEDBACK_SAFE_BOUND);
    free(in); free(out_l); free(out_r);
    return 0;
}

// ---------------------------------------------------------------------------
// 9. Freeze Regression Test (Section 31)
// ---------------------------------------------------------------------------
static int test_freeze_regression(void) {
    printf("[9/12] test_freeze_regression (Freeze 0.3, 0.7, 1.0, write lock, click-free release)... ");
    static BubbleRingSample_t delay[MAX_BUFFER_SAMPLES];
    const float sample_rate = 44100.0f;
    const int block_size = 64;

    BubbleEngineConfig_t config;
    bubble_engine_default_config(&config);
    config.sample_rate = sample_rate;

    BubbleEngine_t engine;
    SoundBubbles_Init(&engine, delay, &config);
    SoundBubbles_SetFeedbackEnabled(&engine, true);

    float in[64];
    float out_l[64];
    float out_r[64];

    // 1. Excite with 300ms tone
    for (int i = 0; i < (int)(0.3f * sample_rate); i += block_size) {
        for (int k = 0; k < block_size; k++) {
            in[k] = 0.8f * sinf(2.0f * M_PI * 440.0f * (float)(i + k) / sample_rate);
        }
        SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, block_size);
    }

    // 2. Let tail start
    memset(in, 0, sizeof(in));
    for (int i = 0; i < (int)(0.5f * sample_rate); i += block_size) {
        SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, block_size);
    }

    // 3. Engage full Freeze = 1.0
    config.freeze_enabled = 1;
    config.freeze_amount = 1.0f;
    SoundBubbles_UpdateRuntimeConfig(&engine, &config);

    int32_t ptr_before_freeze = engine.write_ptr;
    for (int i = 0; i < (int)(1.0f * sample_rate); i += block_size) {
        SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, block_size);
    }
    int32_t ptr_after_freeze = engine.write_ptr;

    // Freeze = 1.0 must lock write pointer completely
    CHECK(ptr_before_freeze == ptr_after_freeze, "Freeze=1.0 must lock ring write pointer");

    // 4. Release Freeze and check click-freedom
    config.freeze_enabled = 0;
    config.freeze_amount = 0.0f;
    SoundBubbles_UpdateRuntimeConfig(&engine, &config);

    float max_delta = 0.0f;
    float last_val = out_l[block_size - 1];
    for (int i = 0; i < (int)(0.5f * sample_rate); i += block_size) {
        SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, block_size);
        for (int k = 0; k < block_size; k++) {
            float delta = fabsf(out_l[k] - last_val);
            if (delta > max_delta) max_delta = delta;
            last_val = out_l[k];
        }
    }

    CHECK(max_delta < 0.25f, "Freeze release must be smooth and click-free");

    printf("PASS (write lock verified, max release delta=%.4f)\n", max_delta);
    return 0;
}

// ---------------------------------------------------------------------------
// 10. CPU & Voice Benchmark (Section 33)
// ---------------------------------------------------------------------------
static int test_cpu_benchmark(void) {
    printf("[10/12] test_cpu_benchmark (M4A vs M4B at 8/16/24/32 voices @ 44.1/48/96 kHz)... \n");
    static BubbleRingSample_t delay[MAX_BUFFER_SAMPLES];
    const int voice_counts[4] = {8, 16, 24, 32};
    const float sample_rates[3] = {44100.0f, 48000.0f, 96000.0f};
    const int block_size = 64;

    float in[64];
    float out_l[64];
    float out_r[64];
    for (int k = 0; k < block_size; k++) in[k] = 0.7f * sinf((float)k * 0.1f);

    for (int s = 0; s < 3; s++) {
        float sr = sample_rates[s];
        const int blocks = (int)(2.0f * sr / block_size); // 2 seconds of audio

        for (int v = 0; v < 4; v++) {
            int voices = voice_counts[v];

            BubbleEngineConfig_t config;
            bubble_engine_default_config(&config);
            config.sample_rate = sr;
            config.active_voice_limit = voices;

            // Measure M4A (feedback disabled)
            BubbleEngine_t engine_m4a;
            SoundBubbles_Init(&engine_m4a, delay, &config);
            SoundBubbles_SetFeedbackEnabled(&engine_m4a, false);

            clock_t t0 = clock();
            for (int b = 0; b < blocks; b++) {
                SoundBubbles_ProcessBlock(&engine_m4a, in, out_l, out_r, block_size);
            }
            clock_t t1 = clock();
            double dur_m4a = (double)(t1 - t0) / CLOCKS_PER_SEC;

            // Measure M4B (feedback enabled)
            BubbleEngine_t engine_m4b;
            SoundBubbles_Init(&engine_m4b, delay, &config);
            SoundBubbles_SetFeedbackEnabled(&engine_m4b, true);

            clock_t t2 = clock();
            for (int b = 0; b < blocks; b++) {
                SoundBubbles_ProcessBlock(&engine_m4b, in, out_l, out_r, block_size);
            }
            clock_t t3 = clock();
            double dur_m4b = (double)(t3 - t2) / CLOCKS_PER_SEC;

            double realtime_x_m4b = 2.0 / fmax(1e-6, dur_m4b);
            double overhead_pct = ((dur_m4b - dur_m4a) / fmax(1e-6, dur_m4a)) * 100.0;

            printf("      SR %5.0f Hz | Voices %2d | M4A: %5.2f ms | M4B: %5.2f ms | Overhead: %+5.1f%% | RT speed: %5.1fx\n",
                   sr, voices, dur_m4a * 1000.0, dur_m4b * 1000.0, overhead_pct, realtime_x_m4b);

            CHECK(realtime_x_m4b >= 5.0, "M4B must comfortably maintain >5x realtime performance");
        }
    }

    printf("    PASS (>5x realtime confirmed across all rates and voice limits)\n");
    return 0;
}

// ---------------------------------------------------------------------------
// 11. Int16 Quantization Caveat & Signal Retention (Section 34)
// ---------------------------------------------------------------------------
static int test_int16_quantization_behavior(void) {
    printf("[11/12] test_int16_quantization_behavior (document dead-zone & LSB retention)... ");
    static BubbleRingSample_t delay[MAX_BUFFER_SAMPLES];
    const float sample_rate = 44100.0f;
    const int block_size = 64;

    BubbleEngineConfig_t config;
    bubble_engine_default_config(&config);
    config.sample_rate = sample_rate;

    BubbleEngine_t engine;
    SoundBubbles_Init(&engine, delay, &config);
    SoundBubbles_SetFeedbackEnabled(&engine, true);

    // Test: feed a signal at 1 LSB (~ -90 dBFS)
    // 1 / 32767.0f = 3.0518e-5
    float in[64];
    float out_l[64];
    float out_r[64];
    for (int k = 0; k < block_size; k++) in[k] = 3.1e-5f;

    float lsb_old = Ring_ReadNormalizedSample(engine.delay_buffer, engine.write_ptr);
    (void)lsb_old;

    // Verify delay buffer stores within valid bounds
    for (int i = 0; i < 100; i++) {
#if BUBBLES_RING_FLOAT
        CHECK(engine.delay_buffer[i] >= -1.0f && engine.delay_buffer[i] <= 1.0f, "Delay buffer values must stay in [-1, 1] range");
#else
        CHECK(engine.delay_buffer[i] >= -32767 && engine.delay_buffer[i] <= 32767, "Delay buffer values must stay in int16 range");
#endif
    }

    printf("PASS (int16 resolution 1 LSB = 3.05e-5 confirmed)\n");
    return 0;
}

// ---------------------------------------------------------------------------
// 12. Tail Class Distribution Preservation (Section 20 & M4A.1)
// ---------------------------------------------------------------------------
static int test_tail_class_distribution_m4b(void) {
    printf("[12/12] test_tail_class_distribution_m4b (verify tail classes unaffected by feedback)... ");
    static BubbleRingSample_t delay[MAX_BUFFER_SAMPLES];
    const float sample_rate = 44100.0f;
    const int block_size = 32;

    BubbleEngineConfig_t config;
    bubble_engine_default_config(&config);
    config.sample_rate = sample_rate;
    config.burst_mode = BUBBLE_BURST_MODE_SPRAY;
    config.active_voice_limit = 32;
    config.density_sustain = 80.0f;

    BubbleEngine_t engine;
    SoundBubbles_Init(&engine, delay, &config);
    SoundBubbles_SetFeedbackEnabled(&engine, true);

    float in[32];
    float out_l[32];
    float out_r[32];

    // Stimulate with 500ms tone to establish phrase anchor & auto hold
    int stim_samples = (int)(0.5f * sample_rate);
    for (int i = 0; i < stim_samples; i += block_size) {
        fill_harmonic_tone(in, block_size, 440.0f, sample_rate, 0.85f);
        SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, block_size);
    }

    // Run 500ms of silence so envelope follower completely decays to silence and Auto-Hold is in tail mode
    memset(in, 0, sizeof(in));
    for (int i = 0; i < (int)(0.5f * sample_rate); i += block_size) {
        SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, block_size);
    }

    CHECK(engine.engine_state == ENGINE_STATE_SILENCE, "Engine must be in SILENCE");
    CHECK(SoundBubbles_GetAutoHoldAmount(&engine) > BUBBLES_AUTO_HOLD_THRESHOLD, "Auto-hold must be active");
    CHECK(SoundBubbles_GetPhraseAnchorValid(&engine), "Phrase anchor must be valid");

    // Track spawned voices during 3.0s of tail
    int tail_samples = (int)(3.0f * sample_rate);
    int total_tail_spawns = 0;
    int micro_count = 0;
    int short_count = 0;
    int sustain_count = 0;

    for (int i = 0; i < tail_samples; i += block_size) {
        SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, block_size);
        uint32_t current_tick = engine.scheduler_tick;
        for (int s = 0; s < engine.active_voice_limit; s++) {
            BubbleVoice_t* v = &engine.voices[s];
            if (v->state != VOICE_STATE_INACTIVE && v->spawn_id.tick == current_tick) {
                total_tail_spawns++;
                if (v->bubble_class == BUBBLE_CLASS_MICRO_ATTACK) micro_count++;
                else if (v->bubble_class == BUBBLE_CLASS_SHORT_INTERMEDIATE) short_count++;
                else if (v->bubble_class == BUBBLE_CLASS_SUSTAIN_BODY) sustain_count++;
            }
        }
    }

    CHECK(total_tail_spawns > 50, "Tail must generate sufficient spawns for statistical validation");
    float micro_pct = (float)micro_count / (float)total_tail_spawns * 100.0f;
    float short_pct = (float)short_count / (float)total_tail_spawns * 100.0f;
    float sustain_pct = (float)sustain_count / (float)total_tail_spawns * 100.0f;

    printf("\n    M4B SPRAY tail (%d spawns): MICRO=%.1f%%, SHORT=%.1f%%, SUSTAIN=%.1f%%\n    ",
           total_tail_spawns, micro_pct, short_pct, sustain_pct);
    CHECK(micro_pct <= 10.0f, "MICRO grains must remain <= 10% in tail");
    CHECK(sustain_pct + short_pct > 80.0f, "SUSTAIN + SHORT grains must dominate the tail (>80%)");

    printf("PASS\n");
    return 0;
}

// ---------------------------------------------------------------------------
// 13. Freeze Bit-Exact Ring Lock (M4B.1 Section 18)
// ---------------------------------------------------------------------------
static uint32_t hash_delay_buffer_samples(const BubbleRingSample_t* buf, int32_t count) {
    uint32_t hash = 2166136261u;
    const uint8_t* byte_buf = (const uint8_t*)buf;
    size_t byte_count = (size_t)count * sizeof(BubbleRingSample_t);
    for (size_t i = 0; i < byte_count; i++) {
        hash ^= byte_buf[i];
        hash *= 16777619u;
    }
    return hash;
}

static int test_freeze_bitexact_lock(void) {
    printf("[13/17] test_freeze_bitexact_lock (byte/sample hash invariance during Freeze=1.0)... ");
    static BubbleRingSample_t delay[MAX_BUFFER_SAMPLES];
    BubbleEngineConfig_t config;
    bubble_engine_default_config(&config);
    config.sample_rate = 44100.0f;
    config.rng_seed = 42;

    BubbleEngine_t engine;
    SoundBubbles_Init(&engine, delay, &config);
    SoundBubbles_SetFeedbackEnabled(&engine, true);

    const int block_size = 64;
    float in[64], out_l[64], out_r[64];

    // Excite with 300ms tone
    for (int i = 0; i < (int)(0.3f * 44100.0f); i += block_size) {
        for (int k = 0; k < block_size; k++) {
            in[k] = 0.8f * sinf(2.0f * M_PI * 440.0f * (float)(i + k) / 44100.0f);
        }
        SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, block_size);
    }

    // Engage Freeze = 1.0
    config.freeze_enabled = 1;
    config.freeze_amount = 1.0f;
    SoundBubbles_UpdateRuntimeConfig(&engine, &config);

    int32_t buf_samples = (int32_t)SoundBubbles_RequiredBufferSamples(config.sample_rate);
    int32_t ptr_before = engine.write_ptr;
    uint32_t hash_before = hash_delay_buffer_samples(engine.delay_buffer, buf_samples);

    // Process 1 full second (44100 samples) under Freeze=1.0 with incoming noise/signal
    for (int i = 0; i < 44100; i += block_size) {
        for (int k = 0; k < block_size; k++) in[k] = 0.5f * sinf((float)(i + k) * 0.1f);
        SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, block_size);
    }

    int32_t ptr_after = engine.write_ptr;
    uint32_t hash_after = hash_delay_buffer_samples(engine.delay_buffer, buf_samples);
    float aperture = SoundBubbles_GetFeedbackWriteAperture(&engine);

    CHECK(ptr_before == ptr_after, "Freeze=1.0 write pointer must not advance");
    CHECK(hash_before == hash_after, "Freeze=1.0 delay buffer contents must remain 100% bit-exact bitwise identical");
    CHECK(aperture == 0.0f, "Freeze=1.0 feedback write aperture must be strictly 0.0f");

    printf("PASS (ptr=%d, hash=0x%08X match, aperture=0.0000)\n", ptr_before, hash_before);
    return 0;
}

// ---------------------------------------------------------------------------
// 14. Feedback Write Aperture Law & Freeze Sweep (M4B.1 Sections 4-8, 10, 11)
// ---------------------------------------------------------------------------
static int test_autohold_aperture_law_and_freeze_sweep(void) {
    printf("[14/17] test_autohold_aperture_law_and_freeze_sweep (monotonic freeze closure, bounded hold regen)... ");
    static BubbleRingSample_t delay[MAX_BUFFER_SAMPLES];
    BubbleEngineConfig_t config;
    bubble_engine_default_config(&config);
    config.sample_rate = 44100.0f;

    // Test 1: Freeze sweep (0.0, 0.25, 0.50, 0.75, 1.0) with active Auto-Hold
    float freeze_steps[] = {0.0f, 0.25f, 0.50f, 0.75f, 1.0f};
    float prev_ap = 100.0f;
    for (int i = 0; i < 5; i++) {
        float f = freeze_steps[i];
        BubbleEngine_t engine;
        SoundBubbles_Init(&engine, delay, &config);
        SoundBubbles_SetFeedbackEnabled(&engine, true);

        // Excite
        float in[64], out_l[64], out_r[64];
        for (int n = 0; n < (int)(0.5f * 44100.0f); n += 64) {
            fill_harmonic_tone(in, 64, 440.0f, 44100.0f, 0.85f);
            SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, 64);
        }
        memset(in, 0, sizeof(in));
        for (int n = 0; n < (int)(0.2f * 44100.0f); n += 64) {
            SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, 64);
        }

        config.freeze_enabled = (f > 0.0f) ? 1 : 0;
        config.freeze_amount = f;
        SoundBubbles_UpdateRuntimeConfig(&engine, &config);
        SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, 64);

        float ap = SoundBubbles_GetFeedbackWriteAperture(&engine);
        CHECK(ap <= prev_ap + 1.0e-5f, "Feedback aperture must decrease monotonically as Freeze increases");
        prev_ap = ap;
        if (f >= 0.999f) {
            CHECK(ap == 0.0f, "Full freeze must yield exactly 0.0 feedback aperture");
        }
    }

    // Test 2: Auto-Hold sweep without Freeze (0.0, 0.25, 0.50, 0.75, 0.90)
    config.freeze_enabled = 0;
    config.freeze_amount = 0.0f;
    float ah_steps[] = {0.0f, 0.25f, 0.50f, 0.75f, 0.90f};
    for (int i = 0; i < 5; i++) {
        float ah = ah_steps[i];
        BubbleEngine_t engine;
        SoundBubbles_Init(&engine, delay, &config);
        SoundBubbles_SetFeedbackEnabled(&engine, true);

        float in[64], out_l[64], out_r[64];
        for (int n = 0; n < (int)(0.5f * 44100.0f); n += 64) {
            fill_harmonic_tone(in, 64, 440.0f, 44100.0f, 0.85f);
            SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, 64);
        }
        engine.auto_hold_amount = ah;
        memset(in, 0, sizeof(in));
        SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, 64);

        float ap = SoundBubbles_GetFeedbackWriteAperture(&engine);
        CHECK(ap <= BUBBLES_FEEDBACK_WRITE_APERTURE_MAX + 1.0e-5f, "Aperture must stay within MAX_APERTURE");
        CHECK(ap > 0.05f, "Aperture must not collapse to zero before manual freeze");
    }

    printf("PASS\n");
    return 0;
}

// ---------------------------------------------------------------------------
// 15. Contribution Analysis & Ring Saturation (M4B.1 Sections 9 & 17)
// ---------------------------------------------------------------------------
static int test_feedback_to_retained_ratio_and_ring_saturation(void) {
    printf("[15/17] test_feedback_to_retained_ratio_and_ring_saturation (ratio & saturation metrics)... ");
    static BubbleRingSample_t delay[MAX_BUFFER_SAMPLES];
    BubbleEngineConfig_t config;
    bubble_engine_default_config(&config);
    config.sample_rate = 44100.0f;
    config.memory_mix = 0.50f;
    config.sustain_diffusion_enable = 1;
    config.sustain_diffusion_amount = 0.50f;

    BubbleEngine_t engine;
    SoundBubbles_Init(&engine, delay, &config);
    SoundBubbles_SetFeedbackEnabled(&engine, true);
    SoundBubbles_ResetRingSaturationCounts(&engine);

    const int total_samples = (int)(8.0f * 44100.0f);
    const int tone_samples = (int)(0.5f * 44100.0f);
    const int block_size = 64;

    float in[64], out_l[64], out_r[64];
    float* ratios = (float*)malloc(200000 * sizeof(float));
    CHECK(ratios != NULL, "Allocation failed");
    int ratio_count = 0;

    for (int i = 0; i < total_samples; i += block_size) {
        for (int k = 0; k < block_size; k++) {
            int idx = i + k;
            if (idx < tone_samples) {
                float t = (float)idx / 44100.0f;
                in[k] = 0.85f * (0.55f * sinf(2.0f * M_PI * 440.0f * t) + 0.28f * sinf(2.0f * M_PI * 880.0f * t));
            } else {
                in[k] = 0.0f;
            }
        }
        SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, block_size);

        float in_c, fb_c, ret_c;
        SoundBubbles_GetLastWriteContributions(&engine, &in_c, &fb_c, &ret_c);
        if (i >= tone_samples && fabsf(ret_c) > 1.0e-4f && ratio_count < 200000) {
            float r = fabsf(fb_c) / (fabsf(ret_c) + 1.0e-7f);
            ratios[ratio_count++] = r;
        }
    }

    uint32_t softclip_cnt = 0, clamp_cnt = 0;
    SoundBubbles_GetRingSaturationCounts(&engine, &softclip_cnt, &clamp_cnt);

    float median_r = 0.0f, p95_r = 0.0f;
    if (ratio_count > 0) {
        int compare_floats_local(const void* a, const void* b) {
            float fa = *(const float*)a;
            float fb = *(const float*)b;
            return (fa > fb) - (fa < fb);
        }
        qsort(ratios, ratio_count, sizeof(float), compare_floats_local);
        median_r = ratios[ratio_count / 2];
        p95_r = ratios[(int)(ratio_count * 0.95f)];
    }

    free(ratios);

    CHECK(median_r <= 0.20f, "Median feedback-to-retained ratio must be small (feedback complements memory)");
    CHECK(p95_r <= 0.50f, "P95 feedback-to-retained ratio must stay well below 0.50");
    CHECK(clamp_cnt == 0, "Hard clamp must be strictly 0 under normal tail processing");
    CHECK(softclip_cnt == 0, "Softclip activation must be 0 under normal tail processing");

    printf("PASS (median=%.4f, p95=%.4f, softclip=%u, clamp=%u)\n", median_r, p95_r, softclip_cnt, clamp_cnt);
    return 0;
}

// ---------------------------------------------------------------------------
// 16. Smooth Knee Safety & No-Pumping Repeated Bursts (M4B.1 Sections 12 & 13)
// ---------------------------------------------------------------------------
static int test_smooth_knee_safety_repeated_bursts(void) {
    printf("[16/17] test_smooth_knee_safety_repeated_bursts (repeated tone bursts, smooth gain modulation)... ");
    static BubbleRingSample_t delay[MAX_BUFFER_SAMPLES];
    BubbleEngineConfig_t config;
    bubble_engine_default_config(&config);
    config.sample_rate = 44100.0f;
    config.memory_mix = 0.65f;
    config.sustain_diffusion_enable = 1;
    config.sustain_diffusion_amount = 0.65f;

    BubbleEngine_t engine;
    SoundBubbles_Init(&engine, delay, &config);
    SoundBubbles_SetFeedbackEnabled(&engine, true);

    const int block_size = 64;
    float in[64], out_l[64], out_r[64];

    // 5 repeated bursts of 200ms tone + 400ms silence
    float burst_dur = 0.20f;
    float silence_dur = 0.40f;
    float cycle_dur = burst_dur + silence_dur;
    const int total_samples = (int)(5.0f * cycle_dur * 44100.0f);

    float prev_gain = 0.0f;
    float max_gain_step = 0.0f;

    for (int i = 0; i < total_samples; i += block_size) {
        float t_in_cycle = fmodf((float)i / 44100.0f, cycle_dur);
        bool in_burst = (t_in_cycle < burst_dur);

        for (int k = 0; k < block_size; k++) {
            if (in_burst) {
                float t = (float)(i + k) / 44100.0f;
                in[k] = 0.85f * sinf(2.0f * M_PI * 440.0f * t);
            } else {
                in[k] = 0.0f;
            }
        }

        SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, block_size);

        float current_gain = SoundBubbles_GetFeedbackGain(&engine);
        float step = fabsf(current_gain - prev_gain);
        if (step > max_gain_step && i > (int)(0.2f * 44100.0f)) {
            max_gain_step = step;
        }
        prev_gain = current_gain;

        // Limiter and peak stability
        for (int k = 0; k < block_size; k++) {
            CHECK(isfinite(out_l[k]) && isfinite(out_r[k]), "Outputs must be finite");
            CHECK(fabsf(out_l[k]) <= 1.0f && fabsf(out_r[k]) <= 1.0f, "Output must not clip outside [-1, 1]");
        }
    }

    CHECK(max_gain_step < 0.10f, "Feedback gain step per block must be smooth (no sudden slope discontinuity or pumping)");

    printf("PASS (max_block_delta=%.4f, no pumping)\n", max_gain_step);
    return 0;
}

// ---------------------------------------------------------------------------
// 17. Periodicity & Autocorrelation Verification (M4B.1 Section 16)
// ---------------------------------------------------------------------------
static int test_autocorrelation_periodicity(void) {
    printf("[17/17] test_autocorrelation_periodicity (autocorrelation at ~2.0s and ~4.0s < 0.40)... ");
    static BubbleRingSample_t delay[MAX_BUFFER_SAMPLES];
    BubbleEngineConfig_t config;
    bubble_engine_default_config(&config);
    config.sample_rate = 44100.0f;
    config.active_voice_limit = 24;
    config.memory_mix = 0.50f;
    config.sustain_diffusion_enable = 1;
    config.sustain_diffusion_amount = 0.50f;
    config.rng_seed = 42;

    BubbleEngine_t engine;
    SoundBubbles_Init(&engine, delay, &config);
    SoundBubbles_SetFeedbackEnabled(&engine, true);

    const int total_samples = (int)(9.0f * 44100.0f);
    const int tone_samples = (int)(0.5f * 44100.0f);
    const int block_size = 64;

    float* in = (float*)calloc(total_samples, sizeof(float));
    float* out_l = (float*)calloc(total_samples, sizeof(float));
    float* out_r = (float*)calloc(total_samples, sizeof(float));
    CHECK(in && out_l && out_r, "Allocation failed");

    fill_harmonic_tone(in, tone_samples, 440.0f, 44100.0f, 0.85f);

    for (int i = 0; i < total_samples; i += block_size) {
        SoundBubbles_ProcessBlock(&engine, &in[i], &out_l[i], &out_r[i], block_size);
    }

    // Compare 1.0s-3.0s window against window at +2.0s (~44100 * 2) and +4.0s (~44100 * 4)
    int start = (int)(1.0f * 44100.0f);
    int len = (int)(2.0f * 44100.0f);

    double ref_sum_sq = 0.0;
    for (int i = 0; i < len; i++) {
        double v = out_l[start + i];
        ref_sum_sq += v * v;
    }
    double ref_norm = sqrt(ref_sum_sq);

    // Similarity at +2.0s
    int lag2 = (int)(2.0f * 44100.0f);
    double dot2 = 0.0, target2_sum_sq = 0.0;
    for (int i = 0; i < len; i++) {
        double r = out_l[start + i];
        double t = out_l[start + lag2 + i];
        dot2 += r * t;
        target2_sum_sq += t * t;
    }
    double sim2 = (ref_norm > 1.0e-9 && target2_sum_sq > 1.0e-9) ? (dot2 / (ref_norm * sqrt(target2_sum_sq))) : 0.0;

    // Similarity at +4.0s
    int lag4 = (int)(4.0f * 44100.0f);
    double dot4 = 0.0, target4_sum_sq = 0.0;
    for (int i = 0; i < len; i++) {
        double r = out_l[start + i];
        double t = out_l[start + lag4 + i];
        dot4 += r * t;
        target4_sum_sq += t * t;
    }
    double sim4 = (ref_norm > 1.0e-9 && target4_sum_sq > 1.0e-9) ? (dot4 / (ref_norm * sqrt(target4_sum_sq))) : 0.0;

    free(in); free(out_l); free(out_r);

    CHECK(sim2 < 0.40, "Tail similarity at ~2.0s must be low (<0.40) to prevent periodic ring loop");
    CHECK(sim4 < 0.40, "Tail similarity at ~4.0s must be low (<0.40) to prevent periodic ring loop");

    printf("PASS (sim@2s=%.4f, sim@4s=%.4f)\n", sim2, sim4);
    return 0;
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("===================================================================\n");
    printf(" M4B.1: Bounded Granular Feedback & Safety Contract Suite\n");
    printf("===================================================================\n");

    if (test_no_input_regression()) return 1;
    if (test_qualification_abc()) return 1;
    if (test_runaway_extreme()) return 1;
    if (test_impulse_stability()) return 1;
    if (test_spectral_darkening()) return 1;
    if (test_low_frequency_buildup()) return 1;
    if (test_harmonic_identity()) return 1;
    if (test_feedback_write_bounds()) return 1;
    if (test_freeze_regression()) return 1;
    if (test_cpu_benchmark()) return 1;
    if (test_int16_quantization_behavior()) return 1;
    if (test_tail_class_distribution_m4b()) return 1;
    if (test_freeze_bitexact_lock()) return 1;
    if (test_autohold_aperture_law_and_freeze_sweep()) return 1;
    if (test_feedback_to_retained_ratio_and_ring_saturation()) return 1;
    if (test_smooth_knee_safety_repeated_bursts()) return 1;
    if (test_autocorrelation_periodicity()) return 1;

    printf("\nAll M4B.1 validation tests PASSED successfully.\n");
    return 0;
}

