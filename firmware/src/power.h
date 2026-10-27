#pragma once
#include <stdint.h>

// VBAT in millivolts, and the camera's own reading of what that means. The
// percentage lives here and nowhere else: the phone and the server show it,
// they never recompute it (docs/protocol.md §3.1).
namespace Power {

// Resting curve for the 1S cell, carried over from selftest where it was
// written for this board. A straight line would read half-full for most of a
// week, because the voltage says little between 3.7 V and 3.9 V and a lot at
// both ends. Still unmeasured: calibrate against a discharge run, which the
// console's `log` dumps from the standby samples main.cpp writes every 5 min.
constexpr uint16_t CURVE_MV[] = {3300, 3600, 3700, 3750, 3800, 3870, 3950, 4050, 4150, 4200};
constexpr uint8_t CURVE_PCT[] = {0, 5, 15, 25, 40, 55, 70, 85, 97, 100};
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

}  // namespace Power
