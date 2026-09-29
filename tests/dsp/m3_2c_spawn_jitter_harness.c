// M3.2C intra-tick spawn jitter regression harness.
//
// White-box harness: it includes the DSP implementation so it can drive the
// static onset helper and the scheduler directly, then links the engine and
// macro layers. It proves:
//
//   1. onset delay is deterministic, bounded to [0, BUBBLES_BLOCK_SIZE), and
//      derived from the canonical SharedSpawnId without consuming the
//      per-channel sequential RNG;
//   2. RHYTHM and STRUM stay sample-exact (delay 0), protecting tempo grids;
//   3. the same canonical event gets the same onset on both stereo engines
//      (no stereo flam) and an asymmetry in another source cannot shift it;
//   4. droplets use their own derived child identity and do not collide
//      systematically with the parent;
//   5. a burst of children on one tick no longer starts on a single sample;
//   6. the result is bit-deterministic and block-size independent;
//   7. a delayed grain near the write head stays inside the guard band.

#define SOUND_BUBBLES_DSP_INTERNAL 1
#define BUBBLES_M3_ONSET_TRACE 1
#include "dsp/sound_bubbles_dsp.c"
#include "engine/bubble_engine.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, (msg)); \
        return 1; \
    } \
} while (0)

#define DELAY_SAMPLES 192000
#define TRACE_CAPACITY 50000

// --- Onset trace capture --------------------------------------------------

typedef struct {
    uint32_t tick;
    uint32_t source;
    uint32_t event_index;
    uint32_t child_index;
    uint32_t onset;
} OnsetRecord;

static OnsetRecord g_trace[TRACE_CAPACITY];
static int g_trace_count;

static void onset_trace_cb(void* user, const SoundBubblesEngine_t* engine,
                           SharedSpawnId_t id, uint32_t onset_delay_samples) {
    (void)user;
    (void)engine;
    if (g_trace_count >= TRACE_CAPACITY) return;
    OnsetRecord* record = &g_trace[g_trace_count++];
    record->tick = id.tick;
    record->source = id.source;
    record->event_index = id.event_index;
    record->child_index = id.child_index;
    record->onset = onset_delay_samples;
}

static void trace_reset(void) {
    g_trace_count = 0;
}

static int trace_same(const OnsetRecord* a, int a_count, const OnsetRecord* b, int b_count) {
    if (a_count != b_count) return 0;
    for (int i = 0; i < a_count; i++) {
        if (a[i].tick != b[i].tick || a[i].source != b[i].source
            || a[i].event_index != b[i].event_index || a[i].child_index != b[i].child_index
            || a[i].onset != b[i].onset) {
            return 0;
        }
    }
    return 1;
}

// --- Helpers --------------------------------------------------------------

static void zero_delay(int16_t* delay) {
    memset(delay, 0, (size_t)DELAY_SAMPLES * sizeof(delay[0]));
}

static void base_config(EngineConfig_t* cfg, uint32_t seed) {
    bubble_engine_default_config(cfg);
    cfg->sample_rate = 48000.0f;
    cfg->rng_seed = seed;
    cfg->smart_start_enable = 0;
    cfg->active_voice_limit = 32;
}

static SharedSpawnId_t make_id(uint32_t tick, BubbleSpawnSource_t source, uint32_t event_index, uint32_t child_index) {
    SharedSpawnId_t id;
    id.tick = tick;
    id.source = (uint32_t)source;
    id.event_index = event_index;
    id.child_index = child_index;
    return id;
}

// --- 1. Determinism, bounds and non-trivial spread ------------------------

static int test_onset_delay_is_deterministic_and_bounded(void) {
    static int16_t delay[DELAY_SAMPLES];
    EngineConfig_t cfg;
    base_config(&cfg, 0x51E5D17u);
    zero_delay(delay);
    BubbleEngine_t engine;
    bubble_engine_init(&engine, delay, &cfg);

    int zero_count = 0;
    int nonzero_count = 0;
    int distinct[8];
    int distinct_count = 0;

    for (uint32_t tick = 1; tick <= 64; tick++) {
        engine.scheduler_tick = tick;
        for (uint32_t e = 0; e < 8; e++) {
            SharedSpawnId_t id = make_id(tick, BUBBLES_SPAWN_SOURCE_DENSITY, e, 0u);
            uint32_t first = ResolveOnsetDelaySamples(&engine, id);
            uint32_t second = ResolveOnsetDelaySamples(&engine, id);
            CHECK(first == second, "onset delay is not a pure function of the event identity");
            CHECK(first < (uint32_t)BUBBLES_BLOCK_SIZE, "onset delay exceeds the control tick window");
            if (first == 0u) zero_count++; else nonzero_count++;
            if (e < 8u) {
                int found = 0;
                for (int i = 0; i < distinct_count; i++) {
                    if (distinct[i] == (int)first) { found = 1; break; }
                }
                if (!found && distinct_count < 8) distinct[distinct_count++] = (int)first;
            }
        }
    }

    CHECK(zero_count > 0, "onset delay never allowed an un-delayed grain");
    CHECK(nonzero_count > 0, "onset delay was always zero");
    CHECK(distinct_count >= 4, "onset delay is not spread across the tick window");
    printf("  onset distribution: %d zero, %d non-zero, %d distinct values in one tick scan\n",
           zero_count, nonzero_count, distinct_count);
    return 0;
}

// --- 2. RHYTHM / STRUM stay sample-exact ----------------------------------

static int test_rhythm_and_strum_are_sample_exact(void) {
    static int16_t delay[DELAY_SAMPLES];
    EngineConfig_t cfg;
    base_config(&cfg, 0x0B1A7u);
    zero_delay(delay);
    BubbleEngine_t engine;
    bubble_engine_init(&engine, delay, &cfg);

    for (uint32_t tick = 1; tick <= 128; tick++) {
        engine.scheduler_tick = tick;
        for (uint32_t e = 0; e < 16; e++) {
            SharedSpawnId_t rhythm = make_id(tick, BUBBLES_SPAWN_SOURCE_RHYTHM, e, 0u);
            SharedSpawnId_t strum = make_id(tick, BUBBLES_SPAWN_SOURCE_STRUM, e, 0u);
            CHECK(ResolveOnsetDelaySamples(&engine, rhythm) == 0u,
                  "RHYTHM onset delay must be zero to keep the grid exact");
            CHECK(ResolveOnsetDelaySamples(&engine, strum) == 0u,
                  "STRUM onset delay must be zero to keep the pattern intact");
        }
    }
    return 0;
}

// --- 3. Stereo coherence --------------------------------------------------

static int test_onset_is_shared_between_stereo_engines(void) {
    static int16_t delay_l[DELAY_SAMPLES];
    static int16_t delay_r[DELAY_SAMPLES];
    EngineConfig_t cfg;
    base_config(&cfg, 0xA11CEu);
    zero_delay(delay_l);
    zero_delay(delay_r);

    BubbleEngine_t left;
    BubbleEngine_t right;
    bubble_engine_init(&left, delay_l, &cfg);
    bubble_engine_init(&right, delay_r, &cfg);
    // Different spatial decorrelation, same shared event seed.
    bubble_engine_set_channel_decorrelation(&right, 0x55555555u);

    int max_abs_delta = 0;
    double sum_abs_delta = 0.0;
    int mismatch_count = 0;
    int total_shared_events = 0;

    for (uint32_t tick = 1; tick <= 96; tick++) {
        left.scheduler_tick = tick;
        right.scheduler_tick = tick;
        for (uint32_t e = 0; e < 8; e++) {
            SharedSpawnId_t id = make_id(tick, BUBBLES_SPAWN_SOURCE_DENSITY, e, 0u);
            uint32_t onset_l = ResolveOnsetDelaySamples(&left, id);
            uint32_t onset_r = ResolveOnsetDelaySamples(&right, id);
            int delta = (int)onset_l - (int)onset_r;
            int abs_delta = abs(delta);
            if (abs_delta > max_abs_delta) max_abs_delta = abs_delta;
            sum_abs_delta += (double)abs_delta;
            if (delta != 0) mismatch_count++;
            total_shared_events++;
            CHECK(onset_l == onset_r,
                  "the same canonical event received a different onset on L and R");
        }
    }

    // Asymmetry in another source must not shift a common event's onset.
    right.scheduler_tick = 500;
    SharedSpawnId_t common = make_id(500, BUBBLES_SPAWN_SOURCE_DENSITY, 3, 0u);
    uint32_t before = ResolveOnsetDelaySamples(&right, common);
    for (uint32_t extra = 0; extra < 64; extra++) {
        SharedSpawnId_t other = make_id(500, BUBBLES_SPAWN_SOURCE_BURST, extra, 0u);
        (void)ResolveOnsetDelaySamples(&right, other);
    }
    CHECK(ResolveOnsetDelaySamples(&right, common) == before,
          "an unrelated extra spawn shifted a common event onset");

    double mean_abs_delta = total_shared_events > 0 ? (sum_abs_delta / (double)total_shared_events) : 0.0;
    CHECK(mismatch_count == 0, "stereo onset mismatch detected");
    printf("  stereo coherence: %d shared events, max delta=%d, mean delta=%.4f, mismatches=%d\n",
           total_shared_events, max_abs_delta, mean_abs_delta, mismatch_count);
    return 0;
}

// --- 4. Droplets use their own derived identity ---------------------------

static int test_droplet_child_onset_uses_derived_identity(void) {
    static int16_t delay[DELAY_SAMPLES];
    EngineConfig_t cfg;
    base_config(&cfg, 0xD20B1E7u);
    zero_delay(delay);
    BubbleEngine_t engine;
    bubble_engine_init(&engine, delay, &cfg);

    int distinct_pairs = 0;
    for (uint32_t tick = 1; tick <= 64; tick++) {
        engine.scheduler_tick = tick;
        for (uint32_t e = 0; e < 4; e++) {
            SharedSpawnId_t parent = make_id(tick, BUBBLES_SPAWN_SOURCE_DENSITY, e, 0u);
            SharedSpawnId_t child = SpawnDerivedId(parent, 1u);
            uint32_t parent_onset = ResolveOnsetDelaySamples(&engine, parent);
            uint32_t child_onset = ResolveOnsetDelaySamples(&engine, child);
            CHECK(child.source == (uint32_t)BUBBLES_SPAWN_SOURCE_DROPLET,
                  "derived droplet identity is not in the droplet source namespace");
            CHECK(child_onset == ResolveOnsetDelaySamples(&engine, child),
                  "droplet onset is not deterministic from its derived identity");
            if (parent_onset != child_onset) distinct_pairs++;
        }
    }
    CHECK(distinct_pairs > 0, "droplet onset always collided with its parent");
    return 0;
}

// --- 5. Burst children spread within a tick -------------------------------

static int test_burst_children_spread_within_tick(void) {
    static int16_t delay[DELAY_SAMPLES];
    EngineConfig_t cfg;
    base_config(&cfg, 0xB0257u);
    cfg.burst_mode = BUBBLE_BURST_MODE_SPRAY;
    cfg.burst_immediate_count = 8;
    cfg.droplet_enable = 0;
    zero_delay(delay);
    BubbleEngine_t engine;
    bubble_engine_init(&engine, delay, &cfg);
    BubblesTest_SetOnsetTrace(onset_trace_cb, NULL);

    int ticks_with_multiple = 0;
    int ticks_with_spread = 0;
    int max_same_sample = 0;

    for (uint32_t tick = 1; tick <= 64; tick++) {
        trace_reset();
        engine.scheduler_tick = tick;
        engine.engine_state = ENGINE_STATE_SUSTAIN_BODY;
        engine.target_density = 10000.0f;
        engine.spawn_accumulator = 5.0f;
        Scheduler_ResetSpawnIdentity(&engine);
        Scheduler_RunTick(&engine);

        CHECK(g_trace_count == SCHED_MAX_SPAWNS_PER_TICK,
              "burst tick did not materialize the expected capped spawn count");

        int counts[BUBBLES_BLOCK_SIZE];
        memset(counts, 0, sizeof(counts));
        for (int i = 0; i < g_trace_count; i++) {
            counts[g_trace[i].onset]++;
        }
        int unique = 0;
        int same = 0;
        for (int s = 0; s < BUBBLES_BLOCK_SIZE; s++) {
            if (counts[s] > 0) unique++;
            if (counts[s] > same) same = counts[s];
        }
        if (g_trace_count > 1) {
            ticks_with_multiple++;
            if (unique > 1) ticks_with_spread++;
        }
        if (same > max_same_sample) max_same_sample = same;

        // Drain any voices so the next tick starts clean.
        for (int v = 0; v < BUBBLES_MAX_VOICES; v++) {
            engine.voices[v].state = VOICE_STATE_INACTIVE;
        }
    }

    CHECK(ticks_with_multiple > 0, "no multi-child tick was observed");
    CHECK(ticks_with_spread > 0, "all children of a burst still started on the same sample");
    CHECK(max_same_sample < SCHED_MAX_SPAWNS_PER_TICK,
          "a full burst still collapsed onto a single sample");
    printf("  burst spread: %d/%d multi-child ticks spread, max same-sample=%d\n",
           ticks_with_spread, ticks_with_multiple, max_same_sample);
    BubblesTest_SetOnsetTrace(NULL, NULL);
    return 0;
}

// --- 6. Real high-density run: spawn count and spread ---------------------

static int test_high_density_spawn_count_and_spread(void) {
    static int16_t delay[DELAY_SAMPLES];
    EngineConfig_t cfg;
    base_config(&cfg, 0xDEC0DEu);
    cfg.density_burst = 400.0f;
    cfg.density_sustain = 300.0f;
    cfg.burst_immediate_count = 2;
    cfg.burst_mode = BUBBLE_BURST_MODE_SPRAY;
    cfg.noise_floor = 0.0005f;
    cfg.tracking_thresh = 0.005f;
    cfg.sustain_thresh = 0.02f;
    cfg.transient_delta = 0.05f;
    zero_delay(delay);
    BubbleEngine_t engine;
    bubble_engine_init(&engine, delay, &cfg);
    // Drive the authored raw density; keep the macro layer from remapping it.
    engine.macro_dirty_mask = 0u;
    engine.motion_base_config.motion_depth = 0.0f;
    engine.config.motion_depth = 0.0f;

    BubblesTest_SetOnsetTrace(onset_trace_cb, NULL);
    trace_reset();

    static float input[48000];
    float left[BUBBLES_BLOCK_SIZE];
    float right[BUBBLES_BLOCK_SIZE];
    for (int i = 0; i < 48000; i++) {
        input[i] = 0.4f * sinf(2.0f * 3.14159265f * 180.0f * (float)i / 48000.0f);
        if ((i % 1200) == 0) input[i] += 0.7f;
    }
    for (int offset = 0; offset < 48000; offset += BUBBLES_BLOCK_SIZE) {
        bubble_engine_process(&engine, &input[offset], left, right, BUBBLES_BLOCK_SIZE);
    }

    int total = g_trace_count;
    int zero = 0;
    int nonzero = 0;
    int histogram[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    for (int i = 0; i < total; i++) {
        if (g_trace[i].onset == 0u) zero++; else nonzero++;
        histogram[(g_trace[i].onset & 31u) / 4]++;
    }
    CHECK(total > 200, "high density run produced too few spawns to measure");
    CHECK(nonzero > 0, "high density run never applied an onset delay");
    CHECK(zero > 0, "high density run never allowed an immediate onset");
    printf("  high density: %d spawns, %d delayed (%.1f%%), %d immediate\n",
           total, nonzero, 100.0 * (double)nonzero / (double)total, zero);
    printf("  onset histogram (4-sample bins):");
    for (int b = 0; b < 8; b++) printf(" %d", histogram[b]);
    printf("\n");
    BubblesTest_SetOnsetTrace(NULL, NULL);
    return 0;
}

// --- 7. Bit determinism and block-size independence -----------------------

static void render_engine(uint32_t seed, int host_block, float* out_l, float* out_r, int total_samples,
                          OnsetRecord* records, int* record_count) {
    static int16_t delay[DELAY_SAMPLES];
    EngineConfig_t cfg;
    base_config(&cfg, seed);
    cfg.density_burst = 120.0f;
    cfg.density_sustain = 90.0f;
    cfg.burst_immediate_count = 3;
    cfg.burst_mode = BUBBLE_BURST_MODE_SPRAY;
    zero_delay(delay);
    BubbleEngine_t engine;
    bubble_engine_init(&engine, delay, &cfg);
    engine.macro_dirty_mask = 0u;
    engine.motion_base_config.motion_depth = 0.0f;
    engine.config.motion_depth = 0.0f;

    BubblesTest_SetOnsetTrace(onset_trace_cb, NULL);
    trace_reset();

    static float input[48000];
    for (int i = 0; i < total_samples; i++) {
        input[i] = 0.3f * sinf(2.0f * 3.14159265f * 220.0f * (float)i / 48000.0f);
        if ((i % 997) == 0) input[i] += 0.5f;
    }

    int processed = 0;
    while (processed < total_samples) {
        int chunk = host_block;
        if (processed + chunk > total_samples) chunk = total_samples - processed;
        bubble_engine_process(&engine, &input[processed], &out_l[processed], &out_r[processed], chunk);
        processed += chunk;
    }

    if (record_count != NULL) {
        int copy = g_trace_count;
        if (copy > TRACE_CAPACITY) copy = TRACE_CAPACITY;
        if (records != NULL) memcpy(records, g_trace, (size_t)copy * sizeof(OnsetRecord));
        *record_count = copy;
    }
    BubblesTest_SetOnsetTrace(NULL, NULL);
}

static int test_bit_determinism_and_block_size_independence(void) {
    const int total = 48000;
    static float left_a[48000];
    static float left_b[48000];
    static float right_a[48000];
    static float right_b[48000];
    static OnsetRecord records_a[TRACE_CAPACITY];
    static OnsetRecord records_b[TRACE_CAPACITY];
    int count_a = 0;
    int count_b = 0;

    // Same seed, same input, two independent runs -> bit identical.
    render_engine(0xFEED5EEDu, BUBBLES_BLOCK_SIZE, left_a, right_a, total, records_a, &count_a);
    render_engine(0xFEED5EEDu, BUBBLES_BLOCK_SIZE, left_b, right_b, total, records_b, &count_b);
    for (int i = 0; i < total; i++) {
        CHECK(left_a[i] == left_b[i], "two identical runs produced different left output");
        CHECK(right_a[i] == right_b[i], "two identical runs produced different right output");
    }
    CHECK(trace_same(records_a, count_a, records_b, count_b), "two identical runs produced different onset traces");

    // Complete host block sizes matrix: 32, 64, 127, 256, 512, 2048
    const int test_blocks[] = {64, 127, 256, 512, 2048};
    for (size_t b = 0; b < sizeof(test_blocks) / sizeof(test_blocks[0]); b++) {
        int block = test_blocks[b];
        render_engine(0xFEED5EEDu, block, left_b, right_b, total, records_b, &count_b);
        for (int i = 0; i < total; i++) {
            CHECK(left_a[i] == left_b[i], "host block size changed the left output");
            CHECK(right_a[i] == right_b[i], "host block size changed the right output");
        }
        CHECK(trace_same(records_a, count_a, records_b, count_b), "host block size changed the onset trace");
    }
    printf("  determinism: %d spawns identical across 32/64/127/256/512/2048 host blocks\n", count_a);
    return 0;
}

// --- 8. Sample rate matrix ------------------------------------------------

static int test_sample_rate_matrix(void) {
    const float rates[4] = {44100.0f, 48000.0f, 88200.0f, 96000.0f};
    static int16_t delay[DELAY_SAMPLES];

    for (int r = 0; r < 4; r++) {
        float sr = rates[r];
        EngineConfig_t cfg;
        base_config(&cfg, 0x5A171E0u + (uint32_t)r * 100u);
        cfg.sample_rate = sr;
        cfg.density_burst = 300.0f;
        cfg.density_sustain = 200.0f;
        cfg.burst_immediate_count = 2;
        cfg.burst_mode = BUBBLE_BURST_MODE_SPRAY;
        zero_delay(delay);

        BubbleEngine_t engine;
        bubble_engine_init(&engine, delay, &cfg);
        engine.macro_dirty_mask = 0u;
        engine.motion_base_config.motion_depth = 0.0f;
        engine.config.motion_depth = 0.0f;

        for (uint32_t tick = 1; tick <= 64; tick++) {
            engine.scheduler_tick = tick;
            for (uint32_t e = 0; e < 8; e++) {
                SharedSpawnId_t d_id = make_id(tick, BUBBLES_SPAWN_SOURCE_DENSITY, e, 0u);
                uint32_t d_delay1 = ResolveOnsetDelaySamples(&engine, d_id);
                uint32_t d_delay2 = ResolveOnsetDelaySamples(&engine, d_id);
                CHECK(d_delay1 == d_delay2, "onset delay not deterministic for sample rate");
                CHECK(d_delay1 < (uint32_t)BUBBLES_BLOCK_SIZE, "onset delay >= BUBBLES_BLOCK_SIZE");

                SharedSpawnId_t r_id = make_id(tick, BUBBLES_SPAWN_SOURCE_RHYTHM, e, 0u);
                CHECK(ResolveOnsetDelaySamples(&engine, r_id) == 0u, "RHYTHM onset != 0");

                SharedSpawnId_t s_id = make_id(tick, BUBBLES_SPAWN_SOURCE_STRUM, e, 0u);
                CHECK(ResolveOnsetDelaySamples(&engine, s_id) == 0u, "STRUM onset != 0");
            }
        }

        const int run_samples = (int)sr;
        float in_buf[BUBBLES_BLOCK_SIZE];
        float out_l[BUBBLES_BLOCK_SIZE];
        float out_r[BUBBLES_BLOCK_SIZE];
        float prev_l = 0.0f;
        float prev_r = 0.0f;

        for (int offset = 0; offset < run_samples; offset += BUBBLES_BLOCK_SIZE) {
            for (int i = 0; i < BUBBLES_BLOCK_SIZE; i++) {
                int n = offset + i;
                in_buf[i] = 0.3f * sinf(2.0f * 3.14159265f * 220.0f * (float)n / sr);
                if ((n % 1000) == 0) in_buf[i] += 0.5f;
            }
            bubble_engine_process(&engine, in_buf, out_l, out_r, BUBBLES_BLOCK_SIZE);
            for (int i = 0; i < BUBBLES_BLOCK_SIZE; i++) {
                float l = out_l[i];
                float r = out_r[i];
                CHECK(!isnan(l) && !isinf(l), "NaN or Inf in left output");
                CHECK(!isnan(r) && !isinf(r), "NaN or Inf in right output");
                CHECK(fabsf(l - prev_l) < 3.0f, "abnormal sample discontinuity in left");
                CHECK(fabsf(r - prev_r) < 3.0f, "abnormal sample discontinuity in right");
                prev_l = l;
                prev_r = r;
            }
        }
    }
    printf("  sample-rate matrix (44.1k, 48k, 88.2k, 96k): delays in [0, 32), rhythm/strum=0, no NaN/Inf/discontinuities\n");
    printf("    temporal window documentation:\n");
    printf("      32 samples @ 44.1 kHz ≈ 0.73 ms\n");
    printf("      32 samples @ 48 kHz   ≈ 0.67 ms\n");
    printf("      32 samples @ 88.2 kHz ≈ 0.36 ms\n");
    printf("      32 samples @ 96 kHz   ≈ 0.33 ms\n");
    printf("    (tick is fixed in samples; sub-ms window scale is intentional)\n");
    return 0;
}

// --- 9. Timing by source --------------------------------------------------

static int test_timing_by_source(void) {
    static int16_t delay[DELAY_SAMPLES];
    EngineConfig_t cfg;
    base_config(&cfg, 0x5002C3u);
    zero_delay(delay);
    BubbleEngine_t engine;
    bubble_engine_init(&engine, delay, &cfg);

    const BubbleSpawnSource_t sources[5] = {
        BUBBLES_SPAWN_SOURCE_DENSITY,
        BUBBLES_SPAWN_SOURCE_BURST,
        BUBBLES_SPAWN_SOURCE_DROPLET,
        BUBBLES_SPAWN_SOURCE_RHYTHM,
        BUBBLES_SPAWN_SOURCE_STRUM
    };
    const char* source_names[5] = {"DENSITY", "BURST", "DROPLET", "RHYTHM", "STRUM"};

    for (int s = 0; s < 5; s++) {
        BubbleSpawnSource_t src = sources[s];
        int zero_count = 0;
        int nonzero_count = 0;
        int histogram[BUBBLES_BLOCK_SIZE];
        memset(histogram, 0, sizeof(histogram));

        for (uint32_t tick = 1; tick <= 128; tick++) {
            engine.scheduler_tick = tick;
            for (uint32_t e = 0; e < 16; e++) {
                SharedSpawnId_t id = make_id(tick, src, e, 0u);
                uint32_t delay_val = ResolveOnsetDelaySamples(&engine, id);
                CHECK(delay_val < (uint32_t)BUBBLES_BLOCK_SIZE, "delay exceeds block size");
                if (delay_val == 0u) zero_count++; else nonzero_count++;
                histogram[delay_val]++;
            }
        }
        int total = zero_count + nonzero_count;
        int distinct = 0;
        for (int i = 0; i < BUBBLES_BLOCK_SIZE; i++) {
            if (histogram[i] > 0) distinct++;
        }

        if (src == BUBBLES_SPAWN_SOURCE_RHYTHM || src == BUBBLES_SPAWN_SOURCE_STRUM) {
            CHECK(zero_count == total && nonzero_count == 0,
                  "RHYTHM and STRUM must have 100% onset_delay = 0");
            CHECK(distinct == 1, "RHYTHM/STRUM must only have onset 0");
            printf("  source %-7s: 100%% onset = 0 (%d/%d events)\n", source_names[s], zero_count, total);
        } else {
            CHECK(distinct >= 20, "free source distribution is degenerate");
            CHECK(zero_count > 0 && nonzero_count > 0, "free source must have both zero and nonzero onsets");
            printf("  source %-7s: %d distinct delays (0..31), zero=%.1f%%, non-zero=%.1f%%\n",
                   source_names[s], distinct,
                   100.0 * (double)zero_count / (double)total,
                   100.0 * (double)nonzero_count / (double)total);
        }
    }
    return 0;
}

// --- 10. Pending queue qualification --------------------------------------

static int test_pending_queue_qualification(void) {
    static int16_t delay[DELAY_SAMPLES];
    EngineConfig_t cfg;
    base_config(&cfg, 0x0E0E0Eu);
    cfg.active_voice_limit = 2;
    zero_delay(delay);
    BubbleEngine_t engine;
    bubble_engine_init(&engine, delay, &cfg);
    engine.macro_dirty_mask = 0u;

    engine.voices[0].state = VOICE_STATE_PLAYING;
    engine.voices[0].phase = 0.1f;
    engine.voices[1].state = VOICE_STATE_PLAYING;
    engine.voices[1].phase = 0.1f;

    // Case A: request queued and flushed in SAME scheduler tick
    engine.scheduler_tick = 42;
    SharedSpawnId_t id_a = make_id(42, BUBBLES_SPAWN_SOURCE_DENSITY, 0u, 0u);
    uint32_t expected_delay = ResolveOnsetDelaySamples(&engine, id_a);
    bool immediate = Voice_RequestSpawn(&engine, BUBBLE_CLASS_SHORT_INTERMEDIATE, 0, id_a);
    CHECK(!immediate, "expected request to queue when voices are full");
    CHECK(engine.pending_spawn_count == 1, "pending spawn queue count != 1");

    engine.voices[0].state = VOICE_STATE_INACTIVE;
    int flushed_a = Voice_FlushPendingSpawns(&engine, 1);
    CHECK(flushed_a == 1, "failed to flush pending spawn");
    if (expected_delay > 0u) {
        CHECK(engine.voices[0].state == VOICE_STATE_PENDING_ONSET, "Case A voice should be PENDING_ONSET");
        CHECK(engine.voices[0].onset_delay_samples == (int16_t)expected_delay,
              "Case A onset_delay_samples did not match ResolveOnsetDelaySamples");
    } else {
        CHECK(engine.voices[0].state == VOICE_STATE_PLAYING, "Case A voice should be PLAYING");
        CHECK(engine.voices[0].onset_delay_samples == 0, "Case A onset_delay_samples != 0");
    }

    // Case B: request queued at tick 50, but only flushed at tick 51+ (later tick)
    engine.voices[0].state = VOICE_STATE_PLAYING;
    engine.voices[1].state = VOICE_STATE_PLAYING;
    engine.scheduler_tick = 50;
    SharedSpawnId_t id_b = make_id(50, BUBBLES_SPAWN_SOURCE_DENSITY, 1u, 0u);
    for (uint32_t e = 1; e < 20; e++) {
        id_b = make_id(50, BUBBLES_SPAWN_SOURCE_DENSITY, e, 0u);
        if (ResolveOnsetDelaySamples(&engine, id_b) > 0u) break;
    }
    CHECK(ResolveOnsetDelaySamples(&engine, id_b) > 0u, "could not find non-zero delay event for test");

    immediate = Voice_RequestSpawn(&engine, BUBBLE_CLASS_SHORT_INTERMEDIATE, 0, id_b);
    CHECK(!immediate, "expected request to queue when voices are full");
    CHECK(engine.pending_spawn_count == 1, "pending spawn queue count != 1");

    engine.scheduler_tick = 51;
    engine.voices[0].state = VOICE_STATE_INACTIVE;
    int flushed_b = Voice_FlushPendingSpawns(&engine, 1);
    CHECK(flushed_b == 1, "failed to flush pending spawn on later tick");
    CHECK(engine.voices[0].state == VOICE_STATE_PLAYING,
          "Case B (delayed tick) must start immediately (PLAYING), not PENDING_ONSET");
    CHECK(engine.voices[0].onset_delay_samples == 0,
          "Case B onset_delay_samples must be 0 (no second jitter window)");

    printf("  pending queue: Case A applies normal jitter; Case B applies immediate onset (delay=0, no second window)\n");
    return 0;
}

// --- 11. Guard validity for delayed grains near the write head ------------

static int test_delayed_onset_keeps_guard_valid(void) {
    static int16_t delay[DELAY_SAMPLES];
    EngineConfig_t cfg;
    base_config(&cfg, 0x6A2Du);
    // A deliberately guard-adjacent attack region plus forward pitch so the guard
    // path is stressed, with Smart Start off so the spawn clamp is what protects.
    cfg.attack_region.min_offset_samples = BUBBLES_GUARD_ZONE_SAMPLES;
    cfg.attack_region.max_offset_samples = BUBBLES_GUARD_ZONE_SAMPLES + 2;
    cfg.pitch_mode = BUBBLE_PITCH_MODE_OCTAVE_UP;
    cfg.droplet_enable = 0;
    zero_delay(delay);
    BubbleEngine_t engine;
    bubble_engine_init(&engine, delay, &cfg);
    engine.engine_state = ENGINE_STATE_SILENCE;
    const int32_t buffer = (int32_t)SoundBubbles_RequiredBufferSamples(cfg.sample_rate);
    const int32_t max_guarded = buffer - BUBBLES_GUARD_ZONE_SAMPLES - 1;

    float silent[1] = {0.0f};
    float out_l[1];
    float out_r[1];
    int delayed = 0;

    for (uint32_t tick = 1; tick <= 256; tick++) {
        engine.scheduler_tick = tick;
        engine.engine_state = ENGINE_STATE_SILENCE;
        SharedSpawnId_t id = make_id(tick, BUBBLES_SPAWN_SOURCE_BURST, 0u, 0u);
        Voice_SpawnInit(&engine, 0, BUBBLE_CLASS_MICRO_ATTACK, 0, id);
        BubbleVoice_t* voice = &engine.voices[0];
        CHECK(voice->spawn_read_offset >= BUBBLES_GUARD_ZONE_SAMPLES
                  && voice->spawn_read_offset <= max_guarded,
              "delayed spawn chose a read offset outside the guard band");

        if (voice->state != VOICE_STATE_PENDING_ONSET) {
            engine.voices[0].state = VOICE_STATE_INACTIVE;
            continue;
        }
        delayed++;

        // Drive real 1-sample blocks with silence until the grain starts, so the
        // write head advances exactly as it would in the audio callback.
        int guard_steps = (int)voice->onset_delay_samples + 4;
        for (int s = 0; s < guard_steps && engine.voices[0].state == VOICE_STATE_PENDING_ONSET; s++) {
            engine.engine_state = ENGINE_STATE_SILENCE;
            bubble_engine_process(&engine, silent, out_l, out_r, 1);
        }
        CHECK(engine.voices[0].state == VOICE_STATE_PLAYING,
              "delayed grain did not start after its onset countdown");
        int32_t distance = WrapIntIndex(engine.write_ptr - (int32_t)engine.voices[0].read_ptr_float, buffer);
        CHECK(distance >= BUBBLES_GUARD_ZONE_SAMPLES && distance <= max_guarded,
              "delayed onset left the read pointer outside the guard band");
        engine.voices[0].state = VOICE_STATE_INACTIVE;
    }
    CHECK(delayed > 0, "guard regression did not exercise any delayed grain");
    printf("  guard: %d delayed grains near the write head stayed guard safe\n", delayed);
    return 0;
}

// --- 12. Extended guard qualification across pitch/direction/interpolators -

static int test_guard_qualification_extended(void) {
    static int16_t delay[DELAY_SAMPLES];
    const float sr = 48000.0f;
    const int32_t buffer = (int32_t)SoundBubbles_RequiredBufferSamples(sr);
    const int32_t max_guarded = buffer - BUBBLES_GUARD_ZONE_SAMPLES - 1;

    typedef struct {
        BubblePitchMode_t pitch;
        float reverse_prob;
        const char* name;
    } RateConfig;

    const RateConfig rates[] = {
        { BUBBLE_PITCH_MODE_UNISON,      0.0f, "forward 1x" },
        { BUBBLE_PITCH_MODE_OCTAVE_UP,   0.0f, "octave up" },
        { BUBBLE_PITCH_MODE_OCTAVE_DOWN, 0.0f, "octave down" },
        { BUBBLE_PITCH_MODE_FIFTH,       0.0f, "fifth" },
        { BUBBLE_PITCH_MODE_UNISON,      1.0f, "reverse 1x" },
        { BUBBLE_PITCH_MODE_OCTAVE_UP,   1.0f, "reverse octave up" }
    };
    const size_t num_rates = sizeof(rates) / sizeof(rates[0]);

    int tested_grains = 0;
    float silent[1] = {0.0f};
    float out_l[1];
    float out_r[1];

    for (int interp = 0; interp < 2; interp++) {
        for (size_t r = 0; r < num_rates; r++) {
            EngineConfig_t cfg;
            base_config(&cfg, 0x6A2Du + (uint32_t)(interp * 100 + r * 10));
            cfg.attack_region.min_offset_samples = BUBBLES_GUARD_ZONE_SAMPLES;
            cfg.attack_region.max_offset_samples = BUBBLES_GUARD_ZONE_SAMPLES + 2;
            cfg.pitch_mode = rates[r].pitch;
            cfg.reverse_probability = rates[r].reverse_prob;
            cfg.quality_profile = (interp == 0) ? BUBBLE_QUALITY_PROFILE_MCU_SAFE : BUBBLE_QUALITY_PROFILE_WEB_ULTRA;
            zero_delay(delay);

            BubbleEngine_t engine;
            bubble_engine_init(&engine, delay, &cfg);
            engine.engine_state = ENGINE_STATE_SILENCE;

            int32_t write_positions[3] = {5, buffer / 2, buffer - 5};
            for (int w = 0; w < 3; w++) {
                engine.write_ptr = write_positions[w];

                for (uint32_t tick = 1; tick <= 16; tick++) {
                    engine.scheduler_tick = tick;
                    engine.engine_state = ENGINE_STATE_SILENCE;
                    SharedSpawnId_t id = make_id(tick, BUBBLES_SPAWN_SOURCE_BURST, (uint32_t)w, 0u);
                    Voice_SpawnInit(&engine, 0, BUBBLE_CLASS_MICRO_ATTACK, 0, id);
                    BubbleVoice_t* voice = &engine.voices[0];
                    CHECK(voice->spawn_read_offset >= BUBBLES_GUARD_ZONE_SAMPLES
                              && voice->spawn_read_offset <= max_guarded,
                          "spawn read offset outside guard band");

                    if (voice->state != VOICE_STATE_PENDING_ONSET) {
                        engine.voices[0].state = VOICE_STATE_INACTIVE;
                        continue;
                    }
                    tested_grains++;

                    int guard_steps = (int)voice->onset_delay_samples + 8;
                    for (int s = 0; s < guard_steps && engine.voices[0].state == VOICE_STATE_PENDING_ONSET; s++) {
                        engine.engine_state = ENGINE_STATE_SILENCE;
                        bubble_engine_process(&engine, silent, out_l, out_r, 1);
                    }
                    CHECK(engine.voices[0].state == VOICE_STATE_PLAYING,
                          "delayed grain did not transition to PLAYING");
                    CHECK(!CheckGuardZoneDirectional(engine.write_ptr, engine.voices[0].read_ptr_float,
                                                     engine.voices[0].rate, buffer),
                          "delayed onset started inside forbidden guard zone");

                    for (int s = 0; s < 16 && engine.voices[0].state == VOICE_STATE_PLAYING; s++) {
                        engine.engine_state = ENGINE_STATE_SILENCE;
                        bubble_engine_process(&engine, silent, out_l, out_r, 1);
                        if (engine.voices[0].state == VOICE_STATE_PLAYING) {
                            CHECK(!CheckGuardZoneDirectional(engine.write_ptr, engine.voices[0].read_ptr_float,
                                                             engine.voices[0].rate, buffer),
                                  "active playback entered forbidden guard zone");
                        }
                    }
                    engine.voices[0].state = VOICE_STATE_INACTIVE;
                }
            }
        }
    }
    CHECK(tested_grains > 0, "extended guard test did not exercise delayed grains");
    printf("  extended guard: %d delayed grains across 6 pitch/direction rates x 2 interpolators (Linear/Hermite) x wrap boundaries stayed guard safe (0 violations)\n",
           tested_grains);
    return 0;
}

// --- 13. Machine-gun boundary concentration metric ------------------------

static int test_machine_gun_boundary_concentration(void) {
    static int16_t delay[DELAY_SAMPLES];
    EngineConfig_t cfg;
    base_config(&cfg, 0xC0FFEEu);
    cfg.density_burst = 350.0f;
    cfg.density_sustain = 250.0f;
    cfg.burst_immediate_count = 3;
    cfg.burst_mode = BUBBLE_BURST_MODE_SPRAY;
    zero_delay(delay);
    BubbleEngine_t engine;
    bubble_engine_init(&engine, delay, &cfg);
    engine.macro_dirty_mask = 0u;
    engine.motion_base_config.motion_depth = 0.0f;
    engine.config.motion_depth = 0.0f;

    BubblesTest_SetOnsetTrace(onset_trace_cb, NULL);
    trace_reset();

    static float input[48000];
    float left[BUBBLES_BLOCK_SIZE];
    float right[BUBBLES_BLOCK_SIZE];
    for (int i = 0; i < 48000; i++) {
        input[i] = 0.35f * sinf(2.0f * 3.14159265f * 150.0f * (float)i / 48000.0f);
        if ((i % 800) == 0) input[i] += 0.6f;
    }
    for (int offset = 0; offset < 48000; offset += BUBBLES_BLOCK_SIZE) {
        bubble_engine_process(&engine, &input[offset], left, right, BUBBLES_BLOCK_SIZE);
    }

    int total = g_trace_count;
    int boundary_count = 0;
    for (int i = 0; i < total; i++) {
        if (g_trace[i].onset == 0u) boundary_count++;
    }
    BubblesTest_SetOnsetTrace(NULL, NULL);

    CHECK(total > 100, "too few events for boundary concentration measurement");
    float boundary_concentration = (float)boundary_count / (float)total;

    // In baseline without jitter, 100% of events started exactly on tick boundary.
    // In jitter, fresh free spawns have ~3.4% boundary concentration (uniform over 32 offsets),
    // and even under heavy saturation where delayed queue flushes begin immediately,
    // boundary concentration remains << baseline (under 35% vs 100%).
    CHECK(boundary_concentration < 0.50f,
          "boundary concentration is too high (failed to break boundary simultaneity)");
    printf("  machine-gun metric: boundary concentration = %.2f%% (%d/%d events on tick boundary, baseline was 100.0%%)\n",
           100.0f * boundary_concentration, boundary_count, total);
    return 0;
}

int main(void) {
    if (test_onset_delay_is_deterministic_and_bounded() != 0) return 1;
    if (test_rhythm_and_strum_are_sample_exact() != 0) return 1;
    if (test_timing_by_source() != 0) return 1;
    if (test_onset_is_shared_between_stereo_engines() != 0) return 1;
    if (test_droplet_child_onset_uses_derived_identity() != 0) return 1;
    if (test_burst_children_spread_within_tick() != 0) return 1;
    if (test_high_density_spawn_count_and_spread() != 0) return 1;
    if (test_bit_determinism_and_block_size_independence() != 0) return 1;
    if (test_sample_rate_matrix() != 0) return 1;
    if (test_pending_queue_qualification() != 0) return 1;
    if (test_delayed_onset_keeps_guard_valid() != 0) return 1;
    if (test_guard_qualification_extended() != 0) return 1;
    if (test_machine_gun_boundary_concentration() != 0) return 1;
    printf("M3.2C spawn jitter harness passed\n");
    return 0;
}
