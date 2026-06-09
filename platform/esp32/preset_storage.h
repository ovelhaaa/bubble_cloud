#ifndef BUBBLE_ESP32_PRESET_STORAGE_H
#define BUBBLE_ESP32_PRESET_STORAGE_H

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "core/engine/bubble_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BUBBLE_ESP32_PRESET_NAMESPACE "bubble"
#define BUBBLE_ESP32_PRESET_SLOT_COUNT 8

typedef struct {
    char name[32];
    BubbleEnginePreset_t preset;
} BubbleEsp32StoredPreset;

esp_err_t bubble_esp32_preset_storage_init(void);
esp_err_t bubble_esp32_preset_save(uint8_t slot, const BubbleEsp32StoredPreset* preset);
esp_err_t bubble_esp32_preset_load(uint8_t slot, BubbleEsp32StoredPreset* preset);
esp_err_t bubble_esp32_preset_erase(uint8_t slot);

#ifdef __cplusplus
}
#endif

#endif // BUBBLE_ESP32_PRESET_STORAGE_H
