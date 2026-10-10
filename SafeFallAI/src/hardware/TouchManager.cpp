#include "TouchManager.h"
#include "I2CBus.h"
#include "../config/BoardConfig.h"
#include "../config/DebugConfig.h"

#include <CST816S.h>

TouchManager Touch;

// Exactly as in LVGL_Arduino: CST816S touch(6, 7, 13, 5); // sda, scl, rst, irq
static CST816S s_touch(BOARD_TOUCH_PIN_SDA, BOARD_TOUCH_PIN_SCL, BOARD_TOUCH_PIN_RST, BOARD_TOUCH_PIN_IRQ);

// The CST816S raises IRQ pulses roughly every 10 ms while a finger is down.
// LVGL polls every LV_INDEV_DEF_READ_PERIOD (30 ms); bridge the gaps so a
// held finger is not reported as a release between pulses.
static const uint32_t TOUCH_HOLD_BRIDGE_MS = 60;

static void touchReadTrampoline(lv_indev_drv_t* drv, lv_indev_data_t* data) {
    Touch.read(drv, data);
}

bool TouchManager::begin() {
    {
        I2CLock guard(200);
        // CST816S::begin(): Wire.begin(6,7), reset pulse on GPIO13, reads chip
        // version registers, attaches the IRQ handler on GPIO5 (RISING).
        s_touch.begin();
    }
    // versionInfo[0] is the chip ID register (0xA7). The controller is awake
    // right after the reset pulse in begin(), so a non-zero ID means it
    // answered. It auto-sleeps later and only wakes on touch, so a missing ID
    // is logged but touch is still registered.
    m_chipId = s_touch.data.versionInfo[0];
    m_detected = (m_chipId != 0x00 && m_chipId != 0xFF) || I2CBus::probe(BOARD_TOUCH_I2C_ADDR);

    if (m_detected) {
        LOG_TOUCH("CST816S detected (chip id 0x%02X, fw %u)", m_chipId, s_touch.data.version);
    } else {
        LOG_TOUCH("WARNING: CST816S did not answer on 0x%02X (may be asleep); will mark OK on first touch",
                  BOARD_TOUCH_I2C_ADDR);
    }
    return m_detected;
}

bool TouchManager::registerWithLvgl() {
    lv_indev_drv_init(&m_indevDrv);
    m_indevDrv.type = LV_INDEV_TYPE_POINTER;
    m_indevDrv.read_cb = touchReadTrampoline;
    m_indevDrv.long_press_time = 800;
    m_indevDrv.scroll_limit = 12;
    m_indevDrv.gesture_min_velocity = 3;
    m_indevDrv.gesture_limit = 40;
    lv_indev_t* indev = lv_indev_drv_register(&m_indevDrv);
    if (indev == nullptr) {
        LOG_TOUCH("ERROR: lv_indev_drv_register failed");
        return false;
    }
    return true;
}

void TouchManager::read(lv_indev_drv_t* /*drv*/, lv_indev_data_t* data) {
    const uint32_t now = millis();
    bool event = false;
    {
        I2CLock guard(5);  // never stall the UI on a busy bus; retry next poll
        if (guard.ok()) event = s_touch.available();
    }

    if (event) {
        m_lastEventMs = now;
        // data.event: 0 = down, 1 = up (lift), 2 = contact
        const bool down = (s_touch.data.event != 1);
        int16_t x = (int16_t)s_touch.data.x;
        int16_t y = (int16_t)s_touch.data.y;
#if BOARD_TOUCH_SWAP_XY
        int16_t t = x; x = y; y = t;
#endif
#if BOARD_TOUCH_MIRROR_X
        x = (BOARD_LCD_WIDTH - 1) - x;
#endif
#if BOARD_TOUCH_MIRROR_Y
        y = (BOARD_LCD_HEIGHT - 1) - y;
#endif
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (x >= BOARD_LCD_WIDTH) x = BOARD_LCD_WIDTH - 1;
        if (y >= BOARD_LCD_HEIGHT) y = BOARD_LCD_HEIGHT - 1;

        if (down) {
            if (!m_pressed) {
                m_touchCount++;
                if (!m_detected) {
                    m_detected = true;
                    LOG_TOUCH("CST816S responding (first touch)");
                }
                m_swallowing = (m_wakeFn != nullptr) && m_wakeFn(m_wakeCtx);
            }
            m_pressed = true;
            m_lastX = x;
            m_lastY = y;
            m_lastTouchMs = now;
        } else {
            m_pressed = false;
        }
    } else if (m_pressed && (now - m_lastEventMs) > TOUCH_HOLD_BRIDGE_MS) {
        m_pressed = false;  // lift event was missed
    }

    if (!m_pressed) m_swallowing = false;

    data->point.x = m_lastX;
    data->point.y = m_lastY;
    data->state = (m_pressed && !m_swallowing) ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
}
