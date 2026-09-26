/*
 * Native parity runner for the Offline C <-> WASM contract.
 *
 * This file compiles the real platform/wasm/bubble_cloud_wasm.c translation unit
 * against a tiny emscripten shim, then drives it exactly like the JS runner does:
 * read a raw float32 mono stream, apply a "param_id value" list, reset, process in
 * 32-sample blocks, and log the same metrics CSV header used by
 * tests/dsp/wasm_metrics_runner.mjs.
 *
 * argv: <raw_f32_mono> <params_txt> <metrics_csv>
 *   params_txt lines: "<param_id> <value>" (blank lines and '#' comments ignored)
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "emscripten.h"
#include "../../platform/wasm/bubble_cloud_wasm.c"

#define BLOCK_SIZE 32

static float* ReadRawF32(const char* path, int* out_count) {
    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "Failed to open raw input '%s'.\n", path);
        exit(EXIT_FAILURE);
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        exit(EXIT_FAILURE);
    }
    long bytes = ftell(file);
    if (bytes < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        exit(EXIT_FAILURE);
    }
    int count = (int)(bytes / (long)sizeof(float));
    float* data = (float*)malloc((size_t)(count > 0 ? count : 1) * sizeof(float));
    if (data == NULL) {
        fclose(file);
        exit(EXIT_FAILURE);
    }
    if (count > 0 && fread(data, sizeof(float), (size_t)count, file) != (size_t)count) {
        free(data);
        fclose(file);
        exit(EXIT_FAILURE);
    }
    fclose(file);
    *out_count = count;
    return data;
}

static void ApplyParams(const char* path) {
    FILE* file = fopen(path, "r");
    if (file == NULL) {
        fprintf(stderr, "Failed to open params file '%s'.\n", path);
        exit(EXIT_FAILURE);
    }
    char line[256];
    while (fgets(line, sizeof(line), file) != NULL) {
        char* cursor = line;
        while (*cursor == ' ' || *cursor == '\t') cursor++;
        if (*cursor == '#' || *cursor == '\n' || *cursor == '\r' || *cursor == '\0') continue;
        int param_id = 0;
        float value = 0.0f;
        if (sscanf(cursor, "%d %f", &param_id, &value) == 2) {
            wasm_set_param(param_id, value);
        }
    }
    fclose(file);
}

int main(int argc, char** argv) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <raw_f32_mono> <params_txt> <metrics_csv>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char* raw_path = argv[1];
    const char* params_path = argv[2];
    const char* metrics_path = argv[3];

    int total = 0;
    float* mono = ReadRawF32(raw_path, &total);

    wasm_init(44100.0f);
    ApplyParams(params_path);
    wasm_reset();

    float in[BLOCK_SIZE];
    float left[BLOCK_SIZE];
    float right[BLOCK_SIZE];
    FILE* metrics = fopen(metrics_path, "w");
    if (metrics == NULL) {
        fprintf(stderr, "Failed to open metrics output '%s'.\n", metrics_path);
        free(mono);
        return EXIT_FAILURE;
    }
    fprintf(metrics, "block,active_voices,engine_state,envelope,out_rms_l,out_rms_r,out_peak_l,out_peak_r,peak_l,peak_r,clip_count,limiter_gain\n");

    int block = 0;
    for (int offset = 0; offset < total; offset += BLOCK_SIZE) {
        int chunk = BLOCK_SIZE;
        if (offset + chunk > total) chunk = total - offset;
        for (int i = 0; i < chunk; i++) in[i] = mono[offset + i];

        wasm_process((uintptr_t)in, (uintptr_t)left, (uintptr_t)right, chunk);
        if (chunk < BLOCK_SIZE) continue;

        double sum_l = 0.0;
        double sum_r = 0.0;
        double peak_l = 0.0;
        double peak_r = 0.0;
        for (int i = 0; i < chunk; i++) {
            sum_l += (double)left[i] * (double)left[i];
            sum_r += (double)right[i] * (double)right[i];
            peak_l = fmax(peak_l, fabs((double)left[i]));
            peak_r = fmax(peak_r, fabs((double)right[i]));
        }
        const double rms_l = sqrt(sum_l / (double)chunk);
        const double rms_r = sqrt(sum_r / (double)chunk);

        fprintf(metrics,
                "%d,%d,%d,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%d,%.9g\n",
                block,
                wasm_get_active_voices(),
                wasm_get_state(),
                (double)wasm_get_envelope(),
                rms_l,
                rms_r,
                peak_l,
                peak_r,
                (double)wasm_get_peak_l(),
                (double)wasm_get_peak_r(),
                wasm_get_clip_count(),
                (double)wasm_get_limiter_gain());
        block++;
    }

    fclose(metrics);
    free(mono);
    return EXIT_SUCCESS;
}
