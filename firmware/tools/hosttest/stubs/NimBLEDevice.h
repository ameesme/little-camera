#pragma once
// Compile-check stub for the slice of NimBLE-Arduino 2.x that sync.cpp uses.
// Signatures mirror the real 2.x headers (callbacks take NimBLEConnInfo&,
// notify() returns bool). It exists so `make check` can catch typos in
// sync.cpp without the target toolchain; it is not NimBLE.
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <vector>


#define BLE_HS_IO_DISPLAY_ONLY 0
#define BLE_HS_IO_NO_INPUT_OUTPUT 3

namespace NIMBLE_PROPERTY {
// Stand-ins, not the real bit values — nothing here talks to a controller.
enum : uint16_t {
    READ = 1, READ_ENC = 2, READ_AUTHEN = 4, WRITE = 8, NOTIFY = 16,
    WRITE_NR = 32, WRITE_ENC = 64, WRITE_AUTHEN = 128,
};
}

class NimBLEConnInfo {
public:
    uint16_t getMTU() const { return 23; }
    uint16_t getConnHandle() const { return 0; }
    bool isEncrypted() const { return false; }
    bool isAuthenticated() const { return false; }
    bool isBonded() const { return false; }
};

class NimBLEAttValue {
public:
    const uint8_t* data() const { return nullptr; }
    size_t size() const { return 0; }
    size_t length() const { return 0; }
};

class NimBLECharacteristic;
class NimBLECharacteristicCallbacks {
public:
    virtual ~NimBLECharacteristicCallbacks() {}
    virtual void onRead(NimBLECharacteristic*, NimBLEConnInfo&) {}
    virtual void onWrite(NimBLECharacteristic*, NimBLEConnInfo&) {}
    virtual void onSubscribe(NimBLECharacteristic*, NimBLEConnInfo&, uint16_t) {}
};

class NimBLECharacteristic {
public:
    NimBLEAttValue getValue() { return NimBLEAttValue(); }
    bool setValue(const uint8_t*, size_t) { return true; }
    bool notify(uint16_t connHandle = 0xFFFF) { return true; }
    void setCallbacks(NimBLECharacteristicCallbacks*) {}
};

class NimBLEService {
public:
    NimBLECharacteristic* createCharacteristic(const char*, uint32_t) { return nullptr; }
    bool start() { return true; }
};

class NimBLEServer;
class NimBLEServerCallbacks {
public:
    virtual ~NimBLEServerCallbacks() {}
    virtual void onConnect(NimBLEServer*, NimBLEConnInfo&) {}
    virtual void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int) {}
    virtual void onMTUChange(uint16_t, NimBLEConnInfo&) {}
    virtual uint32_t onPassKeyDisplay() { return 123456; }
    virtual void onAuthenticationComplete(NimBLEConnInfo&) {}
};

class NimBLEServer {
public:
    void setCallbacks(NimBLEServerCallbacks*, bool deleteCallbacks = true) {}
    void advertiseOnDisconnect(bool) {}
    NimBLEService* createService(const char*) { return nullptr; }
    bool disconnect(uint16_t, uint8_t = 0) { return true; }
    std::vector<uint16_t> getPeerDevices() { return {}; }
};

class NimBLEAdvertising {
public:
    bool setName(const char*) { return true; }
    bool addServiceUUID(const char*) { return true; }
    void setMinInterval(uint16_t) {}
    void setMaxInterval(uint16_t) {}
    void enableScanResponse(bool) {}
    bool start(uint32_t = 0) { return true; }
};

class NimBLEDevice {
public:
    static bool init(const char*) { return true; }
    static bool deinit(bool = false) { return true; }
    static bool setMTU(uint16_t) { return true; }
    static void setSecurityAuth(bool, bool, bool) {}
    static void setSecurityIOCap(uint8_t) {}
    static NimBLEServer* createServer() { return nullptr; }
    static NimBLEServer* getServer() { return nullptr; }
    static NimBLEAdvertising* getAdvertising() { return nullptr; }
    static bool startAdvertising(uint32_t = 0) { return true; }
    static bool stopAdvertising() { return true; }
};
