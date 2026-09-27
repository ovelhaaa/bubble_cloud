// M2.1 stereo-coherence lockstep and microdetune guard regression harness.
//
// This is a white-box harness: it includes the DSP implementation so it can
// drive the static spawn/guard helpers directly, then links the engine and macro
// layers. It covers two M2 logic fixes without changing any public API:
//
//   1. The shared coherence RNG must stay in lockstep even when the L/R engines
//      temporarily resolve to different phrase states (and therefore different
//      `coherence` values). A pre-fix build desynchronises because
//      `SpawnRandomFloat01` consumed a variable number of shared draws.
//   2. The fixed per-grain microdetune must be folded into the playback rate
//      *before* the guard-band clamp, and the forward guard must be based on the
//      real relative grain travel (rate - 1), so a few cents around rate 1.0 do
//      not shove the read into a distant region or let a grain run into the
//      write head.

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

// --- 1. Shared stream lockstep across temporary coherence divergence ---

static int test_shared_stream_lockstep_under_coherence_divergence(void) {
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

    CHECK(left.coherence_rng_state == right.coherence_rng_state,
          "engines start with an identical shared stream");
    CHECK(left.rng_state != right.rng_state, "per-channel streams start decorrelated");

    // L enters an attack (coherence 0.80), R stays in the sustain/decay region
    // (coherence 0.22 / 0.20) for a while. The shared stream must not drift.
    left.engine_state = ENGINE_STATE_TRANSIENT_BURST;
    right.engine_state = ENGINE_STATE_SUSTAIN_BODY;
    const float coherence_left = ResolveSpawnCoherence(&left);
    const float coherence_right = ResolveSpawnCoherence(&right);
    CHECK(fabsf(coherence_left - coherence_right) > 0.1f,
          "test setup actually diverges the two coherence values");

    for (int i = 0; i < 256; i++) {
        (void)SpawnRandomFloat01(&left, coherence_left);
        (void)SpawnRandomFloat01(&right, coherence_right);
    }
    CHECK(left.coherence_rng_state == right.coherence_rng_state,
          "shared stream stays in lockstep while coherence differs");
    CHECK(left.rng_state != right.rng_state,
          "per-channel streams stay decorrelated during divergence");

    // Both engines return to the same phrase state. Shared decisions must line up
    // again exactly (coherence 1.0 forces the shared branch so the comparison is
    // not masked by the intentionally channel-local fallback draws).
    left.engine_state = ENGINE_STATE_SUSTAIN_BODY;
    right.engine_state = ENGINE_STATE_SUSTAIN_BODY;
    int shared_matches = 0;
    for (int i = 0; i < 256; i++) {
        const float a = SpawnRandomFloat01(&left, 1.0f);
        const float b = SpawnRandomFloat01(&right, 1.0f);
        if (a == b) shared_matches++;
    }
    CHECK(shared_matches == 256, "shared decisions reproduce identically after coherence reconverges");
    CHECK(left.coherence_rng_state == right.coherence_rng_state,
          "shared stream lockstep holds after reconvergence");
    return 0;
}

// --- 2. Spawn decisions keep the shared stream in lockstep while states differ ---

static int test_spawn_decision_lockstep_under_state_divergence(void) {
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

    // Let each phrase state pick its own class (attack favours micro, sustain
    // favours body/short) so the test also exercises the class-dependent region
    // path. The shared stream must still advance identically on both channels.
    left.engine_state = ENGINE_STATE_ATTACK_ONGOING;
    right.engine_state = ENGINE_STATE_SUSTAIN_BODY;
    int class_diverged = 0;
    for (int i = 0; i < 256; i++) {
        const BubbleClass_t class_left = Scheduler_SelectClassForState(&left);
        const BubbleClass_t class_right = Scheduler_SelectClassForState(&right);
        if (class_left != class_right) class_diverged++;
        Voice_SpawnInit(&left, 0, class_left, 0);
        Voice_SpawnInit(&right, 0, class_right, 0);
    }
    CHECK(class_diverged > 0, "test actually exercised divergent bubble classes");
    CHECK(left.coherence_rng_state == right.coherence_rng_state,
          "spawn-time shared stream stays in lockstep across state/class divergence");
    CHECK(left.rng_state != right.rng_state,
          "spawn-time per-channel streams stay decorrelated");

    // States reconverge: the shared component of every spawn decision must be
    // bit-identical again, i.e. both channels draw the very same shared values
    // even though their channel-local streams stay decorrelated.
    left.engine_state = ENGINE_STATE_SUSTAIN_BODY;
    right.engine_state = ENGINE_STATE_SUSTAIN_BODY;
    int shared_value_matches = 0;
    for (int i = 0; i < 256; i++) {
        const float a = RandomCoherenceFloat01(&left);
        const float b = RandomCoherenceFloat01(&right);
        if (a == b) shared_value_matches++;
    }
    CHECK(shared_value_matches == 256, "shared values reproduce identically after states reconverge");
    CHECK(left.coherence_rng_state == right.coherence_rng_state,
          "shared stream lockstep holds after states reconverge");
    CHECK(left.rng_state != right.rng_state,
          "per-channel streams remain decorrelated after reconvergence");
    return 0;
}

// --- 3. Guard clamp is based on the real relative grain travel ---

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

// --- 4. Engine-level guard with real microdetuned grains ---

typedef struct {
    int pitch_mode;
    int force_reverse;
    int freeze;
    BubbleClass_t bubble_class;
    float body_duration_ms; // 0 = leave defaults
} GuardScenario;

static int run_guard_scenario(const GuardScenario* scenario, float sample_rate) {
    static int16_t delay[DELAY_SAMPLES];
    zero_delay(delay);

    EngineConfig_t cfg;
    base_config(&cfg, 0x6A11Du ^ (uint32_t)(sample_rate * 0.5f));
    cfg.sample_rate = sample_rate;
    cfg.pitch_mode = scenario->pitch_mode;
    cfg.shimmer_amount = (scenario->pitch_mode == BUBBLE_PITCH_MODE_SHIMMER) ? 1.0f : 0.0f;
    cfg.freeze_enabled = scenario->freeze;
    cfg.freeze_amount = scenario->freeze ? 1.0f : 0.0f;
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
                    "FAIL %s:%d: premature guard at sr=%.0f rate=%.5f dur=%d offset=%d cents=%.4f class=%d rev=%d\n",
                    __FILE__, __LINE__, (double)sample_rate, (double)voice->rate, duration_samples,
                    offset, (double)voice->microdetune_cents, (int)scenario->bubble_class,
                    (int)voice->read_direction);
            return 1;
        }
    }

    // Every scenario must actually exercise microdetune in both directions; this
    // also proves the detune is constant per grain and applied to the runtime rate.
    CHECK(positive_detune > 0, "scenario exercised positive microdetune grains");
    CHECK(negative_detune > 0, "scenario exercised negative microdetune grains");
    return 0;
}

static int test_engine_guard_with_microdetune(void) {
    const float sample_rates[4] = {44100.0f, 48000.0f, 88200.0f, 96000.0f};

    const GuardScenario scenarios[] = {
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
        // long reverse grain at 96 kHz, clamping active: catches the pre-fix
        // order where the guard used the rate before microdetune.
        { BUBBLE_PITCH_MODE_UNISON, 1, 0, BUBBLE_CLASS_SUSTAIN_BODY, 900.0f },
    };

    for (unsigned s = 0; s < sizeof(scenarios) / sizeof(scenarios[0]); s++) {
        for (int r = 0; r < 4; r++) {
            if (run_guard_scenario(&scenarios[s], sample_rates[r]) != 0) {
                return 1;
            }
        }
    }
    return 0;
}

// --- 5. Determinism of the new shared-draw budget with a fixed seed ---

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
        const float coh = ResolveSpawnCoherence(&first);
        Voice_SpawnInit(&first, 0, BUBBLE_CLASS_SHORT_INTERMEDIATE, 0);
        Voice_SpawnInit(&second, 0, BUBBLE_CLASS_SHORT_INTERMEDIATE, 0);
        (void)coh;
        CHECK(first.coherence_rng_state == second.coherence_rng_state,
              "identical seeds consume the shared stream identically");
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
    if (test_shared_stream_lockstep_under_coherence_divergence() != 0) return 1;
    if (test_spawn_decision_lockstep_under_state_divergence() != 0) return 1;
    if (test_guard_clamp_relative_span_math() != 0) return 1;
    if (test_guard_clamp_keeps_read_out_of_guard_zone() != 0) return 1;
    if (test_engine_guard_with_microdetune() != 0) return 1;
    if (test_fixed_seed_spawn_sequence_is_deterministic() != 0) return 1;
    return 0;
}
