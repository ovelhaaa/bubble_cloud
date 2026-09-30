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

// Simple 32-bit FNV-1a hash for delay buffer samples
static uint32_t hash_delay_buffer(const int16_t* buf, int32_t count) {
    uint32_t hash = 2166136261u;
    for (int32_t i = 0; i < count; i++) {
        uint16_t val = (uint16_t)buf[i];
        hash ^= (val & 0xFF);
        hash *= 16777619u;
        hash ^= (val >> 8);
        hash *= 16777619u;
    }
    return hash;
}

static int compare_floats(const void* a, const void* b) {
    float fa = *(const float*)a;
    float fb = *(const float*)b;
    return (fa > fb) - (fa < fb);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <mode> [options]\n", argv[0]);
        return 1;
    }

    const char* mode = argv[1];

    if (strcmp(mode, "--freeze-hash-check") == 0) {
        static int16_t delay[MAX_BUFFER_SAMPLES];
        BubbleEngineConfig_t config;
        bubble_engine_default_config(&config);
        config.sample_rate = 44100.0f;
        config.rng_seed = 42;

        BubbleEngine_t engine;
        SoundBubbles_Init(&engine, delay, &config);
        SoundBubbles_SetFeedbackEnabled(&engine, true);

        const int block_size = 64;
        float in[64], out_l[64], out_r[64];

        // 1. Excite with 300ms tone
        for (int i = 0; i < (int)(0.3f * 44100.0f); i += block_size) {
            for (int k = 0; k < block_size; k++) {
                in[k] = 0.8f * sinf(2.0f * M_PI * 440.0f * (float)(i + k) / 44100.0f);
            }
            SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, block_size);
        }

        // 2. Engage full Freeze = 1.0
        config.freeze_enabled = 1;
        config.freeze_amount = 1.0f;
        SoundBubbles_UpdateRuntimeConfig(&engine, &config);

        int32_t buf_samples = (int32_t)SoundBubbles_RequiredBufferSamples(config.sample_rate);
        int32_t ptr_before = engine.write_ptr;
        uint32_t hash_before = hash_delay_buffer(engine.delay_buffer, buf_samples);

        // Process 1 full second under freeze
        memset(in, 0, sizeof(in));
        for (int i = 0; i < 44100; i += block_size) {
            SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, block_size);
        }

        int32_t ptr_after = engine.write_ptr;
        uint32_t hash_after = hash_delay_buffer(engine.delay_buffer, buf_samples);
        float aperture_during_freeze = SoundBubbles_GetFeedbackWriteAperture(&engine);

        printf("ptr_before=%d ptr_after=%d hash_before=0x%08X hash_after=0x%08X aperture_freeze=%.4f match=%d\n",
               ptr_before, ptr_after, hash_before, hash_after, aperture_during_freeze, (hash_before == hash_after));
        return (hash_before == hash_after && ptr_before == ptr_after) ? 0 : 1;
    }

    if (strcmp(mode, "--freeze-sweep") == 0) {
        printf("freeze,manual_retention,feedback_aperture,feedback_comp,write_locked\n");
        static int16_t delay[MAX_BUFFER_SAMPLES];
        BubbleEngineConfig_t config;
        bubble_engine_default_config(&config);
        config.sample_rate = 44100.0f;

        float freeze_vals[] = {0.0f, 0.25f, 0.50f, 0.75f, 1.0f};
        for (int f_idx = 0; f_idx < 5; f_idx++) {
            float f = freeze_vals[f_idx];
            BubbleEngine_t engine;
            SoundBubbles_Init(&engine, delay, &config);
            SoundBubbles_SetFeedbackEnabled(&engine, true);

            // Prime engine into tail so auto_hold is active
            float in[64], out_l[64], out_r[64];
            for (int i = 0; i < (int)(0.5f * 44100.0f); i += 64) {
                fill_harmonic_tone(in, 64, 440.0f, 44100.0f, 0.85f);
                SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, 64);
            }
            memset(in, 0, sizeof(in));
            for (int i = 0; i < (int)(0.2f * 44100.0f); i += 64) {
                SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, 64);
            }

            // Apply freeze
            config.freeze_enabled = (f > 0.0f) ? 1 : 0;
            config.freeze_amount = f;
            SoundBubbles_UpdateRuntimeConfig(&engine, &config);

            // Process one block to observe write values
            SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, 64);

            float in_c, fb_c, ret_c;
            SoundBubbles_GetLastWriteContributions(&engine, &in_c, &fb_c, &ret_c);
            float ap = SoundBubbles_GetFeedbackWriteAperture(&engine);
            float manual_ret = f * f * (3.0f - 2.0f * f);
            bool locked = (f >= 0.999f);

            printf("%.2f,%.4f,%.4f,%.6f,%d\n", f, manual_ret, ap, fb_c, locked ? 1 : 0);
        }
        return 0;
    }

    if (strcmp(mode, "--autohold-feedback-sweep") == 0) {
        printf("autohold,fb_gain,effective_retention,feedback_aperture,feedback_comp,retained_comp,ring_write_peak,output_rms,feedback_energy\n");
        static int16_t delay[MAX_BUFFER_SAMPLES];
        float ah_vals[] = {0.0f, 0.25f, 0.50f, 0.75f, 0.90f};
        float fb_gains[] = {0.20f, 0.35f, 0.50f, 0.65f};

        for (int a = 0; a < 5; a++) {
            float ah = ah_vals[a];
            for (int g = 0; g < 4; g++) {
                float fg = fb_gains[g];

                BubbleEngineConfig_t config;
                bubble_engine_default_config(&config);
                config.sample_rate = 44100.0f;
                config.memory_mix = 0.50f;

                BubbleEngine_t engine;
                SoundBubbles_Init(&engine, delay, &config);
                SoundBubbles_SetFeedbackEnabled(&engine, true);

                float in[64], out_l[64], out_r[64];
                // Excite with tone
                for (int i = 0; i < (int)(0.5f * 44100.0f); i += 64) {
                    fill_harmonic_tone(in, 64, 440.0f, 44100.0f, 0.85f);
                    SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, 64);
                }

                // Force auto_hold_amount and feedback_gain_target
                engine.auto_hold_amount = ah;
                engine.feedback_gain = fg;
                engine.feedback_gain_target = fg;

                // Process a tail block
                memset(in, 0, sizeof(in));
                SoundBubbles_ProcessBlock(&engine, in, out_l, out_r, 64);

                float in_c, fb_c, ret_c;
                SoundBubbles_GetLastWriteContributions(&engine, &in_c, &fb_c, &ret_c);
                float ap = SoundBubbles_GetFeedbackWriteAperture(&engine);

                float auto_curve = ah * ah * (3.0f - 2.0f * ah);
                float eff_ret = auto_curve * BUBBLES_AUTO_HOLD_MAX_RETENTION;
                float ring_peak = fabsf(fb_c) + fabsf(ret_c);
                float out_rms = calc_rms_stereo(out_l, out_r, 0, 64);
                float fb_e = SoundBubbles_GetFeedbackEnergy(&engine);

                printf("%.2f,%.2f,%.4f,%.4f,%.5f,%.5f,%.5f,%.5f,%.5f\n",
                       ah, fg, eff_ret, ap, fb_c, ret_c, ring_peak, out_rms, fb_e);
            }
        }
        return 0;
    }

    if (strcmp(mode, "--contribution-analysis") == 0) {
        static int16_t delay[MAX_BUFFER_SAMPLES];
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
        int ratio_count = 0;

        float max_in = 0.0f, max_fb = 0.0f, max_ret = 0.0f;

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
            if (fabsf(in_c) > max_in) max_in = fabsf(in_c);
            if (fabsf(fb_c) > max_fb) max_fb = fabsf(fb_c);
            if (fabsf(ret_c) > max_ret) max_ret = fabsf(ret_c);

            if (i >= tone_samples && fabsf(ret_c) > 1.0e-4f && ratio_count < 200000) {
                float r = fabsf(fb_c) / (fabsf(ret_c) + 1.0e-7f);
                ratios[ratio_count++] = r;
            }
        }

        uint32_t softclip_cnt = 0, clamp_cnt = 0;
        SoundBubbles_GetRingSaturationCounts(&engine, &softclip_cnt, &clamp_cnt);

        float median_r = 0.0f, p95_r = 0.0f, max_r = 0.0f;
        if (ratio_count > 0) {
            qsort(ratios, ratio_count, sizeof(float), compare_floats);
            median_r = ratios[ratio_count / 2];
            p95_r = ratios[(int)(ratio_count * 0.95f)];
            max_r = ratios[ratio_count - 1];
        }

        printf("max_in=%.4f max_fb=%.4f max_ret=%.4f median_ratio=%.4f p95_ratio=%.4f max_ratio=%.4f softclip=%u clamp=%u\n",
               max_in, max_fb, max_ret, median_r, p95_r, max_r, softclip_cnt, clamp_cnt);

        free(ratios);
        return 0;
    }

    if (strcmp(mode, "--int16-deadzone") == 0) {
        printf("dbfs,input_amp,tail_w1_db,tail_w2_db,tail_w3_db,feedback_survived\n");
        float test_db[] = {-60.0f, -72.0f, -84.0f, -90.0f};

        for (int d = 0; d < 4; d++) {
            float db = test_db[d];
            float amp = powf(10.0f, db / 20.0f);

            static int16_t delay[MAX_BUFFER_SAMPLES];
            BubbleEngineConfig_t config;
            bubble_engine_default_config(&config);
            config.sample_rate = 44100.0f;
            config.memory_mix = 0.50f;

            BubbleEngine_t engine;
            SoundBubbles_Init(&engine, delay, &config);
            SoundBubbles_SetFeedbackEnabled(&engine, true);

            const int total_samples = (int)(6.0f * 44100.0f);
            const int tone_samples = (int)(0.5f * 44100.0f);
            const int block_size = 64;

            float* in = (float*)calloc(total_samples, sizeof(float));
            float* out_l = (float*)calloc(total_samples, sizeof(float));
            float* out_r = (float*)calloc(total_samples, sizeof(float));

            for (int i = 0; i < tone_samples; i++) {
                float t = (float)i / 44100.0f;
                in[i] = amp * sinf(2.0f * M_PI * 440.0f * t);
            }

            for (int i = 0; i < total_samples; i += block_size) {
                SoundBubbles_ProcessBlock(&engine, &in[i], &out_l[i], &out_r[i], block_size);
            }

            float w1 = calc_rms_stereo(out_l, out_r, (int)(1.0f * 44100.0f), (int)(1.0f * 44100.0f));
            float w2 = calc_rms_stereo(out_l, out_r, (int)(2.0f * 44100.0f), (int)(2.0f * 44100.0f));
            float w3 = calc_rms_stereo(out_l, out_r, (int)(4.0f * 44100.0f), (int)(2.0f * 44100.0f));

            bool survived = (w2 > 1.0e-7f);
            printf("%.1f,%g,%.1f,%.1f,%.1f,%d\n",
                   db, (double)amp, calc_db(w1), calc_db(w2), calc_db(w3), survived ? 1 : 0);

            free(in); free(out_l); free(out_r);
        }
        return 0;
    }

    if (strcmp(mode, "--render-raw") == 0) {
        if (argc < 4) return 1;
        const char* target = argv[2]; // "m4a" or "m4b"
        const char* out_path = argv[3];

        static int16_t delay[MAX_BUFFER_SAMPLES];
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
        SoundBubbles_SetFeedbackEnabled(&engine, (strcmp(target, "m4b") == 0));

        const int total_samples = (int)(10.0f * 44100.0f);
        const int tone_samples = (int)(0.5f * 44100.0f);
        const int block_size = 64;

        float* in = (float*)calloc(total_samples, sizeof(float));
        float* out_l = (float*)calloc(total_samples, sizeof(float));
        float* out_r = (float*)calloc(total_samples, sizeof(float));

        fill_harmonic_tone(in, tone_samples, 440.0f, 44100.0f, 0.85f);

        for (int i = 0; i < total_samples; i += block_size) {
            SoundBubbles_ProcessBlock(&engine, &in[i], &out_l[i], &out_r[i], block_size);
        }

        FILE* f = fopen(out_path, "wb");
        if (f) {
            for (int i = 0; i < total_samples; i++) {
                fwrite(&out_l[i], sizeof(float), 1, f);
            }
            fclose(f);
        }
        free(in); free(out_l); free(out_r);
        printf("Rendered %s to %s\n", target, out_path);
        return 0;
    }

    if (strcmp(mode, "--sweep-safety-matrix") == 0) {
        printf("threshold,scenario,max_fb_energy,max_fb_gain,avg_fb_gain,peak,limiter_gr_db,rms_w1_db,rms_w3_db,rms_w4_db,decay_s,safety_pct\n");

        float thresholds[] = {0.20f, 0.28f, 0.40f, 0.60f};
        const char* scenarios[] = {"normal tail", "high MEMORY", "high BLOOM", "high DENSITY", "WEB_ULTRA"};

        for (int t = 0; t < 4; t++) {
            float th = thresholds[t];
            float knee_width = 0.25f * th;
            float knee_start = th - knee_width;

            for (int s = 0; s < 5; s++) {
                const char* sc = scenarios[s];

                static int16_t delay[MAX_BUFFER_SAMPLES];
                BubbleEngineConfig_t config;
                bubble_engine_default_config(&config);
                config.sample_rate = 44100.0f;
                config.active_voice_limit = 24;
                config.memory_mix = 0.50f;
                config.sustain_diffusion_enable = 1;
                config.sustain_diffusion_amount = 0.50f;
                config.rng_seed = 42;

                float total_s = 12.5f;
                float tone_s = 0.5f;

                if (strcmp(sc, "high MEMORY") == 0) {
                    config.memory_mix = 0.85f;
                } else if (strcmp(sc, "high BLOOM") == 0) {
                    config.sustain_diffusion_amount = 0.85f;
                } else if (strcmp(sc, "high DENSITY") == 0) {
                    config.density_sustain = 60.0f;
                    config.burst_immediate_count = 8;
                } else if (strcmp(sc, "WEB_ULTRA") == 0) {
                    config.quality_profile = BUBBLE_QUALITY_PROFILE_WEB_ULTRA;
                    config.active_voice_limit = 32;
                    config.memory_mix = 0.85f;
                    config.sustain_diffusion_amount = 0.85f;
                    config.density_sustain = 60.0f;
                    config.burst_immediate_count = 8;
                    total_s = 20.0f;
                    tone_s = 1.0f;
                }

                BubbleEngine_t engine;
                SoundBubbles_Init(&engine, delay, &config);
                SoundBubbles_SetFeedbackEnabled(&engine, true);
                SoundBubbles_SetFeedbackSafetyThreshold(&engine, th);

                const int block_size = 64;
                const int total_samples = (((int)(total_s * 44100.0f) + block_size - 1) / block_size) * block_size;
                const int tone_samples = (int)(tone_s * 44100.0f);

                float* in = (float*)calloc(total_samples, sizeof(float));
                float* out_l = (float*)calloc(total_samples, sizeof(float));
                float* out_r = (float*)calloc(total_samples, sizeof(float));

                if (strcmp(sc, "WEB_ULTRA") == 0) {
                    for (int i = 0; i < tone_samples; i++) {
                        float time = (float)i / 44100.0f;
                        in[i] = 0.95f * sinf(2.0f * M_PI * 440.0f * time);
                    }
                } else {
                    fill_harmonic_tone(in, tone_samples, 440.0f, 44100.0f, 0.85f);
                }

                float max_fb_e = 0.0f;
                float max_fb_g = 0.0f;
                double sum_fb_g = 0.0;
                int fb_g_count = 0;
                float peak = 0.0f;
                float min_limiter_gain = 1.0f;
                int safety_active_blocks = 0;
                int total_tail_blocks = 0;
                float last_audible_sample = 0.0f;

                for (int i = 0; i < total_samples; i += block_size) {
                    SoundBubbles_ProcessBlock(&engine, &in[i], &out_l[i], &out_r[i], block_size);

                    if (engine.final_limiter_gain < min_limiter_gain) {
                        min_limiter_gain = engine.final_limiter_gain;
                    }

                    bool in_tail = (i >= tone_samples);
                    if (in_tail) {
                        total_tail_blocks++;
                        float fe = engine.feedback_energy;
                        if (fe > max_fb_e) max_fb_e = fe;
                        if (fe > knee_start) {
                            safety_active_blocks++;
                        }

                        float fg = engine.feedback_gain;
                        if (fg > max_fb_g) max_fb_g = fg;
                        sum_fb_g += fg;
                        fb_g_count++;
                    }

                    for (int k = 0; k < block_size; k++) {
                        float pl = fabsf(out_l[i + k]);
                        float pr = fabsf(out_r[i + k]);
                        float p = (pl > pr) ? pl : pr;
                        if (p > peak) peak = p;
                        if (p > 1.0e-3f) { // > -60 dBFS
                            last_audible_sample = (float)(i + k);
                        }
                    }
                }

                float avg_fb_g = (fb_g_count > 0) ? (float)(sum_fb_g / fb_g_count) : 0.0f;
                float safety_pct = (total_tail_blocks > 0) ? ((float)safety_active_blocks / (float)total_tail_blocks * 100.0f) : 0.0f;
                float decay_s = last_audible_sample / 44100.0f;
                float limiter_gr_db = (min_limiter_gain > 1.0e-6f) ? 20.0f * log10f(min_limiter_gain) : -180.0f;

                float rms_w1 = calc_rms_stereo(out_l, out_r, (int)(1.0f * 44100.0f), (int)(1.0f * 44100.0f));
                float rms_w3 = calc_rms_stereo(out_l, out_r, (int)(4.0f * 44100.0f), (int)(2.0f * 44100.0f));
                float rms_w4 = calc_rms_stereo(out_l, out_r, (int)(6.0f * 44100.0f), (int)(2.0f * 44100.0f));

                printf("%.2f,\"%s\",%.4f,%.4f,%.4f,%.4f,%.2f,%.1f,%.1f,%.1f,%.2f,%.1f\n",
                       th, sc, max_fb_e, max_fb_g, avg_fb_g, peak, limiter_gr_db,
                       calc_db(rms_w1), calc_db(rms_w3), calc_db(rms_w4), decay_s, safety_pct);

                free(in); free(out_l); free(out_r);
            }
        }
        return 0;
    }

    fprintf(stderr, "Unknown mode: %s\n", mode);
    return 1;
}
