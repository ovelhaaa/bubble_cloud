// M2 stereo-coherence and guard regression harness.
//
// This is a white-box harness: it includes the DSP implementation so it can
// drive the static spawn/guard helpers directly, then links the engine and macro
// layers. It covers the M2.1/M2.2 fixes plus the M2.3 explicit-identity milestone
// without changing any public API:
//
//   1. Shared stereo decisions are keyed by an *explicit logical spawn identity*
//      (M2.3). The shared value of a logical spawn is a stateless hash of
//      (base seed, scheduler_tick, spawn_ordinal, decision kind). The scheduler
//      owns the ordinal: one per top-level spawn, while second-generation
//      (droplet) spawns derive a stable child ordinal from their parent. A
//      channel that executes more or fewer spawns — even inside the same tick —
//      therefore cannot shift the shared decision of a later common spawn. The
//      earlier implicit `tick_shared_ordinal++` counter is reproduced in test 10
//      to prove the exact same-tick asymmetry failure it caused.
//   2. Saturated spawns preserve their ordinal in the pending queue and reuse it
//      verbatim when they finally become a voice.
//   3. Smart Start cannot invalidate the guard (M2.2). Smart Start now runs
//      before the guard clamp, so the offset that lands in `read_ptr_float` is
//      always protected for the grain's real rate.
//   4. The fixed per-grain microdetune is folded into the playback rate *before*
//      the guard-band clamp, and the forward guard is based on the real relative
//      grain travel (rate - 1), so a few cents around rate 1.0 do not shove the
//      read into a distant region or let a grain run into the write head.

#define SOUND_BUBBLES_DSP_INTERNAL 1
#include "dsp/sound_bubbles_dsp.c"
#include "engine/bubble_engine.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
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

// --- Shared-event helpers -------------------------------------------------

// Begin a new logical control tick. The identity of a shared decision is
// (scheduler_tick, spawn_ordinal, kind); resetting the spawn ordinal models the
// control-tick boundary the DSP performs internally.
static void begin_tick(BubbleEngine_t* engine, uint32_t tick) {
    engine->scheduler_tick = tick;
    engine->tick_spawn_ordinal = 0;
}

// Reserve the next explicit logical-spawn ordinal, exactly as the scheduler does
// for each top-level spawn.
static uint32_t next_ordinal(BubbleEngine_t* engine) {
    return Scheduler_NextSpawnOrdinal(engine);
}

// Legacy M2.2 model: a single implicit ordinal per tick, advanced by *every*
// shared draw. This is precisely the `tick_shared_ordinal++` behavior that the
// M2.3 explicit spawn identity removes. It exists only so the harness can prove
// the same-tick asymmetry failure without checking out the old tree.
static float legacy_shared_draw(uint32_t seed, uint32_t tick, uint32_t* running_ordinal,
                                BubbleSharedDecisionKind_t kind) {
    const uint32_t ordinal = (*running_ordinal)++;
    return SharedEventUnit(seed, tick, ordinal, (uint32_t)kind, BUBBLES_SHARED_LANE_VALUE);
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
            (void)SharedSpawnRandom(&left, coherence_left, e, BUBBLES_SHARED_DECISION_CLASS);
            (void)SharedSpawnRandom(&right, coherence_right, e, BUBBLES_SHARED_DECISION_CLASS);
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
        const float a = SharedSpawnRandom(&left, 1.0f, i, BUBBLES_SHARED_DECISION_REGION_TIER);
        const float b = SharedSpawnRandom(&right, 1.0f, i, BUBBLES_SHARED_DECISION_REGION_TIER);
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
    // explicit identity model those extras live in the derived namespace and never
    // shift the shared decision of a later primary ordinal.
    for (uint32_t tick = 1; tick <= 512; tick++) {
        begin_tick(&left, tick);
        begin_tick(&right, tick);

        const uint32_t ord = next_ordinal(&left);
        const uint32_t r_ord = next_ordinal(&right);
        const float l_first = SharedSpawnRandom(&left, 1.0f, ord, BUBBLES_SHARED_DECISION_CLASS);
        const float r_first = SharedSpawnRandom(&right, 1.0f, r_ord, BUBBLES_SHARED_DECISION_CLASS);
        CHECK(l_first == r_first, "first logical event keeps the same shared decision");

        const int l_extra = (tick % 4u == 0u) ? 3 : ((tick % 7u == 0u) ? 1 : 0);
        const int r_extra = (tick % 5u == 0u) ? 2 : 0;
        for (int e = 0; e < l_extra; e++) {
            (void)SharedSpawnRandom(&left, 1.0f, SpawnDerivedOrdinal(ord, (uint32_t)(1 + e)), BUBBLES_SHARED_DECISION_REGION_TIER);
        }
        for (int e = 0; e < r_extra; e++) {
            (void)SharedSpawnRandom(&right, 1.0f, SpawnDerivedOrdinal(r_ord, (uint32_t)(1 + e)), BUBBLES_SHARED_DECISION_REGION_TIER);
        }
    }

    // A large asymmetry on one tick must not leak into the next logical event.
    begin_tick(&left, 9000);
    begin_tick(&right, 9000);
    const uint32_t big_ord = next_ordinal(&left);
    (void)next_ordinal(&right);
    for (int e = 0; e < 32; e++) {
        (void)SharedSpawnRandom(&left, 1.0f, SpawnDerivedOrdinal(big_ord, (uint32_t)e), BUBBLES_SHARED_DECISION_OFFSET_BAND);
    }
    begin_tick(&left, 9001);
    begin_tick(&right, 9001);
    for (uint32_t i = 0; i < 32u; i++) {
        const float a = SharedSpawnRandom(&left, 1.0f, i, BUBBLES_SHARED_DECISION_OFFSET_BAND);
        const float b = SharedSpawnRandom(&right, 1.0f, i, BUBBLES_SHARED_DECISION_OFFSET_BAND);
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
    // classes and asymmetric spawn counts per tick. The shared decision of the
    // first logical event each tick must never depend on the other channel's
    // draw history.
    int mismatches = 0;
    for (uint32_t tick = 1; tick <= 4000; tick++) {
        const EngineState_t state =
            (tick % 3u == 0u) ? ENGINE_STATE_TRANSIENT_BURST :
            ((tick % 3u == 1u) ? ENGINE_STATE_SUSTAIN_BODY : ENGINE_STATE_SPARSE_DECAY);
        left.engine_state = state;
        right.engine_state = state;
        begin_tick(&left, tick);
        begin_tick(&right, tick);

        const uint32_t a_ord = next_ordinal(&left);
        const uint32_t r_a_ord = next_ordinal(&right);
        const float a = SharedSpawnRandom(&left, 1.0f, a_ord, BUBBLES_SHARED_DECISION_CLASS);
        const float b = SharedSpawnRandom(&right, 1.0f, r_a_ord, BUBBLES_SHARED_DECISION_CLASS);
        if (a != b) mismatches++;

        const int l_extra = (int)((tick * 7u + 3u) % 5u);
        const int r_extra = (int)((tick * 11u + 1u) % 4u);
        for (int e = 1; e < l_extra; e++) {
            const BubbleSharedDecisionKind_t k = (e % 2) ? BUBBLES_SHARED_DECISION_REGION_TIER
                                                          : BUBBLES_SHARED_DECISION_CLASS;
            (void)SharedSpawnRandom(&left, 1.0f, SpawnDerivedOrdinal(a_ord, (uint32_t)e), k);
        }
        for (int e = 1; e < r_extra; e++) {
            const BubbleSharedDecisionKind_t k = (e % 2) ? BUBBLES_SHARED_DECISION_OFFSET_BAND
                                                          : BUBBLES_SHARED_DECISION_CLASS;
            (void)SharedSpawnRandom(&right, 1.0f, SpawnDerivedOrdinal(r_a_ord, (uint32_t)e), k);
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
        (void)SharedSpawnRandom(&a, 1.0f, i, BUBBLES_SHARED_DECISION_CLASS);
    }

    // Same seed + same event identity => exact same shared decision, regardless
    // of the draw history before it.
    a.scheduler_tick = 4242;
    b.scheduler_tick = 4242;
    for (int i = 0; i < 64; i++) {
        const uint32_t identity = 5u;
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
    CHECK(SharedSpawnRandom(&left, 1.0f, 0u, BUBBLES_SHARED_DECISION_CLASS) ==
              SharedSpawnRandom(&right, 1.0f, 0u, BUBBLES_SHARED_DECISION_CLASS),
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
        engine.tick_spawn_ordinal = (uint32_t)i;
        Voice_SpawnInit(&engine, 0, scenario->bubble_class, 0, (uint32_t)i);
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
            engine.tick_spawn_ordinal = (uint32_t)i;
            Voice_SpawnInit(&engine, 0, BUBBLE_CLASS_SUSTAIN_BODY, 0, (uint32_t)i);
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
        first.tick_spawn_ordinal = (uint32_t)i;
        second.tick_spawn_ordinal = (uint32_t)i;
        Voice_SpawnInit(&first, 0, BUBBLE_CLASS_SHORT_INTERMEDIATE, 0, (uint32_t)i);
        Voice_SpawnInit(&second, 0, BUBBLE_CLASS_SHORT_INTERMEDIATE, 0, (uint32_t)i);
        BubbleVoice_t* a = &first.voices[0];
        BubbleVoice_t* b = &second.voices[0];
        CHECK(a->source_region_id == b->source_region_id && a->memory_tier == b->memory_tier,
              "identical seeds produce identical shared spawn decisions");
        CHECK(a->microdetune_cents == b->microdetune_cents && a->rate == b->rate,
              "identical seeds produce identical microdetune and rate");
    }
    return 0;
}

// --- 10. Same-tick asymmetry: an extra spawn must not shift a common spawn --

// Reproduces the exact failure mode the explicit identity fixes:
//
//     begin tick
//     L/R process event A
//     L processes an extra spawn
//     L/R process event B in the same tick
//
// Under the M2.2 implicit `tick_shared_ordinal++` counter the extra draw advanced
// L's ordinal, so B received ordinal 2 on L and ordinal 1 on R. The explicit
// identity gives the extra a derived child ordinal and keeps B at ordinal 1 on
// both channels.
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

    // -- M2.3 explicit identity: the extra is a derived child, so B keeps its
    //    primary ordinal on both channels.
    begin_tick(&left, tick);
    begin_tick(&right, tick);
    const uint32_t a_ord = next_ordinal(&left);       // 0
    const uint32_t r_a_ord = next_ordinal(&right);    // 0
    CHECK(a_ord == 0u && r_a_ord == 0u, "event A has the same explicit ordinal on L/R");
    const float sa_l = SharedSpawnRandom(&left, 1.0f, a_ord, BUBBLES_SHARED_DECISION_CLASS);
    const float sa_r = SharedSpawnRandom(&right, 1.0f, r_a_ord, BUBBLES_SHARED_DECISION_CLASS);
    CHECK(sa_l == sa_r, "event A produces the same shared decision on L/R");

    const uint32_t extra_l = SpawnDerivedOrdinal(a_ord, 1u);
    (void)SharedSpawnRandom(&left, 1.0f, extra_l, BUBBLES_SHARED_DECISION_CLASS);

    const uint32_t b_ord = next_ordinal(&left);        // 1
    const uint32_t r_b_ord = next_ordinal(&right);     // 1
    CHECK(b_ord == 1u && r_b_ord == 1u, "extra spawn does not shift event B's explicit ordinal");
    CHECK(extra_l != b_ord, "derived child identity cannot collide with a primary ordinal");
    const float sb_l = SharedSpawnRandom(&left, 1.0f, b_ord, BUBBLES_SHARED_DECISION_CLASS);
    const float sb_r = SharedSpawnRandom(&right, 1.0f, r_b_ord, BUBBLES_SHARED_DECISION_CLASS);
    CHECK(sb_l == sb_r, "event B produces the same shared decision on L/R after the extra spawn");
    CHECK(sb_l != sa_l, "distinct logical spawns keep distinct shared identities");
    return 0;
}

// --- 11. Saturated spawns preserve their explicit identity in the queue ------

static int test_pending_spawn_queue_preserves_identity(void) {
    static int16_t delay[DELAY_SAMPLES];
    EngineConfig_t cfg;
    base_config(&cfg, 0xB0BAC1u);
    cfg.active_voice_limit = 4;
    cfg.droplet_enable = 0; // keep the queue deterministic for this test
    zero_delay(delay);

    BubbleEngine_t engine;
    bubble_engine_init(&engine, delay, &cfg);
    begin_tick(&engine, 1);

    // Saturate the pool with playing voices.
    for (int i = 0; i < engine.active_voice_limit; i++) {
        Voice_SpawnInit(&engine, i, BUBBLE_CLASS_SUSTAIN_BODY, 0, next_ordinal(&engine));
    }
    CHECK(engine.pending_spawn_count == 0, "pool saturated, nothing queued yet");

    const uint32_t ord = next_ordinal(&engine);
    const bool immediate = Voice_RequestSpawn(&engine, BUBBLE_CLASS_SHORT_INTERMEDIATE, 0, ord);
    CHECK(!immediate, "saturated request is queued rather than spawned immediately");
    CHECK(engine.pending_spawn_count == 1, "saturated request entered the pending queue");
    const uint32_t stored = engine.pending_spawns[engine.pending_spawn_head].spawn_ordinal;
    CHECK(stored == ord, "pending request preserved its explicit spawn ordinal");

    // Free a slot and flush; the voice must reuse the queued identity verbatim.
    engine.voices[0].state = VOICE_STATE_INACTIVE;
    const int flushed = Voice_FlushPendingSpawns(&engine, 1);
    CHECK(flushed == 1, "pending spawn flushed into the freed slot");
    CHECK(engine.voices[0].spawn_ordinal == ord,
          "flushed voice kept the queued identity instead of recomputing it");
    return 0;
}

// --- 12. Droplets have a deterministic, collision-free derived identity -------

static int test_droplet_identity_is_parent_derived_and_collision_free(void) {
    const uint32_t p0 = 0u, p1 = 1u, p2 = 2u;
    const uint32_t c0 = SpawnDerivedOrdinal(p0, 1u);
    const uint32_t c1 = SpawnDerivedOrdinal(p1, 1u);
    const uint32_t c2 = SpawnDerivedOrdinal(p2, 1u);

    CHECK((c0 & BUBBLES_SPAWN_DERIVED_FLAG) != 0u, "child ordinal lives in the derived namespace");
    CHECK(c0 != c1 && c1 != c2 && c0 != c2, "children of different parents never collide");
    CHECK(c0 != p0 && c1 != p1 && c2 != p2, "child ordinal never equals a primary ordinal");
    CHECK((c2 & ~BUBBLES_SPAWN_DERIVED_FLAG) ==
              ((p2 & BUBBLES_SPAWN_PARENT_MASK) << BUBBLES_SPAWN_PARENT_SHIFT) + 1u,
          "child ordinal encodes parent and generation");
    CHECK(SpawnDerivedOrdinal(p2, 1u) == c2, "child identity is stable across calls");

    // Same parent + generation on both channels => same shared droplet decision.
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
    const float l = SharedSpawnRandom(&left, 1.0f, c2, BUBBLES_SHARED_DECISION_REGION_TIER);
    const float r = SharedSpawnRandom(&right, 1.0f, c2, BUBBLES_SHARED_DECISION_REGION_TIER);
    CHECK(l == r, "droplet shared decision is identical across channels");
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
            const uint32_t ol = next_ordinal(&left);
            const uint32_t orr = next_ordinal(&right);
            if (ol != orr) mismatches++;

            for (int k = 0; k < 3; k++) {
                const float vl = SharedSpawnRandom(&left, 1.0f, ol, kinds[k]);
                const float vr = SharedSpawnRandom(&right, 1.0f, orr, kinds[k]);
                if (vl != vr) mismatches++;
            }

            // Asymmetric derived/second-generation spawns must not consume a primary
            // ordinal on either channel.
            const uint32_t child = SpawnDerivedOrdinal(ol, 1u);
            if (((rng >> (s + 1)) & 1u) == 0u) {
                (void)SharedSpawnRandom(&left, 1.0f, child, BUBBLES_SHARED_DECISION_REGION_TIER);
            }
            if (((rng >> (s + 8)) & 1u) == 0u) {
                (void)SharedSpawnRandom(&right, 1.0f, child, BUBBLES_SHARED_DECISION_REGION_TIER);
            }
        }

        if (left.tick_spawn_ordinal != right.tick_spawn_ordinal) mismatches++;
    }
    CHECK(mismatches == 0, "8000 asymmetric multi-spawn ticks keep shared identities aligned");
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
    return 0;
}
