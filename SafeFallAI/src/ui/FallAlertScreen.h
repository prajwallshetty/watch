/**
 * SafeFall AI - FallAlertScreen.h
 *
 * Full-screen fall alert with a draining countdown ring:
 *
 *        FALL DETECTED
 *         Are you OK?
 *             10
 *     [ I'M OK ]  [  SOS  ]
 *
 * Also shows the short "alert cancelled" confirmation (USER_CANCELLED).
 */
#pragma once

#include <lvgl.h>
#include "UIModel.h"

class FallAlertScreen {
public:
    void create();
    void update(const UiModel& m);
    lv_obj_t* screen() const { return m_scr; }

private:
    void setCancelledMode(bool cancelled);

    lv_obj_t* m_scr = nullptr;
    lv_obj_t* m_ring = nullptr;
    lv_obj_t* m_title = nullptr;
    lv_obj_t* m_subtitle = nullptr;
    lv_obj_t* m_count = nullptr;
    lv_obj_t* m_btnOk = nullptr;
    lv_obj_t* m_btnSos = nullptr;
    bool m_cancelledMode = false;
    int16_t m_lastRing = -1;
};
