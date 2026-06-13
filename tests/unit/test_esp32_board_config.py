from __future__ import annotations

import re
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
BOARD_CONFIG = REPO_ROOT / "platform" / "esp32" / "board_config.h"
CONTROLS_C = REPO_ROOT / "platform" / "esp32" / "controls.c"
CODEC_C = REPO_ROOT / "platform" / "esp32" / "codec.c"

ADC1_CHANNEL_TO_GPIO = {
    "ADC_CHANNEL_0": 36,
    "ADC_CHANNEL_1": 37,
    "ADC_CHANNEL_2": 38,
    "ADC_CHANNEL_3": 39,
    "ADC_CHANNEL_4": 32,
    "ADC_CHANNEL_5": 33,
    "ADC_CHANNEL_6": 34,
    "ADC_CHANNEL_7": 35,
}


def _macro(name: str) -> str:
    match = re.search(rf"^#define\s+{re.escape(name)}\s+([^\s/]+)", BOARD_CONFIG.read_text(), re.MULTILINE)
    assert match is not None, f"Missing {name} in {BOARD_CONFIG}"
    return match.group(1)


def _int_macro(name: str) -> int:
    return int(_macro(name), 0)


def test_default_encoder_pins_do_not_overlap_expression_adc_gpio() -> None:
    adc_unit = _macro("BUBBLE_ESP32_BOARD_EXPRESSION_ADC_UNIT")
    adc_channel = _macro("BUBBLE_ESP32_BOARD_EXPRESSION_ADC_CHANNEL")
    assert adc_unit == "ADC_UNIT_1"
    expression_gpio = ADC1_CHANNEL_TO_GPIO[adc_channel]

    encoder_pins = {
        name: _int_macro(name)
        for name in [
            "BUBBLE_ESP32_BOARD_ENCODER_0_GPIO_A",
            "BUBBLE_ESP32_BOARD_ENCODER_0_GPIO_B",
            "BUBBLE_ESP32_BOARD_ENCODER_0_GPIO_CLICK",
            "BUBBLE_ESP32_BOARD_ENCODER_1_GPIO_A",
            "BUBBLE_ESP32_BOARD_ENCODER_1_GPIO_B",
            "BUBBLE_ESP32_BOARD_ENCODER_1_GPIO_CLICK",
            "BUBBLE_ESP32_BOARD_ENCODER_2_GPIO_A",
            "BUBBLE_ESP32_BOARD_ENCODER_2_GPIO_B",
            "BUBBLE_ESP32_BOARD_ENCODER_2_GPIO_CLICK",
        ]
    }

    conflicts = {name: gpio for name, gpio in encoder_pins.items() if gpio == expression_gpio}
    assert not conflicts, f"Expression ADC GPIO {expression_gpio} conflicts with encoder pins: {conflicts}"


def test_default_encoder_pins_avoid_uart0_console_gpios() -> None:
    reserved_uart0_gpios = {1, 3}
    encoder_pins = {
        name: _int_macro(name)
        for name in [
            "BUBBLE_ESP32_BOARD_ENCODER_0_GPIO_A",
            "BUBBLE_ESP32_BOARD_ENCODER_0_GPIO_B",
            "BUBBLE_ESP32_BOARD_ENCODER_0_GPIO_CLICK",
            "BUBBLE_ESP32_BOARD_ENCODER_1_GPIO_A",
            "BUBBLE_ESP32_BOARD_ENCODER_1_GPIO_B",
            "BUBBLE_ESP32_BOARD_ENCODER_1_GPIO_CLICK",
            "BUBBLE_ESP32_BOARD_ENCODER_2_GPIO_A",
            "BUBBLE_ESP32_BOARD_ENCODER_2_GPIO_B",
            "BUBBLE_ESP32_BOARD_ENCODER_2_GPIO_CLICK",
        ]
    }

    conflicts = {name: gpio for name, gpio in encoder_pins.items() if gpio in reserved_uart0_gpios}
    assert not conflicts, f"Encoder pins must avoid UART0 console GPIOs {reserved_uart0_gpios}: {conflicts}"


def test_controls_skip_internal_pullups_on_esp32_input_only_gpios() -> None:
    source = CONTROLS_C.read_text()
    assert "supports_internal_pullup" in source
    assert "gpio_num < 34 || gpio_num > 39" in source
    assert "GPIO_PULLUP_DISABLE" in source


def test_codec_zero_i2c_address_falls_back_to_board_default() -> None:
    source = CODEC_C.read_text()
    assert "if (active.i2c_address == 0)" in source
    assert "active.i2c_address = BUBBLE_ESP32_BOARD_CODEC_CONTROL_I2C_ADDRESS;" in source
