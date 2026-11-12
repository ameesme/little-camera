#pragma once
#include <stdint.h>
typedef int esp_err_t;
#define ESP_OK 0
enum esp_sleep_wakeup_cause_t { ESP_SLEEP_WAKEUP_UNDEFINED, ESP_SLEEP_WAKEUP_TIMER, ESP_SLEEP_WAKEUP_GPIO, ESP_SLEEP_WAKEUP_BT = 10, ESP_SLEEP_WAKEUP_ALL = 20, ESP_SLEEP_WAKEUP_EXT1 = 21 };
enum esp_sleep_ext1_wakeup_mode_t { ESP_EXT1_WAKEUP_ANY_LOW = 0, ESP_EXT1_WAKEUP_ANY_HIGH = 1 };
inline esp_err_t esp_sleep_enable_ext1_wakeup(uint64_t, esp_sleep_ext1_wakeup_mode_t) { return 0; }
inline void esp_deep_sleep_start() {}
enum esp_sleep_source_t { ESP_SLEEP_WAKEUP_TIMER_SRC = ESP_SLEEP_WAKEUP_TIMER };
inline esp_err_t esp_sleep_enable_timer_wakeup(uint64_t) { return 0; }
inline esp_err_t esp_sleep_enable_gpio_wakeup() { return 0; }
inline esp_err_t esp_light_sleep_start() { return 0; }
inline esp_sleep_wakeup_cause_t esp_sleep_get_wakeup_cause() { return ESP_SLEEP_WAKEUP_GPIO; }
inline esp_err_t esp_sleep_disable_wakeup_source(esp_sleep_wakeup_cause_t) { return 0; }
