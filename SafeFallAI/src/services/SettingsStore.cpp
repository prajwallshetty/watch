#include "SettingsStore.h"
#include "../config/AppConfig.h"
#include "../config/DebugConfig.h"

#include <Preferences.h>

SettingsStore Settings;

static const char* NVS_NS = "safefall";
static Preferences s_prefs;

static uint8_t clampU8(int v, int lo, int hi) {
    if (v < lo) return (uint8_t)lo;
    if (v > hi) return (uint8_t)hi;
    return (uint8_t)v;
}

void SettingsStore::begin() {
    s_prefs.begin(NVS_NS, false);  // read-write: creates the namespace on first boot
    m_s.brightness = clampU8(s_prefs.getUChar("bright", BRIGHTNESS_DEFAULT_PERCENT), BRIGHTNESS_MIN_PERCENT, 100);
    m_s.monitoring = s_prefs.getBool("monitor", true);
    m_s.sensitivity = clampU8(s_prefs.getUChar("sens", 1), 0, 2);
    m_s.countdownS = clampU8(s_prefs.getUChar("countdn", FALL_CONFIRM_COUNTDOWN_S), FALL_COUNTDOWN_MIN_S, FALL_COUNTDOWN_MAX_S);
    m_s.screenTimeoutS = clampU8(s_prefs.getUChar("scrto", SCREEN_TIMEOUT_DEFAULT_S), SCREEN_TIMEOUT_MIN_S, SCREEN_TIMEOUT_MAX_S);
    m_s.tzOffsetS = s_prefs.getInt("tz", 0);
    if (m_s.tzOffsetS < -14 * 3600 || m_s.tzOffsetS > 14 * 3600) m_s.tzOffsetS = 0;
    s_prefs.end();
    LOG_BOOT("Settings: brightness %u%%, monitoring %s, sensitivity %u, countdown %us, timeout %us",
             m_s.brightness, m_s.monitoring ? "on" : "off", m_s.sensitivity, m_s.countdownS, m_s.screenTimeoutS);
}

void SettingsStore::markDirty() {
    m_dirty = true;
    m_dirtyMs = millis();
}

void SettingsStore::setBrightness(uint8_t pct) {
    pct = clampU8(pct, BRIGHTNESS_MIN_PERCENT, 100);
    if (pct != m_s.brightness) { m_s.brightness = pct; markDirty(); }
}
void SettingsStore::setMonitoring(bool en) {
    if (en != m_s.monitoring) { m_s.monitoring = en; markDirty(); }
}
void SettingsStore::setSensitivity(uint8_t s) {
    s = clampU8(s, 0, 2);
    if (s != m_s.sensitivity) { m_s.sensitivity = s; markDirty(); }
}
void SettingsStore::setCountdown(uint8_t s) {
    s = clampU8(s, FALL_COUNTDOWN_MIN_S, FALL_COUNTDOWN_MAX_S);
    if (s != m_s.countdownS) { m_s.countdownS = s; markDirty(); }
}
void SettingsStore::setScreenTimeout(uint8_t s) {
    s = clampU8(s, SCREEN_TIMEOUT_MIN_S, SCREEN_TIMEOUT_MAX_S);
    if (s != m_s.screenTimeoutS) { m_s.screenTimeoutS = s; markDirty(); }
}
void SettingsStore::setTzOffset(int32_t s) {
    if (s < -14 * 3600 || s > 14 * 3600) return;
    if (s != m_s.tzOffsetS) { m_s.tzOffsetS = s; markDirty(); }
}

void SettingsStore::update(uint32_t nowMs) {
    if (m_dirty && (nowMs - m_dirtyMs) >= SETTINGS_SAVE_DELAY_MS) flushNow();
}

void SettingsStore::flushNow() {
    if (!m_dirty) return;
    s_prefs.begin(NVS_NS, false);
    s_prefs.putUChar("bright", m_s.brightness);
    s_prefs.putBool("monitor", m_s.monitoring);
    s_prefs.putUChar("sens", m_s.sensitivity);
    s_prefs.putUChar("countdn", m_s.countdownS);
    s_prefs.putUChar("scrto", m_s.screenTimeoutS);
    s_prefs.putInt("tz", m_s.tzOffsetS);
    s_prefs.end();
    m_dirty = false;
}

uint32_t SettingsStore::loadEventCounter() {
    s_prefs.begin(NVS_NS, false);  // read-write: creates the namespace on first boot
    uint32_t v = s_prefs.getULong("evtctr", 0);
    s_prefs.end();
    return v;
}

void SettingsStore::saveEventCounter(uint32_t v) {
    s_prefs.begin(NVS_NS, false);
    s_prefs.putULong("evtctr", v);
    s_prefs.end();
}

uint32_t SettingsStore::incrementBootCount() {
    s_prefs.begin(NVS_NS, false);
    uint32_t v = s_prefs.getULong("boots", 0) + 1;
    s_prefs.putULong("boots", v);
    s_prefs.end();
    return v;
}
