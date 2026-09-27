#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#endif

#include "engine/bubble_engine.h"

static double get_time_us(void) {
#ifdef _WIN32
    static LARGE_INTEGER freq;
    static int initialized = 0;
    if (!initialized) {
        QueryPerformanceFrequency(&freq);
        initialized = 1;
    }
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return ((double)counter.QuadPart * 1000000.0) / (double)freq.QuadPart;
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec * 1000000.0 + (double)tv.tv_usec;
#endif
}

// WAV File Writer helper
static void write_wav_file(const char* filepath, const float* out_l, const float* out_r, int total_samples, int sample_rate) {
    FILE* fp = fopen(filepath, "wb");
    if (!fp) {
        fprintf(stderr, "Could not open %s for writing\n", filepath);
        return;
    }

    int32_t data_bytes = total_samples * 2 * (int32_t)sizeof(int16_t);
    int32_t riff_bytes = 36 + data_bytes;
    int16_t num_channels = 2;
    int32_t sr = sample_rate;
    int16_t bits_per_sample = 16;
    int32_t byte_rate = sr * num_channels * (bits_per_sample / 8);
    int16_t block_align = num_channels * (bits_per_sample / 8);
    int16_t audio_format = 1; // PCM
    int32_t fmt_size = 16;

    fwrite("RIFF", 1, 4, fp);
    fwrite(&riff_bytes, 4, 1, fp);
    fwrite("WAVE", 1, 4, fp);
    fwrite("fmt ", 1, 4, fp);
    fwrite(&fmt_size, 4, 1, fp);
    fwrite(&audio_format, 2, 1, fp);
    fwrite(&num_channels, 2, 1, fp);
    fwrite(&sr, 4, 1, fp);
    fwrite(&byte_rate, 4, 1, fp);
    fwrite(&block_align, 2, 1, fp);
    fwrite(&bits_per_sample, 2, 1, fp);
    fwrite("data", 1, 4, fp);
    fwrite(&data_bytes, 4, 1, fp);

    int16_t sample_pair[2];
    for (int i = 0; i < total_samples; i++) {
        float l = out_l[i];
        float r = out_r[i];
        if (l > 1.0f) l = 1.0f; else if (l < -1.0f) l = -1.0f;
        if (r > 1.0f) r = 1.0f; else if (r < -1.0f) r = -1.0f;
        sample_pair[0] = (int16_t)(l * 32767.0f);
        sample_pair[1] = (int16_t)(r * 32767.0f);
        fwrite(sample_pair, sizeof(int16_t), 2, fp);
    }
    fclose(fp);
}

// Simple WAV reader for 16-bit PCM (mono or stereo)
static float* load_wav_mono(const char* filepath, int* out_num_samples, int* out_sample_rate) {
    FILE* fp = fopen(filepath, "rb");
    if (!fp) {
        fprintf(stderr, "Error: Could not open WAV file: %s\n", filepath);
        return NULL;
    }

    char id[5] = {0};
    if (fread(id, 1, 4, fp) != 4 || memcmp(id, "RIFF", 4) != 0) {
        fclose(fp);
        return NULL;
    }
    fseek(fp, 8, SEEK_SET);
    if (fread(id, 1, 4, fp) != 4 || memcmp(id, "WAVE", 4) != 0) {
        fclose(fp);
        return NULL;
    }

    int16_t num_channels = 0;
    int32_t sample_rate = 0;
    int16_t bits_per_sample = 0;
    int32_t data_bytes = 0;
    long data_pos = 0;

    // Scan chunks
    while (!feof(fp)) {
        char chunk_id[4];
        uint32_t chunk_size = 0;
        if (fread(chunk_id, 1, 4, fp) != 4) break;
        if (fread(&chunk_size, 4, 1, fp) != 1) break;

        if (memcmp(chunk_id, "fmt ", 4) == 0) {
            int16_t format_tag = 0;
            fread(&format_tag, 2, 1, fp);
            fread(&num_channels, 2, 1, fp);
            fread(&sample_rate, 4, 1, fp);
            fseek(fp, 6, SEEK_CUR); // skip byte_rate and block_align
            fread(&bits_per_sample, 2, 1, fp);
            int remaining = (int)chunk_size - 16;
            if (remaining > 0) fseek(fp, remaining, SEEK_CUR);
        } else if (memcmp(chunk_id, "data", 4) == 0) {
            data_bytes = (int32_t)chunk_size;
            data_pos = ftell(fp);
            fseek(fp, chunk_size, SEEK_CUR);
        } else {
            fseek(fp, chunk_size, SEEK_CUR);
        }
    }

    if (num_channels <= 0 || sample_rate <= 0 || bits_per_sample != 16 || data_bytes <= 0 || data_pos == 0) {
        fprintf(stderr, "Error: Unsupported WAV format (ch=%d, sr=%d, bits=%d, data=%d)\n",
                num_channels, sample_rate, bits_per_sample, data_bytes);
        fclose(fp);
        return NULL;
    }

    fseek(fp, data_pos, SEEK_SET);
    int total_frames = data_bytes / (num_channels * sizeof(int16_t));
    float* mono = (float*)malloc(total_frames * sizeof(float));
    int16_t* raw = (int16_t*)malloc(total_frames * num_channels * sizeof(int16_t));
    fread(raw, sizeof(int16_t), total_frames * num_channels, fp);
    fclose(fp);

    for (int i = 0; i < total_frames; i++) {
        if (num_channels == 1) {
            mono[i] = (float)raw[i] * (1.0f / 32768.0f);
        } else {
            mono[i] = ((float)raw[i * 2] + (float)raw[i * 2 + 1]) * (0.5f / 32768.0f);
        }
    }
    free(raw);

    *out_num_samples = total_frames;
    *out_sample_rate = sample_rate;
    return mono;
}

int main(int argc, char** argv) {
    const char* input_wav = (argc > 1) ? argv[1] : "tests/fixtures/audio/farran_ez-soft-indie-guitar-sample-456142.wav";
    const char* output_dir = (argc > 2) ? argv[2] : "build_check/m3_qualification";

    int total_frames = 0;
    int sample_rate = 0;
    float* input_mono = load_wav_mono(input_wav, &total_frames, &sample_rate);
    if (!input_mono) {
        fprintf(stderr, "Failed to load input wav: %s\n", input_wav);
        return 1;
    }

    printf("Loaded %s: %d frames, %d Hz (%.2f seconds)\n",
           input_wav, total_frames, sample_rate, (double)total_frames / (double)sample_rate);

    const float freeze_levels[5] = {0.0f, 0.25f, 0.50f, 0.75f, 1.0f};
    static int16_t delay_buffer[192000];

    float* out_l = (float*)malloc(total_frames * sizeof(float));
    float* out_r = (float*)malloc(total_frames * sizeof(float));

    printf("\n=== M3 Freeze Perceptual Qualification Matrix ===\n");
    printf("%-8s | %-10s | %-10s | %-12s | %-12s | %-10s | %-14s | %-12s | %-10s\n",
           "FREEZE", "RMS (dBFS)", "Peak (dBFS)", "Corr (L/R)", "Side/Mid(dB)", "ActVoices", "Retention(%)", "Renewal(%)", "LimiterGR(dB)");
    printf("---------------------------------------------------------------------------------------------------------------------------------\n");

    for (int lvl = 0; lvl < 5; lvl++) {
        float f = freeze_levels[lvl];

        memset(delay_buffer, 0, sizeof(delay_buffer));
        BubbleEngineConfig_t config;
        bubble_engine_default_config(&config);
        config.sample_rate = (float)sample_rate;

        BubbleEngine_t engine;
        bubble_engine_init(&engine, delay_buffer, &config);
        bubble_engine_set_parameter(&engine, BUBBLE_PARAM_FREEZE, f);

        double sum_l2 = 0.0, sum_r2 = 0.0, sum_lr = 0.0;
        double sum_m2 = 0.0, sum_s2 = 0.0;
        float peak_abs = 0.0f;
        double sum_active_voices = 0.0;
        int total_blocks = 0;
        float min_limiter_gain = 1.0f;

        double t_start = get_time_us();

        int processed = 0;
        while (processed < total_frames) {
            int chunk = total_frames - processed;
            if (chunk > BUBBLES_BLOCK_SIZE) chunk = BUBBLES_BLOCK_SIZE;

            bubble_engine_process(&engine, &input_mono[processed], &out_l[processed], &out_r[processed], chunk);

            for (int i = 0; i < chunk; i++) {
                float l = out_l[processed + i];
                float r = out_r[processed + i];
                float al = fabsf(l);
                float ar = fabsf(r);
                if (al > peak_abs) peak_abs = al;
                if (ar > peak_abs) peak_abs = ar;

                sum_l2 += (double)(l * l);
                sum_r2 += (double)(r * r);
                sum_lr += (double)(l * r);

                // Mid/Side: M = (L+R)/sqrt(2), S = (L-R)/sqrt(2)
                float m = (l + r) * 0.70710678f;
                float s = (l - r) * 0.70710678f;
                sum_m2 += (double)(m * m);
                sum_s2 += (double)(s * s);
            }

            sum_active_voices += (double)engine.metrics_last_block.active_voices;
            if (engine.metrics_last_block.limiter_gain < min_limiter_gain) {
                min_limiter_gain = engine.metrics_last_block.limiter_gain;
            }
            total_blocks++;
            processed += chunk;
        }

        double t_end = get_time_us();
        double elapsed_us = t_end - t_start;
        double avg_block_us = elapsed_us / (double)total_blocks;

        // Metrics calculations
        double mean_sq = (sum_l2 + sum_r2) / (2.0 * (double)total_frames);
        double rms_dbfs = (mean_sq > 1e-12) ? (10.0 * log10(mean_sq)) : -120.0;
        double peak_dbfs = (peak_abs > 1e-6f) ? (20.0 * log10((double)peak_abs)) : -120.0;

        double denom = sqrt(sum_l2 * sum_r2);
        double corr_lr = (denom > 1e-12) ? (sum_lr / denom) : 1.0;

        double side_mid_db = (sum_m2 > 1e-12 && sum_s2 > 1e-12) ? (10.0 * log10(sum_s2 / sum_m2)) : -60.0;
        double avg_voices = (total_blocks > 0) ? (sum_active_voices / (double)total_blocks) : 0.0;

        float retention = f * f * (3.0f - 2.0f * f);
        float renewal = 1.0f - retention;
        float limiter_gr_db = (min_limiter_gain < 1.0f && min_limiter_gain > 0.0f)
                              ? (float)(-20.0 * log10(min_limiter_gain)) : 0.0f;

        printf("%-8.2f | %-10.2f | %-10.2f | %-12.4f | %-12.2f | %-10.1f | %-14.1f | %-12.1f | %-10.2f  (%.2f us/block)\n",
               (double)f, rms_dbfs, peak_dbfs, corr_lr, side_mid_db, avg_voices,
               (double)(retention * 100.0f), (double)(renewal * 100.0f), (double)limiter_gr_db, avg_block_us);

        // Write output WAV file
        char out_wav_path[512];
        snprintf(out_wav_path, sizeof(out_wav_path), "%s/freeze_%.2f.wav", output_dir, (double)f);
        write_wav_file(out_wav_path, out_l, out_r, total_frames, sample_rate);
    }

    free(input_mono);
    free(out_l);
    free(out_r);
    return 0;
}
