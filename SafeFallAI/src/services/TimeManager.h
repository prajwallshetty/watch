/**
 * SafeFall AI - TimeManager.h
 *
 * This board has no battery-backed RTC, so wall-clock time comes from the
 * phone (BLE TIME_SYNC). The ESP32 system clock survives software resets, so
 * a sync also persists across watchdog/safe reboots until power is lost.
 * Until synced, the clock shows "--:--" and packets carry timestamp 0 plus
 * the device uptime.
 */
#pragma once

#include <Arduino.h>

class TimeManager {
public:
    void begin(int32_t tzOffsetS);
    bool sync(uint32_t epochUtc, int32_t tzOffsetS);   // from TIME_SYNC
    void setTzOffset(int32_t tzOffsetS) { m_tz = tzOffsetS; }

    bool isSynced() const;
    uint32_t epochUtc() const;                          // 0 when not synced
    uint32_t uptimeS() const { return millis() / 1000UL; }
    int32_t tzOffset() const { return m_tz; }

    // Writes "HH:MM" (or "--:--"). Returns true when the minute changed since
    // the previous call, so the UI only redraws when needed.
    bool formatClock(char* buf, size_t len);
    void formatDate(char* buf, size_t len) const;       // "Thu 10 Oct" or ""

private:
    int32_t m_tz = 0;
    int m_lastMinute = -1;
};

extern TimeManager Clock;
