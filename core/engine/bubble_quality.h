#ifndef BUBBLE_QUALITY_H
#define BUBBLE_QUALITY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Compiled, memory-safe ceiling for the engine voice pool. Runtime quality
// profiles choose an active subset of this pool without changing core DSP code.
#define BUBBLE_ENGINE_MAX_VOICES 32

typedef enum {
    MCU_SAFE = 0,
    MCU_PLUS = 1,
    WEB_STANDARD = 2,
    WEB_ULTRA = 3,

    // Backward-compatible descriptive aliases for call sites that prefer namespacing.
    BUBBLE_QUALITY_PROFILE_MCU_SAFE = MCU_SAFE,
    BUBBLE_QUALITY_PROFILE_MCU_PLUS = MCU_PLUS,
    BUBBLE_QUALITY_PROFILE_WEB_STANDARD = WEB_STANDARD,
    BUBBLE_QUALITY_PROFILE_WEB_ULTRA = WEB_ULTRA
} BubbleQualityProfile;

typedef struct {
    BubbleQualityProfile profile;
    const char* name;
    int32_t max_cpu_percent;
    int32_t max_ram_kb;
    int32_t voice_limit;
} BubbleQualityProfileLimits_t;

// Static product/runtime profile budget table. CPU limits are intended as an
// upper real-time audio callback budget for the target class; RAM limits cover
// the engine-owned fixed delay/voice working set expected by hosts choosing the
// profile. The DSP algorithm remains identical: only active_voice_limit changes.
static const BubbleQualityProfileLimits_t BUBBLE_QUALITY_PROFILE_LIMITS[] = {
    { BUBBLE_QUALITY_PROFILE_MCU_SAFE,     "MCU_SAFE",      35, 256,  8 },
    { BUBBLE_QUALITY_PROFILE_MCU_PLUS,     "MCU_PLUS",      50, 384, 16 },
    { BUBBLE_QUALITY_PROFILE_WEB_STANDARD, "WEB_STANDARD",  60, 512, 24 },
    { BUBBLE_QUALITY_PROFILE_WEB_ULTRA,    "WEB_ULTRA",     75, 768, 32 },
};

#define BUBBLE_QUALITY_PROFILE_COUNT ((int32_t)(sizeof(BUBBLE_QUALITY_PROFILE_LIMITS) / sizeof(BUBBLE_QUALITY_PROFILE_LIMITS[0])))

#ifdef __cplusplus
}
#endif

#endif // BUBBLE_QUALITY_H
