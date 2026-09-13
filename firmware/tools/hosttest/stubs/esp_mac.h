#pragma once
#include <stdint.h>
enum esp_mac_type_t { ESP_MAC_WIFI_STA, ESP_MAC_BT };
inline int esp_read_mac(uint8_t*, esp_mac_type_t) { return 0; }
