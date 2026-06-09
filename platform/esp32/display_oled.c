#include "display_oled.h"

#include <stdio.h>
#include <string.h>

#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#define OLED_PAGES (OLED_HEIGHT / 8)

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_device;
static uint8_t s_framebuffer[OLED_WIDTH * OLED_PAGES];

static const uint8_t FONT_5X7[][5] = {
    [' '] = {0x00, 0x00, 0x00, 0x00, 0x00}, ['!'] = {0x00, 0x00, 0x5f, 0x00, 0x00},
    ['%'] = {0x63, 0x13, 0x08, 0x64, 0x63}, ['-'] = {0x08, 0x08, 0x08, 0x08, 0x08},
    ['.'] = {0x00, 0x60, 0x60, 0x00, 0x00}, ['/'] = {0x20, 0x10, 0x08, 0x04, 0x02},
    ['0'] = {0x3e, 0x51, 0x49, 0x45, 0x3e}, ['1'] = {0x00, 0x42, 0x7f, 0x40, 0x00},
    ['2'] = {0x42, 0x61, 0x51, 0x49, 0x46}, ['3'] = {0x21, 0x41, 0x45, 0x4b, 0x31},
    ['4'] = {0x18, 0x14, 0x12, 0x7f, 0x10}, ['5'] = {0x27, 0x45, 0x45, 0x45, 0x39},
    ['6'] = {0x3c, 0x4a, 0x49, 0x49, 0x30}, ['7'] = {0x01, 0x71, 0x09, 0x05, 0x03},
    ['8'] = {0x36, 0x49, 0x49, 0x49, 0x36}, ['9'] = {0x06, 0x49, 0x49, 0x29, 0x1e},
    [':'] = {0x00, 0x36, 0x36, 0x00, 0x00}, ['A'] = {0x7e, 0x11, 0x11, 0x11, 0x7e},
    ['B'] = {0x7f, 0x49, 0x49, 0x49, 0x36}, ['C'] = {0x3e, 0x41, 0x41, 0x41, 0x22},
    ['D'] = {0x7f, 0x41, 0x41, 0x22, 0x1c}, ['E'] = {0x7f, 0x49, 0x49, 0x49, 0x41},
    ['F'] = {0x7f, 0x09, 0x09, 0x09, 0x01}, ['G'] = {0x3e, 0x41, 0x49, 0x49, 0x7a},
    ['H'] = {0x7f, 0x08, 0x08, 0x08, 0x7f}, ['I'] = {0x00, 0x41, 0x7f, 0x41, 0x00},
    ['J'] = {0x20, 0x40, 0x41, 0x3f, 0x01}, ['K'] = {0x7f, 0x08, 0x14, 0x22, 0x41},
    ['L'] = {0x7f, 0x40, 0x40, 0x40, 0x40}, ['M'] = {0x7f, 0x02, 0x0c, 0x02, 0x7f},
    ['N'] = {0x7f, 0x04, 0x08, 0x10, 0x7f}, ['O'] = {0x3e, 0x41, 0x41, 0x41, 0x3e},
    ['P'] = {0x7f, 0x09, 0x09, 0x09, 0x06}, ['Q'] = {0x3e, 0x41, 0x51, 0x21, 0x5e},
    ['R'] = {0x7f, 0x09, 0x19, 0x29, 0x46}, ['S'] = {0x46, 0x49, 0x49, 0x49, 0x31},
    ['T'] = {0x01, 0x01, 0x7f, 0x01, 0x01}, ['U'] = {0x3f, 0x40, 0x40, 0x40, 0x3f},
    ['V'] = {0x1f, 0x20, 0x40, 0x20, 0x1f}, ['W'] = {0x3f, 0x40, 0x38, 0x40, 0x3f},
    ['X'] = {0x63, 0x14, 0x08, 0x14, 0x63}, ['Y'] = {0x07, 0x08, 0x70, 0x08, 0x07},
    ['Z'] = {0x61, 0x51, 0x49, 0x45, 0x43}, ['_'] = {0x40, 0x40, 0x40, 0x40, 0x40},
};

static BubbleEsp32OledConfig default_config(void) {
    return (BubbleEsp32OledConfig) {
        .sda_gpio = 18,
        .scl_gpio = 23,
        .i2c_hz = 400000,
        .i2c_address = 0x3c,
    };
}

static esp_err_t oled_write(uint8_t control, const uint8_t* data, size_t size) {
    uint8_t buffer[17];
    if (size > sizeof(buffer) - 1U) {
        return ESP_ERR_INVALID_SIZE;
    }
    buffer[0] = control;
    memcpy(&buffer[1], data, size);
    return i2c_master_transmit(s_device, buffer, size + 1U, pdMS_TO_TICKS(100));
}

static esp_err_t oled_command(uint8_t command) {
    return oled_write(0x00, &command, 1);
}

static bool has_glyph(char c) {
    if ((unsigned char)c >= sizeof(FONT_5X7) / sizeof(FONT_5X7[0])) {
        return false;
    }
    for (size_t i = 0; i < sizeof(FONT_5X7[(unsigned char)c]); ++i) {
        if (FONT_5X7[(unsigned char)c][i] != 0) {
            return true;
        }
    }
    return c == ' ';
}

static void draw_char(int column, int page, char c) {
    if (column >= OLED_WIDTH || page < 0 || page >= OLED_PAGES) {
        return;
    }
    if (!has_glyph(c)) {
        c = ' ';
    }

    for (int i = 0; i < 5 && column + i < OLED_WIDTH; ++i) {
        s_framebuffer[page * OLED_WIDTH + column + i] = FONT_5X7[(unsigned char)c][i];
    }
    if (column + 5 < OLED_WIDTH) {
        s_framebuffer[page * OLED_WIDTH + column + 5] = 0x00;
    }
}

static void draw_text(int column, int page, const char* text) {
    while (*text != '\0' && column < OLED_WIDTH) {
        char c = *text++;
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        }
        draw_char(column, page, c);
        column += 6;
    }
}

static esp_err_t flush_framebuffer(void) {
    for (uint8_t page = 0; page < OLED_PAGES; ++page) {
        esp_err_t err = oled_command((uint8_t)(0xb0 | page));
        if (err != ESP_OK) {
            return err;
        }
        err = oled_command(0x00);
        if (err != ESP_OK) {
            return err;
        }
        err = oled_command(0x10);
        if (err != ESP_OK) {
            return err;
        }
        for (int column = 0; column < OLED_WIDTH; column += 16) {
            err = oled_write(0x40, &s_framebuffer[page * OLED_WIDTH + column], 16);
            if (err != ESP_OK) {
                return err;
            }
        }
    }
    return ESP_OK;
}

esp_err_t bubble_esp32_oled_init(const BubbleEsp32OledConfig* config) {
    if (s_device != NULL || s_bus != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    BubbleEsp32OledConfig active = config != NULL ? *config : default_config();
    if (active.i2c_hz == 0) {
        active.i2c_hz = 400000;
    }

    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_1,
        .sda_io_num = active.sda_gpio,
        .scl_io_num = active.scl_gpio,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = {
            .enable_internal_pullup = true,
        },
    };
    esp_err_t err = i2c_new_master_bus(&bus_config, &s_bus);
    if (err != ESP_OK) {
        return err;
    }

    i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = active.i2c_address,
        .scl_speed_hz = active.i2c_hz,
    };
    err = i2c_master_bus_add_device(s_bus, &device_config, &s_device);
    if (err != ESP_OK) {
        (void)bubble_esp32_oled_deinit();
        return err;
    }

    const uint8_t init_sequence[] = {
        0xae, 0xd5, 0x80, 0xa8, 0x3f, 0xd3, 0x00, 0x40,
        0x8d, 0x14, 0x20, 0x00, 0xa1, 0xc8, 0xda, 0x12,
        0x81, 0x7f, 0xd9, 0xf1, 0xdb, 0x40, 0xa4, 0xa6, 0xaf,
    };
    for (size_t i = 0; i < sizeof(init_sequence); ++i) {
        err = oled_command(init_sequence[i]);
        if (err != ESP_OK) {
            (void)bubble_esp32_oled_deinit();
            return err;
        }
    }

    memset(s_framebuffer, 0, sizeof(s_framebuffer));
    return flush_framebuffer();
}

esp_err_t bubble_esp32_oled_show_status(const BubbleEsp32OledStatus* status) {
    if (s_device == NULL || status == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    char line[24];
    memset(s_framebuffer, 0, sizeof(s_framebuffer));

    (void)snprintf(line, sizeof(line), "P%u %s", (unsigned)status->preset_slot, status->preset_name != NULL ? status->preset_name : "INIT");
    draw_text(0, 0, line);
    (void)snprintf(line, sizeof(line), "CPU %2.0f%%", (double)status->cpu_percent);
    draw_text(0, 2, line);
    (void)snprintf(line, sizeof(line), "VOICES %02d", status->active_voices);
    draw_text(0, 4, line);
    draw_text(0, 6, status->clipped ? "CLIP!" : "CLIP OK");

    return flush_framebuffer();
}

esp_err_t bubble_esp32_oled_deinit(void) {
    esp_err_t result = ESP_OK;
    if (s_device != NULL) {
        (void)oled_command(0xae);
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
    memset(s_framebuffer, 0, sizeof(s_framebuffer));
    return result;
}
