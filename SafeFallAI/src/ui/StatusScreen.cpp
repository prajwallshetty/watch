#include "StatusScreen.h"
#include "UITheme.h"

using namespace UITheme;

lv_obj_t* StatusScreen::row(lv_obj_t* parent, lv_coord_t y, const char* key) {
    lv_obj_t* r = container(parent, 188, 26);
    lv_obj_align(r, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_t* k = label(r, UI_FONT_BODY, text2(), key);
    lv_obj_align(k, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t* v = label(r, UI_FONT_BODY, text(), "-");
    lv_obj_align(v, LV_ALIGN_RIGHT_MID, 0, 0);
    return v;
}

void StatusScreen::create(lv_obj_t* parent) {
    lv_obj_t* title = label(parent, UI_FONT_TITLE, text(), "Status");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 26);

    m_battery = row(parent, 64, "Battery");
    m_phone = row(parent, 96, "Phone");
    m_monitor = row(parent, 128, "Monitor");
    m_last = row(parent, 160, "Last alert");

    m_footer = label(parent, UI_FONT_SMALL, text2(), "");
    lv_obj_align(m_footer, LV_ALIGN_TOP_MID, 0, 196);
}

void StatusScreen::update(const UiModel& m) {
    char buf[32];
    if (m.batteryPresent) {
        snprintf(buf, sizeof(buf), "%u%%  %u.%02uV", m.battery, m.batteryMv / 1000, (m.batteryMv % 1000) / 10);
    } else {
        snprintf(buf, sizeof(buf), "No battery");
    }
    setText(m_battery, buf);
    setTextColor(m_battery, m.batteryCritical ? red() : (m.batteryLow ? amber() : text()));

    if (!m.bleReady) {
        setText(m_phone, "BLE error");
        setTextColor(m_phone, red());
    } else {
        setText(m_phone, m.bleConnected ? "Connected" : "Not connected");
        setTextColor(m_phone, m.bleConnected ? blue() : text2());
    }

    if (!m.imuOk) {
        setText(m_monitor, "Sensor error");
        setTextColor(m_monitor, red());
    } else if (!m.monitoring) {
        setText(m_monitor, "Paused");
        setTextColor(m_monitor, amber());
    } else {
        setText(m_monitor, "Active");
        setTextColor(m_monitor, green());
    }

    setText(m_last, (m.lastEventId && m.lastEventId[0]) ? m.lastEventId : "None");

    if (m.pendingEvents > 0) {
        snprintf(buf, sizeof(buf), "%u alert(s) waiting for phone", m.pendingEvents);
        setText(m_footer, buf);
        setTextColor(m_footer, amber());
    } else {
        setText(m_footer, m.timeSynced ? m.date : "Time not set");
        setTextColor(m_footer, text2());
    }
}
