#define SOUND_BUBBLES_DSP_INTERNAL 1
#include "sound_bubbles_dsp.h"

#include <float.h>
#include <math.h>
#include <stddef.h>

size_t SoundBubbles_RequiredBufferSamples(float sample_rate) {
    return (size_t)(2.0f * sample_rate);
}

size_t SoundBubbles_RequiredBufferBytes(float sample_rate) {
    return SoundBubbles_RequiredBufferSamples(sample_rate) * sizeof(BubbleRingSample_t);
}

#if BUBBLES_RING_FLOAT
_Static_assert(sizeof(BubbleRingSample_t) == 4, "Float ring sample must be 4 bytes");
#else
_Static_assert(sizeof(BubbleRingSample_t) == 2, "Int16 ring sample must be 2 bytes");
#endif

int32_t SoundBubbles_ReferenceSamplesToSamples(int32_t reference_samples, float sample_rate) {
    if (sample_rate <= 0.0f || !isfinite(sample_rate)) {
        sample_rate = BUBBLES_REFERENCE_SAMPLE_RATE;
    }
    float scaled = (float)reference_samples * (sample_rate / BUBBLES_REFERENCE_SAMPLE_RATE);
    if (!isfinite(scaled)) {
        return reference_samples;
    }
    if (scaled > 2147483000.0f) return 2147483000;
    if (scaled < -2147483000.0f) return -2147483000;
    return (int32_t)lroundf(scaled);
}

void bubble_macro_map_default_values(float macro_values[BUBBLES_MACRO_COUNT]);

static int32_t ResolveFadeSamples(float sample_rate) {
    if (sample_rate <= 0.0f || !isfinite(sample_rate)) {
        sample_rate = BUBBLES_REFERENCE_SAMPLE_RATE;
    }
    int32_t samples = (int32_t)lroundf(BUBBLES_FADE_MS * 0.001f * sample_rate);
    if (samples < 1) samples = 1;
    return samples;
}

const BubbleQualityProfileLimits_t BUBBLE_QUALITY_PROFILE_LIMITS[BUBBLE_QUALITY_PROFILE_COUNT] = {
    { BUBBLE_QUALITY_PROFILE_MCU_SAFE,     "MCU_SAFE",      35, 256,  8 },
    { BUBBLE_QUALITY_PROFILE_MCU_PLUS,     "MCU_PLUS",      50, 384, 16 },
    { BUBBLE_QUALITY_PROFILE_WEB_STANDARD, "WEB_STANDARD",  60, 512, 24 },
    { BUBBLE_QUALITY_PROFILE_WEB_ULTRA,    "WEB_ULTRA",     75, 768, 32 },
};

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

// --- Internal Implementation Constants ---
#define ENV_ATTACK_COEF  0.1f   // ~fast tracking for attacks
#define ENV_RELEASE_COEF 0.01f  // ~slow tracking for sustain/decay

// Young micro-attack bubbles are protected until they are at least this far
// through their lifespan (ENGINE_BEHAVIOR_SPEC.md rule 1).
#define STEAL_MICRO_PROTECT_PHASE 0.5f
#define PRESENCE_BLOOM_TICKS 32

// --- M3.2A Tonal Bus Rebalance ---
// The attack bus only needs its rumble/DC removed so it keeps the fundamental
// and low-mid body instead of collapsing towards a thin "click" (was a fixed
// 1500 Hz HPF). 1-pole, fixed in this milestone.
#define ATTACK_HPF_CUTOFF_HZ 300.0f
// The sustain bus is opened up so it keeps body and presence and glues to the
// attack (was a fixed 2000 Hz LPF). Its cutoff breathes with phrase state and
// the resolved WARMTH/CLARITY darkness, inside this safe range. 1-pole,
// control-rate smoothed.
#define SUSTAIN_LPF_BASE_HZ 5000.0f
#define SUSTAIN_LPF_MIN_HZ 3500.0f
#define SUSTAIN_LPF_MAX_HZ 7000.0f
// Phrase-state openness (1 = open / bright, 0 = closed / dark).
#define SUSTAIN_LPF_OPEN_TRANSIENT 1.00f
#define SUSTAIN_LPF_OPEN_ATTACK    0.90f
#define SUSTAIN_LPF_OPEN_SUSTAIN   0.50f
#define SUSTAIN_LPF_OPEN_DECAY     0.25f
#define SUSTAIN_LPF_OPEN_SILENCE   0.10f
// High resolved darkness (WARMTH/CLARITY) pulls the sustain cutoff increasingly
// dark; low darkness keeps it open.
#define SUSTAIN_LPF_WARMTH_DARKEN 0.65f
// Control-rate cutoff smoothing time (~20 ms) avoids zipper on warmth automation
// and phrase transitions. Coefficients are refreshed only after the smoothed
// cutoff actually moved, so expf() runs at most a few times per control block.
#define SUSTAIN_LPF_SMOOTH_SECONDS 0.020f
#define SUSTAIN_LPF_COEFF_EPSILON_HZ 1.0f

// Internal non-UI defaults for bus and presence shaping (musical tuning constants).
#define CLASS_GAIN_MICRO_DEFAULT   1.15f
#define CLASS_GAIN_SHORT_DEFAULT   0.96f
#define CLASS_GAIN_SUSTAIN_DEFAULT 0.78f
#define DROPLET_OCCUPANCY_DISABLE 0.75f
#define DROPLET_OCCUPANCY_REDUCE 0.50f
#define SMART_START_ENERGY_RADIUS 3
// Extra samples added to the predicted guard path so float truncation at the
// interpolation edge cannot trip the runtime directional guard early (M2.1).
#define BUBBLES_GUARD_PATH_MARGIN 2
#define FINAL_LIMITER_DEFAULT_CEILING_DB -1.0f
#define FINAL_LIMITER_DEFAULT_RELEASE_MS 50.0f
#define BUBBLE_MOTION_FIXED_SEED 0xB06B1E5u
#define BUBBLE_MOTION_DENSITY_SEED_XOR 0x1001u
#define BUBBLE_MOTION_PANORAMA_SEED_XOR 0x2002u
#define BUBBLE_MOTION_MEMORY_PULL_SEED_XOR 0x3003u
#define BUBBLE_MOTION_SPARKLE_SEED_XOR 0x4004u
#define BUBBLE_MOTION_REVERSE_PROBABILITY_SEED_XOR 0x5005u
#define BUBBLE_MOTION_DIFFUSION_AMOUNT_SEED_XOR 0x6006u
#define BUBBLE_MOTION_INITIAL_VALUE_SEED_XOR 0xA5A5A5A5u

// M2 stereo coherence: probability that a spawn-time decision is drawn from the
// shared coherence stream instead of the per-channel stream. Attacks are almost
// identical between channels; sustain/decay/freeze progressively open up.
#define SPAWN_COHERENCE_ATTACK  0.80f
#define SPAWN_COHERENCE_SHORT   0.40f
#define SPAWN_COHERENCE_SUSTAIN 0.22f

// M2 context-conditioned reverse probability scaling per phrase phase.
#define REVERSE_SCALE_TRANSIENT 0.12f
#define REVERSE_SCALE_ATTACK    0.30f
#define REVERSE_SCALE_SUSTAIN   1.00f
#define REVERSE_SCALE_DECAY     1.70f
#define REVERSE_SCALE_SILENCE   0.80f
#define REVERSE_SCALE_FREEZE    2.10f

// --- M4A Auto-Hold & Phrase Anchor Tail Architecture ---
#ifndef BUBBLES_AUTO_HOLD_ATTACK_SECONDS
#define BUBBLES_AUTO_HOLD_ATTACK_SECONDS       0.080f
#define BUBBLES_AUTO_HOLD_BASE_RELEASE_SECONDS 2.200f
#define BUBBLES_AUTO_HOLD_MAX_RETENTION        0.965f
#define BUBBLES_AUTO_HOLD_THRESHOLD            0.015f
#define BUBBLES_AUTO_HOLD_ATTACK_PEAK          0.850f
#define BUBBLES_ANCHOR_MAX_MIX                 0.600f
#endif

#if !defined(BUBBLES_QUALITY_ESP32_SAFE) && !defined(BUBBLES_QUALITY_WASM_FULL)
#define BUBBLES_QUALITY_STANDARD 1
#endif

// --- Internal LUTs ---
static float WindowLUT_Hann[1024];
static float WindowLUT_Tukey[1024];
static bool luts_initialized = false;
static const uint32_t RNG_STATE_FALLBACK = 0x6D2B79F5u;

typedef struct {
    const ReadRegionConfig_t* region;
    uint8_t region_id;
    uint8_t memory_tier;   // BUBBLES_MEMORY_TIER_*
    float recent_bias;     // 0..1, higher = prefer the recent edge of the region
} ReadRegionChoice_t;

// Logical decision kinds that take part in the shared stereo event stream (M2.2).
// Combined with the canonical spawn provenance, the kind keeps multiple decisions
// in the same logical spawn from colliding and documents which musical choice is
// being shared.
typedef enum {
    BUBBLES_SHARED_DECISION_CLASS = 0,
    BUBBLES_SHARED_DECISION_REGION_TIER = 1,
    BUBBLES_SHARED_DECISION_OFFSET_BAND = 2,
    BUBBLES_SHARED_DECISION_COUNT = 3
} BubbleSharedDecisionKind_t;

// Hash lanes that split the share roll from the candidate shared value so the
// two are independent draws of the same event identity.
#define BUBBLES_SHARED_LANE_ROLL  0xA5A5A5A5u
#define BUBBLES_SHARED_LANE_VALUE 0x5A5A5A5Au

// M3.2C intra-tick onset jitter. The onset delay lives in its own hash
// namespace ("kind") and lane so it can never alias the shared class / region /
// offset draws of the same canonical event, and it never advances the
// per-channel sequential stream (pitch RNG, microdetune, reverse, pan, memory
// tier and spawn count are therefore bit-identical to the pre-M3.2C baseline).
#define BUBBLES_SHARED_KIND_ONSET 0x4F4E5345u   // 'ONSE'
#define BUBBLES_SHARED_LANE_ONSET 0x3C6EF372u

// Second-generation (droplet) child identity namespace (M2.4). Derived children
// mark the high bit of `child_index` so they can never collide with a primary
// burst child (which is a small, unflagged value). The parent's child_index and
// the generation are folded in, so a child is stable and parent-derived
// regardless of how many sibling droplets either channel produced earlier in the
// same tick, and the parent's (source, event_index) provenance is inherited.
#define BUBBLES_SPAWN_CHILD_DERIVED_FLAG    0x80000000u
#define BUBBLES_SPAWN_CHILD_PARENT_SHIFT    8u
#define BUBBLES_SPAWN_CHILD_PARENT_MASK     0x007FFFFFu
#define BUBBLES_SPAWN_CHILD_GENERATION_MASK 0x000000FFu

// --- Static Helper Prototypes ---
typedef enum {
    BUBBLES_OUTPUT_FULL = 0,    // dry + wet + final limiter to out_left/out_right
    BUBBLES_OUTPUT_SPATIAL = 1  // wet to out_left/out_right, dry to out_dry
} BubbleOutputMode_t;

// Optional white-box observation hook for the M3.2C spawn-jitter regression.
// Compiled out entirely in production builds; it records the deterministic onset
// delay chosen for each materialized spawn so a harness can compare the trace
// against the canonical identity. Never affects the audio path.
#if defined(BUBBLES_M3_ONSET_TRACE)
typedef void (*BubbleOnsetTraceFn)(void* user, const SoundBubblesEngine_t* engine,
                                   SharedSpawnId_t id, uint32_t onset_delay_samples);
static BubbleOnsetTraceFn g_bubble_onset_trace_fn = NULL;
static void* g_bubble_onset_trace_user = NULL;
static void BubblesTest_SetOnsetTrace(BubbleOnsetTraceFn fn, void* user) {
    g_bubble_onset_trace_fn = fn;
    g_bubble_onset_trace_user = user;
}
#endif

static int32_t ResolveFadeSamples(float sample_rate);
static void ProcessBlockInternal(SoundBubblesEngine_t* engine,
                                 const float* in_mono,
                                 float* out_left,
                                 float* out_right,
                                 float* out_dry,
                                 int num_samples,
                                 BubbleOutputMode_t output_mode);
static void InitWindowLUTs(void);
static uint32_t NextRandomU32(SoundBubblesEngine_t* engine);
static float RandomFloat01(SoundBubblesEngine_t* engine);
static float SharedSpawnRandom(SoundBubblesEngine_t* engine, float coherence, SharedSpawnId_t spawn_id, BubbleSharedDecisionKind_t kind);
static SharedSpawnId_t Scheduler_NextSpawnId(SoundBubblesEngine_t* engine, BubbleSpawnSource_t source);
static SharedSpawnId_t SpawnDerivedId(SharedSpawnId_t parent_id, uint32_t generation);
static uint32_t ResolveOnsetDelaySamples(SoundBubblesEngine_t* engine, SharedSpawnId_t spawn_id);
static void Scheduler_ResetSpawnIdentity(SoundBubblesEngine_t* engine);
static float ResolveSpawnCoherence(SoundBubblesEngine_t* engine);
static float ResolveContextReverseProbability(SoundBubblesEngine_t* engine, BubbleClass_t bubble_class, uint8_t region_id);
static float ResolveMicroDetuneCents(SoundBubblesEngine_t* engine, BubbleClass_t bubble_class);
static inline int32_t WrapIntIndex(int32_t index, int32_t size);
static inline float WrapFloatIndex(float index, float size);
static inline float LinearInterpolate(const BubbleRingSample_t* buffer, float index_float, int32_t buffer_size);
static inline float Hermite4Interpolate(const BubbleRingSample_t* buffer, float index_float, int32_t buffer_size);
static inline BubbleInterpolationMode_t ResolveInterpolationMode(BubbleQualityProfile profile);
static inline float InterpolateSample(SoundBubblesEngine_t* engine, const BubbleRingSample_t* buffer, float index_float, int32_t buffer_size);
static inline bool CheckGuardZoneDirectional(int32_t write_ptr, float read_ptr_float, float rate, int32_t buffer_size);
static float ResolvePitchModeRate(SoundBubblesEngine_t* engine);

static void CalculateFilterCoeffsLPF(Filter1Pole_t* f, float cutoff_hz, float sample_rate);
static void UpdateFilterCoeffsLPF(Filter1Pole_t* f, float cutoff_hz, float sample_rate);
static float ResolveSustainLpfTargetHz(const SoundBubblesEngine_t* engine);
static void UpdateSustainBusTone(SoundBubblesEngine_t* engine);
static inline float Filter1Pole_ProcessLPF(Filter1Pole_t* f, float input);
static inline float Filter1Pole_ProcessHPF(Filter1Pole_t* f, float input);
static void UpdateFeedbackCoeffs(SoundBubblesEngine_t* engine);
static void UpdateFeedbackTone(SoundBubblesEngine_t* engine);
static void UpdateFeedbackTarget(SoundBubblesEngine_t* engine);

static float UpdateEnvelope(float prev_state, float input_peak, float attack_coef, float release_coef);
static void UpdateStateAndDensity(SoundBubblesEngine_t* engine, float block_abs_peak);
static void Scheduler_SpawnImmediateBurst(SoundBubblesEngine_t* engine);
static int Scheduler_SpawnBurstMode(SoundBubblesEngine_t* engine, BubbleSpawnSource_t source, int max_spawns);
static BubbleClass_t Scheduler_SelectClassForState(SoundBubblesEngine_t* engine, SharedSpawnId_t spawn_id);
static bool Scheduler_IsRhythmStepActive(const SoundBubblesEngine_t* engine, int32_t step_index);
static float Scheduler_TicksPerRhythmStep(const SoundBubblesEngine_t* engine);
static void Scheduler_RunTick(SoundBubblesEngine_t* engine);
static int Voice_FindInactiveSlot(SoundBubblesEngine_t* engine);
static int Voice_Allocate(SoundBubblesEngine_t* engine);
static bool Voice_QueuePendingSpawn(SoundBubblesEngine_t* engine, BubbleClass_t b_class, int generation, SharedSpawnId_t spawn_id);
static int32_t Voice_CountFadingVoices(const SoundBubblesEngine_t* engine);
static int Voice_FlushPendingSpawns(SoundBubblesEngine_t* engine, int max_spawns);
static bool Voice_RequestSpawn(SoundBubblesEngine_t* engine, BubbleClass_t b_class, int generation, SharedSpawnId_t spawn_id);
static int32_t ClampSpawnOffsetForGuard(int32_t read_offset_samples, float rate, float duration_samples, int32_t buffer_size);
static void Voice_SpawnInit(SoundBubblesEngine_t* engine, int voice_idx, BubbleClass_t b_class, int generation, SharedSpawnId_t spawn_id);
static float LookupWindow(float phase, WindowType_t type);
static ReadRegionChoice_t ResolveReadRegionChoice(SoundBubblesEngine_t* engine, BubbleClass_t bubble_class, EngineState_t engine_state, SharedSpawnId_t spawn_id);
static int32_t ChooseReadOffsetSamples(SoundBubblesEngine_t* engine, const ReadRegionConfig_t* region, float recent_bias, float coherence, SharedSpawnId_t spawn_id);
static int32_t RefineReadOffsetSmartStart(const SoundBubblesEngine_t* engine, int32_t read_offset_samples, int32_t range, int32_t buffer_size);
static float EnvelopeVariantGain(float phase, uint8_t variant, int family);
static float SoftClip(float x, float amount);
static inline float DbToLinear(float db);
static inline void CacheFinalLimiterBlockParams(SoundBubblesEngine_t* engine);
static inline float ProcessFinalLimiterSample(SoundBubblesEngine_t* engine, float* l, float* r);
static inline float DbToLinear(float db) {
    return powf(10.0f, db * 0.05f);
}

static inline void CacheFinalLimiterBlockParams(SoundBubblesEngine_t* engine) {
    float ceiling = DbToLinear(engine->config.final_limiter_ceiling_db);
    if (!isfinite(ceiling) || ceiling <= 0.0f) ceiling = DbToLinear(FINAL_LIMITER_DEFAULT_CEILING_DB);
    if (ceiling > 1.0f) ceiling = 1.0f;
    engine->final_limiter_ceiling_linear = ceiling;

    float release_ms = engine->config.final_limiter_release_ms;
    if (!isfinite(release_ms) || release_ms <= 0.0f) release_ms = FINAL_LIMITER_DEFAULT_RELEASE_MS;
    float release_samples = release_ms * 0.001f * engine->config.sample_rate;
    engine->final_limiter_release_coef = 1.0f / fmaxf(1.0f, release_samples);
}

static inline float ProcessFinalLimiterSample(SoundBubblesEngine_t* engine, float* l, float* r) {
    float ceiling = engine->final_limiter_ceiling_linear;
    float peak = fmaxf(fabsf(*l), fabsf(*r));
    float target_gain = 1.0f;
    if (peak > ceiling) {
        target_gain = ceiling / peak;
        engine->metrics_clip_count_accum++;
    }

    if (target_gain < engine->final_limiter_gain) {
        engine->final_limiter_gain = target_gain;
    } else {
        engine->final_limiter_gain += (1.0f - engine->final_limiter_gain) * engine->final_limiter_release_coef;
        if (engine->final_limiter_gain > 1.0f) engine->final_limiter_gain = 1.0f;
    }

    *l *= engine->final_limiter_gain;
    *r *= engine->final_limiter_gain;
    return engine->final_limiter_gain;
}

static float ProcessSustainDiffusionSample(SoundBubblesEngine_t* engine, float in, float* delay_line, int delay_samples);
static void ApplyQualityTierDefaults(EngineConfig_t* cfg);
static int32_t ClampActiveVoiceLimit(int32_t requested_limit);
static int32_t ResolveProfileVoiceLimit(BubbleQualityProfile profile);
static void DeactivateVoicesAboveActiveLimit(SoundBubblesEngine_t* engine);
static inline float Clamp(float x, float lo, float hi) {
    return fmaxf(lo, fminf(hi, x));
}

static inline float Clamp01(float x) {
    return Clamp(x, 0.0f, 1.0f);
}

static inline float Lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

static void UpdateAutoHoldCoeffs(SoundBubblesEngine_t* engine) {
    float sr = fmaxf(1.0f, engine->config.sample_rate);
    float memory_val = Clamp01(engine->config.memory_mix);
    float release_sec = BUBBLES_AUTO_HOLD_BASE_RELEASE_SECONDS + 1.2f * memory_val;
    engine->auto_hold_attack_coef = 1.0f - expf(-1.0f / (sr * BUBBLES_AUTO_HOLD_ATTACK_SECONDS));
    engine->auto_hold_release_coef = 1.0f - expf(-1.0f / (sr * release_sec));
}

static void UpdateFeedbackCoeffs(SoundBubblesEngine_t* engine) {
    float sr = fmaxf(1.0f, engine->config.sample_rate);
    CalculateFilterCoeffsLPF(&engine->feedback_hpf, BUBBLES_FEEDBACK_HPF_HZ, sr);
    if (engine->feedback_lpf_cutoff_hz < 1000.0f) {
        engine->feedback_lpf_cutoff_hz = BUBBLES_FEEDBACK_LPF_BASE_HZ;
    }
    CalculateFilterCoeffsLPF(&engine->feedback_lpf, engine->feedback_lpf_cutoff_hz, sr);
    engine->feedback_gain_smooth_coef = 1.0f - expf(-1.0f / (sr * 0.015f));
    engine->feedback_energy_att_coef = 1.0f - expf(-1.0f / (sr * 0.010f));
    engine->feedback_energy_rel_coef = 1.0f - expf(-1.0f / (sr * 0.300f));
}

static int32_t CountActiveVoices(const SoundBubblesEngine_t* engine);
static void MotionResetLfo(BubbleMotionLfoState_t* lfo, uint32_t seed);

// --- M4C Ring Buffer Backend Helpers ---

static inline void Ring_ClearBuffer(BubbleRingSample_t* buffer, size_t sample_count) {
    if (buffer == NULL) return;
    for (size_t i = 0; i < sample_count; ++i) {
        buffer[i] = (BubbleRingSample_t)0;
    }
}

static inline uint32_t Ring_NextDitherU32(SoundBubblesEngine_t* engine) {
    uint32_t x = engine->ring_dither_rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    engine->ring_dither_rng = x;
    return x;
}

static inline int16_t Ring_EncodeInt16(SoundBubblesEngine_t* engine, float x) {
    // Silence protection: do not add dither to pure zero or below sub-audible threshold
    if (x == 0.0f || fabsf(x) < 1.0e-9f) {
        return 0;
    }
    float dither = 0.0f;
    if (engine != NULL && engine->dither_enabled) {
        // TPDF: difference of two uniform random variables in [0, 1),
        // resulting in triangular probability density function in (-1, +1) LSB.
        const float kInv24Bit = 1.0f / 16777216.0f;
        float r1 = (float)(Ring_NextDitherU32(engine) >> 8) * kInv24Bit;
        float r2 = (float)(Ring_NextDitherU32(engine) >> 8) * kInv24Bit;
        dither = r1 - r2;
    }
    float scaled = x * 32767.0f + dither;
    if (scaled > 32767.0f) scaled = 32767.0f;
    if (scaled < -32767.0f) scaled = -32767.0f;
    return (int16_t)lrintf(scaled);
}

static inline void Ring_WriteSample(SoundBubblesEngine_t* engine, int32_t index, float sample_val) {
#if BUBBLES_RING_FLOAT
    // Tiny deterministic zeroing threshold (1.0e-15f, approx -300 dBFS),
    // far below audibility or 32-bit float significant precision,
    // to prevent IEEE 754 denormal/subnormal state slowdowns in late feedback tails.
    if (fabsf(sample_val) < 1.0e-15f) {
        sample_val = 0.0f;
    }
    engine->delay_buffer[index] = sample_val;
#else
    engine->delay_buffer[index] = Ring_EncodeInt16(engine, sample_val);
#endif
}

// --- Initialization & Config ---

void SoundBubbles_Init(SoundBubblesEngine_t* engine, BubbleRingSample_t* delay_buffer_memory, const EngineConfig_t* initial_config) {
    InitWindowLUTs();

    engine->delay_buffer = delay_buffer_memory;
    engine->dither_enabled = 1u;
    engine->config = *initial_config;
    ApplyQualityTierDefaults(&engine->config);
    engine->motion_base_config = engine->config;
    engine->active_voice_limit = engine->config.active_voice_limit;
    engine->interpolation_mode = ResolveInterpolationMode(engine->config.quality_profile);
#if defined(BUBBLES_INTERPOLATION_TELEMETRY) || defined(BUBBLES_BUILD_PROCESSOR_TESTS)
    engine->interpolation_linear_samples = 0;
    engine->interpolation_hermite_samples = 0;
#endif
    engine->fade_samples = ResolveFadeSamples(engine->config.sample_rate);
    engine->channel_decorrelation = 0u;
    SoundBubbles_SetRngSeed(engine, engine->config.rng_seed);

    engine->write_ptr = 0;
    engine->block_counter = 0;
    engine->block_peak_accum = 0.0f;
    engine->scheduler_tick = 0;
    Scheduler_ResetSpawnIdentity(engine);
    engine->engine_state = ENGINE_STATE_SILENCE;

    engine->env_follower_state = 0.0f;
    engine->env_derivative = 0.0f;
    engine->burst_timer_ticks = 0;

    engine->target_density = 0.0f;
    engine->spawn_accumulator = 0.0f;
    engine->rhythm_step_accumulator = 1.0f;
    engine->rhythm_step_index = 0;
    engine->strum_pending_count = 0;
    engine->strum_step_index = 0;
    engine->force_reverse_spawns = 0;

    engine->internal_ducking_target = 1.0f;
    engine->smoothed_ducking_gain = 1.0f;
    engine->class_gain_micro = CLASS_GAIN_MICRO_DEFAULT;
    engine->class_gain_short = CLASS_GAIN_SHORT_DEFAULT;
    engine->class_gain_sustain = CLASS_GAIN_SUSTAIN_DEFAULT;
    engine->wet_presence_target = 0.0f;
    engine->wet_presence_smoothed = 0.0f;
    engine->bloom_timer_ticks = 0;

    engine->master_dry_gain = 1.0f;
    engine->master_wet_gain = 1.0f;
    float init_amount = Clamp01(engine->config.freeze_amount);
    engine->smoothed_freeze = (init_amount > 0.0f) ? init_amount : ((engine->config.freeze_enabled != 0) ? 1.0f : 0.0f);
    engine->auto_hold_state = AUTO_HOLD_IDLE;
    engine->auto_hold_amount = 0.0f;
    engine->auto_hold_target = 0.0f;
    engine->recent_phrase_active = 0;
    engine->phrase_anchor_write_ptr = 0;
    engine->phrase_anchor_valid = false;
    engine->phrase_anchor_age = 0;
    engine->anchor_mix = 0.0f;
    UpdateAutoHoldCoeffs(engine);
    engine->feedback_enabled = 1;
    engine->feedback_sample = 0.0f;
    engine->feedback_gain = 0.0f;
    engine->feedback_gain_target = 0.0f;
    engine->feedback_energy = 0.0f;
    engine->feedback_safety_threshold = BUBBLES_FEEDBACK_ENERGY_SAFETY_TH;
    engine->feedback_lpf_cutoff_hz = BUBBLES_FEEDBACK_LPF_BASE_HZ;
    engine->last_write_input = 0.0f;
    engine->last_write_feedback = 0.0f;
    engine->last_write_retained = 0.0f;
    engine->feedback_write_aperture = 0.0f;
    engine->ring_softclip_count = 0u;
    engine->ring_clamp_count = 0u;
    UpdateFeedbackCoeffs(engine);
    bubble_macro_map_default_values(engine->macro_values);
    bubble_macro_map_default_values(engine->macro_targets);
    engine->macro_dirty_mask = (1u << BUBBLES_MACRO_COUNT) - 1u;
    engine->developer_mode = 0;
    engine->final_limiter_gain = 1.0f;
    engine->final_limiter_ceiling_linear = 1.0f;
    engine->final_limiter_release_coef = 0.0f;
    engine->metrics_peak_l_accum = 0.0f;
    engine->metrics_peak_r_accum = 0.0f;
    engine->metrics_clip_count_accum = 0;
    engine->metrics_limiter_gain_min = 1.0f;
    engine->metrics_callback = NULL;
    engine->metrics_user_data = NULL;
    engine->metrics_last_block.spawn_count = 0;
    engine->metrics_last_block.active_voices = 0;
    engine->metrics_last_block.engine_state = ENGINE_STATE_SILENCE;
    engine->metrics_last_block.ducking_gain = 1.0f;
    engine->metrics_last_block.envelope = 0.0f;
    engine->metrics_last_block.peak_l = 0.0f;
    engine->metrics_last_block.peak_r = 0.0f;
    engine->metrics_last_block.clip_count = 0;
    engine->metrics_last_block.limiter_gain = 1.0f;
    engine->metrics_tick_spawn_count = 0;
    SoundBubbles_ResetMotionPhase(engine);
    engine->pending_spawn_head = 0;
    engine->pending_spawn_count = 0;

    int32_t buffer_size = (int32_t)SoundBubbles_RequiredBufferSamples(engine->config.sample_rate);
    Ring_ClearBuffer(engine->delay_buffer, (size_t)buffer_size);

    for (int i = 0; i < BUBBLES_MAX_VOICES; i++) {
        engine->voices[i].state = VOICE_STATE_INACTIVE;
    }

    // Attack HPF (implemented internally as input - LPF), one state per stereo channel.
    // M3.2A: only removes rumble/DC; fundamental and low-mid body are preserved so
    // attack and sustain share the same source body.
    CalculateFilterCoeffsLPF(&engine->attack_hpf_l, ATTACK_HPF_CUTOFF_HZ, engine->config.sample_rate);
    CalculateFilterCoeffsLPF(&engine->attack_hpf_r, ATTACK_HPF_CUTOFF_HZ, engine->config.sample_rate);
    // Sustain LPF, one state per stereo channel. Starts open at the base cutoff and
    // is then breathed by UpdateSustainBusTone() at control rate.
    CalculateFilterCoeffsLPF(&engine->sustain_lpf_l, SUSTAIN_LPF_BASE_HZ, engine->config.sample_rate);
    CalculateFilterCoeffsLPF(&engine->sustain_lpf_r, SUSTAIN_LPF_BASE_HZ, engine->config.sample_rate);
    engine->sustain_lpf_cutoff_smoothed_hz = SUSTAIN_LPF_BASE_HZ;
    engine->sustain_lpf_applied_hz = SUSTAIN_LPF_BASE_HZ;
    {
        float control_dt = (float)BUBBLES_BLOCK_SIZE / fmaxf(1.0f, engine->config.sample_rate);
        engine->sustain_lpf_smooth_coef = 1.0f - expf(-control_dt / SUSTAIN_LPF_SMOOTH_SECONDS);
    }

    engine->ducking_lpf.b0 = engine->config.duck_attack_coef;
    engine->ducking_lpf.a1 = 1.0f - engine->config.duck_attack_coef;
    engine->ducking_lpf.z1 = 1.0f;

    // Faster response when presence rises, slower response when it relaxes.
    engine->wet_presence_lpf.b0 = 0.2f;
    engine->wet_presence_lpf.a1 = 0.8f;
    engine->wet_presence_lpf.z1 = 0.0f;
    engine->sustain_diffusion_write_idx = 0;
    for (int i = 0; i < BUBBLES_SUSTAIN_DIFFUSION_MAX_DELAY; i++) {
        engine->sustain_diffusion_delay_l[i] = 0.0f;
        engine->sustain_diffusion_delay_r[i] = 0.0f;
        engine->sustain_diffusion_delay2_l[i] = 0.0f;
        engine->sustain_diffusion_delay2_r[i] = 0.0f;
    }
}

void SoundBubbles_UpdateConfig(SoundBubblesEngine_t* engine, const EngineConfig_t* new_config) {
    bool rng_seed_changed = (engine->motion_base_config.rng_seed != new_config->rng_seed);
    engine->motion_base_config = *new_config;
    ApplyQualityTierDefaults(&engine->motion_base_config);
    engine->config = engine->motion_base_config;
    engine->active_voice_limit = engine->config.active_voice_limit;
    engine->interpolation_mode = ResolveInterpolationMode(engine->config.quality_profile);
    engine->fade_samples = ResolveFadeSamples(engine->config.sample_rate);
    DeactivateVoicesAboveActiveLimit(engine);
    UpdateAutoHoldCoeffs(engine);
    UpdateFeedbackCoeffs(engine);
    if (rng_seed_changed) {
        SoundBubbles_SetRngSeed(engine, engine->config.rng_seed);
    }
}

void SoundBubbles_UpdateRuntimeConfig(SoundBubblesEngine_t* engine, const EngineConfig_t* new_config) {
    engine->config = *new_config;
    ApplyQualityTierDefaults(&engine->config);
    engine->active_voice_limit = engine->config.active_voice_limit;
    engine->interpolation_mode = ResolveInterpolationMode(engine->config.quality_profile);
    engine->fade_samples = ResolveFadeSamples(engine->config.sample_rate);
    DeactivateVoicesAboveActiveLimit(engine);
    UpdateAutoHoldCoeffs(engine);
    UpdateFeedbackCoeffs(engine);
}

void SoundBubbles_ResetMotionPhase(SoundBubblesEngine_t* engine) {
    if (engine == NULL) return;
    engine->motion_state.seed = BUBBLE_MOTION_FIXED_SEED;
    MotionResetLfo(&engine->motion_state.density, SoundBubbles_MotionHash(BUBBLE_MOTION_FIXED_SEED ^ BUBBLE_MOTION_DENSITY_SEED_XOR));
    MotionResetLfo(&engine->motion_state.panorama, SoundBubbles_MotionHash(BUBBLE_MOTION_FIXED_SEED ^ BUBBLE_MOTION_PANORAMA_SEED_XOR));
    MotionResetLfo(&engine->motion_state.memory_pull, SoundBubbles_MotionHash(BUBBLE_MOTION_FIXED_SEED ^ BUBBLE_MOTION_MEMORY_PULL_SEED_XOR));
    MotionResetLfo(&engine->motion_state.sparkle, SoundBubbles_MotionHash(BUBBLE_MOTION_FIXED_SEED ^ BUBBLE_MOTION_SPARKLE_SEED_XOR));
    MotionResetLfo(&engine->motion_state.reverse_probability, SoundBubbles_MotionHash(BUBBLE_MOTION_FIXED_SEED ^ BUBBLE_MOTION_REVERSE_PROBABILITY_SEED_XOR));
    MotionResetLfo(&engine->motion_state.diffusion_amount, SoundBubbles_MotionHash(BUBBLE_MOTION_FIXED_SEED ^ BUBBLE_MOTION_DIFFUSION_AMOUNT_SEED_XOR));
}

void SoundBubbles_SetRngSeed(SoundBubblesEngine_t* engine, uint32_t seed) {
    engine->config.rng_seed = seed;
    uint32_t base = (seed == 0u) ? RNG_STATE_FALLBACK : seed;
    // The per-channel stream is deliberately decorrelated; shared decisions are
    // stateless and derive from this undecorrelated base seed, so both engines
    // agree on the shared value of the same logical event.
    engine->rng_state = base ^ engine->channel_decorrelation;
    engine->shared_event_seed = base;

    // Dither RNG: independent stream keyed by engine seed + fixed namespace.
    // Using a distinct constant ensures the dither stream never collides with
    // the audio PRNG state, and toggling dither never alters musical decisions.
    uint32_t dither_seed = base ^ 0x54504446u;
    if (dither_seed == 0u) dither_seed = 0x54504446u;
    engine->ring_dither_rng = dither_seed;
}

void SoundBubbles_SetChannelDecorrelation(SoundBubblesEngine_t* engine, uint32_t decorrelation_mask) {
    if (engine == NULL) return;
    engine->channel_decorrelation = decorrelation_mask;
    SoundBubbles_SetRngSeed(engine, engine->config.rng_seed);
}

void SoundBubbles_SetMetricsCallback(SoundBubblesEngine_t* engine, SoundBubblesMetricsCallback_t callback, void* user_data) {
    engine->metrics_callback = callback;
    engine->metrics_user_data = user_data;
}

// --- Audio-Rate Processing Loop ---

void SoundBubbles_ProcessBlock(SoundBubblesEngine_t* engine, const float* in_mono, float* out_left, float* out_right, int num_samples) {
    ProcessBlockInternal(engine, in_mono, out_left, out_right, NULL, num_samples, BUBBLES_OUTPUT_FULL);
}

void SoundBubbles_ProcessBlockSpatial(SoundBubblesEngine_t* engine, const float* in_mono, float* out_wet_left, float* out_wet_right, float* out_dry_mono, int num_samples) {
    ProcessBlockInternal(engine, in_mono, out_wet_left, out_wet_right, out_dry_mono, num_samples, BUBBLES_OUTPUT_SPATIAL);
}

static void ProcessBlockInternal(SoundBubblesEngine_t* engine, const float* in_mono, float* out_left, float* out_right, float* out_dry, int num_samples, BubbleOutputMode_t output_mode) {
    if (engine == NULL || in_mono == NULL || out_left == NULL || out_right == NULL || num_samples <= 0) return;
    int32_t buffer_size = (int32_t)SoundBubbles_RequiredBufferSamples(engine->config.sample_rate);
    float sr = fmaxf(1.0f, engine->config.sample_rate);
    float freeze_smooth_coef = 1.0f - expf(-1.0f / (sr * 0.020f));
    float amount = Clamp01(engine->config.freeze_amount);
    float freeze_target = (amount > 0.0f) ? amount : ((engine->config.freeze_enabled != 0) ? 1.0f : 0.0f);
    bool discrete_freeze_lock = (engine->config.freeze_enabled != 0 && (amount == 0.0f || amount >= 0.999f));
    if (discrete_freeze_lock) {
        engine->smoothed_freeze = 1.0f;
    }

    for (int i = 0; i < num_samples; i++) {
        if (i == 0) {
            CacheFinalLimiterBlockParams(engine);
        }

        // Update continuous freeze state (M3)
        float f_delta = freeze_target - engine->smoothed_freeze;
        if (fabsf(f_delta) < 1.0e-5f) {
            engine->smoothed_freeze = freeze_target;
        } else {
            engine->smoothed_freeze += f_delta * freeze_smooth_coef;
        }
        float f = engine->smoothed_freeze;

        // Update Auto-Hold continuous envelope (M4A)
        float h_delta = engine->auto_hold_target - engine->auto_hold_amount;
        if (fabsf(h_delta) < 1.0e-6f) {
            engine->auto_hold_amount = engine->auto_hold_target;
        } else {
            float h_coef = (h_delta > 0.0f) ? engine->auto_hold_attack_coef : engine->auto_hold_release_coef;
            engine->auto_hold_amount += h_delta * h_coef;
        }
        float h = engine->auto_hold_amount;

        // Smoothstep write retention curves (M3 & M4A)
        float manual_retention = f * f * (3.0f - 2.0f * f);
        float auto_curve = h * h * (3.0f - 2.0f * h);
        float auto_retention = auto_curve * BUBBLES_AUTO_HOLD_MAX_RETENTION;

        // Composed monotonic retention (Section 5)
        float effective_retention = 1.0f - (1.0f - manual_retention) * (1.0f - auto_retention);
        if (effective_retention > 1.0f) effective_retention = 1.0f;
        if (effective_retention < 0.0f) effective_retention = 0.0f;
        float write_gain = 1.0f - effective_retention;
        bool write_locked = discrete_freeze_lock || (f >= 0.999f);

        // Update feedback gain smoothing (M4B)
        float g_delta = engine->feedback_gain_target - engine->feedback_gain;
        if (fabsf(g_delta) < 1.0e-5f) {
            engine->feedback_gain = engine->feedback_gain_target;
        } else {
            engine->feedback_gain += g_delta * engine->feedback_gain_smooth_coef;
        }

        float dry_sample = in_mono[i];

        // Track peak for control block envelope
        float in_abs = fabsf(dry_sample);
        if (in_abs > engine->block_peak_accum) {
            engine->block_peak_accum = in_abs;
        }

        // Clamp input to [-1.0f, 1.0f] before conversion. Continuous retention blends
        // incoming live audio with retained memory, locking the head only at full freeze.
        float clamped_sample = fmaxf(-1.0f, fminf(1.0f, dry_sample));
        float live_input_component = clamped_sample * write_gain;
        float old_sample = Ring_ReadNormalizedSample(engine->delay_buffer, engine->write_ptr);
        float retained_component = old_sample * effective_retention;

        // Feedback write aperture (M4B.1)
        float base_aperture = 1.0f - effective_retention;
        float hold_regen_aperture = engine->auto_hold_amount * BUBBLES_FEEDBACK_HOLD_APERTURE_MAX;
        float feedback_write_aperture = base_aperture + hold_regen_aperture;
        feedback_write_aperture = Clamp(feedback_write_aperture, 0.0f, BUBBLES_FEEDBACK_WRITE_APERTURE_MAX);
        feedback_write_aperture *= (1.0f - manual_retention);

        float feedback_component = engine->feedback_sample * engine->feedback_gain * feedback_write_aperture;
        feedback_component = Clamp(feedback_component, -BUBBLES_FEEDBACK_SAFE_BOUND, BUBBLES_FEEDBACK_SAFE_BOUND);

        engine->feedback_write_aperture = feedback_write_aperture;
        engine->last_write_input = live_input_component;
        engine->last_write_feedback = feedback_component;
        engine->last_write_retained = retained_component;

        if (!write_locked) {
            float ring_mixed = retained_component + live_input_component;
            if (engine->feedback_enabled && fabsf(feedback_component) > 1.0e-7f) {
                ring_mixed += feedback_component;
                if (fabsf(ring_mixed) > 0.5f) {
                    engine->ring_softclip_count++;
                }
                ring_mixed = SoftClip(ring_mixed, 0.20f);
            }
            if (ring_mixed > 1.0f || ring_mixed < -1.0f) {
                engine->ring_clamp_count++;
            }
            ring_mixed = Clamp(ring_mixed, -1.0f, 1.0f);
            Ring_WriteSample(engine, engine->write_ptr, ring_mixed);
        }

        // Zero audio busses
        float bus_attack_l = 0.0f, bus_attack_r = 0.0f;
        float bus_flat_l = 0.0f, bus_flat_r = 0.0f;
        float bus_sustain_l = 0.0f, bus_sustain_r = 0.0f;

        // Process active voices, plus any retired voices above the active limit
        // that are draining through the normal preempt-fade path after a profile
        // downgrade. No new voices are allocated outside active_voice_limit.
        for (int v_idx = 0; v_idx < BUBBLES_MAX_VOICES; v_idx++) {
            BubbleVoice_t* v = &engine->voices[v_idx];
            if (v->state == VOICE_STATE_INACTIVE) continue;
            if (v_idx >= engine->active_voice_limit && v->state != VOICE_STATE_PREEMPT_FADING) continue;

            // M3.2C: a pending grain is allocated but contributes nothing until
            // its deterministic intra-tick onset arrives. While waiting, neither
            // phase nor read pointer advances and no audio is generated, so it is
            // not yet "started" for any age-dependent logic. At the onset sample
            // the read pointer is re-derived from the current write head, which
            // guarantees the guard stays valid even though the head advanced
            // during the delay (deterministic write-head compensation).
            if (v->state == VOICE_STATE_PENDING_ONSET) {
                if (v->onset_delay_samples > 0) {
                    v->onset_delay_samples--;
                    continue;
                }
                v->state = VOICE_STATE_PLAYING;
                v->read_ptr_float = (float)WrapIntIndex(engine->write_ptr - v->spawn_read_offset, buffer_size);
            }

            // Handle preemption and forced release fading
            if (v->state == VOICE_STATE_PREEMPT_FADING) {
                v->fade_counter--;
                v->amp = (float)v->fade_counter * (1.0f / (float)engine->fade_samples);
                if (v->fade_counter <= 0) {
                    v->state = VOICE_STATE_INACTIVE;
                    continue;
                }
            } else {
                v->amp = 1.0f;
            }

            // Advance phase
            v->phase += v->phase_inc;
            if (v->phase >= 1.0f) {
                v->state = VOICE_STATE_INACTIVE;
                continue;
            }

            // Advance read_ptr with optional spawn-time attack jittered rate.
            v->read_ptr_float += v->rate;
            v->read_ptr_float = WrapFloatIndex(v->read_ptr_float, (float)buffer_size);

            // Directional write-head guard. Reverse voices also reject the
            // opposite-side guard band so interpolation cannot wrap into the
            // frozen/live write head neighborhood.
            if (v->state == VOICE_STATE_PLAYING && CheckGuardZoneDirectional(engine->write_ptr, v->read_ptr_float, v->rate, buffer_size)) {
                v->state = VOICE_STATE_PREEMPT_FADING;
                v->fade_counter = engine->fade_samples;
            }

            // Interpolate (profile-selected linear/Hermite) and apply window
            float sample_val = InterpolateSample(engine, engine->delay_buffer, v->read_ptr_float, buffer_size);
            float window_val = LookupWindow(v->phase, engine->config.class_configs[v->bubble_class].window_type);
            float env_var = EnvelopeVariantGain(v->phase, v->envelope_variant, engine->config.envelope_family);
            float voice_out = sample_val * window_val * env_var * v->amp * v->gain;

            // Accumulate into designated bus
            if (v->bubble_class == BUBBLE_CLASS_MICRO_ATTACK) {
                bus_attack_l += voice_out * v->pan_l;
                bus_attack_r += voice_out * v->pan_r;
            } else if (v->bubble_class == BUBBLE_CLASS_SHORT_INTERMEDIATE) {
                bus_flat_l += voice_out * v->pan_l;
                bus_flat_r += voice_out * v->pan_r;
            } else {
                bus_sustain_l += voice_out * v->pan_l;
                bus_sustain_r += voice_out * v->pan_r;
            }
        }

        // Bus Filters
        float attack_filtered_l = Filter1Pole_ProcessHPF(&engine->attack_hpf_l, bus_attack_l);
        float attack_filtered_r = Filter1Pole_ProcessHPF(&engine->attack_hpf_r, bus_attack_r);
        float sustain_filtered_l = Filter1Pole_ProcessLPF(&engine->sustain_lpf_l, bus_sustain_l);
        float sustain_filtered_r = Filter1Pole_ProcessLPF(&engine->sustain_lpf_r, bus_sustain_r);

        if (engine->config.sustain_diffusion_enable) {
            int delay_samples = SoundBubbles_ReferenceSamplesToSamples(
                engine->config.sustain_diffusion_delay, engine->config.sample_rate);
            if (delay_samples < 2) delay_samples = 2;
            if (delay_samples >= BUBBLES_SUSTAIN_DIFFUSION_MAX_DELAY) {
                delay_samples = BUBBLES_SUSTAIN_DIFFUSION_MAX_DELAY - 1;
            }
            int stages = engine->config.sustain_diffusion_stages;
            if (stages < 1) stages = 1;
            if (stages > 2) stages = 2;

            float d_l = sustain_filtered_l;
            float d_r = sustain_filtered_r;
            d_l = ProcessSustainDiffusionSample(engine, d_l, engine->sustain_diffusion_delay_l, delay_samples);
            d_r = ProcessSustainDiffusionSample(engine, d_r, engine->sustain_diffusion_delay_r, delay_samples);
            if (stages > 1) {
                d_l = ProcessSustainDiffusionSample(engine, d_l, engine->sustain_diffusion_delay2_l, delay_samples);
                d_r = ProcessSustainDiffusionSample(engine, d_r, engine->sustain_diffusion_delay2_r, delay_samples);
            }
            sustain_filtered_l = Lerp(sustain_filtered_l, d_l, Clamp01(engine->config.sustain_diffusion_amount));
            sustain_filtered_r = Lerp(sustain_filtered_r, d_r, Clamp01(engine->config.sustain_diffusion_amount));
        }

        // Final Output Mix (DSP core owns dry/wet policy)
        float attack_tilt = 1.0f;
        float sustain_tilt = 1.0f;
        if (engine->engine_state == ENGINE_STATE_TRANSIENT_BURST || engine->engine_state == ENGINE_STATE_ATTACK_ONGOING) {
            // Brighter onsets: emphasize HPF micro bus and slightly trim darker sustain bus.
            attack_tilt = 1.08f;
            sustain_tilt = 0.92f;
        } else if (engine->engine_state == ENGINE_STATE_SUSTAIN_BODY || engine->engine_state == ENGINE_STATE_SPARSE_DECAY) {
            // Later phrase stages: keep darker halo and avoid clicky forwardness.
            attack_tilt = 0.95f;
            sustain_tilt = 1.06f;
        }

        float wet_sum_l =
            (attack_filtered_l * attack_tilt * engine->class_gain_micro) +
            (bus_flat_l * engine->class_gain_short) +
            (sustain_filtered_l * sustain_tilt * engine->class_gain_sustain);
        float wet_sum_r =
            (attack_filtered_r * attack_tilt * engine->class_gain_micro) +
            (bus_flat_r * engine->class_gain_short) +
            (sustain_filtered_r * sustain_tilt * engine->class_gain_sustain);
        float wet_drive = fmaxf(0.1f, engine->config.wet_drive);
        float wet_clip_amt = Clamp01(engine->config.wet_clip_amount);
        float wet_trim = fmaxf(0.0f, engine->config.wet_output_trim);
        wet_sum_l = SoftClip(wet_sum_l * wet_drive, wet_clip_amt) * wet_trim;
        wet_sum_r = SoftClip(wet_sum_r * wet_drive, wet_clip_amt) * wet_trim;

        // Bounded Granular Feedback conditioning (M4B)
        // Equal-power mono fold-down with granular window compensation
        float feedback_mono = (wet_sum_l + wet_sum_r) * 0.70710678f;
        float fb_hp = Filter1Pole_ProcessHPF(&engine->feedback_hpf, feedback_mono);
        float fb_lp = Filter1Pole_ProcessLPF(&engine->feedback_lpf, fb_hp);
        float fb_sat = SoftClip(fb_lp, 0.30f);
        engine->feedback_sample = fb_sat;

        // Feedback energy follower (M4B)
        float fb_abs = fabsf(fb_sat);
        float e_coef = (fb_abs > engine->feedback_energy) ? engine->feedback_energy_att_coef : engine->feedback_energy_rel_coef;
        engine->feedback_energy += (fb_abs - engine->feedback_energy) * e_coef;

        float wet_gain = engine->smoothed_ducking_gain * engine->wet_presence_smoothed * engine->master_wet_gain;
        float wet_mix_l = wet_sum_l * wet_gain;
        float wet_mix_r = wet_sum_r * wet_gain;
        float dry_mix = dry_sample * engine->master_dry_gain;

        float final_l;
        float final_r;
        if (output_mode == BUBBLES_OUTPUT_SPATIAL) {
            final_l = wet_mix_l;
            final_r = wet_mix_r;
            if (out_dry != NULL) {
                out_dry[i] = dry_mix;
            }
        } else {
            final_l = dry_mix + wet_mix_l;
            final_r = dry_mix + wet_mix_r;
            float limiter_gain = ProcessFinalLimiterSample(engine, &final_l, &final_r);
            if (limiter_gain < engine->metrics_limiter_gain_min) engine->metrics_limiter_gain_min = limiter_gain;
        }

        float out_abs_l = fabsf(final_l);
        float out_abs_r = fabsf(final_r);
        if (out_abs_l > engine->metrics_peak_l_accum) engine->metrics_peak_l_accum = out_abs_l;
        if (out_abs_r > engine->metrics_peak_r_accum) engine->metrics_peak_r_accum = out_abs_r;

        out_left[i] = final_l;
        out_right[i] = final_r;

        if (engine->config.sustain_diffusion_enable) {
            engine->sustain_diffusion_write_idx++;
            if (engine->sustain_diffusion_write_idx >= BUBBLES_SUSTAIN_DIFFUSION_MAX_DELAY) {
                engine->sustain_diffusion_write_idx = 0;
            }
        }

        // Advance write pointer unless memory write is locked (M3)
        if (!write_locked) {
            engine->write_ptr = WrapIntIndex(engine->write_ptr + 1, buffer_size);
        }

        // Execute Control-Rate Tick
        if (++engine->block_counter >= BUBBLES_BLOCK_SIZE) {
            engine->block_counter = 0;
            engine->scheduler_tick++;
            Scheduler_ResetSpawnIdentity(engine);
            engine->metrics_tick_spawn_count = 0;
            UpdateStateAndDensity(engine, engine->block_peak_accum);
            UpdateSustainBusTone(engine);
            UpdateFeedbackTone(engine);
            UpdateFeedbackTarget(engine);
            Scheduler_RunTick(engine);

            engine->metrics_last_block.spawn_count = engine->metrics_tick_spawn_count;
            engine->metrics_last_block.active_voices = CountActiveVoices(engine);
            engine->metrics_last_block.engine_state = (int32_t)engine->engine_state;
            engine->metrics_last_block.ducking_gain = engine->smoothed_ducking_gain;
            engine->metrics_last_block.envelope = engine->env_follower_state;
            engine->metrics_last_block.peak_l = engine->metrics_peak_l_accum;
            engine->metrics_last_block.peak_r = engine->metrics_peak_r_accum;
            engine->metrics_last_block.clip_count = engine->metrics_clip_count_accum;
            engine->metrics_last_block.limiter_gain = engine->metrics_limiter_gain_min;
            engine->metrics_peak_l_accum = 0.0f;
            engine->metrics_peak_r_accum = 0.0f;
            engine->metrics_clip_count_accum = 0;
            engine->metrics_limiter_gain_min = engine->final_limiter_gain;
            if (engine->metrics_callback != NULL) {
                engine->metrics_callback(&engine->metrics_last_block, engine->metrics_user_data);
            }

            engine->block_peak_accum = 0.0f;
        }
    }
}

float SoundBubbles_ApplyFinalLimiter(SoundBubblesEngine_t* engine, float* out_left, float* out_right, int num_samples) {
    if (engine == NULL || out_left == NULL || out_right == NULL || num_samples <= 0) return 1.0f;

    float min_gain = 1.0f;
    CacheFinalLimiterBlockParams(engine);
    for (int i = 0; i < num_samples; i++) {
        float final_l = out_left[i];
        float final_r = out_right[i];
        float limiter_gain = ProcessFinalLimiterSample(engine, &final_l, &final_r);
        if (limiter_gain < min_gain) min_gain = limiter_gain;

        float out_abs_l = fabsf(final_l);
        float out_abs_r = fabsf(final_r);
        if (out_abs_l > engine->metrics_peak_l_accum) engine->metrics_peak_l_accum = out_abs_l;
        if (out_abs_r > engine->metrics_peak_r_accum) engine->metrics_peak_r_accum = out_abs_r;
        if (limiter_gain < engine->metrics_limiter_gain_min) engine->metrics_limiter_gain_min = limiter_gain;

        out_left[i] = final_l;
        out_right[i] = final_r;
    }
    return min_gain;
}

// --- Internal Helper Implementations ---

static void UpdateStateAndDensity(SoundBubblesEngine_t* engine, float block_abs_peak) {
    float prev_env = engine->env_follower_state;
    EngineState_t prev_state = engine->engine_state;

    // Update envelope
    engine->env_follower_state = UpdateEnvelope(prev_env, block_abs_peak, ENV_ATTACK_COEF, ENV_RELEASE_COEF);

    // Hard noise floor gating
    if (engine->env_follower_state < engine->config.noise_floor) {
        engine->env_follower_state = 0.0f;
    }

    engine->env_derivative = engine->env_follower_state - prev_env;

    // Transient Detection & State Transitions
    if (engine->env_derivative > engine->config.transient_delta) {
        engine->engine_state = ENGINE_STATE_TRANSIENT_BURST;
        engine->burst_timer_ticks = engine->config.burst_duration_ticks;
        engine->bloom_timer_ticks = PRESENCE_BLOOM_TICKS;
        Scheduler_SpawnImmediateBurst(engine);

        // M4A: Capture phrase anchor on transient onset
        engine->phrase_anchor_write_ptr = engine->write_ptr;
        engine->phrase_anchor_valid = true;
        engine->phrase_anchor_age = 0;
        engine->recent_phrase_active = 1;
        engine->auto_hold_state = AUTO_HOLD_IDLE;
        engine->auto_hold_target = 0.0f;

    } else if (engine->burst_timer_ticks > 0) {
        engine->burst_timer_ticks--;
        if (engine->engine_state == ENGINE_STATE_TRANSIENT_BURST && engine->burst_timer_ticks < (engine->config.burst_duration_ticks - 2)) {
            engine->engine_state = ENGINE_STATE_ATTACK_ONGOING;
        }
        engine->recent_phrase_active = 1;
        engine->auto_hold_state = AUTO_HOLD_IDLE;
        engine->auto_hold_target = 0.0f;
    } else {
        if (engine->env_follower_state > engine->config.sustain_thresh) {
            engine->engine_state = ENGINE_STATE_SUSTAIN_BODY;
            if (prev_state == ENGINE_STATE_SILENCE || prev_state == ENGINE_STATE_SPARSE_DECAY || prev_env < engine->config.tracking_thresh) {
                engine->phrase_anchor_write_ptr = engine->write_ptr;
                engine->phrase_anchor_valid = true;
                engine->phrase_anchor_age = 0;
            }
            engine->recent_phrase_active = 1;
            engine->auto_hold_state = AUTO_HOLD_IDLE;
            engine->auto_hold_target = 0.0f;
        } else {
            if (engine->env_follower_state > engine->config.tracking_thresh) {
                engine->engine_state = ENGINE_STATE_SPARSE_DECAY;
            } else if (engine->smoothed_freeze > 0.05f) {
                // Active freeze sustains the cloud across quiet input (M3)
                engine->engine_state = ENGINE_STATE_SUSTAIN_BODY;
            } else {
                engine->engine_state = ENGINE_STATE_SILENCE;
            }

            // M4A.1: Auto-Hold State Machine (single monotonic cycle per phrase)
            if (engine->recent_phrase_active && engine->phrase_anchor_valid) {
                switch (engine->auto_hold_state) {
                    case AUTO_HOLD_IDLE:
                        engine->auto_hold_state = AUTO_HOLD_ATTACK;
                        engine->auto_hold_target = 1.0f;
                        break;

                    case AUTO_HOLD_ATTACK:
                        if (engine->auto_hold_amount >= BUBBLES_AUTO_HOLD_ATTACK_PEAK) {
                            engine->auto_hold_state = AUTO_HOLD_RELEASE;
                            engine->auto_hold_target = 0.0f;
                        } else {
                            engine->auto_hold_target = 1.0f;
                        }
                        break;

                    case AUTO_HOLD_RELEASE:
                        engine->auto_hold_target = 0.0f;
                        if (engine->auto_hold_amount <= BUBBLES_AUTO_HOLD_THRESHOLD) {
                            engine->auto_hold_amount = 0.0f;
                            engine->auto_hold_target = 0.0f;
                            engine->auto_hold_state = AUTO_HOLD_IDLE;
                            engine->recent_phrase_active = 0;
                            engine->phrase_anchor_valid = false;
                        }
                        break;
                }
            } else {
                engine->auto_hold_target = 0.0f;
                engine->auto_hold_amount = 0.0f;
                engine->auto_hold_state = AUTO_HOLD_IDLE;
                engine->recent_phrase_active = 0;
                engine->phrase_anchor_valid = false;
            }
        }
    }

    // M4A: Track phrase anchor age
    if (engine->phrase_anchor_valid) {
        engine->phrase_anchor_age += BUBBLES_BLOCK_SIZE;
        float max_age_sec = engine->feedback_enabled ? 20.0f : 10.0f;
        float max_anchor_age_samples = max_age_sec * engine->config.sample_rate;
        if ((float)engine->phrase_anchor_age >= max_anchor_age_samples) {
            engine->phrase_anchor_valid = false;
        }
    }

    // M4A: Derive and smooth anchor mix (Section 11)
    float target_anchor_mix = 0.0f;
    if (engine->phrase_anchor_valid) {
        float state_weight = 0.0f;
        switch (engine->engine_state) {
            case ENGINE_STATE_TRANSIENT_BURST:
            case ENGINE_STATE_ATTACK_ONGOING:
                state_weight = 0.0f;
                break;
            case ENGINE_STATE_SUSTAIN_BODY:
                state_weight = 0.08f;
                break;
            case ENGINE_STATE_SPARSE_DECAY:
                state_weight = 0.40f;
                break;
            case ENGINE_STATE_SILENCE:
            default:
                state_weight = BUBBLES_ANCHOR_MAX_MIX;
                break;
        }
        float mem_mod = 0.85f + 0.30f * Clamp01(engine->config.memory_mix);
        float hold_mod = fmaxf(engine->auto_hold_amount, (engine->engine_state == ENGINE_STATE_SPARSE_DECAY ? 0.35f : 0.0f));
        target_anchor_mix = state_weight * hold_mod * mem_mod;
        target_anchor_mix = Clamp(target_anchor_mix, 0.0f, BUBBLES_ANCHOR_MAX_MIX);
    }
    engine->anchor_mix += (target_anchor_mix - engine->anchor_mix) * 0.15f;

    if (engine->bloom_timer_ticks > 0) {
        engine->bloom_timer_ticks--;
    }

    // Ducking target logic (driven primarily by transient/burst, recovers on sustain/decay)
    if (engine->engine_state == ENGINE_STATE_TRANSIENT_BURST || engine->engine_state == ENGINE_STATE_ATTACK_ONGOING) {
        engine->internal_ducking_target = engine->config.duck_burst_level;
        engine->ducking_lpf.b0 = engine->config.duck_attack_coef;
        engine->ducking_lpf.a1 = 1.0f - engine->config.duck_attack_coef;
    } else {
        // Recover ducking during sustain, decay, or silence
        engine->internal_ducking_target = 1.0f;
        engine->ducking_lpf.b0 = engine->config.duck_release_coef;
        engine->ducking_lpf.a1 = 1.0f - engine->config.duck_release_coef;
    }

    // Control-rate ducking smoothing
    engine->smoothed_ducking_gain = Filter1Pole_ProcessLPF(&engine->ducking_lpf, engine->internal_ducking_target);

    // Wet presence macro-shape across phrase phases:
    // - transient: preserve dry attack clarity
    // - post-attack bloom: brief intentional rise in wet audibility
    // - sustain: stable perceptible halo
    // - decay/silence: thinner but still audible tail
    switch (engine->engine_state) {
        case ENGINE_STATE_TRANSIENT_BURST:
            engine->wet_presence_target = 0.58f;
            break;
        case ENGINE_STATE_ATTACK_ONGOING:
        {
            float bloom_progress = 1.0f;
            if (PRESENCE_BLOOM_TICKS > 0) {
                bloom_progress = 1.0f - ((float)engine->bloom_timer_ticks / (float)PRESENCE_BLOOM_TICKS);
            }
            bloom_progress = Clamp01(bloom_progress);
            engine->wet_presence_target = Lerp(1.28f, 1.02f, bloom_progress);
            break;
        }
        case ENGINE_STATE_SUSTAIN_BODY:
            engine->wet_presence_target = 0.88f;
            break;
        case ENGINE_STATE_SPARSE_DECAY:
            engine->wet_presence_target = 0.54f;
            break;
        case ENGINE_STATE_SILENCE:
        default:
            if (engine->auto_hold_amount > BUBBLES_AUTO_HOLD_THRESHOLD) {
                float tail_pres = Lerp(0.32f, 0.65f, engine->auto_hold_amount);
                engine->wet_presence_target = Lerp(tail_pres, 0.90f, engine->smoothed_freeze);
            } else {
                engine->wet_presence_target = Lerp(0.32f, 0.90f, engine->smoothed_freeze);
            }
            break;
    }

    // Asymmetric smoothing keeps bloom responsive but avoids pumping on release.
    if (engine->wet_presence_target > engine->wet_presence_smoothed) {
        engine->wet_presence_lpf.b0 = 0.25f;
        engine->wet_presence_lpf.a1 = 0.75f;
    } else {
        engine->wet_presence_lpf.b0 = 0.08f;
        engine->wet_presence_lpf.a1 = 0.92f;
    }
    engine->wet_presence_smoothed = Filter1Pole_ProcessLPF(&engine->wet_presence_lpf, engine->wet_presence_target);

    // Target Density updates (Spawns per second), modulated by envelope and derivative within bounded state ranges.
    float env_norm = 0.0f;
    if (engine->config.sustain_thresh > engine->config.noise_floor) {
        env_norm = (engine->env_follower_state - engine->config.noise_floor) / (engine->config.sustain_thresh - engine->config.noise_floor);
    }
    env_norm = Clamp01(env_norm);

    float d_norm = 0.0f;
    if (engine->config.transient_delta > 1.0e-6f) {
        d_norm = engine->env_derivative / engine->config.transient_delta;
    }
    d_norm = Clamp(d_norm, -1.0f, 1.0f);

    switch (engine->engine_state) {
        case ENGINE_STATE_TRANSIENT_BURST:
        {
            float burst_max = engine->config.density_burst;
            float burst_min = burst_max * 0.7f;
            float burst_progress = 0.0f;
            if (engine->config.burst_duration_ticks > 0) {
                burst_progress = 1.0f - (float)engine->burst_timer_ticks / (float)engine->config.burst_duration_ticks;
            }
            burst_progress = Clamp01(burst_progress);
            float accent = Clamp01((1.0f - burst_progress) * 0.7f + Clamp01(d_norm) * 0.3f);
            engine->target_density = Lerp(burst_min, burst_max, accent);
            break;
        }
        case ENGINE_STATE_ATTACK_ONGOING:
        {
            float attack_max = engine->config.density_burst;
            float attack_min = attack_max * 0.55f;
            float attack_shape = Clamp01(0.65f * env_norm + 0.35f * Clamp01(d_norm));
            engine->target_density = Lerp(attack_min, attack_max, attack_shape);
            break;
        }
        case ENGINE_STATE_SUSTAIN_BODY:
        {
            float sustain_min = engine->config.density_sustain * 0.85f;
            float sustain_max = engine->config.density_sustain * 1.1f;
            float sustain_shape = Clamp01((env_norm - 0.4f) / 0.6f);
            float derivative_damp = 1.0f - 0.25f * fabsf(d_norm);
            sustain_shape *= Clamp01(derivative_damp);
            engine->target_density = Lerp(sustain_min, sustain_max, sustain_shape);
            break;
        }
        case ENGINE_STATE_SPARSE_DECAY:
        {
            float decay_max = engine->config.density_decay;
            float decay_min = decay_max * 0.1f;
            float tail = Clamp01((engine->env_follower_state - engine->config.tracking_thresh) /
                                 fmaxf(1.0e-6f, (engine->config.sustain_thresh - engine->config.tracking_thresh)));
            float release_emphasis = Clamp01(0.5f - 0.5f * d_norm);
            engine->target_density = Lerp(decay_min, decay_max, tail * release_emphasis);
            break;
        }
        case ENGINE_STATE_SILENCE:
        default:
            if (engine->auto_hold_amount > BUBBLES_AUTO_HOLD_THRESHOLD && engine->phrase_anchor_valid) {
                float base_density = engine->config.density_sustain;
                float ratio;
                if (engine->feedback_enabled) {
                    float mem_bloom = 0.5f * (Clamp01(engine->config.memory_mix) + Clamp01(engine->config.sustain_diffusion_amount));
                    ratio = Lerp(0.70f, 1.15f, mem_bloom);
                } else {
                    ratio = 0.35f;
                }
                engine->target_density = base_density * ratio * engine->auto_hold_amount;
            } else {
                engine->target_density = 0.0f;
            }
            break;
    }

    // Shimmer increases upper-octave cloud density in a bounded way while
    // preserving the existing scheduler cap.
    float shimmer = Clamp01(engine->config.shimmer_amount);
    if (engine->config.pitch_mode == BUBBLE_PITCH_MODE_SHIMMER && shimmer > 0.0f) {
        engine->target_density *= 1.0f + (0.5f * shimmer);
    }
}

static void Scheduler_SpawnImmediateBurst(SoundBubblesEngine_t* engine) {
    (void)Scheduler_SpawnBurstMode(engine, BUBBLES_SPAWN_SOURCE_BURST, engine->active_voice_limit);
}

static BubbleClass_t Scheduler_SelectClassForState(SoundBubblesEngine_t* engine, SharedSpawnId_t spawn_id) {
    // Class is an "important event": on attacks it is drawn from the shared
    // event stream so both stereo channels pick the same bubble type, while
    // during sustain/decay the channels become independent and the field opens.
    // The decision is keyed by the canonical logical spawn provenance, so an
    // extra spawn in one channel cannot shift the class of a later common spawn.
    float r = SharedSpawnRandom(engine, ResolveSpawnCoherence(engine), spawn_id, BUBBLES_SHARED_DECISION_CLASS);
    switch (engine->engine_state) {
        case ENGINE_STATE_TRANSIENT_BURST:
            return BUBBLE_CLASS_MICRO_ATTACK;
        case ENGINE_STATE_ATTACK_ONGOING:
            return (r < 0.8f) ? BUBBLE_CLASS_MICRO_ATTACK : BUBBLE_CLASS_SHORT_INTERMEDIATE;
        case ENGINE_STATE_SUSTAIN_BODY:
            return (r < 0.7f) ? BUBBLE_CLASS_SUSTAIN_BODY : BUBBLE_CLASS_SHORT_INTERMEDIATE;
        case ENGINE_STATE_SPARSE_DECAY:
            return BUBBLE_CLASS_SUSTAIN_BODY;
        case ENGINE_STATE_SILENCE:
        default:
            if (r < 0.05f) {
                return BUBBLE_CLASS_MICRO_ATTACK;
            } else if (r < 0.40f) {
                return BUBBLE_CLASS_SHORT_INTERMEDIATE;
            } else {
                return BUBBLE_CLASS_SUSTAIN_BODY;
            }
    }
}

static int Scheduler_SpawnBurstMode(SoundBubblesEngine_t* engine, BubbleSpawnSource_t source, int max_spawns) {
    int burst_count = engine->config.burst_immediate_count;
    if (burst_count < 1) burst_count = 1;
    if (burst_count > engine->active_voice_limit) burst_count = engine->active_voice_limit;
    if (max_spawns < burst_count) burst_count = max_spawns;

    int spawned = 0;
    switch (engine->config.burst_mode) {
        case BUBBLE_BURST_MODE_SPRAY:
        {
            // One canonical event index per invocation; child_index selects the
            // grain inside the burst. The class draw and every read decision in
            // Voice_SpawnInit reuse this exact provenance.
            SharedSpawnId_t base = Scheduler_NextSpawnId(engine, source);
            bool tail_mode = (engine->engine_state == ENGINE_STATE_SILENCE &&
                              engine->auto_hold_amount > BUBBLES_AUTO_HOLD_THRESHOLD &&
                              engine->phrase_anchor_valid);
            for (int i = 0; i < burst_count; i++) {
                SharedSpawnId_t id = base;
                id.child_index = (uint32_t)i;
                BubbleClass_t c = (!tail_mode && i == 0) ? BUBBLE_CLASS_MICRO_ATTACK : Scheduler_SelectClassForState(engine, id);
                if (Voice_RequestSpawn(engine, c, 0, id)) { engine->metrics_tick_spawn_count++; spawned++; }
            }
            break;
        }
        case BUBBLE_BURST_MODE_STRUM:
            if (engine->strum_pending_count < burst_count) engine->strum_pending_count = burst_count;
            break;
        case BUBBLE_BURST_MODE_SWARM:
        {
            int swarm_count = burst_count * 2;
            if (swarm_count < 4) swarm_count = 4;
            if (swarm_count > max_spawns) swarm_count = max_spawns;
            if (swarm_count > engine->active_voice_limit) swarm_count = engine->active_voice_limit;
            SharedSpawnId_t base = Scheduler_NextSpawnId(engine, source);
            for (int i = 0; i < swarm_count; i++) {
                SharedSpawnId_t id = base;
                id.child_index = (uint32_t)i;
                if (Voice_RequestSpawn(engine, Scheduler_SelectClassForState(engine, id), 0, id)) { engine->metrics_tick_spawn_count++; spawned++; }
            }
            break;
        }
        case BUBBLE_BURST_MODE_REVERSE_SWELL:
        {
            engine->force_reverse_spawns += burst_count;
            SharedSpawnId_t base = Scheduler_NextSpawnId(engine, source);
            for (int i = 0; i < burst_count; i++) {
                SharedSpawnId_t id = base;
                id.child_index = (uint32_t)i;
                BubbleClass_t c = (i == 0) ? BUBBLE_CLASS_SHORT_INTERMEDIATE : BUBBLE_CLASS_SUSTAIN_BODY;
                if (Voice_RequestSpawn(engine, c, 0, id)) { engine->metrics_tick_spawn_count++; spawned++; }
            }
            break;
        }
        case BUBBLE_BURST_MODE_SINGLE:
        default:
        {
            SharedSpawnId_t id = Scheduler_NextSpawnId(engine, source);
            if (Voice_RequestSpawn(engine, Scheduler_SelectClassForState(engine, id), 0, id)) { engine->metrics_tick_spawn_count++; spawned++; }
            break;
        }
    }
    return spawned;
}

static bool Scheduler_IsRhythmStepActive(const SoundBubblesEngine_t* engine, int32_t step_index) {
    uint32_t pattern = engine->config.rhythm_pattern & 0xFFFFu;
    if (pattern == 0u) pattern = 0x0001u;
    int bit = step_index % 16;
    if (bit < 0) bit += 16;
    return ((pattern >> bit) & 1u) != 0u;
}

static float Scheduler_TicksPerRhythmStep(const SoundBubblesEngine_t* engine) {
    float bpm = engine->config.tempo_bpm;
    if (!isfinite(bpm) || bpm < 20.0f) bpm = 20.0f;
    if (bpm > 300.0f) bpm = 300.0f;
    float steps_per_quarter = 4.0f;
    switch (engine->config.rhythm_division) {
        case BUBBLE_RHYTHM_DIVISION_QUARTER: steps_per_quarter = 1.0f; break;
        case BUBBLE_RHYTHM_DIVISION_EIGHTH: steps_per_quarter = 2.0f; break;
        case BUBBLE_RHYTHM_DIVISION_THIRTY_SECOND: steps_per_quarter = 8.0f; break;
        case BUBBLE_RHYTHM_DIVISION_SIXTEENTH:
        default: steps_per_quarter = 4.0f; break;
    }
    float ticks_per_second = engine->config.sample_rate / (float)BUBBLES_BLOCK_SIZE;
    float steps_per_second = (bpm / 60.0f) * steps_per_quarter;
    return ticks_per_second / fmaxf(steps_per_second, 1.0e-6f);
}

static void Scheduler_RunTick(SoundBubblesEngine_t* engine) {
    int spawns_this_tick = Voice_FlushPendingSpawns(engine, SCHED_MAX_SPAWNS_PER_TICK);

    if (engine->strum_pending_count > 0 && spawns_this_tick < SCHED_MAX_SPAWNS_PER_TICK) {
        BubbleClass_t c = (engine->strum_step_index % 3 == 0) ? BUBBLE_CLASS_MICRO_ATTACK :
                          ((engine->strum_step_index % 3 == 1) ? BUBBLE_CLASS_SHORT_INTERMEDIATE : BUBBLE_CLASS_SUSTAIN_BODY);
        // Voice_RequestSpawn returns true only for an immediate spawn; it queues a
        // pending spawn when the pool is full and returns false, and returns false
        // without queueing when the pending queue is also full. Only consume the
        // strum event when it can be either spawned now or safely queued so strum
        // events are never silently dropped under saturation.
        int inactive_idx = Voice_FindInactiveSlot(engine);
        bool can_place = (inactive_idx >= 0) || (engine->pending_spawn_count < BUBBLES_PENDING_SPAWN_CAPACITY);
        if (can_place) {
            SharedSpawnId_t id = Scheduler_NextSpawnId(engine, BUBBLES_SPAWN_SOURCE_STRUM);
            if (Voice_RequestSpawn(engine, c, 0, id)) { engine->metrics_tick_spawn_count++; spawns_this_tick++; }
            engine->strum_pending_count--;
            engine->strum_step_index++;
        }
    }

    if (engine->engine_state == ENGINE_STATE_SILENCE) {
        if (!(engine->auto_hold_amount > BUBBLES_AUTO_HOLD_THRESHOLD && engine->phrase_anchor_valid)) {
            engine->spawn_accumulator = 0.0f;
            return;
        }
    }

    if (engine->config.tempo_sync_enabled) {
        float ticks_per_step = Scheduler_TicksPerRhythmStep(engine);
        engine->rhythm_step_accumulator += 1.0f / ticks_per_step;
        while (engine->rhythm_step_accumulator >= 1.0f) {
            if (Scheduler_IsRhythmStepActive(engine, engine->rhythm_step_index) && spawns_this_tick < SCHED_MAX_SPAWNS_PER_TICK) {
                spawns_this_tick += Scheduler_SpawnBurstMode(engine, BUBBLES_SPAWN_SOURCE_RHYTHM, SCHED_MAX_SPAWNS_PER_TICK - spawns_this_tick);
            }
            engine->rhythm_step_index++;
            engine->rhythm_step_accumulator -= 1.0f;
        }
        return;
    }

    float spawns_per_tick = engine->target_density * ((float)BUBBLES_BLOCK_SIZE / engine->config.sample_rate);
    engine->spawn_accumulator += spawns_per_tick;

    while (engine->spawn_accumulator >= 1.0f && spawns_this_tick < SCHED_MAX_SPAWNS_PER_TICK) {
        int spawned = Scheduler_SpawnBurstMode(engine, BUBBLES_SPAWN_SOURCE_DENSITY, SCHED_MAX_SPAWNS_PER_TICK - spawns_this_tick);
        engine->spawn_accumulator -= 1.0f;
        spawns_this_tick += spawned;
        if (spawned <= 0 && engine->config.burst_mode != BUBBLE_BURST_MODE_STRUM) {
            break;
        }
    }

    if (engine->spawn_accumulator > 1.0f) {
        engine->spawn_accumulator = 1.0f;
    }
}

static int Voice_FindInactiveSlot(SoundBubblesEngine_t* engine) {
    for (int i = 0; i < engine->active_voice_limit; i++) {
        if (engine->voices[i].state == VOICE_STATE_INACTIVE) return i;
    }

    return -1;
}

static int Voice_Allocate(SoundBubblesEngine_t* engine) {
    // 1. Return an inactive slot immediately if available. Inactive slots are the
    // only allocation result that callers may initialize immediately.
    int inactive_idx = Voice_FindInactiveSlot(engine);
    if (inactive_idx >= 0) {
        return inactive_idx;
    }

    // 2. Stealing policy when fully occupied (deterministic, bounded, musically protective).
    //    Matches ENGINE_BEHAVIOR_SPEC.md section 6 / SOUND_BUBBLES_V1_BASELINE_SPEC.md rule 1-3:
    //      - Protect young micro-attacks: never steal a MICRO voice with phase < 0.5.
    //      - Prefer victims by class rank SUSTAIN (0) -> SHORT (1) -> MICRO (2).
    //      - Inside a class, steal the oldest voice (highest phase), i.e. the lowest
    //        remaining musical contribution.
    //      - If every voice is a protected young micro-attack, deterministically fall
    //        back to the oldest global candidate so a spawn request can still queue.
    //      - Tie-breaks are resolved by lower voice index for fixed-seed reproducibility.
    //
    // Active victims are never returned as ready-to-write slots. They are moved into
    // PREEMPT_FADING and keep their read pointer/phase fields intact until the fade
    // reaches INACTIVE, at which point queued spawns can consume the slot on a later tick.
    int best_protected_idx = -1;
    int best_fallback_idx = -1;
    int best_protected_rank = 99;
    int best_fallback_rank = 99;
    float best_protected_score = -1.0f;
    float best_fallback_score = -1.0f;

    for (int i = 0; i < engine->active_voice_limit; i++) {
        BubbleVoice_t* v = &engine->voices[i];
        if (v->state != VOICE_STATE_PLAYING) {
            continue;
        }

        int class_rank = 2; // MICRO is the most protected victim class by default
        if (v->bubble_class == BUBBLE_CLASS_SUSTAIN_BODY) {
            class_rank = 0;
        } else if (v->bubble_class == BUBBLE_CLASS_SHORT_INTERMEDIATE) {
            class_rank = 1;
        }

        float remaining_contrib = (1.0f - Clamp01(v->phase)) * Clamp01(v->amp);
        float usefulness_score = Clamp01(v->phase) + (1.0f - Clamp01(remaining_contrib));
        bool protected_micro = (v->bubble_class == BUBBLE_CLASS_MICRO_ATTACK)
            && (v->phase < STEAL_MICRO_PROTECT_PHASE);

        bool better_than_best_protected =
            (class_rank < best_protected_rank) ||
            (class_rank == best_protected_rank &&
             (usefulness_score > best_protected_score ||
              (usefulness_score == best_protected_score &&
               (best_protected_idx < 0 || i < best_protected_idx))));

        if (!protected_micro && better_than_best_protected) {
            best_protected_rank = class_rank;
            best_protected_score = usefulness_score;
            best_protected_idx = i;
        }

        bool better_than_best_fallback =
            (class_rank < best_fallback_rank) ||
            (class_rank == best_fallback_rank &&
             (usefulness_score > best_fallback_score ||
              (usefulness_score == best_fallback_score &&
               (best_fallback_idx < 0 || i < best_fallback_idx))));

        if (better_than_best_fallback) {
            best_fallback_rank = class_rank;
            best_fallback_score = usefulness_score;
            best_fallback_idx = i;
        }
    }

    int victim_idx = (best_protected_idx >= 0) ? best_protected_idx : best_fallback_idx;
    if (victim_idx >= 0) {
        BubbleVoice_t* victim = &engine->voices[victim_idx];
        victim->state = VOICE_STATE_PREEMPT_FADING;
        victim->fade_counter = engine->fade_samples;
    }

    return -1;
}

static bool Voice_QueuePendingSpawn(SoundBubblesEngine_t* engine, BubbleClass_t b_class, int generation, SharedSpawnId_t spawn_id) {
    if (engine->pending_spawn_count >= BUBBLES_PENDING_SPAWN_CAPACITY) {
        return false;
    }

    int tail = (engine->pending_spawn_head + engine->pending_spawn_count) % BUBBLES_PENDING_SPAWN_CAPACITY;
    engine->pending_spawns[tail].bubble_class = b_class;
    engine->pending_spawns[tail].generation = (uint8_t)((generation <= 0) ? 0 : 1);
    // Preserve the full canonical identity captured when the request was made; no
    // field (including the origin tick) is recomputed when the saturated request
    // finally becomes a voice.
    engine->pending_spawns[tail].spawn_id = spawn_id;
    engine->pending_spawn_count++;
    return true;
}

static int32_t Voice_CountFadingVoices(const SoundBubblesEngine_t* engine) {
    int32_t fading_count = 0;

    for (int i = 0; i < engine->active_voice_limit; i++) {
        if (engine->voices[i].state == VOICE_STATE_PREEMPT_FADING) {
            fading_count++;
        }
    }

    return fading_count;
}

static int Voice_FlushPendingSpawns(SoundBubblesEngine_t* engine, int max_spawns) {
    int spawned_count = 0;

    while (engine->pending_spawn_count > 0 && spawned_count < max_spawns) {
        int voice_idx = Voice_FindInactiveSlot(engine);
        if (voice_idx < 0) {
            break;
        }

        PendingSpawn_t request = engine->pending_spawns[engine->pending_spawn_head];
        engine->pending_spawn_head = (engine->pending_spawn_head + 1) % BUBBLES_PENDING_SPAWN_CAPACITY;
        engine->pending_spawn_count--;

        Voice_SpawnInit(engine, voice_idx, request.bubble_class, request.generation, request.spawn_id);
        engine->metrics_tick_spawn_count++;
        spawned_count++;
    }

    if (engine->pending_spawn_count == 0) {
        engine->pending_spawn_head = 0;
    }

    return spawned_count;
}

static bool Voice_RequestSpawn(SoundBubblesEngine_t* engine, BubbleClass_t b_class, int generation, SharedSpawnId_t spawn_id) {
    int inactive_idx = Voice_FindInactiveSlot(engine);
    if (inactive_idx >= 0) {
        Voice_SpawnInit(engine, inactive_idx, b_class, generation, spawn_id);
        return true;
    }

    if (engine->pending_spawn_count >= BUBBLES_PENDING_SPAWN_CAPACITY) {
        return false;
    }

    // Only preempt a new victim when currently fading voices cannot already cover
    // all queued spawns plus this new request. Existing PREEMPT_FADING voices are
    // guaranteed future inactive slots, so reusing their pending capacity avoids
    // unnecessary polyphony loss.
    int32_t fading_count = Voice_CountFadingVoices(engine);
    if (fading_count <= engine->pending_spawn_count) {
        (void)Voice_Allocate(engine);
    }

    Voice_QueuePendingSpawn(engine, b_class, generation, spawn_id);
    return false;
}

// Worst-case accumulated single-precision rounding of `read_ptr_float` over the
// grain life. `read_ptr_float` adds `rate` once per sample; each addition rounds
// by at most half an ulp of a value below `buffer_size`. Bounding that drift keeps
// the spawn clamp valid against the real float pointer, not just ideal math.
static float GuardFloatDriftMargin(int32_t buffer_size, float duration_samples) {
    const float half_ulp_bound = 0.5f * ((float)buffer_size + 4.0f) * 1.1920929e-7f; // ~2^-23
    return duration_samples * half_ulp_bound;
}

// Clamp a spawn read offset so the whole predicted grain path stays clear of the
// directional guard band. The forward protection is based on the relative
// catch-up rate (rate - 1), not the absolute playback rate, so a tiny positive
// microdetune around rate 1.0 only shifts the read by roughly the few samples it
// actually gains on the write head instead of a full duration.
static int32_t ClampSpawnOffsetForGuard(int32_t read_offset_samples, float rate, float duration_samples, int32_t buffer_size) {
    const int32_t guard = BUBBLES_GUARD_ZONE_SAMPLES;
    const int32_t max_guarded_offset = buffer_size - guard - 1;
    if (read_offset_samples < guard) read_offset_samples = guard;
    if (read_offset_samples > max_guarded_offset) read_offset_samples = max_guarded_offset;

    // |read - write| changes per sample by: (rate - 1) forward pitch-up,
    // (1 + |rate|) reverse. rate <= 1 forward never closes the gap.
    float span_per_sample;
    if (rate < 0.0f) {
        span_per_sample = 1.0f + fabsf(rate);
    } else if (rate > 1.0f) {
        span_per_sample = rate - 1.0f;
    } else {
        span_per_sample = 0.0f;
    }

    float predicted_span = ceilf(duration_samples * span_per_sample
                                 + GuardFloatDriftMargin(buffer_size, duration_samples));
    if (!(predicted_span > 0.0f)) predicted_span = 0.0f;
    predicted_span += (float)BUBBLES_GUARD_PATH_MARGIN;
    if (predicted_span > (float)buffer_size) predicted_span = (float)buffer_size;
    const int32_t projected_span = (int32_t)predicted_span;

    if (rate < 0.0f) {
        int32_t reverse_max = max_guarded_offset - projected_span;
        if (reverse_max < guard) reverse_max = guard;
        if (read_offset_samples > reverse_max) read_offset_samples = reverse_max;
    } else if (rate > 1.0f) {
        int32_t forward_min = guard + projected_span;
        if (forward_min > max_guarded_offset) forward_min = max_guarded_offset;
        if (read_offset_samples < forward_min) read_offset_samples = forward_min;
    }
    return read_offset_samples;
}

static void Voice_SpawnInit(SoundBubblesEngine_t* engine, int voice_idx, BubbleClass_t b_class, int generation, SharedSpawnId_t spawn_id) {
    BubbleVoice_t* v = &engine->voices[voice_idx];
    BubbleClassConfig_t* class_cfg = &engine->config.class_configs[b_class];

    v->state = VOICE_STATE_PLAYING;
    v->bubble_class = b_class;
    v->generation = (uint8_t)((generation <= 0) ? 0 : 1);
    v->spawn_id = spawn_id;
    v->phase = 0.0f;
    v->amp = 1.0f;
    v->quantized_rate = ResolvePitchModeRate(engine);
    v->gain = 1.0f;

    // M2: spawn-time decisions are split between the shared coherence stream
    // (event alignment) and the per-channel stream (spatial nuance). Attacks
    // share more so transients stay coherent; sustain/decay/freeze open up.
    float coherence = ResolveSpawnCoherence(engine);

    float duration_ms = class_cfg->duration_ms_min +
        (RandomFloat01(engine) * (class_cfg->duration_ms_max - class_cfg->duration_ms_min));
    if (v->generation == 1) {
        duration_ms *= Clamp(engine->config.droplet_length_scale, 0.2f, 1.0f);
        v->gain *= Clamp(engine->config.droplet_gain, 0.0f, 1.0f);
    }
    float duration_samples = duration_ms * (engine->config.sample_rate / 1000.0f);

    // Defensively clamp duration_samples to avoid div-by-zero or extremely rapid phase_inc
    if (duration_samples < 10.0f) {
        duration_samples = 10.0f;
    }
    v->phase_inc = 1.0f / duration_samples;

    ReadRegionChoice_t region_choice = ResolveReadRegionChoice(engine, b_class, engine->engine_state, spawn_id);
    v->memory_tier = region_choice.memory_tier;
    int32_t read_offset_samples = ChooseReadOffsetSamples(engine, region_choice.region, region_choice.recent_bias, coherence, spawn_id);

    // Context-conditioned reverse (M2): rare on attacks, opening through sustain,
    // decay and freeze so the cloud can unfurl backwards after the event.
    if (engine->force_reverse_spawns > 0) {
        v->read_direction = 1u;
        engine->force_reverse_spawns--;
    } else {
        float reverse_probability = ResolveContextReverseProbability(engine, b_class, region_choice.region_id);
        v->read_direction = (RandomFloat01(engine) < reverse_probability) ? 1u : 0u;
    }
    // Attack-only playback jitter is resolved before the fixed microdetune so it
    // is already part of the pitch decision the guard path is based on.
    if (engine->config.attack_rate_jitter && b_class == BUBBLE_CLASS_MICRO_ATTACK) {
        float d = Clamp(engine->config.attack_rate_jitter_depth, 0.0f, 0.2f);
        float j = (RandomFloat01(engine) * 2.0f - 1.0f) * d;
        v->quantized_rate = Clamp(v->quantized_rate + j, 0.25f, 4.0f);
    }

    // M2: fixed per-grain microdetune decided once at birth and held for life.
    // Resolved before the guard so the guard path always reflects the exact
    // playback rate the grain will use, microdetune included.
    v->microdetune_cents = ResolveMicroDetuneCents(engine, b_class);
    if (v->microdetune_cents != 0.0f) {
        float detune_ratio = powf(2.0f, v->microdetune_cents / 1200.0f);
        v->quantized_rate = Clamp(v->quantized_rate * detune_ratio, 0.25f, 4.0f);
    }

    // Final signed playback rate actually used by the grain.
    v->rate = (v->read_direction != 0u) ? -v->quantized_rate : v->quantized_rate;

    // Clamp spawn offsets so reverse and forward-pitch reads keep a guard-banded
    // path through the ring buffer for the whole predicted grain lifetime. The
    // clamp is derived from the real relative travel (rate - 1 forward, 1 + |rate|
    // reverse) so a few cents of microdetune around rate 1.0 cannot shove the read
    // into a distant region. Runtime guards still handle pathological parameter
    // updates and long frozen reads.
    int32_t buffer_size = (int32_t)SoundBubbles_RequiredBufferSamples(engine->config.sample_rate);

    // M2.2: Smart Start proposes a musically smoother nearby offset *before* the
    // guard clamp, so the clamp always runs on the final candidate. This prevents
    // Smart Start from shifting a safe offset back inside the forbidden band for
    // the grain's real (microdetuned, jittered) rate.
    if (engine->config.smart_start_enable) {
        read_offset_samples = RefineReadOffsetSmartStart(engine, read_offset_samples, engine->config.smart_start_range, buffer_size);
    }
    read_offset_samples = ClampSpawnOffsetForGuard(read_offset_samples, v->rate, duration_samples, buffer_size);
    v->read_ptr_float = (float)WrapIntIndex(engine->write_ptr - read_offset_samples, buffer_size);
    // M3.2C: remember the guard-clamped offset so a delayed grain can re-derive
    // its read pointer against the write head that exists at its real onset.
    v->spawn_read_offset = read_offset_samples;

    float spread = (b_class == BUBBLE_CLASS_MICRO_ATTACK) ? engine->config.attack_pan_spread : engine->config.sustain_pan_spread;
    float pan = (RandomFloat01(engine) * 2.0f - 1.0f) * Clamp01(spread) * Clamp01(engine->config.stereo_width);
    float pan_pos = Clamp((pan + 1.0f) * 0.5f, 0.0f, 1.0f);
    float theta = pan_pos * (0.5f * M_PI);
    v->pan_l = cosf(theta);
    v->pan_r = sinf(theta);

    int env_count = (engine->config.envelope_variation > 0.001f) ? 3 : 1;
    v->envelope_variant = (uint8_t)(NextRandomU32(engine) % (uint32_t)env_count);

    int tone_count = (engine->config.tone_variation > 0.001f) ? 3 : 1;
    v->tone_profile = (uint8_t)(NextRandomU32(engine) % (uint32_t)tone_count);
    if (v->tone_profile == 0) {
        v->gain *= (b_class == BUBBLE_CLASS_MICRO_ATTACK) ? Clamp(engine->config.attack_brightness, 0.3f, 1.8f) : 1.0f;
    } else if (v->tone_profile == 2) {
        v->gain *= (b_class == BUBBLE_CLASS_SUSTAIN_BODY) ? Clamp(1.0f - engine->config.sustain_darkness, 0.2f, 1.0f) : 0.92f;
    }

    v->source_region_id = region_choice.region_id;
    if (region_choice.region_id == 2) {
        v->gain *= Clamp(1.0f - 0.6f * engine->config.memory_darkening, 0.2f, 1.0f);
    }

    // Optional hard-limited 2nd-generation droplet (single child, no recursive chain).
    if (engine->config.droplet_enable && v->generation == 0 && b_class == BUBBLE_CLASS_MICRO_ATTACK) {
        int active = CountActiveVoices(engine);
        float occupancy = (float)active / (float)engine->active_voice_limit;
        if (occupancy < DROPLET_OCCUPANCY_DISABLE) {
            float prob = Clamp01(engine->config.droplet_probability);
            if (occupancy > DROPLET_OCCUPANCY_REDUCE) {
                prob *= 0.35f;
            }
            if (RandomFloat01(engine) < prob) {
                // Second-generation grain: its shared identity derives from the
                // parent's *full* canonical provenance plus its generation, so an
                // extra droplet never consumes or shifts a primary event index and
                // can never collide with the parent or a sibling.
                SharedSpawnId_t child_id = SpawnDerivedId(spawn_id, 1u);
                if (Voice_RequestSpawn(engine, BUBBLE_CLASS_SHORT_INTERMEDIATE, 1, child_id)) {
                    engine->metrics_tick_spawn_count++;
                }
            }
        }
    }

    // M3.2C: every musical decision above was already made at allocation time,
    // so the intra-tick onset delay only defers the *audible start*. A grain with
    // a non-zero delay is allocated but held silent in PENDING_ONSET; the render
    // loop re-derives its read pointer from the then-current write head when the
    // delay expires. RHYTHM and STRUM keep a zero delay (see
    // ResolveOnsetDelaySamples) so tempo grids and strum patterns stay exact.
    uint32_t onset_delay = ResolveOnsetDelaySamples(engine, spawn_id);
#if defined(BUBBLES_M3_ONSET_TRACE)
    if (g_bubble_onset_trace_fn != NULL) {
        g_bubble_onset_trace_fn(g_bubble_onset_trace_user, engine, spawn_id, onset_delay);
    }
#endif
    if (onset_delay > 0u) {
        v->state = VOICE_STATE_PENDING_ONSET;
        v->onset_delay_samples = (int16_t)onset_delay;
    } else {
        v->state = VOICE_STATE_PLAYING;
        v->onset_delay_samples = 0;
    }
}

static ReadRegionChoice_t ResolveReadRegionChoice(SoundBubblesEngine_t* engine, BubbleClass_t bubble_class, EngineState_t engine_state, SharedSpawnId_t spawn_id) {
    // Deterministic map from "what bubble" + "what phrase phase" => temporal memory slice.
    // Attack-oriented contexts read from attack/body. Tail-oriented contexts read from memory.
    // M2 annotates the slice with a temporal tier/recent bias; ChooseReadOffsetSamples then
    // applies the non-uniform, recent-weighted distribution inside that region.
    ReadRegionChoice_t choice;
    choice.memory_tier = BUBBLES_MEMORY_TIER_RECENT;
    choice.recent_bias = 0.60f;

    // Consume the shared region/tier decision first, for every class. Micro
    // attacks always read the attack region, but consuming the event-addressable
    // shared decision regardless of class keeps L/R aligned on the same logical
    // event even when the two channels are in phrase states that pick different
    // bubble classes during a temporary divergence.
    float roll = SharedSpawnRandom(engine, ResolveSpawnCoherence(engine), spawn_id, BUBBLES_SHARED_DECISION_REGION_TIER);

    if (bubble_class == BUBBLE_CLASS_MICRO_ATTACK) {
        choice.region = &engine->config.attack_region;
        choice.region_id = 0;
        return choice;
    }

    // M2 temporal depth curve. Recent body material dominates (~60%), medium
    // body is still common (~25%) and deep memory is a rarer ghost (~15%). The
    // MEMORY macro/state modulate the deep share, but a recent floor keeps the
    // cloud connected to the phrase. The tier roll is shared between channels
    // (via SharedSpawnRandom) so stereo attacks stay aligned while sustain
    // opens up.
    float memory_mix = Clamp01(engine->config.memory_mix);
    float memory_pull = Clamp01(engine->config.memory_pull);
    float deep_weight = BUBBLES_MEMORY_WEIGHT_DEEP + 0.30f * memory_mix + 0.10f * memory_pull;
    float recent_weight = BUBBLES_MEMORY_WEIGHT_RECENT - 0.18f * memory_mix;

    if (engine_state == ENGINE_STATE_TRANSIENT_BURST || engine_state == ENGINE_STATE_ATTACK_ONGOING) {
        deep_weight *= 0.30f;
        recent_weight += 0.12f;
    } else if (engine_state == ENGINE_STATE_SPARSE_DECAY) {
        deep_weight += 0.10f;
    }
    float f = engine->smoothed_freeze;
    deep_weight += 0.18f * f;

    if (recent_weight < 0.35f) recent_weight = 0.35f;
    if (deep_weight < 0.02f) deep_weight = 0.02f;
    if (deep_weight > BUBBLES_MEMORY_DEEP_MAX_SHARE) deep_weight = BUBBLES_MEMORY_DEEP_MAX_SHARE;
    float medium_weight = 1.0f - recent_weight - deep_weight;
    if (medium_weight < 0.08f) {
        medium_weight = 0.08f;
        recent_weight = 1.0f - medium_weight - deep_weight;
        if (recent_weight < 0.30f) recent_weight = 0.30f;
    }

    if (roll < recent_weight) {
        choice.region = &engine->config.body_region;
        choice.region_id = 1;
        choice.memory_tier = BUBBLES_MEMORY_TIER_RECENT;
        choice.recent_bias = 0.78f;
    } else if (roll < recent_weight + medium_weight) {
        choice.region = &engine->config.body_region;
        choice.region_id = 1;
        choice.memory_tier = BUBBLES_MEMORY_TIER_MEDIUM;
        choice.recent_bias = 0.45f;
    } else {
        choice.region = &engine->config.memory_region;
        choice.region_id = 2;
        choice.memory_tier = BUBBLES_MEMORY_TIER_DEEP;
        choice.recent_bias = 0.38f;
    }

    return choice;
}

// M2 stereo coherence amount for a spawn, driven by the (shared) phrase state:
// attacks stay almost identical between channels, while sustain/decay/freeze
// progressively open into a wide field. Using state rather than class keeps the
// class draw itself coherent on attacks and independent on tails.
static float ResolveSpawnCoherence(SoundBubblesEngine_t* engine) {
    float coherence;
    switch (engine->engine_state) {
        case ENGINE_STATE_TRANSIENT_BURST: coherence = SPAWN_COHERENCE_ATTACK; break;
        case ENGINE_STATE_ATTACK_ONGOING: coherence = SPAWN_COHERENCE_ATTACK * 0.9f; break;
        case ENGINE_STATE_SUSTAIN_BODY: coherence = SPAWN_COHERENCE_SUSTAIN; break;
        case ENGINE_STATE_SPARSE_DECAY: coherence = SPAWN_COHERENCE_SHORT * 0.5f; break;
        default: coherence = 0.5f; break;
    }
    float f = engine->smoothed_freeze;
    float target_coherence = fminf(coherence, 0.12f);
    coherence = Lerp(coherence, target_coherence, f);
    return Clamp(coherence, 0.0f, 1.0f);
}

// M2 context-conditioned reverse. The authored reverse_probability stays the
// ceiling; phrase phase scales it so articulation is never smeared but the tail
// can progressively unfurl backwards.
static float ResolveContextReverseProbability(SoundBubblesEngine_t* engine, BubbleClass_t bubble_class, uint8_t region_id) {
    float base = Clamp01(engine->config.reverse_probability);
    if (base <= 0.0f) {
        return 0.0f;
    }
    float scale;
    switch (engine->engine_state) {
        case ENGINE_STATE_TRANSIENT_BURST: scale = REVERSE_SCALE_TRANSIENT; break;
        case ENGINE_STATE_ATTACK_ONGOING: scale = REVERSE_SCALE_ATTACK; break;
        case ENGINE_STATE_SUSTAIN_BODY: scale = REVERSE_SCALE_SUSTAIN; break;
        case ENGINE_STATE_SPARSE_DECAY: scale = REVERSE_SCALE_DECAY; break;
        case ENGINE_STATE_SILENCE:
        default: scale = REVERSE_SCALE_SILENCE; break;
    }
    if (bubble_class == BUBBLE_CLASS_MICRO_ATTACK) {
        scale *= 0.6f;
    }
    float f = engine->smoothed_freeze;
    float target_scale = fmaxf(scale, REVERSE_SCALE_FREEZE);
    scale = Lerp(scale, target_scale, f);
    if (region_id == 2) {
        scale *= 1.25f;
    }
    return Clamp01(base * scale);
}

// M2 fixed per-grain microdetune in cents, drawn at birth and held. Attack grains
// stay nearly pure; sustain bodies thicken; freeze accepts the most ensemble.
static float ResolveMicroDetuneCents(SoundBubblesEngine_t* engine, BubbleClass_t bubble_class) {
    float base_max_cents;
    switch (bubble_class) {
        case BUBBLE_CLASS_MICRO_ATTACK: base_max_cents = BUBBLES_MICRODETUNE_ATTACK_CENTS; break;
        case BUBBLE_CLASS_SHORT_INTERMEDIATE: base_max_cents = BUBBLES_MICRODETUNE_SHORT_CENTS; break;
        case BUBBLE_CLASS_SUSTAIN_BODY: base_max_cents = BUBBLES_MICRODETUNE_SUSTAIN_CENTS; break;
        default: base_max_cents = BUBBLES_MICRODETUNE_SHORT_CENTS; break;
    }
    float f = engine->smoothed_freeze;
    float max_cents = Lerp(base_max_cents, BUBBLES_MICRODETUNE_FREEZE_CENTS, f);
    // Envelope variation scales ensemble thickness between 60% and 100%.
    max_cents *= 0.6f + 0.4f * Clamp01(engine->config.envelope_variation);
    return (RandomFloat01(engine) * 2.0f - 1.0f) * max_cents;
}

static int32_t ChooseReadOffsetSamples(SoundBubblesEngine_t* engine, const ReadRegionConfig_t* region, float recent_bias, float coherence, SharedSpawnId_t spawn_id) {
    // Clamp and normalize range so presets stay ring-buffer safe.
    const int32_t min_safe = BUBBLES_GUARD_ZONE_SAMPLES;
    int32_t buffer_size = (int32_t)SoundBubbles_RequiredBufferSamples(engine->config.sample_rate);
    const int32_t max_safe = buffer_size - BUBBLES_GUARD_ZONE_SAMPLES - 1;

    // Region bounds are authored as 44.1 kHz reference samples; convert to the
    // engine's actual sample rate so the musical time span is rate invariant.
    int32_t min_offset = SoundBubbles_ReferenceSamplesToSamples(region->min_offset_samples, engine->config.sample_rate);
    int32_t max_offset = SoundBubbles_ReferenceSamplesToSamples(region->max_offset_samples, engine->config.sample_rate);

    if (min_offset < min_safe) min_offset = min_safe;
    if (max_offset < min_safe) max_offset = min_safe;
    if (min_offset > max_safe) min_offset = max_safe;
    if (max_offset > max_safe) max_offset = max_safe;
    if (max_offset < min_offset) max_offset = min_offset;

    int32_t span = max_offset - min_offset;
    if (span == 0) {
        return min_offset;
    }

    // M2: non-uniform temporal selection. The region span is split into
    // recent / medium / deep thirds. Recent material dominates and deep tails are
    // rare, so most grains stay near the phrase while occasional ghosts reach
    // back. The band choice is shared between channels; the in-band position is
    // per-channel, preserving read-offset differences for width.
    float w_recent = BUBBLES_MEMORY_WEIGHT_RECENT + 0.30f * (recent_bias - 0.5f);
    float w_deep = BUBBLES_MEMORY_WEIGHT_DEEP - 0.12f * (recent_bias - 0.5f);
    if (w_recent < 0.34f) w_recent = 0.34f;
    if (w_deep < 0.02f) w_deep = 0.02f;
    float w_medium = 1.0f - w_recent - w_deep;
    if (w_medium < 0.05f) {
        w_medium = 0.05f;
        w_recent = 1.0f - w_medium - w_deep;
    }

    float band_lo;
    float band_hi;
    float tier_roll = SharedSpawnRandom(engine, coherence, spawn_id, BUBBLES_SHARED_DECISION_OFFSET_BAND);
    if (tier_roll < w_recent) {
        band_lo = 0.0f;
        band_hi = 1.0f / 3.0f;
    } else if (tier_roll < w_recent + w_medium) {
        band_lo = 1.0f / 3.0f;
        band_hi = 2.0f / 3.0f;
    } else {
        band_lo = 2.0f / 3.0f;
        band_hi = 1.0f;
    }

    float u = RandomFloat01(engine);
    float t = band_lo + (band_hi - band_lo) * u;
    int32_t offset = min_offset + (int32_t)(t * (float)span);
    if (offset < min_offset) offset = min_offset;
    if (offset > max_offset) offset = max_offset;

    // M4A: Phrase Anchor reading during decay/tail (Section 10-12)
    // With mixture probability anchor_mix (up to 0.60), grains sample near the phrase anchor.
    if (engine->phrase_anchor_valid && engine->anchor_mix > 0.001f) {
        float mix = Clamp(engine->anchor_mix, 0.0f, BUBBLES_ANCHOR_MAX_MIX);
        float anchor_roll = RandomFloat01(engine);
        if (anchor_roll < mix) {
            int32_t anchor_distance = engine->write_ptr - engine->phrase_anchor_write_ptr;
            if (anchor_distance < 0) anchor_distance += buffer_size;

            float mem_macro = Clamp01(engine->config.memory_mix);
            float spread_sec = 0.050f + 0.150f * mem_macro;
            int32_t spread_samples = (int32_t)(spread_sec * engine->config.sample_rate);
            if (spread_samples < 32) spread_samples = 32;

            float anchor_u = RandomFloat01(engine);
            int32_t candidate_anchor_offset = anchor_distance - (int32_t)(anchor_u * (float)spread_samples);
            if (candidate_anchor_offset < min_safe) candidate_anchor_offset = min_safe;
            if (candidate_anchor_offset > max_safe) candidate_anchor_offset = max_safe;
            offset = candidate_anchor_offset;
        }
    }

    return offset;
}

static int32_t RefineReadOffsetSmartStart(const SoundBubblesEngine_t* engine, int32_t read_offset_samples, int32_t range, int32_t buffer_size) {
    int32_t best = read_offset_samples;
    int32_t scan = range;
    if (scan < 1) return best;
    if (scan > 64) scan = 64;

    float best_energy = FLT_MAX;
    for (int32_t delta = -scan; delta <= scan; delta++) {
        int32_t candidate = read_offset_samples + delta;
        if (candidate < BUBBLES_GUARD_ZONE_SAMPLES) continue;
        if (candidate >= (buffer_size - BUBBLES_GUARD_ZONE_SAMPLES)) continue;
        int32_t idx = WrapIntIndex(engine->write_ptr - candidate, buffer_size);
        int32_t prev = WrapIntIndex(idx - 1, buffer_size);
        float a = Ring_ReadNormalizedSample(engine->delay_buffer, prev);
        float b = Ring_ReadNormalizedSample(engine->delay_buffer, idx);
        if ((a <= 0.0f && b >= 0.0f) || (a >= 0.0f && b <= 0.0f)) {
            return candidate;
        }

        float energy = 0.0f;
        for (int k = -SMART_START_ENERGY_RADIUS; k <= SMART_START_ENERGY_RADIUS; k++) {
            int32_t eidx = WrapIntIndex(idx + k, buffer_size);
            float s = Ring_ReadNormalizedSample(engine->delay_buffer, eidx);
            energy += s * s;
        }
        if (energy < best_energy) {
            best_energy = energy;
            best = candidate;
        }
    }
    return best;
}

// --- Mathematics and Filter Helpers ---

static void InitWindowLUTs(void) {
    if (luts_initialized) return;

    for (int i = 0; i < 1024; i++) {
        float phase = (float)i / 1023.0f;
        // Hann Window: 0.5 * (1 - cos(2*pi*phase))
        WindowLUT_Hann[i] = 0.5f * (1.0f - cosf(2.0f * M_PI * phase));

        // Tukey-like Window: flat top, cosine tapers
        float alpha = 0.2f; // 20% taper each side
        if (phase < alpha) {
            WindowLUT_Tukey[i] = 0.5f * (1.0f - cosf(M_PI * phase / alpha));
        } else if (phase > (1.0f - alpha)) {
            WindowLUT_Tukey[i] = 0.5f * (1.0f - cosf(M_PI * (1.0f - phase) / alpha));
        } else {
            WindowLUT_Tukey[i] = 1.0f;
        }
    }
    luts_initialized = true;
}

// Tiny deterministic xorshift32 PRNG.
static uint32_t NextRandomU32(SoundBubblesEngine_t* engine) {
    uint32_t x = engine->rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    engine->rng_state = x;
    return x;
}

// Convert to deterministic float in [0, 1) by mapping top 24 bits into mantissa range.
static float RandomFloat01(SoundBubblesEngine_t* engine) {
    const float kInv24Bit = 1.0f / 16777216.0f; // 2^24
    uint32_t rnd = NextRandomU32(engine);
    return (float)(rnd >> 8) * kInv24Bit;
}

// Stateless shared-event hash (M2.2). A cheap 32-bit integer mix (xorshift plus
// odd multipliers) turns the logical event identity into an independent unit
// value. It is O(1), allocation-free and lock-free, and uses no mutable shared
// stream state, so the number of spawns either channel executed earlier cannot
// shift the shared decision of a later logical event.
static uint32_t SharedEventMix32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

// Hash the full canonical event provenance into a unit value. The event index is
// split from the child index so a burst invocation and its grains, or a parent
// and its droplet, never alias. `lane` separates the share roll from the
// candidate value for the same (event, kind).
static float SharedEventUnit(uint32_t seed, SharedSpawnId_t id, uint32_t kind, uint32_t lane) {
    const float kInv24Bit = 1.0f / 16777216.0f; // 2^24
    uint32_t h = SharedEventMix32(seed ^ 0x9E3779B9u);
    h ^= SharedEventMix32(id.tick + 0x85EBCA6Bu);
    h += SharedEventMix32(id.source + 0xC2B2AE35u);
    h ^= SharedEventMix32(id.event_index + 0x27D4EB2Fu);
    h += SharedEventMix32(id.child_index + 0x165667B1u);
    h ^= SharedEventMix32(kind + 0xD6E8FEB8u);
    h += SharedEventMix32(lane + 0x9E3779B1u);
    return (float)(SharedEventMix32(h) >> 8) * kInv24Bit;
}

// M3.2C intra-tick onset delay. The scheduler decides spawns at control rate, so
// without this every grain chosen on the same control tick would begin on the
// same sample (machine-gun / quantised timing). This derives a deterministic
// sub-tick onset from the canonical SharedSpawnId through a dedicated hash
// namespace and spreads it across the following BUBBLES_BLOCK_SIZE-sample window.
//
// Properties:
//  - Pure function of (shared_event_seed, tick, source, event_index,
//    child_index). It does not touch the per-channel sequential RNG, so pitch,
//    microdetune, reverse, pan, memory tier and spawn count are unchanged.
//  - Stereo-coherent: both channel engines share the same undecorrelated
//    shared_event_seed, so the same canonical event gets the same delay on L and
//    R; attacks never become a stereo flam.
//  - RHYTHM and STRUM return 0, keeping tempo grids and strum patterns
//    sample-exact. DENSITY, BURST and DROPLET (derived child identity) jitter.
//  - Block-size independent: the window is the internal DSP tick, never the
//    host `num_samples`.
//  - Sample-rate dependent by design: the tick is fixed in samples, so the
//    spread is ~0.73 ms at 44.1 kHz and ~0.33 ms at 96 kHz. That is intentional
//    (breaking sub-ms simultaneity only); multi-ms jitter is out of scope.
static uint32_t ResolveOnsetDelaySamples(SoundBubblesEngine_t* engine, SharedSpawnId_t spawn_id) {
    if (spawn_id.source == (uint32_t)BUBBLES_SPAWN_SOURCE_RHYTHM
        || spawn_id.source == (uint32_t)BUBBLES_SPAWN_SOURCE_STRUM) {
        return 0u;
    }
    // A request materialized from the saturation queue on a later tick has
    // already missed its scheduled onset, so it must start immediately rather
    // than wait a full tick again. Same-tick spawns always see tick equality.
    if (spawn_id.tick != engine->scheduler_tick) {
        return 0u;
    }
    float unit = SharedEventUnit(engine->shared_event_seed, spawn_id,
                                 BUBBLES_SHARED_KIND_ONSET, BUBBLES_SHARED_LANE_ONSET);
    uint32_t span = (uint32_t)BUBBLES_BLOCK_SIZE;
    uint32_t delay = (uint32_t)(unit * (float)span);
    if (delay >= span) delay = span - 1u;
    return delay;
}

// Reset the per-source event indices at the control-tick boundary.
static void Scheduler_ResetSpawnIdentity(SoundBubblesEngine_t* engine) {
    for (int i = 0; i < BUBBLES_SPAWN_SOURCE_COUNT; i++) {
        engine->spawn_source_event_index[i] = 0u;
    }
}

// Reserve the next canonical event identity for a given source in the current
// control tick. Each source owns an independent index sequence, so an extra
// top-level spawn of one source can never shift the identity of a common event
// of another source. The returned identity is carried through the voice request
// and the saturation queue unchanged.
static SharedSpawnId_t Scheduler_NextSpawnId(SoundBubblesEngine_t* engine, BubbleSpawnSource_t source) {
    SharedSpawnId_t id;
    uint32_t s = (uint32_t)source;
    if (s >= (uint32_t)BUBBLES_SPAWN_SOURCE_COUNT) s = 0u;
    id.tick = engine->scheduler_tick;
    id.source = s;
    id.event_index = engine->spawn_source_event_index[s]++;
    id.child_index = 0u;
    return id;
}

// Derive a stable identity for a second-generation (droplet) grain from its
// parent's full canonical provenance and its generation. The child lives in its
// own DROPLET source namespace and folds the parent's source and event index into
// its event index, so no two parents (even across sources) can alias. The derived
// generation lives in its own child-index namespace (high bit set) so the child
// can never collide with the parent, with a primary burst child, or with a
// sibling droplet, and it never consumes or shifts a primary event index.
static SharedSpawnId_t SpawnDerivedId(SharedSpawnId_t parent_id, uint32_t generation) {
    SharedSpawnId_t child_id;
    child_id.tick = parent_id.tick;
    child_id.source = (uint32_t)BUBBLES_SPAWN_SOURCE_DROPLET;
    child_id.event_index = parent_id.event_index * (uint32_t)BUBBLES_SPAWN_SOURCE_COUNT + parent_id.source;
    child_id.child_index = BUBBLES_SPAWN_CHILD_DERIVED_FLAG
        | ((parent_id.child_index & BUBBLES_SPAWN_CHILD_PARENT_MASK) << BUBBLES_SPAWN_CHILD_PARENT_SHIFT)
        | (generation & BUBBLES_SPAWN_CHILD_GENERATION_MASK);
    return child_id;
}

// Optional white-box observation hook for the M2 regression harness. Compiled
// out entirely in production builds (no state, no branch), so it cannot affect
// the audio callback or the deterministic sequence.
#if defined(BUBBLES_M2_SHARED_TRACE)
typedef void (*BubbleSharedTraceFn)(void* user, const SoundBubblesEngine_t* engine,
                                    SharedSpawnId_t id, uint32_t kind,
                                    int shared_branch, float value);
static BubbleSharedTraceFn g_bubble_shared_trace_fn = NULL;
static void* g_bubble_shared_trace_user = NULL;
static void BubblesTest_SetSharedTrace(BubbleSharedTraceFn fn, void* user) {
    g_bubble_shared_trace_fn = fn;
    g_bubble_shared_trace_user = user;
}
#endif

// With probability `coherence` the value comes from the event-addressable shared
// decision; otherwise it is drawn from this channel's own sequential stream.
// Both the share roll and the candidate shared value derive from the same full
// canonical event provenance (seed, tick, source, event_index, child_index,
// kind). The helper is pure with respect to the event identity: it never advances
// a counter and never depends on how many shared decisions either channel
// consumed before it, so an extra spawn cannot shift the shared value of a later
// logical event. The per-channel stream still decorrelates the channel-local
// fallback (pan, fine offset, pitch, detune, duration, reverse).
static float SharedSpawnRandom(SoundBubblesEngine_t* engine, float coherence, SharedSpawnId_t spawn_id, BubbleSharedDecisionKind_t kind) {
    const uint32_t k = (uint32_t)kind;
    const float roll = SharedEventUnit(engine->shared_event_seed, spawn_id, k, BUBBLES_SHARED_LANE_ROLL);
    const int shared_branch = (roll < coherence) ? 1 : 0;
    const float value = shared_branch
        ? SharedEventUnit(engine->shared_event_seed, spawn_id, k, BUBBLES_SHARED_LANE_VALUE)
        : RandomFloat01(engine);
#if defined(BUBBLES_M2_SHARED_TRACE)
    if (g_bubble_shared_trace_fn != NULL) {
        g_bubble_shared_trace_fn(g_bubble_shared_trace_user, engine, spawn_id, k, shared_branch, value);
    }
#endif
    return value;
}


static int32_t CountActiveVoices(const SoundBubblesEngine_t* engine) {
    int32_t count = 0;
    for (int i = 0; i < engine->active_voice_limit; i++) {
        if (engine->voices[i].state != VOICE_STATE_INACTIVE) {
            count++;
        }
    }
    return count;
}

static inline int32_t WrapIntIndex(int32_t index, int32_t size) {
    while (index >= size) index -= size;
    while (index < 0) index += size;
    return index;
}

static inline float WrapFloatIndex(float index, float size) {
    if (index >= 0.0f && index < size) {
        return index;
    }
    if (!isfinite(index) || !isfinite(size) || size <= 0.0f) {
        return 0.0f;
    }

    index = fmodf(index, size);
    if (index < 0.0f) {
        index += size;
    }
    if (!(index >= 0.0f && index < size)) {
        return 0.0f;
    }
    return index;
}

static inline float LinearInterpolate(const BubbleRingSample_t* buffer, float index_float, int32_t buffer_size) {
    int32_t idx_int = (int32_t)index_float;
    float frac = index_float - (float)idx_int;
    int32_t idx_next = (idx_int + 1 == buffer_size) ? 0 : idx_int + 1;

    float val1 = Ring_ReadNormalizedSample(buffer, idx_int);
    float val2 = Ring_ReadNormalizedSample(buffer, idx_next);
    return val1 + frac * (val2 - val1);
}

// M3.2B: quality-profile -> read-position interpolator selection. MCU profiles
// stay on the cheap linear path; the WEB profiles get cubic Hermite.
static inline BubbleInterpolationMode_t ResolveInterpolationMode(BubbleQualityProfile profile) {
    switch (profile) {
        case BUBBLE_QUALITY_PROFILE_WEB_STANDARD:
        case BUBBLE_QUALITY_PROFILE_WEB_ULTRA:
            return BUBBLES_INTERPOLATION_HERMITE;
        case BUBBLE_QUALITY_PROFILE_MCU_SAFE:
        case BUBBLE_QUALITY_PROFILE_MCU_PLUS:
        default:
            return BUBBLES_INTERPOLATION_LINEAR;
    }
}

// M3.2B: 4-point cubic Hermite (Catmull-Rom) interpolation around the fractional
// read position. Uses the four ring-buffer samples xm1/x0/x1/x2, is exact for
// constants and linear ramps, needs no allocation and no powf/trig, and wraps
// correctly at both buffer edges. Samples are scaled to [-1, 1] like the linear
// path so voice gain/normalisation stays unchanged.
//
//   a = -0.5*xm1 + 1.5*x0 - 1.5*x1 + 0.5*x2
//   b =        xm1 - 2.5*x0 + 2.0*x1 - 0.5*x2
//   c = -0.5*xm1        + 0.5*x1
//   d =        x0
//   y(t) = ((a*t + b)*t + c)*t + d
static inline float Hermite4Interpolate(const BubbleRingSample_t* buffer, float index_float, int32_t buffer_size) {
    int32_t idx = (int32_t)index_float;
    float frac = index_float - (float)idx;

    int32_t im1 = (idx > 0) ? idx - 1 : buffer_size - 1;
    int32_t ip1 = (idx + 1 < buffer_size) ? idx + 1 : 0;
    int32_t ip2 = (idx + 2 < buffer_size) ? idx + 2 : idx + 2 - buffer_size;

    float xm1 = Ring_ReadNormalizedSample(buffer, im1);
    float x0  = Ring_ReadNormalizedSample(buffer, idx);
    float x1  = Ring_ReadNormalizedSample(buffer, ip1);
    float x2  = Ring_ReadNormalizedSample(buffer, ip2);

    float a = -0.5f * xm1 + 1.5f * x0 - 1.5f * x1 + 0.5f * x2;
    float b =        xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    float c = -0.5f * xm1 + 0.5f * x1;
    float d = x0;
    return ((a * frac + b) * frac + c) * frac + d;
}

// Dispatch on the cached profile-derived mode. One well-predicted branch per
// voice-sample; the path counters exist only in test/telemetry builds.
static inline float InterpolateSample(SoundBubblesEngine_t* engine, const BubbleRingSample_t* buffer, float index_float, int32_t buffer_size) {
#if defined(BUBBLES_INTERPOLATION_TELEMETRY) || defined(BUBBLES_BUILD_PROCESSOR_TESTS)
    if (engine->interpolation_mode == BUBBLES_INTERPOLATION_HERMITE) {
        engine->interpolation_hermite_samples++;
        return Hermite4Interpolate(buffer, index_float, buffer_size);
    }
    engine->interpolation_linear_samples++;
    return LinearInterpolate(buffer, index_float, buffer_size);
#else
    if (engine->interpolation_mode == BUBBLES_INTERPOLATION_HERMITE) {
        return Hermite4Interpolate(buffer, index_float, buffer_size);
    }
    return LinearInterpolate(buffer, index_float, buffer_size);
#endif
}

BubbleInterpolationMode_t SoundBubbles_InterpolationModeForProfile(BubbleQualityProfile profile) {
    return ResolveInterpolationMode(profile);
}

BubbleInterpolationMode_t SoundBubbles_GetInterpolationMode(const SoundBubblesEngine_t* engine) {
    if (engine == NULL) return BUBBLES_INTERPOLATION_LINEAR;
    return engine->interpolation_mode;
}

#if defined(BUBBLES_INTERPOLATION_TELEMETRY) || defined(BUBBLES_BUILD_PROCESSOR_TESTS)
void SoundBubbles_GetInterpolationCallCounts(const SoundBubblesEngine_t* engine,
                                             uint64_t* out_linear_samples,
                                             uint64_t* out_hermite_samples) {
    if (engine == NULL) return;
    if (out_linear_samples != NULL) *out_linear_samples = engine->interpolation_linear_samples;
    if (out_hermite_samples != NULL) *out_hermite_samples = engine->interpolation_hermite_samples;
}
#endif

static inline bool CheckGuardZoneDirectional(int32_t write_ptr, float read_ptr_float, float rate, int32_t buffer_size) {
    int32_t read_ptr = (int32_t)read_ptr_float;
    if (rate >= 0.0f) {
        int32_t dist_behind_write = write_ptr - read_ptr;
        if (dist_behind_write < 0) dist_behind_write += buffer_size;
        return (dist_behind_write >= 0 && dist_behind_write < BUBBLES_GUARD_ZONE_SAMPLES);
    } else {
        int32_t dist_ahead_of_write = read_ptr - write_ptr;
        if (dist_ahead_of_write < 0) dist_ahead_of_write += buffer_size;
        return (dist_ahead_of_write >= 0 && dist_ahead_of_write < BUBBLES_GUARD_ZONE_SAMPLES);
    }
}

static float ResolvePitchModeRate(SoundBubblesEngine_t* engine) {
    switch (engine->config.pitch_mode) {
        case BUBBLE_PITCH_MODE_OCTAVE_UP:
            return BUBBLES_PITCH_RATIO_OCTAVE_UP;
        case BUBBLE_PITCH_MODE_OCTAVE_DOWN:
            return BUBBLES_PITCH_RATIO_OCTAVE_DOWN;
        case BUBBLE_PITCH_MODE_FIFTH:
            // Tempered (12-TET) fifth, not the just 3/2 ratio.
            return BUBBLES_PITCH_RATIO_FIFTH_12TET;
        case BUBBLE_PITCH_MODE_SHIMMER:
        {
            // Weighted harmonic cloud. Unison stays the anchor and the more
            // extreme intervals are progressively rarer, so high Sparkle adds
            // depth without turning every grain into an obvious pitch shift.
            float shimmer = Clamp01(engine->config.shimmer_amount);
            if (shimmer <= 0.0f) return BUBBLES_PITCH_RATIO_UNISON;
            float w_unison = 1.0f - 0.55f * shimmer;
            float w_octave = 0.28f * shimmer;
            float w_fifth = 0.18f * shimmer;
            float roll = RandomFloat01(engine);
            if (roll < w_unison) return BUBBLES_PITCH_RATIO_UNISON;
            if (roll < w_unison + w_octave) return BUBBLES_PITCH_RATIO_OCTAVE_UP;
            if (roll < w_unison + w_octave + w_fifth) return BUBBLES_PITCH_RATIO_FIFTH_12TET;
            return BUBBLES_PITCH_RATIO_OCTAVE_FIFTH;
        }
        case BUBBLE_PITCH_MODE_UNISON:
        default:
            return BUBBLES_PITCH_RATIO_UNISON;
    }
}

static float UpdateEnvelope(float prev_state, float input_peak, float attack_coef, float release_coef) {
    if (input_peak > prev_state) {
        return prev_state + attack_coef * (input_peak - prev_state);
    } else {
        return prev_state + release_coef * (input_peak - prev_state);
    }
}

// Basic 1-pole Lowpass coefficient calculation (explicit exponential approximation)
static void CalculateFilterCoeffsLPF(Filter1Pole_t* f, float cutoff_hz, float sample_rate) {
    float a1 = expf(-2.0f * M_PI * cutoff_hz / sample_rate);

    f->a1 = a1;
    f->b0 = 1.0f - a1;
    f->z1 = 0.0f;
}

// Recompute the 1-pole lowpass coefficients while preserving z1. Unlike
// CalculateFilterCoeffsLPF() (init only), this is used by the control-rate tone
// update, so the filter state must never be reset: that would click.
static void UpdateFilterCoeffsLPF(Filter1Pole_t* f, float cutoff_hz, float sample_rate) {
    float a1 = expf(-2.0f * M_PI * cutoff_hz / sample_rate);
    f->a1 = a1;
    f->b0 = 1.0f - a1;
}

// Sustain bus target cutoff, driven by phrase state and the resolved tonal
// darkness (WARMTH is folded into sustain_darkness by the macro map, and CLARITY
// also acts through it). Attacks open the sustain bus, decay closes it, and high
// darkness pulls it darker.
static float ResolveSustainLpfTargetHz(const SoundBubblesEngine_t* engine) {
    float state_open;
    switch (engine->engine_state) {
        case ENGINE_STATE_TRANSIENT_BURST: state_open = SUSTAIN_LPF_OPEN_TRANSIENT; break;
        case ENGINE_STATE_ATTACK_ONGOING:  state_open = SUSTAIN_LPF_OPEN_ATTACK; break;
        case ENGINE_STATE_SUSTAIN_BODY:    state_open = SUSTAIN_LPF_OPEN_SUSTAIN; break;
        case ENGINE_STATE_SPARSE_DECAY:    state_open = SUSTAIN_LPF_OPEN_DECAY; break;
        case ENGINE_STATE_SILENCE:
        default:                           state_open = SUSTAIN_LPF_OPEN_SILENCE; break;
    }
    float darkness = Clamp01(engine->config.sustain_darkness);
    float openness = state_open * Lerp(1.0f, 1.0f - SUSTAIN_LPF_WARMTH_DARKEN, darkness);
    return SUSTAIN_LPF_MIN_HZ + (SUSTAIN_LPF_MAX_HZ - SUSTAIN_LPF_MIN_HZ) * Clamp01(openness);
}

// Control-rate sustain LPF tone update. No per-sample or per-voice expf(): the
// cutoff is slewed once per control tick and the coefficients are refreshed only
// when the smoothed cutoff actually moved.
static void UpdateSustainBusTone(SoundBubblesEngine_t* engine) {
    float target = ResolveSustainLpfTargetHz(engine);
    float current = engine->sustain_lpf_cutoff_smoothed_hz;
    current += (target - current) * engine->sustain_lpf_smooth_coef;
    if (fabsf(current - engine->sustain_lpf_applied_hz) >= SUSTAIN_LPF_COEFF_EPSILON_HZ) {
        UpdateFilterCoeffsLPF(&engine->sustain_lpf_l, current, engine->config.sample_rate);
        UpdateFilterCoeffsLPF(&engine->sustain_lpf_r, current, engine->config.sample_rate);
        engine->sustain_lpf_applied_hz = current;
    }
    engine->sustain_lpf_cutoff_smoothed_hz = current;
}

static void UpdateFeedbackTone(SoundBubblesEngine_t* engine) {
    float warmth = Clamp01(engine->config.wet_clip_amount * 1.8f);
    float mem_dark = Clamp01(engine->config.memory_darkening);
    float sus_dark = Clamp01(engine->config.sustain_darkness);
    float darkness_factor = 0.40f * sus_dark + 0.35f * mem_dark + 0.25f * warmth;
    float target_cutoff = Lerp(8500.0f, 4200.0f, darkness_factor);

    // Progressive darkening during decay/silence tail context
    if (engine->engine_state == ENGINE_STATE_SPARSE_DECAY || engine->engine_state == ENGINE_STATE_SILENCE) {
        target_cutoff *= 0.85f;
    }
    target_cutoff = Clamp(target_cutoff, 3600.0f, 9000.0f);
    if (fabsf(target_cutoff - engine->feedback_lpf_cutoff_hz) > 20.0f) {
        engine->feedback_lpf_cutoff_hz = target_cutoff;
        CalculateFilterCoeffsLPF(&engine->feedback_lpf, engine->feedback_lpf_cutoff_hz, engine->config.sample_rate);
    }
}

static void UpdateFeedbackTarget(SoundBubblesEngine_t* engine) {
    if (!engine->feedback_enabled) {
        engine->feedback_gain_target = 0.0f;
        return;
    }

    float state_base = 0.0f;
    // Feedback active strictly in tail context (Section 4):
    // Prioritize Auto-Hold ATTACK/RELEASE, SPARSE_DECAY, and SILENCE tail.
    // During active phrase (Auto-Hold IDLE and active engine state), feedback is zero
    // to preserve attack clarity and prevent continuous regeneration during the phrase.
    bool in_tail_context = (engine->auto_hold_state != AUTO_HOLD_IDLE) ||
                           (engine->engine_state == ENGINE_STATE_SPARSE_DECAY) ||
                           (engine->engine_state == ENGINE_STATE_SILENCE);

    if (in_tail_context) {
        switch (engine->engine_state) {
            case ENGINE_STATE_SPARSE_DECAY:
                state_base = 0.35f;
                break;
            case ENGINE_STATE_SILENCE:
            default:
                if (engine->auto_hold_amount > BUBBLES_AUTO_HOLD_THRESHOLD && engine->phrase_anchor_valid) {
                    float hold_norm = (engine->auto_hold_amount - BUBBLES_AUTO_HOLD_THRESHOLD) /
                                      (1.0f - BUBBLES_AUTO_HOLD_THRESHOLD);
                    hold_norm = Clamp01(hold_norm);
                    // Silence tail: 0.50 to 0.65 depending on Auto-Hold envelope
                    state_base = Lerp(0.50f, 0.65f, hold_norm);
                } else if (engine->auto_hold_state == AUTO_HOLD_ATTACK) {
                    state_base = 0.10f;
                } else if (engine->auto_hold_state == AUTO_HOLD_RELEASE) {
                    state_base = 0.25f;
                } else {
                    state_base = 0.0f;
                }
                break;
        }
    } else {
        state_base = 0.0f;
    }

    float mem = Clamp01(engine->config.memory_mix);
    float bloom = Clamp01(engine->config.sustain_diffusion_amount);
    float macro_factor = (0.85f + 0.30f * mem) * (0.85f + 0.30f * bloom);

    float nominal = state_base * macro_factor;

    // Safety gain reduction based on energy follower with smooth knee (M4B.1)
    // Canonical threshold: BUBBLES_FEEDBACK_ENERGY_SAFETY_TH
    float safety_th = (engine->feedback_safety_threshold > 0.0f) ? engine->feedback_safety_threshold : BUBBLES_FEEDBACK_ENERGY_SAFETY_TH;
    float knee_width = 0.25f * safety_th;
    float t_low = safety_th - knee_width;
    if (t_low < 0.01f) t_low = 0.01f;
    float t_high = safety_th + knee_width;

    float energy_safety = 1.0f;
    if (engine->feedback_energy <= t_low) {
        energy_safety = 1.0f;
    } else if (engine->feedback_energy >= t_high) {
        energy_safety = safety_th / engine->feedback_energy;
    } else {
        float x = engine->feedback_energy - t_low;
        float quad = (x * x) / (4.0f * knee_width * engine->feedback_energy);
        energy_safety = Clamp01(1.0f - quad);
    }
    nominal *= energy_safety;

    engine->feedback_gain_target = Clamp(nominal, 0.0f, BUBBLES_FEEDBACK_MAX_GAIN);
}

static inline float Filter1Pole_ProcessLPF(Filter1Pole_t* f, float input) {
    f->z1 = (input * f->b0) + (f->z1 * f->a1);
    return f->z1;
}

// Attack HPF formulated correctly as: HPF(x) = x - LPF(x)
static inline float Filter1Pole_ProcessHPF(Filter1Pole_t* f, float input) {
    float lpf_out = Filter1Pole_ProcessLPF(f, input);
    return input - lpf_out;
}

static float EnvelopeVariantGain(float phase, uint8_t variant, int family) {
    float p = Clamp01(phase);
    if (variant == 0) return 1.0f;
    if (family == ENVELOPE_FAMILY_SOFT) {
        // "Soft" arch without trig in the audio loop: 4p(1-p) in [0, 1].
        float arch = 4.0f * p * (1.0f - p);
        return (variant == 1) ? (0.85f + 0.15f * arch) : (0.75f + 0.25f * (1.0f - p));
    }
    return (variant == 1) ? (0.92f + 0.08f * (1.0f - p)) : (0.85f + 0.15f * p);
}

static float SoftClip(float x, float amount) {
    float a = Clamp01(amount);
    float x_c = Clamp(x, -1.0f, 1.0f);
    float cubic = x_c - 0.3333333f * x_c * x_c * x_c;
    return Lerp(x, cubic, a);
}


static float ProcessSustainDiffusionSample(SoundBubblesEngine_t* engine, float in, float* delay_line, int delay_samples) {
    int read_idx = engine->sustain_diffusion_write_idx - delay_samples;
    if (read_idx < 0) read_idx += BUBBLES_SUSTAIN_DIFFUSION_MAX_DELAY;
    float delayed = delay_line[read_idx];
    float g = Clamp(engine->config.sustain_diffusion_feedback, 0.0f, 0.95f);
    float y = -g * in + delayed;
    delay_line[engine->sustain_diffusion_write_idx] = in + g * y;
    return y;
}


uint32_t SoundBubbles_MotionHash(uint32_t state) {
    state ^= state >> 16;
    state *= 0x7feb352du;
    state ^= state >> 15;
    state *= 0x846ca68bu;
    state ^= state >> 16;
    return state;
}

float SoundBubbles_MotionHashToBipolar(uint32_t state) {
    uint32_t mantissa = (SoundBubbles_MotionHash(state) >> 8) & 0x00FFFFFFu;
    return ((float)mantissa * (1.0f / 8388607.5f)) - 1.0f;
}

static void MotionResetLfo(BubbleMotionLfoState_t* lfo, uint32_t seed) {
    if (lfo == NULL) return;
    lfo->hold_state = SoundBubbles_MotionHash(seed);
    lfo->phase = (float)((lfo->hold_state >> 8) & 0x00FFFFFFu) * (1.0f / 16777216.0f);
    lfo->value = SoundBubbles_MotionHashToBipolar(lfo->hold_state ^ BUBBLE_MOTION_INITIAL_VALUE_SEED_XOR);
}


static int32_t ClampActiveVoiceLimit(int32_t requested_limit) {
    if (requested_limit < 1) return 1;
    if (requested_limit > BUBBLE_ENGINE_MAX_VOICES) return BUBBLE_ENGINE_MAX_VOICES;
    return requested_limit;
}

static int32_t ResolveProfileVoiceLimit(BubbleQualityProfile profile) {
    for (int i = 0; i < BUBBLE_QUALITY_PROFILE_COUNT; i++) {
        if (BUBBLE_QUALITY_PROFILE_LIMITS[i].profile == profile) {
            return BUBBLE_QUALITY_PROFILE_LIMITS[i].voice_limit;
        }
    }
    return BUBBLE_QUALITY_DEFAULT_VOICE_LIMIT;
}

static void DeactivateVoicesAboveActiveLimit(SoundBubblesEngine_t* engine) {
    for (int i = engine->active_voice_limit; i < BUBBLES_MAX_VOICES; i++) {
        BubbleVoice_t* voice = &engine->voices[i];
        if (voice->state == VOICE_STATE_INACTIVE) continue;
        voice->state = VOICE_STATE_PREEMPT_FADING;
        if (voice->fade_counter <= 0 || voice->fade_counter > engine->fade_samples) {
            voice->fade_counter = engine->fade_samples;
        }
    }
}

static void ApplyQualityTierDefaults(EngineConfig_t* cfg) {
    if (cfg->quality_profile < BUBBLE_QUALITY_PROFILE_MCU_SAFE || cfg->quality_profile > BUBBLE_QUALITY_PROFILE_WEB_ULTRA) {
        cfg->quality_profile = BUBBLE_QUALITY_PROFILE_WEB_STANDARD;
    }
    if (cfg->active_voice_limit <= 0) {
        // A zero-initialized legacy config has quality_profile == MCU_SAFE because
        // that enum value is 0. Treat the absent voice limit as the legacy full
        // pool instead of silently reducing old direct-DSP callers to 8 voices.
        if (cfg->quality_profile == BUBBLE_QUALITY_PROFILE_MCU_SAFE) {
            cfg->quality_profile = BUBBLE_QUALITY_PROFILE_WEB_ULTRA;
        }
        cfg->active_voice_limit = ResolveProfileVoiceLimit(cfg->quality_profile);
    } else {
        cfg->active_voice_limit = ClampActiveVoiceLimit(cfg->active_voice_limit);
    }

    if (cfg->smart_start_range == 0) cfg->smart_start_range = 12;
    if (cfg->wet_drive <= 0.0f) cfg->wet_drive = 1.0f;
    if (cfg->wet_output_trim <= 0.0f) cfg->wet_output_trim = 1.0f;
    bool limiter_config_missing = (cfg->final_limiter_ceiling_db == 0.0f && cfg->final_limiter_release_ms <= 0.0f);
    if (!isfinite(cfg->final_limiter_ceiling_db) || limiter_config_missing) cfg->final_limiter_ceiling_db = FINAL_LIMITER_DEFAULT_CEILING_DB;
    cfg->final_limiter_ceiling_db = Clamp(cfg->final_limiter_ceiling_db, -24.0f, 0.0f);
    if (!isfinite(cfg->final_limiter_release_ms) || cfg->final_limiter_release_ms <= 0.0f) cfg->final_limiter_release_ms = FINAL_LIMITER_DEFAULT_RELEASE_MS;
    cfg->final_limiter_release_ms = Clamp(cfg->final_limiter_release_ms, 5.0f, 500.0f);
    if (cfg->sustain_diffusion_stages == 0) cfg->sustain_diffusion_stages = 1;
    if (cfg->sustain_diffusion_delay == 0) cfg->sustain_diffusion_delay = 18;
    if (cfg->droplet_length_scale <= 0.0f) cfg->droplet_length_scale = 0.6f;
    if (cfg->attack_brightness <= 0.0f) cfg->attack_brightness = 1.15f;
    cfg->freeze_amount = Clamp01(cfg->freeze_amount);
    cfg->freeze_enabled = (cfg->freeze_enabled != 0) ? 1 : 0;
    cfg->reverse_probability = Clamp01(cfg->reverse_probability);
    cfg->shimmer_amount = Clamp01(cfg->shimmer_amount);
    cfg->motion_rate = Clamp(cfg->motion_rate, 0.0f, 1.0f);
    cfg->motion_depth = Clamp01(cfg->motion_depth);
    if (cfg->motion_shape < BUBBLE_MOTION_SHAPE_TRIANGLE || cfg->motion_shape > BUBBLE_MOTION_SHAPE_HOLD) {
        cfg->motion_shape = BUBBLE_MOTION_SHAPE_TRIANGLE;
    }
    if (cfg->pitch_mode < BUBBLE_PITCH_MODE_UNISON || cfg->pitch_mode > BUBBLE_PITCH_MODE_SHIMMER) {
        cfg->pitch_mode = BUBBLE_PITCH_MODE_UNISON;
    }

#if defined(BUBBLES_QUALITY_ESP32_SAFE)
    cfg->sustain_diffusion_stages = 1;
    cfg->droplet_enable = 0;
    cfg->attack_rate_jitter_depth = fminf(cfg->attack_rate_jitter_depth, 0.03f);
    cfg->smart_start_range = (cfg->smart_start_range > 16) ? 16 : cfg->smart_start_range;
#elif defined(BUBBLES_QUALITY_WASM_FULL)
    // Keep caller values; wasm tier can run all optional layers.
#else
    cfg->sustain_diffusion_stages = (cfg->sustain_diffusion_stages > 2) ? 2 : cfg->sustain_diffusion_stages;
    cfg->smart_start_range = (cfg->smart_start_range > 32) ? 32 : cfg->smart_start_range;
#endif
}

static float LookupWindow(float phase, WindowType_t type) {
    int idx = (int)(phase * 1023.0f);
    if (idx < 0) idx = 0;
    if (idx > 1023) idx = 1023;

    if (type == WINDOW_TYPE_HANN) {
        return WindowLUT_Hann[idx];
    } else {
        return WindowLUT_Tukey[idx];
    }
}

AutoHoldState_t SoundBubbles_GetAutoHoldState(const SoundBubblesEngine_t* engine) {
    if (engine == NULL) return AUTO_HOLD_IDLE;
    return engine->auto_hold_state;
}

float SoundBubbles_GetAutoHoldAmount(const SoundBubblesEngine_t* engine) {
    if (engine == NULL) return 0.0f;
    return engine->auto_hold_amount;
}

float SoundBubbles_GetAutoHoldTarget(const SoundBubblesEngine_t* engine) {
    if (engine == NULL) return 0.0f;
    return engine->auto_hold_target;
}

int32_t SoundBubbles_GetPhraseAnchorWritePtr(const SoundBubblesEngine_t* engine) {
    if (engine == NULL) return 0;
    return engine->phrase_anchor_write_ptr;
}

bool SoundBubbles_GetPhraseAnchorValid(const SoundBubblesEngine_t* engine) {
    if (engine == NULL) return false;
    return engine->phrase_anchor_valid;
}

uint32_t SoundBubbles_GetPhraseAnchorAge(const SoundBubblesEngine_t* engine) {
    if (engine == NULL) return 0u;
    return engine->phrase_anchor_age;
}

float SoundBubbles_GetAnchorMix(const SoundBubblesEngine_t* engine) {
    if (engine == NULL) return 0.0f;
    return engine->anchor_mix;
}

float SoundBubbles_GetFeedbackGain(const SoundBubblesEngine_t* engine) {
    if (engine == NULL) return 0.0f;
    return engine->feedback_gain;
}

float SoundBubbles_GetFeedbackEnergy(const SoundBubblesEngine_t* engine) {
    if (engine == NULL) return 0.0f;
    return engine->feedback_energy;
}

float SoundBubbles_GetFeedbackSample(const SoundBubblesEngine_t* engine) {
    if (engine == NULL) return 0.0f;
    return engine->feedback_sample;
}

void SoundBubbles_SetFeedbackEnabled(SoundBubblesEngine_t* engine, bool enabled) {
    if (engine == NULL) return;
    engine->feedback_enabled = enabled ? 1 : 0;
    UpdateAutoHoldCoeffs(engine);
    if (!enabled) {
        engine->feedback_gain = 0.0f;
        engine->feedback_gain_target = 0.0f;
        engine->feedback_sample = 0.0f;
        engine->feedback_energy = 0.0f;
    }
}

void SoundBubbles_GetLastWriteContributions(const SoundBubblesEngine_t* engine,
                                            float* out_input,
                                            float* out_feedback,
                                            float* out_retained) {
    if (out_input != NULL) *out_input = engine ? engine->last_write_input : 0.0f;
    if (out_feedback != NULL) *out_feedback = engine ? engine->last_write_feedback : 0.0f;
    if (out_retained != NULL) *out_retained = engine ? engine->last_write_retained : 0.0f;
}

float SoundBubbles_GetFeedbackWriteAperture(const SoundBubblesEngine_t* engine) {
    if (engine == NULL) return 0.0f;
    return engine->feedback_write_aperture;
}

float SoundBubbles_GetFeedbackSafetyThreshold(const SoundBubblesEngine_t* engine) {
    if (engine == NULL) return BUBBLES_FEEDBACK_ENERGY_SAFETY_TH;
    return (engine->feedback_safety_threshold > 0.0f) ? engine->feedback_safety_threshold : BUBBLES_FEEDBACK_ENERGY_SAFETY_TH;
}

void SoundBubbles_SetFeedbackSafetyThreshold(SoundBubblesEngine_t* engine, float threshold) {
    if (engine == NULL) return;
    engine->feedback_safety_threshold = (threshold > 0.0f) ? threshold : BUBBLES_FEEDBACK_ENERGY_SAFETY_TH;
}

void SoundBubbles_GetRingSaturationCounts(const SoundBubblesEngine_t* engine,
                                         uint32_t* out_softclip,
                                         uint32_t* out_clamp) {
    if (out_softclip != NULL) *out_softclip = engine ? engine->ring_softclip_count : 0u;
    if (out_clamp != NULL) *out_clamp = engine ? engine->ring_clamp_count : 0u;
}

void SoundBubbles_ResetRingSaturationCounts(SoundBubblesEngine_t* engine) {
    if (engine != NULL) {
        engine->ring_softclip_count = 0u;
        engine->ring_clamp_count = 0u;
    }
}

void SoundBubbles_SetDitherEnabled(SoundBubblesEngine_t* engine, bool enabled) {
    if (engine != NULL) {
        engine->dither_enabled = enabled ? 1u : 0u;
    }
}

bool SoundBubbles_GetDitherEnabled(const SoundBubblesEngine_t* engine) {
    if (engine == NULL) return false;
    return engine->dither_enabled != 0;
}
