from __future__ import annotations

import subprocess
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]

HARNESS_SOURCE = r'''
#include <stdio.h>
#include <string.h>
#include "presets/bubble_preset.h"

int main(void) {
    const char* json = "{\"schema_version\":3,\"engine_version\":\"post-diffusion-ui\",\"params\":{\"motion_rate\":0.42,\"motion_depth\":0.66,\"motion_shape\":2},\"macro_values\":{\"density\":0.11,\"bloom\":0.22,\"motion\":0.33,\"texture\":0.44,\"space\":0.55,\"gravity\":0.66,\"memory\":0.77,\"clarity\":0.88,\"freeze\":0.12,\"sparkle\":0.23,\"warmth\":0.34,\"mix\":0.45}}";
    BubbleEnginePreset_t preset;
    char error[256] = {0};
    if (!bubble_preset_load_json(json, &preset, error, sizeof(error))) {
        fprintf(stderr, "%s\n", error);
        return 1;
    }
    if (preset.config.motion_rate < 0.419f || preset.config.motion_rate > 0.421f ||
        preset.config.motion_depth < 0.659f || preset.config.motion_depth > 0.661f ||
        preset.config.motion_shape != BUBBLE_MOTION_SHAPE_HOLD) {
        fprintf(stderr, "motion params did not load into config\n");
        return 2;
    }
    if (preset.macro_values[0] < 0.109f || preset.macro_values[0] > 0.111f ||
        preset.macro_values[2] < 0.329f || preset.macro_values[2] > 0.331f ||
        preset.macro_values[11] < 0.449f || preset.macro_values[11] > 0.451f ||
        preset.macro_targets[2] < 0.329f || preset.macro_targets[2] > 0.331f) {
        fprintf(stderr, "macro_values did not load into preset macro state\n");
        return 5;
    }

    char out[8192];
    if (!bubble_preset_save_json(&preset, out, sizeof(out), error, sizeof(error))) {
        fprintf(stderr, "%s\n", error);
        return 3;
    }
    if (strstr(out, "\"motion_rate\"") == NULL || strstr(out, "\"motion_depth\"") == NULL || strstr(out, "\"motion_shape\"") == NULL) {
        fprintf(stderr, "serialized JSON missing motion params\n%s\n", out);
        return 4;
    }
    if (strstr(out, "\"macro_values\"") == NULL || strstr(out, "\"density\": 0.109") == NULL || strstr(out, "\"mix\": 0.449") == NULL) {
        fprintf(stderr, "serialized JSON missing macro_values\n%s\n", out);
        return 6;
    }
    return 0;
}
'''


def test_motion_params_load_and_save_in_c_presets(tmp_path: Path) -> None:
    source = tmp_path / "motion_preset_harness.c"
    binary = tmp_path / "motion_preset_harness"
    source.write_text(HARNESS_SOURCE, encoding="utf-8")
    subprocess.run(
        [
            "gcc",
            "-O2",
            "-Wall",
            "-Wextra",
            "-std=c11",
            "-Icore",
            "-Icore/dsp",
            str(source),
            "core/presets/bubble_preset.c",
            "core/engine/bubble_engine.c",
            "core/engine/bubble_macro_map.c",
            "core/dsp/sound_bubbles_dsp.c",
            "-lm",
            "-o",
            str(binary),
        ],
        cwd=REPO_ROOT,
        check=True,
    )
    subprocess.run([str(binary)], cwd=REPO_ROOT, check=True)
