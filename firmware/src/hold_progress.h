#pragma once
#include <stdint.h>

namespace HoldProgress {
constexpr uint32_t START_MS = 200;

inline int pixels(uint32_t heldMs, uint32_t thresholdMs, int length) {
    if (length <= 0 || heldMs <= START_MS || thresholdMs <= START_MS) return 0;
    if (heldMs >= thresholdMs) return length;
    return (int)((uint64_t)(heldMs - START_MS) * length / (thresholdMs - START_MS));
}
}
