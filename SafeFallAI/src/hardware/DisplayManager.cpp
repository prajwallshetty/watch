#include "DisplayManager.h"
#include "../config/BoardConfig.h"
#include "../config/AppConfig.h"
#include "../config/DebugConfig.h"

#include <TFT_eSPI.h>
#include <esp_timer.h>
#include <esp_task_wdt.h>
#include <driver/gpio.h>
#include <soc/gpio_periph.h>
#include <soc/io_mux_reg.h>

#if LVGL_VERSION_MAJOR != 8
#error "SafeFall AI requires LVGL 8.3.x (tested with 8.3.10). LVGL 9 is not supported."
#endif
#if LV_COLOR_DEPTH != 16
#error "lv_conf.h: LV_COLOR_DEPTH must be 16 for the GC9A01 (use extras/lv_conf.h)."
#endif
#if LV_COLOR_16_SWAP != 0
#error "lv_conf.h: LV_COLOR_16_SWAP must be 0; the flush callback swaps bytes in pushColors() exactly like LVGL_Arduino."
#endif
#if LV_TICK_CUSTOM != 0
#error "lv_conf.h: LV_TICK_CUSTOM must be 0; the tick is driven by an esp_timer calling lv_tick_inc() like LVGL_Arduino."
#endif

DisplayManager Display;

static TFT_eSPI tft = TFT_eSPI(BOARD_LCD_WIDTH, BOARD_LCD_HEIGHT);
static esp_timer_handle_t s_tickTimer = nullptr;

static void lvglTick(void* /*arg*/) {
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

static void flushTrampoline(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_p) {
    Display.flush(drv, area, color_p);
}

// Same body as the LVGL_Arduino my_disp_flush().
void DisplayManager::flush(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_p) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);

    if (!m_panelAsleep) {
        tft.startWrite();
        tft.setAddrWindow(area->x1, area->y1, w, h);
        tft.pushColors((uint16_t*)&color_p->full, w * h, true);
        tft.endWrite();
    }
    m_flushCount++;
    lv_disp_flush_ready(drv);
}

const char* DisplayManager::powerName(Power p) {
    switch (p) {
        case Power::ON:  return "ON";
        case Power::DIM: return "DIM";
        case Power::OFF: return "OFF";
    }
    return "?";
}

// Fraction of time GPIO2 reads high, sampled over ~2 ms (about 10 PWM periods
// at 5 kHz). Only the pad's input buffer is enabled (FUN_IE); the output
// routing to LEDC is not touched. gpio_set_direction(INPUT_OUTPUT) is NOT
// used because in IDF 4.4 it reconnects the pin to the plain GPIO output,
// which would itself switch the backlight off.
uint8_t DisplayManager::measureBacklightHighPercent() {
    // Re-enabled every time: ledcAttachPin()/pinMode() reconfigure the pad and
    // clear the input-enable bit.
    PIN_INPUT_ENABLE(GPIO_PIN_MUX_REG[BOARD_LCD_PIN_BL]);
    const uint32_t samples = 2000;
    uint32_t high = 0;
    for (uint32_t i = 0; i < samples; i++) {
        high += (uint32_t)gpio_get_level((gpio_num_t)BOARD_LCD_PIN_BL);
        delayMicroseconds(1);
    }
    return (uint8_t)((high * 100u + samples / 2) / samples);
}

void DisplayManager::logBacklightState(const char* context) {
    const uint8_t measured = measureBacklightHighPercent();
    if (m_pwm) {
        LOG_DISPLAY("BL[%s] GPIO%d mode=PWM ch%d duty=%lu/%u (%u%%) measured_high=%u%% power=%s panel=%s%s flushes=%lu",
                    context, BOARD_LCD_PIN_BL, BOARD_BL_PWM_CHANNEL, (unsigned long)ledcRead(BOARD_BL_PWM_CHANNEL),
                    (1u << BOARD_BL_PWM_BITS) - 1u, m_blPercent, measured, powerName(m_power),
                    m_panelAsleep ? "SLEEP" : "AWAKE", m_waking ? " (waking)" : "", (unsigned long)m_flushCount);
    } else {
        LOG_DISPLAY("BL[%s] GPIO%d mode=DIGITAL level_set=%s measured_high=%u%% power=%s panel=%s%s flushes=%lu",
                    context, BOARD_LCD_PIN_BL, m_blPercent > 0 ? "HIGH" : "LOW", measured, powerName(m_power),
                    m_panelAsleep ? "SLEEP" : "AWAKE", m_waking ? " (waking)" : "", (unsigned long)m_flushCount);
    }
}

// Backlight hand-over from TFT_eSPI (which set GPIO2 OUTPUT + TFT_BACKLIGHT_ON)
// to LEDC PWM, verified by reading the pin. If no PWM appears on the pin, fall
// back to a plain HIGH exactly as TFT_eSPI/LVGL_Arduino drive it.
void DisplayManager::setupBacklight() {
    LOG_DISPLAY("Backlight GPIO%d after tft.begin(): measured_high=%u%% (TFT_eSPI sets it %s)", BOARD_LCD_PIN_BL,
                measureBacklightHighPercent(), TFT_BACKLIGHT_ON == HIGH ? "HIGH" : "LOW");
#if DISPLAY_BACKLIGHT_PWM
    const uint32_t freq = ledcSetup(BOARD_BL_PWM_CHANNEL, BOARD_BL_PWM_FREQ_HZ, BOARD_BL_PWM_BITS);
    if (freq == 0) {
        LOG_DISPLAY("ERROR: ledcSetup(ch%d, %d Hz, %d bit) failed - using digital backlight", BOARD_BL_PWM_CHANNEL,
                    BOARD_BL_PWM_FREQ_HZ, BOARD_BL_PWM_BITS);
        m_pwm = false;
    } else {
        ledcAttachPin(BOARD_LCD_PIN_BL, BOARD_BL_PWM_CHANNEL);
        m_pwm = true;
        applyBacklight(100, "init (forced on)");
        const uint8_t measured = measureBacklightHighPercent();
        if (measured < 50) {
            LOG_DISPLAY("ERROR: PWM duty 100%% requested but GPIO%d measured high only %u%% - falling back to digital HIGH",
                        BOARD_LCD_PIN_BL, measured);
            ledcDetachPin(BOARD_LCD_PIN_BL);
            m_pwm = false;
        }
    }
#endif
    if (!m_pwm) {
        pinMode(BOARD_LCD_PIN_BL, OUTPUT);
        applyBacklight(100, "init (forced on)");
    }
    logBacklightState("after init");
}

// Drawn with TFT_eSPI directly, before LVGL owns the panel. Proves SPI wiring,
// the GC9A01 init and the backlight independently of LVGL and the UI.
void DisplayManager::runColorTest() {
    struct Step { uint16_t color; const char* name; };
    static const Step steps[] = {
        {TFT_RED, "RED"}, {TFT_GREEN, "GREEN"}, {TFT_BLUE, "BLUE"}, {TFT_WHITE, "WHITE"},
    };
    LOG_DISPLAY("Colour test: the screen should show RED, GREEN, BLUE, WHITE (%d ms each)",
                DISPLAY_COLOR_TEST_STEP_MS);
    for (const Step& st : steps) {
        tft.fillScreen(st.color);
        LOG_DISPLAY("Colour test: %s", st.name);
        const uint32_t start = millis();
        while (millis() - start < DISPLAY_COLOR_TEST_STEP_MS) {
            esp_task_wdt_reset();  // boot-only wait; keep the watchdog fed
            delay(10);
        }
    }
    tft.fillScreen(TFT_BLACK);
    LOG_DISPLAY("Colour test done (if RED looked blue, the panel colour order is swapped)");
}

bool DisplayManager::begin() {
    m_beginMs = millis();

    // --- TFT_eSPI bring-up (LVGL_Arduino / hardware test) ---
    tft.begin();                       // GC9A01 init sequence from TFT_eSPI, backlight HIGH
    tft.setRotation(BOARD_LCD_ROTATION);
    tft.fillScreen(TFT_BLACK);
    LOG_DISPLAY("GC9A01 initialized (%dx%d, rotation %d, SPI %lu Hz)",
                BOARD_LCD_WIDTH, BOARD_LCD_HEIGHT, BOARD_LCD_ROTATION, (unsigned long)SPI_FREQUENCY);

    // --- Backlight: forced fully on and verified ---
    setupBacklight();

#if DISPLAY_BOOT_COLOR_TEST
    runColorTest();
#endif

    // --- LVGL ---
    lv_init();

    const uint32_t bufPixels = (uint32_t)BOARD_LCD_WIDTH * LVGL_DRAW_BUF_LINES;
    m_buf = (lv_color_t*)heap_caps_malloc(bufPixels * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (m_buf == nullptr) {
        LOG_DISPLAY("ERROR: draw buffer allocation failed (%lu bytes)", (unsigned long)(bufPixels * sizeof(lv_color_t)));
        return false;
    }
    lv_disp_draw_buf_init(&m_drawBuf, m_buf, nullptr, bufPixels);

    lv_disp_drv_init(&m_dispDrv);
    m_dispDrv.hor_res = BOARD_LCD_WIDTH;
    m_dispDrv.ver_res = BOARD_LCD_HEIGHT;
    m_dispDrv.flush_cb = flushTrampoline;
    m_dispDrv.draw_buf = &m_drawBuf;
    m_disp = lv_disp_drv_register(&m_dispDrv);
    if (m_disp == nullptr) {
        LOG_DISPLAY("ERROR: lv_disp_drv_register failed");
        return false;
    }

    // --- LVGL tick: esp_timer, LVGL_TICK_PERIOD_MS (LVGL_Arduino pattern) ---
    const esp_timer_create_args_t args = {
        .callback = &lvglTick,
        .arg = nullptr,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "lvgl_tick",
        .skip_unhandled_events = true,
    };
    if (esp_timer_create(&args, &s_tickTimer) != ESP_OK ||
        esp_timer_start_periodic(s_tickTimer, LVGL_TICK_PERIOD_MS * 1000) != ESP_OK) {
        LOG_DISPLAY("ERROR: LVGL tick timer failed");
        return false;
    }

    m_ok = true;
    LOG_DISPLAY("LVGL %d.%d.%d ready, draw buffer %u lines", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR,
                LVGL_VERSION_PATCH, (unsigned)LVGL_DRAW_BUF_LINES);
    return true;
}

void DisplayManager::update(uint32_t nowMs) {
    // Validation: the panel has no reliable read-back on this board, so the
    // runtime check is that LVGL actually produced and pushed frames.
    if (m_ok && !m_validated) {
        if (m_flushCount > 0) {
            m_validated = true;
            LOG_DISPLAY("First frame flushed after %lu ms", (unsigned long)(nowMs - m_beginMs));
            logBacklightState("first frame");
        } else if (nowMs - m_beginMs > DISPLAY_FIRST_FLUSH_TIMEOUT_MS) {
            m_ok = false;
            m_validated = true;
            LOG_DISPLAY("ERROR: no frame flushed within %d ms", DISPLAY_FIRST_FLUSH_TIMEOUT_MS);
        }
    }

    // Non-blocking wake: SLPOUT needs 120 ms before display-on/backlight.
    if (m_waking && (int32_t)(nowMs - m_wakeReadyMs) >= 0) {
        m_waking = false;
        tft.writecommand(LCD_CMD_DISPON);
        lv_obj_invalidate(lv_scr_act());
        applyBacklight(m_power == Power::DIM ? BRIGHTNESS_DIM_PERCENT : m_brightness, "wake complete");
        LOG_DISPLAY("Panel wake complete: DISPON (0x29) sent, full redraw requested");
        logBacklightState("after wake");
    }

#if DISPLAY_DIAGNOSTIC_MODE
    if (nowMs - m_lastDiagLogMs >= DISPLAY_DIAG_LOG_INTERVAL_MS) {
        m_lastDiagLogMs = nowMs;
        logBacklightState("periodic");
    }
#endif
}

void DisplayManager::applyBacklight(uint8_t percent, const char* why) {
    if (percent > 100) percent = 100;
    const bool changed = (percent != m_blPercent);
    m_blPercent = percent;
    if (m_pwm) {
        const uint32_t maxDuty = (1u << BOARD_BL_PWM_BITS) - 1u;
        const uint32_t duty = (maxDuty * percent) / 100u;
        ledcWrite(BOARD_BL_PWM_CHANNEL, duty);
        if (changed) {
            LOG_DISPLAY("Backlight GPIO%d -> %u%% (PWM duty %lu/%lu) [%s]", BOARD_LCD_PIN_BL, percent,
                        (unsigned long)duty, (unsigned long)maxDuty, why);
        }
    } else {
        // Digital mode: any non-zero level is fully on (no dimming).
        digitalWrite(BOARD_LCD_PIN_BL, percent > 0 ? TFT_BACKLIGHT_ON : !TFT_BACKLIGHT_ON);
        if (changed) {
            LOG_DISPLAY("Backlight GPIO%d -> %s (digital, requested %u%%) [%s]", BOARD_LCD_PIN_BL,
                        percent > 0 ? "ON" : "OFF", percent, why);
        }
    }
}

void DisplayManager::setBrightness(uint8_t percent) {
    if (percent < BRIGHTNESS_MIN_PERCENT) percent = BRIGHTNESS_MIN_PERCENT;
    if (percent > 100) percent = 100;
    m_brightness = percent;
    if (m_power == Power::ON && !m_waking) applyBacklight(m_brightness, "brightness setting");
}

void DisplayManager::setPower(Power p) {
    if (p == m_power) return;
    const Power prev = m_power;
    m_power = p;
    LOG_DISPLAY("Power %s -> %s", powerName(prev), powerName(p));

    if (p == Power::OFF) {
        applyBacklight(0, "screen off");
#if DISPLAY_PANEL_SLEEP_WHEN_OFF
        tft.writecommand(LCD_CMD_DISPOFF);
        tft.writecommand(LCD_CMD_SLPIN);
        m_panelAsleep = true;
        LOG_DISPLAY("Panel sleep: DISPOFF (0x28) + SLPIN (0x10) sent; LVGL flushes paused");
#endif
        m_waking = false;
        LOG_DISPLAY("Screen off");
        return;
    }

    if (prev == Power::OFF && m_panelAsleep) {
        tft.writecommand(LCD_CMD_SLPOUT);
        m_panelAsleep = false;
        m_waking = true;
        m_wakeReadyMs = millis() + LCD_SLPOUT_DELAY_MS;
        LOG_DISPLAY("Screen waking: SLPOUT (0x11) sent, DISPON + backlight in %d ms", LCD_SLPOUT_DELAY_MS);
        return;
    }

    if (!m_waking) {
        applyBacklight(p == Power::DIM ? BRIGHTNESS_DIM_PERCENT : m_brightness, p == Power::DIM ? "dim" : "screen on");
    }
}
