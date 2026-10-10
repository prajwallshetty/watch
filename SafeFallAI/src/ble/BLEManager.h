/**
 * SafeFall AI - BLEManager.h
 *
 * BLE GATT server (ESP32 Arduino 2.0.x built-in Bluedroid "BLE" library).
 * Advertises as "SafeFall-AI" with the SafeFall service UUID, accepts one
 * phone connection, and recovers advertising automatically.
 *
 * Threading: GATT callbacks run in the Bluetooth task; they only copy writes
 * into a FreeRTOS queue. Everything else (parsing, notifying) happens in the
 * application loop.
 */
#pragma once

#include <Arduino.h>
#include "BLEProtocol.h"

class BLEManager {
public:
    bool begin();
    void update(uint32_t nowMs);

    bool isReady() const { return m_ready; }
    bool isConnected() const { return m_connected; }
    bool consumeConnectedEdge();          // true once after each new connection
    uint16_t peerMtu() const { return m_mtu; }

    bool pollCommand(BLEProtocol::Command& out, bool& malformed);

    bool notifyStatus(const char* json, size_t len);
    bool notifyBattery(const char* json, size_t len, uint8_t pct);
    bool notifyFall(const char* json, size_t len);
    bool notifySos(const char* json, size_t len);
    void setConfigValue(const char* json, size_t len);
    void setTimeValue(const char* json, size_t len);

    // Called from GATT callbacks (Bluetooth task)
    void onConnect(uint16_t connId, uint16_t mtu);
    void onDisconnect();
    void onMtu(uint16_t mtu) { m_mtu = mtu; }
    void onWrite(const uint8_t* data, size_t len);

private:
    void startAdvertising(uint32_t nowMs);

    volatile bool m_ready = false;
    volatile bool m_connected = false;
    volatile bool m_connectedEdge = false;
    volatile bool m_restartAdvPending = false;
    volatile uint32_t m_disconnectMs = 0;
    volatile uint16_t m_mtu = 23;
    uint32_t m_lastAdvStartMs = 0;
    uint32_t m_lastConnCheckMs = 0;
};

extern BLEManager Ble;
