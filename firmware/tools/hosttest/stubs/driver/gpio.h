#pragma once
typedef int gpio_num_t;
enum gpio_int_type_t { GPIO_INTR_LOW_LEVEL = 4, GPIO_INTR_HIGH_LEVEL = 5 };
inline int gpio_wakeup_enable(gpio_num_t, gpio_int_type_t) { return 0; }
inline int gpio_wakeup_disable(gpio_num_t) { return 0; }
