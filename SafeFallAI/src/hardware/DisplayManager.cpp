#include "DisplayManager.h"
#include "../config/BoardConfig.h"
#include "../config/AppConfig.h"
#include "../config/DebugConfig.h"

#include <TFT_eSPI.h>
#include <esp_timer.h>

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

bool DisplayManager::begin() {
    m_beginMs = millis();

    // --- TFT_eSPI bring-up (LVGL_Arduino / hardware test) ---
    tft.begin();                       // GC9A01 init sequence from TFT_eSPI, backlight HIGH
    tft.setRotation(BOARD_LCD_ROTATION);
    tft.fillScreen(TFT_BLACK);
    LOG_DISPLAY("GC9A01 initialized (%dx%d, rotation %d, SPI %lu Hz)",
                BOARD_LCD_WIDTH, BOARD_LCD_HEIGHT, BOARD_LCD_ROTATION, (unsigned long)SPI_FREQUENCY);

    // --- Backlight: take over the pin TFT_eSPI drove HIGH and switch to PWM ---
    ledcSetup(BOARD_BL_PWM_CHANNEL, BOARD_BL_PWM_FREQ_HZ, BOARD_BL_PWM_BITS);
    ledcAttachPin(BOARD_LCD_PIN_BL, BOARD_BL_PWM_CHANNEL);
    applyBacklight(m_brightness);

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
        applyBacklight(m_power == Power::DIM ? BRIGHTNESS_DIM_PERCENT : m_brightness);
    }
}

void DisplayManager::applyBacklight(uint8_t percent) {
    if (percent > 100) percent = 100;
    const uint32_t maxDuty = (1u << BOARD_BL_PWM_BITS) - 1u;
    ledcWrite(BOARD_BL_PWM_CHANNEL, (maxDuty * percent) / 100u);
}

void DisplayManager::setBrightness(uint8_t percent) {
    if (percent < BRIGHTNESS_MIN_PERCENT) percent = BRIGHTNESS_MIN_PERCENT;
    if (percent > 100) percent = 100;
    m_brightness = percent;
    if (m_power == Power::ON && !m_waking) applyBacklight(m_brightness);
}

void DisplayManager::setPower(Power p) {
    if (p == m_power) return;
    const Power prev = m_power;
    m_power = p;

    if (p == Power::OFF) {
        applyBacklight(0);
#if DISPLAY_PANEL_SLEEP_WHEN_OFF
        tft.writecommand(LCD_CMD_DISPOFF);
        tft.writecommand(LCD_CMD_SLPIN);
        m_panelAsleep = true;
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
        LOG_DISPLAY("Screen waking");
        return;
    }

    if (!m_waking) applyBacklight(p == Power::DIM ? BRIGHTNESS_DIM_PERCENT : m_brightness);
}
