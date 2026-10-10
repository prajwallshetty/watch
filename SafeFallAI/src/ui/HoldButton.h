/**
 * SafeFall AI - HoldButton.h
 *
 * Press-and-hold control for safety-critical actions (start SOS, cancel an
 * emergency). A progress indicator (arc around a round button, or a bar
 * inside a pill) fills while the finger stays down; the action fires once
 * when the hold time is reached. Releasing early resets it.
 */
#pragma once

#include <lvgl.h>
#include "UIModel.h"

struct HoldButton {
    lv_obj_t* btn;
    lv_obj_t* indicator;   // lv_arc or lv_bar
    lv_obj_t* label;
    bool isArc;
    uint32_t holdMs;
    uint32_t pressStart;
    bool pressing;
    bool fired;
    UiAction action;
};

namespace HoldButtons {

// Round button (diameter d) with a progress arc around it.
void createRound(HoldButton& hb, lv_obj_t* parent, lv_coord_t d, lv_color_t color, const char* text,
                 const lv_font_t* font, uint32_t holdMs, UiAction action);

// Pill button with a progress bar filling its background.
void createPill(HoldButton& hb, lv_obj_t* parent, lv_coord_t w, lv_coord_t h, lv_color_t bg, lv_color_t fill,
                lv_color_t fg, const char* text, const lv_font_t* font, uint32_t holdMs, UiAction action);

void reset(HoldButton& hb);

}  // namespace HoldButtons
