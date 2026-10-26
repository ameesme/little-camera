/**
 * little camera v3q: board self-test.
 *
 * One test per screen, driven by the shutter (press = next, hold 1 s = the
 * action shown at the bottom) and mirrored on the USB serial console.
 *
 *   1 DISPLAY  black / white / checker, then an info screen with chip,
 *              flash, PSRAM and power readings. Reading it is the test.
 *   2 BUZZER   a sweep and three notes. Hold = play again.
 *   3 CAMERA   rails on (CAM_EN), driver up (OV2640 or OV3660), live view, sensor and
 *              frame statistics. Hold = rotate 180.
 *   4 BUTTON   raw level, presses, and a light-sleep wake test.
 *              Hold = sleep (wakes on a press or after 10 s).
 *   5 SUMMARY  everything that can be judged by firmware, PASS / FAIL / ??.
 *
 * Pin map: v3q netlist (gen/design.py), ESP32-S3-WROOM-1 pad -> GPIO.
 */

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <esp_camera.h>
#include <esp_sleep.h>
#include <esp_flash.h>
#include <driver/gpio.h>
#include "spleen_fonts.h"

// ------------------------------------------------------------------ pins --
// Sharp LS027B7DH01 on J3, each line through a 330R guard.
constexpr int PIN_LCD_SCLK = 4;
constexpr int PIN_LCD_SI   = 5;
constexpr int PIN_LCD_SCS  = 6;   // active HIGH
constexpr int PIN_LCD_DISP = 7;   // HIGH = show
// VCOM comes from U8 (EXTCOMIN, ~6 Hz) with EXTMODE tied high, so the panel
// ignores the serial VCOM bit. The test toggles it anyway: free, and it keeps
// the panel safe if EXTMODE turns out not to be high on a real board.

constexpr int PIN_SHUTTER  = 44;  // D2MQ NO to GND, R26 10k pull-up, C24 100n: pressed = LOW
constexpr int PIN_BUZZ     = 46;  // Q2 gate (low-side FET, buzzer to +3V3). Must idle LOW.
constexpr int PIN_VBUS     = 16;  // VBUS * 51/84 (R8 33k / R9 51k): a logic input. At 5.0-5.2 V
                                  // USB it sits at 3.0-3.2 V, the top of the ADC range, so reading
                                  // it as a voltage only gives a saturated number.
constexpr int PIN_CHG      = 17;  // TP4054 CHRG via divider: LOW = charging, HIGH = done/no cell (only with VBUS)
constexpr int PIN_VBAT     = 3;   // VBAT / 2 (R34/R35 1M/1M, C7 100n)
constexpr int PIN_SDA      = 1;   // J4 pads 4/5, 4k7 pull-ups, nothing on board
constexpr int PIN_SCL      = 2;
// GPIO43 (dock pad 2, flash fob trigger, U0TXD) is left alone: it idles high.

// Camera (J2, OV2640 DVP). CAM_Dn = Y(n+2), so CAM_D0 is the driver's d0.
constexpr int PIN_CAM_EN    = 45; // U4/U5/U6 enable, R12 pull-down
constexpr int PIN_CAM_PWDN  = 11;
constexpr int PIN_CAM_RESET = 9;
constexpr int PIN_CAM_XCLK  = 14;
constexpr int PIN_CAM_SIOD  = 18;
constexpr int PIN_CAM_SIOC  = 8;
constexpr int PIN_CAM_VSYNC = 10;
constexpr int PIN_CAM_HREF  = 12;
constexpr int PIN_CAM_PCLK  = 48;
constexpr int PIN_CAM_D[8]  = {39, 41, 42, 40, 38, 47, 21, 13};

// ------------------------------------------------------------- display --
namespace Lcd {

constexpr int W = 400, H = 240, STRIDE = W / 8;
static uint8_t fb[STRIDE * H];          // bit set = white
static SPIClass* spi = nullptr;
static bool vcom = false;
static uint32_t lastVcom = 0;
static uint32_t clockHz = 8000000;      // the app's proven speed; `s` toggles 2 MHz

static void begin() {
    pinMode(PIN_LCD_SCS, OUTPUT);
    digitalWrite(PIN_LCD_SCS, LOW);
    pinMode(PIN_LCD_DISP, OUTPUT);
    digitalWrite(PIN_LCD_DISP, HIGH);
    spi = new SPIClass(HSPI);
    spi->begin(PIN_LCD_SCLK, -1, PIN_LCD_SI, -1);  // write-only panel, no MISO
}

static uint8_t cmd(uint8_t c) {
    vcom = !vcom;
    lastVcom = millis();
    return c | (vcom ? 0x40 : 0x00);
}

// Two bit orders on one wire, as ../src/display.cpp does it: the command and
// line-address bytes go LSB first (bit-reversed here, then shifted MSB first),
// the pixel bytes go out as they are, MSB first = leftmost pixel first, which
// matches the MSB-is-leftmost framebuffer. Sending the pixels LSB first too
// mirrors every 8-pixel group (seen on the first v3q boot).
static inline uint8_t rev(uint8_t b) {
    b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
    b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
    return (b & 0xAA) >> 1 | (b & 0x55) << 1;
}
static inline void sendLsb(uint8_t b) { spi->transfer(rev(b)); }

static void transact(void (*body)()) {
    spi->beginTransaction(SPISettings(clockHz, MSBFIRST, SPI_MODE0));
    digitalWrite(PIN_LCD_SCS, HIGH);
    delayMicroseconds(6);
    body();
    delayMicroseconds(2);
    digitalWrite(PIN_LCD_SCS, LOW);
    spi->endTransaction();
}

static void clearPanel() {
    transact([] { sendLsb(cmd(0x04)); sendLsb(0x00); });
}

static void flush() {
    transact([] {
        sendLsb(cmd(0x01));
        for (int y = 0; y < H; y++) {
            sendLsb((uint8_t)(y + 1));
            spi->writeBytes(fb + y * STRIDE, STRIDE);
            sendLsb(0x00);
        }
        sendLsb(0x00);
    });
}

static void vcomTick() {
    if (millis() - lastVcom < 1000) return;
    transact([] { sendLsb(cmd(0x00)); sendLsb(0x00); });
}

static void fill(uint8_t v) { memset(fb, v, sizeof fb); }

static inline void px(int x, int y, bool white) {
    if ((unsigned)x >= (unsigned)W || (unsigned)y >= (unsigned)H) return;
    uint8_t& b = fb[y * STRIDE + (x >> 3)];
    uint8_t m = 0x80 >> (x & 7);
    if (white) b |= m; else b &= ~m;
}

static void rect(int x, int y, int w, int h, bool white) {
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++) px(i, j, white);
}

// Spleen 8x16, MSB = leftmost pixel. Black text on white unless inverted.
static int text(int x, int y, const char* s, bool inverted = false) {
    for (; *s; s++, x += 8) {
        const uint8_t* g = spleen8x16_getGlyph(*s);
        for (int r = 0; r < 16; r++)
            for (int c = 0; c < 8; c++)
                px(x + c, y + r, ((g[r] << c) & 0x80) ? inverted : !inverted);
    }
    return x;
}

// Same font, every pixel drawn as a scale x scale block: readable from a
// distance while you watch the bench supply.
static void textBig(int x, int y, const char* s, int scale) {
    for (; *s; s++, x += 8 * scale) {
        const uint8_t* g = spleen8x16_getGlyph(*s);
        for (int r = 0; r < 16; r++)
            for (int c = 0; c < 8; c++)
                if ((g[r] << c) & 0x80) rect(x + c * scale, y + r * scale, scale, scale, false);
    }
}

static void textf(int x, int y, const char* fmt, ...) {
    char buf[64];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    text(x, y, buf);
}

// Title bar (inverted) and the button legend on the last line.
static void frame(const char* title, const char* legend) {
    fill(0xFF);
    rect(0, 0, W, 20, false);
    text(4, 2, title, true);
    rect(0, H - 20, W, 20, false);
    text(4, H - 18, legend, true);
}

}  // namespace Lcd

// ----------------------------------------------------------------- power --
struct Power {
    int vbatMv;
    bool vbus, chgLow;
};

static int adcAvgMv(int pin) {
    uint32_t sum = 0;
    for (int i = 0; i < 16; i++) sum += analogReadMilliVolts(pin);
    return sum / 16;
}

static Power readPower() {
    Power p;
    p.vbatMv = adcAvgMv(PIN_VBAT) * 2;                 // 1M/1M
    p.vbus = digitalRead(PIN_VBUS) == HIGH;
    p.chgLow = digitalRead(PIN_CHG) == LOW;
    return p;
}

static const char* chargeState(const Power& p) {
    if (!p.vbus) return "on battery";
    return p.chgLow ? "charging" : "full / no cell";
}

static int lipoPercent(int mv) {
    // Rough resting curve for a 1S LiPo; good enough for a bring-up readout.
    static const int16_t mvAt[] = {3300, 3600, 3700, 3750, 3800, 3870, 3950, 4050, 4150, 4200};
    static const int8_t pct[]   = {0,    5,    15,   25,   40,   55,   70,   85,   97,   100};
    if (mv <= mvAt[0]) return 0;
    for (int i = 1; i < 10; i++)
        if (mv <= mvAt[i]) return pct[i - 1] + (pct[i] - pct[i - 1]) * (mv - mvAt[i - 1]) / (mvAt[i] - mvAt[i - 1]);
    return 100;
}

// ---------------------------------------------------------------- buzzer --
// Bit-banged square wave: no LEDC channel to fight the camera's XCLK over,
// and the pin is guaranteed LOW afterwards (HIGH = coil on, ~DC through it).
static void buzz(uint32_t hz, uint32_t ms) {
    uint32_t half = 500000 / hz;
    uint32_t end = micros() + ms * 1000;
    while ((int32_t)(end - micros()) > 0) {
        digitalWrite(PIN_BUZZ, HIGH);
        delayMicroseconds(half);
        digitalWrite(PIN_BUZZ, LOW);
        delayMicroseconds(half);
    }
    digitalWrite(PIN_BUZZ, LOW);
}

static void buzzerTune() {
    for (uint32_t f = 1000; f <= 5000; f += 100) buzz(f, 8);   // sweep 1-5 kHz
    delay(120);
    buzz(2093, 120); delay(40);   // C7
    buzz(2637, 120); delay(40);   // E7
    buzz(3136, 200);              // G7
}

// ---------------------------------------------------------------- camera --
static bool camUp = false;
static esp_err_t camErr = ESP_OK;
static uint16_t camPid = 0;
static bool camFlip = false;            // 180 deg (vflip + hmirror)
static uint32_t camFrames = 0, camFails = 0;
static uint8_t frameMin = 255, frameMax = 0, frameMean = 0;
static bool frameSeenVaried = false;

// J2 takes either module: the OV2640 the board was drawn for, or an OV3660.
static const char* camName(uint16_t pid) {
    switch (pid) {
        case 0x26:   return "OV2640";
        case 0x3660: return "OV3660";
        case 0x5640: return "OV5640";
        case 0:      return "none";
        default:     return "unknown";
    }
}
static bool camKnown() { return camPid == 0x26 || camPid == 0x3660; }

static void camPinsHighZ() {
    // After the driver is gone nothing should drive into the unpowered sensor.
    const int pins[] = {PIN_CAM_PWDN, PIN_CAM_RESET, PIN_CAM_XCLK, PIN_CAM_SIOD, PIN_CAM_SIOC,
                        PIN_CAM_VSYNC, PIN_CAM_HREF, PIN_CAM_PCLK};
    for (int p : pins) pinMode(p, INPUT);
    for (int p : PIN_CAM_D) pinMode(p, INPUT);
}

static void camApplyFlip() {
    sensor_t* s = esp_camera_sensor_get();
    if (!s) return;
    s->set_vflip(s, camFlip);
    s->set_hmirror(s, camFlip);
}

// Bus-level look at the camera, without the driver: are the rails up (the
// SCCB and PWDN/RESET pull-ups go to DOVDD), who answers on SCCB, and what
// the ID registers say. Runs after a failed init and on serial `q`.
static char probeLines[4][48];

static int sccbRead8(uint8_t addr, uint8_t reg) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    if (Wire.endTransmission(true) != 0) return -1;     // SCCB: stop, then read
    if (Wire.requestFrom(addr, (uint8_t)1) != 1) return -2;
    return Wire.read();
}
static int sccbRead16(uint8_t addr, uint16_t reg) {
    Wire.beginTransmission(addr);
    Wire.write(reg >> 8);
    Wire.write(reg & 0xFF);
    if (Wire.endTransmission(true) != 0) return -1;
    if (Wire.requestFrom(addr, (uint8_t)1) != 1) return -2;
    return Wire.read();
}
static bool sccbWrite8(uint8_t addr, uint8_t reg, uint8_t v) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.write(v);
    return Wire.endTransmission(true) == 0;
}

static void camProbe() {
    Serial.println("[prb] --- camera bus probe ---");
    camPinsHighZ();
    digitalWrite(PIN_CAM_EN, LOW);
    delay(50);
    int offSda = digitalRead(PIN_CAM_SIOD), offScl = digitalRead(PIN_CAM_SIOC);
    digitalWrite(PIN_CAM_EN, HIGH);
    delay(30);
    int sda = digitalRead(PIN_CAM_SIOD), scl = digitalRead(PIN_CAM_SIOC);
    int pwdn = digitalRead(PIN_CAM_PWDN), rst = digitalRead(PIN_CAM_RESET);
    Serial.printf("[prb] rails off: SIOD %d SIOC %d | rails on: SIOD %d SIOC %d PWDN %d RESET %d (all 1 = DOVDD up)\n",
                  offSda, offScl, sda, scl, pwdn, rst);
    snprintf(probeLines[0], 48, "DOVDD pull-ups: SDA%d SCL%d PWDN%d RST%d", sda, scl, pwdn, rst);

    // Wake the sensor the way the driver does: PWDN low, RESET pulse, XCLK on.
    pinMode(PIN_CAM_PWDN, OUTPUT);  digitalWrite(PIN_CAM_PWDN, LOW);
    pinMode(PIN_CAM_RESET, OUTPUT); digitalWrite(PIN_CAM_RESET, LOW);
    bool xclk = ledcAttach(PIN_CAM_XCLK, 20000000, 1) && ledcWrite(PIN_CAM_XCLK, 1);
    delay(10);
    digitalWrite(PIN_CAM_RESET, HIGH);
    delay(30);

    Wire.begin(PIN_CAM_SIOD, PIN_CAM_SIOC, 100000);
    char found[24] = "";
    int nFound = 0;
    for (uint8_t a = 0x08; a < 0x78; a++) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() == 0) {
            Serial.printf("[prb] SCCB ack at 0x%02x\n", a);
            if (nFound < 6) snprintf(found + strlen(found), sizeof found - strlen(found), " %02x", a);
            nFound++;
        }
    }
    Serial.printf("[prb] XCLK %s, %d address(es) answered (OV2640 = 0x30, OV3660/OV5640 = 0x3c)\n", xclk ? "on" : "FAILED", nFound);
    snprintf(probeLines[1], 48, "XCLK %s, SCCB:%s", xclk ? "on" : "fail", nFound ? found : " nobody");
    probeLines[2][0] = probeLines[3][0] = 0;

    if (!nFound) {
        // Cheap what-ifs before anyone reaches for a scope: a sensor that
        // wants PWDN the other way, SDA/SCL crossed somewhere between the pad
        // and the flex, or a slower bus.
        auto anyone = []() {
            for (uint8_t a : {0x30, 0x3C, 0x21, 0x60}) {
                Wire.beginTransmission(a);
                if (Wire.endTransmission() == 0) return (int)a;
            }
            return 0;
        };
        digitalWrite(PIN_CAM_PWDN, HIGH);
        delay(30);
        int a1 = anyone();
        digitalWrite(PIN_CAM_PWDN, LOW);
        delay(30);
        Wire.end();
        Wire.begin(PIN_CAM_SIOC, PIN_CAM_SIOD, 100000);     // swapped
        int a2 = anyone();
        Wire.end();
        Wire.begin(PIN_CAM_SIOD, PIN_CAM_SIOC, 10000);      // 10 kHz
        int a3 = anyone();
        Serial.printf("[prb] what-ifs: PWDN high -> %s | SDA/SCL swapped -> %s | 10 kHz -> %s\n",
                      a1 ? "ACK" : "nothing", a2 ? "ACK" : "nothing", a3 ? "ACK" : "nothing");
        snprintf(probeLines[2], 48, "pwdn-hi %s, swapped %s, 10k %s", a1 ? "ACK" : "-", a2 ? "ACK" : "-", a3 ? "ACK" : "-");
    }

    if (nFound) {
        Wire.beginTransmission(0x30);
        if (Wire.endTransmission() == 0) {
            sccbWrite8(0x30, 0xFF, 0x01);                  // sensor register bank
            int pidh = sccbRead8(0x30, 0x0A), pidl = sccbRead8(0x30, 0x0B);
            int midh = sccbRead8(0x30, 0x1C), midl = sccbRead8(0x30, 0x1D);
            Serial.printf("[prb] 0x30 PID %02x %02x (OV2640: 26 41/42)  MID %02x %02x (7f a2)\n", pidh & 0xFF, pidl & 0xFF, midh & 0xFF, midl & 0xFF);
            snprintf(probeLines[2], 48, "0x30 PID %02x%02x MID %02x%02x", pidh & 0xFF, pidl & 0xFF, midh & 0xFF, midl & 0xFF);
        }
        Wire.beginTransmission(0x3C);
        if (Wire.endTransmission() == 0) {
            int h = sccbRead16(0x3C, 0x300A), l = sccbRead16(0x3C, 0x300B);
            int h2 = sccbRead16(0x3C, 0x300A), l2 = sccbRead16(0x3C, 0x300B);
            Serial.printf("[prb] 0x3c chip ID 0x300A/B: %d %d, again %d %d (OV3660: 54 96 = 0x36 0x60, OV5640: 0x56 0x40; <0 = bus error)\n", h, l, h2, l2);
            snprintf(probeLines[3], 48, "0x3c ID %02x%02x / %02x%02x", h & 0xFF, l & 0xFF, h2 & 0xFF, l2 & 0xFF);
        }
    }

    Wire.end();
    ledcDetach(PIN_CAM_XCLK);
    camPinsHighZ();
    digitalWrite(PIN_CAM_EN, LOW);
    Serial.println("[prb] --- done, rails off ---");
}

// Serial `e`: camera rails on and the sensor awake (PWDN low, RESET high,
// XCLK 20 MHz), held until `e` again, so the rails can be measured:
// C9 DOVDD 3.3 V, C11 AVDD 2.8 V, C13 DVDD 1.2 V. D3 lights from DOVDD.
static bool railHold = false;
static void camRailHold() {
    railHold = !railHold;
    if (railHold) {
        camPinsHighZ();
        digitalWrite(PIN_CAM_EN, HIGH);
        delay(20);
        pinMode(PIN_CAM_PWDN, OUTPUT);  digitalWrite(PIN_CAM_PWDN, LOW);
        pinMode(PIN_CAM_RESET, OUTPUT); digitalWrite(PIN_CAM_RESET, HIGH);
        bool x = ledcAttach(PIN_CAM_XCLK, 20000000, 1) && ledcWrite(PIN_CAM_XCLK, 1);
        Serial.printf("[cam] rails ON and held, XCLK %s. Measure C9 3.3 V, C11 2.8 V, C13 1.2 V. `e` again = off\n", x ? "on" : "FAILED");
    } else {
        ledcDetach(PIN_CAM_XCLK);
        camPinsHighZ();
        digitalWrite(PIN_CAM_EN, LOW);
        Serial.println("[cam] rails off");
    }
}

static bool camStart() {
    if (camUp) return true;
    digitalWrite(PIN_CAM_EN, HIGH);
    delay(20);   // three LDOs up, then R14/C14 releases RESET after ~1 ms

    camera_config_t c = {};
    c.ledc_channel = LEDC_CHANNEL_0;
    c.ledc_timer = LEDC_TIMER_0;
    c.pin_d0 = PIN_CAM_D[0]; c.pin_d1 = PIN_CAM_D[1]; c.pin_d2 = PIN_CAM_D[2]; c.pin_d3 = PIN_CAM_D[3];
    c.pin_d4 = PIN_CAM_D[4]; c.pin_d5 = PIN_CAM_D[5]; c.pin_d6 = PIN_CAM_D[6]; c.pin_d7 = PIN_CAM_D[7];
    c.pin_xclk = PIN_CAM_XCLK;
    c.pin_pclk = PIN_CAM_PCLK;
    c.pin_vsync = PIN_CAM_VSYNC;
    c.pin_href = PIN_CAM_HREF;
    c.pin_sccb_sda = PIN_CAM_SIOD;
    c.pin_sccb_scl = PIN_CAM_SIOC;
    c.pin_pwdn = PIN_CAM_PWDN;
    c.pin_reset = PIN_CAM_RESET;
    c.xclk_freq_hz = 20000000;
    c.frame_size = FRAMESIZE_QVGA;
    c.pixel_format = PIXFORMAT_GRAYSCALE;
    c.grab_mode = CAMERA_GRAB_LATEST;
    c.fb_location = CAMERA_FB_IN_DRAM;    // same as the app
    c.fb_count = 1;

    camErr = esp_camera_init(&c);
    if (camErr != ESP_OK) {
        Serial.printf("[cam] init failed: 0x%x (%s)\n", camErr, esp_err_to_name(camErr));
        camPinsHighZ();
        digitalWrite(PIN_CAM_EN, LOW);
        camProbe();
        return false;
    }
    sensor_t* s = esp_camera_sensor_get();
    camPid = s ? s->id.PID : 0;
    camApplyFlip();
    camUp = true;
    Serial.printf("[cam] up, PID 0x%04x = %s, VER 0x%02x\n", camPid, camName(camPid), s ? s->id.VER : 0);
    return true;
}

static void camStop() {
    if (!camUp) return;
    esp_camera_deinit();
    camPinsHighZ();
    digitalWrite(PIN_CAM_EN, LOW);
    camUp = false;
    Serial.println("[cam] down, rails off");
}

// Grab a frame, keep its statistics, and draw it 4x4-Bayer dithered at x 80.
static bool camFrameToLcd() {
    camera_fb_t* f = esp_camera_fb_get();
    if (!f) { camFails++; return false; }
    camFrames++;
    static const uint8_t bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
    uint32_t sum = 0;
    uint8_t lo = 255, hi = 0;
    int w = min<int>(f->width, 320), h = min<int>(f->height, 240);
    for (int y = 0; y < h; y++) {
        const uint8_t* row = f->buf + y * f->width;
        for (int x = 0; x < w; x++) {
            uint8_t v = row[x];
            sum += v;
            if (v < lo) lo = v;
            if (v > hi) hi = v;
            Lcd::px(80 + x, y, v > bayer[y & 3][x & 3] * 16 + 8);
        }
    }
    frameMin = lo; frameMax = hi; frameMean = sum / (w * h);
    if (hi - lo > 40) frameSeenVaried = true;
    esp_camera_fb_return(f);
    return true;
}

// ---------------------------------------------------------------- button --
static bool pressedRaw() { return digitalRead(PIN_SHUTTER) == LOW; }

enum class Btn { None, Tap, Hold };
constexpr uint32_t HOLD_MS = 1000;
static uint32_t pressCount = 0;
static bool buttonSeenHigh = false, buttonSeenLow = false;

static Btn pollButton() {
    static bool down = false, held = false;
    static uint32_t since = 0;
    bool p = pressedRaw();
    if (p) buttonSeenLow = true; else buttonSeenHigh = true;
    if (p && !down) {
        delay(15);                       // hardware RC is 1 ms; this is belt and braces
        if (!pressedRaw()) return Btn::None;
        down = true; held = false; since = millis(); pressCount++;
        Serial.printf("[btn] press #%lu\n", (unsigned long)pressCount);
        return Btn::None;
    }
    if (p && down && !held && millis() - since >= HOLD_MS) { held = true; return Btn::Hold; }
    if (!p && down) {
        delay(15);
        if (pressedRaw()) return Btn::None;
        down = false;
        return held ? Btn::None : Btn::Tap;
    }
    return Btn::None;
}

// ----------------------------------------------------------------- stages --
enum Stage { DISPLAY_, BUZZER, CAMERA, BUTTON, SUMMARY, N_STAGES };
static Stage stage = DISPLAY_;
static uint32_t stageStart = 0;
static bool buzzerPlayed = false;
static int sleepWakes = 0, sleepTimer = 0;
static uint32_t flashBytes = 0;
static uint32_t flashJedec = 0;

// One second of CHG_STAT and VBAT. With USB in and no cell, the TP4054 hunts:
// its output wanders around 4.0-4.2 V and CHRG flickers between charging and
// done. A real cell, charging or full, gives a steady CHRG and a steady VBAT.
static void printPower() {
    Power p = readPower();
    int lows = 0, lo = 99999, hi = 0;
    for (int i = 0; i < 100; i++) {
        if (digitalRead(PIN_CHG) == LOW) lows++;
        int mv = analogReadMilliVolts(PIN_VBAT) * 2;
        lo = min(lo, mv); hi = max(hi, mv);
        delay(10);
    }
    Serial.printf("[pwr] VBAT %d mV (~%d%%), 1 s spread %d-%d mV | VBUS %s | CHG_STAT low %d/100 -> %s\n",
                  p.vbatMv, lipoPercent(p.vbatMv), lo, hi, p.vbus ? "present" : "absent", lows,
                  !p.vbus ? "on battery"
                  : (lows > 0 && lows < 100) ? "flickering: charger hunting, probably NO CELL"
                  : lows == 100 ? "charging" : "full (or no cell)");
}

static void i2cScan() {
    Wire.begin(PIN_SDA, PIN_SCL, 100000);
    int n = 0;
    for (uint8_t a = 1; a < 127; a++) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() == 0) { Serial.printf("[i2c] device at 0x%02x\n", a); n++; }
    }
    Serial.printf("[i2c] %d device(s) on SDA %d / SCL %d (none expected on a bare board)\n", n, PIN_SDA, PIN_SCL);
    Wire.end();
}

static void drawSummary(bool log);

static void enterStage(Stage s) {
    if (stage == CAMERA && s != CAMERA) camStop();
    stage = s;
    stageStart = millis();
    static const char* names[] = {"DISPLAY", "BUZZER", "CAMERA", "BUTTON", "SUMMARY"};
    Serial.printf("\n=== %d/5 %s ===\n", (int)s + 1, names[s]);
    if (s == BUZZER) { buzzerTune(); buzzerPlayed = true; }
    if (s == CAMERA) camStart();
    if (s == DISPLAY_) printPower();
    if (s == SUMMARY) drawSummary(true);
}

static void drawDisplayStage() {
    Power p = readPower();
    Lcd::frame("1/5 DISPLAY + POWER", "press: next   hold: patterns again");
    int y = 28;
    Lcd::textf(8, y, "chip   ESP32-S3 rev %d, %d cores", ESP.getChipRevision(), ESP.getChipCores()); y += 18;
    Lcd::textf(8, y, "flash  %lu MB (JEDEC %06lx)", (unsigned long)(flashBytes >> 20), (unsigned long)flashJedec); y += 18;
    Lcd::textf(8, y, "psram  %lu KB free of %lu KB", (unsigned long)(ESP.getFreePsram() >> 10), (unsigned long)(ESP.getPsramSize() >> 10)); y += 18;
    uint64_t mac = ESP.getEfuseMac();
    Lcd::textf(8, y, "mac    %02X:%02X:%02X:%02X:%02X:%02X", (uint8_t)mac, (uint8_t)(mac >> 8), (uint8_t)(mac >> 16),
               (uint8_t)(mac >> 24), (uint8_t)(mac >> 32), (uint8_t)(mac >> 40)); y += 26;
    Lcd::textf(8, y, "VBAT   %d.%02d V  ~%d%%", p.vbatMv / 1000, (p.vbatMv % 1000) / 10, lipoPercent(p.vbatMv)); y += 18;
    Lcd::textf(8, y, "VBUS   %s", p.vbus ? "present" : "absent"); y += 18;
    Lcd::textf(8, y, "charge %s", chargeState(p)); y += 26;
    Lcd::textf(8, y, "uptime %lus   SPI %lu MHz", (unsigned long)(millis() / 1000), (unsigned long)(Lcd::clockHz / 1000000));
    // Corner ticks: all four must be visible, or the panel/tail is offset.
    Lcd::rect(0, 20, 6, 6, false);            Lcd::rect(Lcd::W - 6, 20, 6, 6, false);
    Lcd::rect(0, Lcd::H - 26, 6, 6, false);   Lcd::rect(Lcd::W - 6, Lcd::H - 26, 6, 6, false);
    Lcd::flush();
}

static void displayPatterns() {
    Serial.println("[lcd] black, white, checker, stripes");
    Lcd::fill(0x00); Lcd::flush(); delay(600);
    Lcd::fill(0xFF); Lcd::flush(); delay(600);
    for (int y = 0; y < Lcd::H; y++)
        for (int x = 0; x < Lcd::W; x++) Lcd::px(x, y, ((x >> 4) + (y >> 4)) & 1);  // 16 px checker
    Lcd::flush(); delay(800);
    for (int y = 0; y < Lcd::H; y++) memset(Lcd::fb + y * Lcd::STRIDE, (y & 1) ? 0xFF : 0x00, Lcd::STRIDE);
    Lcd::flush(); delay(800);   // 1 px lines: shows a bad bit order or a dropped line at once
}

static void drawBuzzerStage() {
    Lcd::frame("2/5 BUZZER", "press: next   hold: play again");
    Lcd::text(8, 40, "Played: sweep 1-5 kHz, then C-E-G.");
    Lcd::text(8, 66, "Heard nothing? Check BZ1 (MLT-5020),");
    Lcd::text(8, 84, "Q2 (DMG1012UW) and R27/R28 on IO46.");
    Lcd::text(8, 120, "A buzz or hiss while silent means");
    Lcd::text(8, 138, "IO46 is not idling LOW.");
    Lcd::flush();
}

static void drawCameraStage() {
    static uint32_t lastFpsAt = 0, framesAtLast = 0;
    static float fps = 0;
    Lcd::frame("3/5 CAMERA", "press: next   hold: rotate 180");
    if (!camUp) {
        Lcd::textf(8, 40, "init failed: 0x%x", camErr);
        Lcd::text(8, 58, esp_err_to_name(camErr));
        Lcd::text(8, 80, camErr == ESP_ERR_NOT_SUPPORTED ? "a sensor answered, its ID didn't match"
                                                          : "no sensor answered on SCCB");
        for (int i = 0; i < 4; i++) Lcd::text(8, 106 + i * 18, probeLines[i]);
        Lcd::text(8, 186, "serial: c = retry, q = probe again");
        Lcd::flush();
        return;
    }
    Lcd::rect(0, 20, 80, Lcd::H - 40, true);
    camFrameToLcd();
    uint32_t now = millis();
    if (now - lastFpsAt >= 1000) { fps = (camFrames - framesAtLast) * 1000.0f / (now - lastFpsAt); lastFpsAt = now; framesAtLast = camFrames; }
    Lcd::text(2, 26, camName(camPid));
    Lcd::textf(2, 46, "%.1ffps", fps);
    Lcd::textf(2, 66, "avg %u", frameMean);
    Lcd::textf(2, 86, "min %u", frameMin);
    Lcd::textf(2, 106, "max %u", frameMax);
    Lcd::textf(2, 126, "err %lu", (unsigned long)camFails);
    Lcd::text(2, 146, camFlip ? "rot 180" : "rot 0");
    // Re-draw the bars so the dithered frame doesn't overwrite them (rows 0-19 / 220-239).
    Lcd::rect(0, 0, Lcd::W, 20, false);
    Lcd::text(4, 2, "3/5 CAMERA", true);
    Lcd::rect(0, Lcd::H - 20, Lcd::W, 20, false);
    Lcd::text(4, Lcd::H - 18, "press: next   hold: rotate 180", true);
    Lcd::flush();
}

static void drawButtonStage() {
    Lcd::frame("4/5 BUTTON + SLEEP", "press: next   hold: sleep test");
    Lcd::textf(8, 40, "shutter (IO44) now: %s", pressedRaw() ? "PRESSED (LOW)" : "released (HIGH)");
    Lcd::textf(8, 60, "presses so far: %lu", (unsigned long)pressCount);
    Lcd::text(8, 96, "Hold 1 s, let go: the board light-");
    Lcd::text(8, 114, "sleeps with the camera off. A press");
    Lcd::text(8, 132, "should wake it; else it wakes in 10 s.");
    Lcd::textf(8, 168, "button wakes: %d   timer wakes: %d", sleepWakes, sleepTimer);
    Lcd::flush();
}

static void sleepTest() {
    // Wait for release, or the LOW level would wake us immediately.
    while (pressedRaw()) delay(5);
    delay(50);
    Lcd::frame("4/5 SLEEPING", "press the shutter to wake");
    Lcd::text(8, 60, "light sleep... (USB serial may drop)");
    Lcd::flush();
    Serial.println("[slp] light sleep: wake on IO44 LOW or 10 s timer");
    Serial.flush();
    delay(20);
    gpio_wakeup_enable((gpio_num_t)PIN_SHUTTER, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
    esp_sleep_enable_timer_wakeup(10ULL * 1000 * 1000);
    gpio_hold_en((gpio_num_t)PIN_BUZZ);      // keep Q2 off through sleep
    uint32_t t0 = millis();
    esp_light_sleep_start();
    gpio_hold_dis((gpio_num_t)PIN_BUZZ);
    gpio_wakeup_disable((gpio_num_t)PIN_SHUTTER);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
    esp_sleep_wakeup_cause_t c = esp_sleep_get_wakeup_cause();
    if (c == ESP_SLEEP_WAKEUP_GPIO) sleepWakes++; else if (c == ESP_SLEEP_WAKEUP_TIMER) sleepTimer++;
    delay(200);   // let USB come back before printing
    Serial.printf("[slp] woke after %lu ms, cause %d (%s)\n", (unsigned long)(millis() - t0), c,
                  c == ESP_SLEEP_WAKEUP_GPIO ? "button" : c == ESP_SLEEP_WAKEUP_TIMER ? "timer" : "other");
    buzz(2637, 40);
    while (pressedRaw()) delay(5);   // the waking press is not a tap
    delay(30);
    pollButton();
}

// Sleep power sweep, for the bench supply (no USB needed). Five steps, each
// one change more than the last; read the supply while the step number is up.
//   1  light sleep as the self-test does it            (baseline, ~4 mA)
//   2  + unused/unpowered pins pulled down              (camera bus with its
//        rails off, IO15): a floating CMOS input can draw current
//   3  + ADC switched off                               (Arduino keeps it on
//        after the first analogRead)
//   4  + fast oscillators forced off in sleep
//   5  deep sleep: the rest of the board alone, then it reboots
// Light-sleep steps wake on the timer only, so a press can't cut one short.
static const int SWEEP_FLOAT_PINS[] = {39, 41, 42, 40, 38, 47, 21, 13, 48, 10, 12, 14, 11, 9, 18, 8, 15};
constexpr int SWEEP_STEP_S = 15;

static void sweepScreen(int step, const char* what) {
    Lcd::fill(0xFF);
    char big[8];
    snprintf(big, sizeof big, "%d/5", step);
    Lcd::textBig(16, 20, big, 6);
    Lcd::text(16, 130, what);
    Lcd::textf(16, 156, "read the bench current now (%d s)", SWEEP_STEP_S);
    Lcd::text(16, 182, "write it down, the next step follows");
    Lcd::flush();
}

static void sweepLightSleep() {
    Serial.flush();
    delay(300);                                   // let the supply reading settle after the redraw
    esp_sleep_enable_timer_wakeup((uint64_t)SWEEP_STEP_S * 1000 * 1000);
    gpio_hold_en((gpio_num_t)PIN_BUZZ);
    esp_light_sleep_start();
    gpio_hold_dis((gpio_num_t)PIN_BUZZ);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
}

static void sleepSweep() {
    while (pressedRaw()) delay(5);
    Serial.println("[swp] sleep power sweep: 4 light-sleep steps + deep sleep");
    camStop();
    camPinsHighZ();
    digitalWrite(PIN_CAM_EN, LOW);

    sweepScreen(1, "light sleep, as before");
    sweepLightSleep();

    for (int p : SWEEP_FLOAT_PINS) pinMode(p, INPUT_PULLDOWN);
    sweepScreen(2, "+ floating pins pulled down");
    sweepLightSleep();

    pinMode(PIN_VBAT, INPUT);                     // detaches the ADC; the unit is deleted
    sweepScreen(3, "+ ADC off");
    sweepLightSleep();

    esp_sleep_pd_config(ESP_PD_DOMAIN_RC_FAST, ESP_PD_OPTION_OFF);
    esp_sleep_pd_config(ESP_PD_DOMAIN_XTAL, ESP_PD_OPTION_OFF);
    sweepScreen(4, "+ oscillators off");
    sweepLightSleep();
    esp_sleep_pd_config(ESP_PD_DOMAIN_RC_FAST, ESP_PD_OPTION_AUTO);
    esp_sleep_pd_config(ESP_PD_DOMAIN_XTAL, ESP_PD_OPTION_AUTO);

    sweepScreen(5, "deep sleep: board only, then reboot");
    Serial.println("[swp] deep sleep, reboot follows");
    Serial.flush();
    delay(300);
    digitalWrite(PIN_BUZZ, LOW);
    gpio_hold_en((gpio_num_t)PIN_BUZZ);
    gpio_hold_en((gpio_num_t)PIN_LCD_DISP);
    gpio_hold_en((gpio_num_t)PIN_LCD_SCS);
    gpio_deep_sleep_hold_en();
    esp_sleep_enable_timer_wakeup((uint64_t)SWEEP_STEP_S * 1000 * 1000);
    esp_deep_sleep_start();
}

static void drawSummary(bool log) {
    Power p = readPower();
    struct Row { const char* name; int ok; char info[40]; };  // ok: 1 pass, 0 fail, -1 judge yourself
    Row r[] = {
        {"flash 16 MB", flashBytes == 16u << 20, ""},
        {"psram 8 MB", ESP.getPsramSize() >= 8u * 1000 * 1000, ""},
        {"display", -1, ""},
        {"buzzer", -1, ""},
        {"camera sensor", camKnown(), ""},
        {"camera image", frameSeenVaried, ""},
        {"button press", pressCount > 0 && buttonSeenHigh, ""},
        {"wake on button", sleepWakes > 0, ""},
        {"VBAT 3.0-4.35 V", p.vbatMv > 3000 && p.vbatMv < 4350, ""},
        {"VBUS seen", p.vbus, ""},
    };
    snprintf(r[0].info, 40, "%lu MB", (unsigned long)(flashBytes >> 20));
    snprintf(r[1].info, 40, "%lu KB", (unsigned long)(ESP.getPsramSize() >> 10));
    snprintf(r[2].info, 40, "you are reading it");
    snprintf(r[3].info, 40, buzzerPlayed ? "did you hear it?" : "not played");
    snprintf(r[4].info, 40, "%s (PID 0x%04x)", camName(camPid), camPid);
    snprintf(r[5].info, 40, "%lu frames, %lu errors", (unsigned long)camFrames, (unsigned long)camFails);
    snprintf(r[6].info, 40, "%lu presses", (unsigned long)pressCount);
    snprintf(r[7].info, 40, sleepWakes ? "%d wake(s)" : "run the sleep test", sleepWakes);
    snprintf(r[8].info, 40, "%d mV", p.vbatMv);
    snprintf(r[9].info, 40, "%s", chargeState(p));

    Lcd::frame("5/5 SUMMARY", "press: start over  hold: sleep sweep");
    if (log) Serial.println("[sum] ------------------------------");
    int y = 24;
    for (auto& row : r) {
        const char* v = row.ok == 1 ? "PASS" : row.ok == 0 ? "FAIL" : " ?? ";
        Lcd::textf(4, y, "%s %-16s %s", v, row.name, row.info);
        if (row.ok == 0) Lcd::rect(4, y + 15, 32, 1, false);
        if (log) Serial.printf("[sum] %s  %-16s %s\n", v, row.name, row.info);
        y += 19;
    }
    Lcd::flush();
}

// ------------------------------------------------------------ console --
static void serialCommands() {
    while (Serial.available()) {
        char c = Serial.read();
        switch (c) {
            case 'n': enterStage((Stage)((stage + 1) % N_STAGES)); break;
            case 'b': buzzerTune(); buzzerPlayed = true; break;
            case 'p': printPower(); break;
            case 'i': i2cScan(); break;
            case 'q': camStop(); camProbe(); break;
            case 'e': camStop(); camRailHold(); break;
            case 'c':
                camStop();
                camStart();
                if (stage != CAMERA) camStop();   // only the camera stage keeps the rails on
                break;
            case 'r': camFlip = !camFlip; camApplyFlip(); Serial.printf("[cam] rotate 180: %d\n", camFlip); break;
            case 'v': { sensor_t* s = esp_camera_sensor_get(); if (s) { s->set_vflip(s, !s->status.vflip); Serial.printf("[cam] vflip %d\n", s->status.vflip); } } break;
            case 'h': { sensor_t* s = esp_camera_sensor_get(); if (s) { s->set_hmirror(s, !s->status.hmirror); Serial.printf("[cam] hmirror %d\n", s->status.hmirror); } } break;
            case 's': Lcd::clockHz = Lcd::clockHz == 8000000 ? 2000000 : 8000000; Serial.printf("[lcd] SPI %lu Hz\n", (unsigned long)Lcd::clockHz); break;
            case 'd': digitalWrite(PIN_LCD_DISP, !digitalRead(PIN_LCD_DISP)); Serial.printf("[lcd] DISP %d\n", digitalRead(PIN_LCD_DISP)); break;
            case '?':
                Serial.println("n next stage | b buzzer | p power | i I2C scan | c camera restart | q camera bus probe | e hold camera rails on\n"
                               "r rotate 180 | v vflip | h hmirror | s SPI 8/2 MHz | d DISP toggle");
                break;
            default: break;
        }
    }
}

// ----------------------------------------------------------------- main --
void setup() {
    // A deep-sleep test leaves these pins latched; a held pin ignores writes,
    // so release them before anything else (no-ops on a normal boot).
    gpio_deep_sleep_hold_dis();
    for (int p : {PIN_BUZZ, PIN_LCD_DISP, PIN_LCD_SCS}) gpio_hold_dis((gpio_num_t)p);
    // Outputs that must be safe first: buzzer off, camera rails off.
    pinMode(PIN_BUZZ, OUTPUT);   digitalWrite(PIN_BUZZ, LOW);
    pinMode(PIN_CAM_EN, OUTPUT); digitalWrite(PIN_CAM_EN, LOW);
    pinMode(PIN_SHUTTER, INPUT);           // R26 is the pull-up
    pinMode(PIN_VBUS, INPUT);
    pinMode(PIN_CHG, INPUT);
    analogSetPinAttenuation(PIN_VBAT, ADC_11db);

    Serial.begin(115200);
    // Never let the console stall the test. The USB-Serial-JTAG drops off the
    // bus during light sleep; afterwards the host usually still polls the port
    // (so the core thinks it is connected) while no terminal reads it. Then
    // every print waits up to 20 x the TX timeout (100 ms by default) for ring
    // space: up to 2 s per line. With 0 a full buffer just drops the text.
    Serial.setTxTimeoutMs(0);
    uint32_t t0 = millis();
    while (!Serial && millis() - t0 < 2000) delay(10);

    esp_flash_read_id(nullptr, &flashJedec);
    flashBytes = 1u << (flashJedec & 0xFF);
    Serial.println("\nlittle camera v3q self-test");
    Serial.printf("chip ESP32-S3 rev %d | flash JEDEC %06lx = %lu MB | PSRAM %lu KB | heap %lu KB\n",
                  ESP.getChipRevision(), (unsigned long)flashJedec, (unsigned long)(flashBytes >> 20),
                  (unsigned long)(ESP.getPsramSize() >> 10), (unsigned long)(ESP.getFreeHeap() >> 10));
    Serial.println("type ? for commands");

    Lcd::begin();
    Lcd::clearPanel();
    buzz(2637, 40);
    displayPatterns();
    enterStage(DISPLAY_);
}

void loop() {
    serialCommands();
    Btn b = pollButton();

    if (b == Btn::Tap) {
        buzz(3136, 15);
        enterStage((Stage)((stage + 1) % N_STAGES));
    } else if (b == Btn::Hold) {
        buzz(2093, 30);
        switch (stage) {
            case DISPLAY_: displayPatterns(); break;
            case BUZZER:   buzzerTune(); break;
            case CAMERA:   camFlip = !camFlip; camApplyFlip(); Serial.printf("[cam] rotate 180: %d\n", camFlip); break;
            case BUTTON:   sleepTest(); break;
            case SUMMARY:  sleepSweep(); break;
            default: break;
        }
    }

    static uint32_t lastDraw = 0, lastLog = 0;
    uint32_t now = millis();
    if (stage == CAMERA) {
        drawCameraStage();                       // as fast as frames come
        if (now - lastLog >= 2000) {
            lastLog = now;
            Serial.printf("[cam] frames %lu errors %lu  mean %u min %u max %u\n", (unsigned long)camFrames,
                          (unsigned long)camFails, frameMean, frameMin, frameMax);
        }
    } else if (now - lastDraw >= 250) {
        lastDraw = now;
        switch (stage) {
            case DISPLAY_: drawDisplayStage(); break;
            case BUZZER:   drawBuzzerStage(); break;
            case BUTTON:   drawButtonStage(); break;
            case SUMMARY:  drawSummary(false); break;
            default: break;
        }
    }
    Lcd::vcomTick();
}
