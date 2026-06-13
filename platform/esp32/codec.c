#include "codec.h"

#include "board_config.h"

#include "driver/i2c_master.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "bubble_codec";

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_device;
static uint8_t s_volume = 80;

// A conservative TLV320AIC3204-style placeholder: reset, unmute DAC path, set nominal gain.
// Board ports should replace this table with the codec vendor's recommended power sequence.
static const BubbleEsp32CodecRegisterWrite s_default_init[] = {
    { .reg = 0x00, .value = 0x00, .delay_ms_after_write = 0 },
    { .reg = 0x01, .value = 0x01, .delay_ms_after_write = 10 },
    { .reg = 0x00, .value = 0x00, .delay_ms_after_write = 0 },
};

static BubbleEsp32CodecConfig default_config(void) {
    return (BubbleEsp32CodecConfig) {
        .i2c_port = BUBBLE_ESP32_BOARD_CODEC_CONTROL_I2C_PORT,
        .sda_gpio = BUBBLE_ESP32_BOARD_CODEC_CONTROL_I2C_SDA_GPIO,
        .scl_gpio = BUBBLE_ESP32_BOARD_CODEC_CONTROL_I2C_SCL_GPIO,
        .i2c_hz = BUBBLE_ESP32_BOARD_CODEC_CONTROL_I2C_HZ,
        .i2c_address = BUBBLE_ESP32_BOARD_CODEC_CONTROL_I2C_ADDRESS,
        .init_sequence = s_default_init,
        .init_sequence_count = sizeof(s_default_init) / sizeof(s_default_init[0]),
    };
}

esp_err_t bubble_esp32_codec_write_register(uint8_t reg, uint8_t value) {
    if (s_device == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    const uint8_t bytes[2] = { reg, value };
    return i2c_master_transmit(s_device, bytes, sizeof(bytes), pdMS_TO_TICKS(100));
}

esp_err_t bubble_esp32_codec_init(const BubbleEsp32CodecConfig* config) {
    if (s_device != NULL || s_bus != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    BubbleEsp32CodecConfig active = config != NULL ? *config : default_config();
    if (active.i2c_hz == 0) {
        active.i2c_hz = BUBBLE_ESP32_BOARD_CODEC_CONTROL_I2C_HZ;
    }
    if (active.i2c_address == 0) {
        active.i2c_address = BUBBLE_ESP32_BOARD_CODEC_CONTROL_I2C_ADDRESS;
    }
    if (active.init_sequence == NULL || active.init_sequence_count == 0) {
        active.init_sequence = s_default_init;
        active.init_sequence_count = sizeof(s_default_init) / sizeof(s_default_init[0]);
    }

    i2c_master_bus_config_t bus_config = {
        .i2c_port = active.i2c_port,
        .sda_io_num = active.sda_gpio,
        .scl_io_num = active.scl_gpio,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = {
            .enable_internal_pullup = true,
        },
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_config, &s_bus), TAG, "new i2c bus");

    i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = active.i2c_address,
        .scl_speed_hz = active.i2c_hz,
    };
    esp_err_t err = i2c_master_bus_add_device(s_bus, &device_config, &s_device);
    if (err != ESP_OK) {
        (void)bubble_esp32_codec_deinit();
        return err;
    }

    for (size_t i = 0; i < active.init_sequence_count; ++i) {
        err = bubble_esp32_codec_write_register(active.init_sequence[i].reg, active.init_sequence[i].value);
        if (err != ESP_OK) {
            (void)bubble_esp32_codec_deinit();
            return err;
        }
        if (active.init_sequence[i].delay_ms_after_write > 0) {
            vTaskDelay(pdMS_TO_TICKS(active.init_sequence[i].delay_ms_after_write));
        }
    }

    return bubble_esp32_codec_set_output_volume(s_volume);
}

esp_err_t bubble_esp32_codec_set_output_volume(uint8_t volume_0_to_100) {
    if (volume_0_to_100 > 100) {
        volume_0_to_100 = 100;
    }
    s_volume = volume_0_to_100;

    if (s_device == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    // Generic hook used by board-specific register maps. The default target treats 0x41 as a
    // headphone/DAC gain register; override this function or the sequence when using another codec.
    const uint8_t codec_gain = (uint8_t)((volume_0_to_100 * 0x7fU) / 100U);
    return bubble_esp32_codec_write_register(0x41, codec_gain);
}

esp_err_t bubble_esp32_codec_deinit(void) {
    esp_err_t result = ESP_OK;
    if (s_device != NULL) {
        esp_err_t err = i2c_master_bus_rm_device(s_device);
        if (err != ESP_OK && result == ESP_OK) {
            result = err;
        }
        s_device = NULL;
    }
    if (s_bus != NULL) {
        esp_err_t err = i2c_del_master_bus(s_bus);
        if (err != ESP_OK && result == ESP_OK) {
            result = err;
        }
        s_bus = NULL;
    }
    return result;
}
