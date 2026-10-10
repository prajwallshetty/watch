#include "SOSScreen.h"
#include "UIManager.h"
#include "UITheme.h"
#include "../config/AppConfig.h"

using namespace UITheme;

static void pulseOpa(void* obj, int32_t v) {
    lv_obj_set_style_border_opa(static_cast<lv_obj_t*>(obj), (lv_opa_t)v, 0);
}

void SOSScreen::create() {
    m_scr = UITheme::screen();
    lv_obj_set_style_bg_color(m_scr, redDeep(), 0);

    // Pulsing ring around the edge of the round display
    m_ring = lv_obj_create(m_scr);
    lv_obj_remove_style_all(m_ring);
    lv_obj_set_size(m_ring, 232, 232);
    lv_obj_center(m_ring);
    lv_obj_set_style_radius(m_ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(m_ring, 6, 0);
    lv_obj_set_style_border_color(m_ring, text(), 0);
    lv_obj_set_style_border_opa(m_ring, LV_OPA_50, 0);
    lv_obj_clear_flag(m_ring, LV_OBJ_FLAG_CLICKABLE);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, m_ring);
    lv_anim_set_exec_cb(&a, pulseOpa);
    lv_anim_set_values(&a, LV_OPA_20, LV_OPA_COVER);
    lv_anim_set_time(&a, 700);
    lv_anim_set_playback_time(&a, 700);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);

    m_title = label(m_scr, UI_FONT_HUGE, text(), "SOS");
    lv_obj_align(m_title, LV_ALIGN_TOP_MID, 0, 26);

    m_subtitle = label(m_scr, UI_FONT_TITLE, text(), "Emergency Alert");
    lv_obj_align(m_subtitle, LV_ALIGN_TOP_MID, 0, 82);

    m_status = label(m_scr, UI_FONT_BODY, text(), "Sending alert...");
    lv_obj_align(m_status, LV_ALIGN_TOP_MID, 0, 112);

    m_eventId = label(m_scr, UI_FONT_SMALL, lv_color_hex(0xFFD0D0), "");
    lv_obj_align(m_eventId, LV_ALIGN_TOP_MID, 0, 136);

    HoldButtons::createPill(m_cancel, m_scr, 168, 50, text(), lv_color_hex(0xD8D8D8), redDeep(), "HOLD TO CANCEL",
                            UI_FONT_BODY, CANCEL_HOLD_MS, UiAction::CANCEL_EMERGENCY);
    lv_obj_align(m_cancel.btn, LV_ALIGN_TOP_MID, 0, 164);
}

void SOSScreen::createTriggerPage(lv_obj_t* parent) {
    lv_obj_t* title = label(parent, UI_FONT_TITLE, text(), "Need help?");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 22);

    HoldButtons::createRound(m_trigger, parent, 120, red(), "SOS", UI_FONT_LARGE, SOS_HOLD_MS, UiAction::MANUAL_SOS);
    lv_obj_align(m_trigger.indicator, LV_ALIGN_CENTER, 0, 4);
    lv_obj_align(m_trigger.btn, LV_ALIGN_CENTER, 0, 4);

    lv_obj_t* hint = label(parent, UI_FONT_BODY, text2(), "Press and hold");
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 206);
}

void SOSScreen::onShow() {
    HoldButtons::reset(m_cancel);
}

void SOSScreen::resetTrigger() {
    HoldButtons::reset(m_trigger);
}

void SOSScreen::update(const UiModel& m) {
    const bool fall = (m.state == DeviceState::EMERGENCY);
    setText(m_subtitle, fall ? "Fall Emergency" : "Emergency Alert");

    if (!m.bleReady) {
        setText(m_status, "Bluetooth unavailable");
    } else if (!m.bleConnected) {
        setText(m_status, "Waiting for phone...");
    } else if (m.eventDelivered) {
        setText(m_status, LV_SYMBOL_OK " Alert delivered");
    } else {
        setText(m_status, "Sending alert...");
    }
    setText(m_eventId, m.activeEventId ? m.activeEventId : "");
}
