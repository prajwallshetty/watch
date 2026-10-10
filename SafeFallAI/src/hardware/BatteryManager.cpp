#include "BatteryManager.h"
#include "../config/BoardConfig.h"
#include "../config/AppConfig.h"
#include "../config/DebugConfig.h"

BatteryManager Battery;

// Single-cell LiPo resting-voltage curve (typical, 0.2C). Engineering
// approximation; the true curve depends on the cell and load.
struct CurvePoint { float v; uint8_t pct; };
static const CurvePoint kCurve[] = {
    {4.20f, 100}, {4.10f, 90}, {4.00f, 80}, {3.92f, 70}, {3.85f, 60}, {3.80f, 50},
    {3.75f, 40},  {3.71f, 30}, {3.67f, 20}, {3.62f, 10}, {3.50f, 5},  {3.30f, 0},
};

void BatteryManager::begin() {
    analogReadResolution(BOARD_BAT_ADC_BITS);
    pinMode(BOARD_BAT_ADC_PIN, INPUT);
    m_lastSampleMs = millis();
    update(m_lastSampleMs);
    LOG_BATTERY("%.2f V, %u%%%s", m_voltage, m_percent, m_present ? "" : " (no battery detected)");
}

float BatteryManager::sampleVoltage() {
    uint32_t sum = 0;
    for (int i = 0; i < BATTERY_ADC_SAMPLES; i++) sum += analogRead(BOARD_BAT_ADC_PIN);
    const float adValue = (float)sum / BATTERY_ADC_SAMPLES;
    // Waveshare: 3.3 / (1 << 12) * 3 * AD_Value
    return BOARD_BAT_ADC_REF_V / (float)(1 << BOARD_BAT_ADC_BITS) * BOARD_BAT_DIVIDER * adValue;
}

uint8_t BatteryManager::voltageToPercent(float v) {
    const size_t n = sizeof(kCurve) / sizeof(kCurve[0]);
    if (v >= kCurve[0].v) return 100;
    if (v <= kCurve[n - 1].v) return 0;
    for (size_t i = 1; i < n; i++) {
        if (v >= kCurve[i].v) {
            const float span = kCurve[i - 1].v - kCurve[i].v;
            const float frac = (v - kCurve[i].v) / span;
            return (uint8_t)(kCurve[i].pct + frac * (kCurve[i - 1].pct - kCurve[i].pct) + 0.5f);
        }
    }
    return 0;
}

bool BatteryManager::update(uint32_t nowMs) {
    if (m_primed && (nowMs - m_lastSampleMs) < BATTERY_SAMPLE_INTERVAL_MS) return false;
    m_lastSampleMs = nowMs;

    const float v = sampleVoltage();
    m_voltage = m_primed ? (m_voltage * 0.8f + v * 0.2f) : v;
    m_primed = true;

    const uint8_t prevPct = m_percent;
    const bool prevLow = m_low;
    const bool prevCrit = m_critical;
    const bool prevPresent = m_present;

    m_present = m_voltage >= BATTERY_PRESENT_MIN_V;
    uint8_t pct = m_present ? voltageToPercent(m_voltage) : 0;
    // Hold the displayed value unless it moved at least 1 % to stop flicker.
    m_percent = (abs((int)pct - (int)prevPct) >= 1) ? pct : prevPct;

    if (m_present) {
        if (!m_low && m_percent <= BATTERY_LOW_PERCENT) m_low = true;
        else if (m_low && m_percent > BATTERY_LOW_PERCENT + BATTERY_HYSTERESIS_PERCENT) m_low = false;
        if (!m_critical && m_percent <= BATTERY_CRITICAL_PERCENT) m_critical = true;
        else if (m_critical && m_percent > BATTERY_CRITICAL_PERCENT + BATTERY_HYSTERESIS_PERCENT) m_critical = false;
    } else {
        m_low = false;
        m_critical = false;
    }

    if (m_low != prevLow || m_critical != prevCrit) {
        LOG_BATTERY("%u%% (%.2f V) %s", m_percent, m_voltage,
                    m_critical ? "CRITICAL" : (m_low ? "LOW" : "OK"));
    }
    if (m_present != prevPresent) {
        LOG_BATTERY("Battery %s (%.2f V)", m_present ? "connected" : "not detected", m_voltage);
    }
    return m_percent != prevPct || m_low != prevLow || m_critical != prevCrit || m_present != prevPresent;
}
