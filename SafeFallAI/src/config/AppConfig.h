/**
 * SafeFall AI - AppConfig.h
 *
 * Application-level configuration: firmware identity, fall-detection
 * thresholds, timings, power and BLE behaviour.
 *
 * ENGINEERING PROTOTYPE - NOT A MEDICAL DEVICE.
 * The thresholds below are starting points for experimentation. They have not
 * been clinically validated and the algorithm will not detect every fall.
 */
#pragma once

#include <Arduino.h>

// ---------------------------------------------------------------------------
// Firmware identity
// ---------------------------------------------------------------------------
#define SAFE_FALL_VERSION        "0.1.0"
#define SAFE_FALL_PRODUCT        "SafeFall AI"
#define BLE_DEVICE_NAME          "SafeFall-AI"
#define EVENT_ID_PREFIX          "SF-"          // Event IDs look like SF-000001

// ---------------------------------------------------------------------------
// IMU sampling
// ---------------------------------------------------------------------------
#define IMU_SAMPLE_PERIOD_MS     10             // 100 Hz processing rate
// QMI8658 register values. Ranges follow the Waveshare driver (+-8 g, +-512 dps).
// The output data rate is lowered from Waveshare's 1 kHz to 125 Hz so that the
// sensor's internal decimation filter band-limits the signal we sample at 100 Hz.
#define IMU_ACCEL_RANGE_REG      0x02           // 0x00=2g 0x01=4g 0x02=8g 0x03=16g
#define IMU_ACCEL_LSB_PER_G      4096.0f        // must match IMU_ACCEL_RANGE_REG
#define IMU_ACCEL_ODR_REG        0x06           // 0x06 = 125 Hz
#define IMU_GYRO_RANGE_REG       0x04           // 0x04 = 512 dps
#define IMU_GYRO_LSB_PER_DPS     64.0f          // must match IMU_GYRO_RANGE_REG
#define IMU_GYRO_ODR_REG         0x06           // 0x06 = 125 Hz

#define IMU_MAX_CONSECUTIVE_ERRORS   10         // read failures before declaring fault
#define IMU_RECOVERY_INTERVAL_MS     2000       // re-init attempt period while faulted
#define IMU_STUCK_SAMPLE_COUNT       300        // identical raw frames (3 s) => stuck sensor
#define IMU_HEALTH_CHECK_MS          10000      // periodic WHO_AM_I verification
#define IMU_TASK_STACK               6144
#define IMU_TASK_PRIORITY            3
#define IMU_TASK_CORE                1

// ---------------------------------------------------------------------------
// Fall detection - multi-stage algorithm (see FallDetector.cpp)
// All accelerations in g, angular rates in degrees/second.
// ---------------------------------------------------------------------------
// Stage 1: free-fall (optional evidence, increases confidence)
#define FALL_FREEFALL_G                  0.55f
#define FALL_FREEFALL_MIN_MS             60
#define FALL_FREEFALL_TO_IMPACT_MAX_MS   1000

// Stage 2: impact
#define FALL_IMPACT_G                    2.5f   // "Normal" sensitivity trigger
#define FALL_IMPACT_FULL_SCORE_G         6.0f   // impact magnitude giving the full impact score
#define FALL_IMPACT_DEBOUNCE_MS          200
#define FALL_IMPACT_MAX_HIGH_G_MS        400    // longer high-g periods = vibration/sport, not a fall

// Stage 3: post-impact analysis
#define FALL_POST_IMPACT_SETTLE_MS       1500   // let the body come to rest after impact
#define FALL_PRE_IMPACT_LOOKBACK_MS      1500   // orientation reference taken this long before impact
#define FALL_GYRO_MIN_SCORE_DPS          100.0f
#define FALL_GYRO_FULL_SCORE_DPS         400.0f

// Stage 4: orientation change
#define FALL_ORIENT_MIN_SCORE_DEG        20.0f
#define FALL_ORIENT_FULL_SCORE_DEG       70.0f

// Stage 5: motionless period
#define FALL_INACTIVITY_WINDOW_MS        4000
#define FALL_STILL_ACC_DEV_G             0.25f  // | |a| - 1g | below this counts as still
#define FALL_STILL_GYRO_DPS              40.0f
#define FALL_INACTIVITY_MIN_RATIO        0.80f  // fraction of still samples required (mandatory)
#define FALL_RECOVERY_ACC_DEV_G          0.60f  // a sample above either limit counts as "active"
#define FALL_RECOVERY_GYRO_DPS           150.0f
#define FALL_RECOVERY_WINDOW_MS          1000   // early abort: active fraction over this window...
#define FALL_RECOVERY_ACTIVE_FRACTION    0.50f  // ...at or above this = user is up and moving
// Strong evidence (impact + free-fall + rotation + orientation, max 80 points)
// relaxes the stillness rule: a conscious person moving an arm on the floor
// must still get the "Are you OK?" prompt.
#define FALL_STRONG_EVIDENCE_SCORE       55
#define FALL_INACTIVITY_MIN_RATIO_STRONG 0.40f

// Decision + false-positive filtering
#define FALL_CONFIDENCE_THRESHOLD        60     // 0..100, "Normal" sensitivity
#define FALL_SENSITIVITY_CONF_STEP       10     // Low: +10, High: -10
#define FALL_SENSITIVITY_LOW_IMPACT_MUL  1.25f
#define FALL_SENSITIVITY_HIGH_IMPACT_MUL 0.80f
#define FALL_REPETITIVE_WINDOW_MS        4000   // impacts within this window...
#define FALL_REPETITIVE_COUNT            3      // ...at or above this count = activity, not a fall
#define FALL_COOLDOWN_MS                 20000  // after a confirmed fall
#define FALL_REJECT_COOLDOWN_MS          1000   // after a rejected candidate
#define FALL_POSSIBLE_TIMEOUT_MS         15000  // safety net for POSSIBLE_FALL state

// Motion classification for the UI ("Motion Normal")
#define MOTION_STILL_STDDEV_G            0.03f
#define MOTION_ACTIVE_STDDEV_G           0.30f

// ---------------------------------------------------------------------------
// User confirmation / emergency
// ---------------------------------------------------------------------------
#define FALL_CONFIRM_COUNTDOWN_S         10
#define FALL_COUNTDOWN_MIN_S             5
#define FALL_COUNTDOWN_MAX_S             60
#define USER_CANCELLED_DISPLAY_MS        2500
#define SOS_HOLD_MS                      1500   // hold-to-trigger SOS on the SOS page
#define CANCEL_HOLD_MS                   1500   // hold-to-cancel an active emergency
#define BOOT_BUTTON_SOS_HOLD_MS          3000   // physical BOOT key long-press = SOS
#define ENABLE_BOOT_BUTTON_SOS           1

// ---------------------------------------------------------------------------
// Battery
// ---------------------------------------------------------------------------
#define BATTERY_SAMPLE_INTERVAL_MS       2000
#define BATTERY_ADC_SAMPLES              16
#define BATTERY_LOW_PERCENT              20
#define BATTERY_CRITICAL_PERCENT         10
#define BATTERY_HYSTERESIS_PERCENT       3
#define BATTERY_PRESENT_MIN_V            2.5f   // below this no battery is considered connected

// ---------------------------------------------------------------------------
// Display / power
// ---------------------------------------------------------------------------
#define BRIGHTNESS_DEFAULT_PERCENT       80
#define BRIGHTNESS_MIN_PERCENT           10
#define BRIGHTNESS_STEP_PERCENT          10
#define BRIGHTNESS_DIM_PERCENT           8
#define SCREEN_TIMEOUT_DEFAULT_S         20     // display off after this idle time
#define SCREEN_TIMEOUT_MIN_S             10
#define SCREEN_TIMEOUT_MAX_S             120
#define SCREEN_DIM_BEFORE_OFF_S          6      // dim this many seconds before turning off
#define DISPLAY_PANEL_SLEEP_WHEN_OFF     1      // GC9A01 sleep-in while the screen is off
#define LVGL_DRAW_BUF_LINES              24     // 240 * 240 / 10 = 24 lines (LVGL_Arduino sizing)
#define LVGL_TICK_PERIOD_MS              2      // esp_timer -> lv_tick_inc()

// ---------------------------------------------------------------------------
// BLE
// ---------------------------------------------------------------------------
#define BLE_PREFERRED_MTU                247
#define BLE_MAX_PACKET_LEN               180    // fits iOS default ATT MTU 185 (payload 182)
#define BLE_STATUS_INTERVAL_MS           30000
#define BLE_BATTERY_INTERVAL_MS          60000
#define BLE_EVENT_RETRY_MS               4000
#define BLE_ADV_RESTART_DELAY_MS         500
#define BLE_ADV_WATCHDOG_MS              120000 // re-arm advertising periodically while disconnected
#define BLE_RX_QUEUE_LEN                 8
#define EVENT_OUTBOX_SIZE                8
#define EVENT_RECENT_CANCEL_SIZE         8

// ---------------------------------------------------------------------------
// Robustness
// ---------------------------------------------------------------------------
#define WDT_TIMEOUT_S                    8
#define IMU_FAULT_REBOOT_MS              300000 // reboot if the IMU stays dead 5 min (only when idle)
#define DISPLAY_FIRST_FLUSH_TIMEOUT_MS   3000
#define SETTINGS_SAVE_DELAY_MS           2000   // debounce NVS writes

// ---------------------------------------------------------------------------
// Time
// ---------------------------------------------------------------------------
#define TIME_FORMAT_24H                  1
#define TIME_VALID_EPOCH_MIN             1700000000UL  // anything earlier = never synced
