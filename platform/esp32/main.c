#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "audio_i2s.h"
#include "codec.h"
#include "controls.h"
#include "display_oled.h"
#include "preset_storage.h"

#include "core/engine/bubble_engine.h"

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "bubble_main";

static int16_t s_delay_buffer[BUBBLES_BUFFER_SIZE_SAMPLES];
static BubbleEngine_t s_engine;
static float s_input_block[BUBBLE_ESP32_AUDIO_BLOCK_FRAMES];
static float s_output_left[BUBBLE_ESP32_AUDIO_BLOCK_FRAMES];
static float s_output_right[BUBBLE_ESP32_AUDIO_BLOCK_FRAMES];
static int16_t s_i2s_rx[BUBBLE_ESP32_AUDIO_BLOCK_FRAMES * 2];
static int16_t s_i2s_tx[BUBBLE_ESP32_AUDIO_BLOCK_FRAMES * 2];
static volatile BubbleEngineBlockMetrics_t s_metrics;
static volatile float s_cpu_percent;
static volatile bool s_clipped;
static uint8_t s_preset_slot;
static char s_preset_name[32] = "DEFAULT";

static void metrics_callback(const BubbleEngineBlockMetrics_t* metrics, void* user_data) {
    (void)user_data;
    if (metrics != NULL) {
        s_metrics = *metrics;
    }
}

static float clamp_float(float value, float lo, float hi) {
    if (value < lo) {
        return lo;
    }
    if (value > hi) {
        return hi;
    }
    return value;
}

static int16_t float_to_s16(float value) {
    const float clipped = clamp_float(value, -1.0f, 1.0f);
    if (clipped != value) {
        s_clipped = true;
    }
    return (int16_t)lrintf(clipped * 32767.0f);
}

static void load_boot_preset_or_default(void) {
    BubbleEngineConfig_t config;
    bubble_engine_default_config(&config);
    config.quality_profile = BUBBLE_QUALITY_PROFILE_MCU_SAFE;
    config.active_voice_limit = 8;
    bubble_engine_init(&s_engine, s_delay_buffer, &config);
    bubble_engine_set_metrics_callback(&s_engine, metrics_callback, NULL);

    BubbleEsp32StoredPreset stored;
    if (bubble_esp32_preset_load(0, &stored) == ESP_OK) {
        if (bubble_engine_load_preset(&s_engine, &stored.preset)) {
            s_preset_slot = 0;
            (void)snprintf(s_preset_name, sizeof(s_preset_name), "%s", stored.name);
        }
    }
}

static void audio_task(void* arg) {
    (void)arg;

    while (true) {
        size_t frames_read = 0;
        esp_err_t err = bubble_esp32_i2s_read(s_i2s_rx, BUBBLE_ESP32_AUDIO_BLOCK_FRAMES, &frames_read);
        if (err != ESP_OK || frames_read == 0) {
            ESP_LOGW(TAG, "I2S read failed: %s", esp_err_to_name(err));
            continue;
        }

        for (size_t i = 0; i < frames_read; ++i) {
            const float left = (float)s_i2s_rx[i * 2U] / 32768.0f;
            const float right = (float)s_i2s_rx[i * 2U + 1U] / 32768.0f;
            s_input_block[i] = 0.5f * (left + right);
        }

        s_clipped = false;
        const int64_t start_us = esp_timer_get_time();
        bubble_engine_process(&s_engine, s_input_block, s_output_left, s_output_right, (int)frames_read);
        const int64_t elapsed_us = esp_timer_get_time() - start_us;
        const float block_budget_us = (1000000.0f * (float)frames_read) / (float)BUBBLE_ESP32_AUDIO_SAMPLE_RATE_HZ;
        s_cpu_percent = block_budget_us > 0.0f ? (100.0f * (float)elapsed_us) / block_budget_us : 0.0f;

        for (size_t i = 0; i < frames_read; ++i) {
            s_i2s_tx[i * 2U] = float_to_s16(s_output_left[i]);
            s_i2s_tx[i * 2U + 1U] = float_to_s16(s_output_right[i]);
        }

        size_t frames_written = 0;
        err = bubble_esp32_i2s_write(s_i2s_tx, frames_read, &frames_written);
        if (err != ESP_OK || frames_written != frames_read) {
            ESP_LOGW(TAG, "I2S write failed: %s", esp_err_to_name(err));
        }
    }
}

static void controls_task(void* arg) {
    (void)arg;
    BubbleEsp32ControlsState state;

    while (true) {
        if (bubble_esp32_controls_poll(&state) == ESP_OK) {
            (void)bubble_esp32_controls_apply(&s_engine, &state);
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

static void display_task(void* arg) {
    (void)arg;
    while (true) {
        BubbleEsp32OledStatus status = {
            .preset_name = s_preset_name,
            .preset_slot = s_preset_slot,
            .cpu_percent = s_cpu_percent,
            .clipped = s_clipped,
            .active_voices = s_metrics.active_voices,
        };
        (void)bubble_esp32_oled_show_status(&status);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void app_main(void) {
    ESP_ERROR_CHECK(bubble_esp32_preset_storage_init());
    load_boot_preset_or_default();

    const BubbleEsp32PotConfig pots[] = {
        { .channel = ADC_CHANNEL_0, .parameter = BUBBLE_ENGINE_PARAM_DENSITY_SUSTAIN, .min_value = 2.0f, .max_value = 50.0f },
        { .channel = ADC_CHANNEL_3, .parameter = BUBBLE_ENGINE_PARAM_MIX_WET_GAIN, .min_value = 0.0f, .max_value = 1.0f },
        { .channel = ADC_CHANNEL_6, .parameter = BUBBLE_ENGINE_PARAM_STEREO_WIDTH, .min_value = 0.0f, .max_value = 1.0f },
        { .channel = ADC_CHANNEL_7, .parameter = BUBBLE_ENGINE_PARAM_FREEZE_AMOUNT, .min_value = 0.0f, .max_value = 1.0f },
    };
    const BubbleEsp32RotaryConfig encoders[] = {
        { .gpio_a = 32, .gpio_b = 34, .gpio_button = 35 },
    };
    const BubbleEsp32ControlsConfig controls = {
        .adc_unit = ADC_UNIT_1,
        .encoders = encoders,
        .encoder_count = sizeof(encoders) / sizeof(encoders[0]),
        .pots = pots,
        .pot_count = sizeof(pots) / sizeof(pots[0]),
        .footswitch_gpio = 4,
        .freeze_switch_gpio = 5,
    };

    ESP_ERROR_CHECK(bubble_esp32_codec_init(NULL));
    ESP_ERROR_CHECK(bubble_esp32_i2s_init(NULL));
    ESP_ERROR_CHECK(bubble_esp32_controls_init(&controls));
    ESP_ERROR_CHECK(bubble_esp32_oled_init(NULL));

    xTaskCreatePinnedToCore(audio_task, "bubble_audio", 4096, NULL, configMAX_PRIORITIES - 1, NULL, 0);
    xTaskCreatePinnedToCore(controls_task, "bubble_controls", 3072, NULL, tskIDLE_PRIORITY + 2, NULL, 1);
    xTaskCreatePinnedToCore(display_task, "bubble_oled", 3072, NULL, tskIDLE_PRIORITY + 1, NULL, 1);
}
