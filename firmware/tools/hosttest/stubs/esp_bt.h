#pragma once
enum esp_bt_controller_status_t { ESP_BT_CONTROLLER_STATUS_IDLE, ESP_BT_CONTROLLER_STATUS_INITED, ESP_BT_CONTROLLER_STATUS_ENABLED };
inline esp_bt_controller_status_t esp_bt_controller_get_status() { return ESP_BT_CONTROLLER_STATUS_IDLE; }
inline int esp_bt_controller_disable() { return 0; }
inline int esp_bt_controller_deinit() { return 0; }
