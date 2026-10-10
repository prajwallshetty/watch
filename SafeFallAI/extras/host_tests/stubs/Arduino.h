// Host-test stub of the small Arduino surface used by SafeFall's pure-logic modules.
#pragma once
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <cstdarg>

#define IRAM_ATTR
typedef uint8_t byte;

extern uint32_t g_millis;
inline uint32_t millis() { return g_millis; }
inline void delay(uint32_t ms) { g_millis += ms; }

using std::min;
using std::max;
template <typename T, typename L, typename H>
inline T constrain(T v, L lo, H hi) { return v < (T)lo ? (T)lo : (v > (T)hi ? (T)hi : v); }

struct HostSerial {
    bool quiet = true;
    void printf(const char* fmt, ...) {
        if (quiet) return;
        va_list ap;
        va_start(ap, fmt);
        vprintf(fmt, ap);
        va_end(ap);
    }
};
extern HostSerial Serial;
