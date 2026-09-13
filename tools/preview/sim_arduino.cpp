#include <Arduino.h>

SimSerial Serial;

namespace Sim {
namespace {
uint32_t g_millis = 0;
}

void setMillis(uint32_t ms) { g_millis = ms; }
void advanceMillis(uint32_t ms) { g_millis += ms; }
uint32_t nowMillis() { return g_millis; }

}  // namespace Sim
