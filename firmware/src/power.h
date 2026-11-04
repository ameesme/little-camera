#pragma once
#include <stdint.h>

// VBAT in millivolts, and the camera's own reading of what that means. The
// percentage lives here and nowhere else: the phone and the server show it,
// they never recompute it (docs/protocol.md §3.1).
namespace Power {

// Interpolate the chosen resting-voltage estimates; retain 3.3V as the empty floor below the 3.5V point.
constexpr uint16_t CURVE_MV[] = {3300, 3500, 3600, 3700, 3800, 3900, 4000, 4200};
constexpr uint8_t CURVE_PCT[] = {0, 10, 20, 40, 55, 65, 80, 100};
constexpr int CURVE_LEN = sizeof(CURVE_PCT) / sizeof(CURVE_PCT[0]);

// The TP4054 cannot be paused, so a reading taken while it charges is lifted by
// the charge current across the cell's own resistance. Measured on nothing yet:
// this is the knob to turn once a charge run is in /debug.log.
constexpr uint16_t CHARGING_OFFSET_MV = 60;

inline uint8_t fromCurve(uint32_t mv) {
    if (mv <= CURVE_MV[0]) return 0;
    for (int i = 1; i < CURVE_LEN; i++) {
        if (mv > CURVE_MV[i]) continue;
        return (uint8_t)(CURVE_PCT[i - 1] + (uint32_t)(CURVE_PCT[i] - CURVE_PCT[i - 1]) *
                                                (mv - CURVE_MV[i - 1]) / (CURVE_MV[i] - CURVE_MV[i - 1]));
    }
    return 100;
}

// Full is the charger saying so, not the curve: near 4.2 V the voltage flattens
// into ADC noise, but CHG going high with VBUS still present is the TP4054
// reporting constant-voltage done. So charging caps at 99 and only that signal
// reads 100.
inline uint8_t percent(uint32_t mv, bool charging, bool usb) {
    if (usb && !charging) return 100;
    if (!charging) return fromCurve(mv);
    const uint32_t resting = mv > CHARGING_OFFSET_MV ? mv - CHARGING_OFFSET_MV : 0;
    const uint8_t p = fromCurve(resting);
    return p > 99 ? 99 : p;
}

// 1-4 bars with hysteresis so the icon doesn't flip at a threshold.
// Rising thresholds are 5% above falling to create a dead zone.
inline uint8_t bars(uint8_t pct, uint8_t prev) {
    if (prev <= 1) return pct >= 20 ? (pct >= 45 ? (pct >= 80 ? 4 : 3) : 2) : 1;
    if (prev == 2) return pct < 15 ? 1 : (pct >= 45 ? (pct >= 80 ? 4 : 3) : 2);
    if (prev == 3) return pct < 35 ? (pct < 15 ? 1 : 2) : (pct >= 80 ? 4 : 3);
    return pct < 70 ? (pct < 35 ? (pct < 15 ? 1 : 2) : 3) : 4;
}

}  // namespace Power
