#include "HoldButton.h"
#include "UIManager.h"
#include "UITheme.h"

namespace HoldButtons {

static void setProgress(HoldButton& hb, int32_t pct) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    if (hb.isArc) lv_arc_set_value(hb.indicator, (int16_t)pct);
    else lv_bar_set_value(hb.indicator, pct, LV_ANIM_OFF);
}

void reset(HoldButton& hb) {
    hb.pressing = false;
    hb.fired = false;
    setProgress(hb, 0);
}

static void eventCb(lv_event_t* e) {
    HoldButton* hb = static_cast<HoldButton*>(lv_event_get_user_data(e));
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        hb->pressing = true;
        hb->fired = false;
        hb->pressStart = lv_tick_get();
        setProgress(*hb, 0);
    } else if (code == LV_EVENT_PRESSING) {
        if (!hb->pressing || hb->fired) return;
        const uint32_t held = lv_tick_elaps(hb->pressStart);
        setProgress(*hb, (int32_t)(held * 100u / hb->holdMs));
        if (held >= hb->holdMs) {
            hb->fired = true;
            UI.post(hb->action);
        }
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        hb->pressing = false;
        if (!hb->fired) setProgress(*hb, 0);
    }
}

static void attach(HoldButton& hb) {
    lv_obj_add_event_cb(hb.btn, eventCb, LV_EVENT_PRESSED, &hb);
    lv_obj_add_event_cb(hb.btn, eventCb, LV_EVENT_PRESSING, &hb);
    lv_obj_add_event_cb(hb.btn, eventCb, LV_EVENT_RELEASED, &hb);
    lv_obj_add_event_cb(hb.btn, eventCb, LV_EVENT_PRESS_LOST, &hb);
}

void createRound(HoldButton& hb, lv_obj_t* parent, lv_coord_t d, lv_color_t color, const char* text,
                 const lv_font_t* font, uint32_t holdMs, UiAction action) {
    hb.isArc = true;
    hb.holdMs = holdMs;
    hb.action = action;
    hb.pressing = false;
    hb.fired = false;

    const lv_coord_t ring = 12;
    hb.indicator = lv_arc_create(parent);
    lv_obj_set_size(hb.indicator, d + 2 * ring + 8, d + 2 * ring + 8);
    lv_arc_set_rotation(hb.indicator, 270);
    lv_arc_set_bg_angles(hb.indicator, 0, 360);
    lv_arc_set_range(hb.indicator, 0, 100);
    lv_arc_set_value(hb.indicator, 0);
    lv_obj_remove_style(hb.indicator, nullptr, LV_PART_KNOB);
    lv_obj_clear_flag(hb.indicator, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(hb.indicator, ring, LV_PART_MAIN);
    lv_obj_set_style_arc_width(hb.indicator, ring, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(hb.indicator, UITheme::surface(), LV_PART_MAIN);
    lv_obj_set_style_arc_color(hb.indicator, UITheme::text(), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(hb.indicator, true, LV_PART_INDICATOR);

    hb.btn = lv_btn_create(parent);
    lv_obj_set_size(hb.btn, d, d);
    lv_obj_set_style_radius(hb.btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(hb.btn, color, 0);
    lv_obj_set_style_bg_color(hb.btn, lv_color_darken(color, LV_OPA_30), LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(hb.btn, 0, 0);
    lv_obj_set_style_border_width(hb.btn, 0, 0);
    hb.label = UITheme::label(hb.btn, font, UITheme::text(), text);
    lv_obj_center(hb.label);
    attach(hb);
}

void createPill(HoldButton& hb, lv_obj_t* parent, lv_coord_t w, lv_coord_t h, lv_color_t bg, lv_color_t fill,
                lv_color_t fg, const char* text, const lv_font_t* font, uint32_t holdMs, UiAction action) {
    hb.isArc = false;
    hb.holdMs = holdMs;
    hb.action = action;
    hb.pressing = false;
    hb.fired = false;

    hb.btn = lv_btn_create(parent);
    lv_obj_set_size(hb.btn, w, h);
    lv_obj_set_style_radius(hb.btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(hb.btn, bg, 0);
    lv_obj_set_style_shadow_width(hb.btn, 0, 0);
    lv_obj_set_style_border_width(hb.btn, 0, 0);
    lv_obj_set_style_pad_all(hb.btn, 0, 0);
    lv_obj_set_style_clip_corner(hb.btn, true, 0);

    hb.indicator = lv_bar_create(hb.btn);
    lv_obj_set_size(hb.indicator, w, h);
    lv_obj_center(hb.indicator);
    lv_bar_set_range(hb.indicator, 0, 100);
    lv_bar_set_value(hb.indicator, 0, LV_ANIM_OFF);
    lv_obj_set_style_radius(hb.indicator, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(hb.indicator, 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(hb.indicator, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_bg_color(hb.indicator, fill, LV_PART_INDICATOR);
    lv_obj_clear_flag(hb.indicator, LV_OBJ_FLAG_CLICKABLE);

    hb.label = UITheme::label(hb.btn, font, fg, text);
    lv_obj_center(hb.label);
    attach(hb);
}

}  // namespace HoldButtons
