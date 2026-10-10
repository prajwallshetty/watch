/**
 * SafeFall AI - SettingsScreen.h
 *
 * Settings page in the main swipe pager:
 *   Brightness   [ - ] ====== [ + ]
 *   Fall detection ON   (hold to pause, tap to resume)
 *   Sensitivity: Normal (tap to cycle)
 *   v0.1.0              (long-press: hidden diagnostics)
 */
#pragma once

#include <lvgl.h>
#include "UIModel.h"

class SettingsScreen {
public:
    void create(lv_obj_t* parent);
    void update(const UiModel& m);

private:
    static void monitorCb(lv_event_t* e);

    lv_obj_t* m_brightLabel = nullptr;
    lv_obj_t* m_brightBar = nullptr;
    lv_obj_t* m_monBtn = nullptr;
    lv_obj_t* m_monLabel = nullptr;
    lv_obj_t* m_sensLabel = nullptr;
    int m_lastBright = -1;
    int m_lastMon = -1;
    uint32_t m_hintUntil = 0;
};
