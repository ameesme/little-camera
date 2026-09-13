#pragma once
#include <stdint.h>
enum esp_sleep_wakeup_cause_t { ESP_SLEEP_WAKEUP_UNDEFINED, ESP_SLEEP_WAKEUP_TIMER, ESP_SLEEP_WAKEUP_GPIO };
enum esp_sleep_source_t { ESP_SLEEP_WAKEUP_TIMER_SRC = ESP_SLEEP_WAKEUP_TIMER };
inline int esp_sleep_enable_timer_wakeup(uint64_t) { return 0; }
inline int esp_sleep_enable_gpio_wakeup() { return 0; }
inline int esp_light_sleep_start() { return 0; }
inline esp_sleep_wakeup_cause_t esp_sleep_get_wakeup_cause() { return ESP_SLEEP_WAKEUP_GPIO; }
inline int esp_sleep_disable_wakeup_source(esp_sleep_wakeup_cause_t) { return 0; }
