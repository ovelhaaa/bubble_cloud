// M2 stereo-coherence and guard regression harness.
//
// This is a white-box harness: it includes the DSP implementation so it can
// drive the static spawn/guard helpers directly, then links the engine and macro
// layers. It covers the M2.1/M2.2/M2.3 fixes plus the M2.4 canonical scheduler
// event identity milestone without changing any public API:
//
//   1. Shared stereo decisions are keyed by a *canonical logical event identity*
//      (M2.4): a stateless hash of
//      (base seed, scheduler_tick, source, event_index, child_index, kind).
//      Every real top-level scheduler path owns its own per-tick event index, so
//      an extra top-level spawn of one source can never shift the shared
//      identity of a common event of another source. The pre-M2.2/M2.3 implicit
//      `tick_shared_ordinal++` / `tick_spawn_ordinal++` counter is reproduced in
//      the same-tick tests to prove the exact failure it caused.
//   2. Saturated spawns preserve their full identity in the pending queue and
//      reuse it verbatim when they finally become a voice (including the origin
//      tick, so the shared decision is not recomputed).
//   3. Smart Start cannot invalidate the guard (M2.2). Smart Start now runs
//      before the guard clamp, so the offset that lands in `read_ptr_float` is
//      always protected for the grain's real rate.
//   4. The fixed per-grain microdetune is folded into the playback rate *before*
//      the guard-band clamp, and the forward guard is based on the real relative
//      grain travel (rate - 1), so a few cents around rate 1.0 do not shove the
//      read into a distant region or let a grain run into the write head.
//
// Required M2.4 regressions:
//   A. real same-tick top-level asymmetry (extra top-level spawn, not derived);
//   B. cross-source asymmetry (extra STRUM between two common DENSITY events);
//   C. burst asymmetry (extra SPRAY/SWARM-style burst between common events);
//   D. pending queue identity preservation;
//   E. droplet derived identity;
//   F. long real-scheduler stress through Scheduler_RunTick.

#define SOUND_BUBBLES_DSP_INTERNAL 1
#define BUBBLES_M2_SHARED_TRACE 1
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

static void zero_delay(int16_t* delay) {
    memset(delay, 0, (size_t)DELAY_SAMPLES * sizeof(delay[0]));
}

static void base_config(EngineConfig_t* cfg, uint32_t seed) {
    bubble_engine_default_config(cfg);
    cfg->sample_rate = 48000.0f;
    cfg->rng_seed = seed;
    cfg->smart_start_enable = 0;
    cfg->density_burst = 0.0f;
    cfg->density_sustain = 60.0f;
    cfg->density_decay = 30.0f;
    cfg->active_voice_limit = 32;
}

// --- Canonical-event helpers ----------------------------------------------

static SharedSpawnId_t make_id(uint32_t tick, BubbleSpawnSource_t source, uint32_t event_index, uint32_t child_index) {
    SharedSpawnId_t id;
    id.tick = tick;
    id.source = (uint32_t)source;
    id.event_index = event_index;
    id.child_index = child_index;
    return id;
}

// Begin a new logical control tick exactly as the DSP does: advance the tick and
// reset every per-source event index.
static void begin_tick(BubbleEngine_t* engine, uint32_t tick) {
    engine->scheduler_tick = tick;
    Scheduler_ResetSpawnIdentity(engine);
}

// Reserve the next canonical event identity for a source, exactly as the real
// scheduler does for each top-level spawn.
static SharedSpawnId_t next_id(BubbleEngine_t* engine, BubbleSpawnSource_t source) {
    return Scheduler_NextSpawnId(engine, source);
}

static int id_equal(SharedSpawnId_t a, SharedSpawnId_t b) {
    return a.tick == b.tick && a.source == b.source
        && a.event_index == b.event_index && a.child_index == b.child_index;
}

// Legacy M2.2 model: a single implicit ordinal per tick, advanced by *every*
// shared draw. This is precisely the `tick_shared_ordinal++` behavior replaced by
// the explicit/canonical identity. It exists only so the harness can prove the
// same-tick asymmetry failure without checking out the old tree.
static uint32_t legacy_mix32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

static float legacy_event_unit(uint32_t seed, uint32_t tick, uint32_t ordinal, uint32_t kind, uint32_t lane) {
    const float kInv24Bit = 1.0f / 16777216.0f; // 2^24
    uint32_t h = legacy_mix32(seed ^ 0x9E3779B9u);
    h ^= legacy_mix32(tick + 0x85EBCA6Bu);
    h += legacy_mix32(ordinal + 0xC2B2AE35u);
    h ^= legacy_mix32(kind + 0x27D4EB2Fu);
    h += legacy_mix32(lane + 0x165667B1u);
    return (float)(legacy_mix32(h) >> 8) * kInv24Bit;
}

static float legacy_shared_draw(uint32_t seed, uint32_t tick, uint32_t* running_ordinal,
                                BubbleSharedDecisionKind_t kind) {
    const uint32_t ordinal = (*running_ordinal)++;
    return legacy_event_unit(seed, tick, ordinal, (uint32_t)kind, BUBBLES_SHARED_LANE_VALUE);
}

// --- 1. Shared decisions ignore temporary coherence divergence (test A) ---

static int test_shared_decisions_ignore_coherence_divergence(void) {
    static int16_t delay_l[DELAY_SAMPLES];
    static int16_t delay_r[DELAY_SAMPLES];
    EngineConfig_t cfg;
    base_config(&cfg, 0xC0FFEEu);
    zero_delay(delay_l);
    zero_delay(delay_r);

    BubbleEngine_t left;
    BubbleEngine_t right;
    bubble_engine_init(&left, delay_l, &cfg);
    bubble_engine_init(&right, delay_r, &cfg);
    bubble_engine_set_channel_decorrelation(&left, 0u);
    bubble_engine_set_channel_decorrelation(&right, 0x55555555u);

    // L enters an attack (coherence 0.80), R stays in the sustain region
    // (coherence 0.22). Both process the same event sequence with their own
    // coherence, so each makes a different share/local choice.
    left.engine_state = ENGINE_STATE_TRANSIENT_BURST;
    right.engine_state = ENGINE_STATE_SUSTAIN_BODY;
    const float coherence_left = ResolveSpawnCoherence(&left);
    const float coherence_right = ResolveSpawnCoherence(&right);
    CHECK(fabsf(coherence_left - coherence_right) > 0.1f,
          "test setup actually diverges the two coherence values");

    for (uint32_t tick = 1; tick <= 256; tick++) {
        begin_tick(&left, tick);
        begin_tick(&right, tick);
        for (uint32_t e = 0; e < 4u; e++) {
            SharedSpawnId_t id = make_id(tick, BUBBLES_SPAWN_SOURCE_DENSITY, e, 0u);
            (void)SharedSpawnRandom(&left, coherence_left, id, BUBBLES_SHARED_DECISION_CLASS);
            (void)SharedSpawnRandom(&right, coherence_right, id, BUBBLES_SHARED_DECISION_CLASS);
        }
    }
    CHECK(left.rng_state != right.rng_state,
          "per-channel streams stay decorrelated during divergence");

    // The shared candidate of a logical event is addressed by identity, so once
    // both channels take the shared branch the values are bit-identical even
    // though their coherence histories differed.
    begin_tick(&left, 100000);
    begin_tick(&right, 100000);
    int shared_matches = 0;
    for (uint32_t i = 0; i < 256u; i++) {
        SharedSpawnId_t id = make_id(100000, BUBBLES_SPAWN_SOURCE_DENSITY, i, 0u);
        const float a = SharedSpawnRandom(&left, 1.0f, id, BUBBLES_SHARED_DECISION_REGION_TIER);
        const float b = SharedSpawnRandom(&right, 1.0f, id, BUBBLES_SHARED_DECISION_REGION_TIER);
        if (a == b) shared_matches++;
    }
    CHECK(shared_matches == 256, "shared decisions reproduce identically across coherence divergence");
    return 0;
}

// --- 2. Unequal spawn count reconverges (test B) --------------------------

static int test_unequal_spawn_count_reconverges(void) {
    static int16_t delay_l[DELAY_SAMPLES];
    static int16_t delay_r[DELAY_SAMPLES];
    EngineConfig_t cfg;
    base_config(&cfg, 0x51EEDu);
    zero_delay(delay_l);
    zero_delay(delay_r);

    BubbleEngine_t left;
    BubbleEngine_t right;
    bubble_engine_init(&left, delay_l, &cfg);
    bubble_engine_init(&right, delay_r, &cfg);
    bubble_engine_set_channel_decorrelation(&left, 0u);
    bubble_engine_set_channel_decorrelation(&right, 0x55555555u);

    // Both channels process the same logical event every tick, but L periodically
    // consumes additional (derived) shared decisions that R never draws. Under the
    // canonical identity model those extras live in the derived namespace and never
    // shift the shared decision of a later primary event index.
    for (uint32_t tick = 1; tick <= 512; tick++) {
        begin_tick(&left, tick);
        begin_tick(&right, tick);

        const SharedSpawnId_t ord = next_id(&left, BUBBLES_SPAWN_SOURCE_DENSITY);
        const SharedSpawnId_t r_ord = next_id(&right, BUBBLES_SPAWN_SOURCE_DENSITY);
        const float l_first = SharedSpawnRandom(&left, 1.0f, ord, BUBBLES_SHARED_DECISION_CLASS);
        const float r_first = SharedSpawnRandom(&right, 1.0f, r_ord, BUBBLES_SHARED_DECISION_CLASS);
        CHECK(l_first == r_first, "first logical event keeps the same shared decision");

        const int l_extra = (tick % 4u == 0u) ? 3 : ((tick % 7u == 0u) ? 1 : 0);
        const int r_extra = (tick % 5u == 0u) ? 2 : 0;
        for (int e = 0; e < l_extra; e++) {
            SharedSpawnId_t child = SpawnDerivedId(ord, (uint32_t)(1 + e));
            (void)SharedSpawnRandom(&left, 1.0f, child, BUBBLES_SHARED_DECISION_REGION_TIER);
        }
        for (int e = 0; e < r_extra; e++) {
            SharedSpawnId_t child = SpawnDerivedId(r_ord, (uint32_t)(1 + e));
            (void)SharedSpawnRandom(&right, 1.0f, child, BUBBLES_SHARED_DECISION_REGION_TIER);
        }
    }

    // A large asymmetry on one tick must not leak into the next logical event.
    begin_tick(&left, 9000);
    begin_tick(&right, 9000);
    const SharedSpawnId_t big_ord = next_id(&left, BUBBLES_SPAWN_SOURCE_DENSITY);
    (void)next_id(&right, BUBBLES_SPAWN_SOURCE_DENSITY);
    for (int e = 0; e < 32; e++) {
        SharedSpawnId_t child = SpawnDerivedId(big_ord, (uint32_t)e);
        (void)SharedSpawnRandom(&left, 1.0f, child, BUBBLES_SHARED_DECISION_OFFSET_BAND);
    }
    begin_tick(&left, 9001);
    begin_tick(&right, 9001);
    for (uint32_t i = 0; i < 32u; i++) {
        SharedSpawnId_t id = make_id(9001, BUBBLES_SPAWN_SOURCE_DENSITY, i, 0u);
        const float a = SharedSpawnRandom(&left, 1.0f, id, BUBBLES_SHARED_DECISION_OFFSET_BAND);
        const float b = SharedSpawnRandom(&right, 1.0f, id, BUBBLES_SHARED_DECISION_OFFSET_BAND);
        CHECK(a == b, "shared decisions reconverge after unequal spawn counts");
    }
    return 0;
}

// --- 3. Long stereo divergence keeps event identity (test C) --------------

static int test_long_stereo_divergence_keeps_event_identity(void) {
    static int16_t delay_l[DELAY_SAMPLES];
    static int16_t delay_r[DELAY_SAMPLES];
    EngineConfig_t cfg;
    base_config(&cfg, 0x10E6E1u);
    zero_delay(delay_l);
    zero_delay(delay_r);

    BubbleEngine_t left;
    BubbleEngine_t right;
    bubble_engine_init(&left, delay_l, &cfg);
    bubble_engine_init(&right, delay_r, &cfg);
    bubble_engine_set_channel_decorrelation(&left, 0u);
    bubble_engine_set_channel_decorrelation(&right, 0x55555555u);

    // Thousands of ticks with alternating phrase states, different bubble
    // classes and asymmetric derived spawn counts per tick. The first logical
    // event each tick must never depend on the other channel's draw history.
    int mismatches = 0;
    for (uint32_t tick = 1; tick <= 4000; tick++) {
        const EngineState_t state =
            (tick % 3u == 0u) ? ENGINE_STATE_TRANSIENT_BURST :
            ((tick % 3u == 1u) ? ENGINE_STATE_SUSTAIN_BODY : ENGINE_STATE_SPARSE_DECAY);
        left.engine_state = state;
        right.engine_state = state;
        begin_tick(&left, tick);
        begin_tick(&right, tick);

        const SharedSpawnId_t a_ord = next_id(&left, BUBBLES_SPAWN_SOURCE_DENSITY);
        const SharedSpawnId_t r_a_ord = next_id(&right, BUBBLES_SPAWN_SOURCE_DENSITY);
        const float a = SharedSpawnRandom(&left, 1.0f, a_ord, BUBBLES_SHARED_DECISION_CLASS);
        const float b = SharedSpawnRandom(&right, 1.0f, r_a_ord, BUBBLES_SHARED_DECISION_CLASS);
        if (a != b) mismatches++;

        const int l_extra = (int)((tick * 7u + 3u) % 5u);
        const int r_extra = (int)((tick * 11u + 1u) % 4u);
        for (int e = 1; e < l_extra; e++) {
            const BubbleSharedDecisionKind_t k = (e % 2) ? BUBBLES_SHARED_DECISION_REGION_TIER
                                                          : BUBBLES_SHARED_DECISION_CLASS;
            SharedSpawnId_t child = SpawnDerivedId(a_ord, (uint32_t)e);
            (void)SharedSpawnRandom(&left, 1.0f, child, k);
        }
        for (int e = 1; e < r_extra; e++) {
            const BubbleSharedDecisionKind_t k = (e % 2) ? BUBBLES_SHARED_DECISION_OFFSET_BAND
                                                          : BUBBLES_SHARED_DECISION_CLASS;
            SharedSpawnId_t child = SpawnDerivedId(r_a_ord, (uint32_t)e);
            (void)SharedSpawnRandom(&right, 1.0f, child, k);
        }
    }
    CHECK(mismatches == 0, "shared event identity holds across long asymmetric divergence");
    return 0;
}

// --- 4. Determinism of the shared decision (test D) -----------------------

static int test_shared_event_decision_determinism(void) {
    static int16_t delay_a[DELAY_SAMPLES];
    static int16_t delay_b[DELAY_SAMPLES];
    EngineConfig_t cfg;
    base_config(&cfg, 0x0D37E1u);
    zero_delay(delay_a);
    zero_delay(delay_b);

    BubbleEngine_t a;
    BubbleEngine_t b;
    bubble_engine_init(&a, delay_a, &cfg);
    bubble_engine_init(&b, delay_b, &cfg);

    // Burn a very different number of prior shared decisions into `a`.
    begin_tick(&a, 77);
    for (uint32_t i = 0; i < 1000u; i++) {
        SharedSpawnId_t id = make_id(77, BUBBLES_SPAWN_SOURCE_DENSITY, i, 0u);
        (void)SharedSpawnRandom(&a, 1.0f, id, BUBBLES_SHARED_DECISION_CLASS);
    }

    // Same seed + same event identity => exact same shared decision, regardless
    // of the draw history before it.
    for (int i = 0; i < 64; i++) {
        SharedSpawnId_t identity = make_id(4242, BUBBLES_SPAWN_SOURCE_DENSITY, 5u, 0u);
        const float va = SharedSpawnRandom(&a, 1.0f, identity, BUBBLES_SHARED_DECISION_OFFSET_BAND);
        const float vb = SharedSpawnRandom(&b, 1.0f, identity, BUBBLES_SHARED_DECISION_OFFSET_BAND);
        CHECK(va == vb, "same seed + same event identity => exact same shared decision");
    }
    return 0;
}

// --- 5. Channel-local decisions stay decorrelated (test E) ----------------

static int test_channel_local_decisions_stay_decorrelated(void) {
    static int16_t delay_l[DELAY_SAMPLES];
    static int16_t delay_r[DELAY_SAMPLES];
    EngineConfig_t cfg;
    base_config(&cfg, 0xDEC011u);
    zero_delay(delay_l);
    zero_delay(delay_r);

    BubbleEngine_t left;
    BubbleEngine_t right;
    bubble_engine_init(&left, delay_l, &cfg);
    bubble_engine_init(&right, delay_r, &cfg);
    bubble_engine_set_channel_decorrelation(&left, 0u);
    bubble_engine_set_channel_decorrelation(&right, 0x55555555u);

    int local_diff = 0;
    for (int i = 0; i < 512; i++) {
        const float a = RandomFloat01(&left);
        const float b = RandomFloat01(&right);
        if (a != b) local_diff++;
    }
    CHECK(local_diff > 500, "channel-local decisions remain decorrelated between L/R");

    begin_tick(&left, 7);
    begin_tick(&right, 7);
    SharedSpawnId_t id = make_id(7, BUBBLES_SPAWN_SOURCE_DENSITY, 0u, 0u);
    CHECK(SharedSpawnRandom(&left, 1.0f, id, BUBBLES_SHARED_DECISION_CLASS) ==
              SharedSpawnRandom(&right, 1.0f, id, BUBBLES_SHARED_DECISION_CLASS),
          "shared decision matches even though the local streams differ");
    return 0;
}

// --- 6. Guard clamp is based on the real relative grain travel ------------

static int test_guard_clamp_relative_span_math(void) {
    const int32_t guard = BUBBLES_GUARD_ZONE_SAMPLES;
    const int32_t buffers[4] = {88200, 96000, 176400, 192000};
    const float durations[3] = {480.0f, 4800.0f, 19200.0f};

    const float ratio_plus_two_cents = powf(2.0f, 2.0f / 1200.0f);
    const float ratio_minus_two_cents = powf(2.0f, -2.0f / 1200.0f);

    for (int b = 0; b < 4; b++) {
        const int32_t buffer = buffers[b];
        const int32_t max_guarded = buffer - guard - 1;
        for (int d = 0; d < 3; d++) {
            const float duration = durations[d];
            const float drift = GuardFloatDriftMargin(buffer, duration);

            // Tiny positive microdetune around rate 1.0: clamp to the real
            // relative travel, never a whole grain duration behind the write head.
            const float forward_rate = 1.0f * ratio_plus_two_cents;
            const int32_t expected_min = guard
                + (int32_t)ceilf(duration * (forward_rate - 1.0f) + drift)
                + BUBBLES_GUARD_PATH_MARGIN;
            const int32_t clamped = ClampSpawnOffsetForGuard(guard, forward_rate, duration, buffer);
            CHECK(clamped == expected_min,
                  "tiny positive detune clamps to the real relative travel, not the full rate");
            CHECK(clamped < guard + (int32_t)duration,
                  "tiny positive detune does not shove the read a full grain duration back");

            // Tiny negative microdetune: the read falls behind the write head, so
            // an already-safe offset must be left untouched.
            const float falling_rate = 1.0f * ratio_minus_two_cents;
            const int32_t safe_offset = max_guarded / 2;
            CHECK(ClampSpawnOffsetForGuard(safe_offset, falling_rate, duration, buffer) == safe_offset,
                  "negative detune below rate 1.0 never pushes the read away");

            // Octave-up with microdetune: relative catch-up is (rate - 1).
            const float octave_rate = 2.0f * ratio_plus_two_cents;
            const int32_t octave_min = guard
                + (int32_t)ceilf(duration * (octave_rate - 1.0f) + drift)
                + BUBBLES_GUARD_PATH_MARGIN;
            CHECK(ClampSpawnOffsetForGuard(guard, octave_rate, duration, buffer) == octave_min,
                  "octave-up guard uses the relative catch-up rate");

            // Reverse with microdetune: bound by (1 + |rate|) * duration.
            const float reverse_rate = -2.0f * ratio_plus_two_cents;
            const int32_t reverse_span = (int32_t)ceilf(duration * (1.0f + fabsf(reverse_rate)) + drift)
                + BUBBLES_GUARD_PATH_MARGIN;
            const int32_t reverse_max = max_guarded - reverse_span;
            const int32_t expected_reverse = (reverse_max < guard) ? guard : reverse_max;
            CHECK(ClampSpawnOffsetForGuard(max_guarded, reverse_rate, duration, buffer) == expected_reverse,
                  "reverse guard uses the full reverse travel plus margin");
        }
    }
    return 0;
}

// Simulate the runtime directional guard across a grain's predicted lifetime.
static int simulate_guard_hits(int32_t read_offset, float rate, int32_t duration_samples, int32_t buffer) {
    int32_t write_ptr = 0;
    float read = (float)WrapIntIndex(write_ptr - read_offset, buffer);
    const int32_t life = (duration_samples > 1) ? (duration_samples - 1) : 0;
    for (int32_t t = 0; t < life; t++) {
        read += rate;
        read = WrapFloatIndex(read, (float)buffer);
        write_ptr = WrapIntIndex(write_ptr + 1, buffer);
        if (CheckGuardZoneDirectional(write_ptr, read, rate, buffer)) {
            return 1;
        }
    }
    return 0;
}

static int test_guard_clamp_keeps_read_out_of_guard_zone(void) {
    const int32_t guard = BUBBLES_GUARD_ZONE_SAMPLES;
    const int32_t buffers[4] = {88200, 96000, 176400, 192000};
    const float rates[] = {
        1.0f, 1.00116f, 0.99884f, 2.0093f, -1.00116f, -2.0093f,
        1.00463f, -1.00463f, 3.0f, -3.0f, 4.0f, -4.0f,
    };
    const int32_t durations[] = {480, 4800, 19200, 43200};

    for (int b = 0; b < 4; b++) {
        const int32_t buffer = buffers[b];
        const int32_t max_guarded = buffer - guard - 1;
        for (unsigned ri = 0; ri < sizeof(rates) / sizeof(rates[0]); ri++) {
            const float rate = rates[ri];
            for (unsigned di = 0; di < sizeof(durations) / sizeof(durations[0]); di++) {
                const int32_t duration = durations[di];

                // Only assert feasibility: a grain whose predicted path exceeds the
                // ring buffer cannot be protected by any spawn clamp.
                const float relative = (rate < 0.0f) ? (1.0f + fabsf(rate))
                                                     : ((rate > 1.0f) ? (rate - 1.0f) : 0.0f);
                const double predicted = (double)relative * (double)duration
                    + (double)GuardFloatDriftMargin(buffer, (float)duration)
                    + (double)guard + BUBBLES_GUARD_PATH_MARGIN;
                if (predicted > (double)buffer) {
                    continue;
                }

                const int32_t clamped = ClampSpawnOffsetForGuard(guard, rate, (float)duration, buffer);
                CHECK(clamped >= guard && clamped <= max_guarded,
                      "clamped offset stays inside the guard-banded range");
                if (simulate_guard_hits(clamped, rate, duration, buffer) != 0) {
                    fprintf(stderr,
                            "FAIL %s:%d: guard hit before grain end (buffer=%d rate=%.5f dur=%d offset=%d)\n",
                            __FILE__, __LINE__, buffer, (double)rate, duration, clamped);
                    return 1;
                }
            }
        }
    }
    return 0;
}

// --- 7. Engine-level guard with real microdetuned grains ------------------

typedef struct {
    int pitch_mode;
    int force_reverse;
    int freeze;
    BubbleClass_t bubble_class;
    float body_duration_ms; // 0 = leave defaults
} GuardScenario;

static int run_guard_scenario(const GuardScenario* scenario, float sample_rate, int smart_start) {
    static int16_t delay[DELAY_SAMPLES];
    zero_delay(delay);

    EngineConfig_t cfg;
    base_config(&cfg, 0x6A11Du ^ (uint32_t)(sample_rate * 0.5f));
    cfg.sample_rate = sample_rate;
    cfg.pitch_mode = scenario->pitch_mode;
    cfg.shimmer_amount = (scenario->pitch_mode == BUBBLE_PITCH_MODE_SHIMMER) ? 1.0f : 0.0f;
    cfg.freeze_enabled = scenario->freeze;
    cfg.freeze_amount = scenario->freeze ? 1.0f : 0.0f;
    cfg.smart_start_enable = smart_start ? 1 : 0;
    cfg.smart_start_range = smart_start ? 64 : cfg.smart_start_range;
    if (scenario->body_duration_ms > 0.0f) {
        cfg.class_configs[BUBBLE_CLASS_SUSTAIN_BODY].duration_ms_min = scenario->body_duration_ms;
        cfg.class_configs[BUBBLE_CLASS_SUSTAIN_BODY].duration_ms_max = scenario->body_duration_ms;
    }

    BubbleEngine_t engine;
    bubble_engine_init(&engine, delay, &cfg);
    const int32_t buffer = (int32_t)SoundBubbles_RequiredBufferSamples(sample_rate);
    const int32_t max_guarded = buffer - BUBBLES_GUARD_ZONE_SAMPLES - 1;

    int positive_detune = 0;
    int negative_detune = 0;
    for (int i = 0; i < 600; i++) {
        engine.force_reverse_spawns = scenario->force_reverse ? 1 : 0;
        SharedSpawnId_t id = make_id((uint32_t)i, BUBBLES_SPAWN_SOURCE_DENSITY, (uint32_t)i, 0u);
        Voice_SpawnInit(&engine, 0, scenario->bubble_class, 0, id);
        BubbleVoice_t* voice = &engine.voices[0];

        if (voice->microdetune_cents > 0.0005f) positive_detune++;
        if (voice->microdetune_cents < -0.0005f) negative_detune++;

        if (scenario->force_reverse) {
            CHECK(voice->read_direction == 1u, "reverse scenario spawned reverse grains");
        }

        int32_t duration_samples = (int32_t)lroundf(1.0f / voice->phase_inc);
        if (duration_samples < 1) duration_samples = 1;
        int32_t offset = WrapIntIndex(engine.write_ptr - (int32_t)voice->read_ptr_float, buffer);
        CHECK(offset >= BUBBLES_GUARD_ZONE_SAMPLES && offset <= max_guarded,
              "engine spawn keeps the read offset inside the guard-banded range");

        if (simulate_guard_hits(offset, voice->rate, duration_samples, buffer) != 0) {
            fprintf(stderr,
                    "FAIL %s:%d: premature guard at sr=%.0f rate=%.5f dur=%d offset=%d cents=%.4f class=%d rev=%d smart=%d\n",
                    __FILE__, __LINE__, (double)sample_rate, (double)voice->rate, duration_samples,
                    offset, (double)voice->microdetune_cents, (int)scenario->bubble_class,
                    (int)voice->read_direction, smart_start);
            return 1;
        }
    }

    // Every scenario must actually exercise microdetune in both directions; this
    // also proves the detune is constant per grain and applied to the runtime rate.
    CHECK(positive_detune > 0, "scenario exercised positive microdetune grains");
    CHECK(negative_detune > 0, "scenario exercised negative microdetune grains");
    return 0;
}

static const GuardScenario GUARD_SCENARIOS[] = {
    // rate base 1.0 +/- microdetune
    { BUBBLE_PITCH_MODE_UNISON, 0, 0, BUBBLE_CLASS_SHORT_INTERMEDIATE, 0.0f },
    // long grains at every sample rate (a stress case for the guard path)
    { BUBBLE_PITCH_MODE_UNISON, 0, 0, BUBBLE_CLASS_SUSTAIN_BODY, 0.0f },
    // octave-up + microdetune
    { BUBBLE_PITCH_MODE_OCTAVE_UP, 0, 0, BUBBLE_CLASS_SHORT_INTERMEDIATE, 0.0f },
    { BUBBLE_PITCH_MODE_OCTAVE_UP, 0, 0, BUBBLE_CLASS_SUSTAIN_BODY, 0.0f },
    // reverse + microdetune
    { BUBBLE_PITCH_MODE_UNISON, 1, 0, BUBBLE_CLASS_SUSTAIN_BODY, 0.0f },
    { BUBBLE_PITCH_MODE_OCTAVE_UP, 1, 0, BUBBLE_CLASS_SUSTAIN_BODY, 0.0f },
    // Sparkle (up to +19) + microdetune
    { BUBBLE_PITCH_MODE_SHIMMER, 0, 0, BUBBLE_CLASS_SUSTAIN_BODY, 0.0f },
    // freeze (largest microdetune bound)
    { BUBBLE_PITCH_MODE_UNISON, 0, 1, BUBBLE_CLASS_SUSTAIN_BODY, 0.0f },
    // long reverse grain at 96 kHz, clamping active
    { BUBBLE_PITCH_MODE_UNISON, 1, 0, BUBBLE_CLASS_SUSTAIN_BODY, 900.0f },
};

static int test_engine_guard_with_microdetune(void) {
    const float sample_rates[4] = {44100.0f, 48000.0f, 88200.0f, 96000.0f};
    for (unsigned s = 0; s < sizeof(GUARD_SCENARIOS) / sizeof(GUARD_SCENARIOS[0]); s++) {
        for (int r = 0; r < 4; r++) {
            if (run_guard_scenario(&GUARD_SCENARIOS[s], sample_rates[r], 0) != 0) {
                return 1;
            }
        }
    }
    return 0;
}

// --- 8. Smart Start cannot invalidate the guard (M2.2) --------------------

static int test_smart_start_cannot_invalidate_guard(void) {
    const float sample_rates[4] = {44100.0f, 48000.0f, 88200.0f, 96000.0f};

    // Smart Start now runs before the clamp, so the same scenario matrix used for
    // the plain guard must hold with Smart Start enabled. The zeroed delay buffer
    // makes Smart Start propose the first valid (lowest) candidate, i.e. it pulls
    // a safe offset down toward the guard band; the final clamp must restore it.
    for (unsigned s = 0; s < sizeof(GUARD_SCENARIOS) / sizeof(GUARD_SCENARIOS[0]); s++) {
        for (int r = 0; r < 4; r++) {
            if (run_guard_scenario(&GUARD_SCENARIOS[s], sample_rates[r], 1) != 0) {
                return 1;
            }
        }
    }

    // Explicitly reproduce the pre-M2.2 hazard: a safe forward-pitch offset is
    // lowered by Smart Start into the forbidden [guard, forward_min) band, and
    // only the final clamp prevents that offset from reaching `read_ptr_float`.
    static int16_t delay[DELAY_SAMPLES];
    for (int r = 0; r < 4; r++) {
        const float sample_rate = sample_rates[r];
        zero_delay(delay);
        EngineConfig_t cfg;
        base_config(&cfg, 0x5A117u ^ (uint32_t)(sample_rate * 0.5f));
        cfg.sample_rate = sample_rate;
        cfg.pitch_mode = BUBBLE_PITCH_MODE_OCTAVE_UP;
        cfg.smart_start_enable = 1;
        cfg.smart_start_range = 64;
        BubbleEngine_t engine;
        bubble_engine_init(&engine, delay, &cfg);
        const int32_t buffer = (int32_t)SoundBubbles_RequiredBufferSamples(sample_rate);

        int reproduced = 0;
        for (int i = 0; i < 64; i++) {
            SharedSpawnId_t id = make_id((uint32_t)i, BUBBLES_SPAWN_SOURCE_DENSITY, (uint32_t)i, 0u);
            Voice_SpawnInit(&engine, 0, BUBBLE_CLASS_SUSTAIN_BODY, 0, id);
            BubbleVoice_t* voice = &engine.voices[0];
            if (voice->rate <= 1.0f) continue;
            const int32_t offset = WrapIntIndex(engine.write_ptr - (int32_t)voice->read_ptr_float, buffer);
            if (offset <= BUBBLES_GUARD_ZONE_SAMPLES) continue;
            // With the zeroed buffer Smart Start would return offset - range.
            const int32_t refined = RefineReadOffsetSmartStart(&engine, offset, 64, buffer);
            if (refined < offset) reproduced++;
        }
        CHECK(reproduced > 0,
              "Smart Start would move a safe forward offset toward the forbidden band");
    }
    return 0;
}

// --- 9. Determinism of a fixed-seed spawn sequence ------------------------

static int test_fixed_seed_spawn_sequence_is_deterministic(void) {
    static int16_t delay_a[DELAY_SAMPLES];
    static int16_t delay_b[DELAY_SAMPLES];
    EngineConfig_t cfg;
    base_config(&cfg, 0x0D37E1u);
    cfg.pitch_mode = BUBBLE_PITCH_MODE_SHIMMER;
    cfg.shimmer_amount = 0.7f;
    cfg.reverse_probability = 0.4f;

    BubbleEngine_t first;
    BubbleEngine_t second;
    zero_delay(delay_a);
    zero_delay(delay_b);
    bubble_engine_init(&first, delay_a, &cfg);
    bubble_engine_init(&second, delay_b, &cfg);

    for (int i = 0; i < 2000; i++) {
        first.engine_state = (i % 5 == 0) ? ENGINE_STATE_ATTACK_ONGOING : ENGINE_STATE_SUSTAIN_BODY;
        second.engine_state = first.engine_state;
        SharedSpawnId_t id = make_id((uint32_t)i, BUBBLES_SPAWN_SOURCE_DENSITY, (uint32_t)i, 0u);
        Voice_SpawnInit(&first, 0, BUBBLE_CLASS_SHORT_INTERMEDIATE, 0, id);
        Voice_SpawnInit(&second, 0, BUBBLE_CLASS_SHORT_INTERMEDIATE, 0, id);
        BubbleVoice_t* a = &first.voices[0];
        BubbleVoice_t* b = &second.voices[0];
        CHECK(a->source_region_id == b->source_region_id && a->memory_tier == b->memory_tier,
              "identical seeds produce identical shared spawn decisions");
        CHECK(a->microdetune_cents == b->microdetune_cents && a->rate == b->rate,
              "identical seeds produce identical microdetune and rate");
    }
    return 0;
}

// --- 10. Same-tick derived asymmetry does not shift a common spawn ---------

static int test_same_tick_asymmetry_does_not_shift_common_spawns(void) {
    static int16_t delay_l[DELAY_SAMPLES];
    static int16_t delay_r[DELAY_SAMPLES];
    EngineConfig_t cfg;
    base_config(&cfg, 0xA5C11Du);
    zero_delay(delay_l);
    zero_delay(delay_r);

    BubbleEngine_t left;
    BubbleEngine_t right;
    bubble_engine_init(&left, delay_l, &cfg);
    bubble_engine_init(&right, delay_r, &cfg);

    const uint32_t seed = left.shared_event_seed;
    const uint32_t tick = 1u;

    // -- M2.2 model: every shared draw advances one running ordinal. This is the
    //    pre-M2.3 behavior and must reproduce the asymmetry bug.
    {
        uint32_t legacy_l = 0u;
        uint32_t legacy_r = 0u;
        const float a_l = legacy_shared_draw(seed, tick, &legacy_l, BUBBLES_SHARED_DECISION_CLASS);
        const float a_r = legacy_shared_draw(seed, tick, &legacy_r, BUBBLES_SHARED_DECISION_CLASS);
        CHECK(a_l == a_r, "M2.2 model: event A aligns on both channels");
        // L-only extra spawn.
        (void)legacy_shared_draw(seed, tick, &legacy_l, BUBBLES_SHARED_DECISION_CLASS);
        const float b_l = legacy_shared_draw(seed, tick, &legacy_l, BUBBLES_SHARED_DECISION_CLASS);
        const float b_r = legacy_shared_draw(seed, tick, &legacy_r, BUBBLES_SHARED_DECISION_CLASS);
        CHECK(b_l != b_r, "M2.2 implicit counter reproduces the same-tick asymmetry failure");
    }

    // -- Canonical identity: a derived child never consumes a primary event index,
    //    so B keeps its event index on both channels.
    begin_tick(&left, tick);
    begin_tick(&right, tick);
    const SharedSpawnId_t a_ord = next_id(&left, BUBBLES_SPAWN_SOURCE_DENSITY);
    const SharedSpawnId_t r_a_ord = next_id(&right, BUBBLES_SPAWN_SOURCE_DENSITY);
    CHECK(a_ord.event_index == 0u && r_a_ord.event_index == 0u,
          "event A has the same explicit event index on L/R");
    const float sa_l = SharedSpawnRandom(&left, 1.0f, a_ord, BUBBLES_SHARED_DECISION_CLASS);
    const float sa_r = SharedSpawnRandom(&right, 1.0f, r_a_ord, BUBBLES_SHARED_DECISION_CLASS);
    CHECK(sa_l == sa_r, "event A produces the same shared decision on L/R");

    const SharedSpawnId_t extra_l = SpawnDerivedId(a_ord, 1u);
    (void)SharedSpawnRandom(&left, 1.0f, extra_l, BUBBLES_SHARED_DECISION_CLASS);

    const SharedSpawnId_t b_ord = next_id(&left, BUBBLES_SPAWN_SOURCE_DENSITY);
    const SharedSpawnId_t r_b_ord = next_id(&right, BUBBLES_SPAWN_SOURCE_DENSITY);
    CHECK(b_ord.event_index == 1u && r_b_ord.event_index == 1u,
          "derived spawn does not shift event B's explicit event index");
    CHECK(!id_equal(extra_l, b_ord), "derived child identity cannot collide with a primary event");
    const float sb_l = SharedSpawnRandom(&left, 1.0f, b_ord, BUBBLES_SHARED_DECISION_CLASS);
    const float sb_r = SharedSpawnRandom(&right, 1.0f, r_b_ord, BUBBLES_SHARED_DECISION_CLASS);
    CHECK(sb_l == sb_r, "event B produces the same shared decision on L/R after the derived spawn");
    CHECK(sb_l != sa_l, "distinct logical spawns keep distinct shared identities");
    return 0;
}

// --- 11. Saturated spawns preserve their full identity in the queue ------

static int test_pending_spawn_queue_preserves_identity(void) {
    static int16_t delay[DELAY_SAMPLES];
    EngineConfig_t cfg;
    base_config(&cfg, 0xB0BAC1u);
    cfg.active_voice_limit = 4;
    cfg.droplet_enable = 0; // keep the queue deterministic for this test
    zero_delay(delay);

    BubbleEngine_t engine;
    bubble_engine_init(&engine, delay, &cfg);
    begin_tick(&engine, 7);

    // Saturate the pool with playing voices.
    for (int i = 0; i < engine.active_voice_limit; i++) {
        Voice_SpawnInit(&engine, i, BUBBLE_CLASS_SUSTAIN_BODY, 0, next_id(&engine, BUBBLES_SPAWN_SOURCE_DENSITY));
    }
    CHECK(engine.pending_spawn_count == 0, "pool saturated, nothing queued yet");

    const SharedSpawnId_t req = next_id(&engine, BUBBLES_SPAWN_SOURCE_DENSITY);
    const bool immediate = Voice_RequestSpawn(&engine, BUBBLE_CLASS_SHORT_INTERMEDIATE, 0, req);
    CHECK(!immediate, "saturated request is queued rather than spawned immediately");
    CHECK(engine.pending_spawn_count == 1, "saturated request entered the pending queue");
    const SharedSpawnId_t stored = engine.pending_spawns[engine.pending_spawn_head].spawn_id;
    CHECK(id_equal(stored, req), "pending request preserved its full canonical identity");

    // Simulate materialization several ticks later; the origin tick must survive.
    engine.scheduler_tick = 99;
    Scheduler_ResetSpawnIdentity(&engine);
    engine.voices[0].state = VOICE_STATE_INACTIVE;
    const int flushed = Voice_FlushPendingSpawns(&engine, 1);
    CHECK(flushed == 1, "pending spawn flushed into the freed slot");
    CHECK(id_equal(engine.voices[0].spawn_id, req),
          "flushed voice kept the queued identity instead of recomputing it");
    CHECK(engine.voices[0].spawn_id.tick == 7u, "flushed voice kept the origin tick");

    // The shared decision of the materialized grain must equal a direct draw with
    // the original identity, i.e. it was not recomputed from the later tick.
    static int16_t delay_other[DELAY_SAMPLES];
    zero_delay(delay_other);
    BubbleEngine_t other;
    bubble_engine_init(&other, delay_other, &cfg);
    for (BubbleSharedDecisionKind_t k = BUBBLES_SHARED_DECISION_CLASS;
         k < BUBBLES_SHARED_DECISION_COUNT; k++) {
        const float direct = SharedSpawnRandom(&other, 1.0f, req, k);
        const float from_voice = SharedSpawnRandom(&engine, 1.0f, engine.voices[0].spawn_id, k);
        CHECK(direct == from_voice, "pending identity recomputes the exact same shared decision");
    }
    return 0;
}

// --- 12. Droplets have a deterministic, collision-free derived identity -------

static int test_droplet_identity_is_parent_derived_and_collision_free(void) {
    const SharedSpawnId_t p0 = make_id(5, BUBBLES_SPAWN_SOURCE_DENSITY, 3u, 0u);
    const SharedSpawnId_t p1 = make_id(5, BUBBLES_SPAWN_SOURCE_RHYTHM, 3u, 0u);
    const SharedSpawnId_t p2 = make_id(5, BUBBLES_SPAWN_SOURCE_STRUM, 9u, 0u);
    const SharedSpawnId_t c0 = SpawnDerivedId(p0, 1u);
    const SharedSpawnId_t c1 = SpawnDerivedId(p1, 1u);
    const SharedSpawnId_t c2 = SpawnDerivedId(p2, 1u);

    CHECK(c0.source == (uint32_t)BUBBLES_SPAWN_SOURCE_DROPLET, "child identity lives in the droplet source");
    CHECK(!id_equal(c0, p0) && !id_equal(c1, p1) && !id_equal(c2, p2),
          "child identity never collides with its parent");
    CHECK(!id_equal(c0, c1) && !id_equal(c1, c2) && !id_equal(c0, c2),
          "children of different parents never collide, even across sources");
    CHECK(!id_equal(SpawnDerivedId(p0, 1u), SpawnDerivedId(p0, 2u)),
          "different generations of the same parent never collide");
    CHECK(id_equal(SpawnDerivedId(p0, 1u), c0), "child identity is stable across calls");

    // A burst child has a small child_index; droplet children set the derived flag
    // so they can never alias a burst grain.
    const SharedSpawnId_t burst_child = make_id(5, BUBBLES_SPAWN_SOURCE_DENSITY, 3u, 3u);
    CHECK(!id_equal(c0, burst_child), "droplet never aliases a primary burst child");

    // Same parent + generation on both channels => same shared droplet decision,
    // and a droplet does not consume a top-level event index.
    static int16_t delay_l[DELAY_SAMPLES];
    static int16_t delay_r[DELAY_SAMPLES];
    EngineConfig_t cfg;
    base_config(&cfg, 0xD20F1Eu);
    zero_delay(delay_l);
    zero_delay(delay_r);
    BubbleEngine_t left;
    BubbleEngine_t right;
    bubble_engine_init(&left, delay_l, &cfg);
    bubble_engine_init(&right, delay_r, &cfg);
    begin_tick(&left, 5);
    begin_tick(&right, 5);
    const SharedSpawnId_t parent = next_id(&left, BUBBLES_SPAWN_SOURCE_DENSITY);
    (void)next_id(&right, BUBBLES_SPAWN_SOURCE_DENSITY);
    const SharedSpawnId_t child = SpawnDerivedId(parent, 1u);
    const SharedSpawnId_t next_primary = next_id(&left, BUBBLES_SPAWN_SOURCE_DENSITY);
    CHECK(next_primary.event_index == 1u, "droplet derivation does not consume a primary event index");
    const float direct_l = SharedSpawnRandom(&left, 1.0f, child, BUBBLES_SHARED_DECISION_OFFSET_BAND);
    const float direct_r = SharedSpawnRandom(&right, 1.0f, child, BUBBLES_SHARED_DECISION_OFFSET_BAND);
    CHECK(direct_l == direct_r, "droplet shared decision is identical across channels");
    return 0;
}

// --- 13. Long same-tick asymmetric multi-spawn stress ------------------------

static int test_long_asymmetric_multispawn_stress(void) {
    static int16_t delay_l[DELAY_SAMPLES];
    static int16_t delay_r[DELAY_SAMPLES];
    EngineConfig_t cfg;
    base_config(&cfg, 0x57E55u);
    zero_delay(delay_l);
    zero_delay(delay_r);

    BubbleEngine_t left;
    BubbleEngine_t right;
    bubble_engine_init(&left, delay_l, &cfg);
    bubble_engine_init(&right, delay_r, &cfg);
    bubble_engine_set_channel_decorrelation(&left, 0u);
    bubble_engine_set_channel_decorrelation(&right, 0x55555555u);

    const BubbleSharedDecisionKind_t kinds[3] = {
        BUBBLES_SHARED_DECISION_CLASS,
        BUBBLES_SHARED_DECISION_REGION_TIER,
        BUBBLES_SHARED_DECISION_OFFSET_BAND
    };

    uint32_t rng = 0x1234ABCDu;
    int mismatches = 0;
    for (uint32_t tick = 1; tick <= 8000; tick++) {
        const EngineState_t state = (EngineState_t)(tick % 5u);
        left.engine_state = state;
        right.engine_state = state;
        begin_tick(&left, tick);
        begin_tick(&right, tick);

        // Canonical multi-spawn count for the tick (config/state driven, equal on
        // both channels).
        rng = rng * 1664525u + 1013904223u;
        const int canonical = 1 + (int)((rng >> 28) % 3u);

        for (int s = 0; s < canonical; s++) {
            const SharedSpawnId_t ol = next_id(&left, BUBBLES_SPAWN_SOURCE_DENSITY);
            const SharedSpawnId_t orr = next_id(&right, BUBBLES_SPAWN_SOURCE_DENSITY);
            if (!id_equal(ol, orr)) mismatches++;

            for (int k = 0; k < 3; k++) {
                const float vl = SharedSpawnRandom(&left, 1.0f, ol, kinds[k]);
                const float vr = SharedSpawnRandom(&right, 1.0f, orr, kinds[k]);
                if (vl != vr) mismatches++;
            }

            // Asymmetric derived/second-generation spawns must not consume a primary
            // event index on either channel.
            const SharedSpawnId_t child = SpawnDerivedId(ol, 1u);
            if (((rng >> (s + 1)) & 1u) == 0u) {
                (void)SharedSpawnRandom(&left, 1.0f, child, BUBBLES_SHARED_DECISION_REGION_TIER);
            }
            if (((rng >> (s + 8)) & 1u) == 0u) {
                (void)SharedSpawnRandom(&right, 1.0f, child, BUBBLES_SHARED_DECISION_REGION_TIER);
            }
        }

        if (left.spawn_source_event_index[BUBBLES_SPAWN_SOURCE_DENSITY] !=
            right.spawn_source_event_index[BUBBLES_SPAWN_SOURCE_DENSITY]) mismatches++;
    }
    CHECK(mismatches == 0, "8000 asymmetric multi-spawn ticks keep shared identities aligned");
    return 0;
}

// --- 14. (A) REAL same-tick top-level asymmetry ------------------------------
//
// The extra event here is a genuine *top-level* scheduler spawn of a different
// source (STRUM): it consumes a primary canonical event index through
// Scheduler_NextSpawnId, exactly like the real scheduler. It is deliberately NOT
// a droplet or a derived child. The common DENSITY event B must keep its shared
// identity. Under the removed M2.3 per-channel top-level counter the extra moved
// every later primary ordinal, so B diverged (proven by the legacy block).
static int test_real_same_tick_top_level_asymmetry(void) {
    static int16_t delay_l[DELAY_SAMPLES];
    static int16_t delay_r[DELAY_SAMPLES];
    EngineConfig_t cfg;
    base_config(&cfg, 0x7241Du);
    zero_delay(delay_l);
    zero_delay(delay_r);

    BubbleEngine_t left;
    BubbleEngine_t right;
    bubble_engine_init(&left, delay_l, &cfg);
    bubble_engine_init(&right, delay_r, &cfg);

    const uint32_t seed = left.shared_event_seed;
    const uint32_t tick = 1u;

    // Legacy M2.3 top-level counter: A=0, X=1 (L only), B=2 on L vs 1 on R.
    {
        uint32_t legacy_l = 0u;
        uint32_t legacy_r = 0u;
        const float a_l = legacy_event_unit(seed, tick, legacy_l++, BUBBLES_SHARED_DECISION_CLASS, BUBBLES_SHARED_LANE_VALUE);
        const float a_r = legacy_event_unit(seed, tick, legacy_r++, BUBBLES_SHARED_DECISION_CLASS, BUBBLES_SHARED_LANE_VALUE);
        CHECK(a_l == a_r, "M2.3 model: event A aligns");
        (void)legacy_event_unit(seed, tick, legacy_l++, BUBBLES_SHARED_DECISION_CLASS, BUBBLES_SHARED_LANE_VALUE);
        const float b_l = legacy_event_unit(seed, tick, legacy_l++, BUBBLES_SHARED_DECISION_CLASS, BUBBLES_SHARED_LANE_VALUE);
        const float b_r = legacy_event_unit(seed, tick, legacy_r++, BUBBLES_SHARED_DECISION_CLASS, BUBBLES_SHARED_LANE_VALUE);
        CHECK(b_l != b_r, "M2.3 per-channel top-level counter reproduces the real asymmetry failure");
    }

    begin_tick(&left, tick);
    begin_tick(&right, tick);

    const SharedSpawnId_t a_l = next_id(&left, BUBBLES_SPAWN_SOURCE_DENSITY);   // density #0
    const SharedSpawnId_t a_r = next_id(&right, BUBBLES_SPAWN_SOURCE_DENSITY);  // density #0
    CHECK(id_equal(a_l, a_r), "event A gets the same canonical identity on L/R");
    CHECK(SharedSpawnRandom(&left, 1.0f, a_l, BUBBLES_SHARED_DECISION_CLASS) ==
              SharedSpawnRandom(&right, 1.0f, a_r, BUBBLES_SHARED_DECISION_CLASS),
          "event A shares identity");

    // L-only EXTRA top-level spawn of a different source (STRUM, primary index).
    const SharedSpawnId_t x_l = next_id(&left, BUBBLES_SPAWN_SOURCE_STRUM);
    CHECK(x_l.event_index == 0u, "extra top-level spawn consumes its own source index");
    (void)SharedSpawnRandom(&left, 1.0f, x_l, BUBBLES_SHARED_DECISION_CLASS);

    const SharedSpawnId_t b_l = next_id(&left, BUBBLES_SPAWN_SOURCE_DENSITY);   // density #1
    const SharedSpawnId_t b_r = next_id(&right, BUBBLES_SPAWN_SOURCE_DENSITY);  // density #1
    CHECK(id_equal(b_l, b_r), "extra top-level spawn in L does not shift event B's identity");
    const float sb_l = SharedSpawnRandom(&left, 1.0f, b_l, BUBBLES_SHARED_DECISION_CLASS);
    const float sb_r = SharedSpawnRandom(&right, 1.0f, b_r, BUBBLES_SHARED_DECISION_CLASS);
    CHECK(sb_l == sb_r,
          "common event B keeps its shared identity after an extra top-level spawn in L");
    CHECK(sb_l != SharedSpawnRandom(&right, 1.0f, a_r, BUBBLES_SHARED_DECISION_CLASS),
          "B and A remain distinct logical events");
    return 0;
}

// --- 15. (B) Cross-source asymmetry ------------------------------------------
//
//   L: density A, strum extra, density B
//   R: density A, density B
// Density B must align despite the extra STRUM processed in L.
static int test_cross_source_asymmetry(void) {
    static int16_t delay_l[DELAY_SAMPLES];
    static int16_t delay_r[DELAY_SAMPLES];
    EngineConfig_t cfg;
    base_config(&cfg, 0xC205E0u);
    zero_delay(delay_l);
    zero_delay(delay_r);

    BubbleEngine_t left;
    BubbleEngine_t right;
    bubble_engine_init(&left, delay_l, &cfg);
    bubble_engine_init(&right, delay_r, &cfg);
    begin_tick(&left, 12);
    begin_tick(&right, 12);

    const SharedSpawnId_t a_l = next_id(&left, BUBBLES_SPAWN_SOURCE_DENSITY);
    const SharedSpawnId_t a_r = next_id(&right, BUBBLES_SPAWN_SOURCE_DENSITY);
    CHECK(id_equal(a_l, a_r), "density A aligned");

    // L processes an extra STRUM (real top-level source) before density B.
    const SharedSpawnId_t strum_l = next_id(&left, BUBBLES_SPAWN_SOURCE_STRUM);
    (void)SharedSpawnRandom(&left, 1.0f, strum_l, BUBBLES_SHARED_DECISION_CLASS);

    const SharedSpawnId_t b_l = next_id(&left, BUBBLES_SPAWN_SOURCE_DENSITY);
    const SharedSpawnId_t b_r = next_id(&right, BUBBLES_SPAWN_SOURCE_DENSITY);
    CHECK(id_equal(b_l, b_r), "density B aligned across L/R despite the extra STRUM");
    CHECK(id_equal(b_l, make_id(12, BUBBLES_SPAWN_SOURCE_DENSITY, 1u, 0u)),
          "density B kept index 1 in both channels");
    CHECK(SharedSpawnRandom(&left, 1.0f, b_l, BUBBLES_SHARED_DECISION_REGION_TIER) ==
              SharedSpawnRandom(&right, 1.0f, b_r, BUBBLES_SHARED_DECISION_REGION_TIER),
          "density B shared tier decision aligned");
    return 0;
}

// --- 16. (C) Burst asymmetry -------------------------------------------------
//
//   L: common event, extra SPRAY/SWARM-style burst, common event
//   R: common event, common event
// The later common event must align.
static int test_burst_asymmetry(void) {
    static int16_t delay_l[DELAY_SAMPLES];
    static int16_t delay_r[DELAY_SAMPLES];
    EngineConfig_t cfg;
    base_config(&cfg, 0xB0577u);
    zero_delay(delay_l);
    zero_delay(delay_r);

    BubbleEngine_t left;
    BubbleEngine_t right;
    bubble_engine_init(&left, delay_l, &cfg);
    bubble_engine_init(&right, delay_r, &cfg);
    begin_tick(&left, 3);
    begin_tick(&right, 3);

    // Common density event #0.
    const SharedSpawnId_t a_l = next_id(&left, BUBBLES_SPAWN_SOURCE_DENSITY);
    const SharedSpawnId_t a_r = next_id(&right, BUBBLES_SPAWN_SOURCE_DENSITY);
    CHECK(id_equal(a_l, a_r), "common event #0 aligned");

    // L-only extra immediate burst invocation (SPRAY/SWARM style) with children.
    const SharedSpawnId_t burst = next_id(&left, BUBBLES_SPAWN_SOURCE_BURST);
    for (uint32_t child = 0; child < 4u; child++) {
        SharedSpawnId_t id = burst;
        id.child_index = child;
        (void)SharedSpawnRandom(&left, 1.0f, id, BUBBLES_SHARED_DECISION_CLASS);
        (void)SharedSpawnRandom(&left, 1.0f, id, BUBBLES_SHARED_DECISION_OFFSET_BAND);
    }

    // Common density event #1 after the burst must align.
    const SharedSpawnId_t b_l = next_id(&left, BUBBLES_SPAWN_SOURCE_DENSITY);
    const SharedSpawnId_t b_r = next_id(&right, BUBBLES_SPAWN_SOURCE_DENSITY);
    CHECK(id_equal(b_l, b_r), "post-burst common density event aligned across L/R");
    CHECK(SharedSpawnRandom(&left, 1.0f, b_l, BUBBLES_SHARED_DECISION_CLASS) ==
              SharedSpawnRandom(&right, 1.0f, b_r, BUBBLES_SHARED_DECISION_CLASS),
          "post-burst common event shares its class decision");
    return 0;
}

// --- 17. (F) Long real-scheduler stress --------------------------------------
//
// Drive the real Scheduler_RunTick for thousands of ticks on two engines with the
// same seed/config and different channel decorrelation. Every tick both engines
// receive the same engine_state / target_density / spawn_accumulator / burst_mode;
// L additionally receives real top-level STRUM and immediate BURST extras. Using
// the trace hook we record every shared decision from the real paths and verify:
//   - DENSITY/RHYTHM canonical event indices stay identical across channels;
//   - every shared-branch value observed for a given canonical identity is equal.
#define STRESS_TRACE_CAP 400000
typedef struct {
    const BubbleEngine_t* channel;
    SharedSpawnId_t id;
    uint32_t kind;
    int shared;
    float value;
} StressTraceEvent;
static StressTraceEvent g_stress_trace[STRESS_TRACE_CAP];
static int g_stress_count;
static const BubbleEngine_t* g_stress_left;
static const BubbleEngine_t* g_stress_right;

static void stress_trace_cb(void* user, const SoundBubblesEngine_t* engine, SharedSpawnId_t id,
                            uint32_t kind, int shared_branch, float value) {
    (void)user;
    (void)engine;
    if (g_stress_count >= STRESS_TRACE_CAP) return;
    g_stress_trace[g_stress_count].channel = engine;
    g_stress_trace[g_stress_count].id = id;
    g_stress_trace[g_stress_count].kind = kind;
    g_stress_trace[g_stress_count].shared = shared_branch;
    g_stress_trace[g_stress_count].value = value;
    g_stress_count++;
}

static int stress_cmp(const void* pa, const void* pb) {
    const StressTraceEvent* a = (const StressTraceEvent*)pa;
    const StressTraceEvent* b = (const StressTraceEvent*)pb;
    if (a->id.tick != b->id.tick) return (a->id.tick < b->id.tick) ? -1 : 1;
    if (a->id.source != b->id.source) return (a->id.source < b->id.source) ? -1 : 1;
    if (a->id.event_index != b->id.event_index) return (a->id.event_index < b->id.event_index) ? -1 : 1;
    if (a->id.child_index != b->id.child_index) return (a->id.child_index < b->id.child_index) ? -1 : 1;
    if (a->kind != b->kind) return (a->kind < b->kind) ? -1 : 1;
    return 0;
}

static int test_long_real_scheduler_stress(void) {
    static int16_t delay_l[DELAY_SAMPLES];
    static int16_t delay_r[DELAY_SAMPLES];
    EngineConfig_t cfg;
    base_config(&cfg, 0x57E5Eu);
    cfg.tempo_sync_enabled = 0;
    cfg.droplet_enable = 0; // isolate identity from local droplet draws
    zero_delay(delay_l);
    zero_delay(delay_r);

    BubbleEngine_t left;
    BubbleEngine_t right;
    bubble_engine_init(&left, delay_l, &cfg);
    bubble_engine_init(&right, delay_r, &cfg);
    bubble_engine_set_channel_decorrelation(&left, 0u);
    bubble_engine_set_channel_decorrelation(&right, 0x55555555u);

    g_stress_left = &left;
    g_stress_right = &right;
    g_stress_count = 0;
    BubblesTest_SetSharedTrace(stress_trace_cb, NULL);

    const BubbleBurstMode_t modes[4] = {
        BUBBLE_BURST_MODE_SINGLE,
        BUBBLE_BURST_MODE_SPRAY,
        BUBBLE_BURST_MODE_SWARM,
        BUBBLE_BURST_MODE_REVERSE_SWELL,
    };

    uint32_t rng = 0xBEEF1234u;
    for (uint32_t tick = 1; tick <= 3000; tick++) {
        rng = rng * 1664525u + 1013904223u;

        const EngineState_t state = (EngineState_t)(rng % 5u);
        left.engine_state = state;
        right.engine_state = state;

        // Same density schedule on both channels (varies across ticks).
        const float density = 900.0f + (float)((rng >> 8) % 2100u);
        left.target_density = density;
        right.target_density = density;
        left.spawn_accumulator = 0.0f;
        right.spawn_accumulator = 0.0f;
        left.rhythm_step_accumulator = 1.0f;
        right.rhythm_step_accumulator = 1.0f;
        left.rhythm_step_index = (int32_t)tick;
        right.rhythm_step_index = (int32_t)tick;

        const BubbleBurstMode_t mode = modes[(rng >> 16) % 4u];
        left.config.burst_mode = mode;
        right.config.burst_mode = mode;
        left.config.burst_immediate_count = 1 + (int32_t)((rng >> 20) % 3u);
        right.config.burst_immediate_count = left.config.burst_immediate_count;

        // Alternate free-running density and tempo-synced rhythm scheduling on
        // both channels to exercise the rhythm source through the real paths.
        const int tempo_sync = ((tick % 11u) == 0u) ? 1 : 0;
        left.config.tempo_sync_enabled = tempo_sync;
        right.config.tempo_sync_enabled = tempo_sync;
        left.config.rhythm_pattern = 0x0001u | ((rng >> 4) & 0xFFFFu);
        right.config.rhythm_pattern = left.config.rhythm_pattern;

        begin_tick(&left, tick);
        begin_tick(&right, tick);

        // Symmetric STRUM schedule on both channels (exercises the strum path).
        if ((tick % 5u) == 0u) {
            left.strum_pending_count += 1;
            right.strum_pending_count += 1;
        }
        // L-only real top-level immediate burst: a different source, and the path
        // does not touch the per-tick density spawn budget, so the common DENSITY
        // schedule stays identical while the identity model is stressed.
        if ((tick % 7u) == 0u) {
            Scheduler_SpawnImmediateBurst(&left);
        }

        // Drop the voice footprint of the L-only extras so the common density
        // schedule has identical capacity on both channels (identity isolation is
        // what is under test here, not saturation).
        for (int v = 0; v < left.active_voice_limit; v++) {
            left.voices[v].state = VOICE_STATE_INACTIVE;
            right.voices[v].state = VOICE_STATE_INACTIVE;
        }

        Scheduler_RunTick(&left);
        Scheduler_RunTick(&right);
    }

    BubblesTest_SetSharedTrace(NULL, NULL);

    CHECK(g_stress_count > 1000, "stress produced a meaningful number of shared decisions");

    // Common DENSITY decisions must be produced in equal number by both channels
    // even though L processed extra STRUM/BURST top-level events (identity
    // isolation, not saturation). Compute this before compacting the trace.
    int density_l = 0;
    int density_r = 0;
    int rhythm_l = 0;
    int rhythm_r = 0;
    for (int i = 0; i < g_stress_count; i++) {
        if (g_stress_trace[i].id.source == (uint32_t)BUBBLES_SPAWN_SOURCE_DENSITY) {
            if (g_stress_trace[i].channel == g_stress_left) density_l++;
            else if (g_stress_trace[i].channel == g_stress_right) density_r++;
        } else if (g_stress_trace[i].id.source == (uint32_t)BUBBLES_SPAWN_SOURCE_RHYTHM) {
            if (g_stress_trace[i].channel == g_stress_left) rhythm_l++;
            else if (g_stress_trace[i].channel == g_stress_right) rhythm_r++;
        }
    }
    CHECK(density_l == density_r, "common DENSITY event counts match across channels");
    CHECK(rhythm_l == rhythm_r, "common RHYTHM event counts match across channels");
    CHECK(density_l > 100, "stress exercised the density path through the real scheduler");
    CHECK(rhythm_l > 10, "stress exercised the rhythm path through the real scheduler");

    // Every shared-branch value for a canonical identity must be identical across
    // channels (and across repeats within a channel). Sort by identity + kind and
    // scan adjacent groups, so the check stays O(n log n).
    int shared_n = 0;
    for (int i = 0; i < g_stress_count; i++) {
        if (g_stress_trace[i].shared) {
            g_stress_trace[shared_n++] = g_stress_trace[i];
        }
    }
    qsort(g_stress_trace, (size_t)shared_n, sizeof(g_stress_trace[0]), stress_cmp);
    int compared = 0;
    int conflicts = 0;
    for (int i = 1; i < shared_n; i++) {
        if (stress_cmp(&g_stress_trace[i - 1], &g_stress_trace[i]) == 0) {
            compared++;
            if (g_stress_trace[i - 1].value != g_stress_trace[i].value) {
                conflicts++;
            }
        }
    }
    CHECK(compared > 0, "stress observed repeated canonical identities to compare");
    CHECK(conflicts == 0, "same canonical identity always yields the same shared value");

    return 0;
}

int main(void) {
    if (test_shared_decisions_ignore_coherence_divergence() != 0) return 1;
    if (test_unequal_spawn_count_reconverges() != 0) return 1;
    if (test_long_stereo_divergence_keeps_event_identity() != 0) return 1;
    if (test_shared_event_decision_determinism() != 0) return 1;
    if (test_channel_local_decisions_stay_decorrelated() != 0) return 1;
    if (test_guard_clamp_relative_span_math() != 0) return 1;
    if (test_guard_clamp_keeps_read_out_of_guard_zone() != 0) return 1;
    if (test_engine_guard_with_microdetune() != 0) return 1;
    if (test_smart_start_cannot_invalidate_guard() != 0) return 1;
    if (test_fixed_seed_spawn_sequence_is_deterministic() != 0) return 1;
    if (test_same_tick_asymmetry_does_not_shift_common_spawns() != 0) return 1;
    if (test_pending_spawn_queue_preserves_identity() != 0) return 1;
    if (test_droplet_identity_is_parent_derived_and_collision_free() != 0) return 1;
    if (test_long_asymmetric_multispawn_stress() != 0) return 1;
    if (test_real_same_tick_top_level_asymmetry() != 0) return 1;
    if (test_cross_source_asymmetry() != 0) return 1;
    if (test_burst_asymmetry() != 0) return 1;
    if (test_long_real_scheduler_stress() != 0) return 1;
    return 0;
}
