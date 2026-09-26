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

typedef struct {
    int calls;
    SoundBubblesBlockMetrics_t last;
} MetricsCapture;

static void capture_metrics(const SoundBubblesBlockMetrics_t* metrics, void* user_data) {
    MetricsCapture* capture = (MetricsCapture*)user_data;
    capture->calls++;
    capture->last = *metrics;
}

static void init_engine(BubbleEngine_t* engine, int16_t* delay, BubbleEngineConfig_t* config) {
    bubble_engine_default_config(config);
    config->rng_seed = 0x12345678u;
    config->smart_start_enable = 0;
    config->burst_duration_ticks = 4;
    config->burst_immediate_count = 1;
    config->density_burst = 0.0f;
    config->density_sustain = 0.0f;
    config->density_decay = 0.0f;
    memset(delay, 0, (size_t)88200 * sizeof(delay[0]));
    bubble_engine_init(engine, delay, config);
}

static void process_constant(BubbleEngine_t* engine, float sample, int frames) {
    float in[BUBBLES_BLOCK_SIZE];
    float left[BUBBLES_BLOCK_SIZE];
    float right[BUBBLES_BLOCK_SIZE];
    for (int offset = 0; offset < frames; offset += BUBBLES_BLOCK_SIZE) {
        int chunk = BUBBLES_BLOCK_SIZE;
        if (offset + chunk > frames) chunk = frames - offset;
        for (int i = 0; i < chunk; i++) in[i] = sample;
        bubble_engine_process(engine, in, left, right, chunk);
    }
}

static int active_voice_count(const BubbleEngine_t* engine) {
    int count = 0;
    for (int i = 0; i < BUBBLES_MAX_VOICES; i++) {
        if (engine->voices[i].state != VOICE_STATE_INACTIVE) count++;
    }
    return count;
}

static int test_developer_parameter_gate_and_clamping(void) {
    static int16_t delay[88200];
    BubbleEngineConfig_t config;
    BubbleEngine_t engine;
    float value = -1.0f;
    init_engine(&engine, delay, &config);

    CHECK(!bubble_engine_set_parameter(&engine, BUBBLE_ENGINE_PARAM_REVERSE_PROBABILITY, 2.0f),
          "raw DSP parameters must be rejected while developer mode is disabled");
    CHECK(!bubble_engine_get_parameter(&engine, BUBBLE_ENGINE_PARAM_REVERSE_PROBABILITY, &value),
          "raw DSP parameters must not be readable while developer mode is disabled");
    CHECK(bubble_engine_set_parameter(&engine, BUBBLE_PARAM_DEVELOPER_MODE, 1.0f), "enable developer mode");
    CHECK(bubble_engine_set_parameter(&engine, BUBBLE_ENGINE_PARAM_REVERSE_PROBABILITY, 2.0f), "set reverse probability");
    CHECK(bubble_engine_get_parameter(&engine, BUBBLE_ENGINE_PARAM_REVERSE_PROBABILITY, &value), "read reverse probability");
    CHECK_CLOSE(value, 1.0f, 0.0001f, "reverse probability clamps to normalized range");
    CHECK(bubble_engine_set_parameter(&engine, BUBBLE_ENGINE_PARAM_MOTION_SHAPE, 99.0f), "set invalid motion shape");
    CHECK(bubble_engine_get_parameter(&engine, BUBBLE_ENGINE_PARAM_MOTION_SHAPE, &value), "read motion shape");
    CHECK_CLOSE(value, (float)BUBBLE_MOTION_SHAPE_TRIANGLE, 0.0001f, "invalid motion shape falls back to triangle");
    CHECK(bubble_engine_set_parameter(&engine, BUBBLE_ENGINE_PARAM_TEMPO_BPM, 500.0f), "set high tempo");
    CHECK(bubble_engine_get_parameter(&engine, BUBBLE_ENGINE_PARAM_TEMPO_BPM, &value), "read tempo");
    CHECK_CLOSE(value, 300.0f, 0.0001f, "tempo clamps to supported upper bound");
    CHECK(bubble_engine_set_parameter(&engine, BUBBLE_ENGINE_PARAM_FINAL_LIMITER_RELEASE_MS, 1.0f), "set limiter release");
    CHECK(bubble_engine_get_parameter(&engine, BUBBLE_ENGINE_PARAM_FINAL_LIMITER_RELEASE_MS, &value), "read limiter release");
    CHECK_CLOSE(value, 5.0f, 0.0001f, "limiter release clamps to minimum stable value");
    return 0;
}

static int test_freeze_stops_memory_writes_and_macro_reaches_freeze(void) {
    static int16_t delay[88200];
    BubbleEngineConfig_t config;
    BubbleEngine_t engine;
    init_engine(&engine, delay, &config);

    process_constant(&engine, 0.25f, BUBBLES_BLOCK_SIZE);
    CHECK(engine.write_ptr == BUBBLES_BLOCK_SIZE, "write pointer advances before freeze");
    int frozen_ptr = engine.write_ptr;
    int16_t before = delay[frozen_ptr];
    CHECK(bubble_engine_set_parameter(&engine, BUBBLE_PARAM_DEVELOPER_MODE, 1.0f), "enable developer mode for freeze param");
    CHECK(bubble_engine_set_parameter(&engine, BUBBLE_ENGINE_PARAM_FREEZE_ENABLED, 1.0f), "enable raw freeze");
    process_constant(&engine, 0.75f, BUBBLES_BLOCK_SIZE * 2);
    CHECK(engine.write_ptr == frozen_ptr, "freeze keeps write pointer stationary");
    CHECK(delay[frozen_ptr] == before, "freeze leaves delay memory untouched at the locked write head");

    bubble_engine_reset(&engine);
    CHECK(bubble_engine_set_parameter(&engine, BUBBLE_PARAM_FREEZE, 1.0f), "set product freeze macro");
    process_constant(&engine, 0.0f, BUBBLES_BLOCK_SIZE * 6);
    CHECK(engine.config.freeze_enabled == 1, "freeze macro slews into the raw freeze-enabled DSP state");
    return 0;
}

static int test_pitch_reverse_and_droplet_spawn_metadata(void) {
    static int16_t delay[88200];
    BubbleEngineConfig_t config;
    BubbleEngine_t engine;
    init_engine(&engine, delay, &config);
    CHECK(bubble_engine_set_parameter(&engine, BUBBLE_PARAM_DEVELOPER_MODE, 1.0f), "enable developer mode");
    CHECK(bubble_engine_set_parameter(&engine, BUBBLE_ENGINE_PARAM_PITCH_MODE, (float)BUBBLE_PITCH_MODE_OCTAVE_UP), "set octave-up mode");
    CHECK(bubble_engine_set_parameter(&engine, BUBBLE_ENGINE_PARAM_REVERSE_PROBABILITY, 1.0f), "force reverse direction");
    process_constant(&engine, 1.0f, BUBBLES_BLOCK_SIZE);
    CHECK(active_voice_count(&engine) >= 1, "transient block creates at least one bubble voice");
    int checked = 0;
    for (int i = 0; i < engine.active_voice_limit; i++) {
        BubbleVoice_t* voice = &engine.voices[i];
        if (voice->state == VOICE_STATE_INACTIVE) continue;
        CHECK(voice->read_direction == 1u, "reverse probability of 1 creates reverse voices");
        CHECK_CLOSE(voice->quantized_rate, 2.0f, 0.0001f, "octave-up pitch mode uses 2x quantized rate");
        CHECK_CLOSE(voice->rate, -2.0f, 0.0001f, "reverse octave-up voice has negative 2x playback rate");
        checked++;
    }
    CHECK(checked > 0, "inspected octave-up reverse voice metadata");

    init_engine(&engine, delay, &config);
    CHECK(bubble_engine_set_parameter(&engine, BUBBLE_PARAM_DEVELOPER_MODE, 1.0f), "enable developer mode for droplets");
    CHECK(bubble_engine_set_parameter(&engine, BUBBLE_ENGINE_PARAM_DROPLET_ENABLE, 1.0f), "enable droplets");
    CHECK(bubble_engine_set_parameter(&engine, BUBBLE_ENGINE_PARAM_DROPLET_PROBABILITY, 1.0f), "force droplets");
    CHECK(bubble_engine_set_parameter(&engine, BUBBLE_ENGINE_PARAM_DROPLET_GAIN, 0.25f), "set droplet gain");
    CHECK(bubble_engine_set_parameter(&engine, BUBBLE_ENGINE_PARAM_DROPLET_LENGTH_SCALE, 0.5f), "set droplet length");
    process_constant(&engine, 1.0f, BUBBLES_BLOCK_SIZE);
    int child_count = 0;
    for (int i = 0; i < engine.active_voice_limit; i++) {
        BubbleVoice_t* voice = &engine.voices[i];
        if (voice->state == VOICE_STATE_INACTIVE) continue;
        if (voice->generation == 1u) {
            child_count++;
            CHECK(voice->bubble_class == BUBBLE_CLASS_SHORT_INTERMEDIATE, "droplet child uses short/intermediate class");
            CHECK(voice->gain <= 0.25f * 1.8f, "droplet gain scaling is applied before tone shaping");
        }
    }
    CHECK(child_count == 1, "forced micro attack creates exactly one second-generation droplet child");
    return 0;
}

static int test_tempo_patterns_burst_modes_motion_and_metrics(void) {
    static int16_t delay[88200];
    BubbleEngineConfig_t config;
    BubbleEngine_t engine;
    MetricsCapture capture = {0};
    init_engine(&engine, delay, &config);
    CHECK(bubble_engine_set_parameter(&engine, BUBBLE_PARAM_DEVELOPER_MODE, 1.0f), "enable developer mode");
    CHECK(bubble_engine_set_parameter(&engine, BUBBLE_ENGINE_PARAM_TEMPO_SYNC_ENABLED, 1.0f), "enable tempo sync");
    CHECK(bubble_engine_set_parameter(&engine, BUBBLE_ENGINE_PARAM_RHYTHM_PATTERN, 2.0f), "make first rhythm step inactive");
    engine.env_follower_state = 0.5f;
    bubble_engine_set_metrics_callback(&engine, capture_metrics, &capture);
    process_constant(&engine, 0.5f, BUBBLES_BLOCK_SIZE);
    CHECK(capture.calls == 1, "metrics callback fires once per processed control block");
    CHECK(capture.last.spawn_count == 0, "inactive rhythm pattern step suppresses tempo-synced spawns");
    CHECK(active_voice_count(&engine) == 0, "no voices are allocated on inactive rhythm step");

    init_engine(&engine, delay, &config);
    capture.calls = 0;
    CHECK(bubble_engine_set_parameter(&engine, BUBBLE_PARAM_DEVELOPER_MODE, 1.0f), "enable developer mode for active rhythm");
    CHECK(bubble_engine_set_parameter(&engine, BUBBLE_ENGINE_PARAM_TEMPO_SYNC_ENABLED, 1.0f), "enable tempo sync active rhythm");
    CHECK(bubble_engine_set_parameter(&engine, BUBBLE_ENGINE_PARAM_RHYTHM_PATTERN, 1.0f), "make first rhythm step active");
    CHECK(bubble_engine_set_parameter(&engine, BUBBLE_ENGINE_PARAM_BURST_MODE, (float)BUBBLE_BURST_MODE_SWARM), "use swarm burst mode");
    CHECK(bubble_engine_set_parameter(&engine, BUBBLE_ENGINE_PARAM_BURST_IMMEDIATE_COUNT, 3.0f), "set swarm base count");
    engine.env_follower_state = 0.5f;
    bubble_engine_set_metrics_callback(&engine, capture_metrics, &capture);
    process_constant(&engine, 0.5f, BUBBLES_BLOCK_SIZE);
    CHECK(capture.calls == 1, "metrics callback fires for active rhythm block");
    CHECK(capture.last.spawn_count == SCHED_MAX_SPAWNS_PER_TICK, "swarm mode respects per-tick scheduler cap");
    CHECK(active_voice_count(&engine) == SCHED_MAX_SPAWNS_PER_TICK, "swarm mode allocates capped number of voices");

    float base_density = engine.motion_base_config.density_burst;
    CHECK(bubble_engine_set_parameter(&engine, BUBBLE_ENGINE_PARAM_MOTION_DEPTH, 1.0f), "enable runtime motion depth");
    CHECK(bubble_engine_set_parameter(&engine, BUBBLE_ENGINE_PARAM_MOTION_RATE, 1.0f), "set runtime motion rate");
    process_constant(&engine, 0.0f, BUBBLES_BLOCK_SIZE);
    CHECK(fabsf(engine.config.density_burst - base_density) > 0.001f, "runtime motion modulates live config without changing base config");
    CHECK_CLOSE(engine.motion_base_config.density_burst, base_density, 0.0001f, "motion keeps the authored base config stable");
    return 0;
}

static int test_host_rhythm_phase_sync_and_fixed_pitch_override(void) {
    static int16_t delay[88200];
    BubbleEngineConfig_t config;
    BubbleEngine_t engine;
    init_engine(&engine, delay, &config);
    CHECK(bubble_engine_set_parameter(&engine, BUBBLE_PARAM_DEVELOPER_MODE, 1.0f), "enable developer mode for host sync");
    CHECK(bubble_engine_set_parameter(&engine, BUBBLE_ENGINE_PARAM_RHYTHM_DIVISION,
                                      (float)BUBBLE_RHYTHM_DIVISION_SIXTEENTH),
          "set sixteenth-note host sync division");

    bubble_engine_sync_rhythm_phase(&engine, 0.0);
    CHECK(engine.rhythm_step_index == 0, "PPQ zero schedules rhythm step zero");
    CHECK_CLOSE(engine.rhythm_step_accumulator, 1.0f, 0.0001f, "exact PPQ boundary triggers immediately");

    bubble_engine_sync_rhythm_phase(&engine, 0.625);
    CHECK(engine.rhythm_step_index == 3, "mid-step PPQ schedules the following sixteenth step");
    CHECK_CLOSE(engine.rhythm_step_accumulator, 0.5f, 0.0001f, "mid-step PPQ preserves fractional progress");

    bubble_engine_sync_rhythm_phase(&engine, 1.0);
    CHECK(engine.rhythm_step_index == 4, "quarter-note PPQ boundary maps to sixteenth step four");
    CHECK_CLOSE(engine.rhythm_step_accumulator, 1.0f, 0.0001f, "quarter boundary triggers immediately");

    CHECK(bubble_engine_set_parameter(&engine, BUBBLE_ENGINE_PARAM_PITCH_MODE,
                                      (float)BUBBLE_PITCH_MODE_FIFTH),
          "set fixed fifth pitch mode");
    CHECK(bubble_engine_set_parameter(&engine, BUBBLE_PARAM_DENSITY, 0.8f), "move an unrelated macro");
    process_constant(&engine, 0.0f, BUBBLES_BLOCK_SIZE * 8);
    CHECK(engine.config.pitch_mode == BUBBLE_PITCH_MODE_FIFTH,
          "fixed fifth survives unrelated macro resolution");

    CHECK(bubble_engine_set_parameter(&engine, BUBBLE_ENGINE_PARAM_PITCH_MODE,
                                      (float)BUBBLE_PITCH_MODE_UNISON),
          "return pitch mode to macro control");
    CHECK(bubble_engine_set_parameter(&engine, BUBBLE_PARAM_SPARKLE, 1.0f), "raise sparkle macro");
    process_constant(&engine, 0.0f, BUBBLES_BLOCK_SIZE * 8);
    CHECK(engine.config.pitch_mode == BUBBLE_PITCH_MODE_SHIMMER,
          "macro-controlled pitch follows sparkle after override is cleared");
    return 0;
}

static int test_quality_profile_limits_allocation_and_drain(void) {
    static int16_t delay[88200];
    BubbleEngineConfig_t config;
    BubbleEngine_t engine;
    init_engine(&engine, delay, &config);

    CHECK(bubble_engine_set_quality_profile(&engine, BUBBLE_QUALITY_PROFILE_WEB_ULTRA), "switch to web ultra profile");
    CHECK(engine.active_voice_limit == 32, "web ultra exposes the full compiled voice pool");
    for (int i = 0; i < BUBBLES_MAX_VOICES; i++) {
        engine.voices[i].state = VOICE_STATE_PLAYING;
        engine.voices[i].phase = 0.1f;
        engine.voices[i].phase_inc = 0.00001f;
        engine.voices[i].rate = 1.0f;
        engine.voices[i].amp = 1.0f;
        engine.voices[i].fade_counter = 0;
    }
    CHECK(bubble_engine_set_quality_profile(&engine, BUBBLE_QUALITY_PROFILE_MCU_SAFE), "downgrade to MCU-safe profile");
    CHECK(engine.active_voice_limit == 8, "MCU-safe profile applies the 8-voice active limit");
    for (int i = 8; i < BUBBLES_MAX_VOICES; i++) {
        CHECK(engine.voices[i].state == VOICE_STATE_PREEMPT_FADING, "voices above downgraded active limit are faded out instead of hard-stopped");
        CHECK(engine.voices[i].fade_counter == BUBBLES_FADE_SAMPLES, "profile downgrade uses the standard preemption fade length");
    }
    CHECK(!bubble_engine_set_quality_profile(&engine, (BubbleQualityProfile)99), "unknown quality profile is rejected");
    return 0;
}

static void reset_voice_pool(BubbleEngine_t* engine) {
    for (int i = 0; i < BUBBLES_MAX_VOICES; i++) {
        engine->voices[i].state = VOICE_STATE_INACTIVE;
    }
}

static void set_playing_voice(BubbleEngine_t* engine, int idx, BubbleClass_t cls, float phase) {
    BubbleVoice_t* v = &engine->voices[idx];
    v->state = VOICE_STATE_PLAYING;
    v->bubble_class = cls;
    v->phase = phase;
    v->phase_inc = 0.0f;
    v->read_ptr_float = 40000.0f;
    v->rate = 0.0f;
    v->quantized_rate = 1.0f;
    v->amp = 1.0f;
    v->gain = 0.0f;
    v->pan_l = 1.0f;
    v->pan_r = 0.0f;
    v->fade_counter = 0;
}

static int find_preempt_fading_index(const BubbleEngine_t* engine) {
    for (int i = 0; i < engine->active_voice_limit; i++) {
        if (engine->voices[i].state == VOICE_STATE_PREEMPT_FADING) {
            return i;
        }
    }
    return -1;
}

static void force_transient_spawn(BubbleEngine_t* engine) {
    float in[BUBBLES_BLOCK_SIZE];
    float left[BUBBLES_BLOCK_SIZE];
    float right[BUBBLES_BLOCK_SIZE];
    for (int i = 0; i < BUBBLES_BLOCK_SIZE; i++) {
        in[i] = 1.0f;
    }
    engine->env_follower_state = 0.0f;
    engine->env_derivative = 0.0f;
    engine->burst_timer_ticks = 0;
    bubble_engine_process(engine, in, left, right, BUBBLES_BLOCK_SIZE);
}

static int init_stealing_engine(BubbleEngine_t* engine, int16_t* delay, BubbleEngineConfig_t* config) {
    bubble_engine_default_config(config);
    config->rng_seed = 0x12345678u;
    config->smart_start_enable = 0;
    config->burst_duration_ticks = 4;
    config->burst_immediate_count = 1;
    config->density_burst = 0.0f;
    config->density_sustain = 0.0f;
    config->density_decay = 0.0f;
    config->active_voice_limit = 4;
    memset(delay, 0, (size_t)88200 * sizeof(delay[0]));
    bubble_engine_init(engine, delay, config);
    return 0;
}

static int test_region_offsets_and_fade_are_sample_rate_invariant(void) {
    const float rates[4] = {44100.0f, 48000.0f, 88200.0f, 96000.0f};
    const int32_t ref_min[4] = {441, 3528, 11025, 8000};
    const int32_t ref_max[4] = {3528, 11025, 39690, 50000};

    for (int r = 0; r < 4; r++) {
        const double reference_seconds =
            (double)(ref_max[r] - ref_min[r]) / (double)BUBBLES_REFERENCE_SAMPLE_RATE_INT;
        for (int s = 0; s < 4; s++) {
            int32_t lo = bubble_engine_reference_samples_to_samples(ref_min[r], rates[s]);
            int32_t hi = bubble_engine_reference_samples_to_samples(ref_max[r], rates[s]);
            double seconds = (double)(hi - lo) / (double)rates[s];
            CHECK_CLOSE((float)seconds, (float)reference_seconds, 1.0e-4f, "region musical span is rate invariant");
        }
    }

    CHECK(bubble_engine_reference_samples_to_samples(441, 44100.0f) == 441,
          "44.1 kHz reference offset is unchanged at the reference rate");
    CHECK(bubble_engine_reference_samples_to_samples(441, 88200.0f) == 882,
          "reference offset doubles at 88.2 kHz");

    static int16_t delay[192000];
    for (int s = 0; s < 4; s++) {
        BubbleEngine_t engine;
        BubbleEngineConfig_t config;
        bubble_engine_default_config(&config);
        config.sample_rate = rates[s];
        memset(delay, 0, sizeof(delay));
        bubble_engine_init(&engine, delay, &config);
        int32_t expected = (int32_t)lroundf(BUBBLES_FADE_MS * 0.001f * rates[s]);
        CHECK(engine.fade_samples == expected, "preemption fade resolves from musical time per sample rate");
    }
    return 0;
}

static int test_voice_stealing_prefers_old_sustain_and_protects_young_micro(void) {
    static int16_t delay[88200];
    BubbleEngineConfig_t config;
    BubbleEngine_t engine;

    // Scenario 1: young micro must survive; oldest sustain is the victim.
    init_stealing_engine(&engine, delay, &config);
    reset_voice_pool(&engine);
    set_playing_voice(&engine, 0, BUBBLE_CLASS_MICRO_ATTACK, 0.10f);
    set_playing_voice(&engine, 1, BUBBLE_CLASS_SUSTAIN_BODY, 0.80f);
    set_playing_voice(&engine, 2, BUBBLE_CLASS_SHORT_INTERMEDIATE, 0.70f);
    set_playing_voice(&engine, 3, BUBBLE_CLASS_SUSTAIN_BODY, 0.30f);
    force_transient_spawn(&engine);
    CHECK(find_preempt_fading_index(&engine) == 1,
          "oldest sustain is stolen before short and before protected young micro");

    // Scenario 2: every voice is a young micro -> deterministic fallback to the oldest.
    init_stealing_engine(&engine, delay, &config);
    reset_voice_pool(&engine);
    set_playing_voice(&engine, 0, BUBBLE_CLASS_MICRO_ATTACK, 0.10f);
    set_playing_voice(&engine, 1, BUBBLE_CLASS_MICRO_ATTACK, 0.20f);
    set_playing_voice(&engine, 2, BUBBLE_CLASS_MICRO_ATTACK, 0.15f);
    set_playing_voice(&engine, 3, BUBBLE_CLASS_MICRO_ATTACK, 0.25f);
    force_transient_spawn(&engine);
    CHECK(find_preempt_fading_index(&engine) == 3,
          "all-young-micro fallback steals the oldest voice deterministically");

    // Scenario 3: no sustain -> short is preferred over an old non-protected micro.
    init_stealing_engine(&engine, delay, &config);
    reset_voice_pool(&engine);
    set_playing_voice(&engine, 0, BUBBLE_CLASS_MICRO_ATTACK, 0.90f);
    set_playing_voice(&engine, 1, BUBBLE_CLASS_SHORT_INTERMEDIATE, 0.60f);
    set_playing_voice(&engine, 2, BUBBLE_CLASS_MICRO_ATTACK, 0.20f);
    set_playing_voice(&engine, 3, BUBBLE_CLASS_SHORT_INTERMEDIATE, 0.80f);
    force_transient_spawn(&engine);
    CHECK(find_preempt_fading_index(&engine) == 3,
          "oldest short is stolen before an older non-protected micro");
    return 0;
}

static int test_strum_saturation_does_not_drop_events(void) {
    static int16_t delay[88200];
    BubbleEngineConfig_t config;
    BubbleEngine_t engine;
    init_engine(&engine, delay, &config);
    engine.active_voice_limit = 2;
    engine.config.active_voice_limit = 2;
    engine.motion_base_config.active_voice_limit = 2;

    reset_voice_pool(&engine);
    set_playing_voice(&engine, 0, BUBBLE_CLASS_SUSTAIN_BODY, 0.30f);
    set_playing_voice(&engine, 1, BUBBLE_CLASS_SUSTAIN_BODY, 0.60f);

    for (int i = 0; i < BUBBLES_PENDING_SPAWN_CAPACITY; i++) {
        engine.pending_spawns[i].bubble_class = BUBBLE_CLASS_MICRO_ATTACK;
        engine.pending_spawns[i].generation = 0;
    }
    engine.pending_spawn_head = 0;
    engine.pending_spawn_count = BUBBLES_PENDING_SPAWN_CAPACITY;
    engine.strum_pending_count = 3;
    engine.strum_step_index = 0;

    process_constant(&engine, 0.5f, BUBBLES_BLOCK_SIZE);
    CHECK(engine.strum_pending_count == 3,
          "strum events are not consumed while voices and the pending queue are saturated");

    engine.voices[0].state = VOICE_STATE_INACTIVE;
    process_constant(&engine, 0.5f, BUBBLES_BLOCK_SIZE);
    CHECK(engine.strum_pending_count < 3,
          "strum event is retried and consumed once capacity frees up");
    return 0;
}

static int test_spatial_split_keeps_dry_out_of_the_wet_bus(void) {
    static int16_t delay[88200];
    BubbleEngineConfig_t config;
    BubbleEngine_t engine;
    init_engine(&engine, delay, &config);
    CHECK(bubble_engine_set_parameter(&engine, BUBBLE_PARAM_DEVELOPER_MODE, 1.0f), "enable developer mode");
    CHECK(bubble_engine_set_parameter(&engine, BUBBLE_ENGINE_PARAM_STEREO_WIDTH, 1.0f), "widen stereo");
    CHECK(bubble_engine_set_parameter(&engine, BUBBLE_ENGINE_PARAM_ATTACK_PAN_SPREAD, 1.0f), "spread attacks");
    CHECK(bubble_engine_set_parameter(&engine, BUBBLE_ENGINE_PARAM_SUSTAIN_PAN_SPREAD, 1.0f), "spread sustain");
    CHECK(bubble_engine_set_parameter(&engine, BUBBLE_ENGINE_PARAM_TRANSIENT_DELTA, 0.02f), "sensitive transient detection");
    CHECK(bubble_engine_set_parameter(&engine, BUBBLE_ENGINE_PARAM_BURST_MODE, (float)BUBBLE_BURST_MODE_SPRAY), "spray burst mode");
    // Pull the read regions close so wet appears immediately in this short probe.
    bubble_engine_set_parameter(&engine, BUBBLE_ENGINE_PARAM_ATTACK_REGION_MIN_OFFSET_SAMPLES, 100.0f);
    bubble_engine_set_parameter(&engine, BUBBLE_ENGINE_PARAM_ATTACK_REGION_MAX_OFFSET_SAMPLES, 200.0f);
    bubble_engine_set_parameter(&engine, BUBBLE_ENGINE_PARAM_BODY_REGION_MIN_OFFSET_SAMPLES, 100.0f);
    bubble_engine_set_parameter(&engine, BUBBLE_ENGINE_PARAM_BODY_REGION_MAX_OFFSET_SAMPLES, 200.0f);
    bubble_engine_set_parameter(&engine, BUBBLE_ENGINE_PARAM_MEMORY_REGION_MIN_OFFSET_SAMPLES, 100.0f);
    bubble_engine_set_parameter(&engine, BUBBLE_ENGINE_PARAM_MEMORY_REGION_MAX_OFFSET_SAMPLES, 200.0f);
    bubble_engine_set_parameter(&engine, BUBBLE_ENGINE_PARAM_DENSITY_SUSTAIN, 80.0f);
    bubble_engine_set_parameter(&engine, BUBBLE_ENGINE_PARAM_BURST_IMMEDIATE_COUNT, 3.0f);
    engine.master_dry_gain = 1.0f;
    engine.master_wet_gain = 1.0f;
    engine.macro_dirty_mask = 0u;

    float in[BUBBLES_BLOCK_SIZE];
    float wet_l[BUBBLES_BLOCK_SIZE];
    float wet_r[BUBBLES_BLOCK_SIZE];
    float dry[BUBBLES_BLOCK_SIZE];
    for (int i = 0; i < BUBBLES_BLOCK_SIZE; i++) {
        in[i] = 0.5f;
    }

    float wet_energy = 0.0f;
    float wet_stereo_delta = 0.0f;
    for (int block = 0; block < 32; block++) {
        bubble_engine_process_spatial(&engine, in, wet_l, wet_r, dry, BUBBLES_BLOCK_SIZE);
        for (int i = 0; i < BUBBLES_BLOCK_SIZE; i++) {
            CHECK_CLOSE(dry[i], in[i] * engine.master_dry_gain, 1.0e-5f,
                        "spatial dry bus equals the engine dry policy");
            wet_energy += fabsf(wet_l[i]) + fabsf(wet_r[i]);
            wet_stereo_delta += fabsf(wet_l[i] - wet_r[i]);
        }
    }
    CHECK(wet_energy > 0.001f, "spatial split emits a wet bus");
    CHECK(wet_stereo_delta > 0.0f, "spatial wet field fills both channels asymmetrically");
    return 0;
}

int main(void) {
    if (test_developer_parameter_gate_and_clamping() != 0) return 1;
    if (test_freeze_stops_memory_writes_and_macro_reaches_freeze() != 0) return 1;
    if (test_pitch_reverse_and_droplet_spawn_metadata() != 0) return 1;
    if (test_tempo_patterns_burst_modes_motion_and_metrics() != 0) return 1;
    if (test_host_rhythm_phase_sync_and_fixed_pitch_override() != 0) return 1;
    if (test_quality_profile_limits_allocation_and_drain() != 0) return 1;
    if (test_region_offsets_and_fade_are_sample_rate_invariant() != 0) return 1;
    if (test_voice_stealing_prefers_old_sustain_and_protects_young_micro() != 0) return 1;
    if (test_strum_saturation_does_not_drop_events() != 0) return 1;
    if (test_spatial_split_keeps_dry_out_of_the_wet_bus() != 0) return 1;
    return 0;
}
