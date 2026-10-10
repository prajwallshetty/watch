#include "PowerManager.h"
#include "SettingsStore.h"
#include "../hardware/DisplayManager.h"
#include "../config/BoardConfig.h"
#include "../config/AppConfig.h"
#include "../config/DebugConfig.h"

#include <esp_system.h>
#include <esp_task_wdt.h>

PowerManager Power;

static const char* resetReasonName(esp_reset_reason_t r, bool* abnormal) {
    *abnormal = false;
    switch (r) {
        case ESP_RST_POWERON:   return "POWER_ON";
        case ESP_RST_EXT:       return "EXTERNAL";
        case ESP_RST_SW:        return "SOFTWARE";
        case ESP_RST_PANIC:     *abnormal = true; return "PANIC";
        case ESP_RST_INT_WDT:   *abnormal = true; return "INT_WDT";
        case ESP_RST_TASK_WDT:  *abnormal = true; return "TASK_WDT";
        case ESP_RST_WDT:       *abnormal = true; return "WDT";
        case ESP_RST_DEEPSLEEP: return "DEEP_SLEEP";
        case ESP_RST_BROWNOUT:  *abnormal = true; return "BROWNOUT";
        case ESP_RST_SDIO:      return "SDIO";
        default:                return "UNKNOWN";
    }
}

void PowerManager::begin() {
    m_resetReason = resetReasonName(esp_reset_reason(), &m_abnormalReset);
    m_bootCount = Settings.incrementBootCount();
    LOG_BOOT("Reset reason: %s%s, boot #%lu", m_resetReason, m_abnormalReset ? " (abnormal)" : "",
             (unsigned long)m_bootCount);

    // The core already starts the TWDT (5 s, idle task CPU0). Re-init only
    // updates timeout/panic on IDF 4.4, then subscribe the loop task.
    esp_err_t err = esp_task_wdt_init(WDT_TIMEOUT_S, true);
    if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
        err = esp_task_wdt_add(nullptr);
        m_wdtArmed = (err == ESP_OK || err == ESP_ERR_INVALID_ARG);  // INVALID_ARG = already subscribed
    }
    LOG_BOOT("Task watchdog %s (%d s, panic reboot)", m_wdtArmed ? "armed" : "NOT armed", WDT_TIMEOUT_S);

#if ENABLE_BOOT_BUTTON_SOS
    pinMode(BOARD_BOOT_BUTTON_PIN, INPUT_PULLUP);
#endif
    m_lastActivityMs = millis();

#if DISPLAY_DIAGNOSTIC_MODE
    m_autoTimeout = false;
    LOG_POWER("Screen timeout DISABLED (DISPLAY_DIAGNOSTIC_MODE=1): display stays on; "
              "IMU, fall detection and BLE are unaffected");
#else
    m_autoTimeout = true;
    LOG_POWER("Screen timeout enabled (dim %d s before off, timeout from settings)", SCREEN_DIM_BEFORE_OFF_S);
#endif
}

void PowerManager::setPower(DisplayManager::Power want, const char* reason, uint32_t idleMs) {
    if (want == Display.power()) return;
    LOG_POWER("Screen %s -> %s (%s, idle %lu ms)", DisplayManager::powerName(Display.power()),
              DisplayManager::powerName(want), reason, (unsigned long)idleMs);
    Display.setPower(want);
}

void PowerManager::feedWatchdog() {
    if (m_wdtArmed) esp_task_wdt_reset();
}

void PowerManager::noteActivity(uint32_t nowMs) {
    // Callers pass either the loop's cached "now" or a fresh millis(); keep the
    // newest so the idle time can never go backwards.
    if ((int32_t)(nowMs - m_lastActivityMs) > 0) m_lastActivityMs = nowMs;
}

bool PowerManager::isScreenOff() const {
    return Display.power() == DisplayManager::Power::OFF;
}

bool PowerManager::wakeFromTouch() {
    const bool wasOff = isScreenOff();
    noteActivity(millis());
    setPower(DisplayManager::Power::ON, "touch", 0);
    return wasOff;  // swallow the touch that woke a dark screen
}

void PowerManager::pollBootButton(uint32_t nowMs) {
#if ENABLE_BOOT_BUTTON_SOS
    const bool down = digitalRead(BOARD_BOOT_BUTTON_PIN) == LOW;
    if (down && !m_btnDown) {
        m_btnDown = true;
        m_btnDownMs = nowMs;
        m_btnFired = false;
        noteActivity(nowMs);
        setPower(DisplayManager::Power::ON, "BOOT key", 0);
    } else if (down && m_btnDown && !m_btnFired && (nowMs - m_btnDownMs) >= BOOT_BUTTON_SOS_HOLD_MS) {
        m_btnFired = true;
        m_sosPending = true;
        LOG_POWER("BOOT key held %d ms -> SOS", BOOT_BUTTON_SOS_HOLD_MS);
    } else if (!down && m_btnDown) {
        m_btnDown = false;
    }
#else
    (void)nowMs;
#endif
}

bool PowerManager::consumeBootButtonSOS() {
    const bool p = m_sosPending;
    m_sosPending = false;
    return p;
}

void PowerManager::update(uint32_t nowMs, bool forceAwake, uint8_t timeoutS) {
    pollBootButton(nowMs);

    if (forceAwake) {
        noteActivity(nowMs);
        setPower(DisplayManager::Power::ON, "alert on screen", 0);
        return;
    }

    // Wrap-safe idle time. noteActivity() may have been given a millis() value
    // newer than this loop's cached nowMs; unsigned subtraction would then
    // underflow to ~49 days and switch the screen off immediately.
    const int32_t diff = (int32_t)(nowMs - m_lastActivityMs);
    const uint32_t idle = diff > 0 ? (uint32_t)diff : 0;

    if (!m_autoTimeout) {
        setPower(DisplayManager::Power::ON, "timeout disabled", idle);
        return;
    }

    const uint32_t offMs = (uint32_t)timeoutS * 1000u;
    const uint32_t dimMs = offMs > (SCREEN_DIM_BEFORE_OFF_S * 1000u) ? offMs - SCREEN_DIM_BEFORE_OFF_S * 1000u : offMs / 2;

    DisplayManager::Power want = DisplayManager::Power::ON;
    const char* reason = "activity";
    if (idle >= offMs) {
        want = DisplayManager::Power::OFF;
        reason = "screen timeout";
    } else if (idle >= dimMs) {
        want = DisplayManager::Power::DIM;
        reason = "dim before timeout";
    }
    setPower(want, reason, idle);
}

void PowerManager::safeReboot(const char* reason) {
    LOG_POWER("Safe reboot: %s", reason);
    Settings.flushNow();
    setPower(DisplayManager::Power::OFF, "safe reboot", 0);
    Serial.flush();
    delay(100);
    esp_restart();
}
