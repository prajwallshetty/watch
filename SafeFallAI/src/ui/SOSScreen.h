/**
 * SafeFall AI - SOSScreen.h
 *
 * 1) Emergency screen (EMERGENCY / SOS_ACTIVE):
 *              SOS
 *        Emergency Alert
 *        Sending alert...
 *      [ HOLD TO CANCEL ]
 *
 * 2) SOS page inside the main swipe pager: one large red button that must be
 *    held for SOS_HOLD_MS to raise an SOS (prevents accidental alarms).
 */
#pragma once

#include <lvgl.h>
#include "UIModel.h"
#include "HoldButton.h"

class SOSScreen {
public:
    void create();
    void createTriggerPage(lv_obj_t* parent);
    void update(const UiModel& m);
    void onShow();          // reset hold controls when the screen appears
    void resetTrigger();
    lv_obj_t* screen() const { return m_scr; }

private:
    lv_obj_t* m_scr = nullptr;
    lv_obj_t* m_ring = nullptr;
    lv_obj_t* m_title = nullptr;
    lv_obj_t* m_subtitle = nullptr;
    lv_obj_t* m_status = nullptr;
    lv_obj_t* m_eventId = nullptr;
    HoldButton m_cancel;
    HoldButton m_trigger;
};
