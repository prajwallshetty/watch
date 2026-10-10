/**
 * SafeFall AI - BoardConfig.h
 *
 * Hardware definition for the Waveshare ESP32-S3-Touch-LCD-1.28.
 *
 * Every value here was taken from Waveshare reference material, NOT guessed:
 *
 *  LCD (GC9A01, SPI)    : Waveshare's own TFT_eSPI setup for this board
 *                         (User_Setups/Setup302_Waveshare_ESP32S3_GC9A01.h,
 *                         submitted to Bodmer/TFT_eSPI PR #2844):
 *                         MOSI 11, SCLK 10, CS 9, DC 8, RST 14, MISO 12, BL 2,
 *                         backlight active HIGH, 240x240, 40 MHz SPI.
 *  Touch (CST816S, I2C) : LVGL_Arduino example: CST816S touch(6, 7, 13, 5);
 *                         -> SDA 6, SCL 7, RST 13, IRQ 5, address 0x15.
 *  IMU (QMI8658, I2C)   : shares the touch I2C bus (SDA 6, SCL 7), address 0x6B,
 *                         WHO_AM_I = 0x05 (Waveshare qmi8658 driver).
 *  Battery              : GPIO1 through a 200K/100K divider; Waveshare formula
 *                         V = 3.3 / (1 << 12) * 3 * AD_value.
 *
 * The LCD pins themselves live in the TFT_eSPI setup file (TFT_eSPI owns the
 * SPI bus). This header re-states them only so that BoardConfig can verify at
 * COMPILE TIME that the TFT_eSPI setup selected in the library matches this
 * board. A mismatch is a hard #error instead of a blank screen.
 */
#pragma once

#include <Arduino.h>

// ---------------------------------------------------------------------------
// Board identity
// ---------------------------------------------------------------------------
#define BOARD_NAME "Waveshare ESP32-S3-Touch-LCD-1.28"

// ---------------------------------------------------------------------------
// Display (GC9A01, owned by TFT_eSPI)
// ---------------------------------------------------------------------------
#define BOARD_LCD_WIDTH        240
#define BOARD_LCD_HEIGHT       240
#define BOARD_LCD_ROTATION     0      // LVGL_Arduino / hardware test use setRotation(0)

#define BOARD_LCD_PIN_MOSI     11
#define BOARD_LCD_PIN_SCLK     10
#define BOARD_LCD_PIN_CS       9
#define BOARD_LCD_PIN_DC       8
#define BOARD_LCD_PIN_RST      14
#define BOARD_LCD_PIN_BL       2

// Backlight PWM (ESP32 Arduino Core 2.0.x LEDC API: ledcSetup/ledcAttachPin)
#define BOARD_BL_PWM_CHANNEL   7
#define BOARD_BL_PWM_FREQ_HZ   5000
#define BOARD_BL_PWM_BITS      8

// Standard MIPI-DCS commands, the same ones TFT_eSPI's GC9A01_Init.h sends
// (0x11 sleep-out + 120 ms, 0x29 display-on). Used for screen-off power saving.
#define LCD_CMD_SLPIN          0x10
#define LCD_CMD_SLPOUT         0x11
#define LCD_CMD_DISPOFF        0x28
#define LCD_CMD_DISPON         0x29
#define LCD_SLPOUT_DELAY_MS    120

// ---------------------------------------------------------------------------
// Shared I2C bus (touch + IMU)
// ---------------------------------------------------------------------------
#define BOARD_I2C_SDA          6
#define BOARD_I2C_SCL          7
#define BOARD_I2C_FREQ_HZ      400000

// ---------------------------------------------------------------------------
// Touch (CST816S) - identical to LVGL_Arduino: CST816S touch(6, 7, 13, 5)
// ---------------------------------------------------------------------------
#define BOARD_TOUCH_PIN_SDA    BOARD_I2C_SDA
#define BOARD_TOUCH_PIN_SCL    BOARD_I2C_SCL
#define BOARD_TOUCH_PIN_RST    13
#define BOARD_TOUCH_PIN_IRQ    5
#define BOARD_TOUCH_I2C_ADDR   0x15

// The LVGL_Arduino example passes touch.data.x/y straight to LVGL with
// rotation 0. Keep these at 0 unless you change BOARD_LCD_ROTATION.
#define BOARD_TOUCH_SWAP_XY    0
#define BOARD_TOUCH_MIRROR_X   0
#define BOARD_TOUCH_MIRROR_Y   0

// ---------------------------------------------------------------------------
// IMU (QMI8658)
// ---------------------------------------------------------------------------
#define BOARD_IMU_I2C_ADDR     0x6B
#define BOARD_IMU_I2C_ADDR_ALT 0x6A   // SA0 low variant, probed as a fallback
#define BOARD_IMU_WHO_AM_I     0x05

// ---------------------------------------------------------------------------
// Battery measurement (GPIO1, 200K/100K divider)
// ---------------------------------------------------------------------------
#define BOARD_BAT_ADC_PIN      1
#define BOARD_BAT_ADC_BITS     12
#define BOARD_BAT_ADC_REF_V    3.3f
#define BOARD_BAT_DIVIDER      3.0f

// ---------------------------------------------------------------------------
// Buttons
// ---------------------------------------------------------------------------
#define BOARD_BOOT_BUTTON_PIN  0      // BOOT key, active LOW, usable after boot

// ---------------------------------------------------------------------------
// Compile-time verification of the TFT_eSPI setup.
// TFT_eSPI.h pulls in the setup selected in User_Setup_Select.h; if that is not
// the Waveshare GC9A01 setup, stop the build with a clear message.
// ---------------------------------------------------------------------------
#include <TFT_eSPI.h>

#if !defined(GC9A01_DRIVER)
#error "TFT_eSPI is not configured for GC9A01. Select the Waveshare ESP32-S3-Touch-LCD-1.28 setup (see README: 'TFT_eSPI setup')."
#endif
#if (TFT_WIDTH != BOARD_LCD_WIDTH) || (TFT_HEIGHT != BOARD_LCD_HEIGHT)
#error "TFT_eSPI TFT_WIDTH/TFT_HEIGHT must be 240x240 for this board."
#endif
#if (TFT_MOSI != BOARD_LCD_PIN_MOSI) || (TFT_SCLK != BOARD_LCD_PIN_SCLK) || (TFT_CS != BOARD_LCD_PIN_CS) || \
    (TFT_DC != BOARD_LCD_PIN_DC) || (TFT_RST != BOARD_LCD_PIN_RST)
#error "TFT_eSPI SPI pins do not match the Waveshare ESP32-S3-Touch-LCD-1.28 (MOSI 11, SCLK 10, CS 9, DC 8, RST 14)."
#endif
#if !defined(TFT_BL) || (TFT_BL != BOARD_LCD_PIN_BL)
#error "TFT_eSPI TFT_BL must be 2 for the Waveshare ESP32-S3-Touch-LCD-1.28."
#endif

// ---------------------------------------------------------------------------
// Core version guard. Arduino-ESP32 2.0.12 still reports 2.0.11 internally,
// so only the major version is checked. 3.x changed LEDC/BLE/WDT APIs.
// ---------------------------------------------------------------------------
#include <esp_arduino_version.h>
#if ESP_ARDUINO_VERSION_MAJOR != 2
#error "SafeFall AI targets ESP32 Arduino Core 2.0.x (tested API set: 2.0.12). Core 3.x is not supported."
#endif

#if !defined(CONFIG_IDF_TARGET_ESP32S3)
#error "Select an ESP32-S3 board (Tools > Board > ESP32S3 Dev Module)."
#endif
