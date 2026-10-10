#include "HomeScreen.h"
#include "UITheme.h"

using namespace UITheme;

static const char* batterySymbol(uint8_t pct) {
    if (pct >= 90) return LV_SYMBOL_BATTERY_FULL;
    if (pct >= 65) return LV_SYMBOL_BATTERY_3;
    if (pct >= 40) return LV_SYMBOL_BATTERY_2;
    if (pct >= 15) return LV_SYMBOL_BATTERY_1;
    return LV_SYMBOL_BATTERY_EMPTY;
}

void HomeScreen::create(lv_obj_t* parent) {
    lv_obj_t* brand = label(parent, UI_FONT_SMALL, text2(), "SAFEFALL AI");
    lv_obj_set_style_text_letter_space(brand, 3, 0);
    lv_obj_align(brand, LV_ALIGN_TOP_MID, 0, 24);

    m_time = label(parent, UI_FONT_HUGE, text(), "--:--");
    lv_obj_align(m_time, LV_ALIGN_TOP_MID, 0, 44);

    m_date = label(parent, UI_FONT_SMALL, text2(), "");
    lv_obj_align(m_date, LV_ALIGN_TOP_MID, 0, 96);

    // Battery + Bluetooth row
    lv_obj_t* row = container(parent, 170, 24);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 116);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 8, 0);
    m_battIcon = label(row, UI_FONT_BODY, text(), LV_SYMBOL_BATTERY_FULL);
    m_battText = label(row, UI_FONT_BODY, text(), "--%");
    lv_obj_t* spacer = container(row, 10, 1);
    (void)spacer;
    m_bleIcon = label(row, UI_FONT_BODY, text2(), LV_SYMBOL_BLUETOOTH);

    // Status pill
    m_pill = lv_obj_create(parent);
    lv_obj_remove_style_all(m_pill);
    lv_obj_set_size(m_pill, 172, 44);
    lv_obj_set_style_radius(m_pill, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(m_pill, LV_OPA_20, 0);
    lv_obj_set_style_bg_color(m_pill, green(), 0);
    lv_obj_clear_flag(m_pill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(m_pill, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(m_pill, LV_ALIGN_TOP_MID, 0, 146);
    lv_obj_set_flex_flow(m_pill, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(m_pill, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(m_pill, 8, 0);

    m_dot = lv_obj_create(m_pill);
    lv_obj_remove_style_all(m_dot);
    lv_obj_set_size(m_dot, 12, 12);
    lv_obj_set_style_radius(m_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(m_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(m_dot, green(), 0);
    m_status = label(m_pill, UI_FONT_TITLE, green(), "SAFE");

    m_motion = label(parent, UI_FONT_BODY, text2(), "Motion Normal");
    lv_obj_align(m_motion, LV_ALIGN_TOP_MID, 0, 198);
}

void HomeScreen::update(const UiModel& m) {
    setText(m_time, m.clock);
    setText(m_date, m.timeSynced ? m.date : "Time not set");

    char buf[24];
    if (m.batteryPresent) {
        snprintf(buf, sizeof(buf), "%u%%", m.battery);
        setText(m_battIcon, batterySymbol(m.battery));
    } else {
        snprintf(buf, sizeof(buf), "--%%");
        setText(m_battIcon, LV_SYMBOL_BATTERY_EMPTY);
    }
    setText(m_battText, buf);
    const lv_color_t bc = m.batteryCritical ? red() : (m.batteryLow ? amber() : text());
    setTextColor(m_battIcon, bc);
    setTextColor(m_battText, bc);
    setTextColor(m_bleIcon, m.bleConnected ? blue() : text2());

    // Status pill + motion line
    const char* status = "SAFE";
    lv_color_t c = green();
    const char* motionTxt = "Motion Normal";

    if (!m.imuOk || m.state == DeviceState::DEVICE_ERROR) {
        status = "SENSOR ERROR";
        c = red();
        motionTxt = "Fall check unavailable";
    } else if (!m.monitoring) {
        status = "PAUSED";
        c = amber();
        motionTxt = "Fall check paused";
    } else if (m.state == DeviceState::POSSIBLE_FALL) {
        status = "CHECKING";
        c = amber();
        motionTxt = "Analysing movement...";
    } else {
        if (m.state == DeviceState::LOW_BATTERY || m.batteryCritical) {
            status = "LOW BATTERY";
            c = amber();
        }
        switch (m.motion) {
            case MotionLevel::STILL:  motionTxt = "Motion Still"; break;
            case MotionLevel::ACTIVE: motionTxt = "Motion Active"; break;
            default:                  motionTxt = "Motion Normal"; break;
        }
    }
    setText(m_status, status);
    setTextColor(m_status, c);
    const uint32_t c32 = lv_color_to32(c);
    if (c32 != m_pillColor) {  // restyle only on change to avoid redrawing every frame
        m_pillColor = c32;
        lv_obj_set_style_bg_color(m_dot, c, 0);
        lv_obj_set_style_bg_color(m_pill, c, 0);
    }
    setText(m_motion, motionTxt);
}
