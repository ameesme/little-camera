//
// Host-side UI preview for src/display.cpp.
//
// Compiles the real rendering code against a stub Arduino/SPI layer, feeds it a
// scene, and dumps what the panel would be showing to a BMP. No hardware, no
// flashing, no waiting for the board to wake up.
//
// See `make help` or run with no arguments for usage.

#include <Arduino.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "../../src/display.h"
#include "../../src/camera.h"
#include "sim_panel.h"

// Pins are arbitrary here; they only have to be distinct so the panel model can
// tell CS and DISP apart.
constexpr uint8_t PIN_SCLK = 7;
constexpr uint8_t PIN_MOSI = 9;
constexpr uint8_t PIN_CS   = 44;
constexpr uint8_t PIN_DISP = 3;

static uint8_t g_source[Camera::WIDTH * Camera::HEIGHT];

// Synthetic 320x240 grayscale scene. Deliberately mixes smooth ramps, flat
// mid-greys and hard edges — those are exactly where Bayer and Floyd-Steinberg
// diverge, so a glance at the output tells you which dither ran.
static void makeTestImage() {
    for (int y = 0; y < Camera::HEIGHT; y++) {
        for (int x = 0; x < Camera::WIDTH; x++) {
            int v;

            if (y < 40) {
                // Horizontal ramp, full black to full white
                v = x * 255 / (Camera::WIDTH - 1);
            } else if (y < 70) {
                // Flat grey steps
                v = (x / 40) * 36;
            } else {
                // Vignetted radial blob plus a couple of hard-edged bars
                float dx = (x - 160) / 150.0f;
                float dy = (y - 160) / 90.0f;
                float d = sqrtf(dx * dx + dy * dy);
                float f = 1.0f - d;
                if (f < 0) f = 0;
                v = (int)(40 + f * 200);
                if (x > 250 && x < 280) v = 0;
                if (x > 290 && x < 320 && y > 120 && y < 180) v = 255;
            }

            g_source[y * Camera::WIDTH + x] = (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
        }
    }
}

// Binary PGM (P5), 8-bit, exactly Camera::WIDTH x Camera::HEIGHT.
static bool loadPGM(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "error: cannot open %s\n", path);
        return false;
    }

    char magic[3] = {0};
    if (fscanf(f, "%2s", magic) != 1 || strcmp(magic, "P5") != 0) {
        fprintf(stderr, "error: %s is not a binary PGM (P5)\n", path);
        fclose(f);
        return false;
    }

    // Skip comments, then read width/height/maxval.
    int dims[3] = {0, 0, 0};
    for (int i = 0; i < 3;) {
        int c = fgetc(f);
        if (c == '#') {
            while (c != '\n' && c != EOF) c = fgetc(f);
        } else if (c >= '0' && c <= '9') {
            ungetc(c, f);
            if (fscanf(f, "%d", &dims[i]) != 1) { fclose(f); return false; }
            i++;
        } else if (c == EOF) {
            fclose(f);
            return false;
        }
    }
    fgetc(f);  // Single whitespace byte before the raster

    if (dims[0] != Camera::WIDTH || dims[1] != Camera::HEIGHT) {
        fprintf(stderr, "error: %s is %dx%d, need %dx%d\n",
                path, dims[0], dims[1], Camera::WIDTH, Camera::HEIGHT);
        fclose(f);
        return false;
    }

    size_t want = sizeof(g_source);
    size_t got = fread(g_source, 1, want, f);
    fclose(f);
    if (got != want) {
        fprintf(stderr, "error: %s truncated (%zu of %zu bytes)\n", path, got, want);
        return false;
    }
    return true;
}

static void usage(const char* argv0) {
    fprintf(stderr,
        "usage: %s <scene> [options]\n"
        "\n"
        "scenes:\n"
        "  viewfinder   live viewfinder, Bayer dither\n"
        "  capture      captured frame, Floyd-Steinberg dither\n"
        "  toast        viewfinder with the 'press to shoot' hint\n"
        "  sleep        sleep face\n"
        "  splash       boot splash\n"
        "\n"
        "options:\n"
        "  --out PATH   output BMP (default preview.bmp)\n"
        "  --scale N    integer upscale, default 2\n"
        "  --src FILE   320x240 binary PGM instead of the synthetic test image\n"
        "  --at MS      virtual clock value at render time, default 0\n",
        argv0);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        usage(argv[0]);
        return 2;
    }

    const char* scene = argv[1];
    const char* out = "preview.bmp";
    const char* src = nullptr;
    int scale = 2;
    uint32_t at = 0;

    for (int i = 2; i < argc; i++) {
        bool hasValue = (i + 1 < argc);
        if (!strcmp(argv[i], "--out") && hasValue) {
            out = argv[++i];
        } else if (!strcmp(argv[i], "--scale") && hasValue) {
            scale = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--src") && hasValue) {
            src = argv[++i];
        } else if (!strcmp(argv[i], "--at") && hasValue) {
            at = (uint32_t)strtoul(argv[++i], nullptr, 10);
        } else {
            fprintf(stderr, "error: unknown or incomplete option '%s'\n\n", argv[i]);
            usage(argv[0]);
            return 2;
        }
    }

    if (src) {
        if (!loadPGM(src)) return 1;
    } else {
        makeTestImage();
    }

    SimPanel::attach(PIN_CS, PIN_DISP);
    Sim::setMillis(0);

    Display::init(PIN_SCLK, PIN_MOSI, PIN_CS, PIN_DISP);

    Sim::setMillis(at);

    if (!strcmp(scene, "viewfinder")) {
        Display::drawViewfinder(g_source, Camera::WIDTH, Camera::HEIGHT);
    } else if (!strcmp(scene, "capture")) {
        Display::drawCapture(g_source, Camera::WIDTH, Camera::HEIGHT);
    } else if (!strcmp(scene, "gallery")) {
        Display::drawGallery(g_source, Camera::WIDTH, Camera::HEIGHT);
    } else if (!strcmp(scene, "toast")) {
        Display::showToast("press to shoot", Display::ToastHAlign::Right,
                           Display::ToastVAlign::Top, false, 0);
        Display::drawViewfinder(g_source, Camera::WIDTH, Camera::HEIGHT);
    } else if (!strcmp(scene, "sleep")) {
        Display::drawSleep();
    } else if (!strcmp(scene, "splash")) {
        Display::drawSplash();
    } else {
        fprintf(stderr, "error: unknown scene '%s'\n\n", scene);
        usage(argv[0]);
        return 2;
    }

    if (!SimPanel::displayEnabled()) {
        fprintf(stderr, "warning: DISP is low — a real panel would show nothing\n");
    }
    if (SimPanel::protocolErrors()) {
        fprintf(stderr, "warning: %u SPI protocol errors\n", SimPanel::protocolErrors());
    }

    if (!SimPanel::writeBMP(out, scale)) {
        fprintf(stderr, "error: cannot write %s\n", out);
        return 1;
    }

    fprintf(stderr, "wrote %s (%dx%d, scale %d)\n",
            out, SimPanel::WIDTH * scale, SimPanel::HEIGHT * scale, scale);
    return 0;
}
