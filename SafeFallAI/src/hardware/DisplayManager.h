/**
 * SafeFall AI - DisplayManager.h
 *
 * GC9A01 240x240 LCD through TFT_eSPI + LVGL 8.3 display driver.
 *
 * The bring-up is the Waveshare LVGL_Arduino architecture:
 *   tft.begin(); tft.setRotation(0);
 *   lv_init(); lv_disp_draw_buf_init(buf, screen/10);
 *   flush callback = startWrite / setAddrWindow / pushColors(swap=true) / endWrite
 *   esp_timer every 2 ms -> lv_tick_inc(2)
 * Only additions: PWM backlight dimming, panel sleep, and init validation.
 */
#pragma once

#include <Arduino.h>
#include <lvgl.h>

class DisplayManager {
public:
    enum class Power : uint8_t { ON, DIM, OFF };

    bool begin();
    void update(uint32_t nowMs);          // non-blocking wake sequencing + validation

    void setBrightness(uint8_t percent);  // user brightness (applies while ON)
    uint8_t brightness() const { return m_brightness; }
    void setPower(Power p);
    Power power() const { return m_power; }

    bool isOk() const { return m_ok; }
    bool firstFlushDone() const { return m_flushCount > 0; }
    uint32_t flushCount() const { return m_flushCount; }
    lv_disp_t* disp() const { return m_disp; }
    bool panelAsleep() const { return m_panelAsleep; }
    bool backlightPwm() const { return m_pwm; }

    // Diagnostics: duty requested on GPIO2, and the fraction of time GPIO2 is
    // actually high, sampled from the pad (does not disturb the LEDC output).
    uint8_t backlightPercent() const { return m_blPercent; }
    uint8_t measureBacklightHighPercent();
    void logBacklightState(const char* context);

    // LVGL callback (public so the C trampoline can reach it)
    void flush(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_p);

    static const char* powerName(Power p);

private:
    void setupBacklight();
    void runColorTest();
    void applyBacklight(uint8_t percent, const char* why);

    lv_disp_draw_buf_t m_drawBuf;
    lv_disp_drv_t m_dispDrv;
    lv_disp_t* m_disp = nullptr;
    lv_color_t* m_buf = nullptr;

    bool m_ok = false;
    bool m_validated = false;
    uint32_t m_beginMs = 0;
    volatile uint32_t m_flushCount = 0;

    uint8_t m_brightness = 80;
    Power m_power = Power::ON;
    bool m_panelAsleep = false;
    bool m_waking = false;
    uint32_t m_wakeReadyMs = 0;

    bool m_pwm = false;            // LEDC PWM active on GPIO2 (else plain digital)
    uint8_t m_blPercent = 0;       // last value written to the backlight
    uint32_t m_lastDiagLogMs = 0;
};

extern DisplayManager Display;
