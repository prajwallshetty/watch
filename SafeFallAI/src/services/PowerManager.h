/**
 * SafeFall AI - PowerManager.h
 *
 * - Screen power policy: ON -> DIM -> OFF after inactivity; forced ON while an
 *   alert needs the user. Only the display sleeps: IMU sampling, fall
 *   detection and BLE keep running regardless of screen state.
 * - BOOT key: short press wakes the screen, long press (3 s) raises SOS.
 * - Task watchdog configuration and feeding.
 * - Reset-reason capture and a safe reboot path.
 */
#pragma once

#include <Arduino.h>

class PowerManager {
public:
    void begin();
    void update(uint32_t nowMs, bool forceAwake, uint8_t timeoutS);

    void noteActivity(uint32_t nowMs);
    bool wakeFromTouch();               // TouchManager wake handler; true = swallow the press
    bool isScreenOff() const;

    bool consumeBootButtonSOS();        // long press detected since last call

    void feedWatchdog();
    bool watchdogArmed() const { return m_wdtArmed; }

    const char* resetReason() const { return m_resetReason; }
    bool lastResetAbnormal() const { return m_abnormalReset; }
    uint32_t bootCount() const { return m_bootCount; }

    void safeReboot(const char* reason);

private:
    void pollBootButton(uint32_t nowMs);

    uint32_t m_lastActivityMs = 0;
    const char* m_resetReason = "UNKNOWN";
    bool m_abnormalReset = false;
    bool m_wdtArmed = false;
    uint32_t m_bootCount = 0;

    bool m_btnDown = false;
    uint32_t m_btnDownMs = 0;
    bool m_btnFired = false;
    bool m_sosPending = false;
};

extern PowerManager Power;
