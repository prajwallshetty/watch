/**
 * SafeFall AI - StatusScreen.h
 *
 * Health / device status page in the main swipe pager:
 *   Battery     82%
 *   Phone       Connected
 *   Monitor     Active
 *   Last alert  SF-000003
 */
#pragma once

#include <lvgl.h>
#include "UIModel.h"

class StatusScreen {
public:
    void create(lv_obj_t* parent);
    void update(const UiModel& m);

private:
    lv_obj_t* row(lv_obj_t* parent, lv_coord_t y, const char* key);

    lv_obj_t* m_battery = nullptr;
    lv_obj_t* m_phone = nullptr;
    lv_obj_t* m_monitor = nullptr;
    lv_obj_t* m_last = nullptr;
    lv_obj_t* m_footer = nullptr;
};
