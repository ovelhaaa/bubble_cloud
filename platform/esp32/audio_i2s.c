#include "audio_i2s.h"

#include <stdbool.h>
#include <string.h>

#include "driver/i2s_std.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"

static const char* TAG = "bubble_i2s";

static i2s_chan_handle_t s_rx_channel;
static i2s_chan_handle_t s_tx_channel;
static BubbleEsp32I2sConfig s_config;

static BubbleEsp32I2sConfig default_config(void) {
    return (BubbleEsp32I2sConfig) {
        .bclk_gpio = 26,
        .ws_gpio = 25,
        .din_gpio = 33,
        .dout_gpio = 22,
        .mclk_gpio = 0,
        .sample_rate_hz = BUBBLE_ESP32_AUDIO_SAMPLE_RATE_HZ,
        .block_frames = BUBBLE_ESP32_AUDIO_BLOCK_FRAMES,
    };
}

esp_err_t bubble_esp32_i2s_init(const BubbleEsp32I2sConfig* config) {
    if (s_rx_channel != NULL || s_tx_channel != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    s_config = config != NULL ? *config : default_config();
    if (s_config.sample_rate_hz <= 0) {
        s_config.sample_rate_hz = BUBBLE_ESP32_AUDIO_SAMPLE_RATE_HZ;
    }
    if (s_config.block_frames <= 0) {
        s_config.block_frames = BUBBLE_ESP32_AUDIO_BLOCK_FRAMES;
    }

    i2s_chan_config_t channel_config = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    channel_config.dma_desc_num = 4;
    channel_config.dma_frame_num = (uint32_t)s_config.block_frames;

    ESP_RETURN_ON_ERROR(i2s_new_channel(&channel_config, &s_tx_channel, &s_rx_channel), TAG, "new channel");

    i2s_std_config_t std_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG((uint32_t)s_config.sample_rate_hz),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = s_config.mclk_gpio,
            .bclk = s_config.bclk_gpio,
            .ws = s_config.ws_gpio,
            .dout = s_config.dout_gpio,
            .din = s_config.din_gpio,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    esp_err_t err = i2s_channel_init_std_mode(s_tx_channel, &std_config);
    if (err != ESP_OK) {
        (void)bubble_esp32_i2s_deinit();
        return err;
    }

    err = i2s_channel_init_std_mode(s_rx_channel, &std_config);
    if (err != ESP_OK) {
        (void)bubble_esp32_i2s_deinit();
        return err;
    }

    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_rx_channel), TAG, "enable rx");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx_channel), TAG, "enable tx");
    return ESP_OK;
}

esp_err_t bubble_esp32_i2s_read(int16_t* interleaved_stereo, size_t frames, size_t* frames_read) {
    if (s_rx_channel == NULL || interleaved_stereo == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    size_t bytes_read = 0;
    esp_err_t err = i2s_channel_read(s_rx_channel,
                                     interleaved_stereo,
                                     frames * 2U * sizeof(int16_t),
                                     &bytes_read,
                                     portMAX_DELAY);
    if (frames_read != NULL) {
        *frames_read = bytes_read / (2U * sizeof(int16_t));
    }
    return err;
}

esp_err_t bubble_esp32_i2s_write(const int16_t* interleaved_stereo, size_t frames, size_t* frames_written) {
    if (s_tx_channel == NULL || interleaved_stereo == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    size_t bytes_written = 0;
    esp_err_t err = i2s_channel_write(s_tx_channel,
                                      interleaved_stereo,
                                      frames * 2U * sizeof(int16_t),
                                      &bytes_written,
                                      portMAX_DELAY);
    if (frames_written != NULL) {
        *frames_written = bytes_written / (2U * sizeof(int16_t));
    }
    return err;
}

esp_err_t bubble_esp32_i2s_deinit(void) {
    esp_err_t result = ESP_OK;

    if (s_rx_channel != NULL) {
        esp_err_t err = i2s_channel_disable(s_rx_channel);
        if (err != ESP_OK && result == ESP_OK) {
            result = err;
        }
    }
    if (s_tx_channel != NULL) {
        esp_err_t err = i2s_channel_disable(s_tx_channel);
        if (err != ESP_OK && result == ESP_OK) {
            result = err;
        }
    }
    if (s_rx_channel != NULL) {
        esp_err_t err = i2s_del_channel(s_rx_channel);
        if (err != ESP_OK && result == ESP_OK) {
            result = err;
        }
        s_rx_channel = NULL;
    }
    if (s_tx_channel != NULL) {
        esp_err_t err = i2s_del_channel(s_tx_channel);
        if (err != ESP_OK && result == ESP_OK) {
            result = err;
        }
        s_tx_channel = NULL;
    }
    memset(&s_config, 0, sizeof(s_config));
    return result;
}
