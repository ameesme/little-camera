#include "camera.h"
#include "esp_camera.h"

namespace Camera {

// v3q: OV2640 on J2, pins from the netlist. CAM_Dn is the sensor's Y(n+2), so
// CAM_D0 is the driver's d0 and the array is already in driver order.
constexpr int PIN_CAM_EN    = 45;  // enables U4/U5/U6, R12 pull-down
constexpr int PIN_CAM_PWDN  = 11;
constexpr int PIN_CAM_RESET = 9;   // R14/C14 releases it ~1ms after the rails
constexpr int PIN_CAM_XCLK  = 14;
constexpr int PIN_CAM_SIOD  = 18;
constexpr int PIN_CAM_SIOC  = 8;
constexpr int PIN_CAM_VSYNC = 10;
constexpr int PIN_CAM_HREF  = 12;
constexpr int PIN_CAM_PCLK  = 48;
constexpr int PIN_CAM_D[8]  = {39, 41, 42, 40, 38, 47, 21, 13};

static camera_fb_t* fb = nullptr;

// With CAM_EN low the sensor's rails are gone, so nothing may drive into it:
// a driven pin into a dead rail leaks through the ESD diodes.
static void pinsHighZ() {
    const int pins[] = {PIN_CAM_PWDN, PIN_CAM_RESET, PIN_CAM_XCLK, PIN_CAM_SIOD,
                        PIN_CAM_SIOC, PIN_CAM_VSYNC, PIN_CAM_HREF, PIN_CAM_PCLK};
    for (int p : pins) pinMode(p, INPUT);
    for (int p : PIN_CAM_D) pinMode(p, INPUT);
}

bool init() {
    // Rails first, then the driver. 20ms covers the three LDOs coming up and
    // R14/C14 releasing RESET; the driver probes over SCCB immediately after.
    pinMode(PIN_CAM_EN, OUTPUT);
    digitalWrite(PIN_CAM_EN, HIGH);
    delay(20);

    camera_config_t config = {};
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = PIN_CAM_D[0];
    config.pin_d1 = PIN_CAM_D[1];
    config.pin_d2 = PIN_CAM_D[2];
    config.pin_d3 = PIN_CAM_D[3];
    config.pin_d4 = PIN_CAM_D[4];
    config.pin_d5 = PIN_CAM_D[5];
    config.pin_d6 = PIN_CAM_D[6];
    config.pin_d7 = PIN_CAM_D[7];
    config.pin_xclk = PIN_CAM_XCLK;
    config.pin_pclk = PIN_CAM_PCLK;
    config.pin_vsync = PIN_CAM_VSYNC;
    config.pin_href = PIN_CAM_HREF;
    config.pin_sccb_sda = PIN_CAM_SIOD;
    config.pin_sccb_scl = PIN_CAM_SIOC;
    config.pin_pwdn = PIN_CAM_PWDN;
    config.pin_reset = PIN_CAM_RESET;
    config.xclk_freq_hz = 20000000;
    config.frame_size = FRAMESIZE_QVGA;      // 320x240
    config.pixel_format = PIXFORMAT_GRAYSCALE;
    config.grab_mode = CAMERA_GRAB_LATEST;
    config.fb_location = CAMERA_FB_IN_DRAM;  // Internal DRAM, not PSRAM
    config.fb_count = 1;

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("Camera init failed: 0x%x\n", err);
        // Don't leave the rails up behind a driver that isn't there.
        pinsHighZ();
        digitalWrite(PIN_CAM_EN, LOW);
        return false;
    }

    // The module is mounted upside down, so rotate 180 in the sensor: vflip +
    // hmirror together. Done here rather than in the render path because it's
    // free (sensor register writes, no per-frame CPU) and it applies to
    // captures as well as the viewfinder — flipping in the display code would
    // have to be duplicated in both dither paths.
    // Carried over from the previous board; confirm against a v3q with a
    // module in J2, since `selftest` has a rotate toggle for exactly this.
    sensor_t* s = esp_camera_sensor_get();
    if (s) {
        s->set_vflip(s, 1);
        s->set_hmirror(s, 1);
    }

    Serial.println("Camera initialized");
    return true;
}

// Takes the sensor's power away, which the previous board could not do at all
// (see docs/camera_standby.md). CAM_EN gates all three camera LDOs, so this
// removes the ~20mA the biased OV2640 used to draw straight through sleep.
// Costs a full re-init before the viewfinder returns on wake.
void deinit() {
    // The driver owns the framebuffer; hand it back before freeing the driver.
    if (fb) {
        esp_camera_fb_return(fb);
        fb = nullptr;
    }

    esp_err_t err = esp_camera_deinit();
    if (err != ESP_OK) {
        Serial.printf("Camera deinit failed: 0x%x\n", err);
    }

    // Order matters: stop driving the pins before the rails go, not after.
    pinsHighZ();
    digitalWrite(PIN_CAM_EN, LOW);
}

uint8_t* capture() {
    // Return previous framebuffer if any
    if (fb) {
        esp_camera_fb_return(fb);
        fb = nullptr;
    }

    fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("Camera capture failed");
        return nullptr;
    }

    return fb->buf;
}

}
