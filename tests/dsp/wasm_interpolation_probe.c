// M3.2B WASM interpolation selection probe.
//
// Compiles the real platform/wasm/bubble_cloud_wasm.c translation unit against
// the native emscripten shim (no Emscripten required) and asserts that the WASM
// module selects the same interpolator as the shared core: WEB_* -> Hermite,
// MCU_* -> linear. This is the same selection the Offline C renderer and the
// JUCE wrapper receive, because all three link one core.

#include <stdio.h>

#include "emscripten.h"
#include "../../platform/wasm/bubble_cloud_wasm.c"

static int failures = 0;
static void check(int condition, const char* message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

int main(void) {
    wasm_init(48000.0f);

    wasm_set_quality_profile(BUBBLE_QUALITY_PROFILE_WEB_STANDARD);
    check(wasm_get_interpolation_mode() == 1, "WASM WEB_STANDARD -> Hermite");
    wasm_set_quality_profile(BUBBLE_QUALITY_PROFILE_WEB_ULTRA);
    check(wasm_get_interpolation_mode() == 1, "WASM WEB_ULTRA -> Hermite");
    wasm_set_quality_profile(BUBBLE_QUALITY_PROFILE_MCU_SAFE);
    check(wasm_get_interpolation_mode() == 0, "WASM MCU_SAFE -> linear");
    wasm_set_quality_profile(BUBBLE_QUALITY_PROFILE_MCU_PLUS);
    check(wasm_get_interpolation_mode() == 0, "WASM MCU_PLUS -> linear");

    if (failures != 0) {
        fprintf(stderr, "=== WASM interpolation probe FAILED ===\n");
        return 1;
    }
    printf("=== WASM interpolation probe passed: WEB->Hermite, MCU->linear ===\n");
    return 0;
}
