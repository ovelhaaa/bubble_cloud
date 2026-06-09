# Embedded Porting Notes (ESP32)

This repository now includes an ESP-IDF reference port under `platform/esp32/`. The port is intentionally a thin hardware layer around the shared DSP API in `core/engine/bubble_engine.h`; it must not include or link anything from `web/frontend`.

## Audio format and real-time budget

| Setting | Value | Notes |
| --- | ---: | --- |
| Sample rate | 44,100 Hz | Matches `BUBBLES_SAMPLE_RATE` in the engine and avoids resampling. |
| Audio block | 32 frames | Matches `BUBBLES_BLOCK_SIZE`; about 0.73 ms per block at 44.1 kHz. |
| I2S sample format | 16-bit stereo, Philips standard | Input is summed to mono before `bubble_engine_process`; output remains stereo. |
| Default quality profile | `BUBBLE_QUALITY_PROFILE_MCU_SAFE` | The reference app caps active voices to 8 for ESP32-class targets. |

If a board must run at another codec rate, keep the hardware codec and I2S rate synchronized and add an explicit resampler before calling the core engine. Do not silently run the engine at a mismatched sample rate. Control changes are queued from the UI task and applied only by the audio task between blocks, keeping `BubbleEngine_t` mutation serialized with `bubble_engine_process`.

## Reference pin map

The defaults are conservative placeholders for an external I2S codec plus a small SSD1306 OLED. Override them in the config structs for the final PCB.

### I2S audio (`audio_i2s.*`)

| Signal | Default GPIO | Direction | Notes |
| --- | ---: | --- | --- |
| MCLK | GPIO0 | Output | Set `mclk_gpio = -1` for boards that do not need MCLK. |
| BCLK | GPIO26 | Output | I2S bit clock. |
| LRCLK / WS | GPIO25 | Output | Word-select at 44.1 kHz. |
| DIN | GPIO33 | Input | Codec ADC data into ESP32. |
| DOUT | GPIO22 | Output | ESP32 DAC data to codec. |

### Codec control I2C (`codec.*`)

| Signal | Default GPIO | Notes |
| --- | ---: | --- |
| SDA | GPIO21 | I2C0 data, 400 kHz default. |
| SCL | GPIO27 | I2C0 clock, 400 kHz default. |
| Address | `0x18` | Placeholder codec address. |

`codec.c` contains a minimal register-write abstraction and a deliberately small placeholder init sequence. Replace the default `BubbleEsp32CodecRegisterWrite` table with the exact power, PLL, routing, gain, and mute sequence from the chosen codec datasheet.

### Controls (`controls.*`)

| Control | Default GPIO / ADC | Notes |
| --- | --- | --- |
| Rotary encoder A | GPIO32 | Polled quadrature input with pull-up. |
| Rotary encoder B | GPIO34 | Input-only pin; add external pull-up on hardware. |
| Rotary push | GPIO35 | Input-only pin; add external pull-up on hardware. |
| Pot 1 | ADC1 channel 0 | Maps to sustain density. |
| Pot 2 | ADC1 channel 3 | Maps to wet gain. |
| Pot 3 | ADC1 channel 6 | Maps to stereo width. |
| Pot 4 | ADC1 channel 7 | Maps to freeze amount. |
| Footswitch | GPIO4 | Active-low; reference behavior is momentary wet bypass. |
| Freeze switch | GPIO5 | Active-low; maps to `BUBBLE_ENGINE_PARAM_FREEZE_ENABLED`. |

For production pedals, add hardware debounce/RC filtering where possible and tune software polling intervals for the final switch parts.

### OLED status display (`display_oled.*`)

| Signal | Default GPIO | Notes |
| --- | ---: | --- |
| SDA | GPIO18 | I2C1 data, separate from codec I2C by default. |
| SCL | GPIO23 | I2C1 clock, 400 kHz default. |
| Address | `0x3c` | SSD1306-compatible 128x64 OLED. |

The reference status page shows preset slot/name, estimated CPU percentage per audio block, clip state, and active voice count.

## Preset storage

`preset_storage.*` uses ESP-IDF NVS under the namespace `bubble` with eight binary preset slots. Each slot stores a name, `BubbleEnginePreset_t`, version, magic value, and CRC32. Keep preset migration at the hardware layer or in the core engine; do not depend on browser preset code.

## Porting checklist

1. Choose the target ESP32 variant and confirm available internal RAM/PSRAM for the engine delay buffer. The reference app places the delay buffer in external RAM when `CONFIG_SPIRAM` is enabled.
2. Replace the placeholder codec register sequence with board-specific codec initialization.
3. Confirm I2S MCLK/BCLK/LRCLK polarity and slot format on a scope or logic analyzer.
4. Update the pin map tables above and the config passed to `bubble_esp32_i2s_init`, `bubble_esp32_codec_init`, `bubble_esp32_controls_init`, and `bubble_esp32_oled_init`.
5. Validate CPU headroom with the OLED CPU percentage while forcing maximum density and voice count.
6. Save/load all NVS preset slots and verify CRC errors are handled gracefully after flash corruption tests.
