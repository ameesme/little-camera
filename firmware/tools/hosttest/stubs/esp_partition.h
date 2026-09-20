#pragma once
// Compile-check stub: the slice of the partition API ota.cpp uses.
#include <stdint.h>
#include <stddef.h>
typedef int esp_err_t;
#define ESP_OK 0
struct esp_partition_t {
    const char* label;
    uint32_t address;
    size_t size;
};
