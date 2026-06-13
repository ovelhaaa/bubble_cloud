#ifndef BUBBLE_ESP32_DISPLAY_TFT_H
#define BUBBLE_ESP32_DISPLAY_TFT_H

#include <stdint.h>

#include "driver/spi_common.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    spi_host_device_t spi_host;
    int sclk_gpio;
    int mosi_gpio;
    int miso_gpio;
    int cs_gpio;
    int dc_gpio;
    int reset_gpio;
    int backlight_gpio;
    uint32_t spi_clock_hz;
} BubbleEsp32TftConfig;

esp_err_t bubble_esp32_tft_init(const BubbleEsp32TftConfig* config);
esp_err_t bubble_esp32_tft_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // BUBBLE_ESP32_DISPLAY_TFT_H
