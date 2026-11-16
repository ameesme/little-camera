// The system menu.

#include "app.h"

namespace App {

// System menu
const char* MENU_ITEMS[] = {"diagnostics", "share blog", "sound test", "contrast test", "reboot", "factory reset", "mute", "pal screen", "exit"};
int menuSelected = 0;

// ---- System menu -----------------------------------------------------------

void showMenu() {
    MENU_ITEMS[MENU_MUTE] = Audio::muted() ? "unmute" : "mute";
    Display::drawMenu(MENU_ITEMS, MENU_COUNT, menuSelected);
}

void enterMenu() {
    cameraOff();
    captureHeld = false;
    gestureActive = false;
    holdFired = false;
    Display::clearToast();
    hintToastShowing = false;
    menuSelected = 0;
    mode = Mode::Menu;
    lastActivityTime = millis();
    Audio::playClick();
    Serial.println("Menu: open");
    showMenu();
}

void leaveMenu() {
    Audio::playClick();
    Serial.println("Menu: close");
    cameraOn();
    mode = Mode::Viewfinder;
    gestureActive = false;
    holdFired = false;
    lastActivityTime = millis();
    hintToastShowing = false;
}

void menuRunDiagnostics() {
    char versionBuf[16], bootBuf[16], shotsBuf[16], photosBuf[16], storageBuf[32], battBuf[16], uptimeBuf[16];
    snprintf(versionBuf, sizeof(versionBuf), "%s", LC_VERSION_STRING);
    snprintf(bootBuf, sizeof(bootBuf), "%u", (unsigned)Storage::bootCount());
    snprintf(shotsBuf, sizeof(shotsBuf), "%u", (unsigned)Storage::shotCount());
    snprintf(photosBuf, sizeof(photosBuf), "%d (%d unsynced)", Storage::photoCount(), Storage::unsyncedCount());
    snprintf(storageBuf, sizeof(storageBuf), "%uK / %uK",
             (unsigned)(Storage::usedBytes() / 1024), (unsigned)(Storage::totalBytes() / 1024));
    snprintf(battBuf, sizeof(battBuf), "%umV", (unsigned)batterySampleMv);
    const uint32_t upS = millis() / 1000;
    if (upS >= 3600) snprintf(uptimeBuf, sizeof(uptimeBuf), "%uh%02um", (unsigned)(upS / 3600), (unsigned)((upS % 3600) / 60));
    else snprintf(uptimeBuf, sizeof(uptimeBuf), "%um%02us", (unsigned)(upS / 60), (unsigned)(upS % 60));
    Display::DiagLine lines[] = {
        {"firmware", versionBuf},
        {"camera", Identity::deviceName()},
        {"code", Identity::shortCode()},
        {"boots", bootBuf},
        {"photos", photosBuf},
        {"storage", storageBuf},
        {"battery", battBuf},
        {"uptime", uptimeBuf},
    };
    Display::drawDiagnostics(lines, 8);
    Serial.println("Menu: diagnostics");
    while (!shutterPressed()) delay(10);
    while (shutterPressed()) delay(10);
    showMenu();
}

void menuShareBlog() {
    Display::drawShareBlog(Identity::shortCode(), "lttl.cam");
    Serial.printf("Menu: share blog — code %s\n", Identity::shortCode());
    while (!shutterPressed()) delay(10);
    while (shutterPressed()) delay(10);
    showMenu();
}

void menuSoundTest() {
    Serial.println("Menu: sound test");
    while (shutterPressed()) delay(5);
    Display::showToast("sound test", Display::ToastHAlign::Center, Display::ToastVAlign::Top, true, 0);
    showMenu();
    for (int band = 3; band >= 0; band--) {
        uint8_t h = band == 3 ? 240 : band == 2 ? 160 : band == 1 ? 80 : 20;
        playMoodChirp(h, "Sound test", 1);
        delay(400);
    }
    Display::clearToast();
    showMenu();
}

void menuContrastTest() {
    Serial.println("Menu: contrast test");
    Display::drawTestPattern();
    while (!shutterPressed()) delay(10);
    while (shutterPressed()) delay(10);
    showMenu();
}

void menuFactoryReset() {
    Display::drawPairing("reset?", "hold to confirm, press to cancel");
    Serial.println("Menu: factory reset prompt");
    while (!shutterPressed()) delay(10);
    uint32_t start = millis();
    while (shutterPressed()) {
        if (millis() - start >= TRASH_HOLD_MS) {
            Serial.println("Menu: FACTORY RESET");
            Display::drawPairing("resetting", "goodbye");
            delay(500);
            // Erase NVS and LittleFS
            {
                Preferences prefs;
                prefs.begin("storage", false); prefs.clear(); prefs.end();
                prefs.begin("sync", false);    prefs.clear(); prefs.end();
                prefs.begin("mood", false);    prefs.clear(); prefs.end();
                prefs.begin("pal", false);     prefs.clear(); prefs.end();
            }
            LittleFS.format();
            delay(200);
            esp_restart();
            return;
        }
        delay(10);
    }
    showMenu();
}

void menuReboot() {
    Serial.println("Menu: reboot");
    Display::drawPairing("reboot", nullptr);
    delay(300);
    esp_restart();
}

void menuMute() {
    MoodStore::saveMuteDay(Mood::detail::dailyNow(moodState, moodClock()) / Mood::DAY_S);
    applyMute();
    leaveMenu();
    Display::showToast("muted till midnight", Display::ToastHAlign::Right, Display::ToastVAlign::Top);
}

void menuUnmute() {
    MoodStore::clearMuteDay();
    Audio::muteUntil(0);
    Serial.println("Audio: unmuted");
    // After the unmute, so leaving the menu clicks and confirms sound is back.
    leaveMenu();
    Display::showToast("sound on", Display::ToastHAlign::Right, Display::ToastVAlign::Top);
}

// Test entry: offers a random pal whether or not one is equipped, and equipping
// replaces it.
void menuPalScreen() {
    Serial.println("Menu: pal screen");
    enterPalFound();
    palFromMenu = true;
}

void menuSelect() {
    switch (menuSelected) {
        case 0: menuRunDiagnostics(); break;
        case 1: menuShareBlog(); break;
        case 2: menuSoundTest(); break;
        case 3: menuContrastTest(); break;
        case 4: menuReboot(); break;
        case 5: menuFactoryReset(); break;
        case MENU_MUTE: Audio::muted() ? menuUnmute() : menuMute(); break;
        case 7: menuPalScreen(); break;
        case 8: leaveMenu(); break;
    }
}

}  // namespace App
