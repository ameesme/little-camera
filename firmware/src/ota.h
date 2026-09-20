#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

// Firmware update, camera side. The phone pushes an image over BLE
// (docs/protocol.md §3.7) and this writes it to the *other* app slot in the
// partition table — `default_8MB.csv` has two of 3.3MB and the running
// firmware only ever fills one, so the room was already paid for.
//
// The whole design is about one thing: a half-written image must not cost the
// owner their camera. Four things stand between the two:
//
//   1. The bytes go to the inactive slot. Whatever happens to it, the slot the
//      camera is running from is untouched and still bootable.
//   2. The bootloader is only pointed at the new slot after every byte has
//      arrived, the SHA-256 of the stream matches what the phone promised, and
//      esp_ota_end() has checked the image's own embedded digest.
//   3. The first boot of a new image is a trial: begin() marks it, confirm()
//      clears it once the camera is demonstrably up. An image that never gets
//      that far is rolled back — by the bootloader if the core was built with
//      rollback, by our own NVS counter if it wasn't.
//   4. Nothing here touches the `spiffs` partition, so photos are not in the
//      blast radius at any point.
//
// Threading: everything runs on the main loop. Sync stages the BLE writes and
// hands them over from Sync::loop(); a flash write blocks for tens of
// milliseconds and has no business on the radio's task.
namespace Ota {

// Mirrors the wire states in sync_protocol.h (UPDATE_IDLE..UPDATE_FAILED).
enum class State : uint8_t { Idle = 0, Receiving = 1, Verifying = 2, Ready = 3, Failed = 4 };

// Boot-time bookkeeping: adopt the trial we are running, or give up on it and
// go back. Call early in setup(), before anything slow — a rollback reboots.
void begin();

// This image works. Ends the trial (and with it the automatic rollback).
// Cheap and idempotent after the first call; main.cpp calls it once the camera
// has been up long enough to have proved the point.
void confirm();

bool trial();            // Running an image that has not confirmed itself yet
uint32_t freeSpace();    // Bytes the inactive slot can take; 0 if there is no second slot

// Open a session for an image of `size` bytes with that SHA-256. False if it
// does not fit, if flash refuses, or if a session is already open for another
// image; lastStatus() says which.
bool start(uint32_t size, const uint8_t sha256[32]);

// True if `sha256` is the image the open session is already receiving — the
// phone reconnecting after a dropped link, which resumes from received().
bool resumes(const uint8_t sha256[32]);

// Append `len` bytes at received(). Writes are sequential by construction:
// the caller only ever passes the chunk at that exact offset. False on a flash
// error, which ends the session.
bool write(const uint8_t* data, size_t len);

// Every byte is in: verify the digest, let the bootloader check the image,
// point it at the new slot and mark the trial. True means the camera is ready
// to reboot into the new firmware — the caller does the rebooting, so the
// screen can say so first.
bool finish();

// Throw the session away. The running firmware is untouched; the half-written
// slot is simply never armed.
void abort();

State state();
uint8_t lastStatus();    // The SyncProto STATUS_* for the last failure
uint32_t received();
uint32_t total();
int percent();           // 0-100 of the image received, 0 when idle

// Console helpers (docs/protocol.md §6).
const char* runningLabel();
const char* otherLabel();
// Point the bootloader back at the other slot, if it holds a valid image. The
// way home after a BLE update: a USB upload writes the first slot while the
// bootloader is still pointed at the second. Caller reboots.
bool revert();

}
