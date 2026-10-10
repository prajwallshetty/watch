/**
 * SafeFall AI - SettingsStore.h
 *
 * User settings persisted in NVS (Preferences, namespace "safefall").
 * Writes are debounced so dragging brightness up and down does not wear the
 * flash; the event counter is written immediately (event-ID uniqueness).
 */
#pragma once

#include <Arduino.h>

struct UserSettings {
    uint8_t brightness;      // percent
    bool monitoring;         // fall detection enabled
    uint8_t sensitivity;     // 0 low, 1 normal, 2 high
    uint8_t countdownS;      // fall confirmation countdown
    uint8_t screenTimeoutS;  // display off after idle
    int32_t tzOffsetS;       // local time offset from UTC, seconds
};

class SettingsStore {
public:
    void begin();
    void update(uint32_t nowMs);   // flushes debounced writes

    const UserSettings& get() const { return m_s; }
    void setBrightness(uint8_t pct);
    void setMonitoring(bool en);
    void setSensitivity(uint8_t s);
    void setCountdown(uint8_t s);
    void setScreenTimeout(uint8_t s);
    void setTzOffset(int32_t s);

    uint32_t loadEventCounter();
    void saveEventCounter(uint32_t v);
    uint32_t incrementBootCount();

    void flushNow();

private:
    void markDirty();
    UserSettings m_s;
    bool m_dirty = false;
    uint32_t m_dirtyMs = 0;
};

extern SettingsStore Settings;
