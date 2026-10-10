/**
 * SafeFall AI - UITheme.h
 *
 * Visual language: true-black background (OLED-style contrast on the IPS
 * panel), white primary text, Apple-like system accent colours, large
 * Montserrat type, pill-shaped touch targets of 44 px or more.
 */
#pragma once

#include <Arduino.h>
#include <lvgl.h>

#if !LV_FONT_MONTSERRAT_14 || !LV_FONT_MONTSERRAT_16 || !LV_FONT_MONTSERRAT_20 || !LV_FONT_MONTSERRAT_28 || \
    !LV_FONT_MONTSERRAT_48
#error "lv_conf.h must enable LV_FONT_MONTSERRAT_14, _16, _20, _28 and _48 (use extras/lv_conf.h from this project)."
#endif

namespace UITheme {

// Colours
inline lv_color_t bg()        { return lv_color_hex(0x000000); }
inline lv_color_t surface()   { return lv_color_hex(0x1C1C1E); }
inline lv_color_t surface2()  { return lv_color_hex(0x2C2C2E); }
inline lv_color_t text()      { return lv_color_hex(0xFFFFFF); }
inline lv_color_t text2()     { return lv_color_hex(0x9A9AA0); }
inline lv_color_t green()     { return lv_color_hex(0x30D158); }
inline lv_color_t red()       { return lv_color_hex(0xFF453A); }
inline lv_color_t redDeep()   { return lv_color_hex(0xC1121F); }
inline lv_color_t amber()     { return lv_color_hex(0xFF9F0A); }
inline lv_color_t blue()      { return lv_color_hex(0x0A84FF); }

// Fonts
#define UI_FONT_SMALL  (&lv_font_montserrat_14)
#define UI_FONT_BODY   (&lv_font_montserrat_16)
#define UI_FONT_TITLE  (&lv_font_montserrat_20)
#define UI_FONT_LARGE  (&lv_font_montserrat_28)
#define UI_FONT_HUGE   (&lv_font_montserrat_48)

void init(lv_disp_t* disp);

// Plain, non-scrollable, transparent container
lv_obj_t* container(lv_obj_t* parent, lv_coord_t w, lv_coord_t h);
// Black full-screen object for lv_scr_load
lv_obj_t* screen();
lv_obj_t* label(lv_obj_t* parent, const lv_font_t* font, lv_color_t color, const char* txt);
// Rounded pill button with a centred label; returns the button, label via out param
lv_obj_t* pillButton(lv_obj_t* parent, lv_coord_t w, lv_coord_t h, lv_color_t bg, lv_color_t fg,
                     const lv_font_t* font, const char* txt, lv_obj_t** labelOut);

// Only touches LVGL (and invalidates) when the text really changed
void setText(lv_obj_t* label, const char* txt);
void setTextColor(lv_obj_t* label, lv_color_t c);
void setHidden(lv_obj_t* obj, bool hidden);

}  // namespace UITheme
