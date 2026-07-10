from __future__ import annotations

import subprocess
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]

HARNESS_SOURCE = r'''
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "engine/bubble_engine.h"

#define BLOCKS 8192
#define MAX_AVG_BLOCK_US 1000.0

int main(void) {
    static int16_t delay[88200];
    float in[BUBBLES_BLOCK_SIZE];
    float left[BUBBLES_BLOCK_SIZE];
    float right[BUBBLES_BLOCK_SIZE];
    EngineConfig_t config;
    bubble_engine_default_config(&config);
    config.density_burst = 100.0f;
    config.density_sustain = 80.0f;
    config.density_decay = 40.0f;
    memset(delay, 0, sizeof(delay));
    BubbleEngine_t engine;
    bubble_engine_init(&engine, delay, &config);

    clock_t start = clock();
    for (int b = 0; b < BLOCKS; ++b) {
        for (int i = 0; i < BUBBLES_BLOCK_SIZE; ++i) {
            int n = b * BUBBLES_BLOCK_SIZE + i;
            in[i] = 0.25f * sinf(2.0f * 3.14159265358979323846f * 440.0f * (float)n / (float)44100);
            if ((n % 257) == 0) in[i] += 0.7f;
        }
        bubble_engine_process(&engine, in, left, right, BUBBLES_BLOCK_SIZE);
    }
    clock_t end = clock();
    double elapsed_us = ((double)(end - start) * 1000000.0) / (double)CLOCKS_PER_SEC;
    double avg_block_us = elapsed_us / (double)BLOCKS;
    printf("avg_block_us=%.3f budget_us=%.3f\n", avg_block_us, MAX_AVG_BLOCK_US);
    return avg_block_us <= MAX_AVG_BLOCK_US ? 0 : 1;
}
'''


def test_core_processing_stays_within_smoke_block_budget(tmp_path: Path) -> None:
    source = tmp_path / "performance_harness.c"
    binary = tmp_path / "performance_harness"
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
