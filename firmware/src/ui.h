#pragma once

#include <stdint.h>
#include "spleen_fonts.h"
#include "icons.h"

namespace UI {

// Font selection
enum class Font {
    Small,   // Spleen 6x12
    Large    // Spleen 8x16
};

// Get font dimensions
inline int fontWidth(Font font) {
    return (font == Font::Large) ? SPLEEN8X16_WIDTH : SPLEEN6X12_WIDTH;
}

inline int fontHeight(Font font) {
    return (font == Font::Large) ? SPLEEN8X16_HEIGHT : SPLEEN6X12_HEIGHT;
}

// Get glyph data for a character
inline const uint8_t* getGlyph(char c, Font font) {
    if (font == Font::Large) {
        return spleen8x16_getGlyph(c);
    }
    return spleen6x12_getGlyph(c);
}

// Calculate text width in pixels
inline int textWidth(const char* text, Font font) {
    int len = 0;
    while (text[len]) len++;
    int charW = fontWidth(font);
    return len * (charW + 1) - 1;
}

// Icon size
constexpr int ICON_SIZE = Icons::SIZE;

// Battery level
enum class BatteryLevel {
    Empty,
    Low,
    Medium,
    Full
};

inline const uint8_t* getBatteryIcon(BatteryLevel level) {
    switch (level) {
        case BatteryLevel::Low:    return Icons::BATTERY_LOW;
        case BatteryLevel::Medium: return Icons::BATTERY_MED;
        case BatteryLevel::Full:   return Icons::BATTERY_FULL;
        default:                   return Icons::BATTERY_EMPTY;
    }
}

inline BatteryLevel batteryPercentToLevel(int percent) {
    if (percent >= 75) return BatteryLevel::Full;
    if (percent >= 40) return BatteryLevel::Medium;
    if (percent >= 15) return BatteryLevel::Low;
    return BatteryLevel::Empty;
}

// Signal strength
enum class SignalLevel {
    None,
    Low,
    Medium,
    High,
    Full
};

inline const uint8_t* getSignalIcon(SignalLevel level) {
    switch (level) {
        case SignalLevel::Low:    return Icons::SIGNAL_1;
        case SignalLevel::Medium: return Icons::SIGNAL_2;
        case SignalLevel::High:   return Icons::SIGNAL_3;
        case SignalLevel::Full:   return Icons::SIGNAL_FULL;
        default:                  return Icons::SIGNAL_0;
    }
}

// Mail icon
inline const uint8_t* getMailIcon() {
    return Icons::MAIL;
}

// Review-screen action icons
inline const uint8_t* getSendIcon() {
    return Icons::SEND;
}

inline const uint8_t* getTrashIcon() {
    return Icons::TRASH;
}

}  // namespace UI
