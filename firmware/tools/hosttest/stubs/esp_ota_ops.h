#pragma once
// Compile-check stub for the OTA API in ota.cpp. Signatures mirror ESP-IDF 5.x
// (esp_ota_begin takes a size or one of the OTA_* sentinels, esp_ota_end
// releases the handle whatever it returns). Nothing here does anything.
#include <stdint.h>
#include <stddef.h>
#include "esp_partition.h"

typedef uint32_t esp_ota_handle_t;
#define OTA_SIZE_UNKNOWN 0xffffffff
#define OTA_WITH_SEQUENTIAL_WRITES 0xfffffffe

enum esp_ota_img_states_t {
    ESP_OTA_IMG_NEW = 0,
    ESP_OTA_IMG_PENDING_VERIFY = 1,
    ESP_OTA_IMG_VALID = 2,
    ESP_OTA_IMG_INVALID = 3,
    ESP_OTA_IMG_ABORTED = 4,
    ESP_OTA_IMG_UNDEFINED = 0xFF,
};

inline const esp_partition_t* esp_ota_get_running_partition() { return nullptr; }
inline const esp_partition_t* esp_ota_get_next_update_partition(const esp_partition_t*) { return nullptr; }
inline esp_err_t esp_ota_begin(const esp_partition_t*, size_t, esp_ota_handle_t*) { return ESP_OK; }
inline esp_err_t esp_ota_write(esp_ota_handle_t, const void*, size_t) { return ESP_OK; }
inline esp_err_t esp_ota_end(esp_ota_handle_t) { return ESP_OK; }
inline esp_err_t esp_ota_abort(esp_ota_handle_t) { return ESP_OK; }
inline esp_err_t esp_ota_set_boot_partition(const esp_partition_t*) { return ESP_OK; }
inline esp_err_t esp_ota_get_state_partition(const esp_partition_t*, esp_ota_img_states_t*) { return ESP_OK; }
inline esp_err_t esp_ota_mark_app_valid_cancel_rollback() { return ESP_OK; }
inline esp_err_t esp_ota_mark_app_invalid_rollback_and_reboot() { return ESP_OK; }
inline esp_err_t esp_ota_check_rollback_is_possible() { return ESP_OK; }
