/**
 * SafeFall AI - IMUManager.h
 *
 * QMI8658 6-axis IMU on the shared I2C bus. The register sequence mirrors the
 * Waveshare qmi8658 driver (waveshareteam/Waveshare-ESP32-components):
 *   WHO_AM_I (0x00) == 0x05
 *   CTRL1 (0x02) = 0x60            address auto-increment
 *   CTRL2 (0x03) = range<<4 | odr  accelerometer
 *   CTRL3 (0x04) = range<<4 | odr  gyroscope
 *   CTRL7 (0x08) = 0x03            enable accel + gyro
 *   data burst from AX_L (0x35), 12 bytes
 *
 * Sampling runs in a dedicated FreeRTOS task, completely independent of the
 * UI and of the display power state.
 */
#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

struct ImuSample {
    uint32_t ms;            // millis() at read time
    float ax, ay, az;       // g
    float gx, gy, gz;       // degrees / second
};

class IMUManager {
public:
    typedef void (*SampleCallback)(const ImuSample& s, void* ctx);

    bool begin();                                     // probe + configure (blocking, boot only)
    bool startTask(SampleCallback cb, void* ctx);     // 100 Hz sampling task

    bool isHealthy() const { return m_healthy; }
    bool isDetected() const { return m_detected; }
    uint8_t address() const { return m_addr; }
    bool bigEndian() const { return m_bigEndian; }
    uint32_t faultSinceMs() const { return m_faultSinceMs; }
    uint32_t recoveries() const { return m_recoveries; }
    uint32_t sampleCount() const { return m_sampleCount; }

    ImuSample latest();                               // thread-safe copy

private:
    static void taskEntry(void* arg);
    void taskLoop();

    bool init(bool detectOrder);
    bool configure();
    bool readSample(ImuSample& out, uint8_t raw[12]);
    void detectByteOrder();
    void decode(const uint8_t raw[12], bool bigEndian, ImuSample& out) const;
    void markFault(const char* reason);

    uint8_t m_addr = 0;
    volatile bool m_detected = false;
    volatile bool m_healthy = false;
    volatile bool m_bigEndian = false;
    bool m_orderKnown = false;
    volatile uint32_t m_faultSinceMs = 0;
    volatile uint32_t m_recoveries = 0;
    volatile uint32_t m_sampleCount = 0;

    uint16_t m_consecutiveErrors = 0;
    uint16_t m_identicalFrames = 0;
    uint8_t m_lastRaw[12] = {0};
    uint32_t m_lastRecoveryMs = 0;
    uint32_t m_recoveryAttempts = 0;
    uint32_t m_lastHealthCheckMs = 0;

    SampleCallback m_cb = nullptr;
    void* m_cbCtx = nullptr;
    TaskHandle_t m_task = nullptr;

    portMUX_TYPE m_mux = portMUX_INITIALIZER_UNLOCKED;
    ImuSample m_latest = {0, 0, 0, 1, 0, 0, 0};
};

extern IMUManager IMU;
