#include "display.h"
#include "hold_progress.h"
#include "splash_data.h"
#include "chirp.h"  // Chirp::band: the face follows the mood in the same four steps as the sounds
#include "ui.h"
#include "little_camera_pals.h"
#include <SPI.h>
#include <math.h>

namespace Display {

static SPIClass* _spi = nullptr;
static uint8_t _cs;
static bool _vcom = false;
static uint32_t _lastVcomToggle = 0;

// Sharp Memory LCD commands (active bits before VCOM is OR'd in)
constexpr uint8_t CMD_WRITE = 0x01;
constexpr uint8_t CMD_CLEAR = 0x04;
constexpr uint8_t CMD_VCOM  = 0x00;

// Bit-reverse table for LSB-first transmission
static const uint8_t REVERSE[256] = {
    0x00,0x80,0x40,0xC0,0x20,0xA0,0x60,0xE0,0x10,0x90,0x50,0xD0,0x30,0xB0,0x70,0xF0,
    0x08,0x88,0x48,0xC8,0x28,0xA8,0x68,0xE8,0x18,0x98,0x58,0xD8,0x38,0xB8,0x78,0xF8,
    0x04,0x84,0x44,0xC4,0x24,0xA4,0x64,0xE4,0x14,0x94,0x54,0xD4,0x34,0xB4,0x74,0xF4,
    0x0C,0x8C,0x4C,0xCC,0x2C,0xAC,0x6C,0xEC,0x1C,0x9C,0x5C,0xDC,0x3C,0xBC,0x7C,0xFC,
    0x02,0x82,0x42,0xC2,0x22,0xA2,0x62,0xE2,0x12,0x92,0x52,0xD2,0x32,0xB2,0x72,0xF2,
    0x0A,0x8A,0x4A,0xCA,0x2A,0xAA,0x6A,0xEA,0x1A,0x9A,0x5A,0xDA,0x3A,0xBA,0x7A,0xFA,
    0x06,0x86,0x46,0xC6,0x26,0xA6,0x66,0xE6,0x16,0x96,0x56,0xD6,0x36,0xB6,0x76,0xF6,
    0x0E,0x8E,0x4E,0xCE,0x2E,0xAE,0x6E,0xEE,0x1E,0x9E,0x5E,0xDE,0x3E,0xBE,0x7E,0xFE,
    0x01,0x81,0x41,0xC1,0x21,0xA1,0x61,0xE1,0x11,0x91,0x51,0xD1,0x31,0xB1,0x71,0xF1,
    0x09,0x89,0x49,0xC9,0x29,0xA9,0x69,0xE9,0x19,0x99,0x59,0xD9,0x39,0xB9,0x79,0xF9,
    0x05,0x85,0x45,0xC5,0x25,0xA5,0x65,0xE5,0x15,0x95,0x55,0xD5,0x35,0xB5,0x75,0xF5,
    0x0D,0x8D,0x4D,0xCD,0x2D,0xAD,0x6D,0xED,0x1D,0x9D,0x5D,0xDD,0x3D,0xBD,0x7D,0xFD,
    0x03,0x83,0x43,0xC3,0x23,0xA3,0x63,0xE3,0x13,0x93,0x53,0xD3,0x33,0xB3,0x73,0xF3,
    0x0B,0x8B,0x4B,0xCB,0x2B,0xAB,0x6B,0xEB,0x1B,0x9B,0x5B,0xDB,0x3B,0xBB,0x7B,0xFB,
    0x07,0x87,0x47,0xC7,0x27,0xA7,0x67,0xE7,0x17,0x97,0x57,0xD7,0x37,0xB7,0x77,0xF7,
    0x0F,0x8F,0x4F,0xCF,0x2F,0xAF,0x6F,0xEF,0x1F,0x9F,0x5F,0xDF,0x3F,0xBF,0x7F,0xFF
};

static inline uint8_t reverseByte(uint8_t b) {
    return REVERSE[b];
}

static void sendByte(uint8_t b) {
    // Sharp LCD requires LSB-first; ESP32 SPI is MSB-first, so bit-reverse
    _spi->transfer(reverseByte(b));
}

static uint8_t makeCommand(uint8_t cmd) {
    // VCOM bit is bit 6 in the command byte
    return cmd | (_vcom ? 0x40 : 0x00);
}

void init(uint8_t sclk, uint8_t mosi, uint8_t cs, uint8_t disp) {
    _cs = cs;

    pinMode(_cs, OUTPUT);
    digitalWrite(_cs, LOW);  // CS active-high, so LOW = deselected

    // DISP is GPIO-driven, and must be HIGH for the panel to show pixels
    pinMode(disp, OUTPUT);
    digitalWrite(disp, HIGH);

    // Use HSPI bus with custom pins
    _spi = new SPIClass(HSPI);
    _spi->begin(sclk, -1, mosi, -1);  // SCLK, MISO (unused), MOSI, SS (unused)
    // 8 MHz: 4x over the LS027B7DH01 datasheet max (2 MHz), but proven stable
    // on this panel since the LilyGO board. Tradeoff: ~13ms vs ~50ms per full
    // frame push — at 2 MHz the extra ~80ms/loop makes capture feel laggy.
    // If the panel ever glitches on the PCB, drop back toward 2 MHz.
    _spi->setFrequency(8000000);
    _spi->setDataMode(SPI_MODE0);

    _lastVcomToggle = millis();

    Serial.printf("Display SPI on SCLK=%d, MOSI=%d, CS=%d, DISP=%d\n", sclk, mosi, cs, disp);

    // Clear display on init
    clear();
}

void clear() {
    digitalWrite(_cs, HIGH);
    delayMicroseconds(6);

    sendByte(makeCommand(CMD_CLEAR));
    sendByte(0x00);  // Trailing dummy byte

    delayMicroseconds(2);
    digitalWrite(_cs, LOW);
}

void fillPattern(uint8_t pattern) {
    digitalWrite(_cs, HIGH);
    delayMicroseconds(6);

    sendByte(makeCommand(CMD_WRITE));

    for (int line = 1; line <= HEIGHT; line++) {
        // Line address (1-indexed, LSB-first — sendByte handles the reversal)
        sendByte(line);

        // Pixel data: 50 bytes per line
        for (int i = 0; i < BYTES_PER_LINE; i++) {
            sendByte(pattern);
        }

        // Trailing dummy byte for this line
        sendByte(0x00);
    }

    // Final trailing dummy byte
    sendByte(0x00);

    delayMicroseconds(2);
    digitalWrite(_cs, LOW);
}

void drawTestPattern() {
    digitalWrite(_cs, HIGH);
    delayMicroseconds(6);

    sendByte(makeCommand(CMD_WRITE));

    for (int line = 1; line <= HEIGHT; line++) {
        sendByte(line);

        // Simple horizontal stripes: alternating white/black every 4 lines
        uint8_t pattern = ((line / 4) % 2 == 0) ? 0xFF : 0x00;

        for (int i = 0; i < BYTES_PER_LINE; i++) {
            sendByte(pattern);
        }

        sendByte(0x00);
    }

    sendByte(0x00);

    delayMicroseconds(2);
    digitalWrite(_cs, LOW);
}

// 4x4 Bayer ordered dither matrix (values 0-15, scaled to 0-255)
static const uint8_t BAYER4[4][4] = {
    {  0, 128,  32, 160},
    {192,  64, 224,  96},
    { 48, 176,  16, 144},
    {240, 112, 208,  80}
};

// Display framebuffer: 400x240 @ 1bpp = 12000 bytes
static uint8_t _framebuffer[BYTES_PER_LINE * HEIGHT];

// 10px rounded corner mask: for each row (0-9), how many pixels from corner to mask
// Computed from (10-x)² + (10-y)² > 100
static const uint8_t CORNER_MASK_10[10] = {10, 7, 5, 4, 3, 3, 2, 1, 1, 0};

// Viewfinder padding
constexpr int VF_PADDING_TOP = 5;
constexpr int VF_PADDING_LEFT = 5;
constexpr int VF_PADDING_RIGHT = 5;
constexpr int VF_PADDING_BOTTOM = 5;
constexpr int VF_CORNER_RADIUS = 10;

// Floyd-Steinberg error buffers for 320px wide image + 2 for boundary
static int16_t _errCurr[322];
static int16_t _errNext[322];

// The captured photo, dithered once and kept as 1-bit (see display.h for the
// geometry). The capture -> save slide re-blits this at a new offset every
// frame; re-running Floyd-Steinberg per frame would roughly double the cost of
// a frame for an identical result, since the dither loop works in image
// coordinates and doesn't care where the photo lands on the panel.
static uint8_t _photoBits[PHOTO_BYTES * PHOTO_HEIGHT];  // 9600 bytes

const uint8_t* photoBits() { return _photoBits; }

// UI state
static int _batteryPercent = 100;
static bool _charging = false;
static bool _usb = false;
static Prompt _prompt = Prompt::None;
static int _photoCount = 0;
static UI::SignalLevel _signalLevel = UI::SignalLevel::Full;
static bool _showHints = true;
static uint8_t _batteryBars = 4;

// Toast state
static char _toastText[64] = {0};
static ToastHAlign _toastHAlign = ToastHAlign::Center;
static ToastVAlign _toastVAlign = ToastVAlign::Top;
static bool _toastInverted = false;
static uint32_t _toastExpireAt = 0;  // 0 = no toast, UINT32_MAX = indefinite
static bool _toastActive = false;

// Sidebar layout constants
constexpr int SIDEBAR_PADDING = 5;
constexpr int BOX_WIDTH = 70;
// Inbox is the only sidebar box for now (battery/signal pulled until there's a
// real source for either), so it spans the full panel height minus padding.
constexpr int INBOX_HEIGHT = HEIGHT - SIDEBAR_PADDING * 2;
constexpr int BOX_RADIUS = 10;

// Review-screen action column: mirrors the sidebar on the opposite edge, split
// into two stacked buttons. Gap is 6 so the two halves divide the column
// height exactly and the stack stays symmetric top to bottom.
constexpr int ACTION_GAP = 6;
constexpr int ACTION_BUTTON_HEIGHT = (INBOX_HEIGHT - ACTION_GAP) / 2;

// Photo card geometry. On the viewfinder and capture screens the photo starts
// just right of the sidebar; on the save screen it sits at the left margin.
// It keeps a 5px gap from whatever is on either side, so it travels 75px, not
// the full 70px column plus both gaps.
constexpr int PHOTO_X_CAPTURE = SIDEBAR_PADDING + BOX_WIDTH + SIDEBAR_PADDING;  // 80
constexpr int SAVE_SLIDE_PX = PHOTO_X_CAPTURE - VF_PADDING_LEFT;               // 75
// 320px of source minus the 5 columns cropped to leave a margin on the far side
constexpr int CARD_WIDTH = PHOTO_WIDTH - VF_PADDING_RIGHT;                     // 315
// The gesture sits in a knocked-out white strip along the bottom of the button,
// so it reads as a separate affordance rather than a third line of the title.
// It's centred horizontally and every gesture word is far narrower than the
// box, so the 10px corners never reach it.
constexpr int BOX_FOOTER_PADDING = 4;
constexpr int BOX_LABEL_GAP = 3;  // Icon to label
// The gesture sits on the same black as the rest of the button, divided off by
// a 1px rule. Every attempt at a lighter footer — white, or a grey dither —
// either cost too much legibility at this font size or fought the photo next to
// it; a hairline gets the separation for none of the contrast.
constexpr int BOX_DIVIDER_HEIGHT = 1;

// Default font for sidebar UI
constexpr UI::Font SIDEBAR_FONT = UI::Font::Large;
// The buttons carry a second line naming the gesture, which needs the small face
constexpr UI::Font HINT_FONT = UI::Font::Small;

// Toast layout constants
// Update screen progress bar
constexpr int UPDATE_BAR_HEIGHT = 20;
constexpr int UPDATE_BAR_RADIUS = 6;
constexpr int UPDATE_BAR_PADDING = 3;   // Between the outline and the fill
constexpr int UPDATE_MARK_SCALE = 3;    // Sparkles above the line: 72px, the same weight as the text
constexpr int UPDATE_MARK_GAP = 14;
// Where the last drawUpdate() put its bar, so drawUpdateProgress() can repaint
// that strip alone. -1 = the screen currently up has no bar.
static int _updateBarX = -1;
static int _updateBarY = -1;
static int _updateBarW = 0;

constexpr int TOAST_PADDING_H = 10;   // Horizontal padding inside toast
constexpr int TOAST_PADDING_V = 6;    // Vertical padding inside toast
constexpr int TOAST_MARGIN = 8;       // Margin from screen edges
constexpr int TOAST_RADIUS = 8;       // Corner radius
constexpr int TOAST_MIN_X = 80;       // Don't overlap sidebar
constexpr UI::Font TOAST_FONT = UI::Font::Large;

// Set pixel in framebuffer (0=black, 1=white)
// Columns setPixel may touch. The panel by default; the face block narrows it
// so a character sliding out of the box cannot paint over the photo.
static int _clipX0 = 0, _clipX1 = WIDTH;

static void setPixel(int x, int y, bool white) {
    if (x < _clipX0 || x >= _clipX1 || y < 0 || y >= HEIGHT) return;
    int byteIdx = y * BYTES_PER_LINE + x / 8;
    int bitIdx = 7 - (x % 8);
    if (white) {
        _framebuffer[byteIdx] |= (1 << bitIdx);
    } else {
        _framebuffer[byteIdx] &= ~(1 << bitIdx);
    }
}

// Check if point is inside rounded corner mask
static bool isInsideRoundedRect(int px, int py, int x, int y, int w, int h, int r) {
    // Check if point is outside the rectangle entirely
    if (px < x || px >= x + w || py < y || py >= y + h) return false;

    // Check corners
    int cx, cy;

    // Top-left corner
    if (px < x + r && py < y + r) {
        cx = x + r; cy = y + r;
        int dx = px - cx, dy = py - cy;
        return (dx * dx + dy * dy) <= (r * r);
    }
    // Top-right corner
    if (px >= x + w - r && py < y + r) {
        cx = x + w - r - 1; cy = y + r;
        int dx = px - cx, dy = py - cy;
        return (dx * dx + dy * dy) <= (r * r);
    }
    // Bottom-left corner
    if (px < x + r && py >= y + h - r) {
        cx = x + r; cy = y + h - r - 1;
        int dx = px - cx, dy = py - cy;
        return (dx * dx + dy * dy) <= (r * r);
    }
    // Bottom-right corner
    if (px >= x + w - r && py >= y + h - r) {
        cx = x + w - r - 1; cy = y + h - r - 1;
        int dx = px - cx, dy = py - cy;
        return (dx * dx + dy * dy) <= (r * r);
    }

    return true;
}

// Draw filled rounded rectangle
static void fillRoundedRect(int x, int y, int w, int h, int r, bool white) {
    for (int py = y; py < y + h; py++) {
        for (int px = x; px < x + w; px++) {
            if (isInsideRoundedRect(px, py, x, y, w, h, r)) {
                setPixel(px, py, white);
            }
        }
    }
}

// Draw rounded rectangle border (1px)
static void drawRoundedRectBorder(int x, int y, int w, int h, int r, bool white) {
    for (int py = y; py < y + h; py++) {
        for (int px = x; px < x + w; px++) {
            bool inside = isInsideRoundedRect(px, py, x, y, w, h, r);
            bool insideInner = isInsideRoundedRect(px, py, x + 1, y + 1, w - 2, h - 2, r - 1);
            if (inside && !insideInner) {
                setPixel(px, py, white);
            }
        }
    }
}

// Draw icon at position
static void drawIcon(const uint8_t* icon, int iconW, int iconH, int x, int y, bool white) {
    int bytesPerRow = (iconW + 7) / 8;
    for (int row = 0; row < iconH; row++) {
        for (int col = 0; col < iconW; col++) {
            int byteIdx = row * bytesPerRow + col / 8;
            int bitIdx = 7 - (col % 8);
            if (icon[byteIdx] & (1 << bitIdx)) {
                setPixel(x + col, y + row, white);
            }
        }
    }
}

// The same icon, each pixel a scale x scale block. The update screen wants the
// mark big enough to read across a room, like the pairing code.
static void drawIconScaled(const uint8_t* icon, int iconW, int iconH, int x, int y, bool white, int scale) {
    const int bytesPerRow = (iconW + 7) / 8;
    for (int row = 0; row < iconH; row++) {
        for (int col = 0; col < iconW; col++) {
            if (!(icon[row * bytesPerRow + col / 8] & (1 << (7 - (col % 8))))) continue;
            for (int dy = 0; dy < scale; dy++)
                for (int dx = 0; dx < scale; dx++) setPixel(x + col * scale + dx, y + row * scale + dy, white);
        }
    }
}

// Draw icon centered horizontally in box at given Y
static void drawIconCentered(const uint8_t* icon, int iconW, int iconH, int boxX, int boxW, int y, bool white) {
    int startX = boxX + (boxW - iconW) / 2;
    drawIcon(icon, iconW, iconH, startX, y, white);
}

// Draw text at position with specified font and alignment
static void drawText(const char* text, int x, int y, UI::Font font, bool white) {
    int charW = UI::fontWidth(font);
    int charH = UI::fontHeight(font);

    for (int i = 0; text[i]; i++) {
        const uint8_t* glyph = UI::getGlyph(text[i], font);

        for (int row = 0; row < charH; row++) {
            uint8_t rowData = glyph[row];
            for (int col = 0; col < charW; col++) {
                if (rowData & (1 << (7 - col))) {
                    setPixel(x + i * (charW + 1) + col, y + row, white);
                }
            }
        }
    }
}

// Draw text centered in box at given Y position
// Each glyph pixel becomes a scale x scale block. For the pairing screen,
// where the code has to be readable at arm's length on a phone's terms.
static void drawTextScaled(const char* text, int x, int y, UI::Font font, bool white, int scale) {
    const int charW = UI::fontWidth(font);
    const int charH = UI::fontHeight(font);
    for (int i = 0; text[i]; i++) {
        const uint8_t* glyph = UI::getGlyph(text[i], font);
        for (int row = 0; row < charH; row++) {
            const uint8_t rowData = glyph[row];
            for (int col = 0; col < charW; col++) {
                if (!(rowData & (1 << (7 - col)))) continue;
                const int px = x + (i * (charW + 1) + col) * scale;
                const int py = y + row * scale;
                for (int dy = 0; dy < scale; dy++)
                    for (int dx = 0; dx < scale; dx++) setPixel(px + dx, py + dy, white);
            }
        }
    }
}

static void drawTextCenteredAt(const char* text, int boxX, int boxW, int y, UI::Font font, bool white) {
    int textW = UI::textWidth(text, font);
    int startX = boxX + (boxW - textW) / 2;
    drawText(text, startX, y, font, white);
}

// The sleeping face is 16px pixel art, white on black: two closed eyes and a
// mouth, centred on the panel. It follows the mood (docs/mood.md) in the same
// four bands as the chirps: happy sleeps with a smile, content is the original
// face (a dot for a mouth), glum goes flat, sad turns the eyes over and the
// mouth down. `breathIn` is the second frame of a two-frame breath: eyes up a
// touch, mouth down a touch, so the distance between them grows and shrinks
// every 5s while it sleeps. The block size is a parameter because the
// viewfinder's sidebar draws the same face at a fifth of the size.
namespace Face {
constexpr int B = 16;             // Block size on the sleep screen
constexpr int EYE_L_X = 96;       // Left eye's left corner block
constexpr int EYE_R_X = 240;      // Right eye's left corner block
constexpr int EYE_Y = 96;         // Top row of a happy (u-shaped) eye; a flat eye sits a block lower
constexpr int MOUTH_X = 176;      // Left block of a three-block mouth; the square sits in the middle
constexpr int MOUTH_Y = 144;
constexpr int BREATH_EYES_DY = -4;
constexpr int BREATH_MOUTH_DY = 8;

static void block(int x, int y, int b) { fillRoundedRect(x, y, b, b, 0, true); }

// A closed eye. Happy is a u, corners lifted off the line; everything else is
// the same four blocks flat.
static void eye(int x, int y, bool happy, int b = B) {
    const int lo = y + b;
    block(x, happy ? y : lo, b);
    block(x + 3 * b, happy ? y : lo, b);
    block(x + b, lo, b);
    block(x + 2 * b, lo, b);
}

static void awakeEye(int center, int y, uint8_t band, bool left, int b = B) {
    const int x = center - 3 * b / 2;
    if (band == 3) {
        block(x + b, y, b);
        block(x, y + b, b);
        block(x + 2 * b, y + b, b);
    } else if (band == 2) {
        fillRoundedRect(center - b / 2, y,
                        b, 2 * b, 0, true);
    } else if (band == 1) {
        fillRoundedRect(center - b, y + b,
                        2 * b, b, 0, true);
    } else {
        fillRoundedRect(x + (left ? b : 0), y,
                        2 * b, b, 0, true);
        block(x + (left ? 0 : 2 * b), y + b, b);
    }
}

static void mouth(int x, int y, uint8_t band, int b = B) {
    switch (band) {
        case 3:  // Happy: a wide smile, flat with both ends lifted. Wider than
                 // the three the others get, and wider than an eye, or at the
                 // size the viewfinder draws this it reads as a third eye.
            block(x - b, y, b);
            block(x, y + b, b);
            block(x + b, y + b, b);
            block(x + 2 * b, y + b, b);
            block(x + 3 * b, y, b);
            break;
        case 2:  // Content: a square under flat eyes
            block(x + b, y, b);
            break;
        case 1:  // Glum: the square stretches into a line
            block(x, y, b);
            block(x + b, y, b);
            block(x + 2 * b, y, b);
            break;
        default:  // Sad: corners down
            block(x, y + b, b);
            block(x + 2 * b, y + b, b);
            block(x + b, y, b);
            break;
    }
}

// The whole face inside a box, at whatever block size fits it. Used by the
// sidebar; the sleep screen places its own blocks at panel coordinates.
static void mini(int x, int y, int w, int h, uint8_t band, bool breathIn, int b) {
    const int eyesW = 4 * b + 2 * b + 4 * b;  // Two eyes and the gap between them
    const int eyeX = x + (w - eyesW) / 2;
    // Eyes, two blocks of air, mouth. The air is what keeps a smile from
    // reading as a third eyebrow at a block size this small.
    const int rows = 6 * b;
    const int top = y + (h - rows) / 2;
    const int eyeY = top + (breathIn ? 0 : 1);
    const int mouthY = top + 4 * b - (breathIn ? 0 : 1);
    awakeEye(eyeX + 2 * b, eyeY, band, true, b);
    awakeEye(eyeX + 8 * b, eyeY, band, false, b);
    mouth(x + (w - 3 * b) / 2, mouthY, band, b);
}
}  // namespace Face

// A black box with a centred icon and a label hung under it, plus a footer
// naming the gesture that triggers it, ruled off by a hairline. Shared by the
// viewfinder sidebar and the save-screen actions so the two stay identical.
struct ButtonState {
    int x = 0, y = 0, w = 0, h = 0, filledPixels = 0;
    const uint8_t* icon = nullptr;
};
static ButtonState _holdButton, _pressButton;
static const uint8_t* _completedHoldIcon = nullptr;
static bool _completedHoldVisible = false;
static const uint8_t* _pressFlashIcon = nullptr;
static uint32_t _pressFlashUntil = 0;
constexpr uint32_t PRESS_FLASH_MS = 120;
static int _pendingHoldPixels = 0;

static void clearButtonFeedback() {
    _holdButton = {};
    _pressButton = {};
    _completedHoldIcon = nullptr;
    _pressFlashIcon = nullptr;
    _pendingHoldPixels = 0;
}

static bool pressFlashActive() {
    return _pressFlashIcon && (int32_t)(_pressFlashUntil - millis()) > 0;
}

static void invertButtonColumns(int x, int y, int w, int h, int from, int to) {
    for (int py = y; py < y + h; py++) {
        for (int px = x + from; px < x + to; px++) {
            if (px < 0 || px >= WIDTH) continue;
            if (isInsideRoundedRect(px, py, x, y, w, h, BOX_RADIUS) &&
                isInsideRoundedRect(px, py, x + 1, y + 1, w - 2, h - 2, BOX_RADIUS - 1)) {
                _framebuffer[py * BYTES_PER_LINE + px / 8] ^= (0x80 >> (px % 8));
            }
        }
    }
}

static void drawBoxButton(int x, int y, int w, int h,
                          const uint8_t* icon, const char* label, const char* gesture,
                          bool feedback = true) {
    // setPixel clips, so drawing off-panel is harmless — but during the slide
    // one column is always fully off-screen, and skipping it saves ~16k
    // no-op setPixel calls per frame.
    if (x + w <= 0 || x >= WIDTH) return;

    const int footerH = UI::fontHeight(HINT_FONT) + BOX_FOOTER_PADDING * 2;
    const int footerY = y + h - footerH;

    fillRoundedRect(x, y, w, h, BOX_RADIUS, false);

    // Rule between the body and the gesture footer. Masked against the same
    // rounded rect so it stops at the button's edge instead of overhanging it.
    for (int py = footerY; py < footerY + BOX_DIVIDER_HEIGHT; py++) {
        for (int px = x; px < x + w; px++) {
            if (isInsideRoundedRect(px, py, x, y, w, h, BOX_RADIUS)) {
                setPixel(px, py, true);
            }
        }
    }

    // The icon is the anchor: it centres in the body — the box above the rule —
    // and the label hangs off it. Centring the icon+label block as a whole is
    // more correct on paper but reads as too high, because the eye takes the
    // icon for the centre and the label for a caption.
    const int bodyH = h - footerH;
    int iconY = y + (bodyH - UI::ICON_SIZE) / 2;
    int labelY = iconY + UI::ICON_SIZE + BOX_LABEL_GAP;
    int gestureY = footerY + BOX_FOOTER_PADDING;

    drawIconCentered(icon, UI::ICON_SIZE, UI::ICON_SIZE, x, w, iconY, true);
    drawTextCenteredAt(label, x, w, labelY, SIDEBAR_FONT, true);
    drawTextCenteredAt(gesture, x, w, gestureY, HINT_FONT, true);
    drawRoundedRectBorder(x, y, w, h, BOX_RADIUS, false);
    if (!strcmp(gesture, "hold")) {
        const bool completed = feedback && icon == _completedHoldIcon;
        if (completed) {
            invertButtonColumns(x, y, w, h, 0, w);
            _completedHoldVisible = true;
        }
        if (x >= 0 && x + w <= WIDTH) _holdButton = {x, y, w, h, completed ? w : 0, icon};
    } else if (!strcmp(gesture, "press")) {
        const bool flashed = feedback && icon == _pressFlashIcon && pressFlashActive();
        if (flashed) invertButtonColumns(x, y, w, h, 0, w);
        if (x >= 0 && x + w <= WIDTH) _pressButton = {x, y, w, h, flashed ? w : 0, icon};
    }
}

// The face block above the sidebar button: the same face the camera sleeps
// with, so the thing in your hands looks back at you while you aim it. One
// toast tall, which is as much as the photos button can spare.
constexpr int FACE_BLOCK_HEIGHT = SPLEEN8X16_HEIGHT + TOAST_PADDING_V * 2 + 12;
constexpr int FACE_BLOCK_GAP = ACTION_GAP;
constexpr int FACE_BLOCK_B = 3;  // Block size of the miniature, a fifth of the sleep face
// Breathing follows the clock so restoring the static sidebar never freezes the face.
constexpr uint32_t FACE_BLOCK_BREATH_MS = 2500;

// The mood the face wears while awake, as the last telemetry read it. The
// panel has no business asking mood.h anything, so main.cpp pushes it here.
static uint8_t _faceBand = 3;

void setFaceBand(uint8_t band) {
    _faceBand = band > 3 ? 3 : band;
}

// Companion pal — which one lives in the viewfinder and on the sleep screen.
static uint8_t _palId = 0;
constexpr uint32_t PAL_SWAP_MS = 5000;

void setPal(uint8_t palId) { _palId = palId; }

struct PalInfo {
    const pal_dim_t* dim;
    const pal_anim_t* idle;
    const char* name;
};

// Sir Croaks' generated idle is one still frame, so it looked switched off next
// to the others. This one sits, blinks, sits, and hops, from his own frames.
static const pal_step_t PAL_CROAKS_LIVELY_STEPS[] = {
    {PAL_CROAKS_BMP_IDLE_A, 0, 0, 1600},
    {PAL_CROAKS_BMP_BLINK, 0, 0, 150},
    {PAL_CROAKS_BMP_IDLE_A, 0, 0, 1300},
    {PAL_CROAKS_BMP_CROUCH, 0, 0, 150},
    {PAL_CROAKS_BMP_JUMP, 0, -3, 200},
    {PAL_CROAKS_BMP_JUMP, 0, -1, 100},
    {PAL_CROAKS_BMP_CROUCH, 0, 0, 120},
};
static const pal_anim_t PAL_CROAKS_ANIM_LIVELY = {PAL_CROAKS_LIVELY_STEPS, 7, 1};

static PalInfo palLookup(uint8_t id) {
    switch (id) {
        case 1: return {&PAL_MOCHI_DIM, &PAL_MOCHI_ANIM_IDLE, "mochi"};
        case 2: return {&PAL_CROAKS_DIM, &PAL_CROAKS_ANIM_LIVELY, "sir croaks"};
        case 3: return {&PAL_TWEET_DIM, &PAL_TWEET_ANIM_IDLE, "tweet seat"};
        case 4: return {&PAL_PILLOWCASE_DIM, &PAL_PILLOWCASE_ANIM_IDLE, "pillowcase"};
        default: return {nullptr, nullptr, ""};
    }
}

static const uint8_t* palIdleFrame(const pal_anim_t* anim, uint32_t ms,
                                    int8_t* outDx = nullptr, int8_t* outDy = nullptr) {
    uint32_t total = 0;
    for (int i = 0; i < anim->n; i++) total += anim->steps[i].ms;
    if (total == 0) { if (outDx) *outDx = 0; if (outDy) *outDy = 0; return anim->steps[0].bmp; }
    uint32_t t = ms % total;
    uint32_t acc = 0;
    for (int i = 0; i < anim->n; i++) {
        acc += anim->steps[i].ms;
        if (t < acc) {
            if (outDx) *outDx = anim->steps[i].dx;
            if (outDy) *outDy = anim->steps[i].dy;
            return anim->steps[i].bmp;
        }
    }
    if (outDx) *outDx = 0;
    if (outDy) *outDy = 0;
    return anim->steps[0].bmp;
}

static void drawPalBmp(const uint8_t* bmp, uint8_t bmpW, uint8_t bmpH, uint8_t bpr,
                       int x, int y, int scale, bool white) {
    for (int row = 0; row < bmpH; row++) {
        for (int col = 0; col < bmpW; col++) {
            if (!(bmp[row * bpr + col / 8] & (0x80 >> (col & 7)))) continue;
            for (int sy = 0; sy < scale; sy++)
                for (int sx = 0; sx < scale; sx++)
                    setPixel(x + col * scale + sx, y + row * scale + sy, white);
        }
    }
}

// Pal care, as main.cpp last reported it: the bar, the food in hand, the tombstone.
static uint8_t _palLife = 255;
static uint8_t _palFood = 0;
static bool _palDead = false;

// Each pal eats what its kind eats. Drawn facing left, towards the pal.
// Mochi the cat: a fish.
static const uint8_t FOOD_FISH_BMP[] = {0x3c, 0x40, 0x7e, 0xc0, 0xbf, 0xc0, 0xff, 0xc0, 0x7e, 0xc0, 0x3c, 0x40};
// Sir Croaks the frog: a fly.
static const uint8_t FOOD_FLY_BMP[] = {0x63, 0x00, 0x94, 0x80, 0x88, 0x80, 0x6b, 0x00, 0x1c, 0x00, 0x3e, 0x00, 0x1c, 0x00};
// Tweet Seat the bird: a worm.
static const uint8_t FOOD_WORM_BMP[] = {0x00, 0x60, 0x30, 0xf0, 0x79, 0xb0, 0xcf, 0x00, 0x86, 0x00};
// Pillowcase, a pillow with a face: a feather, what pillows are stuffed with.
static const uint8_t FOOD_FEATHER_BMP[] = {0x07, 0x0f, 0x1e, 0x2c, 0x58, 0x30, 0x40, 0x80};

struct PalFood {
    const uint8_t* bmp;
    uint8_t w, h, bpr;
    const char* name;
};

static PalFood palFood(uint8_t id) {
    switch (id) {
        case 1: return {FOOD_FISH_BMP, 10, 6, 2, "fish"};
        case 2: return {FOOD_FLY_BMP, 9, 7, 2, "fly"};
        case 3: return {FOOD_WORM_BMP, 12, 5, 2, "worm"};
        case 4: return {FOOD_FEATHER_BMP, 8, 8, 1, "feather"};
        default: return {nullptr, 0, 0, 0, ""};
    }
}

const char* palFoodName(uint8_t palId) { return palFood(palId).name; }

// Where a pal used to be: a headstone with a cross cut out of it.
static const uint8_t PAL_TOMB_BMP[] = {0x3f, 0x00, 0x7f, 0x80, 0xff, 0xc0, 0xf3, 0xc0, 0xe1, 0xc0, 0xf3, 0xc0,
                                       0xf3, 0xc0, 0xff, 0xc0, 0xff, 0xc0, 0xff, 0xc0, 0xff, 0xc0, 0xff, 0xc0};
constexpr uint8_t PAL_TOMB_W = 10, PAL_TOMB_H = 12, PAL_TOMB_BPR = 2;
// Rises over a pal that has just eaten.
static const uint8_t PAL_HEART_BMP[] = {0x6c, 0xfe, 0xfe, 0x7c, 0x38, 0x10};
constexpr uint8_t PAL_HEART_W = 7, PAL_HEART_H = 6;

static void drawPalInBox(int boxX, int boxY, int boxW, int boxH, uint8_t id, int scale, bool white) {
    if (_palDead && id == _palId) {
        const int tw = PAL_TOMB_W * scale, th = PAL_TOMB_H * scale;
        drawPalBmp(PAL_TOMB_BMP, PAL_TOMB_W, PAL_TOMB_H, PAL_TOMB_BPR,
                   boxX + (boxW - tw) / 2, boxY + (boxH - th) / 2, scale, white);
        return;
    }
    PalInfo info = palLookup(id);
    if (!info.dim || !info.idle) return;
    int8_t dx = 0, dy = 0;
    const uint8_t* bmp = palIdleFrame(info.idle, millis(), &dx, &dy);
    int pw = info.dim->w * scale, ph = info.dim->h * scale;
    int x = boxX + (boxW - pw) / 2 + dx * scale;
    int y = boxY + (boxH - ph) / 2 + dy * scale;
    drawPalBmp(bmp, info.dim->w, info.dim->h, info.dim->bpr, x, y, scale, white);
}

// Badge behind the pal: a 12-point star with every corner filleted, tips and
// dips alike. A cosine edge reads as a flower and a triangle wave as spikes at
// this size, so the edge is the real shape's radius over half a lobe (tip to
// dip, 0-255 of the tip radius), from tools/preview/badge_profile.py.
static const uint8_t PAL_BADGE_PROFILE[] = {
    255, 255, 255, 254, 254, 253, 252, 251, 250, 249, 247, 245, 243, 241, 239, 237, 235,
    233, 232, 230, 228, 227, 225, 224, 222, 221, 220, 219, 218, 218, 218, 217, 217,
};
constexpr int PAL_BADGE_STEPS = sizeof(PAL_BADGE_PROFILE) - 1;
constexpr int PAL_BADGE_R = 78;
constexpr int PAL_BADGE_POINTS = 12;
constexpr uint32_t PAL_BADGE_TURN_MS = 30000;  // One revolution: a slow turn, not a spin

// Pixels [x0, x1) of row y, a byte at a time where it can.
static void fillSpan(int y, int x0, int x1, bool white) {
    if (y < 0 || y >= HEIGHT) return;
    if (x0 < 0) x0 = 0;
    if (x1 > WIDTH) x1 = WIDTH;
    if (x0 >= x1) return;
    uint8_t* row = _framebuffer + y * BYTES_PER_LINE;
    const int b0 = x0 >> 3, b1 = (x1 - 1) >> 3;
    uint8_t m0 = 0xFF >> (x0 & 7);
    const uint8_t m1 = 0xFF << (7 - ((x1 - 1) & 7));
    if (b0 == b1) m0 &= m1;
    if (white) row[b0] |= m0; else row[b0] &= ~m0;
    if (b0 == b1) return;
    if (b1 - b0 > 1) memset(row + b0 + 1, white ? 0xFF : 0x00, b1 - b0 - 1);
    if (white) row[b1] |= m1; else row[b1] &= ~m1;
}

// atan2 to ~0.005 rad (under half a pixel at the badge's tips) for a fraction
// of atan2f's cost: one divide and a short polynomial.
static float fastAtan2(float y, float x) {
    const float ax = fabsf(x), ay = fabsf(y);
    const float mx = ax > ay ? ax : ay;
    if (mx == 0.0f) return 0.0f;
    const float t = (ax < ay ? ax : ay) / mx;
    const float t2 = t * t;
    float r = ((-0.0464964749f * t2 + 0.15931422f) * t2 - 0.327622764f) * t2 * t + t;
    if (ay > ax) r = 1.57079637f - r;
    if (x < 0) r = 3.14159274f - r;
    return y < 0 ? -r : r;
}

// Only the ring the lobes live in needs an angle: everything inside the deepest
// dip is a solid span. That is ~5k angles a frame instead of ~25k.
static void drawPalBadge(int cx, int cy, float turn) {
    const float lobesPerRad = PAL_BADGE_POINTS / 6.2831853f;
    const float rOut = PAL_BADGE_R;
    const float rIn = PAL_BADGE_PROFILE[PAL_BADGE_STEPS] * (PAL_BADGE_R / 255.0f) - 1.0f;
    for (int py = cy - PAL_BADGE_R; py <= cy + PAL_BADGE_R; py++) {
        const float fy = py - cy + 0.5f;
        if (fabsf(fy) > rOut) continue;
        const int wo = (int)sqrtf(rOut * rOut - fy * fy) + 1;
        int wi = -1;
        if (fabsf(fy) < rIn) {
            wi = (int)sqrtf(rIn * rIn - fy * fy);
            fillSpan(py, cx - wi, cx + wi, false);
        }
        uint8_t* row = _framebuffer + py * BYTES_PER_LINE;
        for (int side = 0; side < 2; side++) {
            const int from = side ? (wi >= 0 ? cx + wi : cx - wo) : cx - wo;
            const int to = side ? cx + wo : (wi >= 0 ? cx - wi : cx - wo);
            for (int px = from; px < to; px++) {
                if (px < 0 || px >= WIDTH) continue;
                const float fx = px - cx + 0.5f;
                const float d2 = fx * fx + fy * fy;
                if (d2 > rOut * rOut) continue;
                float t = (fastAtan2(fy, fx) + turn) * lobesPerRad;
                t -= floorf(t);
                float f = 2.0f * t;  // 0 at a tip, 1 at a dip, back to 0 at the next tip
                if (f > 1.0f) f = 2.0f - f;
                const float r = PAL_BADGE_PROFILE[(int)(f * PAL_BADGE_STEPS + 0.5f)] * (PAL_BADGE_R / 255.0f);
                if (d2 <= r * r) row[px >> 3] &= ~(0x80 >> (px & 7));
            }
        }
    }
}

static void flushLines(int fromY, int toY);
static void flushFramebuffer();
static void renderToast();

constexpr int PAL_DIALOG_SCALE = 7;

struct PalDialogLayout {
    int cardX, cardY, cardW, cardH, cx, cy;
};

static PalDialogLayout palDialogLayout() {
    PalDialogLayout l;
    l.cardX = VF_PADDING_LEFT;
    l.cardY = VF_PADDING_TOP;
    l.cardW = CARD_WIDTH;
    l.cardH = HEIGHT - VF_PADDING_TOP - VF_PADDING_BOTTOM;
    // Badge + pal, centred between the title and the name
    const int contentTop = l.cardY + 34;
    const int contentBot = l.cardY + l.cardH - 30;
    l.cx = l.cardX + l.cardW / 2;
    l.cy = (contentTop + contentBot) / 2;
    return l;
}

// The only part of the screen that moves: the badge's square and what is in it.
static void drawPalDialogArt(const PalInfo& info, const PalDialogLayout& l, uint32_t ms) {
    for (int y = l.cy - PAL_BADGE_R; y <= l.cy + PAL_BADGE_R; y++)
        fillSpan(y, l.cx - PAL_BADGE_R, l.cx + PAL_BADGE_R + 1, true);
    drawPalBadge(l.cx, l.cy, (ms % PAL_BADGE_TURN_MS) * (6.2831853f / PAL_BADGE_TURN_MS));

    int8_t dx = 0, dy = 0;
    const uint8_t* bmp = palIdleFrame(info.idle, ms, &dx, &dy);
    const int s = PAL_DIALOG_SCALE;
    const int x0 = l.cx - info.dim->w * s / 2 + dx * s;
    const int y0 = l.cy - info.dim->h * s / 2 + dy * s;
    for (int row = 0; row < info.dim->h; row++) {
        for (int col = 0; col < info.dim->w; col++) {
            if (!(bmp[row * info.dim->bpr + col / 8] & (0x80 >> (col & 7)))) continue;
            for (int sy = 0; sy < s; sy++) fillSpan(y0 + row * s + sy, x0 + col * s, x0 + (col + 1) * s, true);
        }
    }
}

void drawPalDialog(uint8_t palId, uint32_t ms) {
    _holdButton = {};
    _pressButton = {};
    _completedHoldVisible = false;
    memset(_framebuffer, 0xFF, sizeof(_framebuffer));

    PalInfo info = palLookup(palId);
    if (!info.dim) { flushFramebuffer(); return; }
    const PalDialogLayout l = palDialogLayout();

    drawRoundedRectBorder(l.cardX, l.cardY, l.cardW, l.cardH, VF_CORNER_RADIUS, false);
    // One line, so the badge gets the height a second line would take.
    drawTextCenteredAt("you got a little pal!", l.cardX, l.cardW, l.cardY + 14, SIDEBAR_FONT, false);
    drawPalDialogArt(info, l, ms);
    drawTextCenteredAt(info.name, l.cardX, l.cardW, l.cardY + l.cardH - 24, SIDEBAR_FONT, false);

    // Right-side buttons (same column as save screen's action bar)
    const int barX = WIDTH - SIDEBAR_PADDING - BOX_WIDTH;
    drawBoxButton(barX, SIDEBAR_PADDING, BOX_WIDTH, ACTION_BUTTON_HEIGHT,
                  UI::getSparklesIcon(), "equip", "press");
    drawBoxButton(barX, SIDEBAR_PADDING + ACTION_BUTTON_HEIGHT + ACTION_GAP,
                  BOX_WIDTH, ACTION_BUTTON_HEIGHT, UI::getCameraIcon(), "skip", "hold");
    if (!_completedHoldVisible) _completedHoldIcon = nullptr;

    renderToast();
    if (_pendingHoldPixels > 0 && _holdButton.w > 0) {
        int px = _pendingHoldPixels < _holdButton.w ? _pendingHoldPixels : _holdButton.w;
        invertButtonColumns(_holdButton.x, _holdButton.y, _holdButton.w, _holdButton.h, 0, px);
        _holdButton.filledPixels = px;
    }
    flushFramebuffer();
}

void drawPalDialogFrame(uint8_t palId, uint32_t ms) {
    PalInfo info = palLookup(palId);
    if (!info.dim) return;
    const PalDialogLayout l = palDialogLayout();
    drawPalDialogArt(info, l, ms);
    flushLines(l.cy - PAL_BADGE_R, l.cy + PAL_BADGE_R + 1);
}

// How long the face and the pal take to trade places in the face block.
constexpr uint32_t FACE_SWAP_SLIDE_MS = 450;

static void drawFaceOrPal(int x, int y, bool pal) {
    if (pal) drawPalInBox(x, y, BOX_WIDTH, FACE_BLOCK_HEIGHT, _palId, 2, true);
    else Face::mini(x, y, BOX_WIDTH, FACE_BLOCK_HEIGHT, _faceBand,
                    (millis() / FACE_BLOCK_BREATH_MS) & 1, FACE_BLOCK_B);
}

// The face and the pal take turns every PAL_SWAP_MS. At the turn the one
// leaving slides out to the left as the other slides in from the right,
// clipped to the box.
static void drawFaceBlockContent(int x, int y) {
    const uint32_t ms = millis();
    const bool palNow = _palId > 0 && ((ms / PAL_SWAP_MS) & 1);
    const uint32_t into = ms % PAL_SWAP_MS;
    if (_palId == 0 || ms < PAL_SWAP_MS || into >= FACE_SWAP_SLIDE_MS) {
        drawFaceOrPal(x, y, palNow);
        return;
    }
    const float p = 1.0f - (float)into / FACE_SWAP_SLIDE_MS;
    const int shift = (int)(BOX_WIDTH * (1.0f - p * p * p) + 0.5f);  // Ease-out cubic
    _clipX0 = x < 0 ? 0 : x;
    _clipX1 = x + BOX_WIDTH > WIDTH ? WIDTH : x + BOX_WIDTH;
    drawFaceOrPal(x - shift, y, !palNow);
    drawFaceOrPal(x + BOX_WIDTH - shift, y, palNow);
    _clipX0 = 0;
    _clipX1 = WIDTH;
}

static void drawFaceBlock(int x, int faceDy = 0) {
    if (x + BOX_WIDTH <= 0 || x >= WIDTH) return;
    fillRoundedRect(x, SIDEBAR_PADDING, BOX_WIDTH, FACE_BLOCK_HEIGHT, BOX_RADIUS, false);
    drawFaceBlockContent(x, SIDEBAR_PADDING + faceDy);
}

// Viewfinder sidebar: the face block, then a box that names the one gesture the
// viewfinder has besides shooting — hold for the gallery. x is a parameter so
// the save slide can walk it off the left edge. While the phone has asked for a
// particular shot (docs/protocol.md §3.3 PROMPT) the button says which, because
// a camera that knows what it is being pointed at should say so.
static void drawSidebarButton(int x, bool feedback = true) {
    const int buttonY = SIDEBAR_PADDING + FACE_BLOCK_HEIGHT + FACE_BLOCK_GAP;
    const int buttonH = INBOX_HEIGHT - FACE_BLOCK_HEIGHT - FACE_BLOCK_GAP;
    switch (_prompt) {
        case Prompt::Verification:
            drawBoxButton(x, buttonY, BOX_WIDTH, buttonH, UI::getScanIcon(), "card", "press", feedback);
            break;
        case Prompt::Selfie:
            drawBoxButton(x, buttonY, BOX_WIDTH, buttonH, UI::getUserIcon(), "you", "press", feedback);
            break;
        default:
            drawBoxButton(x, buttonY, BOX_WIDTH, buttonH, UI::getImagesIcon(), "photos", "hold", feedback);
            break;
    }
}

static void drawSidebar(int x, int faceDy = 0) {
    drawFaceBlock(x, faceDy);
    drawSidebarButton(x);
}

static void drawCachedSidebar() {
    // Spend 2.4 KB of DRAM on the stationary column; slides keep the uncached renderer.
    constexpr int bytes = PHOTO_X_CAPTURE / 8;
    static uint8_t cache[HEIGHT * bytes];
    static bool valid = false;
    static Prompt prompt;
    static ButtonState hold, press;
    if (!valid || prompt != _prompt) {
        for (int y = 0; y < HEIGHT; y++) memset(_framebuffer + y * BYTES_PER_LINE, 0xFF, bytes);
        fillRoundedRect(SIDEBAR_PADDING, SIDEBAR_PADDING, BOX_WIDTH, FACE_BLOCK_HEIGHT, BOX_RADIUS, false);
        // Cache plain button pixels, then apply current gesture feedback after every restore.
        drawSidebarButton(SIDEBAR_PADDING, false);
        hold = _holdButton;
        press = _pressButton;
        for (int y = 0; y < HEIGHT; y++) memcpy(cache + y * bytes, _framebuffer + y * BYTES_PER_LINE, bytes);
        prompt = _prompt;
        valid = true;
    }
    for (int y = 0; y < HEIGHT; y++) memcpy(_framebuffer + y * BYTES_PER_LINE, cache + y * bytes, bytes);
    _holdButton = hold;
    _pressButton = press;
    drawFaceBlockContent(SIDEBAR_PADDING, SIDEBAR_PADDING);
    if (_holdButton.icon && _holdButton.icon == _completedHoldIcon) {
        invertButtonColumns(hold.x, hold.y, hold.w, hold.h, 0, hold.w);
        _holdButton.filledPixels = hold.w;
        _completedHoldVisible = true;
    }
    if (_pressButton.icon && _pressButton.icon == _pressFlashIcon && pressFlashActive()) {
        invertButtonColumns(press.x, press.y, press.w, press.h, 0, press.w);
        _pressButton.filledPixels = press.w;
    }
}

// Gallery column: same two-button stack as the save screen so the two review
// screens read as siblings. The top button's label is the position counter —
// the arrow says next, the counter says where, the footer says how, in the
// three slots the button already has. "150/150" is 62px in the large face,
// inside the 70px box.
static void drawGalleryBar(int x, int index, int total) {
    char counter[16];
    snprintf(counter, sizeof(counter), "%d/%d", index + 1, total);
    drawBoxButton(x, SIDEBAR_PADDING, BOX_WIDTH, ACTION_BUTTON_HEIGHT,
                  UI::getNextIcon(), counter, "press");
    drawBoxButton(x, SIDEBAR_PADDING + ACTION_BUTTON_HEIGHT + ACTION_GAP,
                  BOX_WIDTH, ACTION_BUTTON_HEIGHT, UI::getCameraIcon(), "back", "hold");
}

// Save-screen actions, stacked in the right-hand column
static void drawActionBar(int x) {
    drawBoxButton(x, SIDEBAR_PADDING, BOX_WIDTH, ACTION_BUTTON_HEIGHT,
                  UI::getSendIcon(), "send", "press");
    drawBoxButton(x, SIDEBAR_PADDING + ACTION_BUTTON_HEIGHT + ACTION_GAP,
                  BOX_WIDTH, ACTION_BUTTON_HEIGHT, UI::getTrashIcon(), "trash", "hold");
}

// Render active toast onto framebuffer (call after image + sidebar are drawn)
static void renderToast() {
    if (!_toastActive) return;

    // Check expiration
    if (_toastExpireAt != UINT32_MAX && millis() >= _toastExpireAt) {
        _toastActive = false;
        return;
    }

    int textW = UI::textWidth(_toastText, TOAST_FONT);
    int textH = UI::fontHeight(TOAST_FONT);
    int boxW = textW + TOAST_PADDING_H * 2;
    int boxH = textH + TOAST_PADDING_V * 2;

    // Calculate X position (constrained to not overlap sidebar)
    int boxX;
    switch (_toastHAlign) {
        case ToastHAlign::Left:
            boxX = TOAST_MIN_X + TOAST_MARGIN;
            break;
        case ToastHAlign::Right:
            boxX = WIDTH - boxW - TOAST_MARGIN;
            break;
        case ToastHAlign::Center:
        default:
            boxX = TOAST_MIN_X + (WIDTH - TOAST_MIN_X - boxW) / 2;
            break;
    }
    // Clamp to valid range
    if (boxX < TOAST_MIN_X + TOAST_MARGIN) boxX = TOAST_MIN_X + TOAST_MARGIN;
    if (boxX + boxW > WIDTH - TOAST_MARGIN) boxX = WIDTH - boxW - TOAST_MARGIN;

    // Calculate Y position
    int boxY;
    if (_toastVAlign == ToastVAlign::Top) {
        boxY = TOAST_MARGIN;
    } else {
        boxY = HEIGHT - boxH - TOAST_MARGIN;
    }

    // Draw background
    bool bgWhite = !_toastInverted;
    fillRoundedRect(boxX, boxY, boxW, boxH, TOAST_RADIUS, bgWhite);

    // Draw border (1px, opposite of background)
    drawRoundedRectBorder(boxX, boxY, boxW, boxH, TOAST_RADIUS, !bgWhite);

    // Draw text centered in box
    int textX = boxX + TOAST_PADDING_H;
    int textY = boxY + TOAST_PADDING_V;
    drawText(_toastText, textX, textY, TOAST_FONT, !bgWhite);
}

void showToast(const char* text, ToastHAlign halign, ToastVAlign valign,
               bool inverted, uint32_t duration_ms) {
    // Copy text (truncate if needed)
    int i = 0;
    while (text[i] && i < (int)sizeof(_toastText) - 1) {
        _toastText[i] = text[i];
        i++;
    }
    _toastText[i] = '\0';

    _toastHAlign = halign;
    _toastVAlign = valign;
    _toastInverted = inverted;
    _toastActive = true;

    if (duration_ms == 0) {
        _toastExpireAt = UINT32_MAX;  // Indefinite
    } else {
        _toastExpireAt = millis() + duration_ms;
    }
}

void clearToast() {
    _toastActive = false;
    _toastText[0] = '\0';
}

bool toastVisible() {
    if (!_toastActive) return false;
    return _toastExpireAt == UINT32_MAX || millis() < _toastExpireAt;
}

void setBatteryPercent(int percent) {
    _batteryPercent = percent < 0 ? 0 : (percent > 100 ? 100 : percent);
}

void setBatteryBars(uint8_t bars) {
    _batteryBars = bars < 1 ? 1 : (bars > 4 ? 4 : bars);
}

void setPhotoCount(int count) {
    _photoCount = count < 0 ? 0 : count;
}

void setSignalLevel(UI::SignalLevel level) {
    _signalLevel = level;
}

void setCharging(bool charging) {
    _charging = charging;
}

void setUsb(bool usb) {
    _usb = usb;
}

void setShowHints(bool show) {
    _showHints = show;
}

bool promptPending() {
    return _prompt != Prompt::None;
}

void setPrompt(Prompt p) {
    _prompt = p;
}

// Whiten the padding strips around the photo and round its corners.
//
// drawnX/drawnW is the span the dither loop actually wrote image pixels to;
// imgStartX/imgEndX is the span that should stay visible. The photo sits flush
// against one panel edge (right for the viewfinder, left while reviewing), so
// only that edge has image pixels to paint over — the opposite 80px column is
// already white from the render loop and gets the sidebar or buttons drawn on
// top of it.
static void applyImagePaddingAndCorners(int drawnX, int drawnW, int imgStartX, int imgEndX) {
    const int drawnEndX = drawnX + drawnW;
    const int imgStartY = VF_PADDING_TOP;  // 5
    const int imgEndY = HEIGHT - VF_PADDING_BOTTOM;  // 235

    // Fill top padding
    for (int y = 0; y < imgStartY; y++) {
        for (int x = drawnX; x < drawnEndX; x++) {
            setPixel(x, y, true);  // white
        }
    }

    // Fill bottom padding
    for (int y = imgEndY; y < HEIGHT; y++) {
        for (int x = drawnX; x < drawnEndX; x++) {
            setPixel(x, y, true);  // white
        }
    }

    // Fill whichever side strip has image pixels under it
    for (int y = imgStartY; y < imgEndY; y++) {
        for (int x = drawnX; x < imgStartX; x++) {
            setPixel(x, y, true);  // white
        }
        for (int x = imgEndX; x < drawnEndX; x++) {
            setPixel(x, y, true);  // white
        }
    }

    // Apply 10px rounded corners at the image edges (inside the padding)
    for (int row = 0; row < VF_CORNER_RADIUS; row++) {
        int maskPixels = CORNER_MASK_10[row];
        if (maskPixels == 0) continue;

        // Top-left corner of image (at imgStartX, imgStartY)
        for (int p = 0; p < maskPixels; p++) {
            setPixel(imgStartX + p, imgStartY + row, true);
        }

        // Top-right corner of image (at imgEndX-1, imgStartY)
        for (int p = 0; p < maskPixels; p++) {
            setPixel(imgEndX - 1 - p, imgStartY + row, true);
        }

        // Bottom-left corner of image (at imgStartX, imgEndY-1)
        for (int p = 0; p < maskPixels; p++) {
            setPixel(imgStartX + p, imgEndY - 1 - row, true);
        }

        // Bottom-right corner of image (at imgEndX-1, imgEndY-1)
        for (int p = 0; p < maskPixels; p++) {
            setPixel(imgEndX - 1 - p, imgEndY - 1 - row, true);
        }
    }
}

// Push framebuffer rows [fromY, toY) to the panel in one CS-high transaction.
// The panel is addressed per line, so a partial update costs only the lines
// sent — what the breathing sleep face relies on to stay cheap.
static void flushLines(int fromY, int toY) {
    digitalWrite(_cs, HIGH);
    delayMicroseconds(6);

    sendByte(makeCommand(CMD_WRITE));

    uint8_t* fbPtr = _framebuffer + fromY * BYTES_PER_LINE;
    for (int line = fromY + 1; line <= toY; line++) {
        sendByte(line);
        _spi->transferBytes(fbPtr, nullptr, BYTES_PER_LINE);
        fbPtr += BYTES_PER_LINE;
        sendByte(0x00);
    }

    sendByte(0x00);

    delayMicroseconds(2);
    digitalWrite(_cs, LOW);
}

static void flushFramebuffer() { flushLines(0, HEIGHT); }

static void updateHoldPixels(int pixels) {
    const int previous = _holdButton.filledPixels;
    if (pixels == previous) return;
    invertButtonColumns(_holdButton.x, _holdButton.y, _holdButton.w, _holdButton.h,
                        pixels < previous ? pixels : previous, pixels > previous ? pixels : previous);
    _holdButton.filledPixels = pixels;
    flushLines(_holdButton.y, _holdButton.y + _holdButton.h);
}

void drawHoldProgress(uint32_t heldMs, uint32_t thresholdMs) {
    if (_holdButton.icon && _holdButton.icon == _completedHoldIcon) return;
    int px = HoldProgress::pixels(heldMs, thresholdMs, _holdButton.w);
    _pendingHoldPixels = px;
    updateHoldPixels(px);
}

void completeHold() {
    _completedHoldIcon = _holdButton.icon;
    _pendingHoldPixels = 0;
    updateHoldPixels(_holdButton.w);
}

void flashPress() {
    if (!_pressButton.icon) return;
    _pressFlashIcon = _pressButton.icon;
    _pressFlashUntil = millis() + PRESS_FLASH_MS;
    if (!_pressButton.filledPixels) {
        invertButtonColumns(_pressButton.x, _pressButton.y, _pressButton.w, _pressButton.h, 0, _pressButton.w);
        _pressButton.filledPixels = _pressButton.w;
        flushLines(_pressButton.y, _pressButton.y + _pressButton.h);
    }
}

// Ordered-Bayer dither the source into _photoBits. Image only — placement on
// the panel is blitPhoto()'s job.
static void bayerPhoto(const uint8_t* grayscale, int srcWidth) {
    // Pre-compute Bayer thresholds as flat array
    static const uint8_t BAYER_FLAT[16] = {
        0, 128, 32, 160, 192, 64, 224, 96, 48, 176, 16, 144, 240, 112, 208, 80
    };

    int imgBytes = srcWidth / 8;
    if (imgBytes > PHOTO_BYTES) imgBytes = PHOTO_BYTES;

    uint8_t* fbPtr = _photoBits;

    for (int y = 0; y < HEIGHT; y++) {
        const int bayerRowOffset = (y & 3) << 2;
        const uint8_t* srcRow = grayscale + y * srcWidth;

        for (int byteIdx = 0; byteIdx < imgBytes; byteIdx++) {
            const int baseX = byteIdx << 3;
            uint8_t outByte = 0;

            #define PIXEL(bit) \
                if (srcRow[baseX + bit] > BAYER_FLAT[bayerRowOffset + ((baseX + bit) & 3)]) \
                    outByte |= (0x80 >> bit);

            PIXEL(0); PIXEL(1); PIXEL(2); PIXEL(3);
            PIXEL(4); PIXEL(5); PIXEL(6); PIXEL(7);

            #undef PIXEL

            *fbPtr++ = outByte;
        }
    }
}

// Floyd-Steinberg dither the source into _photoBits. Image only — placement on
// the panel is blitPhoto()'s job.
static void ditherPhoto(const uint8_t* grayscale, int srcWidth) {
    memset(_errCurr, 0, sizeof(_errCurr));
    memset(_errNext, 0, sizeof(_errNext));

    int imgBytes = srcWidth / 8;
    if (imgBytes > PHOTO_BYTES) imgBytes = PHOTO_BYTES;

    uint8_t* fbPtr = _photoBits;

    for (int y = 0; y < HEIGHT; y++) {
        const uint8_t* srcRow = grayscale + y * srcWidth;

        for (int byteIdx = 0; byteIdx < imgBytes; byteIdx++) {
            uint8_t outByte = 0;

            for (int bit = 0; bit < 8; bit++) {
                int x = byteIdx * 8 + bit;
                int bufX = x + 1;  // +1 for boundary padding

                // Get source pixel and add accumulated error
                int16_t pixel = srcRow[x] + _errCurr[bufX];

                // Clamp and quantize
                pixel = pixel < 0 ? 0 : (pixel > 255 ? 255 : pixel);
                uint8_t newPixel = (pixel > 127) ? 255 : 0;
                int16_t err = pixel - newPixel;

                if (newPixel) {
                    outByte |= (0x80 >> bit);  // White
                }

                // Distribute error (Floyd-Steinberg weights: 7/16, 3/16, 5/16, 1/16)
                _errCurr[bufX + 1] += (err * 7) >> 4;
                _errNext[bufX - 1] += (err * 3) >> 4;
                _errNext[bufX]     += (err * 5) >> 4;
                _errNext[bufX + 1] += (err * 1) >> 4;
            }

            *fbPtr++ = outByte;
        }

        // Swap error buffers and clear next
        memcpy(_errCurr, _errNext, sizeof(_errCurr));
        memset(_errNext, 0, sizeof(_errNext));
    }
}

// Copy _photoBits into the framebuffer with its left edge at destX, filling the
// rest of each line white. destX is in pixels and need not be byte-aligned —
// sub-byte offsets are handled by carrying bits between output bytes, which is
// what lets the slide move smoothly instead of jumping 8px at a time.
// Caller must keep destX within [0, WIDTH - PHOTO_WIDTH].
static void blitPhoto(int destX) {
    const int byteOff = destX >> 3;
    const int shift = destX & 7;
    const int endByte = byteOff + PHOTO_BYTES + (shift ? 1 : 0);

    for (int y = 0; y < HEIGHT; y++) {
        const uint8_t* src = _photoBits + y * PHOTO_BYTES;
        uint8_t* line = _framebuffer + y * BYTES_PER_LINE;

        memset(line, 0xFF, byteOff);

        if (shift == 0) {
            memcpy(line + byteOff, src, PHOTO_BYTES);
        } else {
            // bit7 is the leftmost pixel, so shifting the image right by
            // `shift` pixels means each output byte takes the low bits of the
            // previous source byte and the high bits of the current one.
            uint8_t prev = 0xFF;  // White off the left edge
            for (int k = 0; k < PHOTO_BYTES; k++) {
                line[byteOff + k] = (uint8_t)((prev << (8 - shift)) | (src[k] >> shift));
                prev = src[k];
            }
            line[byteOff + PHOTO_BYTES] = (uint8_t)((prev << (8 - shift)) | (0xFF >> shift));
        }

        memset(line + endByte, 0xFF, BYTES_PER_LINE - endByte);
    }
}

// The photo card: _photoBits blitted with its left edge at photoX, padding
// whitened and corners rounded. Shared by every screen that shows a photo.
static void renderPhotoCard(int photoX) {
    blitPhoto(photoX);
    applyImagePaddingAndCorners(photoX, PHOTO_WIDTH, photoX, photoX + CARD_WIDTH);
}

#ifdef LC_PROFILE_VIEWFINDER
static ViewfinderTiming _viewfinderTiming{};
ViewfinderTiming viewfinderTiming() { return _viewfinderTiming; }
#endif

// A shared offset keeps the photo and both columns together through the save slide.
static void renderSaveLayout(float t, int faceDy = 0) {
    _holdButton = {};
    _pressButton = {};
    _completedHoldVisible = false;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    const int shift = (int)(SAVE_SLIDE_PX * t + 0.5f);

    // The card is the leftmost 315 source columns, same as the viewfinder
    // crops. Keeping the crop identical everywhere means the slide is a pure
    // translation and the photo doesn't jump sideways relative to itself at
    // either the capture moment or the end of the slide.
#ifdef LC_PROFILE_VIEWFINDER
    const uint32_t photoStart = micros();
#endif
    renderPhotoCard(PHOTO_X_CAPTURE - shift);
#ifdef LC_PROFILE_VIEWFINDER
    _viewfinderTiming.photoUs = micros() - photoStart;
    const uint32_t sidebarStart = micros();
#endif

    if (shift == 0 && faceDy == 0) drawCachedSidebar();
    else drawSidebar(SIDEBAR_PADDING - shift, faceDy);
#ifdef LC_PROFILE_VIEWFINDER
    _viewfinderTiming.sidebarUs = micros() - sidebarStart;
    const uint32_t overlayStart = micros();
#endif
    drawActionBar(WIDTH - shift);
    if (!_completedHoldVisible) _completedHoldIcon = nullptr;

    renderToast();
    if (_pendingHoldPixels > 0 && _holdButton.w > 0) {
        int px = _pendingHoldPixels < _holdButton.w ? _pendingHoldPixels : _holdButton.w;
        invertButtonColumns(_holdButton.x, _holdButton.y, _holdButton.w, _holdButton.h, 0, px);
        _holdButton.filledPixels = px;
    }
#ifdef LC_PROFILE_VIEWFINDER
    _viewfinderTiming.overlayUs = micros() - overlayStart;
    const uint32_t spiStart = micros();
#endif
    flushFramebuffer();
#ifdef LC_PROFILE_VIEWFINDER
    _viewfinderTiming.spiUs = micros() - spiStart;
#endif
}

void drawViewfinder(const uint8_t* grayscale, int srcWidth, int srcHeight) {
    if (!grayscale) return;
#ifdef LC_PROFILE_VIEWFINDER
    const uint32_t ditherStart = micros();
#endif
    bayerPhoto(grayscale, srcWidth);
#ifdef LC_PROFILE_VIEWFINDER
    _viewfinderTiming.ditherUs = micros() - ditherStart;
    const uint32_t layoutStart = micros();
#endif
    renderSaveLayout(0.0f);
#ifdef LC_PROFILE_VIEWFINDER
    _viewfinderTiming.layoutUs = micros() - layoutStart - _viewfinderTiming.spiUs;
#endif
}

void drawWakeEnter(const uint8_t* grayscale, int srcWidth, int srcHeight, float t) {
    if (grayscale) bayerPhoto(grayscale, srcWidth);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    const int dy = -(int)(FACE_BLOCK_HEIGHT * (1.0f - t) + 0.5f);
    if (grayscale) {
        renderSaveLayout(0.0f, dy);
    } else {
        // Only the miniature moves; resending the photo costs a full panel frame.
        drawFaceBlock(SIDEBAR_PADDING, dy);
        flushLines(0, SIDEBAR_PADDING + FACE_BLOCK_HEIGHT);
    }
}

void drawCapture(const uint8_t* grayscale, int srcWidth, int srcHeight) {
    if (!grayscale) return;
    ditherPhoto(grayscale, srcWidth);
    renderSaveLayout(0.0f);
}

void drawSave(const uint8_t* grayscale, int srcWidth, int srcHeight) {
    if (!grayscale) return;
    ditherPhoto(grayscale, srcWidth);
    renderSaveLayout(1.0f);
}

void drawSaveTransition(float t) {
    renderSaveLayout(t);
}

void drawDismissTransition(const uint8_t* grayscale, int srcWidth, int srcHeight, float t) {
    if (grayscale) bayerPhoto(grayscale, srcWidth);
    renderSaveLayout(t);
}

// Same slide as capture -> save, with the browse column entering instead of
// the actions: one shift drives the card, the sidebar walking off the left,
// and the gallery bar walking in from the right. Renders whatever is in
// _photoBits.
static void renderGalleryLayout(int index, int total, float t) {
    _holdButton = {};
    _pressButton = {};
    _completedHoldVisible = false;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    const int shift = (int)(SAVE_SLIDE_PX * t + 0.5f);
    renderPhotoCard(PHOTO_X_CAPTURE - shift);
    drawSidebar(SIDEBAR_PADDING - shift);
    drawGalleryBar(WIDTH - shift, index, total);
    if (!_completedHoldVisible) _completedHoldIcon = nullptr;
    renderToast();
    flushFramebuffer();
}

void drawGalleryTransition(const uint8_t* bits, int index, int total, float t) {
    if (!bits) return;
    // A copy rather than decoding straight into _photoBits: 9.6KB of memcpy is
    // microseconds against a 13ms panel flush, and it keeps Display's contract
    // one-directional (bitmaps in, pixels out), which is what the preview tool
    // relies on.
    memcpy(_photoBits, bits, sizeof(_photoBits));
    renderGalleryLayout(index, total, t);
}

void drawGalleryDismissTransition(const uint8_t* grayscale, int srcWidth, int srcHeight,
                                  int index, int total, float t) {
    if (grayscale) bayerPhoto(grayscale, srcWidth);
    renderGalleryLayout(index, total, t);
}

void drawGallery(const uint8_t* bits, int index, int total) {
    // The gallery at rest is the far end of the slide: card flush left at
    // VF_PADDING_LEFT, sidebar fully off-screen, column at the right margin.
    drawGalleryTransition(bits, index, total, 1.0f);
}

void drawGalleryEmpty() {
    _holdButton = {};
    _pressButton = {};
    _completedHoldVisible = false;
    memset(_framebuffer, 0xFF, sizeof(_framebuffer));
    // The card's outline where the photo would be, with the message inside it,
    // so the empty state has the same shape as the full one.
    const int cardH = HEIGHT - VF_PADDING_TOP - VF_PADDING_BOTTOM;
    drawRoundedRectBorder(VF_PADDING_LEFT, VF_PADDING_TOP, CARD_WIDTH, cardH, VF_CORNER_RADIUS, false);
    const char* text = "no photos yet";
    drawTextCenteredAt(text, VF_PADDING_LEFT, CARD_WIDTH,
                       VF_PADDING_TOP + (cardH - UI::fontHeight(SIDEBAR_FONT)) / 2, SIDEBAR_FONT, false);
    // Nothing to step through, so only the back button — full height, like
    // the viewfinder's sidebar.
    drawBoxButton(WIDTH - SIDEBAR_PADDING - BOX_WIDTH, SIDEBAR_PADDING, BOX_WIDTH, INBOX_HEIGHT,
                  UI::getCameraIcon(), "back", "hold");
    if (!_completedHoldVisible) _completedHoldIcon = nullptr;
    renderToast();
    flushFramebuffer();
}

// System menu: full-screen list, selected row inverted, right-side buttons.
// The card area holds the rows; the right column mirrors the gallery's layout.
void drawMenu(const char* const* items, int count, int selected) {
    clearButtonFeedback();
    memset(_framebuffer, 0xFF, sizeof(_framebuffer));

    const int cardX = VF_PADDING_LEFT;
    const int cardY = VF_PADDING_TOP;
    const int cardW = CARD_WIDTH;
    const int cardH = HEIGHT - VF_PADDING_TOP - VF_PADDING_BOTTOM;
    drawRoundedRectBorder(cardX, cardY, cardW, cardH, VF_CORNER_RADIUS, false);

    const int rowH = cardH / count;
    const int fontH = UI::fontHeight(SIDEBAR_FONT);

    for (int i = 0; i < count; i++) {
        const int ry = cardY + i * rowH;
        if (i > 0) {
            for (int px = cardX + 1; px < cardX + cardW - 1; px++)
                setPixel(px, ry, false);
        }
        if (i == selected) {
            for (int py = ry + (i > 0 ? 1 : 0); py < ry + rowH; py++)
                for (int px = cardX + 1; px < cardX + cardW - 1; px++)
                    if (isInsideRoundedRect(px, py, cardX, cardY, cardW, cardH, VF_CORNER_RADIUS))
                        setPixel(px, py, false);
        }
        drawText(items[i], cardX + 16, ry + (rowH - fontH) / 2, SIDEBAR_FONT, i == selected);
    }

    const int barX = WIDTH - SIDEBAR_PADDING - BOX_WIDTH;
    char counter[8];
    snprintf(counter, sizeof(counter), "%d/%d", selected + 1, count);
    drawBoxButton(barX, SIDEBAR_PADDING, BOX_WIDTH, ACTION_BUTTON_HEIGHT,
                  UI::getNextIcon(), counter, "press");
    drawBoxButton(barX, SIDEBAR_PADDING + ACTION_BUTTON_HEIGHT + ACTION_GAP,
                  BOX_WIDTH, ACTION_BUTTON_HEIGHT, UI::getNextIcon(), "select", "hold");
    if (!_completedHoldVisible) _completedHoldIcon = nullptr;
    renderToast();
    flushFramebuffer();
}

void drawMenuSelect(const char* const* items, int count, int prev, int next) {
    const int cardX = VF_PADDING_LEFT;
    const int cardY = VF_PADDING_TOP;
    const int cardW = CARD_WIDTH;
    const int cardH = HEIGHT - VF_PADDING_TOP - VF_PADDING_BOTTOM;
    const int rowH = cardH / count;
    const int fontH = UI::fontHeight(SIDEBAR_FONT);

    int flushTop = HEIGHT, flushBot = 0;

    for (int which = 0; which < 2; which++) {
        int i = which == 0 ? prev : next;
        bool sel = (i == next);
        int ry = cardY + i * rowH;
        int rowTop = ry + (i > 0 ? 1 : 0);
        int rowBot = ry + rowH;
        bool nearEdge = (i == 0 || i == count - 1);
        for (int py = rowTop; py < rowBot; py++)
            for (int px = cardX + 1; px < cardX + cardW - 1; px++) {
                if (nearEdge && !isInsideRoundedRect(px, py, cardX, cardY, cardW, cardH, VF_CORNER_RADIUS))
                    continue;
                setPixel(px, py, sel ? false : true);
            }
        if (nearEdge)
            drawRoundedRectBorder(cardX, cardY, cardW, cardH, VF_CORNER_RADIUS, false);
        drawText(items[i], cardX + 16, ry + (rowH - fontH) / 2, SIDEBAR_FONT, sel);
        if (ry < flushTop) flushTop = ry;
        if (rowBot > flushBot) flushBot = rowBot;
    }

    const int barX = WIDTH - SIDEBAR_PADDING - BOX_WIDTH;
    char counter[8];
    snprintf(counter, sizeof(counter), "%d/%d", next + 1, count);
    drawBoxButton(barX, SIDEBAR_PADDING, BOX_WIDTH, ACTION_BUTTON_HEIGHT,
                  UI::getNextIcon(), counter, "press");
    if (SIDEBAR_PADDING < flushTop) flushTop = SIDEBAR_PADDING;
    if (SIDEBAR_PADDING + ACTION_BUTTON_HEIGHT > flushBot)
        flushBot = SIDEBAR_PADDING + ACTION_BUTTON_HEIGHT;

    flushLines(flushTop, flushBot);
}

// Diagnostics: key-value lines in a bordered card, like a receipt.
void drawDiagnostics(const DiagLine* lines, int count) {
    clearButtonFeedback();
    memset(_framebuffer, 0xFF, sizeof(_framebuffer));

    const int inset = 12;
    const int cardX = inset;
    const int cardY = inset;
    const int cardW = WIDTH - 2 * inset;
    const int cardH = HEIGHT - 2 * inset;
    drawRoundedRectBorder(cardX, cardY, cardW, cardH, VF_CORNER_RADIUS, false);

    const int fontH = UI::fontHeight(HINT_FONT);
    const int lineH = fontH + 6;
    const int startY = cardY + (cardH - count * lineH) / 2;
    const int labelX = cardX + 16;
    const int valueX = cardX + cardW / 2 + 10;

    for (int i = 0; i < count; i++) {
        const int y = startY + i * lineH;
        drawText(lines[i].label, labelX, y, HINT_FONT, false);
        drawText(lines[i].value, valueX, y, HINT_FONT, false);
    }

    drawTextCenteredAt("press to close", 0, WIDTH, cardY + cardH - inset - fontH, HINT_FONT, false);
    flushFramebuffer();
}

// Share blog: the short code in big text with "lttl.cam" underneath.
void drawShareBlog(const char* code, const char* hint) {
    drawPairing(code, hint);
}

// Pairing gets a screen of its own rather than a toast: the code has to stay
// readable while the phone asks for it, and the viewfinder's own hint used to
// paint straight over it. `big` is scaled to fit, so both the six digits and
// the outcome line use one call.
void drawPairing(const char* big, const char* hint) {
    clearButtonFeedback();
    memset(_framebuffer, 0xFF, sizeof(_framebuffer));  // White paper, like every screen but sleep

    const int inset = 12;
    drawRoundedRectBorder(inset, inset, WIDTH - 2 * inset, HEIGHT - 2 * inset, VF_CORNER_RADIUS, false);

    const int avail = WIDTH - 2 * (inset + 16);
    const int unitW = UI::textWidth(big, UI::Font::Large);
    int scale = 4;
    while (scale > 1 && unitW * scale > avail) scale--;

    const int bigW = unitW * scale;
    const int bigH = UI::fontHeight(UI::Font::Large) * scale;
    const bool hasHint = hint && hint[0];
    const int hintH = hasHint ? UI::fontHeight(HINT_FONT) : 0;
    const int gap = hasHint ? 20 : 0;

    const int y = (HEIGHT - bigH - gap - hintH) / 2;
    drawTextScaled(big, (WIDTH - bigW) / 2, y, UI::Font::Large, false, scale);
    if (hasHint) drawTextCenteredAt(hint, 0, WIDTH, y + bigH + gap, HINT_FONT, false);

    flushFramebuffer();
}

// The filled part of the progress bar, from the geometry the last full draw
// recorded. Clears the inside first so the bar can also go backwards (a rewind
// after a dropped chunk does exactly that).
static void fillUpdateBar(int percent) {
    const int p = percent < 0 ? 0 : (percent > 100 ? 100 : percent);
    const int innerW = _updateBarW - 2 * UPDATE_BAR_PADDING;
    const int innerH = UPDATE_BAR_HEIGHT - 2 * UPDATE_BAR_PADDING;
    const int x = _updateBarX + UPDATE_BAR_PADDING;
    const int y = _updateBarY + UPDATE_BAR_PADDING;
    for (int py = y; py < y + innerH; py++)
        for (int px = x; px < x + innerW; px++) setPixel(px, py, true);
    const int fillW = innerW * p / 100;
    if (fillW <= 0) return;
    // Square the fill off below a few pixels: a rounded corner wider than the
    // shape it is rounding eats the whole thing.
    const int r = fillW < 2 * UPDATE_BAR_RADIUS ? 0 : UPDATE_BAR_RADIUS - UPDATE_BAR_PADDING;
    fillRoundedRect(x, y, fillW, innerH, r, false);
}

// The update screen: what the camera is doing to itself, and how far along it
// is. Its own screen for the same reason pairing has one — it owns the panel
// for minutes, and a toast would be painted over by the next one. The bar is
// the only progress indicator in the UI; everything else here is instant.
void drawUpdate(const char* line, int percent, const char* hint, const uint8_t* mark) {
    clearButtonFeedback();
    memset(_framebuffer, 0xFF, sizeof(_framebuffer));  // White paper, like every screen but sleep

    const int inset = 12;
    drawRoundedRectBorder(inset, inset, WIDTH - 2 * inset, HEIGHT - 2 * inset, VF_CORNER_RADIUS, false);

    const int avail = WIDTH - 2 * (inset + 16);
    const int unitW = UI::textWidth(line, UI::Font::Large);
    // One notch smaller than the pairing code: this is a sentence, not six
    // digits, and it is read at arm's length rather than across the room.
    int scale = 3;
    while (scale > 1 && unitW * scale > avail) scale--;

    const int textW = unitW * scale;
    const int textH = UI::fontHeight(UI::Font::Large) * scale;
    const bool hasBar = percent >= 0;
    const bool hasHint = hint && hint[0];
    const int barH = hasBar ? UPDATE_BAR_HEIGHT : 0;
    const int hintH = hasHint ? UI::fontHeight(HINT_FONT) : 0;
    const int gapBar = hasBar ? 24 : 0;
    const int gapHint = hasHint ? (hasBar ? 14 : 20) : 0;
    const int markH = UI::ICON_SIZE * UPDATE_MARK_SCALE;

    int y = (HEIGHT - markH - UPDATE_MARK_GAP - textH - gapBar - barH - gapHint - hintH) / 2;
    // The same four-pointed star the phone shows while it sends, so the two
    // screens in front of whoever is waiting are one screen.
    drawIconScaled(mark ? mark : UI::getSparklesIcon(), UI::ICON_SIZE, UI::ICON_SIZE,
                   (WIDTH - UI::ICON_SIZE * UPDATE_MARK_SCALE) / 2, y, false, UPDATE_MARK_SCALE);
    y += markH + UPDATE_MARK_GAP;
    drawTextScaled(line, (WIDTH - textW) / 2, y, UI::Font::Large, false, scale);
    y += textH + gapBar;

    _updateBarX = _updateBarY = -1;
    if (hasBar) {
        _updateBarX = (WIDTH - avail) / 2;
        _updateBarY = y;
        _updateBarW = avail;
        drawRoundedRectBorder(_updateBarX, y, avail, barH, UPDATE_BAR_RADIUS, false);
        fillUpdateBar(percent);
        y += barH + gapHint;
    }

    if (hasHint) drawTextCenteredAt(hint, 0, WIDTH, y, HINT_FONT, false);

    flushFramebuffer();
}

void drawUpdateProgress(int percent) {
    if (_updateBarY < 0) return;  // The screen up right now has no bar
    fillUpdateBar(percent);
    // Just the bar's rows. A whole frame is ~50ms of SPI, which is long enough
    // for the radio to outrun the chunk ring underneath it; twenty lines is
    // nothing, so the bar can follow every percent without costing a rewind.
    flushLines(_updateBarY, _updateBarY + UPDATE_BAR_HEIGHT);
}

void drawSplash() {
    clearButtonFeedback();
    digitalWrite(_cs, HIGH);
    delayMicroseconds(6);

    sendByte(makeCommand(CMD_WRITE));

    const uint8_t* ptr = SPLASH_BITMAP;
    for (int line = 1; line <= HEIGHT; line++) {
        sendByte(line);

        // Send bitmap data directly without bit-reversal — ImageMagick's
        // MSB-first pixel order matches what the LCD expects after SPI transmission
        for (int i = 0; i < BYTES_PER_LINE; i++) {
            _spi->transfer(pgm_read_byte(ptr++));
        }

        sendByte(0x00);
    }

    sendByte(0x00);

    delayMicroseconds(2);
    digitalWrite(_cs, LOW);
}

constexpr int SLEEP_BADGE_SCALE = 1;
constexpr int SLEEP_BADGE_SIZE = UI::ICON_SIZE * SLEEP_BADGE_SCALE;

// Top-left, level with the "hold to wake" pill; the pal owns the bottom-left.
constexpr int SLEEP_BADGE_Y = TOAST_MARGIN;

static void drawSleepBadge(bool breathIn) {
    const int iconY = SLEEP_BADGE_Y;
    if (_usb) {
        drawIconScaled(UI::getChargingIcon(), UI::ICON_SIZE, UI::ICON_SIZE,
                       TOAST_MARGIN, iconY, true, SLEEP_BADGE_SCALE);
        return;
    }
    UI::BatteryLevel level;
    switch (_batteryBars) {
        case 1: level = UI::BatteryLevel::Empty; break;
        case 2: level = UI::BatteryLevel::Low; break;
        case 3: level = UI::BatteryLevel::Medium; break;
        default: level = UI::BatteryLevel::Full; break;
    }
    drawIconScaled(UI::getBatteryIcon(level), UI::ICON_SIZE, UI::ICON_SIZE,
                   TOAST_MARGIN, iconY, true, SLEEP_BADGE_SCALE);
}

// The pal's corner of the sleep face: flush bottom-left, the bar above it.
constexpr int SLEEP_PAL_SCALE = 6;
constexpr int PAL_BAR_W = 60;
constexpr int PAL_BAR_H = 8;
constexpr int PAL_BAR_GAP = 8;  // Clear of the one-pixel bob the pal makes while eating
// The food in hand, bottom-right: the item, then the count.
constexpr int SLEEP_FOOD_SCALE = 3;
constexpr int SLEEP_FOOD_GAP = 6;

// Pixels of fill inside the bar's outline, rounded up so a pal with any life left shows some.
static int palBarFill(uint8_t life) {
    const int inner = PAL_BAR_W - 4;
    return (life * inner + 254) / 255;
}

// What the last full sleep frame sent, so a change stays owed until one goes
// out: a breath sends only the face rows, which miss the corners.
static int _sentBarFill = -1;
static uint8_t _sentFood = 0;
static bool _sentDead = false;
static uint8_t _sentPalId = 0;

bool setPalCare(uint8_t life, uint8_t food, bool dead) {
    _palLife = life;
    _palFood = food;
    _palDead = dead;
    return palBarFill(life) != _sentBarFill || food != _sentFood || dead != _sentDead || _palId != _sentPalId;
}

static int sleepPalHeight() {
    if (_palDead) return PAL_TOMB_H * SLEEP_PAL_SCALE;
    PalInfo pi = palLookup(_palId);
    return pi.dim ? pi.dim->h * SLEEP_PAL_SCALE : 0;
}

static int palBarY() { return HEIGHT - sleepPalHeight() - PAL_BAR_GAP - PAL_BAR_H; }

static void drawPalBar(int fill) {
    const int y = palBarY();
    drawRoundedRectBorder(TOAST_MARGIN, y, PAL_BAR_W, PAL_BAR_H, 2, true);
    if (fill > 0) fillRoundedRect(TOAST_MARGIN + 2, y + 2, fill, PAL_BAR_H - 4, 0, true);
}

// The food badge's item: where it sits, so the feed toss can start from it.
static void foodBadgeItem(const PalFood& f, int* x, int* y) {
    char text[8];
    snprintf(text, sizeof(text), "x%u", (unsigned)_palFood);
    const int textX = WIDTH - TOAST_MARGIN - UI::textWidth(text, TOAST_FONT);
    *x = textX - SLEEP_FOOD_GAP - f.w * SLEEP_FOOD_SCALE;
    *y = HEIGHT - TOAST_MARGIN - UI::fontHeight(TOAST_FONT) / 2 - f.h * SLEEP_FOOD_SCALE / 2;
}

static void drawFoodBadge() {
    const PalFood f = palFood(_palId);
    if (!f.bmp || !_palFood) return;
    char text[8];
    snprintf(text, sizeof(text), "x%u", (unsigned)_palFood);
    const int textX = WIDTH - TOAST_MARGIN - UI::textWidth(text, TOAST_FONT);
    drawText(text, textX, HEIGHT - TOAST_MARGIN - UI::fontHeight(TOAST_FONT), TOAST_FONT, true);
    int ix, iy;
    foodBadgeItem(f, &ix, &iy);
    drawPalBmp(f.bmp, f.w, f.h, f.bpr, ix, iy, SLEEP_FOOD_SCALE, true);
}

// The pal (or what is left of it) in its corner, with its bar. `bmp` overrides
// the first idle frame, `dy` lifts it, `fill` is the bar's.
static void drawSleepPalCorner(const uint8_t* bmp, int dy, int fill) {
    if (_palDead) {
        drawPalBmp(PAL_TOMB_BMP, PAL_TOMB_W, PAL_TOMB_H, PAL_TOMB_BPR, TOAST_MARGIN,
                   HEIGHT - PAL_TOMB_H * SLEEP_PAL_SCALE, SLEEP_PAL_SCALE, true);
        return;
    }
    PalInfo pi = palLookup(_palId);
    if (!pi.dim || !pi.idle) return;
    // The first idle frame unless told otherwise: a breath re-sends only the
    // face rows, which overlap the pal, so an animated pal would tear across them.
    if (!bmp) bmp = pi.idle->steps[0].bmp;
    drawPalBmp(bmp, pi.dim->w, pi.dim->h, pi.dim->bpr, TOAST_MARGIN,
               HEIGHT - pi.dim->h * SLEEP_PAL_SCALE + dy * SLEEP_PAL_SCALE, SLEEP_PAL_SCALE, true);
    drawPalBar(fill);
}

static int _wakeFilledRows = 0;
static bool _wakeExitActive = false;
static int _wakeExitDy = 0;

// The sleep face into the framebuffer, sent by the caller. Without `palCorner`
// the pal's corner and the food are left out, for drawPalFeed() to draw.
static void renderSleepFrame(uint8_t happiness, bool breathIn, const char* status, bool awake,
                             bool palCorner) {
    _wakeFilledRows = 0;
    _wakeExitActive = false;
    clearButtonFeedback();
    memset(_framebuffer, 0, sizeof(_framebuffer));  // Black
    const uint8_t band = Chirp::band(happiness);
    const int eyeY = Face::EYE_Y + (breathIn ? Face::BREATH_EYES_DY : 0);
    const int mouthY = Face::MOUTH_Y + (breathIn ? Face::BREATH_MOUTH_DY : 0);
    if (awake) {
        const int centers[] = {Face::EYE_L_X + 2 * Face::B, Face::EYE_R_X + 2 * Face::B};
        for (int center : centers) {
            Face::awakeEye(center, eyeY, band, center < WIDTH / 2);
        }
    } else {
        Face::eye(Face::EYE_L_X, eyeY, band == 3);
        Face::eye(Face::EYE_R_X, eyeY, band == 3);
    }
    Face::mouth(Face::MOUTH_X, mouthY, band);

    if (_showHints && !awake) {
        const char* text = "hold to wake";
        int textW = UI::textWidth(text, TOAST_FONT);
        int textH = UI::fontHeight(TOAST_FONT);
        int boxW = textW + TOAST_PADDING_H * 2;
        int boxH = textH + TOAST_PADDING_V * 2;
        int boxX = WIDTH - boxW - TOAST_MARGIN;
        int boxY = TOAST_MARGIN;
        fillRoundedRect(boxX, boxY, boxW, boxH, TOAST_RADIUS, false);
        drawRoundedRectBorder(boxX, boxY, boxW, boxH, TOAST_RADIUS, true);
        drawText(text, boxX + TOAST_PADDING_H, boxY + TOAST_PADDING_V, TOAST_FONT, true);
    }

    if (_palId > 0 && !awake && palCorner) {
        drawSleepPalCorner(nullptr, 0, palBarFill(_palLife));
        if (!_palDead) drawFoodBadge();
    }

    drawSleepBadge(breathIn);
    if (status) {
        drawText(status, TOAST_MARGIN + SLEEP_BADGE_SIZE + TOAST_MARGIN,
                 SLEEP_BADGE_Y, TOAST_FONT, true);
    }

    if (awake) {
        // Keep the completed white fill behind the awake face until the sensor is ready.
        for (uint8_t& byte : _framebuffer) byte = ~byte;
    }
}

static void drawSleepFrame(uint8_t happiness, bool breathIn, bool faceOnly, const char* status,
                           bool awake) {
    renderSleepFrame(happiness, breathIn, status, awake, true);
    if (!faceOnly && !awake) {
        _sentBarFill = palBarFill(_palLife);
        _sentFood = _palFood;
        _sentDead = _palDead;
        _sentPalId = _palId;
    }
    if (faceOnly) {
        flushLines(Face::EYE_Y + Face::BREATH_EYES_DY, Face::MOUTH_Y + 2 * Face::B + Face::BREATH_MOUTH_DY);
        flushLines(SLEEP_BADGE_Y, SLEEP_BADGE_Y + SLEEP_BADGE_SIZE);
    } else {
        flushFramebuffer();
    }
}

// Eating: a chomp-chomp from each pal's own frames, a pixel of bob at most so
// it never reaches the bar above it.
static const pal_step_t PAL_MOCHI_EAT_STEPS[] = {
    {PAL_MOCHI_BMP_PERK, 0, -1, 150}, {PAL_MOCHI_BMP_IDLE_A, 0, 0, 150},
    {PAL_MOCHI_BMP_PERK, 0, -1, 150}, {PAL_MOCHI_BMP_IDLE_A, 0, 0, 350},
};
static const pal_step_t PAL_CROAKS_EAT_STEPS[] = {
    {PAL_CROAKS_BMP_CROUCH, 0, 0, 150}, {PAL_CROAKS_BMP_IDLE_A, 0, 0, 150},
    {PAL_CROAKS_BMP_CROUCH, 0, 0, 150}, {PAL_CROAKS_BMP_BLINK, 0, 0, 350},
};
static const pal_step_t PAL_TWEET_EAT_STEPS[] = {
    {PAL_TWEET_BMP_CHIRP, 0, 0, 150}, {PAL_TWEET_BMP_IDLE_A, 0, 0, 150},
    {PAL_TWEET_BMP_CHIRP, 0, 0, 150}, {PAL_TWEET_BMP_IDLE_A, 0, 0, 350},
};
static const pal_step_t PAL_PILLOWCASE_EAT_STEPS[] = {
    {PAL_PILLOWCASE_BMP_IDLE_B, 0, -1, 150}, {PAL_PILLOWCASE_BMP_IDLE_A, 0, 0, 150},
    {PAL_PILLOWCASE_BMP_IDLE_B, 0, -1, 150}, {PAL_PILLOWCASE_BMP_IDLE_A, 0, 0, 350},
};

static const pal_step_t* palEatStep(uint8_t id, uint32_t ms) {
    const pal_step_t* steps;
    switch (id) {
        case 1: steps = PAL_MOCHI_EAT_STEPS; break;
        case 2: steps = PAL_CROAKS_EAT_STEPS; break;
        case 3: steps = PAL_TWEET_EAT_STEPS; break;
        case 4: steps = PAL_PILLOWCASE_EAT_STEPS; break;
        default: return nullptr;
    }
    uint32_t acc = 0;
    for (int i = 0; i < 4; i++) {
        acc += steps[i].ms;
        if (ms < acc) return &steps[i];
    }
    return &steps[3];
}

constexpr int PAL_FEED_ARC_PX = 16;  // Low enough to pass under the widest mouth
constexpr int PAL_HEART_SCALE = 3;
constexpr int PAL_HEART_RISE_PX = 28;

void drawPalFeed(uint8_t happiness, bool breathIn, uint32_t ms, uint8_t fromLife, bool full) {
    renderSleepFrame(happiness, breathIn, nullptr, false, false);
    drawFoodBadge();
    PalInfo pi = palLookup(_palId);
    const PalFood f = palFood(_palId);
    if (!pi.dim || !f.bmp) {
        flushFramebuffer();
        return;
    }
    const int pw = pi.dim->w * SLEEP_PAL_SCALE, ph = pi.dim->h * SLEEP_PAL_SCALE;
    const int fromFill = palBarFill(fromLife), toFill = palBarFill(_palLife);
    if (ms < PAL_FEED_TOSS_MS) {
        drawSleepPalCorner(nullptr, 0, fromFill);
        // From the badge to just in front of the pal's face, eased out, in a low hop.
        // The badge already shows one fewer, so the item reads as taken from it.
        int x0, y0;
        const int savedFood = _palFood;
        _palFood = savedFood + 1;
        foodBadgeItem(f, &x0, &y0);
        _palFood = savedFood;
        const int fh = f.h * SLEEP_FOOD_SCALE;
        const int x1 = TOAST_MARGIN + pw;
        const int y1 = HEIGHT - ph / 2 - fh / 2;
        const float t = (float)ms / PAL_FEED_TOSS_MS;
        const float e = 1.0f - (1.0f - t) * (1.0f - t);
        const int x = x0 + (int)((x1 - x0) * e);
        const int y = y0 + (int)((y1 - y0) * t) - (int)(4.0f * PAL_FEED_ARC_PX * t * (1.0f - t));
        drawPalBmp(f.bmp, f.w, f.h, f.bpr, x, y, SLEEP_FOOD_SCALE, true);
    } else {
        const uint32_t eat = ms - PAL_FEED_TOSS_MS;
        const uint32_t eatMs = PAL_FEED_MS - PAL_FEED_TOSS_MS;
        const pal_step_t* step = palEatStep(_palId, eat);
        float p = (float)eat / eatMs;
        if (p > 1.0f) p = 1.0f;
        drawSleepPalCorner(step ? step->bmp : nullptr, step ? step->dy : 0,
                           fromFill + (int)((toFill - fromFill) * p + 0.5f));
        const int hw = PAL_HEART_W * PAL_HEART_SCALE, hh = PAL_HEART_H * PAL_HEART_SCALE;
        const int hy = palBarY() - 4 - hh - (int)(PAL_HEART_RISE_PX * p);
        drawPalBmp(PAL_HEART_BMP, PAL_HEART_W, PAL_HEART_H, 1, TOAST_MARGIN + (PAL_BAR_W - hw) / 2, hy,
                   PAL_HEART_SCALE, true);
    }
    if (full) {
        flushFramebuffer();
        return;
    }
    // Everything that moves is below the heart's highest point: send from there down.
    const int top = palBarY() - 4 - PAL_HEART_H * PAL_HEART_SCALE - PAL_HEART_RISE_PX;
    flushLines(top < 0 ? 0 : top, HEIGHT);
}

void drawSleep(uint8_t happiness, bool breathIn, bool faceOnly, const char* status) {
    drawSleepFrame(happiness, breathIn, faceOnly, status, false);
}

void drawAwake(uint8_t happiness) {
    drawSleepFrame(happiness, false, false, nullptr, true);
}

// Same size and corner as the pal on the sleep face, so the welcome reads as
// the pal taking the spot it will keep.
constexpr int PAL_WELCOME_SCALE = 6;

// The awake face, full screen, as the wake handoff shows it, with the new pal
// sliding in from the left to the bottom-left corner. Drawn in sleep-face
// colours and inverted at the end, like drawSleepFrame()'s awake branch.
void drawPalWelcome(uint8_t happiness, uint8_t palId, float slideT, uint32_t ms, bool full) {
    clearButtonFeedback();
    memset(_framebuffer, 0, sizeof(_framebuffer));
    const uint8_t band = Chirp::band(happiness);
    const int centers[] = {Face::EYE_L_X + 2 * Face::B, Face::EYE_R_X + 2 * Face::B};
    for (int center : centers) Face::awakeEye(center, Face::EYE_Y, band, center < WIDTH / 2);
    Face::mouth(Face::MOUTH_X, Face::MOUTH_Y, band);

    PalInfo info = palLookup(palId);
    int palTop = HEIGHT;
    if (info.dim) {
        char text[32];
        snprintf(text, sizeof(text), "say hi to %s", info.name);
        const int boxW = UI::textWidth(text, TOAST_FONT) + TOAST_PADDING_H * 2;
        const int boxH = UI::fontHeight(TOAST_FONT) + TOAST_PADDING_V * 2;
        const int boxX = WIDTH - boxW - TOAST_MARGIN;
        fillRoundedRect(boxX, TOAST_MARGIN, boxW, boxH, TOAST_RADIUS, false);
        drawRoundedRectBorder(boxX, TOAST_MARGIN, boxW, boxH, TOAST_RADIUS, true);
        drawText(text, boxX + TOAST_PADDING_H, TOAST_MARGIN + TOAST_PADDING_V, TOAST_FONT, true);

        const int ps = PAL_WELCOME_SCALE;
        const int pw = info.dim->w * ps, ph = info.dim->h * ps;
        if (slideT < 0.0f) slideT = 0.0f;
        if (slideT > 1.0f) slideT = 1.0f;
        int8_t dx = 0, dy = 0;
        const uint8_t* bmp = palIdleFrame(info.idle, ms, &dx, &dy);
        const int restX = TOAST_MARGIN;
        const int x = -pw + (int)((restX + pw) * slideT + 0.5f) + dx * ps;
        drawPalBmp(bmp, info.dim->w, info.dim->h, info.dim->bpr, x, HEIGHT - ph + dy * ps, ps, true);
        // Room for the highest hop any pal makes (three of its own pixels).
        palTop = HEIGHT - ph - 3 * ps;
    }

    for (uint8_t& byte : _framebuffer) byte = ~byte;
    // Only the pal moves once the screen is up: send its rows and nothing else.
    if (full) flushFramebuffer();
    else flushLines(palTop < 0 ? 0 : palTop, HEIGHT);
}

void drawWakeExit(uint8_t happiness, float t) {
    clearButtonFeedback();
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    const int dy = -(int)(HEIGHT * t + 0.5f);
    const uint8_t band = Chirp::band(happiness);
    memset(_framebuffer, 0, sizeof(_framebuffer));
    Face::awakeEye(Face::EYE_L_X + 2 * Face::B, Face::EYE_Y + dy, band, true);
    Face::awakeEye(Face::EYE_R_X + 2 * Face::B, Face::EYE_Y + dy, band, false);
    Face::mouth(Face::MOUTH_X, Face::MOUTH_Y + dy, band);
    drawSleepBadge(false);
    for (uint8_t& byte : _framebuffer) byte = ~byte;
    if (!_wakeExitActive || t == 0.0f) {
        flushFramebuffer();
    } else {
        // Erase the old face and draw the new one without resending the fixed badge.
        const int top = Face::EYE_Y + (dy < _wakeExitDy ? dy : _wakeExitDy);
        const int bottom = Face::MOUTH_Y + 2 * Face::B + (dy > _wakeExitDy ? dy : _wakeExitDy);
        if (bottom > 0) flushLines(top > 0 ? top : 0, bottom);
    }
    _wakeExitDy = dy;
    _wakeExitActive = t < 1.0f;
}

void drawWakeProgress(uint32_t heldMs, uint32_t thresholdMs) {
    int rows = HoldProgress::pixels(heldMs, thresholdMs, HEIGHT);
    if (rows <= _wakeFilledRows) return;
    int prevStart = HEIGHT - _wakeFilledRows;
    int newStart = HEIGHT - rows;
    for (int y = newStart; y < prevStart; y++) {
        uint8_t* row = &_framebuffer[y * BYTES_PER_LINE];
        for (int x = 0; x < BYTES_PER_LINE; x++) row[x] = ~row[x];
    }
    flushLines(newStart, prevStart);
    _wakeFilledRows = rows;
}

void clearWakeProgress() {
    _wakeFilledRows = 0;
}

void toggleVcom() {
    _vcom = !_vcom;
    _lastVcomToggle = millis();

    // Send VCOM toggle command (no data, just updates the internal VCOM state)
    digitalWrite(_cs, HIGH);
    delayMicroseconds(6);
    sendByte(makeCommand(CMD_VCOM));
    sendByte(0x00);
    delayMicroseconds(2);
    digitalWrite(_cs, LOW);
}

void refresh() {
    if (_pressFlashIcon && !pressFlashActive()) {
        _pressFlashIcon = nullptr;
        if (_pressButton.filledPixels) {
            invertButtonColumns(_pressButton.x, _pressButton.y, _pressButton.w, _pressButton.h, 0, _pressButton.w);
            _pressButton.filledPixels = 0;
            flushLines(_pressButton.y, _pressButton.y + _pressButton.h);
        }
    }
    // Toggle VCOM at least every VCOM_INTERVAL_MS to prevent DC bias / burn-in
    if (millis() - _lastVcomToggle >= VCOM_INTERVAL_MS) {
        toggleVcom();
    }
}

}
