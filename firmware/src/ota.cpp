#include "ota.h"

#include <Preferences.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_system.h>
#include <string.h>

#include "sha256.h"
#include "sync_protocol.h"
#include "version.h"

namespace Ota {

namespace {

using namespace SyncProto;

// NVS: which image is on trial and how many times it has booted without
// confirming. The address rather than the label, because the label is the
// same shape for both slots and the address is what the bootloader acts on.
constexpr const char* NVS_NS = "ota";
constexpr const char* KEY_ADDR = "addr";
constexpr const char* KEY_TRIES = "tries";

// Boots a new image gets to confirm itself before it is given up on. One
// would do if the core's bootloader rollback is on (it rolls back after a
// single unconfirmed boot); three covers the case where it isn't, at the cost
// of two extra reboots of a camera that is already broken.
constexpr uint8_t MAX_TRIES = 3;

// Smaller than a flash sector is not an app image; refuse it before erasing
// anything. The real check is the digest, this just fails fast on nonsense.
constexpr uint32_t MIN_IMAGE_BYTES = 4096;

const esp_partition_t* _running = nullptr;
const esp_partition_t* _target = nullptr;

State _state = State::Idle;
uint8_t _status = STATUS_OK;
bool _trial = false;

esp_ota_handle_t _handle = 0;
uint32_t _received = 0;
uint32_t _total = 0;
uint8_t _digest[SHA256_SIZE] = {0};
Sha256::Ctx _sha;

void forgetTrialMark() {
    Preferences p;
    if (!p.begin(NVS_NS, false)) return;
    p.remove(KEY_ADDR);
    p.remove(KEY_TRIES);
    p.end();
}

void markTrial(uint32_t address) {
    Preferences p;
    if (!p.begin(NVS_NS, false)) {
        // Worth a line: without the mark the trial rests on the bootloader
        // alone, which may not have rollback compiled in.
        Serial.println("Ota: could not mark the trial in NVS");
        return;
    }
    p.putUInt(KEY_ADDR, address);
    p.putUChar(KEY_TRIES, 0);
    p.end();
}

// The session is over and the slot is half written. Release the handle; the
// bootloader is still pointed at the running image, so nothing else to undo.
void fail(uint8_t status) {
    if (_handle) {
        esp_ota_abort(_handle);
        _handle = 0;
    }
    _status = status;
    _state = State::Failed;
}

// Last resort at boot: go back to the other slot. Only reached when an image
// has had MAX_TRIES boots without confirming, so anything is better than
// carrying on with it.
void rollBack(bool pendingVerify) {
    Serial.println("Ota: image never confirmed itself — rolling back");
    // Clear first: whatever we boot next must not inherit this trial.
    forgetTrialMark();
    if (pendingVerify && esp_ota_check_rollback_is_possible()) {
        esp_ota_mark_app_invalid_rollback_and_reboot();  // Does not return
    }
    // No bootloader rollback (or it declined): point at the other slot by
    // hand. set_boot_partition verifies the image there, so a slot that was
    // never written simply refuses and we carry on with what we have.
    const esp_partition_t* other = esp_ota_get_next_update_partition(nullptr);
    if (other && other != _running && esp_ota_set_boot_partition(other) == ESP_OK) {
        Serial.printf("Ota: booting %s instead\n", other->label);
        delay(50);
        esp_restart();
    }
    Serial.println("Ota: nothing valid to roll back to — keeping this image");
}

}  // namespace

void begin() {
    _running = esp_ota_get_running_partition();
    _target = esp_ota_get_next_update_partition(nullptr);
    // A table with one app slot would hand back the slot we are running from.
    // Writing an update into it means overwriting ourselves mid-flight, which
    // is the one thing this whole file exists to prevent.
    if (_target == _running) _target = nullptr;

    Serial.printf("Ota: fw %s running from %s, %u bytes free in %s\n", LC_VERSION_STRING,
                  _running ? _running->label : "?", (unsigned)freeSpace(),
                  _target ? _target->label : "no second slot");

    esp_ota_img_states_t imgState = ESP_OTA_IMG_UNDEFINED;
    const bool pendingVerify = _running && esp_ota_get_state_partition(_running, &imgState) == ESP_OK &&
                               imgState == ESP_OTA_IMG_PENDING_VERIFY;

    Preferences p;
    uint32_t markedAddr = 0;
    uint8_t tries = 0;
    const bool haveNvs = p.begin(NVS_NS, false);
    if (haveNvs) {
        markedAddr = p.getUInt(KEY_ADDR, 0);
        tries = p.getUChar(KEY_TRIES, 0);
    }
    const bool ourMark = _running && markedAddr != 0 && markedAddr == _running->address;

    if (!pendingVerify && !ourMark) {
        // Ordinary boot. A mark for some other slot is stale (we rolled back,
        // or the slot was reflashed over USB): drop it.
        if (haveNvs && markedAddr != 0) {
            p.remove(KEY_ADDR);
            p.remove(KEY_TRIES);
        }
        if (haveNvs) p.end();
        return;
    }

    tries++;
    if (tries > MAX_TRIES) {
        if (haveNvs) p.end();
        rollBack(pendingVerify);
        return;
    }
    if (haveNvs) {
        p.putUChar(KEY_TRIES, tries);
        // The bootloader knows about the trial but NVS doesn't (first boot
        // after an update that could not write its mark): adopt it now.
        if (!ourMark && _running) p.putUInt(KEY_ADDR, _running->address);
        p.end();
    }
    _trial = true;
    Serial.printf("Ota: trial boot %u of %u for %s%s\n", tries, MAX_TRIES,
                  _running ? _running->label : "?", pendingVerify ? " (bootloader is watching)" : "");
}

void confirm() {
    if (!_trial) return;
    _trial = false;
    const esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    forgetTrialMark();
    Serial.printf("Ota: %s confirmed (%s)\n", _running ? _running->label : "?", esp_err_to_name(err));
}

bool trial() { return _trial; }

uint32_t freeSpace() { return _target ? (uint32_t)_target->size : 0; }

bool start(uint32_t size, const uint8_t sha256[32]) {
    if (_state == State::Receiving) {
        // The phone came back after a dropped link and wants the same image:
        // keep the half-written slot and let it resume from received().
        if (resumes(sha256) && size == _total) return true;
        abort();
    }
    _status = STATUS_OK;
    if (!_target) {
        Serial.println("Ota: no second app slot — cannot update");
        _state = State::Failed;
        _status = STATUS_ERROR;
        return false;
    }
    if (size < MIN_IMAGE_BYTES || size > (uint32_t)_target->size) {
        Serial.printf("Ota: refusing %u bytes (slot takes %u)\n", (unsigned)size, (unsigned)_target->size);
        _state = State::Failed;
        _status = STATUS_TOO_LARGE;
        return false;
    }
    // OTA_WITH_SEQUENTIAL_WRITES erases sector by sector as the image lands.
    // Erasing 3.3MB up front would block the main loop for seconds and stall
    // the radio; this way each write pays for at most one 4KB sector.
    const esp_err_t err = esp_ota_begin(_target, OTA_WITH_SEQUENTIAL_WRITES, &_handle);
    if (err != ESP_OK) {
        Serial.printf("Ota: esp_ota_begin failed (%s)\n", esp_err_to_name(err));
        _handle = 0;
        _state = State::Failed;
        _status = STATUS_ERROR;
        return false;
    }
    memcpy(_digest, sha256, SHA256_SIZE);
    Sha256::init(_sha);
    _received = 0;
    _total = size;
    _state = State::Receiving;
    Serial.printf("Ota: receiving %u bytes into %s\n", (unsigned)size, _target->label);
    return true;
}

bool resumes(const uint8_t sha256[32]) {
    return _state == State::Receiving && memcmp(_digest, sha256, SHA256_SIZE) == 0;
}

bool write(const uint8_t* data, size_t len) {
    if (_state != State::Receiving || len == 0) return false;
    if (_received + len > _total) {
        // More bytes than the phone promised: the stream is not what it said
        // it was, and the digest could not be checked against a moving size.
        Serial.println("Ota: more bytes than announced");
        fail(STATUS_BAD_IMAGE);
        return false;
    }
    const esp_err_t err = esp_ota_write(_handle, data, len);
    if (err != ESP_OK) {
        Serial.printf("Ota: flash write failed at %u (%s)\n", (unsigned)_received, esp_err_to_name(err));
        fail(STATUS_ERROR);
        return false;
    }
    Sha256::update(_sha, data, len);
    _received += (uint32_t)len;
    return true;
}

bool finish() {
    if (_state != State::Receiving) {
        _status = STATUS_ERROR;
        return false;
    }
    _state = State::Verifying;
    if (_received != _total) {
        Serial.printf("Ota: %u of %u bytes\n", (unsigned)_received, (unsigned)_total);
        fail(STATUS_BAD_IMAGE);
        return false;
    }
    uint8_t got[SHA256_SIZE];
    Sha256::final(_sha, got);
    if (memcmp(got, _digest, SHA256_SIZE) != 0) {
        // Everything arrived but it is not the image the phone described.
        // This is the check that makes the server and the phone untrusted
        // carriers rather than trusted ones.
        Serial.println("Ota: digest mismatch");
        fail(STATUS_BAD_IMAGE);
        return false;
    }
    // esp_ota_end validates the image's own embedded digest and releases the
    // handle whichever way it goes, so nothing to abort afterwards.
    const esp_err_t ended = esp_ota_end(_handle);
    _handle = 0;
    if (ended != ESP_OK) {
        Serial.printf("Ota: bootloader refused the image (%s)\n", esp_err_to_name(ended));
        _state = State::Failed;
        _status = STATUS_BAD_IMAGE;
        return false;
    }
    const esp_err_t armed = esp_ota_set_boot_partition(_target);
    if (armed != ESP_OK) {
        Serial.printf("Ota: could not arm %s (%s)\n", _target->label, esp_err_to_name(armed));
        _state = State::Failed;
        _status = STATUS_ERROR;
        return false;
    }
    markTrial(_target->address);
    _state = State::Ready;
    _status = STATUS_OK;
    Serial.printf("Ota: %s armed, %u bytes verified — rebooting into it\n", _target->label, (unsigned)_total);
    return true;
}

void abort() {
    // An armed image is not a session any more: the bootloader is already
    // pointed at it and the camera is on its way to a reboot. Tearing that
    // down here is how a successful update would quietly become a no-op.
    if (_state == State::Ready) return;
    if (_state == State::Receiving) {
        Serial.printf("Ota: abandoned at %u of %u bytes\n", (unsigned)_received, (unsigned)_total);
    }
    if (_handle) {
        esp_ota_abort(_handle);
        _handle = 0;
    }
    _received = 0;
    _total = 0;
    _state = State::Idle;
    _status = STATUS_ABORTED;
}

State state() { return _state; }
uint8_t lastStatus() { return _status; }
uint32_t received() { return _received; }
uint32_t total() { return _total; }

int percent() {
    if (!_total) return 0;
    if (_received >= _total) return 100;
    return (int)((uint64_t)_received * 100 / _total);
}

const char* runningLabel() { return _running ? _running->label : "?"; }
const char* otherLabel() { return _target ? _target->label : "none"; }

bool revert() {
    const esp_partition_t* other = esp_ota_get_next_update_partition(nullptr);
    if (!other || other == _running) return false;
    if (esp_ota_set_boot_partition(other) != ESP_OK) return false;
    // The slot we are going back to ran before; it is not on trial.
    forgetTrialMark();
    _trial = false;
    return true;
}

}
