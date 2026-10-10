/**
 * SafeFall AI - HomeScreen.h
 *
 *        SAFEFALL AI
 *           10:42
 *       [bat] 82%   [bt]
 *        ( ● SAFE )
 *       Motion Normal
 */
#pragma once

#include <lvgl.h>
#include "UIModel.h"

class HomeScreen {
public:
    void create(lv_obj_t* parent);
    void update(const UiModel& m);

private:
    lv_obj_t* m_time = nullptr;
    lv_obj_t* m_date = nullptr;
    lv_obj_t* m_battIcon = nullptr;
    lv_obj_t* m_battText = nullptr;
    lv_obj_t* m_bleIcon = nullptr;
    lv_obj_t* m_pill = nullptr;
    lv_obj_t* m_dot = nullptr;
    lv_obj_t* m_status = nullptr;
    lv_obj_t* m_motion = nullptr;
    uint32_t m_pillColor = 0;
};
