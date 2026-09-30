#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdbool.h>
#include <string.h>

#define SOUND_BUBBLES_DSP_INTERNAL 1
#include "engine/bubble_engine.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

#define MAX_BUFFER_SAMPLES 192000

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

int main(int argc, char** argv) {
    const char* out_csv = (argc > 1) ? argv[1] : NULL;
    const char* out_raw = (argc > 2) ? argv[2] : NULL;

    static int16_t delay_buffer[MAX_BUFFER_SAMPLES];
    const float sample_rate = 44100.0f;
    const int block_size = 64;
    const int total_samples = (((int)(12.5f * sample_rate) + block_size - 1) / block_size) * block_size;
    const int tone_samples = (int)(0.5f * sample_rate);

    float* in = (float*)calloc(total_samples, sizeof(float));
    float* out_l = (float*)calloc(total_samples, sizeof(float));
    float* out_r = (float*)calloc(total_samples, sizeof(float));
    if (!in || !out_l || !out_r) return 1;

    fill_harmonic_tone(in, tone_samples, 440.0f, sample_rate, 0.85f);

    BubbleEngineConfig_t config;
    bubble_engine_default_config(&config);
    config.sample_rate = sample_rate;
    config.active_voice_limit = 24;
    config.memory_mix = 0.50f;
    config.sustain_diffusion_enable = 1;
    config.sustain_diffusion_amount = 0.50f;
    config.rng_seed = 42;

    BubbleEngine_t engine;
    SoundBubbles_Init(&engine, delay_buffer, &config);

    for (int i = 0; i < total_samples; i += block_size) {
        SoundBubbles_ProcessBlock(&engine, &in[i], &out_l[i], &out_r[i], block_size);
    }

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

    float rms[6];
    float db[6];
    for (int w = 0; w < 6; w++) {
        rms[w] = calc_rms_stereo(out_l, out_r, w_starts[w], w_lens[w]);
        db[w] = calc_db(rms[w]);
    }

    if (out_csv) {
        FILE* f = fopen(out_csv, "w");
        if (f) {
            fprintf(f, "w0,w1,w2,w3,w4,w5\n");
            fprintf(f, "%.2f,%.2f,%.2f,%.2f,%.2f,%.2f\n",
                    db[0], db[1], db[2], db[3], db[4], db[5]);
            fclose(f);
        }
    }

    if (out_raw) {
        FILE* f = fopen(out_raw, "wb");
        if (f) {
            // Write interleaved float stereo
            for (int i = 0; i < total_samples; i++) {
                fwrite(&out_l[i], sizeof(float), 1, f);
                fwrite(&out_r[i], sizeof(float), 1, f);
            }
            fclose(f);
        }
    }

    printf("W0=%.1f W1=%.1f W2=%.1f W3=%.1f W4=%.1f W5=%.1f dB\n",
           db[0], db[1], db[2], db[3], db[4], db[5]);

    free(in); free(out_l); free(out_r);
    return 0;
}
