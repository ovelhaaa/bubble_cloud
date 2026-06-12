#ifndef BUBBLE_ESP32_CODEC_H
#define BUBBLE_ESP32_CODEC_H

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "hal/i2c_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t reg;
    uint8_t value;
    uint32_t delay_ms_after_write;
} BubbleEsp32CodecRegisterWrite;

typedef struct {
    i2c_port_t i2c_port;
    int sda_gpio;
    int scl_gpio;
    uint32_t i2c_hz;
    uint8_t i2c_address;
    const BubbleEsp32CodecRegisterWrite* init_sequence;
    size_t init_sequence_count;
} BubbleEsp32CodecConfig;

esp_err_t bubble_esp32_codec_init(const BubbleEsp32CodecConfig* config);
esp_err_t bubble_esp32_codec_write_register(uint8_t reg, uint8_t value);
esp_err_t bubble_esp32_codec_set_output_volume(uint8_t volume_0_to_100);
esp_err_t bubble_esp32_codec_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // BUBBLE_ESP32_CODEC_H
