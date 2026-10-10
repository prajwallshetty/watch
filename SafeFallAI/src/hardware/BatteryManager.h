/**
 * SafeFall AI - BatteryManager.h
 *
 * Battery voltage on GPIO1 through the board's 200K/100K divider, converted
 * with Waveshare's formula: V = 3.3 / (1 << 12) * 3 * AD_value.
 * Readings are averaged and low-pass filtered, then mapped to a percentage
 * with a single-cell LiPo discharge curve.
 */
#pragma once

#include <Arduino.h>

class BatteryManager {
public:
    void begin();
    // Returns true when the displayed percentage or a warning level changed.
    bool update(uint32_t nowMs);

    float voltage() const { return m_voltage; }
    uint16_t millivolts() const { return (uint16_t)(m_voltage * 1000.0f + 0.5f); }
    uint8_t percent() const { return m_percent; }
    bool present() const { return m_present; }
    bool isLow() const { return m_low; }
    bool isCritical() const { return m_critical; }

    static uint8_t voltageToPercent(float v);

private:
    float sampleVoltage();

    float m_voltage = 0.0f;
    uint8_t m_percent = 0;
    bool m_present = false;
    bool m_low = false;
    bool m_critical = false;
    bool m_primed = false;
    uint32_t m_lastSampleMs = 0;
};

extern BatteryManager Battery;
