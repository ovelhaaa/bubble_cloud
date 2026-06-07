#ifndef BUBBLE_ENGINE_H
#define BUBBLE_ENGINE_H

#include <stdbool.h>
#include <stdint.h>
#include "../dsp/sound_bubbles_dsp.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef SoundBubblesEngine_t BubbleEngine_t;
typedef EngineConfig_t BubbleEngineConfig_t;
typedef SoundBubblesBlockMetrics_t BubbleEngineBlockMetrics_t;
typedef SoundBubblesMetricsCallback_t BubbleEngineMetricsCallback_t;

typedef struct {
    BubbleEngineConfig_t config;
    float master_dry_gain;
    float master_wet_gain;
} BubbleEnginePreset_t;

typedef enum {
    BUBBLE_ENGINE_PARAM_NOISE_FLOOR = 0,
    BUBBLE_ENGINE_PARAM_TRACKING_THRESH = 1,
    BUBBLE_ENGINE_PARAM_SUSTAIN_THRESH = 2,
    BUBBLE_ENGINE_PARAM_TRANSIENT_DELTA = 3,
    BUBBLE_ENGINE_PARAM_DUCK_BURST_LEVEL = 4,
    BUBBLE_ENGINE_PARAM_DUCK_ATTACK_COEF = 5,
    BUBBLE_ENGINE_PARAM_DUCK_RELEASE_COEF = 6,
    BUBBLE_ENGINE_PARAM_BURST_DURATION_TICKS = 7,
    BUBBLE_ENGINE_PARAM_BURST_IMMEDIATE_COUNT = 8,
    BUBBLE_ENGINE_PARAM_DENSITY_BURST = 9,
    BUBBLE_ENGINE_PARAM_DENSITY_SUSTAIN = 10,
    BUBBLE_ENGINE_PARAM_DENSITY_DECAY = 11,
    BUBBLE_ENGINE_PARAM_ATTACK_REGION_MIN_OFFSET_SAMPLES = 12,
    BUBBLE_ENGINE_PARAM_ATTACK_REGION_MAX_OFFSET_SAMPLES = 13,
    BUBBLE_ENGINE_PARAM_BODY_REGION_MIN_OFFSET_SAMPLES = 14,
    BUBBLE_ENGINE_PARAM_BODY_REGION_MAX_OFFSET_SAMPLES = 15,
    BUBBLE_ENGINE_PARAM_MEMORY_REGION_MIN_OFFSET_SAMPLES = 16,
    BUBBLE_ENGINE_PARAM_MEMORY_REGION_MAX_OFFSET_SAMPLES = 17,
    BUBBLE_ENGINE_PARAM_MICRO_DURATION_MS_MIN = 18,
    BUBBLE_ENGINE_PARAM_MICRO_DURATION_MS_MAX = 19,
    BUBBLE_ENGINE_PARAM_SHORT_DURATION_MS_MIN = 20,
    BUBBLE_ENGINE_PARAM_SHORT_DURATION_MS_MAX = 21,
    BUBBLE_ENGINE_PARAM_BODY_DURATION_MS_MIN = 22,
    BUBBLE_ENGINE_PARAM_BODY_DURATION_MS_MAX = 23,
    BUBBLE_ENGINE_PARAM_RNG_SEED = 24,
    BUBBLE_ENGINE_PARAM_MIX_DRY_GAIN = 25,
    BUBBLE_ENGINE_PARAM_MIX_WET_GAIN = 26,
    BUBBLE_ENGINE_PARAM_STEREO_WIDTH = 27,
    BUBBLE_ENGINE_PARAM_ATTACK_PAN_SPREAD = 28,
    BUBBLE_ENGINE_PARAM_SUSTAIN_PAN_SPREAD = 29,
    BUBBLE_ENGINE_PARAM_SMART_START_ENABLE = 30,
    BUBBLE_ENGINE_PARAM_SMART_START_RANGE = 31,
    BUBBLE_ENGINE_PARAM_ENVELOPE_VARIATION = 32,
    BUBBLE_ENGINE_PARAM_ENVELOPE_FAMILY = 33,
    BUBBLE_ENGINE_PARAM_WET_DRIVE = 34,
    BUBBLE_ENGINE_PARAM_WET_CLIP_AMOUNT = 35,
    BUBBLE_ENGINE_PARAM_WET_OUTPUT_TRIM = 36,
    BUBBLE_ENGINE_PARAM_SUSTAIN_DIFFUSION_ENABLE = 37,
    BUBBLE_ENGINE_PARAM_SUSTAIN_DIFFUSION_AMOUNT = 38,
    BUBBLE_ENGINE_PARAM_SUSTAIN_DIFFUSION_STAGES = 39,
    BUBBLE_ENGINE_PARAM_SUSTAIN_DIFFUSION_DELAY = 40,
    BUBBLE_ENGINE_PARAM_SUSTAIN_DIFFUSION_FEEDBACK = 41,
    BUBBLE_ENGINE_PARAM_DROPLET_ENABLE = 42,
    BUBBLE_ENGINE_PARAM_DROPLET_PROBABILITY = 43,
    BUBBLE_ENGINE_PARAM_DROPLET_GAIN = 44,
    BUBBLE_ENGINE_PARAM_DROPLET_LENGTH_SCALE = 45,
    BUBBLE_ENGINE_PARAM_MEMORY_MIX = 46,
    BUBBLE_ENGINE_PARAM_MEMORY_PULL = 47,
    BUBBLE_ENGINE_PARAM_MEMORY_DARKENING = 48,
    BUBBLE_ENGINE_PARAM_TONE_VARIATION = 49,
    BUBBLE_ENGINE_PARAM_ATTACK_BRIGHTNESS = 50,
    BUBBLE_ENGINE_PARAM_SUSTAIN_DARKNESS = 51,
    BUBBLE_ENGINE_PARAM_ATTACK_RATE_JITTER = 52,
    BUBBLE_ENGINE_PARAM_ATTACK_RATE_JITTER_DEPTH = 53,

    BUBBLE_ENGINE_PARAM_RUNTIME_ENVELOPE = 1000,
    BUBBLE_ENGINE_PARAM_RUNTIME_STATE = 1001,
    BUBBLE_ENGINE_PARAM_RUNTIME_ACTIVE_VOICES = 1002
} BubbleEngineParameterId_t;

void bubble_engine_default_config(BubbleEngineConfig_t* config);
void bubble_engine_init(BubbleEngine_t* engine, int16_t* delay_buffer_memory, const BubbleEngineConfig_t* initial_config);
void bubble_engine_reset(BubbleEngine_t* engine);
void bubble_engine_process(BubbleEngine_t* engine, const float* in_mono, float* out_left, float* out_right, int num_samples);
bool bubble_engine_set_parameter(BubbleEngine_t* engine, BubbleEngineParameterId_t parameter, float value);
bool bubble_engine_get_parameter(const BubbleEngine_t* engine, BubbleEngineParameterId_t parameter, float* value);
bool bubble_engine_load_preset(BubbleEngine_t* engine, const BubbleEnginePreset_t* preset);
bool bubble_engine_save_preset(const BubbleEngine_t* engine, BubbleEnginePreset_t* preset);
void bubble_engine_set_metrics_callback(BubbleEngine_t* engine, BubbleEngineMetricsCallback_t callback, void* user_data);

#ifdef __cplusplus
}
#endif

#endif // BUBBLE_ENGINE_H
