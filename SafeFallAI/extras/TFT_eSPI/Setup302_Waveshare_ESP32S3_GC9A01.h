// TFT_eSPI setup for the Waveshare ESP32-S3-Touch-LCD-1.28 (GC9A01 240x240).
//
// This is Waveshare's own setup, submitted upstream as
// User_Setups/Setup302_Waveshare_ESP32S3_GC9A01.h (Bodmer/TFT_eSPI PR #2844)
// and reproduced unchanged except for these comments. TFT_eSPI 2.5.34 predates
// it, so it is shipped here.
//
// INSTALL (TFT_eSPI 2.5.34):
//   1. Copy this file to   Arduino/libraries/TFT_eSPI/User_Setups/
//   2. Edit                Arduino/libraries/TFT_eSPI/User_Setup_Select.h
//        - comment out:    #include <User_Setup.h>
//        - add:            #include <User_Setups/Setup302_Waveshare_ESP32S3_GC9A01.h>
// If you already use the TFT_eSPI_Setups folder from the Waveshare demo
// package and the LVGL_Arduino example works, keep it: SafeFall AI verifies
// the pins at compile time (src/config/BoardConfig.h) either way.

#define USER_SETUP_ID 302

#define GC9A01_DRIVER

#define TFT_MISO 12
#define TFT_MOSI 11
#define TFT_SCLK 10
#define TFT_CS    9  // Chip select control pin
#define TFT_DC    8  // Data Command control pin
#define TFT_RST   14  // Reset pin (could connect to RST pin)
#define TFT_BL 2
#define TFT_BACKLIGHT_ON HIGH

#define LOAD_GLCD   // Font 1. Original Adafruit 8 pixel font needs ~1820 bytes in FLASH
#define LOAD_FONT2  // Font 2. Small 16 pixel high font, needs ~3534 bytes in FLASH, 96 characters
#define LOAD_FONT4  // Font 4. Medium 26 pixel high font, needs ~5848 bytes in FLASH, 96 characters
#define LOAD_FONT6  // Font 6. Large 48 pixel font, needs ~2666 bytes in FLASH, only characters 1234567890:-.apm
#define LOAD_FONT7  // Font 7. 7 segment 48 pixel font, needs ~2438 bytes in FLASH, only characters 1234567890:.
#define LOAD_FONT8  // Font 8. Large 75 pixel font needs ~3256 bytes in FLASH, only characters 1234567890:-.
#define LOAD_GFXFF  // FreeFonts. Include access to the 48 Adafruit_GFX free fonts FF1 to FF48 and custom fonts
#define SMOOTH_FONT

#define TFT_WIDTH 240
#define TFT_HEIGHT 240

// Note: the later upstream copy adds "#define USE_HSPI_PORT" for ESP32 core
// >= 3.0. It is not needed on core 2.0.12 and is intentionally not set here.

#define SPI_FREQUENCY  40000000

#define SPI_READ_FREQUENCY  20000000

#define SPI_TOUCH_FREQUENCY  2500000

// #define SUPPORT_TRANSACTIONS
