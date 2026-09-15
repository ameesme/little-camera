#include "identity.h"

#include <Preferences.h>
#include <esp_mac.h>
#include <esp_random.h>

#include "shortcode.h"

namespace Identity {

namespace {

char _id[13] = {0};
char _code[7] = {0};
char _name[8] = {0};
uint8_t _secret[16];
bool _haveSecret = false;

}  // namespace

void init() {
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_BT);
    snprintf(_id, sizeof(_id), "%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    ShortCode::compute(_id, _code);
    snprintf(_name, sizeof(_name), "lc-%02X%02X", mac[4], mac[5]);
    Serial.printf("Identity: %s (%s), code %s\n", _id, _name, _code);
}

const char* cameraId() { return _id; }
const char* shortCode() { return _code; }
const char* deviceName() { return _name; }

const uint8_t* secret() {
    if (_haveSecret) return _secret;
    Preferences prefs;
    if (prefs.begin("sync", false)) {
        if (prefs.getBytesLength("secret") == sizeof(_secret)) {
            prefs.getBytes("secret", _secret, sizeof(_secret));
        } else {
            // First boot ever: mint it. The hardware RNG is seeded once the
            // radio has run at least once, which is why this is lazy rather
            // than done in init().
            esp_fill_random(_secret, sizeof(_secret));
            prefs.putBytes("secret", _secret, sizeof(_secret));
            Serial.println("Identity: minted a new secret");
        }
        prefs.end();
        _haveSecret = true;
    }
    return _secret;
}

}
