#include "SettingsScreen.h"
#include "UIManager.h"
#include "UITheme.h"
#include "../config/AppConfig.h"

using namespace UITheme;

static void brightDownCb(lv_event_t* /*e*/) { UI.post(UiAction::BRIGHTNESS_DOWN); }
static void brightUpCb(lv_event_t* /*e*/) { UI.post(UiAction::BRIGHTNESS_UP); }
static void sensCb(lv_event_t* /*e*/) { UI.post(UiAction::CYCLE_SENSITIVITY); }

static const uint32_t HINT_MS = 2000;

// Turning fall detection OFF needs a long press; turning it back ON is a tap.
// A tap while ON shows a hint instead of silently doing nothing.
void SettingsScreen::monitorCb(lv_event_t* e) {
    SettingsScreen* self = static_cast<SettingsScreen*>(lv_event_get_user_data(e));
    const bool on = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    const lv_event_code_t code = lv_event_get_code(e);
    if (on && code == LV_EVENT_LONG_PRESSED) {
        UI.post(UiAction::MONITORING_OFF);
    } else if (on && code == LV_EVENT_SHORT_CLICKED) {
        self->m_hintUntil = lv_tick_get() + HINT_MS;
        setText(self->m_monLabel, "Hold to pause");
    } else if (!on && code == LV_EVENT_SHORT_CLICKED) {
        UI.post(UiAction::MONITORING_ON);
    }
}

static void diagCb(lv_event_t* /*e*/) { UI.openDiagnostics(); }

void SettingsScreen::create(lv_obj_t* parent) {
    lv_obj_t* title = label(parent, UI_FONT_TITLE, text(), "Settings");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 22);

    // Brightness
    m_brightLabel = label(parent, UI_FONT_BODY, text2(), "Brightness");
    lv_obj_align(m_brightLabel, LV_ALIGN_TOP_MID, 0, 50);

    lv_obj_t* minus = pillButton(parent, 54, 44, surface2(), text(), UI_FONT_TITLE, LV_SYMBOL_MINUS, nullptr);
    lv_obj_align(minus, LV_ALIGN_TOP_MID, -70, 72);
    lv_obj_add_event_cb(minus, brightDownCb, LV_EVENT_CLICKED, nullptr);

    m_brightBar = lv_bar_create(parent);
    lv_obj_set_size(m_brightBar, 70, 10);
    lv_obj_align(m_brightBar, LV_ALIGN_TOP_MID, 0, 89);
    lv_bar_set_range(m_brightBar, 0, 100);
    lv_obj_set_style_bg_color(m_brightBar, surface2(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(m_brightBar, text(), LV_PART_INDICATOR);
    lv_obj_clear_flag(m_brightBar, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* plus = pillButton(parent, 54, 44, surface2(), text(), UI_FONT_TITLE, LV_SYMBOL_PLUS, nullptr);
    lv_obj_align(plus, LV_ALIGN_TOP_MID, 70, 72);
    lv_obj_add_event_cb(plus, brightUpCb, LV_EVENT_CLICKED, nullptr);

    // Fall detection toggle (checkable pill)
    m_monBtn = pillButton(parent, 184, 44, surface2(), text(), UI_FONT_BODY, "Fall detection ON", &m_monLabel);
    // Not LV_OBJ_FLAG_CHECKABLE: the CHECKED state mirrors the model, taps only post actions.
    lv_obj_align(m_monBtn, LV_ALIGN_TOP_MID, 0, 126);
    lv_obj_add_event_cb(m_monBtn, monitorCb, LV_EVENT_LONG_PRESSED, this);
    lv_obj_add_event_cb(m_monBtn, monitorCb, LV_EVENT_SHORT_CLICKED, this);

    // Sensitivity
    lv_obj_t* sens = pillButton(parent, 184, 40, surface2(), text(), UI_FONT_BODY, "Sensitivity: Normal", &m_sensLabel);
    lv_obj_align(sens, LV_ALIGN_TOP_MID, 0, 176);
    lv_obj_add_event_cb(sens, sensCb, LV_EVENT_CLICKED, nullptr);

    // Version: long-press opens the hidden diagnostics screen
    lv_obj_t* ver = label(parent, UI_FONT_SMALL, text2(), "v" SAFE_FALL_VERSION);
    lv_obj_add_flag(ver, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(ver, 16);
    lv_obj_align(ver, LV_ALIGN_TOP_MID, 0, 220);
    lv_obj_add_event_cb(ver, diagCb, LV_EVENT_LONG_PRESSED, nullptr);
}

void SettingsScreen::update(const UiModel& m) {
    if (m.brightness != m_lastBright) {
        m_lastBright = m.brightness;
        char buf[24];
        snprintf(buf, sizeof(buf), "Brightness %u%%", m.brightness);
        setText(m_brightLabel, buf);
        lv_bar_set_value(m_brightBar, m.brightness, LV_ANIM_OFF);
    }

    const int mon = m.monitoring ? 1 : 0;
    if (mon != m_lastMon) {
        m_lastMon = mon;
        m_hintUntil = 0;
        if (m.monitoring) {
            lv_obj_add_state(m_monBtn, LV_STATE_CHECKED);
            lv_obj_set_style_bg_color(m_monBtn, lv_color_darken(green(), LV_OPA_40), 0);
        } else {
            lv_obj_clear_state(m_monBtn, LV_STATE_CHECKED);
            lv_obj_set_style_bg_color(m_monBtn, lv_color_darken(amber(), LV_OPA_40), 0);
        }
    }
    const bool hint = m_hintUntil != 0 && (int32_t)(m_hintUntil - lv_tick_get()) > 0;
    if (!hint) {
        m_hintUntil = 0;
        setText(m_monLabel, m.monitoring ? "Fall detection ON" : "Paused - tap to resume");
    }

    static const char* kSens[] = {"Sensitivity: Low", "Sensitivity: Normal", "Sensitivity: High"};
    setText(m_sensLabel, kSens[m.sensitivity > 2 ? 1 : m.sensitivity]);
}
