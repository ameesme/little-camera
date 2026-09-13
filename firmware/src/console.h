#pragma once

#include <Arduino.h>

// Line-oriented commands on the USB-CDC console: `ls`, `get N`, `stat`. The
// wired way to get photos off the camera when Bluetooth or the phone is not
// cooperating; firmware/tools/export/pull_serial.py speaks it. Protocol in
// docs/protocol.md §6.
//
// Polled from loop(), never blocks, and only does work when bytes arrived —
// so it costs nothing when nothing is plugged in.
namespace Console {

void begin();

// Returns true if a command ran this call, so the caller can count it as
// activity and hold off sleep.
bool poll();

}
