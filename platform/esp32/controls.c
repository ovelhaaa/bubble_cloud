#include "controls.h"

#include <string.h>

#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"

static BubbleEsp32ControlsConfig s_config;
static BubbleEsp32RotaryConfig s_encoders[BUBBLE_ESP32_MAX_ROTARY_ENCODERS];
static BubbleEsp32PotConfig s_pots[BUBBLE_ESP32_MAX_POTS];
static adc_oneshot_unit_handle_t s_adc;
static uint8_t s_encoder_last_state[BUBBLE_ESP32_MAX_ROTARY_ENCODERS];
static bool s_initialized;

static const int8_t QUADRATURE_TABLE[16] = {
    0, -1, 1, 0,
    1, 0, 0, -1,
    -1, 0, 0, 1,
    0, 1, -1, 0,
};

static int read_active_low_gpio(int gpio_num) {
    return gpio_num >= 0 ? gpio_get_level((gpio_num_t)gpio_num) == 0 : 0;
}

static uint8_t read_encoder_state(const BubbleEsp32RotaryConfig* encoder) {
    const uint8_t a = (uint8_t)(gpio_get_level((gpio_num_t)encoder->gpio_a) & 1);
    const uint8_t b = (uint8_t)(gpio_get_level((gpio_num_t)encoder->gpio_b) & 1);
    return (uint8_t)((a << 1) | b);
}

static esp_err_t configure_input_pullup(int gpio_num) {
    if (gpio_num < 0) {
        return ESP_OK;
    }

    gpio_config_t config = {
        .pin_bit_mask = 1ULL << (uint32_t)gpio_num,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&config);
}

esp_err_t bubble_esp32_controls_init(const BubbleEsp32ControlsConfig* config) {
    if (s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (config->encoder_count > BUBBLE_ESP32_MAX_ROTARY_ENCODERS || config->pot_count > BUBBLE_ESP32_MAX_POTS) {
        return ESP_ERR_INVALID_SIZE;
    }

    memset(&s_config, 0, sizeof(s_config));
    s_config = *config;
    if (s_config.adc_unit == 0) {
        s_config.adc_unit = ADC_UNIT_1;
    }
    if (config->encoder_count > 0) {
        if (config->encoders == NULL) {
            return ESP_ERR_INVALID_ARG;
        }
        memcpy(s_encoders, config->encoders, config->encoder_count * sizeof(s_encoders[0]));
    }
    if (config->pot_count > 0) {
        if (config->pots == NULL) {
            return ESP_ERR_INVALID_ARG;
        }
        memcpy(s_pots, config->pots, config->pot_count * sizeof(s_pots[0]));
    }
    s_config.encoders = s_encoders;
    s_config.pots = s_pots;

    for (size_t i = 0; i < s_config.encoder_count; ++i) {
        esp_err_t err = configure_input_pullup(s_encoders[i].gpio_a);
        if (err != ESP_OK) {
            return err;
        }
        err = configure_input_pullup(s_encoders[i].gpio_b);
        if (err != ESP_OK) {
            return err;
        }
        err = configure_input_pullup(s_encoders[i].gpio_button);
        if (err != ESP_OK) {
            return err;
        }
        s_encoder_last_state[i] = read_encoder_state(&s_encoders[i]);
    }

    esp_err_t err = configure_input_pullup(s_config.footswitch_gpio);
    if (err != ESP_OK) {
        return err;
    }
    err = configure_input_pullup(s_config.freeze_switch_gpio);
    if (err != ESP_OK) {
        return err;
    }

    if (s_config.pot_count > 0) {
        adc_oneshot_unit_init_cfg_t unit_config = {
            .unit_id = s_config.adc_unit,
        };
        err = adc_oneshot_new_unit(&unit_config, &s_adc);
        if (err != ESP_OK) {
            return err;
        }

        adc_oneshot_chan_cfg_t channel_config = {
            .atten = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        for (size_t i = 0; i < s_config.pot_count; ++i) {
            err = adc_oneshot_config_channel(s_adc, s_pots[i].channel, &channel_config);
            if (err != ESP_OK) {
                return err;
            }
        }
    }

    s_initialized = true;
    return ESP_OK;
}

esp_err_t bubble_esp32_controls_poll(BubbleEsp32ControlsState* state) {
    if (!s_initialized || state == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    memset(state, 0, sizeof(*state));

    for (size_t i = 0; i < s_config.encoder_count; ++i) {
        const uint8_t current = read_encoder_state(&s_encoders[i]);
        const uint8_t index = (uint8_t)((s_encoder_last_state[i] << 2) | current);
        state->encoder_delta[i] = QUADRATURE_TABLE[index & 0x0f];
        state->encoder_pressed[i] = read_active_low_gpio(s_encoders[i].gpio_button) != 0;
        s_encoder_last_state[i] = current;
    }

    for (size_t i = 0; i < s_config.pot_count; ++i) {
        int raw = 0;
        esp_err_t err = adc_oneshot_read(s_adc, s_pots[i].channel, &raw);
        if (err != ESP_OK) {
            return err;
        }
        float normalized = (float)raw / 4095.0f;
        if (normalized < 0.0f) {
            normalized = 0.0f;
        } else if (normalized > 1.0f) {
            normalized = 1.0f;
        }
        state->pot_normalized[i] = normalized;
    }

    state->footswitch_pressed = read_active_low_gpio(s_config.footswitch_gpio) != 0;
    state->freeze_enabled = read_active_low_gpio(s_config.freeze_switch_gpio) != 0;
    return ESP_OK;
}

esp_err_t bubble_esp32_controls_apply(BubbleEngine_t* engine, const BubbleEsp32ControlsState* state) {
    if (!s_initialized || engine == NULL || state == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    for (size_t i = 0; i < s_config.pot_count; ++i) {
        const BubbleEsp32PotConfig* pot = &s_pots[i];
        const float normalized = state->pot_normalized[i];
        const float value = pot->min_value + (pot->max_value - pot->min_value) * normalized;
        if (!bubble_engine_set_parameter(engine, pot->parameter, value)) {
            return ESP_FAIL;
        }
    }

    if (!bubble_engine_set_parameter(engine, BUBBLE_ENGINE_PARAM_FREEZE_ENABLED, state->freeze_enabled ? 1.0f : 0.0f)) {
        return ESP_FAIL;
    }

    // Footswitch is the global wet-bypass gesture in this reference port.
    if (state->footswitch_pressed) {
        (void)bubble_engine_set_parameter(engine, BUBBLE_ENGINE_PARAM_MIX_WET_GAIN, 0.0f);
    }

    return ESP_OK;
}

esp_err_t bubble_esp32_controls_deinit(void) {
    if (s_adc != NULL) {
        esp_err_t err = adc_oneshot_del_unit(s_adc);
        s_adc = NULL;
        if (err != ESP_OK) {
            return err;
        }
    }
    memset(&s_config, 0, sizeof(s_config));
    memset(s_encoders, 0, sizeof(s_encoders));
    memset(s_pots, 0, sizeof(s_pots));
    memset(s_encoder_last_state, 0, sizeof(s_encoder_last_state));
    s_initialized = false;
    return ESP_OK;
}
