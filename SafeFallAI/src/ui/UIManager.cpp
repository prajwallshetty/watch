#include "UIManager.h"
#include "UITheme.h"
#include "../config/DebugConfig.h"

using namespace UITheme;

UIManager UI;

bool UIManager::begin(lv_disp_t* disp) {
    UITheme::init(disp);

    // --- Main pager: Home | SOS | Status | Settings ---
    m_main = UITheme::screen();
    m_tiles = lv_tileview_create(m_main);
    lv_obj_set_size(m_tiles, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(m_tiles, bg(), 0);
    lv_obj_set_style_bg_opa(m_tiles, LV_OPA_COVER, 0);
    lv_obj_set_scrollbar_mode(m_tiles, LV_SCROLLBAR_MODE_OFF);

    m_tile[0] = lv_tileview_add_tile(m_tiles, 0, 0, LV_DIR_RIGHT);
    m_tile[1] = lv_tileview_add_tile(m_tiles, 1, 0, LV_DIR_HOR);
    m_tile[2] = lv_tileview_add_tile(m_tiles, 2, 0, LV_DIR_HOR);
    m_tile[3] = lv_tileview_add_tile(m_tiles, 3, 0, LV_DIR_LEFT);
    for (auto* t : m_tile) {
        lv_obj_set_scrollbar_mode(t, LV_SCROLLBAR_MODE_OFF);
        lv_obj_clear_flag(t, LV_OBJ_FLAG_SCROLLABLE);
    }

    m_home.create(m_tile[0]);
    m_sos.createTriggerPage(m_tile[1]);
    m_status.create(m_tile[2]);
    m_settings.create(m_tile[3]);

    // Page dots (top edge, where the round display is still wide enough)
    lv_obj_t* dots = container(m_main, 64, 10);
    lv_obj_align(dots, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_set_flex_flow(dots, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dots, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(dots, 7, 0);
    for (auto& d : m_dots) {
        d = lv_obj_create(dots);
        lv_obj_remove_style_all(d);
        lv_obj_set_size(d, 6, 6);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(d, surface2(), 0);
    }

    m_alert.create();
    m_sos.create();
    m_diag.create();

    lv_scr_load(m_main);
    m_view = View::MAIN;
    updateDots();
    LOG_UI("UI ready");
    return true;
}

void UIManager::post(UiAction a) {
    const uint8_t next = (uint8_t)((m_head + 1) % QUEUE_LEN);
    if (next == m_tail) return;  // full: drop (user is tapping faster than we run)
    m_queue[m_head] = a;
    m_head = next;
}

bool UIManager::poll(UiAction& out) {
    if (m_tail == m_head) return false;
    out = m_queue[m_tail];
    m_tail = (uint8_t)((m_tail + 1) % QUEUE_LEN);
    return true;
}

void UIManager::openDiagnostics() {
    m_diagOpen = true;
    LOG_UI("Diagnostics opened");
}

void UIManager::closeDiagnostics() {
    m_diagOpen = false;
}

void UIManager::goHome() {
    m_diagOpen = false;
    if (m_tiles) lv_obj_set_tile_id(m_tiles, 0, 0, LV_ANIM_OFF);
    m_sos.resetTrigger();
}

UIManager::View UIManager::viewFor(DeviceState s) const {
    switch (s) {
        case DeviceState::FALL_DETECTED:
        case DeviceState::AWAITING_CONFIRMATION:
        case DeviceState::USER_CANCELLED:
            return View::ALERT;
        case DeviceState::EMERGENCY:
        case DeviceState::SOS_ACTIVE:
            return View::SOS;
        default:
            return m_diagOpen ? View::DIAG : View::MAIN;
    }
}

void UIManager::show(View v, bool animate) {
    if (v == m_view) return;
    lv_obj_t* target = m_main;
    lv_scr_load_anim_t anim = LV_SCR_LOAD_ANIM_FADE_ON;
    switch (v) {
        case View::ALERT: target = m_alert.screen(); break;
        case View::SOS:
            target = m_sos.screen();
            m_sos.onShow();
            break;
        case View::DIAG:
            target = m_diag.screen();
            anim = LV_SCR_LOAD_ANIM_MOVE_LEFT;
            break;
        case View::MAIN:
            if (m_view == View::DIAG) anim = LV_SCR_LOAD_ANIM_MOVE_RIGHT;
            m_sos.resetTrigger();
            break;
    }
    if (animate) lv_scr_load_anim(target, anim, 220, 0, false);
    else lv_scr_load(target);
    m_view = v;
}

void UIManager::updateDots() {
    lv_obj_t* act = lv_tileview_get_tile_act(m_tiles);
    int8_t idx = 0;
    for (int8_t i = 0; i < 4; i++) {
        if (m_tile[i] == act) idx = i;
    }
    if (idx == m_activeTile) return;
    m_activeTile = idx;
    for (int8_t i = 0; i < 4; i++) {
        lv_obj_set_style_bg_color(m_dots[i], i == idx ? text() : surface2(), 0);
    }
}

void UIManager::update(const UiModel& m, uint32_t nowMs, bool screenOff) {
    show(viewFor(m.state), !screenOff);

    switch (m_view) {
        case View::ALERT:
            m_alert.update(m);
            break;
        case View::SOS:
            m_sos.update(m);
            break;
        case View::DIAG:
            m_diag.update(m, nowMs);
            break;
        case View::MAIN:
            updateDots();
            if (screenOff) break;  // nothing visible to refresh
            // All pages stay current so a swipe never reveals stale values;
            // setText() makes unchanged values free.
            m_home.update(m);
            m_status.update(m);
            m_settings.update(m);
            break;
    }
}
