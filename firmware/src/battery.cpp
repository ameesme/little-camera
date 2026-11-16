// The battery, the charger and USB, as read off the pins.

#include "app.h"

namespace App {

char sleepStatus[16] = "";      // Battery debug line, as last drawn
uint32_t sleepStatusMv = 0;
bool sleepStatusChg = false;
bool sleepStatusUsb = false;
bool lastUsb = false;

// Millivolts, never a percentage: the TP4054 cannot be paused, so a reading
// taken on USB is pulled up by the charge current and a percentage would be
// confidently wrong just when someone is watching it.
uint32_t batteryMv() {
    analogSetPinAttenuation(PIN_VBAT, ADC_11db);
    uint32_t sum = 0;
    for (int i = 0; i < 16; i++) sum += analogReadMilliVolts(PIN_VBAT);
    // Hand the pin back so the ADC unit is deleted instead of sitting attached
    // through light sleep: a power readout that costs power is worth nothing.
    pinMode(PIN_VBAT, INPUT);
    return (sum / 16) * 2;
}

bool onCharge() {
    return digitalRead(PIN_VBUS) == HIGH && digitalRead(PIN_CHG) == LOW;
}

bool onUsb() {
    return digitalRead(PIN_VBUS) == HIGH;
}
uint32_t batterySampledAt = 0;
uint16_t batterySampleMv = 0;
uint8_t prevBatteryBars = 4;

void readTelemetry(Sync::Telemetry* out) {
    const uint8_t happiness = moodHappiness();
    out->vbatMv = batterySampleMv;
    out->usb = onUsb();
    out->charging = onCharge();
    out->batteryPercent = Power::percent(batterySampleMv, out->charging, out->usb);
    const Mood::Clock c = moodClock();
    out->happiness = happiness;
    out->moodElapsedS = Mood::elapsed(moodState, c);
}
uint32_t batteryLoggedAt = 0;
bool batteryLogged = false;

void logBattery(uint32_t mv, bool chg, bool chgChanged) {
    const uint32_t now = millis();
    if (batteryLogged && !chgChanged && now - batteryLoggedAt < BATTERY_LOG_INTERVAL_MS) return;
    batteryLoggedAt = now;
    batteryLogged = true;
    char line[32];
    snprintf(line, sizeof line, "%lu,%lu,%d", (unsigned long)(now / 1000),
             (unsigned long)mv, chg ? 1 : 0);
    Storage::appendLog(line);
}

// Reads the battery, keeps `sleepStatus` in step with it, logs a standby
// sample. True when the text moved. Separate from the face because once the
// breath stops the sleep tick still has to sample, with nothing to draw.
bool sampleBattery(bool standby) {
    const uint32_t mv = batteryMv();
    const bool chg = onCharge();
    const bool usb = onUsb();
    batterySampleMv = (uint16_t)mv;
    batterySampledAt = millis();
    const bool chgChanged = sleepStatus[0] && chg != sleepStatusChg;
    const bool usbChanged = sleepStatus[0] && usb != sleepStatusUsb;
    const uint32_t drift = mv > sleepStatusMv ? mv - sleepStatusMv : sleepStatusMv - mv;
    const uint8_t pct = Power::percent(mv, chg, usb);
    const uint8_t bars = Power::bars(pct, prevBatteryBars);
    bool moved = bars != prevBatteryBars;
    prevBatteryBars = bars;
    Display::setBatteryPercent(pct);
    Display::setBatteryBars(bars);
    if (!sleepStatus[0] || chgChanged || usbChanged || drift >= BATTERY_DEADBAND_MV) {
        sleepStatusMv = mv;
        sleepStatusChg = chg;
        sleepStatusUsb = usb;
        Display::setCharging(chg);
        Display::setUsb(usb);
        snprintf(sleepStatus, sizeof sleepStatus, "%u.%02uV", (unsigned)(mv / 1000),
                 (unsigned)(mv % 1000 / 10));
        if (!moved) moved = true;
    }
    if (standby) logBattery(mv, chg, chgChanged || usbChanged);
    return moved;
}

void serviceUsb(bool asleep) {
    const bool usb = onUsb();
    if (usb == lastUsb) return;
    lastUsb = usb;
    sampleBattery();
    if (asleep) showSleepFace(true);
    if (usb) playMoodChirp(255, "Charger", 1);
}

}  // namespace App
