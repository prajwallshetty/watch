#include "DiagnosticsScreen.h"
#include "UIManager.h"
#include "UITheme.h"
#include "../config/AppConfig.h"

using namespace UITheme;

static void closeCb(lv_event_t* /*e*/) { UI.closeDiagnostics(); }

static void gestureCb(lv_event_t* /*e*/) {
    if (lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_RIGHT) UI.closeDiagnostics();
}

void DiagnosticsScreen::create() {
    m_scr = UITheme::screen();
    lv_obj_add_event_cb(m_scr, gestureCb, LV_EVENT_GESTURE, nullptr);

    lv_obj_t* title = label(m_scr, UI_FONT_BODY, amber(), "DIAGNOSTICS");
    lv_obj_set_style_text_letter_space(title, 2, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 14);

    // Scrollable text column inside the round display
    lv_obj_t* box = lv_obj_create(m_scr);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, 186, 150);
    lv_obj_align(box, LV_ALIGN_TOP_MID, 0, 38);
    lv_obj_set_scroll_dir(box, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(box, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(box, LV_OBJ_FLAG_GESTURE_BUBBLE);  // horizontal swipes reach the screen handler

    m_text = lv_label_create(box);
    lv_obj_set_width(m_text, 186);
    lv_obj_set_style_text_font(m_text, UI_FONT_SMALL, 0);
    lv_obj_set_style_text_color(m_text, text(), 0);
    lv_obj_set_style_text_line_space(m_text, 2, 0);
    lv_label_set_long_mode(m_text, LV_LABEL_LONG_WRAP);
    lv_label_set_text(m_text, "");

    lv_obj_t* close = pillButton(m_scr, 110, 40, surface2(), text(), UI_FONT_BODY, "Close", nullptr);
    lv_obj_align(close, LV_ALIGN_TOP_MID, 0, 192);
    lv_obj_add_event_cb(close, closeCb, LV_EVENT_CLICKED, nullptr);
}

void DiagnosticsScreen::update(const UiModel& m, uint32_t nowMs) {
    if (nowMs - m_lastUpdateMs < 250) return;  // 4 Hz is plenty for humans
    m_lastUpdateMs = nowMs;

    char bat[24];
    if (m.batteryPresent) snprintf(bat, sizeof(bat), "%u%% (%u mV)", m.battery, m.batteryMv);
    else snprintf(bat, sizeof(bat), "not detected");

    char psram[20];
    if (m.psramFound) snprintf(psram, sizeof(psram), "OK %lu KB", (unsigned long)(m.psramSize / 1024));
    else snprintf(psram, sizeof(psram), "not enabled");

    static char buf[900];
    snprintf(buf, sizeof(buf),
             "Display: %s\n"
             "Touch: %s (0x%02X)\n"
             "IMU: %s%s\n"
             "BLE: %s (MTU %u)\n"
             "Battery: %s\n"
             "Flash: %s %lu MB\n"
             "PSRAM: %s\n"
             "Firmware: %s\n"
             "\n"
             "Accelerometer (g)\n"
             "X %+.3f\nY %+.3f\nZ %+.3f\n"
             "|a| %.3f\n"
             "Gyroscope (dps)\n"
             "X %+.1f\nY %+.1f\nZ %+.1f\n"
             "\n"
             "Detector: %s\n"
             "Last confidence: %u\n"
             "Candidates %lu / falls %lu / rejected %lu\n"
             "IMU byte order: %s, recoveries %lu\n"
             "\n"
             "State: %s\n"
             "Heap free %lu / min %lu\n"
             "Uptime %lu s\n"
             "Reset: %s",
             m.displayOk ? "OK" : "ERROR",
             m.touchOk ? "OK" : "not responding", m.touchChipId,
             m.imuOk ? "OK" : "ERROR", m.monitoring ? "" : " (paused)",
             !m.bleReady ? "ERROR" : (m.bleConnected ? "Connected" : "Disconnected"), m.bleMtu,
             bat,
             m.flashSize >= 16UL * 1024 * 1024 ? "OK" : "CHECK", (unsigned long)(m.flashSize / (1024UL * 1024UL)),
             psram,
             SAFE_FALL_VERSION,
             m.imu.ax, m.imu.ay, m.imu.az,
             m.det.accelMag,
             m.imu.gx, m.imu.gy, m.imu.gz,
             FallDetector::phaseName(m.det.phase),
             m.det.lastConfidence,
             (unsigned long)m.det.candidates, (unsigned long)m.det.confirmed, (unsigned long)m.det.rejected,
             m.imuBigEndian ? "BE" : "LE", (unsigned long)m.imuRecoveries,
             FallStateMachine::stateName(m.state),
             (unsigned long)m.freeHeap, (unsigned long)m.minFreeHeap,
             (unsigned long)m.uptimeS,
             m.resetReason ? m.resetReason : "?");
    setText(m_text, buf);
}
