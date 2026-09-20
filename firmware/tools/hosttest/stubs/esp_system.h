#pragma once
#include "esp_partition.h"
inline int esp_reset_reason() { return 1; }
inline void esp_restart() {}
inline const char* esp_err_to_name(esp_err_t) { return "ESP_OK"; }
