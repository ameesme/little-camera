#pragma once
#include <stddef.h>
#include <stdint.h>
inline void esp_fill_random(void*, size_t) {}
inline uint32_t esp_random() { return 4; }
