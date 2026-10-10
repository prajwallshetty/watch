/**
 * SafeFall AI - TouchManager.h
 *
 * CST816S capacitive touch through the same library and constructor as the
 * Waveshare LVGL_Arduino example: CST816S touch(6, 7, 13, 5).
 * Registered as an LVGL 8 pointer input device (lv_indev_drv_t).
 */
#pragma once

#include <Arduino.h>
#include <lvgl.h>

class TouchManager {
public:
    // Called on the first contact of a new press. Return true to swallow that
    // whole press (used to wake the screen without clicking whatever is under
    // the finger).
    typedef bool (*WakeHandler)(void* ctx);

    bool begin();
    bool registerWithLvgl();
    void setWakeHandler(WakeHandler fn, void* ctx) { m_wakeFn = fn; m_wakeCtx = ctx; }

    bool isDetected() const { return m_detected; }
    uint8_t chipId() const { return m_chipId; }
    uint32_t lastTouchMs() const { return m_lastTouchMs; }
    uint32_t touchCount() const { return m_touchCount; }

    void read(lv_indev_drv_t* drv, lv_indev_data_t* data);

private:
    bool m_detected = false;
    uint8_t m_chipId = 0;
    lv_indev_drv_t m_indevDrv;

    bool m_pressed = false;
    bool m_swallowing = false;
    int16_t m_lastX = 0;
    int16_t m_lastY = 0;
    uint32_t m_lastEventMs = 0;
    uint32_t m_lastTouchMs = 0;
    uint32_t m_touchCount = 0;

    WakeHandler m_wakeFn = nullptr;
    void* m_wakeCtx = nullptr;
};

extern TouchManager Touch;
