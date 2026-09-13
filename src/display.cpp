#include "display.h"
#include "splash_data.h"
#include "sleep_data.h"
#include "ui.h"
#include <SPI.h>

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

    // DISP is GPIO-driven on the Xiao_Shutter carrier (was tied high on the
    // old board) — must be HIGH for the panel to show pixels
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

// The captured photo, dithered once and kept as 1-bit. The capture -> save
// slide re-blits this at a new offset every frame; re-running Floyd-Steinberg
// per frame would roughly double the cost of a frame for an identical result,
// since the dither loop works in image coordinates and doesn't care where the
// photo lands on the panel.
constexpr int PHOTO_WIDTH = 320;
constexpr int PHOTO_BYTES = PHOTO_WIDTH / 8;  // 40
static uint8_t _photoBits[PHOTO_BYTES * HEIGHT];  // 9600 bytes

// UI state
static int _batteryPercent = 100;
static int _inboxCount = 0;
static UI::SignalLevel _signalLevel = UI::SignalLevel::Full;

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
// The gesture line sits as low as the corner radius allows. It's centred
// horizontally and every gesture word is far narrower than the box, so the
// 10px corners never reach it.
constexpr int BOX_PADDING_BOTTOM = 4;
constexpr int BOX_LABEL_GAP = 3;  // Icon to label

// Default font for sidebar UI
constexpr UI::Font SIDEBAR_FONT = UI::Font::Large;
// The buttons carry a second line naming the gesture, which needs the small face
constexpr UI::Font HINT_FONT = UI::Font::Small;

// Toast layout constants
constexpr int TOAST_PADDING_H = 10;   // Horizontal padding inside toast
constexpr int TOAST_PADDING_V = 6;    // Vertical padding inside toast
constexpr int TOAST_MARGIN = 8;       // Margin from screen edges
constexpr int TOAST_RADIUS = 8;       // Corner radius
constexpr int TOAST_MIN_X = 80;       // Don't overlap sidebar
constexpr UI::Font TOAST_FONT = UI::Font::Large;

// Set pixel in framebuffer (0=black, 1=white)
static void setPixel(int x, int y, bool white) {
    if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT) return;
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
static void drawTextCenteredAt(const char* text, int boxX, int boxW, int y, UI::Font font, bool white) {
    int textW = UI::textWidth(text, font);
    int startX = boxX + (boxW - textW) / 2;
    drawText(text, startX, y, font, white);
}

// A filled box with a centred icon, a label hung under it, and the gesture that
// triggers it pinned to the bottom edge. Shared by the viewfinder sidebar and
// the save-screen actions so the two columns stay visually identical.
static void drawBoxButton(int x, int y, int w, int h,
                          const uint8_t* icon, const char* label, const char* gesture) {
    // setPixel clips, so drawing off-panel is harmless — but during the slide
    // one column is always fully off-screen, and skipping it saves ~16k
    // no-op setPixel calls per frame.
    if (x + w <= 0 || x >= WIDTH) return;

    fillRoundedRect(x, y, w, h, BOX_RADIUS, false);

    // The icon is the anchor: it sits dead centre in the box, with the label
    // hung directly under it. The gesture line is not part of that group — it's
    // pinned to the bottom edge, so it reads as a footnote on the button rather
    // than a third line of the title.
    int iconY = y + (h - UI::ICON_SIZE) / 2;
    int labelY = iconY + UI::ICON_SIZE + BOX_LABEL_GAP;
    int gestureY = y + h - BOX_PADDING_BOTTOM - UI::fontHeight(HINT_FONT);

    drawIconCentered(icon, UI::ICON_SIZE, UI::ICON_SIZE, x, w, iconY, true);
    drawTextCenteredAt(label, x, w, labelY, SIDEBAR_FONT, true);
    drawTextCenteredAt(gesture, x, w, gestureY, HINT_FONT, true);
}

// Viewfinder sidebar: single full-height inbox box. x is a parameter so the
// save slide can walk it off the left edge.
static void drawSidebar(int x) {
    drawBoxButton(x, SIDEBAR_PADDING, BOX_WIDTH, INBOX_HEIGHT,
                  UI::getMailIcon(), "inbox", "hold");
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

void setBatteryPercent(int percent) {
    _batteryPercent = percent < 0 ? 0 : (percent > 100 ? 100 : percent);
}

void setInboxCount(int count) {
    _inboxCount = count < 0 ? 0 : count;
}

void setSignalLevel(UI::SignalLevel level) {
    _signalLevel = level;
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

// Push the whole framebuffer to the panel in one CS-high transaction.
static void flushFramebuffer() {
    digitalWrite(_cs, HIGH);
    delayMicroseconds(6);

    sendByte(makeCommand(CMD_WRITE));

    uint8_t* fbPtr = _framebuffer;
    for (int line = 1; line <= HEIGHT; line++) {
        sendByte(line);
        _spi->transferBytes(fbPtr, nullptr, BYTES_PER_LINE);
        fbPtr += BYTES_PER_LINE;
        sendByte(0x00);
    }

    sendByte(0x00);

    delayMicroseconds(2);
    digitalWrite(_cs, LOW);
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

// Render the photo card and both columns at an arbitrary point on the capture
// -> save slide. t=0 is the capture layout (sidebar in place, actions parked
// off the right edge), t=1 is the save layout (sidebar gone off the left,
// actions in place). The whole layout moves as one block, so a single offset
// drives all three elements — that's the "everything shifts over" effect.
static void renderSaveLayout(float t) {
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    const int shift = (int)(SAVE_SLIDE_PX * t + 0.5f);

    // The card is the leftmost 315 source columns, same as the viewfinder
    // crops. Keeping the crop identical everywhere means the slide is a pure
    // translation and the photo doesn't jump sideways relative to itself at
    // either the capture moment or the end of the slide.
    const int photoX = PHOTO_X_CAPTURE - shift;
    blitPhoto(photoX);
    applyImagePaddingAndCorners(photoX, PHOTO_WIDTH, photoX, photoX + CARD_WIDTH);

    drawSidebar(SIDEBAR_PADDING - shift);
    drawActionBar(WIDTH - shift);

    renderToast();
    flushFramebuffer();
}

void drawViewfinder(const uint8_t* grayscale, int srcWidth, int srcHeight) {
    if (!grayscale) return;
    bayerPhoto(grayscale, srcWidth);
    renderSaveLayout(0.0f);
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

void drawSplash() {
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

void drawSleep() {
    // Copy sleep bitmap to framebuffer
    const uint8_t* ptr = SLEEP_BITMAP;
    for (int y = 0; y < HEIGHT; y++) {
        for (int x = 0; x < BYTES_PER_LINE; x++) {
            _framebuffer[y * BYTES_PER_LINE + x] = pgm_read_byte(ptr++);
        }
    }

    // Draw "press to wake" top-right, inverted (white on black)
    const char* text = "press to wake";
    int textW = UI::textWidth(text, TOAST_FONT);
    int textH = UI::fontHeight(TOAST_FONT);
    int boxW = textW + TOAST_PADDING_H * 2;
    int boxH = textH + TOAST_PADDING_V * 2;
    int boxX = WIDTH - boxW - TOAST_MARGIN;
    int boxY = TOAST_MARGIN;

    fillRoundedRect(boxX, boxY, boxW, boxH, TOAST_RADIUS, false);  // Black bg
    drawRoundedRectBorder(boxX, boxY, boxW, boxH, TOAST_RADIUS, true);  // White border
    drawText(text, boxX + TOAST_PADDING_H, boxY + TOAST_PADDING_V, TOAST_FONT, true);  // White text

    // Send framebuffer to display
    digitalWrite(_cs, HIGH);
    delayMicroseconds(6);

    sendByte(makeCommand(CMD_WRITE));

    uint8_t* fbPtr = _framebuffer;
    for (int line = 1; line <= HEIGHT; line++) {
        sendByte(line);
        _spi->transferBytes(fbPtr, nullptr, BYTES_PER_LINE);
        fbPtr += BYTES_PER_LINE;
        sendByte(0x00);
    }

    sendByte(0x00);

    delayMicroseconds(2);
    digitalWrite(_cs, LOW);
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
    // Toggle VCOM at least every VCOM_INTERVAL_MS to prevent DC bias / burn-in
    if (millis() - _lastVcomToggle >= VCOM_INTERVAL_MS) {
        toggleVcom();
    }
}

}
