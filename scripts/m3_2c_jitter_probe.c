#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#endif

#define SOUND_BUBBLES_DSP_INTERNAL 1

#ifndef IS_CANDIDATE_BUILD
#if defined(BUBBLES_M3_ONSET_TRACE)
#define IS_CANDIDATE_BUILD 1
#else
#define IS_CANDIDATE_BUILD 0
#endif
#endif

#include "dsp/sound_bubbles_dsp.c"
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

#define MAX_SPAWNS 100000

typedef struct {
    uint32_t sample_offset;
    uint32_t onset_delay;
    uint32_t source;
    uint32_t tick;
} SpawnEvent;

static SpawnEvent g_spawns[MAX_SPAWNS];
static int g_spawn_count = 0;
static uint32_t g_current_block_sample = 0;

#if defined(BUBBLES_M3_ONSET_TRACE)
static void probe_onset_trace_cb(void* user, const SoundBubblesEngine_t* engine,
                                SharedSpawnId_t id, uint32_t onset_delay_samples) {
    (void)user;
    (void)engine;
    if (g_spawn_count >= MAX_SPAWNS) return;
    SpawnEvent* ev = &g_spawns[g_spawn_count++];
    ev->sample_offset = g_current_block_sample + onset_delay_samples;
    ev->onset_delay = onset_delay_samples;
    ev->source = id.source;
    ev->tick = id.tick;
}
#endif

static void reset_spawns(void) {
    g_spawn_count = 0;
    g_current_block_sample = 0;
}

static void run_simultaneity_scenario(const char* scenario_name, const char* out_csv_path) {
    static int16_t delay[192000];
    EngineConfig_t cfg;
    bubble_engine_default_config(&cfg);
    cfg.sample_rate = 48000.0f;
    cfg.rng_seed = 0x51E5D17u;
    cfg.smart_start_enable = 0;
    cfg.active_voice_limit = 32;

    if (strcmp(scenario_name, "high_density") == 0) {
        cfg.density_burst = 400.0f;
        cfg.density_sustain = 300.0f;
        cfg.burst_immediate_count = 2;
        cfg.burst_mode = BUBBLE_BURST_MODE_SPRAY;
    } else if (strcmp(scenario_name, "spray") == 0) {
        cfg.density_burst = 250.0f;
        cfg.density_sustain = 150.0f;
        cfg.burst_immediate_count = 6;
        cfg.burst_mode = BUBBLE_BURST_MODE_SPRAY;
    } else if (strcmp(scenario_name, "swarm") == 0) {
        cfg.density_burst = 250.0f;
        cfg.density_sustain = 150.0f;
        cfg.burst_immediate_count = 6;
        cfg.burst_mode = BUBBLE_BURST_MODE_SWARM;
    }

    memset(delay, 0, sizeof(delay));
    BubbleEngine_t engine;
    bubble_engine_init(&engine, delay, &cfg);
    engine.macro_dirty_mask = 0u;
    engine.motion_base_config.motion_depth = 0.0f;
    engine.config.motion_depth = 0.0f;

    reset_spawns();

#if defined(BUBBLES_M3_ONSET_TRACE)
    BubblesTest_SetOnsetTrace(probe_onset_trace_cb, NULL);
#endif

    const int total_samples = 48000 * 3; // 3 seconds
    static float in_audio[BUBBLES_BLOCK_SIZE];
    static float out_l[BUBBLES_BLOCK_SIZE];
    static float out_r[BUBBLES_BLOCK_SIZE];

    for (int offset = 0; offset < total_samples; offset += BUBBLES_BLOCK_SIZE) {
        g_current_block_sample = (uint32_t)offset;
        for (int i = 0; i < BUBBLES_BLOCK_SIZE; i++) {
            int n = offset + i;
            in_audio[i] = 0.35f * sinf(2.0f * 3.14159265f * 180.0f * (float)n / 48000.0f);
            if ((n % 1200) == 0) in_audio[i] += 0.7f;
        }

        bubble_engine_process(&engine, in_audio, out_l, out_r, BUBBLES_BLOCK_SIZE);

#if !defined(BUBBLES_M3_ONSET_TRACE)
        // Baseline: all spawns occurred at sample offset 0 of this block
        int spawns_in_block = engine.metrics_last_block.spawn_count;
        for (int s = 0; s < spawns_in_block; s++) {
            if (g_spawn_count < MAX_SPAWNS) {
                SpawnEvent* ev = &g_spawns[g_spawn_count++];
                ev->sample_offset = (uint32_t)offset;
                ev->onset_delay = 0;
                ev->source = 0;
                ev->tick = (uint32_t)(offset / BUBBLES_BLOCK_SIZE);
            }
        }
#endif
    }

#if defined(BUBBLES_M3_ONSET_TRACE)
    BubblesTest_SetOnsetTrace(NULL, NULL);
#endif

    // Compute metrics
    int total_spawns = g_spawn_count;
    int same_sample_pairs = 0;
    int max_simultaneous = 1;
    int zero_distance_count = 0;

    int offset_histogram[32];
    memset(offset_histogram, 0, sizeof(offset_histogram));

    int ioi_bins[7] = {0}; // 0, 1-7, 8-15, 16-31, 32-63, 64-127, 128+

    // Sort by sample offset if needed (already monotonic in block order)
    for (int i = 0; i < total_spawns; i++) {
        offset_histogram[g_spawns[i].onset_delay & 31u]++;

        // Find consecutive events on same sample
        int sim = 1;
        while (i + 1 < total_spawns && g_spawns[i + 1].sample_offset == g_spawns[i].sample_offset) {
            sim++;
            same_sample_pairs++;
            zero_distance_count++;
            i++;
        }
        if (sim > max_simultaneous) max_simultaneous = sim;

        if (i + 1 < total_spawns) {
            uint32_t diff = g_spawns[i + 1].sample_offset - g_spawns[i].sample_offset;
            if (diff == 0) ioi_bins[0]++;
            else if (diff < 8) ioi_bins[1]++;
            else if (diff < 16) ioi_bins[2]++;
            else if (diff < 32) ioi_bins[3]++;
            else if (diff < 64) ioi_bins[4]++;
            else if (diff < 128) ioi_bins[5]++;
            else ioi_bins[6]++;
        }
    }

    double zero_distance_pct = (total_spawns > 1) ? (100.0 * (double)zero_distance_count / (double)(total_spawns - 1)) : 0.0;
    int tick_boundary_events = offset_histogram[0];
    double boundary_concentration = (total_spawns > 0) ? (100.0 * (double)tick_boundary_events / (double)total_spawns) : 0.0;

    FILE* fp = fopen(out_csv_path, "a");
    if (!fp) {
        fprintf(stderr, "Could not open %s for writing\n", out_csv_path);
        return;
    }
    // format: scenario,tier,total_spawns,same_sample_count,max_simultaneous,zero_distance_pct,boundary_concentration_pct,offset0,offset1,offset2,...
    fprintf(fp, "%s,%s,%d,%d,%d,%.2f,%.2f",
            scenario_name,
            IS_CANDIDATE_BUILD ? "jitter" : "baseline",
            total_spawns, same_sample_pairs, max_simultaneous,
            zero_distance_pct, boundary_concentration);
    for (int b = 0; b < 32; b++) {
        fprintf(fp, ",%d", offset_histogram[b]);
    }
    for (int b = 0; b < 7; b++) {
        fprintf(fp, ",%d", ioi_bins[b]);
    }
    fprintf(fp, "\n");
    fclose(fp);

    printf("[%s - %s] spawns=%d same_sample=%d max_sim=%d zero_dist=%.2f%% boundary_conc=%.2f%%\n",
           scenario_name, IS_CANDIDATE_BUILD ? "jitter" : "baseline",
           total_spawns, same_sample_pairs, max_simultaneous,
           zero_distance_pct, boundary_concentration);
}

static void run_cpu_benchmark(const char* out_csv_path) {
    const int voice_limits[4] = {8, 16, 24, 32};
    const float sample_rates[3] = {44100.0f, 48000.0f, 96000.0f};
    const BubbleQualityProfile profiles[2] = {BUBBLE_QUALITY_PROFILE_MCU_SAFE, BUBBLE_QUALITY_PROFILE_WEB_ULTRA};
    const char* profile_names[2] = {"ECO_LINEAR", "PRISTINE_HERMITE"};

    static int16_t delay[192000];
    const int blocks = 4096;
    const int total_samples = blocks * BUBBLES_BLOCK_SIZE;

    static float in_audio[BUBBLES_BLOCK_SIZE];
    static float out_l[BUBBLES_BLOCK_SIZE];
    static float out_r[BUBBLES_BLOCK_SIZE];

    FILE* fp = fopen(out_csv_path, "a");
    if (!fp) {
        fprintf(stderr, "Could not open %s\n", out_csv_path);
        return;
    }

    for (int p = 0; p < 2; p++) {
        for (int sr_idx = 0; sr_idx < 3; sr_idx++) {
            float sr = sample_rates[sr_idx];
            for (int v_idx = 0; v_idx < 4; v_idx++) {
                int voices = voice_limits[v_idx];

                EngineConfig_t cfg;
                bubble_engine_default_config(&cfg);
                cfg.sample_rate = sr;
                cfg.quality_profile = profiles[p];
                cfg.active_voice_limit = voices;
                cfg.density_burst = 250.0f;
                cfg.density_sustain = 150.0f;
                cfg.burst_immediate_count = 4;
                cfg.burst_mode = BUBBLE_BURST_MODE_SPRAY;

                memset(delay, 0, sizeof(delay));
                BubbleEngine_t engine;
                bubble_engine_init(&engine, delay, &cfg);
                engine.macro_dirty_mask = 0u;
                engine.motion_base_config.motion_depth = 0.0f;
                engine.config.motion_depth = 0.0f;

                // Warm up
                for (int b = 0; b < 64; b++) {
                    for (int i = 0; i < BUBBLES_BLOCK_SIZE; i++) {
                        in_audio[i] = 0.25f * sinf(2.0f * 3.14159265f * 220.0f * (float)i / sr);
                    }
                    bubble_engine_process(&engine, in_audio, out_l, out_r, BUBBLES_BLOCK_SIZE);
                }

                double t_start = get_time_us();
                for (int b = 0; b < blocks; b++) {
                    for (int i = 0; i < BUBBLES_BLOCK_SIZE; i++) {
                        in_audio[i] = 0.25f * sinf(2.0f * 3.14159265f * 220.0f * (float)(b * BUBBLES_BLOCK_SIZE + i) / sr);
                    }
                    bubble_engine_process(&engine, in_audio, out_l, out_r, BUBBLES_BLOCK_SIZE);
                }
                double t_end = get_time_us();

                double elapsed_us = t_end - t_start;
                double elapsed_sec = elapsed_us / 1000000.0;
                double audio_sec = (double)total_samples / (double)sr;
                double speed_realtime = audio_sec / elapsed_sec;
                double avg_block_us = elapsed_us / (double)blocks;
                double avg_sample_ns = (elapsed_us * 1000.0) / (double)total_samples;

                fprintf(fp, "%s,%s,%d,%.0f,%s,%.4f,%.2f,%.3f,%.2f\n",
                        IS_CANDIDATE_BUILD ? "jitter" : "baseline",
                        profile_names[p],
                        voices,
                        sr,
                        profile_names[p],
                        elapsed_sec * 1000.0,
                        speed_realtime,
                        avg_block_us,
                        avg_sample_ns);

                printf("CPU [%s] %-16s voices=%2d sr=%5.0f: speed=%6.1fx avg_block=%.2fus avg_sample=%.1fns\n",
                       IS_CANDIDATE_BUILD ? "jitter" : "baseline",
                       profile_names[p], voices, sr,
                       speed_realtime, avg_block_us, avg_sample_ns);
            }
        }
    }
    fclose(fp);
}

int main(int argc, char** argv) {
    if (argc >= 4 && strcmp(argv[1], "--simultaneity") == 0) {
        run_simultaneity_scenario(argv[3], argv[2]);
        return 0;
    } else if (argc >= 3 && strcmp(argv[1], "--cpu-benchmark") == 0) {
        run_cpu_benchmark(argv[2]);
        return 0;
    }
    printf("Usage:\n");
    printf("  %s --simultaneity <out.csv> <high_density|spray|swarm>\n", argv[0]);
    printf("  %s --cpu-benchmark <out.csv>\n", argv[0]);
    return 1;
}
