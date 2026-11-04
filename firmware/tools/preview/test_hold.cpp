#include <Arduino.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../src/display.h"
#include "sim_panel.h"

constexpr int PIXELS = Display::WIDTH * Display::HEIGHT;
static uint8_t source[320 * 240];
static uint8_t photo[Display::PHOTO_BYTES * Display::PHOTO_HEIGHT];
static uint8_t before[PIXELS], full[PIXELS];

static void checkChangedColumn(int fromX, int toX) {
    int changed = 0;
    for (int i = 0; i < PIXELS; i++) {
        if (before[i] == SimPanel::pixels()[i]) continue;
        assert(i % Display::WIDTH >= fromX && i % Display::WIDTH < toX);
        changed++;
    }
    assert(changed > 100);
}

static void checkButton(int x, int y, int h, uint32_t threshold) {
    memcpy(before, SimPanel::pixels(), PIXELS);
    memcpy(photo, Display::photoBits(), sizeof(photo));
    Display::drawHoldProgress(threshold, threshold);
    memcpy(full, SimPanel::pixels(), PIXELS);
    int changed = 0;
    for (int py = 0; py < Display::HEIGHT; py++) {
        for (int px = 0; px < Display::WIDTH; px++) {
            const int i = py * Display::WIDTH + px;
            const bool inside = px >= x && px < x + 70 && py >= y && py < y + h;
            if (before[i] != full[i]) { assert(inside); changed++; }
            if (px >= x + 10 && px < x + 60 && py > y && py < y + h - 1) {
                assert(before[i] != full[i]);
            }
            if (px >= x + 10 && px < x + 60 && (py == y || py == y + h - 1)) {
                assert(before[i] == 0 && full[i] == 0);
            }
            if ((px == x || px == x + 69) && py >= y + 10 && py < y + h - 10) {
                assert(before[i] == 0 && full[i] == 0);
            }
        }
    }
    assert(changed > 70 * h / 2);
    const uint32_t samples[] = {0, 200, (threshold + 200) / 2, threshold, 0};
    for (uint32_t elapsed : samples) {
        Display::drawHoldProgress(elapsed, threshold);
        const int pixels = elapsed <= 200 ? 0 : elapsed >= threshold ? 70 : 35;
        for (int i = 0; i < PIXELS; i++) {
            const bool invert = before[i] != full[i] && i % Display::WIDTH < x + pixels;
            assert(SimPanel::pixels()[i] == (before[i] ^ invert));
        }
        assert(!memcmp(photo, Display::photoBits(), sizeof(photo)));
    }
    assert(SimPanel::protocolErrors() == 0);
}

int main() {
    memset(source, 128, sizeof(source));
    SimPanel::attach(6, 7);
    Display::init(4, 5, 6, 7);
    Display::drawCapture(source, 320, 240);
    checkButton(5, 51, 184, 700);
    Display::drawSave(source, 320, 240);
    checkButton(325, 123, 112, 1500);
    Display::drawGallery(photo, 0, 1);
    checkButton(325, 123, 112, 700);
    Display::drawGalleryEmpty();
    checkButton(325, 5, 230, 700);

    Display::drawCapture(source, 320, 240);
    Display::drawHoldProgress(700, 700);
    memcpy(full, SimPanel::pixels(), PIXELS);
    Display::completeHold();
    Display::drawSaveTransition(0);
    Display::drawHoldProgress(0, 700);
    assert(!memcmp(full, SimPanel::pixels(), PIXELS));
    Display::drawGallery(photo, 0, 1);
    Display::drawCapture(source, 320, 240);
    memcpy(before, SimPanel::pixels(), PIXELS);
    Display::drawHoldProgress(0, 700);
    assert(!memcmp(before, SimPanel::pixels(), PIXELS));

    Display::drawGalleryTransition(photo, 0, 1, 0.5f);
    memcpy(before, SimPanel::pixels(), PIXELS);
    Display::drawCapture(source, 320, 240);
    Display::completeHold();
    Display::drawGalleryTransition(photo, 0, 1, 0.5f);
    checkChangedColumn(0, 43);
    Display::drawGallery(photo, 0, 1);

    Display::drawGalleryDismissTransition(nullptr, 320, 240, 0, 1, 0.5f);
    memcpy(before, SimPanel::pixels(), PIXELS);
    Display::drawGallery(photo, 0, 1);
    Display::completeHold();
    Display::drawGalleryDismissTransition(nullptr, 320, 240, 0, 1, 0.5f);
    checkChangedColumn(362, 400);
    Display::drawViewfinder(source, 320, 240);

    Display::drawSave(source, 320, 240);
    Display::drawSaveTransition(0.5f);
    memcpy(before, SimPanel::pixels(), PIXELS);
    Display::drawSave(source, 320, 240);
    Display::completeHold();
    Display::drawSaveTransition(0.5f);
    checkChangedColumn(362, 400);
    Display::drawViewfinder(source, 320, 240);

    Display::drawSave(source, 320, 240);
    memcpy(before, SimPanel::pixels(), PIXELS);
    Sim::setMillis(1000);
    Display::flashPress();
    assert(memcmp(before, SimPanel::pixels(), PIXELS));
    Display::drawSaveTransition(1);
    assert(memcmp(before, SimPanel::pixels(), PIXELS));
    Sim::setMillis(1119);
    Display::refresh();
    assert(memcmp(before, SimPanel::pixels(), PIXELS));
    Sim::setMillis(1120);
    Display::refresh();
    assert(!memcmp(before, SimPanel::pixels(), PIXELS));
    Display::flashPress();
    Display::drawPairing("123456");
    memcpy(before, SimPanel::pixels(), PIXELS);
    Sim::setMillis(1300);
    Display::refresh();
    assert(!memcmp(before, SimPanel::pixels(), PIXELS));
    Display::setPrompt(Display::Prompt::Verification);
    Display::drawCapture(source, 320, 240);
    memcpy(before, SimPanel::pixels(), PIXELS);
    Display::drawHoldProgress(700, 700);
    assert(!memcmp(before, SimPanel::pixels(), PIXELS));
    puts("ok: horizontal fill, border, cancellation, completion latch, press flash, photo preservation and SPI protocol");
}
