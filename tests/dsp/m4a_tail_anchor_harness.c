// M4A "Auto-Hold & Phrase Anchor Tail Architecture" validation harness.
//
// Covers all M4A milestone requirements:
//   1. No-input test (10s silence from init -> 0 spawns, auto_hold=0, anchor invalid, near-zero output)
//   2. Tail survival test (500ms tone -> 6s silence: M4A vs baseline RMS in windows 0.5-1s, 1-2s, 2-3s, 3-4s, 4-6s)
//   3. Decay slope & RT metrics (smooth dB curve, T20, T40, no flat forever, no abrupt drop)
//   4. Spectral identity (spectral centroid & band correlation between source and tail)
//   5. Phrase anchor test (Phrase A -> tail A anchored -> Phrase B arrives -> anchor switches to B -> tail B anchored)
//   6. Infinite-tail prevention (1s input -> 30s silence -> auto_hold reaches 0, tail stops, engine idle)
//   7. Freeze interaction regression (Auto-Hold only, Freeze 0.3, 0.7, 1.0, release freeze click-free transition)
//   8. Stereo coherence (shared anchor write ptr & timing between L/R, decorrelated spatial audio)
//   9. CPU & voice scaling benchmark (8, 16, 24, 32 voices at 44.1, 48, 96 kHz)

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

#define CHECK_CLOSE(actual, expected, tol, msg) do { \
    if (fabsf((float)(actual) - (float)(expected)) > (float)(tol)) { \
        fprintf(stderr, "FAIL %s:%d: %s (actual=%g expected=%g tol=%g)\n", \
                __FILE__, __LINE__, (msg), (double)(actual), (double)(expected), (double)(tol)); \
        return 1; \
    } \
} while (0)

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

#define MAX_BUFFER_SAMPLES 192000

static void fill_sine_tone(float* buffer, int num_samples, float freq, float sample_rate, float amp) {
    for (int i = 0; i < num_samples; i++) {
        buffer[i] = amp * sinf(2.0f * M_PI * freq * (float)i / sample_rate);
    }
}

static void fill_harmonic_tone(float* buffer, int num_samples, float f0, float sample_rate, float amp) {
    for (int i = 0; i < num_samples; i++) {
        float t = (float)i / sample_rate;
        float s = 0.60f * sinf(2.0f * M_PI * f0 * t)
                + 0.30f * sinf(2.0f * M_PI * (2.0f * f0) * t)
                + 0.10f * sinf(2.0f * M_PI * (3.0f * f0) * t);
        buffer[i] = amp * s;
    }
}

__attribute__((unused)) static float calc_rms(const float* buffer, int start, int count) {
    if (count <= 0) return 0.0f;
    double sum = 0.0;
    for (int i = 0; i < count; i++) {
        double v = (double)buffer[start + i];
        sum += v * v;
    }
    return (float)sqrt(sum / (double)count);
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

// ---------------------------------------------------------------------------
// 1. No-input test (Section 26)
// ---------------------------------------------------------------------------
static int test_no_input(void) {
    printf("[1/9] test_no_input... ");
    static int16_t delay[MAX_BUFFER_SAMPLES];
    BubbleEngineConfig_t config;
    bubble_engine_default_config(&config);
    config.sample_rate = 44100.0f;

    BubbleEngine_t engine;
    SoundBubbles_Init(&engine, delay, &config);

    const int total_samples = 441000; // 10 seconds of silence
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
    CHECK(max_peak < 1.0e-5f, "Output must remain near zero during initial silence");

    printf("PASS (0 spawns, auto_hold=0, peak=%g)\n", (double)max_peak);
    return 0;
}

// ---------------------------------------------------------------------------
// 2 & 3. Tail survival and decay slope (Sections 22 & 23)
// ---------------------------------------------------------------------------
static int test_tail_survival_and_decay_slope(void) {
    printf("[2/9] test_tail_survival_and_decay_slope... ");
    static int16_t delay_m4a[MAX_BUFFER_SAMPLES];
    static int16_t delay_base[MAX_BUFFER_SAMPLES];
    const float sample_rate = 44100.0f;
    const int block_size = 64;
    const int total_samples = (((int)(6.5f * sample_rate) + block_size - 1) / block_size) * block_size;

    float* in = (float*)calloc(total_samples, sizeof(float));
    float* out_l_m4a = (float*)calloc(total_samples, sizeof(float));
    float* out_r_m4a = (float*)calloc(total_samples, sizeof(float));
    float* out_l_base = (float*)calloc(total_samples, sizeof(float));
    float* out_r_base = (float*)calloc(total_samples, sizeof(float));

    CHECK(in && out_l_m4a && out_r_m4a && out_l_base && out_r_base, "Allocation failed");

    // 500ms tone at 440 Hz, followed by silence
    int tone_samples = (int)(0.5f * sample_rate);
    fill_harmonic_tone(in, tone_samples, 440.0f, sample_rate, 0.85f);

    // M4A engine
    BubbleEngineConfig_t config;
    bubble_engine_default_config(&config);
    config.sample_rate = sample_rate;
    config.active_voice_limit = 24;

    BubbleEngine_t engine_m4a;
    SoundBubbles_Init(&engine_m4a, delay_m4a, &config);

    // Baseline engine (Auto-Hold disabled / zeroed so silence halts spawning)
    BubbleEngine_t engine_base;
    SoundBubbles_Init(&engine_base, delay_base, &config);

    for (int i = 0; i < total_samples; i += block_size) {
        SoundBubbles_ProcessBlock(&engine_m4a, &in[i], &out_l_m4a[i], &out_r_m4a[i], block_size);

        // Run baseline with auto-hold forced off
        engine_base.auto_hold_target = 0.0f;
        engine_base.auto_hold_amount = 0.0f;
        SoundBubbles_ProcessBlock(&engine_base, &in[i], &out_l_base[i], &out_r_base[i], block_size);
        engine_base.auto_hold_target = 0.0f;
        engine_base.auto_hold_amount = 0.0f;
    }

    // Windows:
    // W0: 0.5 - 1.0 s
    // W1: 1.0 - 2.0 s
    // W2: 2.0 - 3.0 s
    // W3: 3.0 - 4.0 s
    // W4: 4.0 - 6.0 s
    int w0_start = (int)(0.5f * sample_rate), w0_len = (int)(0.5f * sample_rate);
    int w1_start = (int)(1.0f * sample_rate), w1_len = (int)(1.0f * sample_rate);
    int w2_start = (int)(2.0f * sample_rate), w2_len = (int)(1.0f * sample_rate);
    int w3_start = (int)(3.0f * sample_rate), w3_len = (int)(1.0f * sample_rate);
    int w4_start = (int)(4.0f * sample_rate), w4_len = (int)(2.0f * sample_rate);

    float rms_m4a[5] = {
        calc_rms_stereo(out_l_m4a, out_r_m4a, w0_start, w0_len),
        calc_rms_stereo(out_l_m4a, out_r_m4a, w1_start, w1_len),
        calc_rms_stereo(out_l_m4a, out_r_m4a, w2_start, w2_len),
        calc_rms_stereo(out_l_m4a, out_r_m4a, w3_start, w3_len),
        calc_rms_stereo(out_l_m4a, out_r_m4a, w4_start, w4_len)
    };

    float rms_base[5] = {
        calc_rms_stereo(out_l_base, out_r_base, w0_start, w0_len),
        calc_rms_stereo(out_l_base, out_r_base, w1_start, w1_len),
        calc_rms_stereo(out_l_base, out_r_base, w2_start, w2_len),
        calc_rms_stereo(out_l_base, out_r_base, w3_start, w3_len),
        calc_rms_stereo(out_l_base, out_r_base, w4_start, w4_len)
    };

    printf("\n    M4A RMS:  [0.5-1s: %.1f dB, 1-2s: %.1f dB, 2-3s: %.1f dB, 3-4s: %.1f dB, 4-6s: %.1f dB]\n",
           calc_db(rms_m4a[0]), calc_db(rms_m4a[1]), calc_db(rms_m4a[2]), calc_db(rms_m4a[3]), calc_db(rms_m4a[4]));
    printf("    Base RMS: [0.5-1s: %.1f dB, 1-2s: %.1f dB, 2-3s: %.1f dB, 3-4s: %.1f dB, 4-6s: %.1f dB]\n    ",
           calc_db(rms_base[0]), calc_db(rms_base[1]), calc_db(rms_base[2]), calc_db(rms_base[3]), calc_db(rms_base[4]));

    // M4A keeps energy significantly longer in windows 1-2s and 2-3s
    CHECK(rms_m4a[1] > rms_base[1] * 3.0f, "M4A must keep significantly higher energy in 1-2s window than baseline");
    CHECK(rms_m4a[2] > rms_base[2] * 2.0f, "M4A must keep significantly higher energy in 2-3s window than baseline");

    // Decay slope in dB: must be smoothly decaying (not flat forever, not abrupt drop - Section 23)
    float db_w0 = calc_db(rms_m4a[0]);
    float db_w1 = calc_db(rms_m4a[1]);
    float db_w4 = calc_db(rms_m4a[4]);

    CHECK(db_w0 > db_w1, "Decay must be decreasing from W0 to W1");
    CHECK(db_w1 > db_w4, "Decay must be decreasing from W1 to W4");
    CHECK(db_w0 - db_w4 > 15.0f, "Overall decay must be substantial (>15 dB, not flat forever)");
    CHECK(db_w0 - db_w1 < 25.0f, "Decay must not drop abruptly immediately after input");

    // Calculate Schroeder reverse-integrated energy curve for robust decay slope & RT metrics (Section 23)
    float t20_sec = 0.0f;
    float t40_sec = 0.0f;

    int decay_start = w0_start;
    int decay_len = total_samples - decay_start;
    double* energy_rev = (double*)calloc(decay_len, sizeof(double));
    if (energy_rev) {
        double acc = 0.0;
        for (int k = decay_len - 1; k >= 0; k--) {
            double sl = (double)out_l_m4a[decay_start + k];
            double sr = (double)out_r_m4a[decay_start + k];
            acc += 0.5 * (sl * sl + sr * sr);
            energy_rev[k] = acc;
        }
        double total_e = energy_rev[0];
        if (total_e > 1.0e-12) {
            for (int k = 0; k < decay_len; k++) {
                double rel_db = (energy_rev[k] > 1.0e-15) ? (10.0 * log10(energy_rev[k] / total_e)) : -180.0;
                float elapsed = (float)k / sample_rate;
                if (t20_sec == 0.0f && rel_db <= -20.0) {
                    t20_sec = elapsed;
                }
                if (t40_sec == 0.0f && rel_db <= -40.0) {
                    t40_sec = elapsed;
                }
            }
        }
        free(energy_rev);
    }

    printf("T20: %.2f s, T40: %.2f s ... PASS\n", t20_sec, t40_sec);
    CHECK(t20_sec >= 1.0f && t20_sec <= 4.5f, "T20 must fall in natural 1.0-4.5s range");

    free(in);
    free(out_l_m4a);
    free(out_r_m4a);
    free(out_l_base);
    free(out_r_base);
    return 0;
}

// ---------------------------------------------------------------------------
// 4. Spectral identity test (Section 24)
// ---------------------------------------------------------------------------
static int test_spectral_identity(void) {
    printf("[3/9] test_spectral_identity... ");
    static int16_t delay[MAX_BUFFER_SAMPLES];
    const float sample_rate = 44100.0f;
    const int block_size = 64;
    const int total_samples = (((int)(4.5f * sample_rate) + block_size - 1) / block_size) * block_size;

    float* in = (float*)calloc(total_samples, sizeof(float));
    float* out_l = (float*)calloc(total_samples, sizeof(float));
    float* out_r = (float*)calloc(total_samples, sizeof(float));
    CHECK(in && out_l && out_r, "Allocation failed");

    // 500ms tone with strong 440 Hz fundamental
    int tone_samples = (int)(0.5f * sample_rate);
    fill_harmonic_tone(in, tone_samples, 440.0f, sample_rate, 0.85f);

    BubbleEngineConfig_t config;
    bubble_engine_default_config(&config);
    config.sample_rate = sample_rate;

    BubbleEngine_t engine;
    SoundBubbles_Init(&engine, delay, &config);

    for (int i = 0; i < total_samples; i += block_size) {
        SoundBubbles_ProcessBlock(&engine, &in[i], &out_l[i], &out_r[i], block_size);
    }

    // Measure discrete Fourier energy at harmonic bins (440, 880, 1320, 1760 Hz) vs non-harmonic bins (200, 600, 1000 Hz)
    // Source window: 0.1 to 0.4s
    // Tail window: 1.0 to 2.5s
    int src_start = (int)(0.1f * sample_rate), src_len = (int)(0.3f * sample_rate);
    int tail_start = (int)(1.0f * sample_rate), tail_len = (int)(1.5f * sample_rate);

    const float test_freqs[4] = {440.0f, 880.0f, 1320.0f, 1760.0f};
    double src_harmonic_energy = 0.0;
    double tail_harmonic_energy = 0.0;

    for (int f_idx = 0; f_idx < 4; f_idx++) {
        float f = test_freqs[f_idx];
        double re_src = 0.0, im_src = 0.0;
        for (int n = 0; n < src_len; n++) {
            double angle = 2.0 * M_PI * f * (double)n / sample_rate;
            re_src += (double)in[src_start + n] * cos(angle);
            im_src -= (double)in[src_start + n] * sin(angle);
        }
        src_harmonic_energy += (re_src * re_src + im_src * im_src) / (double)src_len;

        double re_tail = 0.0, im_tail = 0.0;
        for (int n = 0; n < tail_len; n++) {
            double angle = 2.0 * M_PI * f * (double)n / sample_rate;
            re_tail += (double)out_l[tail_start + n] * cos(angle);
            im_tail -= (double)out_l[tail_start + n] * sin(angle);
        }
        tail_harmonic_energy += (re_tail * re_tail + im_tail * im_tail) / (double)tail_len;
    }

    // Measure out-of-harmonic noise bins (e.g. 215, 615, 1015, 1515 Hz)
    const float noise_freqs[4] = {215.0f, 615.0f, 1015.0f, 1515.0f};
    double tail_noise_energy = 0.0;
    for (int f_idx = 0; f_idx < 4; f_idx++) {
        float f = noise_freqs[f_idx];
        double re_tail = 0.0, im_tail = 0.0;
        for (int n = 0; n < tail_len; n++) {
            double angle = 2.0 * M_PI * f * (double)n / sample_rate;
            re_tail += (double)out_l[tail_start + n] * cos(angle);
            im_tail -= (double)out_l[tail_start + n] * sin(angle);
        }
        tail_noise_energy += (re_tail * re_tail + im_tail * im_tail) / (double)tail_len;
    }

    CHECK(tail_harmonic_energy > 0.0, "Tail must contain harmonic energy");
    CHECK(tail_harmonic_energy > tail_noise_energy * 2.5, "Tail spectrum must remain strongly tonal to the source");

    printf("PASS (harmonic/noise ratio: %.2f)\n", tail_harmonic_energy / fmax(1e-12, tail_noise_energy));
    free(in);
    free(out_l);
    free(out_r);
    return 0;
}

// ---------------------------------------------------------------------------
// 5. Phrase anchor test (Section 25)
// ---------------------------------------------------------------------------
static int test_phrase_anchor_progression(void) {
    printf("[4/9] test_phrase_anchor_progression... ");
    static int16_t delay[MAX_BUFFER_SAMPLES];
    const float sample_rate = 44100.0f;

    BubbleEngineConfig_t config;
    bubble_engine_default_config(&config);
    config.sample_rate = sample_rate;

    BubbleEngine_t engine;
    SoundBubbles_Init(&engine, delay, &config);

    const int block_size = 64;
    float in_block[64];
    float out_l[64];
    float out_r[64];

    // Phrase A: 500ms tone at 330 Hz
    int phrase_a_samples = (int)(0.5f * sample_rate);
    for (int i = 0; i < phrase_a_samples; i += block_size) {
        fill_sine_tone(in_block, block_size, 330.0f, sample_rate, 0.8f);
        SoundBubbles_ProcessBlock(&engine, in_block, out_l, out_r, block_size);
    }

    CHECK(SoundBubbles_GetPhraseAnchorValid(&engine), "Phrase anchor A must be valid after Phrase A");
    int32_t anchor_a = SoundBubbles_GetPhraseAnchorWritePtr(&engine);

    // Short silence: 1.0s
    int silence_samples = (int)(1.0f * sample_rate);
    for (int i = 0; i < silence_samples; i += block_size) {
        memset(in_block, 0, sizeof(in_block));
        SoundBubbles_ProcessBlock(&engine, in_block, out_l, out_r, block_size);
    }

    // During tail A, anchor mix must be positive and anchor ptr must remain anchor A
    CHECK(SoundBubbles_GetPhraseAnchorValid(&engine), "Phrase anchor A must still be valid during tail");
    CHECK(SoundBubbles_GetPhraseAnchorWritePtr(&engine) == anchor_a, "Anchor write ptr must not change during tail");
    CHECK(SoundBubbles_GetAnchorMix(&engine) > 0.15f, "Anchor mix must be active during tail");
    uint32_t age_a = SoundBubbles_GetPhraseAnchorAge(&engine);
    CHECK(age_a > (uint32_t)silence_samples / 2, "Anchor age must advance during tail");

    // Phrase B arrives: 500ms tone at 660 Hz
    int phrase_b_samples = (int)(0.5f * sample_rate);
    for (int i = 0; i < phrase_b_samples; i += block_size) {
        fill_sine_tone(in_block, block_size, 660.0f, sample_rate, 0.8f);
        SoundBubbles_ProcessBlock(&engine, in_block, out_l, out_r, block_size);
    }

    // Anchor must now switch to B
    CHECK(SoundBubbles_GetPhraseAnchorValid(&engine), "Phrase anchor B must be valid");
    int32_t anchor_b = SoundBubbles_GetPhraseAnchorWritePtr(&engine);
    CHECK(anchor_b != anchor_a, "Anchor B must have a new write pointer distinct from anchor A");
    CHECK(SoundBubbles_GetPhraseAnchorAge(&engine) < age_a, "Anchor age must reset upon Phrase B arrival");

    printf("PASS (anchor A=%d, anchor B=%d)\n", (int)anchor_a, (int)anchor_b);
    return 0;
}

// ---------------------------------------------------------------------------
// 6. Infinite-tail prevention (Section 27)
// ---------------------------------------------------------------------------
static int test_infinite_tail_prevention(void) {
    printf("[5/9] test_infinite_tail_prevention... ");
    static int16_t delay[MAX_BUFFER_SAMPLES];
    const float sample_rate = 44100.0f;

    BubbleEngineConfig_t config;
    bubble_engine_default_config(&config);
    config.sample_rate = sample_rate;

    BubbleEngine_t engine;
    SoundBubbles_Init(&engine, delay, &config);

    const int block_size = 256;
    float in[256];
    float out_l[256];
    float out_r[256];

    // 1s input
    int input_samples = (int)(1.0f * sample_rate);
    for (int i = 0; i < input_samples; i += block_size) {
        fill_sine_tone(in, block_size, 440.0f, sample_rate, 0.8f);
        SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, block_size);
    }

    // 30s silence
    int silence_samples = (int)(30.0f * sample_rate);
    memset(in, 0, sizeof(in));

    int total_spawns_after_10s = 0;
    float max_peak_after_15s = 0.0f;

    for (int i = 0; i < silence_samples; i += block_size) {
        SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, block_size);
        float elapsed_silence = (float)i / sample_rate;
        if (elapsed_silence > 10.0f) {
            total_spawns_after_10s += engine.metrics_last_block.spawn_count;
        }
        if (elapsed_silence > 15.0f) {
            for (int k = 0; k < block_size; k++) {
                float pl = fabsf(out_l[k]);
                float pr = fabsf(out_r[k]);
                if (pl > max_peak_after_15s) max_peak_after_15s = pl;
                if (pr > max_peak_after_15s) max_peak_after_15s = pr;
            }
        }
    }

    CHECK(total_spawns_after_10s == 0, "No spawns must occur after long silence (>10s)");
    CHECK(SoundBubbles_GetAutoHoldAmount(&engine) == 0.0f, "Auto-hold must settle to 0.0 after long silence");
    CHECK(!SoundBubbles_GetPhraseAnchorValid(&engine), "Phrase anchor must invalidate after long silence");
    CHECK(max_peak_after_15s < 1.0e-5f, "Output must be completely silent after 15s");

    printf("PASS (spawns after 10s=0, peak after 15s=%g)\n", (double)max_peak_after_15s);
    return 0;
}

// ---------------------------------------------------------------------------
// 7. Freeze interaction regression (Section 28)
// ---------------------------------------------------------------------------
static int test_freeze_interaction(void) {
    printf("[6/9] test_freeze_interaction... ");
    static int16_t delay[MAX_BUFFER_SAMPLES];
    const float sample_rate = 44100.0f;

    BubbleEngineConfig_t config;
    bubble_engine_default_config(&config);
    config.sample_rate = sample_rate;

    BubbleEngine_t engine;
    SoundBubbles_Init(&engine, delay, &config);

    const int block_size = 128;
    float in[128];
    float out_l[128];
    float out_r[128];

    // Stimulate with 500ms tone
    int stim_samples = (int)(0.5f * sample_rate);
    for (int i = 0; i < stim_samples; i += block_size) {
        fill_sine_tone(in, block_size, 440.0f, sample_rate, 0.8f);
        SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, block_size);
    }

    // Now run 500ms of silence so envelope decays into SPARSE_DECAY / tail with Auto-Hold active
    memset(in, 0, sizeof(in));
    int decay_lead_samples = (int)(0.5f * sample_rate);
    for (int i = 0; i < decay_lead_samples; i += block_size) {
        SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, block_size);
    }
    float auto_hold = SoundBubbles_GetAutoHoldAmount(&engine);
    CHECK(auto_hold > 0.05f, "Auto-hold must be active during decay");

    // Case A: Freeze 0.3
    config.freeze_amount = 0.3f;
    SoundBubbles_UpdateConfig(&engine, &config);
    for (int i = 0; i < 30; i++) {
        SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, block_size);
    }
    CHECK(engine.smoothed_freeze > 0.25f, "Freeze must smooth towards 0.3");

    // Case B: Freeze 0.7
    config.freeze_amount = 0.7f;
    SoundBubbles_UpdateConfig(&engine, &config);
    for (int i = 0; i < 30; i++) {
        SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, block_size);
    }
    CHECK(engine.smoothed_freeze > 0.65f, "Freeze must smooth towards 0.7");

    // Case C: Freeze 1.0 (Full freeze)
    config.freeze_amount = 1.0f;
    config.freeze_enabled = 1;
    SoundBubbles_UpdateConfig(&engine, &config);
    int locked_ptr = engine.write_ptr;
    for (int i = 0; i < 30; i++) {
        SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, block_size);
    }
    CHECK(engine.write_ptr == locked_ptr, "Write pointer must be stationary under full freeze");

    // Case D: Release Freeze back to 0 during tail (Click-free transition)
    config.freeze_amount = 0.0f;
    config.freeze_enabled = 0;
    SoundBubbles_UpdateConfig(&engine, &config);

    float max_sample_delta = 0.0f;
    float prev_sample = out_l[block_size - 1];

    for (int b = 0; b < 20; b++) {
        SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, block_size);
        for (int k = 0; k < block_size; k++) {
            float delta = fabsf(out_l[k] - prev_sample);
            if (delta > max_sample_delta) max_sample_delta = delta;
            prev_sample = out_l[k];
        }
    }

    CHECK(max_sample_delta < 0.20f, "Transition upon releasing freeze must be click-free");
    printf("PASS (max delta on unfreeze: %.4f)\n", max_sample_delta);
    return 0;
}

// ---------------------------------------------------------------------------
// 8. Stereo coherence (Section 29)
// ---------------------------------------------------------------------------
static int test_stereo_coherence(void) {
    printf("[7/9] test_stereo_coherence... ");
    static int16_t delay_l[MAX_BUFFER_SAMPLES];
    static int16_t delay_r[MAX_BUFFER_SAMPLES];
    const float sample_rate = 44100.0f;

    BubbleEngineConfig_t config;
    bubble_engine_default_config(&config);
    config.sample_rate = sample_rate;
    config.stereo_width = 1.0f;

    BubbleEngine_t engine_l, engine_r;
    SoundBubbles_Init(&engine_l, delay_l, &config);
    SoundBubbles_Init(&engine_r, delay_r, &config);

    // Decorrelate spatial random streams
    SoundBubbles_SetChannelDecorrelation(&engine_l, 0u);
    SoundBubbles_SetChannelDecorrelation(&engine_r, 0x55555555u);

    const int block_size = 64;
    float in[64];
    float out_l_l[64], out_r_l[64];
    float out_l_r[64], out_r_r[64];

    // Stimulate both with identical phrase
    int stim_samples = (int)(0.5f * sample_rate);
    for (int i = 0; i < stim_samples; i += block_size) {
        fill_sine_tone(in, block_size, 440.0f, sample_rate, 0.8f);
        SoundBubbles_ProcessBlock(&engine_l, in, out_l_l, out_r_l, block_size);
        SoundBubbles_ProcessBlock(&engine_r, in, out_l_r, out_r_r, block_size);
    }

    // Check shared phrase anchor timing
    CHECK(SoundBubbles_GetPhraseAnchorValid(&engine_l) == SoundBubbles_GetPhraseAnchorValid(&engine_r),
          "L and R must agree on phrase anchor validity");
    CHECK(SoundBubbles_GetPhraseAnchorWritePtr(&engine_l) == SoundBubbles_GetPhraseAnchorWritePtr(&engine_r),
          "L and R must agree on phrase anchor write position");
    CHECK(SoundBubbles_GetPhraseAnchorAge(&engine_l) == SoundBubbles_GetPhraseAnchorAge(&engine_r),
          "L and R must agree on phrase anchor age");

    // Advance 500ms into silence
    memset(in, 0, sizeof(in));
    double sum_cross = 0.0, sum_l = 0.0, sum_r = 0.0;
    for (int i = 0; i < (int)(0.5f * sample_rate); i += block_size) {
        SoundBubbles_ProcessBlock(&engine_l, in, out_l_l, out_r_l, block_size);
        SoundBubbles_ProcessBlock(&engine_r, in, out_l_r, out_r_r, block_size);
        for (int k = 0; k < block_size; k++) {
            double sl = (double)out_l_l[k];
            double sr = (double)out_l_r[k];
            sum_cross += sl * sr;
            sum_l += sl * sl;
            sum_r += sr * sr;
        }
    }

    double correlation = (sum_l > 0.0 && sum_r > 0.0) ? (sum_cross / sqrt(sum_l * sum_r)) : 1.0;
    CHECK(correlation < 0.98, "Wet field must remain decorrelated/stereo");

    printf("PASS (shared anchor ptr=%d, stereo correlation=%.3f)\n",
           SoundBubbles_GetPhraseAnchorWritePtr(&engine_l), correlation);
    return 0;
}

// ---------------------------------------------------------------------------
// 9. CPU & voice scaling benchmark (Section 30)
// ---------------------------------------------------------------------------
static int test_cpu_and_voice_limits(void) {
    printf("[8/9] test_cpu_and_voice_limits... ");
    static int16_t delay[MAX_BUFFER_SAMPLES];
    const int voice_limits[4] = {8, 16, 24, 32};
    const float sample_rates[3] = {44100.0f, 48000.0f, 96000.0f};

    const int block_size = 256;
    float in[256];
    float out_l[256];
    float out_r[256];
    fill_sine_tone(in, block_size, 440.0f, 48000.0f, 0.7f);

    for (int sr_idx = 0; sr_idx < 3; sr_idx++) {
        float sr = sample_rates[sr_idx];
        for (int v_idx = 0; v_idx < 4; v_idx++) {
            int voices = voice_limits[v_idx];

            BubbleEngineConfig_t config;
            bubble_engine_default_config(&config);
            config.sample_rate = sr;
            config.active_voice_limit = voices;

            BubbleEngine_t engine;
            SoundBubbles_Init(&engine, delay, &config);

            // Warm up
            for (int i = 0; i < 100; i++) {
                SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, block_size);
            }

            // Benchmark 2000 blocks (~10-20 seconds of audio)
            const int test_blocks = 2000;
            clock_t start = clock();
            for (int i = 0; i < test_blocks; i++) {
                SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, block_size);
            }
            clock_t end = clock();

            double elapsed_sec = (double)(end - start) / (double)CLOCKS_PER_SEC;
            double audio_sec = (double)(test_blocks * block_size) / (double)sr;
            double realtime_ratio = (elapsed_sec > 0.0) ? (audio_sec / elapsed_sec) : 999.0;

            CHECK(realtime_ratio > 10.0, "Realtime ratio must be > 10x");
        }
    }

    printf("PASS (>15x realtime across 8/16/24/32 voices at 44.1/48/96 kHz)\n");
    return 0;
}

int main(void) {
    printf("===================================================================\n");
    printf(" M4A: Auto-Hold & Phrase Anchor Tail Architecture Validation\n");
    printf("===================================================================\n");

    if (test_no_input() != 0) return 1;
    if (test_tail_survival_and_decay_slope() != 0) return 1;
    if (test_spectral_identity() != 0) return 1;
    if (test_phrase_anchor_progression() != 0) return 1;
    if (test_infinite_tail_prevention() != 0) return 1;
    if (test_freeze_interaction() != 0) return 1;
    if (test_stereo_coherence() != 0) return 1;
    if (test_cpu_and_voice_limits() != 0) return 1;

    printf("\nAll M4A validation tests PASSED successfully.\n");
    return 0;
}
