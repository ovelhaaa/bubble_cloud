#ifndef BUBBLE_ESP32_AUDIO_I2S_H
#define BUBBLE_ESP32_AUDIO_I2S_H

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "board_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BUBBLE_ESP32_AUDIO_SAMPLE_RATE_HZ BUBBLE_ESP32_BOARD_AUDIO_SAMPLE_RATE_HZ
#define BUBBLE_ESP32_AUDIO_BLOCK_FRAMES BUBBLE_ESP32_BOARD_AUDIO_BLOCK_FRAMES

typedef struct {
    int bclk_gpio;
    int ws_gpio;
    int din_gpio;
    int dout_gpio;
    int mclk_gpio;      // Set to -1 when the board/codec does not need MCLK.
    int sample_rate_hz;
    int block_frames;
} BubbleEsp32I2sConfig;

esp_err_t bubble_esp32_i2s_init(const BubbleEsp32I2sConfig* config);
esp_err_t bubble_esp32_i2s_read(int16_t* interleaved_stereo, size_t frames, size_t* frames_read);
esp_err_t bubble_esp32_i2s_write(const int16_t* interleaved_stereo, size_t frames, size_t* frames_written);
esp_err_t bubble_esp32_i2s_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // BUBBLE_ESP32_AUDIO_I2S_H
