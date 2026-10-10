/**
 * SafeFall AI - DiagnosticsScreen.h
 *
 * Hidden developer screen (Settings > long-press the version label).
 * Shows subsystem health, firmware version, live accelerometer / gyroscope
 * axes and detector statistics. Swipe right or tap Close to leave.
 * Development use only.
 */
#pragma once

#include <lvgl.h>
#include "UIModel.h"

class DiagnosticsScreen {
public:
    void create();
    void update(const UiModel& m, uint32_t nowMs);
    lv_obj_t* screen() const { return m_scr; }

private:
    lv_obj_t* m_scr = nullptr;
    lv_obj_t* m_text = nullptr;
    uint32_t m_lastUpdateMs = 0;
};
