#include "display_tft.h"

#include <stdbool.h>
#include <string.h>

#include "board_config.h"
#include "driver/gpio.h"

static BubbleEsp32TftConfig s_config;
static bool s_initialized;

static BubbleEsp32TftConfig default_config(void) {
    return (BubbleEsp32TftConfig) {
        .spi_host = BUBBLE_ESP32_BOARD_TFT_SPI_HOST,
        .sclk_gpio = BUBBLE_ESP32_BOARD_TFT_SPI_SCLK_GPIO,
        .mosi_gpio = BUBBLE_ESP32_BOARD_TFT_SPI_MOSI_GPIO,
        .miso_gpio = BUBBLE_ESP32_BOARD_TFT_SPI_MISO_GPIO,
        .cs_gpio = BUBBLE_ESP32_BOARD_TFT_SPI_CS_GPIO,
        .dc_gpio = BUBBLE_ESP32_BOARD_TFT_DC_GPIO,
        .reset_gpio = BUBBLE_ESP32_BOARD_TFT_RESET_GPIO,
        .backlight_gpio = BUBBLE_ESP32_BOARD_TFT_BACKLIGHT_GPIO,
        .spi_clock_hz = BUBBLE_ESP32_BOARD_TFT_SPI_CLOCK_HZ,
    };
}

static esp_err_t configure_output_gpio(int gpio_num, uint32_t level) {
    if (gpio_num < 0) {
        return ESP_OK;
    }

    gpio_config_t config = {
        .pin_bit_mask = 1ULL << (uint32_t)gpio_num,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&config);
    if (err != ESP_OK) {
        return err;
    }
    return gpio_set_level((gpio_num_t)gpio_num, level);
}

esp_err_t bubble_esp32_tft_init(const BubbleEsp32TftConfig* config) {
    if (s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    s_config = config != NULL ? *config : default_config();
    if (s_config.spi_clock_hz == 0) {
        s_config.spi_clock_hz = BUBBLE_ESP32_BOARD_TFT_SPI_CLOCK_HZ;
    }

    esp_err_t err = configure_output_gpio(s_config.cs_gpio, 1U);
    if (err != ESP_OK) {
        return err;
    }
    err = configure_output_gpio(s_config.dc_gpio, 0U);
    if (err != ESP_OK) {
        return err;
    }
    err = configure_output_gpio(s_config.reset_gpio, 1U);
    if (err != ESP_OK) {
        return err;
    }
    err = configure_output_gpio(s_config.backlight_gpio, 1U);
    if (err != ESP_OK) {
        return err;
    }

    s_initialized = true;
    return ESP_OK;
}

esp_err_t bubble_esp32_tft_deinit(void) {
    memset(&s_config, 0, sizeof(s_config));
    s_initialized = false;
    return ESP_OK;
}
