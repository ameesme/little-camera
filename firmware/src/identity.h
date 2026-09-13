#pragma once

#include <Arduino.h>

// Who this camera is, as far as the phone and the server are concerned. The id
// is the Bluetooth MAC (unique per chip, no provisioning step), the short code
// is derived from it (docs/protocol.md §1), and the secret is 16 random bytes
// minted once and kept in NVS. The secret is what the app presents to the
// server on the camera's behalf, so it is only handed out over an encrypted
// BLE link.
namespace Identity {

void init();

const char* cameraId();    // 12 lowercase hex characters
const char* shortCode();   // 6 characters from the protocol alphabet
const char* deviceName();  // "lc-XXXX", advertised name

// 16 bytes. Generated on first call across the life of the device.
const uint8_t* secret();

}
