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
    memset(delay, 0, (size_t)BUBBLES_BUFFER_SIZE_SAMPLES * sizeof(delay[0]));
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
    static int16_t delay[BUBBLES_BUFFER_SIZE_SAMPLES];
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
    static int16_t delay[BUBBLES_BUFFER_SIZE_SAMPLES];
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
    static int16_t delay[BUBBLES_BUFFER_SIZE_SAMPLES];
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
    static int16_t delay[BUBBLES_BUFFER_SIZE_SAMPLES];
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

static int test_quality_profile_limits_allocation_and_drain(void) {
    static int16_t delay[BUBBLES_BUFFER_SIZE_SAMPLES];
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

int main(void) {
    if (test_developer_parameter_gate_and_clamping() != 0) return 1;
    if (test_freeze_stops_memory_writes_and_macro_reaches_freeze() != 0) return 1;
    if (test_pitch_reverse_and_droplet_spawn_metadata() != 0) return 1;
    if (test_tempo_patterns_burst_modes_motion_and_metrics() != 0) return 1;
    if (test_quality_profile_limits_allocation_and_drain() != 0) return 1;
    return 0;
}
