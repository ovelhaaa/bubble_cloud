#include "preset_storage.h"

#include <stdio.h>
#include <string.h>

#include "esp_crc.h"
#include "nvs.h"
#include "nvs_flash.h"

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t payload_size;
    uint32_t crc32;
    BubbleEsp32StoredPreset payload;
} PresetRecord;

#define PRESET_MAGIC 0x4255424CU // "BUBL"
#define PRESET_VERSION 1

static char key_for_slot(uint8_t slot, char* key, size_t key_size) {
    (void)snprintf(key, key_size, "preset_%u", (unsigned)slot);
    return key[0];
}

static esp_err_t open_namespace(nvs_open_mode_t mode, nvs_handle_t* handle) {
    return nvs_open(BUBBLE_ESP32_PRESET_NAMESPACE, mode, handle);
}

esp_err_t bubble_esp32_preset_storage_init(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

esp_err_t bubble_esp32_preset_save(uint8_t slot, const BubbleEsp32StoredPreset* preset) {
    if (slot >= BUBBLE_ESP32_PRESET_SLOT_COUNT || preset == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    PresetRecord record = {
        .magic = PRESET_MAGIC,
        .version = PRESET_VERSION,
        .payload_size = sizeof(*preset),
        .payload = *preset,
    };
    record.crc32 = esp_crc32_le(UINT32_MAX, (const uint8_t*)&record.payload, sizeof(record.payload));

    nvs_handle_t handle = 0;
    esp_err_t err = open_namespace(NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    char key[16];
    (void)key_for_slot(slot, key, sizeof(key));
    err = nvs_set_blob(handle, key, &record, sizeof(record));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t bubble_esp32_preset_load(uint8_t slot, BubbleEsp32StoredPreset* preset) {
    if (slot >= BUBBLE_ESP32_PRESET_SLOT_COUNT || preset == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle = 0;
    esp_err_t err = open_namespace(NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }

    char key[16];
    (void)key_for_slot(slot, key, sizeof(key));
    PresetRecord record;
    size_t record_size = sizeof(record);
    err = nvs_get_blob(handle, key, &record, &record_size);
    nvs_close(handle);
    if (err != ESP_OK) {
        return err;
    }
    if (record_size != sizeof(record) || record.magic != PRESET_MAGIC || record.version != PRESET_VERSION ||
        record.payload_size != sizeof(record.payload)) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    const uint32_t crc32 = esp_crc32_le(UINT32_MAX, (const uint8_t*)&record.payload, sizeof(record.payload));
    if (crc32 != record.crc32) {
        return ESP_ERR_INVALID_CRC;
    }

    *preset = record.payload;
    return ESP_OK;
}

esp_err_t bubble_esp32_preset_erase(uint8_t slot) {
    if (slot >= BUBBLE_ESP32_PRESET_SLOT_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle = 0;
    esp_err_t err = open_namespace(NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    char key[16];
    (void)key_for_slot(slot, key, sizeof(key));
    err = nvs_erase_key(handle, key);
    if (err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}
