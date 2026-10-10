#include "FallAlertScreen.h"
#include "UIManager.h"
#include "UITheme.h"

using namespace UITheme;

static void okCb(lv_event_t* /*e*/) { UI.post(UiAction::IM_OK); }
static void sosCb(lv_event_t* /*e*/) { UI.post(UiAction::FALL_SOS); }

void FallAlertScreen::create() {
    m_scr = UITheme::screen();

    m_ring = lv_arc_create(m_scr);
    lv_obj_set_size(m_ring, 236, 236);
    lv_obj_center(m_ring);
    lv_arc_set_rotation(m_ring, 270);
    lv_arc_set_bg_angles(m_ring, 0, 360);
    lv_arc_set_range(m_ring, 0, 1000);
    lv_arc_set_value(m_ring, 1000);
    lv_obj_remove_style(m_ring, nullptr, LV_PART_KNOB);
    lv_obj_clear_flag(m_ring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(m_ring, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_width(m_ring, 8, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(m_ring, surface(), LV_PART_MAIN);
    lv_obj_set_style_arc_color(m_ring, amber(), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(m_ring, true, LV_PART_INDICATOR);

    m_title = label(m_scr, UI_FONT_TITLE, amber(), "FALL DETECTED");
    lv_obj_align(m_title, LV_ALIGN_TOP_MID, 0, 34);

    m_subtitle = label(m_scr, UI_FONT_TITLE, text(), "Are you OK?");
    lv_obj_align(m_subtitle, LV_ALIGN_TOP_MID, 0, 60);

    m_count = label(m_scr, UI_FONT_HUGE, text(), "10");
    lv_obj_align(m_count, LV_ALIGN_TOP_MID, 0, 86);

    // Two large side-by-side targets (92 x 58 px each)
    m_btnOk = pillButton(m_scr, 92, 58, green(), bg(), UI_FONT_TITLE, "I'M OK", nullptr);
    lv_obj_align(m_btnOk, LV_ALIGN_TOP_MID, -50, 146);
    lv_obj_add_event_cb(m_btnOk, okCb, LV_EVENT_CLICKED, nullptr);

    m_btnSos = pillButton(m_scr, 92, 58, red(), text(), UI_FONT_TITLE, "SOS", nullptr);
    lv_obj_align(m_btnSos, LV_ALIGN_TOP_MID, 50, 146);
    lv_obj_add_event_cb(m_btnSos, sosCb, LV_EVENT_CLICKED, nullptr);
}

void FallAlertScreen::setCancelledMode(bool cancelled) {
    if (cancelled == m_cancelledMode) return;
    m_cancelledMode = cancelled;
    setHidden(m_btnOk, cancelled);
    setHidden(m_btnSos, cancelled);
    lv_obj_set_style_arc_color(m_ring, cancelled ? green() : amber(), LV_PART_INDICATOR);
    setTextColor(m_title, cancelled ? green() : amber());
    m_lastRing = -1;
}

void FallAlertScreen::update(const UiModel& m) {
    const bool cancelled = (m.state == DeviceState::USER_CANCELLED);
    setCancelledMode(cancelled);

    if (cancelled) {
        setText(m_title, "ALERT CANCELLED");
        setText(m_count, LV_SYMBOL_OK);
        setTextColor(m_count, green());
        if (m.lastTrigger == Trigger::REMOTE_CANCEL) setText(m_subtitle, "Cancelled by phone");
        else if (m.cancelledFrom == DeviceState::AWAITING_CONFIRMATION) setText(m_subtitle, "Glad you're OK");
        else setText(m_subtitle, "Emergency ended");
        if (m_lastRing != 1000) {
            m_lastRing = 1000;
            lv_arc_set_value(m_ring, 1000);
        }
        return;
    }

    setText(m_title, "FALL DETECTED");
    setText(m_subtitle, "Are you OK?");
    setTextColor(m_count, text());

    char buf[8];
    const uint32_t secs = (m.countdownMsLeft + 999u) / 1000u;
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)secs);
    setText(m_count, buf);

    const uint32_t total = (uint32_t)m.countdownTotalS * 1000u;
    int16_t v = total ? (int16_t)((m.countdownMsLeft * 1000u) / total) : 0;
    if (v != m_lastRing) {
        m_lastRing = v;
        lv_arc_set_value(m_ring, v);
    }
}
