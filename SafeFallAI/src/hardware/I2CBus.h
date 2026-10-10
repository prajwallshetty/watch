/**
 * SafeFall AI - I2CBus.h
 *
 * The CST816S touch controller and the QMI8658 IMU share one I2C bus
 * (SDA 6 / SCL 7). Touch is read from the LVGL thread (loop task) and the IMU
 * from its own FreeRTOS task, so every multi-step transaction is serialised
 * through this mutex (priority inheritance avoids inversion).
 */
#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class I2CBus {
public:
    static bool begin();
    static bool lock(uint32_t timeoutMs = 50);
    static void unlock();
    static bool probe(uint8_t addr);
    static bool readRegs(uint8_t addr, uint8_t reg, uint8_t* buf, size_t len);
    static bool writeReg(uint8_t addr, uint8_t reg, uint8_t value);
    static bool recoverBus();

private:
    static SemaphoreHandle_t s_mutex;
};

// RAII guard. Check ok() before touching the bus.
class I2CLock {
public:
    explicit I2CLock(uint32_t timeoutMs = 50) : m_ok(I2CBus::lock(timeoutMs)) {}
    ~I2CLock() { if (m_ok) I2CBus::unlock(); }
    bool ok() const { return m_ok; }
private:
    bool m_ok;
    I2CLock(const I2CLock&) = delete;
    I2CLock& operator=(const I2CLock&) = delete;
};
