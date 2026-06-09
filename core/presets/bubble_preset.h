#ifndef BUBBLE_PRESET_H
#define BUBBLE_PRESET_H

#include <stdbool.h>
#include <stddef.h>
#include "../engine/bubble_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

bool bubble_preset_load_json(const char* json, BubbleEnginePreset_t* preset, char* error, size_t error_size);
bool bubble_preset_load_file(const char* path, BubbleEnginePreset_t* preset, char* error, size_t error_size);
bool bubble_preset_save_json(const BubbleEnginePreset_t* preset, char* out_json, size_t out_json_size, char* error, size_t error_size);
bool bubble_preset_save_file(const char* path, const BubbleEnginePreset_t* preset, char* error, size_t error_size);

#ifdef __cplusplus
}
#endif

#endif // BUBBLE_PRESET_H
