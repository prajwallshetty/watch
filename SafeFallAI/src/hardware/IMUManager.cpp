#include "IMUManager.h"
#include "I2CBus.h"
#include "../config/BoardConfig.h"
#include "../config/AppConfig.h"
#include "../config/DebugConfig.h"

#include <esp_task_wdt.h>
#include <math.h>

IMUManager IMU;

// QMI8658 registers (Waveshare qmi8658.h)
static const uint8_t REG_WHO_AM_I = 0x00;
static const uint8_t REG_CTRL1 = 0x02;
static const uint8_t REG_CTRL2 = 0x03;
static const uint8_t REG_CTRL3 = 0x04;
static const uint8_t REG_CTRL7 = 0x08;
static const uint8_t REG_AX_L = 0x35;

static const uint8_t CTRL1_ADDR_AI = 0x60;       // value written by the Waveshare driver
static const uint8_t CTRL7_ACC_GYRO_EN = 0x03;
static const uint8_t CTRL1_SOFT_RESET = 0x80;    // qmi8658_reset() in the Waveshare driver

bool IMUManager::begin() {
    return init(true);
}

bool IMUManager::init(bool detectOrder) {
    m_detected = false;
    const uint8_t candidates[2] = {BOARD_IMU_I2C_ADDR, BOARD_IMU_I2C_ADDR_ALT};
    for (uint8_t i = 0; i < 2 && !m_detected; i++) {
        uint8_t who = 0;
        if (I2CBus::readRegs(candidates[i], REG_WHO_AM_I, &who, 1) && who == BOARD_IMU_WHO_AM_I) {
            m_addr = candidates[i];
            m_detected = true;
        }
    }
    if (!m_detected) {
        if (m_recoveryAttempts <= 1 || (m_recoveryAttempts % 15) == 0) {  // don't spam during recovery
            LOG_IMU("ERROR: QMI8658 not found (WHO_AM_I != 0x%02X at 0x6B/0x6A)", BOARD_IMU_WHO_AM_I);
        }
        markFault("not detected");
        return false;
    }
    LOG_IMU("QMI8658 detected at 0x%02X", m_addr);

    if (!configure()) {
        markFault("configuration failed");
        return false;
    }
    delay(30);  // first conversion at 125 Hz
    // If the IMU was absent at boot, the order was never measured: do it now.
    if (detectOrder || !m_orderKnown) detectByteOrder();

    ImuSample s;
    uint8_t raw[12];
    if (!readSample(s, raw)) {
        markFault("first read failed");
        return false;
    }
    memcpy(m_lastRaw, raw, sizeof(m_lastRaw));
    portENTER_CRITICAL(&m_mux);
    m_latest = s;
    portEXIT_CRITICAL(&m_mux);

    m_healthy = true;
    m_faultSinceMs = 0;
    LOG_IMU("Accel +-8g, gyro +-512dps, 125 Hz ODR, |a| = %.2f g", sqrtf(s.ax * s.ax + s.ay * s.ay + s.az * s.az));
    return true;
}

bool IMUManager::configure() {
    bool ok = true;
    ok &= I2CBus::writeReg(m_addr, REG_CTRL1, CTRL1_ADDR_AI);
    ok &= I2CBus::writeReg(m_addr, REG_CTRL2, (uint8_t)((IMU_ACCEL_RANGE_REG << 4) | IMU_ACCEL_ODR_REG));
    ok &= I2CBus::writeReg(m_addr, REG_CTRL3, (uint8_t)((IMU_GYRO_RANGE_REG << 4) | IMU_GYRO_ODR_REG));
    ok &= I2CBus::writeReg(m_addr, REG_CTRL7, CTRL7_ACC_GYRO_EN);
    if (!ok) {
        LOG_IMU("ERROR: register configuration failed");
        return false;
    }
    // Read back the enable register to make sure the writes landed.
    uint8_t ctrl7 = 0;
    if (!I2CBus::readRegs(m_addr, REG_CTRL7, &ctrl7, 1) || (ctrl7 & 0x03) != CTRL7_ACC_GYRO_EN) {
        LOG_IMU("ERROR: CTRL7 read-back mismatch (0x%02X)", ctrl7);
        return false;
    }
    return true;
}

void IMUManager::decode(const uint8_t raw[12], bool bigEndian, ImuSample& out) const {
    int16_t v[6];
    for (int i = 0; i < 6; i++) {
        const uint8_t lo = bigEndian ? raw[2 * i + 1] : raw[2 * i];
        const uint8_t hi = bigEndian ? raw[2 * i] : raw[2 * i + 1];
        v[i] = (int16_t)((uint16_t)hi << 8 | lo);
    }
    out.ax = v[0] / IMU_ACCEL_LSB_PER_G;
    out.ay = v[1] / IMU_ACCEL_LSB_PER_G;
    out.az = v[2] / IMU_ACCEL_LSB_PER_G;
    out.gx = v[3] / IMU_GYRO_LSB_PER_DPS;
    out.gy = v[4] / IMU_GYRO_LSB_PER_DPS;
    out.gz = v[5] / IMU_GYRO_LSB_PER_DPS;
}

// The Waveshare driver writes CTRL1 = 0x60 and decodes little-endian. Bit 5
// of CTRL1 is documented as the big-endian flag in some QMI8658 datasheet
// revisions, so instead of assuming, measure: at boot the accelerometer must
// read about 1 g. Whichever byte order gives |a| closest to 1 g wins.
void IMUManager::detectByteOrder() {
    float errLE = 0.0f, errBE = 0.0f;
    int good = 0;
    for (int i = 0; i < 8; i++) {
        uint8_t raw[12];
        if (I2CBus::readRegs(m_addr, REG_AX_L, raw, sizeof(raw))) {
            ImuSample le, be;
            decode(raw, false, le);
            decode(raw, true, be);
            errLE += fabsf(sqrtf(le.ax * le.ax + le.ay * le.ay + le.az * le.az) - 1.0f);
            errBE += fabsf(sqrtf(be.ax * be.ax + be.ay * be.ay + be.az * be.az) - 1.0f);
            good++;
        }
        delay(10);
    }
    if (good == 0) {
        m_bigEndian = false;
        return;
    }
    m_bigEndian = errBE < errLE;
    m_orderKnown = true;
    LOG_IMU("Data byte order: %s (|a|-1g error LE %.2f / BE %.2f)", m_bigEndian ? "big-endian" : "little-endian",
            errLE / good, errBE / good);
}

bool IMUManager::readSample(ImuSample& out, uint8_t raw[12]) {
    if (!I2CBus::readRegs(m_addr, REG_AX_L, raw, 12)) return false;
    decode(raw, m_bigEndian, out);
    out.ms = millis();
    return true;
}

void IMUManager::markFault(const char* reason) {
    // Log only the healthy -> fault transition, not every failed recovery.
    if (m_healthy || m_faultSinceMs == 0) {
        m_faultSinceMs = millis();
        if (m_faultSinceMs == 0) m_faultSinceMs = 1;
        LOG_IMU("FAULT: %s", reason);
    }
    m_healthy = false;
}

ImuSample IMUManager::latest() {
    ImuSample copy;
    portENTER_CRITICAL(&m_mux);
    copy = m_latest;
    portEXIT_CRITICAL(&m_mux);
    return copy;
}

bool IMUManager::startTask(SampleCallback cb, void* ctx) {
    m_cb = cb;
    m_cbCtx = ctx;
    BaseType_t r = xTaskCreatePinnedToCore(taskEntry, "imu", IMU_TASK_STACK, this, IMU_TASK_PRIORITY, &m_task,
                                           IMU_TASK_CORE);
    if (r != pdPASS) {
        LOG_IMU("ERROR: could not start sampling task");
        return false;
    }
    return true;
}

void IMUManager::taskEntry(void* arg) {
    static_cast<IMUManager*>(arg)->taskLoop();
}

void IMUManager::taskLoop() {
    esp_task_wdt_add(nullptr);
    TickType_t lastWake = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(IMU_SAMPLE_PERIOD_MS);
#if DEBUG_IMU_STREAM
    uint32_t lastDebugMs = 0;
#endif

    for (;;) {
        vTaskDelayUntil(&lastWake, period);
        esp_task_wdt_reset();
        const uint32_t now = millis();

        if (!m_healthy) {
            if (now - m_lastRecoveryMs >= IMU_RECOVERY_INTERVAL_MS) {
                m_lastRecoveryMs = now;
                m_recoveryAttempts++;
                if (m_recoveryAttempts == 1 || (m_recoveryAttempts % 15) == 0) {
                    LOG_IMU("Recovery attempt %lu", (unsigned long)m_recoveryAttempts);
                }
                I2CBus::writeReg(m_addr ? m_addr : BOARD_IMU_I2C_ADDR, REG_CTRL1, CTRL1_SOFT_RESET);
                vTaskDelay(pdMS_TO_TICKS(20));
                // Keep the byte order measured at boot: the wearer may be moving now.
                if (!init(false)) {
                    I2CBus::recoverBus();
                } else {
                    m_recoveries++;
                    m_recoveryAttempts = 0;
                    m_consecutiveErrors = 0;
                    m_identicalFrames = 0;
                    LOG_IMU("Recovered");
                }
                lastWake = xTaskGetTickCount();
            }
            continue;
        }

        ImuSample s;
        uint8_t raw[12];
        if (!readSample(s, raw)) {
            if (++m_consecutiveErrors >= IMU_MAX_CONSECUTIVE_ERRORS) markFault("consecutive read errors");
            continue;
        }
        m_consecutiveErrors = 0;

        // A healthy MEMS sensor always has LSB noise; a frozen frame means the
        // sensor stopped converting.
        if (memcmp(raw, m_lastRaw, sizeof(m_lastRaw)) == 0) {
            if (++m_identicalFrames >= IMU_STUCK_SAMPLE_COUNT) {
                m_identicalFrames = 0;
                markFault("data frozen");
                continue;
            }
        } else {
            m_identicalFrames = 0;
            memcpy(m_lastRaw, raw, sizeof(m_lastRaw));
        }

        if (now - m_lastHealthCheckMs >= IMU_HEALTH_CHECK_MS) {
            m_lastHealthCheckMs = now;
            uint8_t who = 0;
            if (!I2CBus::readRegs(m_addr, REG_WHO_AM_I, &who, 1) || who != BOARD_IMU_WHO_AM_I) {
                markFault("WHO_AM_I check failed");
                continue;
            }
        }

        portENTER_CRITICAL(&m_mux);
        m_latest = s;
        portEXIT_CRITICAL(&m_mux);
        m_sampleCount++;

        if (m_cb) m_cb(s, m_cbCtx);

#if DEBUG_IMU_STREAM
        if (now - lastDebugMs >= DEBUG_IMU_PERIOD_MS) {
            lastDebugMs = now;
            LOG_IMU("a=(%.2f %.2f %.2f)g g=(%.0f %.0f %.0f)dps", s.ax, s.ay, s.az, s.gx, s.gy, s.gz);
        }
#endif
    }
}
