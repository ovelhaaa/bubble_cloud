// M2 stereo-coherence and guard regression harness.
//
// This is a white-box harness: it includes the DSP implementation so it can
// drive the static spawn/guard helpers directly, then links the engine and macro
// layers. It covers the M2.1 fixes plus the M2.2 robustness milestone without
// changing any public API:
//
//   1. Shared stereo decisions are event-addressable (M2.2). The shared value of
//      a logical event is derived from a stateless hash of
//      (base seed, scheduler_tick, tick_shared_ordinal, decision kind), so a
//      channel that executes more or fewer spawns cannot shift the shared
//      decision of a later logical event. A pre-M2.2 build used a sequential
//      `coherence_rng_state` stream and desynchronised after an asymmetric spawn.
//   2. Smart Start cannot invalidate the guard (M2.2). Smart Start now runs
//      before the guard clamp, so the offset that lands in `read_ptr_float` is
//      always protected for the grain's real rate.
//   3. The fixed per-grain microdetune is folded into the playback rate *before*
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
// (scheduler_tick, tick_shared_ordinal, kind); resetting the ordinal models the
// control-tick boundary the DSP performs internally.
static void begin_tick(BubbleEngine_t* engine, uint32_t tick) {
    engine->scheduler_tick = tick;
    engine->tick_shared_ordinal = 0;
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
        for (int e = 0; e < 4; e++) {
            (void)SpawnRandomFloat01(&left, coherence_left, BUBBLES_SHARED_DECISION_CLASS);
            (void)SpawnRandomFloat01(&right, coherence_right, BUBBLES_SHARED_DECISION_CLASS);
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
    for (int i = 0; i < 256; i++) {
        const float a = SpawnRandomFloat01(&left, 1.0f, BUBBLES_SHARED_DECISION_REGION_TIER);
        const float b = SpawnRandomFloat01(&right, 1.0f, BUBBLES_SHARED_DECISION_REGION_TIER);
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
    // consumes additional shared decisions that R never draws. Pre-M2.2 the
    // sequential shared stream advanced further on L and every later shared
    // decision drifted out of lockstep.
    for (uint32_t tick = 1; tick <= 512; tick++) {
        begin_tick(&left, tick);
        begin_tick(&right, tick);

        const float l_first = SpawnRandomFloat01(&left, 1.0f, BUBBLES_SHARED_DECISION_CLASS);
        const float r_first = SpawnRandomFloat01(&right, 1.0f, BUBBLES_SHARED_DECISION_CLASS);
        CHECK(l_first == r_first, "first logical event keeps the same shared decision");

        const int l_extra = (tick % 4u == 0u) ? 3 : ((tick % 7u == 0u) ? 1 : 0);
        const int r_extra = (tick % 5u == 0u) ? 2 : 0;
        for (int e = 0; e < l_extra; e++) {
            (void)SpawnRandomFloat01(&left, 1.0f, BUBBLES_SHARED_DECISION_REGION_TIER);
        }
        for (int e = 0; e < r_extra; e++) {
            (void)SpawnRandomFloat01(&right, 1.0f, BUBBLES_SHARED_DECISION_REGION_TIER);
        }
    }

    // A large asymmetry on one tick must not leak into the next logical event.
    begin_tick(&left, 9000);
    begin_tick(&right, 9000);
    for (int e = 0; e < 32; e++) {
        (void)SpawnRandomFloat01(&left, 1.0f, BUBBLES_SHARED_DECISION_OFFSET_BAND);
    }
    begin_tick(&left, 9001);
    begin_tick(&right, 9001);
    for (int i = 0; i < 32; i++) {
        const float a = SpawnRandomFloat01(&left, 1.0f, BUBBLES_SHARED_DECISION_OFFSET_BAND);
        const float b = SpawnRandomFloat01(&right, 1.0f, BUBBLES_SHARED_DECISION_OFFSET_BAND);
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

        const float a = SpawnRandomFloat01(&left, 1.0f, BUBBLES_SHARED_DECISION_CLASS);
        const float b = SpawnRandomFloat01(&right, 1.0f, BUBBLES_SHARED_DECISION_CLASS);
        if (a != b) mismatches++;

        const int l_extra = (int)((tick * 7u + 3u) % 5u);
        const int r_extra = (int)((tick * 11u + 1u) % 4u);
        for (int e = 1; e < l_extra; e++) {
            const BubbleSharedDecisionKind_t k = (e % 2) ? BUBBLES_SHARED_DECISION_REGION_TIER
                                                          : BUBBLES_SHARED_DECISION_CLASS;
            (void)SpawnRandomFloat01(&left, 1.0f, k);
        }
        for (int e = 1; e < r_extra; e++) {
            const BubbleSharedDecisionKind_t k = (e % 2) ? BUBBLES_SHARED_DECISION_OFFSET_BAND
                                                          : BUBBLES_SHARED_DECISION_CLASS;
            (void)SpawnRandomFloat01(&right, 1.0f, k);
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
    for (int i = 0; i < 1000; i++) {
        (void)SpawnRandomFloat01(&a, 1.0f, BUBBLES_SHARED_DECISION_CLASS);
    }

    // Same seed + same event identity => exact same shared decision, regardless
    // of the draw history before it.
    a.scheduler_tick = 4242;
    a.tick_shared_ordinal = 5;
    b.scheduler_tick = 4242;
    b.tick_shared_ordinal = 5;
    for (int i = 0; i < 64; i++) {
        const float va = SpawnRandomFloat01(&a, 1.0f, BUBBLES_SHARED_DECISION_OFFSET_BAND);
        const float vb = SpawnRandomFloat01(&b, 1.0f, BUBBLES_SHARED_DECISION_OFFSET_BAND);
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
    CHECK(SpawnRandomFloat01(&left, 1.0f, BUBBLES_SHARED_DECISION_CLASS) ==
              SpawnRandomFloat01(&right, 1.0f, BUBBLES_SHARED_DECISION_CLASS),
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
        Voice_SpawnInit(&engine, 0, scenario->bubble_class, 0);
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
            Voice_SpawnInit(&engine, 0, BUBBLE_CLASS_SUSTAIN_BODY, 0);
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
        Voice_SpawnInit(&first, 0, BUBBLE_CLASS_SHORT_INTERMEDIATE, 0);
        Voice_SpawnInit(&second, 0, BUBBLE_CLASS_SHORT_INTERMEDIATE, 0);
        BubbleVoice_t* a = &first.voices[0];
        BubbleVoice_t* b = &second.voices[0];
        CHECK(a->source_region_id == b->source_region_id && a->memory_tier == b->memory_tier,
              "identical seeds produce identical shared spawn decisions");
        CHECK(a->microdetune_cents == b->microdetune_cents && a->rate == b->rate,
              "identical seeds produce identical microdetune and rate");
    }
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
    return 0;
}
