// M2 "Musical Cloud Character" deterministic test harness.
//
// Covers the five M2 pillars without a public API change:
//   1. recent-weighted, non-uniform temporal memory distribution + rare deep ghosts
//   2. state-driven stereo coherence (verified through the wrapper probe)
//   3. weighted Sparkle interval cloud with an exact 12-TET fifth
//   4. fixed per-grain microdetune (bounded, birth-time, reproducible)
//   5. context-conditioned reverse probability
// plus determinism and sample-rate/block-size invariants.

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "engine/bubble_engine.h"

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, (msg)); \
        return 1; \
    } \
} while (0)

#define CHECK_CLOSE(actual, expected, tol, msg) do { \
    if (fabsf((actual) - (expected)) > (tol)) { \
        fprintf(stderr, "FAIL %s:%d: %s (actual=%g expected=%g)\n", __FILE__, __LINE__, (msg), (double)(actual), (double)(expected)); \
        return 1; \
    } \
} while (0)

#define FIFTH_12TET 1.4983070768766815f
#define OCTAVE_FIFTH 2.9966141537533630f

typedef struct {
    int bubble_class;
    int reverse;
    float rate;
    float cents;
    int offset;
    int region_id;
    int memory_tier;
} SpawnRecord;

typedef struct {
    SpawnRecord records[4096];
    int count;
} SpawnLog;

static void init_memory(int16_t* delay, int samples) {
    memset(delay, 0, (size_t)samples * sizeof(delay[0]));
}

// M3.2C: a freshly materialized spawn may still be waiting for its intra-tick
// onset (PENDING_ONSET) but already carries every spawn-time decision (class,
// reverse, rate, microdetune, offset, region). Both states are "fresh" here.
static int is_fresh_spawn(const BubbleVoice_t* voice) {
    return (voice->state == VOICE_STATE_PLAYING || voice->state == VOICE_STATE_PENDING_ONSET)
        && voice->phase == 0.0f;
}

static void record_if_fresh(BubbleEngine_t* engine, SpawnLog* log, int buffer_size, int expect_state) {
    for (int v = 0; v < engine->active_voice_limit; v++) {
        BubbleVoice_t* voice = &engine->voices[v];
        if (!is_fresh_spawn(voice)) continue;
        if (log->count >= (int)(sizeof(log->records) / sizeof(log->records[0]))) return;
        SpawnRecord* rec = &log->records[log->count++];
        rec->bubble_class = (int)voice->bubble_class;
        rec->reverse = voice->read_direction != 0u ? 1 : 0;
        rec->rate = voice->rate;
        rec->cents = voice->microdetune_cents;
        int offset = (engine->write_ptr - (int)voice->read_ptr_float) % buffer_size;
        if (offset < 0) offset += buffer_size;
        rec->offset = offset;
        rec->region_id = voice->source_region_id;
        rec->memory_tier = voice->memory_tier;
    }
    (void)expect_state;
}

static void run_and_capture(BubbleEngine_t* engine, const float* block, int ticks, SpawnLog* log, int buffer_size) {
    float left[BUBBLES_BLOCK_SIZE];
    float right[BUBBLES_BLOCK_SIZE];
    for (int t = 0; t < ticks; t++) {
        bubble_engine_process(engine, block, left, right, BUBBLES_BLOCK_SIZE);
        record_if_fresh(engine, log, buffer_size, 0);
    }
}

static void configure_sustain(EngineConfig_t* cfg, uint32_t seed) {
    bubble_engine_default_config(cfg);
    cfg->sample_rate = 48000.0f;
    cfg->rng_seed = seed;
    cfg->smart_start_enable = 0;
    cfg->density_burst = 0.0f;
    cfg->density_sustain = 80.0f;
    cfg->density_decay = 60.0f;
    cfg->active_voice_limit = 32;
    cfg->envelope_variation = 0.5f;
}

static int test_memory_distribution_is_recent_weighted_and_not_uniform(void) {
    static int16_t delay[192000];
    static float input[32];
    SpawnLog log;
    log.count = 0;
    for (int i = 0; i < 32; i++) input[i] = 0.4f;

    EngineConfig_t cfg;
    configure_sustain(&cfg, 0xC0FFEEu);
    init_memory(delay, 192000);
    BubbleEngine_t engine;
    bubble_engine_init(&engine, delay, &cfg);
    int buffer_size = (int)SoundBubbles_RequiredBufferSamples(cfg.sample_rate);

    run_and_capture(&engine, input, 20000, &log, buffer_size);

    const int32_t attack_min = bubble_engine_reference_samples_to_samples(cfg.attack_region.min_offset_samples, cfg.sample_rate);
    const int32_t body_min = bubble_engine_reference_samples_to_samples(cfg.body_region.min_offset_samples, cfg.sample_rate);
    const int32_t body_max = bubble_engine_reference_samples_to_samples(cfg.body_region.max_offset_samples, cfg.sample_rate);
    const int32_t memory_max = bubble_engine_reference_samples_to_samples(cfg.memory_region.max_offset_samples, cfg.sample_rate);
    (void)attack_min;
    (void)memory_max;

    int body_thirds[3] = {0, 0, 0};
    int body_total = 0;
    int memory_count = 0;
    int total = 0;
    for (int i = 0; i < log.count; i++) {
        const SpawnRecord* rec = &log.records[i];
        if (rec->bubble_class == BUBBLE_CLASS_MICRO_ATTACK) continue;
        total++;
        if (rec->offset > body_max) {
            memory_count++;
            continue;
        }
        if (rec->offset < body_min) continue;
        int32_t span = body_max - body_min;
        int third = 0;
        if (span > 0) {
            third = (int)(((double)(rec->offset - body_min) / (double)span) * 3.0);
            if (third > 2) third = 2;
            if (third < 0) third = 0;
        }
        body_thirds[third]++;
        body_total++;
    }
    CHECK(total > 300, "captured enough non-micro spawns for the memory distribution");
    CHECK(body_total > 0, "body-region spawns exist");
    CHECK(memory_count > 0, "deep memory must still appear occasionally");
    float memory_fraction = (float)memory_count / (float)total;
    CHECK(memory_fraction < 0.45f, "deep memory stays a minority of reads");
    CHECK(memory_fraction > 0.03f, "deep memory is not entirely suppressed");
    float body_fraction = (float)body_total / (float)total;
    CHECK(body_fraction > 0.5f, "body/recent region dominates the temporal distribution");
    CHECK(body_thirds[0] > body_thirds[1], "recent third of the body region is favoured over medium");
    CHECK(body_thirds[1] > body_thirds[2], "medium third of the body region is favoured over deep");
    float recent_fraction = (float)body_thirds[0] / (float)body_total;
    CHECK(recent_fraction > 0.36f, "recent third of the body region exceeds a uniform 1/3 share");
    return 0;
}

static int classify_interval(float raw_rate) {
    const float candidates[4] = {1.0f, FIFTH_12TET, 2.0f, OCTAVE_FIFTH};
    int best = 0;
    float best_delta = fabsf(raw_rate - candidates[0]);
    for (int i = 1; i < 4; i++) {
        float delta = fabsf(raw_rate - candidates[i]);
        if (delta < best_delta) { best_delta = delta; best = i; }
    }
    return best;
}

static int test_sparkle_interval_weights_and_exact_fifth(void) {
    static int16_t delay[192000];
    static float input[32];
    SpawnLog log;
    log.count = 0;
    for (int i = 0; i < 32; i++) input[i] = 0.4f;

    EngineConfig_t cfg;
    configure_sustain(&cfg, 0x5A17E5u);
    cfg.pitch_mode = BUBBLE_PITCH_MODE_SHIMMER;
    cfg.shimmer_amount = 1.0f;
    init_memory(delay, 192000);
    BubbleEngine_t engine;
    bubble_engine_init(&engine, delay, &cfg);
    int buffer_size = (int)SoundBubbles_RequiredBufferSamples(cfg.sample_rate);
    // Drive Sparkle through the public macro path so the resolved pitch mode and
    // shimmer amount match what a host would produce, then let the macro smoothing
    // settle before capturing spawns.
    bubble_engine_set_parameter(&engine, BUBBLE_PARAM_SPARKLE, 1.0f);
    {
        float left[BUBBLES_BLOCK_SIZE];
        float right[BUBBLES_BLOCK_SIZE];
        for (int t = 0; t < 400; t++) bubble_engine_process(&engine, input, left, right, BUBBLES_BLOCK_SIZE);
    }
    run_and_capture(&engine, input, 20000, &log, buffer_size);

    int interval_counts[4] = {0, 0, 0, 0};
    int total = 0;
    for (int i = 0; i < log.count; i++) {
        const SpawnRecord* rec = &log.records[i];
        float detune_ratio = powf(2.0f, rec->cents / 1200.0f);
        float raw_rate = fabsf(rec->rate) / detune_ratio;
        interval_counts[classify_interval(raw_rate)]++;
        total++;
    }
    CHECK(total > 300, "captured enough Sparkle spawns");
    CHECK(interval_counts[0] > 0 && interval_counts[1] > 0 && interval_counts[2] > 0 && interval_counts[3] > 0,
          "Sparkle produces all weighted intervals (unison, +12, +7, +19)");
    CHECK(interval_counts[0] > interval_counts[2], "unison is more common than the octave");
    CHECK(interval_counts[2] > interval_counts[1], "octave is more common than the fifth");
    CHECK(interval_counts[1] > interval_counts[3], "fifth is more common than the +19 interval");
    float unison_fraction = (float)interval_counts[0] / (float)total;
    CHECK(unison_fraction > 0.30f, "Sparkle keeps a meaningful unison anchor");

    // Fixed 12-TET fifth override.
    SpawnLog fifth_log;
    fifth_log.count = 0;
    EngineConfig_t fifth_cfg;
    configure_sustain(&fifth_cfg, 0x5A17E5u);
    fifth_cfg.pitch_mode = BUBBLE_PITCH_MODE_FIFTH;
    init_memory(delay, 192000);
    bubble_engine_init(&engine, delay, &fifth_cfg);
    run_and_capture(&engine, input, 4000, &fifth_log, buffer_size);
    CHECK(fifth_log.count > 50, "captured enough fifth-mode spawns");
    for (int i = 0; i < fifth_log.count; i++) {
        float detune_ratio = powf(2.0f, fifth_log.records[i].cents / 1200.0f);
        float raw_rate = fabsf(fifth_log.records[i].rate) / detune_ratio;
        CHECK_CLOSE(raw_rate, FIFTH_12TET, 1.0e-4f, "fixed fifth uses the exact 12-TET ratio 2^(7/12)");
    }
    return 0;
}

static int test_sparkle_zero_has_no_random_pitch(void) {
    static int16_t delay[192000];
    static float input[32];
    SpawnLog log;
    log.count = 0;
    for (int i = 0; i < 32; i++) input[i] = 0.4f;

    EngineConfig_t cfg;
    configure_sustain(&cfg, 0x0FACEu);
    cfg.pitch_mode = BUBBLE_PITCH_MODE_SHIMMER;
    cfg.shimmer_amount = 0.0f;
    // Isolate the authored SPARKLE=0 pitch path: the default MOTION macro would
    // otherwise inject a small runtime shimmer on top of the authored zero.
    cfg.motion_depth = 0.0f;
    init_memory(delay, 192000);
    BubbleEngine_t engine;
    bubble_engine_init(&engine, delay, &cfg);
    engine.motion_base_config.motion_depth = 0.0f;
    engine.config.motion_depth = 0.0f;
    engine.macro_dirty_mask = 0u;
    int buffer_size = (int)SoundBubbles_RequiredBufferSamples(cfg.sample_rate);
    run_and_capture(&engine, input, 4000, &log, buffer_size);
    CHECK(log.count > 50, "captured enough zero-Sparkle spawns");
    for (int i = 0; i < log.count; i++) {
        float detune_ratio = powf(2.0f, log.records[i].cents / 1200.0f);
        float raw_rate = fabsf(log.records[i].rate) / detune_ratio;
        CHECK_CLOSE(raw_rate, 1.0f, 1.0e-4f, "SPARKLE=0 introduces no random pitch interval");
    }
    return 0;
}

static float class_detune_bound(int bubble_class) {
    switch (bubble_class) {
        case BUBBLE_CLASS_MICRO_ATTACK: return BUBBLES_MICRODETUNE_ATTACK_CENTS;
        case BUBBLE_CLASS_SHORT_INTERMEDIATE: return BUBBLES_MICRODETUNE_SHORT_CENTS;
        case BUBBLE_CLASS_SUSTAIN_BODY: return BUBBLES_MICRODETUNE_SUSTAIN_CENTS;
        default: return BUBBLES_MICRODETUNE_SUSTAIN_CENTS;
    }
}

static int test_microdetune_is_bounded_fixed_and_reproducible(void) {
    static int16_t delay[192000];
    static float input[32];
    SpawnLog log;
    log.count = 0;
    for (int i = 0; i < 32; i++) input[i] = 0.4f;

    EngineConfig_t cfg;
    configure_sustain(&cfg, 0xBEEF01u);
    init_memory(delay, 192000);
    BubbleEngine_t engine;
    bubble_engine_init(&engine, delay, &cfg);
    int buffer_size = (int)SoundBubbles_RequiredBufferSamples(cfg.sample_rate);
    run_and_capture(&engine, input, 12000, &log, buffer_size);
    CHECK(log.count > 300, "captured enough spawns for microdetune bounds");
    int nonzero = 0;
    for (int i = 0; i < log.count; i++) {
        float bound = class_detune_bound(log.records[i].bubble_class);
        CHECK(fabsf(log.records[i].cents) <= bound + 1.0e-3f, "per-grain microdetune stays within its class bound");
        if (fabsf(log.records[i].cents) > 0.01f) nonzero++;
    }
    CHECK(nonzero > 0, "microdetune is actually applied to some grains");

    // Fixed for the whole grain lifetime: find a freshly spawned grain and watch
    // its detune/rate while it stays alive.
    BubbleVoice_t* target = NULL;
    float left[BUBBLES_BLOCK_SIZE];
    float right[BUBBLES_BLOCK_SIZE];
    for (int t = 0; t < 20000 && target == NULL; t++) {
        bubble_engine_process(&engine, input, left, right, BUBBLES_BLOCK_SIZE);
        for (int v = 0; v < engine.active_voice_limit; v++) {
            if (is_fresh_spawn(&engine.voices[v])) {
                target = &engine.voices[v];
                break;
            }
        }
    }
    CHECK(target != NULL, "found a freshly spawned voice to watch");
    float cents_at_birth = target->microdetune_cents;
    float rate_at_birth = target->rate;
    int observed = 0;
    for (int i = 0; i < 100; i++) {
        bubble_engine_process(&engine, input, left, right, BUBBLES_BLOCK_SIZE);
        // Allow a pending-onset grain to reach its real start before comparing.
        if (target->state == VOICE_STATE_PENDING_ONSET) continue;
        if (target->state != VOICE_STATE_PLAYING) break;
        CHECK_CLOSE(target->microdetune_cents, cents_at_birth, 1.0e-6f, "microdetune is constant during the grain lifetime");
        CHECK_CLOSE(target->rate, rate_at_birth, 1.0e-6f, "microdetuned playback rate is constant during the grain lifetime");
        observed++;
    }
    CHECK(observed > 0, "observed the grain across at least one control block");

    // Reproducible: two identical runs must produce identical spawn metadata.
    SpawnLog log_b;
    log_b.count = 0;
    init_memory(delay, 192000);
    BubbleEngine_t engine_b;
    bubble_engine_init(&engine_b, delay, &cfg);
    run_and_capture(&engine_b, input, 1500, &log_b, buffer_size);
    SpawnLog log_a;
    log_a.count = 0;
    init_memory(delay, 192000);
    BubbleEngine_t engine_a;
    bubble_engine_init(&engine_a, delay, &cfg);
    run_and_capture(&engine_a, input, 1500, &log_a, buffer_size);
    CHECK(log_a.count == log_b.count, "identical seeds capture the same number of spawns");
    for (int i = 0; i < log_a.count; i++) {
        CHECK(log_a.records[i].bubble_class == log_b.records[i].bubble_class, "spawn class is reproducible");
        CHECK(log_a.records[i].offset == log_b.records[i].offset, "read offset is reproducible");
        CHECK_CLOSE(log_a.records[i].cents, log_b.records[i].cents, 1.0e-6f, "microdetune is reproducible");
        CHECK_CLOSE(log_a.records[i].rate, log_b.records[i].rate, 1.0e-6f, "spawn rate is reproducible");
    }
    return 0;
}

static int state_is_attack_like(int state) {
    return state == (int)ENGINE_STATE_TRANSIENT_BURST || state == (int)ENGINE_STATE_ATTACK_ONGOING;
}

static int test_reverse_is_context_conditioned(void) {
    static int16_t delay[192000];
    EngineConfig_t cfg;
    bubble_engine_default_config(&cfg);
    cfg.sample_rate = 48000.0f;
    cfg.rng_seed = 0x2E71u;
    cfg.smart_start_enable = 0;
    cfg.noise_floor = 0.0001f;
    cfg.tracking_thresh = 0.005f;
    cfg.sustain_thresh = 0.02f;
    cfg.transient_delta = 0.01f;
    cfg.density_burst = 0.0f;
    cfg.density_sustain = 0.0f;
    cfg.density_decay = 200.0f;
    cfg.burst_immediate_count = 8;
    cfg.burst_mode = BUBBLE_BURST_MODE_SPRAY;
    cfg.burst_duration_ticks = 2;
    cfg.reverse_probability = 1.0f;
    cfg.active_voice_limit = 32;
    init_memory(delay, 192000);
    BubbleEngine_t engine;
    bubble_engine_init(&engine, delay, &cfg);
    // This probe drives the phrase state machine directly; keep the authored raw
    // thresholds instead of letting the macro layer remap them.
    engine.macro_dirty_mask = 0u;

    static float input[48000];
    memset(input, 0, sizeof(input));
    for (int i = 0; i < 96; i++) input[i] = 0.9f;

    float left[BUBBLES_BLOCK_SIZE];
    float right[BUBBLES_BLOCK_SIZE];
    int attack_spawns = 0, attack_reverse = 0;
    int tail_spawns = 0, tail_reverse = 0;
    for (int offset = 0; offset < 48000; offset += BUBBLES_BLOCK_SIZE) {
        bubble_engine_process(&engine, &input[offset], left, right, BUBBLES_BLOCK_SIZE);
        int attack_like = state_is_attack_like((int)engine.engine_state);
        for (int v = 0; v < engine.active_voice_limit; v++) {
            BubbleVoice_t* voice = &engine.voices[v];
            if (!is_fresh_spawn(voice)) continue;
            if (attack_like) {
                attack_spawns++;
                if (voice->read_direction != 0u) attack_reverse++;
            } else if (engine.engine_state == ENGINE_STATE_SPARSE_DECAY) {
                tail_spawns++;
                if (voice->read_direction != 0u) tail_reverse++;
            }
        }
    }
    CHECK(attack_spawns > 0, "captured attack-phase spawns");
    CHECK(tail_spawns > 0, "captured decay-phase spawns");
    float attack_fraction = (float)attack_reverse / (float)attack_spawns;
    float tail_fraction = (float)tail_reverse / (float)tail_spawns;
    CHECK(attack_fraction < 0.35f, "attacks use significantly less reverse than the tail");
    CHECK(tail_fraction > attack_fraction + 0.3f, "decay opens reverse progressively relative to attacks");
    return 0;
}

static int test_determinism_same_seed_same_output(void) {
    static int16_t delay_a[192000];
    static int16_t delay_b[192000];
    static float input[48000];
    static float left_a[48000];
    static float right_a[48000];
    static float left_b[48000];
    static float right_b[48000];
    for (int i = 0; i < 48000; i++) {
        input[i] = 0.3f * sinf(2.0f * 3.14159265f * 220.0f * (float)i / 48000.0f);
        if ((i % 997) == 0) input[i] += 0.5f;
    }

    EngineConfig_t cfg;
    configure_sustain(&cfg, 0xDEADBEEFu);
    cfg.density_burst = 40.0f;
    cfg.density_sustain = 30.0f;
    cfg.density_decay = 10.0f;

    for (int pass = 0; pass < 2; pass++) {
        int16_t* delay = (pass == 0) ? delay_a : delay_b;
        float* left = (pass == 0) ? left_a : left_b;
        float* right = (pass == 0) ? right_a : right_b;
        init_memory(delay, 192000);
        BubbleEngine_t engine;
        bubble_engine_init(&engine, delay, &cfg);
        float block_left[BUBBLES_BLOCK_SIZE];
        float block_right[BUBBLES_BLOCK_SIZE];
        for (int offset = 0; offset < 48000; offset += BUBBLES_BLOCK_SIZE) {
            bubble_engine_process(&engine, &input[offset], block_left, block_right, BUBBLES_BLOCK_SIZE);
            for (int i = 0; i < BUBBLES_BLOCK_SIZE; i++) {
                left[offset + i] = block_left[i];
                right[offset + i] = block_right[i];
            }
        }
    }
    for (int i = 0; i < 48000; i++) {
        CHECK(left_a[i] == left_b[i], "same seed + same input => identical left output");
        CHECK(right_a[i] == right_b[i], "same seed + same input => identical right output");
    }
    return 0;
}

static int test_sample_rate_and_block_size_matrix(void) {
    const float rates[4] = {44100.0f, 48000.0f, 88200.0f, 96000.0f};
    const int blocks[6] = {32, 64, 127, 256, 512, 2048};
    static int16_t delay[192000];
    static float input[4096];
    static float left[4096];
    static float right[4096];

    EngineConfig_t cfg;
    configure_sustain(&cfg, 0x517E5u);
    cfg.pitch_mode = BUBBLE_PITCH_MODE_SHIMMER;
    cfg.shimmer_amount = 0.8f;
    cfg.reverse_probability = 0.4f;

    for (int r = 0; r < 4; r++) {
        for (int b = 0; b < 6; b++) {
            cfg.sample_rate = rates[r];
            init_memory(delay, 192000);
            BubbleEngine_t engine;
            bubble_engine_init(&engine, delay, &cfg);
            for (int i = 0; i < blocks[b]; i++) {
                input[i] = 0.35f * sinf(2.0f * 3.14159265f * 330.0f * (float)i / rates[r]);
            }
            bubble_engine_process(&engine, input, left, right, blocks[b]);
            for (int i = 0; i < blocks[b]; i++) {
                CHECK(isfinite(left[i]) && isfinite(right[i]), "output stays finite across rate/block matrix");
            }
            for (int v = 0; v < engine.active_voice_limit; v++) {
                if (engine.voices[v].state == VOICE_STATE_INACTIVE) continue;
                CHECK(fabsf(engine.voices[v].microdetune_cents) <= BUBBLES_MICRODETUNE_FREEZE_CENTS + 1.0e-3f,
                      "microdetune bound holds across sample rates");
            }
        }
    }
    return 0;
}

int main(void) {
    if (test_memory_distribution_is_recent_weighted_and_not_uniform() != 0) return 1;
    if (test_sparkle_interval_weights_and_exact_fifth() != 0) return 1;
    if (test_sparkle_zero_has_no_random_pitch() != 0) return 1;
    if (test_microdetune_is_bounded_fixed_and_reproducible() != 0) return 1;
    if (test_reverse_is_context_conditioned() != 0) return 1;
    if (test_determinism_same_seed_same_output() != 0) return 1;
    if (test_sample_rate_and_block_size_matrix() != 0) return 1;
    return 0;
}
