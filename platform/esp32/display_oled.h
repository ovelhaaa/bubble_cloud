#ifndef BUBBLE_ESP32_DISPLAY_OLED_H
#define BUBBLE_ESP32_DISPLAY_OLED_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int sda_gpio;
    int scl_gpio;
    uint32_t i2c_hz;
    uint8_t i2c_address;
} BubbleEsp32OledConfig;

typedef struct {
    const char* preset_name;
    uint8_t preset_slot;
    float cpu_percent;
    bool clipped;
    int active_voices;
} BubbleEsp32OledStatus;

esp_err_t bubble_esp32_oled_init(const BubbleEsp32OledConfig* config);
esp_err_t bubble_esp32_oled_show_status(const BubbleEsp32OledStatus* status);
esp_err_t bubble_esp32_oled_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // BUBBLE_ESP32_DISPLAY_OLED_H
