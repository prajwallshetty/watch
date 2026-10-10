#include "I2CBus.h"
#include "../config/BoardConfig.h"

SemaphoreHandle_t I2CBus::s_mutex = nullptr;

bool I2CBus::begin() {
    if (s_mutex == nullptr) {
        s_mutex = xSemaphoreCreateMutex();
        if (s_mutex == nullptr) return false;
    }
    // CST816S::begin() also calls Wire.begin(6, 7); calling it here first with
    // the same pins is harmless and lets us set the bus frequency explicitly.
    return Wire.begin(BOARD_I2C_SDA, BOARD_I2C_SCL, BOARD_I2C_FREQ_HZ);
}

bool I2CBus::lock(uint32_t timeoutMs) {
    if (s_mutex == nullptr) return true;  // before begin(): single-threaded boot
    return xSemaphoreTake(s_mutex, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

void I2CBus::unlock() {
    if (s_mutex != nullptr) xSemaphoreGive(s_mutex);
}

bool I2CBus::probe(uint8_t addr) {
    I2CLock guard;
    if (!guard.ok()) return false;
    Wire.beginTransmission(addr);
    return Wire.endTransmission(true) == 0;
}

bool I2CBus::readRegs(uint8_t addr, uint8_t reg, uint8_t* buf, size_t len) {
    I2CLock guard;
    if (!guard.ok()) return false;
    Wire.beginTransmission(addr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    size_t got = Wire.requestFrom(addr, len, true);
    if (got != len) {
        while (Wire.available()) Wire.read();
        return false;
    }
    for (size_t i = 0; i < len; i++) buf[i] = (uint8_t)Wire.read();
    return true;
}

bool I2CBus::writeReg(uint8_t addr, uint8_t reg, uint8_t value) {
    I2CLock guard;
    if (!guard.ok()) return false;
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission(true) == 0;
}

bool I2CBus::recoverBus() {
    I2CLock guard(200);
    if (!guard.ok()) return false;
    // Re-create the driver; clears a peripheral stuck mid-transaction.
    Wire.end();
    delay(2);
    return Wire.begin(BOARD_I2C_SDA, BOARD_I2C_SCL, BOARD_I2C_FREQ_HZ);
}
