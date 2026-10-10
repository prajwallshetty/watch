#include "UITheme.h"

namespace UITheme {

void init(lv_disp_t* disp) {
    lv_theme_t* th = lv_theme_default_init(disp, blue(), red(), true, UI_FONT_BODY);
    lv_disp_set_theme(disp, th);
    lv_obj_set_style_bg_color(lv_scr_act(), bg(), 0);
}

lv_obj_t* container(lv_obj_t* parent, lv_coord_t w, lv_coord_t h) {
    lv_obj_t* c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, w, h);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_CLICKABLE);
    return c;
}

lv_obj_t* screen() {
    lv_obj_t* s = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(s, bg(), 0);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s, 0, 0);
    lv_obj_set_style_pad_all(s, 0, 0);
    lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);
    return s;
}

lv_obj_t* label(lv_obj_t* parent, const lv_font_t* font, lv_color_t color, const char* txt) {
    lv_obj_t* l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(l, txt);
    return l;
}

lv_obj_t* pillButton(lv_obj_t* parent, lv_coord_t w, lv_coord_t h, lv_color_t bgc, lv_color_t fg,
                     const lv_font_t* font, const char* txt, lv_obj_t** labelOut) {
    lv_obj_t* b = lv_btn_create(parent);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(b, bgc, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_border_width(b, 0, 0);
    // Pressed feedback: slightly darker
    lv_obj_set_style_bg_color(b, lv_color_darken(bgc, LV_OPA_30), LV_STATE_PRESSED);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_t* l = label(b, font, fg, txt);
    lv_obj_center(l);
    if (labelOut) *labelOut = l;
    return b;
}

void setText(lv_obj_t* l, const char* txt) {
    if (l == nullptr || txt == nullptr) return;
    const char* cur = lv_label_get_text(l);
    if (cur != nullptr && strcmp(cur, txt) == 0) return;
    lv_label_set_text(l, txt);
}

void setTextColor(lv_obj_t* l, lv_color_t c) {
    if (l == nullptr) return;
    if (lv_color_to32(lv_obj_get_style_text_color(l, 0)) == lv_color_to32(c)) return;
    lv_obj_set_style_text_color(l, c, 0);
}

void setHidden(lv_obj_t* obj, bool hidden) {
    if (obj == nullptr) return;
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN) == hidden) return;
    if (hidden) lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

}  // namespace UITheme
