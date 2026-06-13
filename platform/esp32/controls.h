#ifndef BUBBLE_ESP32_CONTROLS_H
#define BUBBLE_ESP32_CONTROLS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "hal/adc_types.h"
#include "core/engine/bubble_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BUBBLE_ESP32_MAX_ROTARY_ENCODERS 3
#define BUBBLE_ESP32_MAX_POTS 8

typedef struct {
    int gpio_a;
    int gpio_b;
    int gpio_button;
} BubbleEsp32RotaryConfig;

typedef struct {
    adc_channel_t channel;
    BubbleEngineParameterId_t parameter;
    float min_value;
    float max_value;
} BubbleEsp32PotConfig;

typedef struct {
    adc_unit_t adc_unit;
    const BubbleEsp32RotaryConfig* encoders;
    size_t encoder_count;
    const BubbleEsp32PotConfig* pots;
    size_t pot_count;
    int bypass_footswitch_gpio;
    int action_footswitch_gpio;
} BubbleEsp32ControlsConfig;

typedef struct {
    int32_t encoder_delta[BUBBLE_ESP32_MAX_ROTARY_ENCODERS];
    bool encoder_pressed[BUBBLE_ESP32_MAX_ROTARY_ENCODERS];
    float pot_normalized[BUBBLE_ESP32_MAX_POTS];
    bool bypass_pressed;
    bool action_pressed;
} BubbleEsp32ControlsState;

esp_err_t bubble_esp32_controls_init(const BubbleEsp32ControlsConfig* config);
esp_err_t bubble_esp32_controls_poll(BubbleEsp32ControlsState* state);
esp_err_t bubble_esp32_controls_apply(BubbleEngine_t* engine, const BubbleEsp32ControlsState* state);
esp_err_t bubble_esp32_controls_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // BUBBLE_ESP32_CONTROLS_H
