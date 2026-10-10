/**
 * SafeFall AI - DebugConfig.h
 *
 * Tagged serial logging. Set DEBUG_MODE to 0 for a silent production build;
 * every LOG_* macro then compiles to nothing.
 *
 * Output goes to UART0, which the board routes through its CH343P USB-UART
 * bridge. Use "USB CDC On Boot: Disabled" so Serial is that UART.
 */
#pragma once

#include <Arduino.h>

#ifndef DEBUG_MODE
#define DEBUG_MODE 1
#endif

#define DEBUG_BAUD 115200

// Optional periodic IMU summary (one line every DEBUG_IMU_PERIOD_MS). Off by
// default so the Serial Monitor is not spammed.
#define DEBUG_IMU_STREAM     0
#define DEBUG_IMU_PERIOD_MS  2000

#if DEBUG_MODE
#define SF_LOG(tag, fmt, ...) Serial.printf("[" tag "] " fmt "\r\n", ##__VA_ARGS__)
#else
#define SF_LOG(tag, fmt, ...) do { } while (0)
#endif

#define LOG_BOOT(fmt, ...)    SF_LOG("BOOT", fmt, ##__VA_ARGS__)
#define LOG_DISPLAY(fmt, ...) SF_LOG("DISPLAY", fmt, ##__VA_ARGS__)
#define LOG_TOUCH(fmt, ...)   SF_LOG("TOUCH", fmt, ##__VA_ARGS__)
#define LOG_IMU(fmt, ...)     SF_LOG("IMU", fmt, ##__VA_ARGS__)
#define LOG_BLE(fmt, ...)     SF_LOG("BLE", fmt, ##__VA_ARGS__)
#define LOG_BATTERY(fmt, ...) SF_LOG("BATTERY", fmt, ##__VA_ARGS__)
#define LOG_FALL(fmt, ...)    SF_LOG("FALL", fmt, ##__VA_ARGS__)
#define LOG_STATE(fmt, ...)   SF_LOG("STATE", fmt, ##__VA_ARGS__)
#define LOG_EVENT(fmt, ...)   SF_LOG("EVENT", fmt, ##__VA_ARGS__)
#define LOG_POWER(fmt, ...)   SF_LOG("POWER", fmt, ##__VA_ARGS__)
#define LOG_TIME(fmt, ...)    SF_LOG("TIME", fmt, ##__VA_ARGS__)
#define LOG_UI(fmt, ...)      SF_LOG("UI", fmt, ##__VA_ARGS__)
