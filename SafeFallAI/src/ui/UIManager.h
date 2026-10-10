/**
 * SafeFall AI - UIManager.h
 *
 * Owns all LVGL screens and decides which one is visible:
 *
 *   AWAITING_CONFIRMATION / FALL_DETECTED / USER_CANCELLED -> FallAlertScreen
 *   EMERGENCY / SOS_ACTIVE                                  -> SOSScreen
 *   anything else -> main pager (Home | SOS | Status | Settings) or the
 *                    hidden DiagnosticsScreen if the developer opened it
 *
 * Screen selection is derived from the model every frame; the UI keeps only
 * navigation state (current page, diagnostics open), never safety state.
 */
#pragma once

#include <Arduino.h>
#include <lvgl.h>
#include "UIModel.h"
#include "HomeScreen.h"
#include "FallAlertScreen.h"
#include "SOSScreen.h"
#include "StatusScreen.h"
#include "SettingsScreen.h"
#include "DiagnosticsScreen.h"

class UIManager {
public:
    bool begin(lv_disp_t* disp);
    void update(const UiModel& m, uint32_t nowMs, bool screenOff);

    void post(UiAction a);
    bool poll(UiAction& out);

    void openDiagnostics();
    void closeDiagnostics();
    void goHome();                 // back to the Home page (e.g. after the screen turned off)

private:
    enum class View : uint8_t { MAIN, ALERT, SOS, DIAG };
    View viewFor(DeviceState s) const;
    void show(View v, bool animate);
    void updateDots();

    lv_obj_t* m_main = nullptr;
    lv_obj_t* m_tiles = nullptr;
    lv_obj_t* m_tile[4] = {nullptr, nullptr, nullptr, nullptr};
    lv_obj_t* m_dots[4] = {nullptr, nullptr, nullptr, nullptr};
    int8_t m_activeTile = -1;

    HomeScreen m_home;
    FallAlertScreen m_alert;
    SOSScreen m_sos;
    StatusScreen m_status;
    SettingsScreen m_settings;
    DiagnosticsScreen m_diag;

    View m_view = View::MAIN;
    bool m_diagOpen = false;

    static const uint8_t QUEUE_LEN = 8;
    UiAction m_queue[QUEUE_LEN];
    uint8_t m_head = 0;
    uint8_t m_tail = 0;
};

extern UIManager UI;
