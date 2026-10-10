#include "BLEManager.h"
#include "../config/AppConfig.h"
#include "../config/DebugConfig.h"

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

BLEManager Ble;

namespace {

struct RxItem {
    uint16_t len;
    char data[BLE_MAX_PACKET_LEN + 1];
};

QueueHandle_t s_rxQueue = nullptr;
BLEServer* s_server = nullptr;
BLECharacteristic* s_chStatus = nullptr;
BLECharacteristic* s_chBattery = nullptr;
BLECharacteristic* s_chFall = nullptr;
BLECharacteristic* s_chSos = nullptr;
BLECharacteristic* s_chAck = nullptr;
BLECharacteristic* s_chConfig = nullptr;
BLECharacteristic* s_chTime = nullptr;
BLECharacteristic* s_chBas = nullptr;   // standard Battery Level 0x2A19

class ServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* server, esp_ble_gatts_cb_param_t* param) override {
        const uint16_t connId = param->connect.conn_id;
        Ble.onConnect(connId, server->getPeerMTU(connId));
    }
    void onDisconnect(BLEServer* /*server*/) override {
        Ble.onDisconnect();
    }
    void onMtuChanged(BLEServer* /*server*/, esp_ble_gatts_cb_param_t* param) override {
        Ble.onMtu(param->mtu.mtu);
    }
};

class WriteCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* ch) override {
        std::string v = ch->getValue();  // std::string in core 2.0.x
        Ble.onWrite((const uint8_t*)v.data(), v.length());
    }
};

ServerCallbacks s_serverCb;
WriteCallbacks s_writeCb;

BLECharacteristic* makeNotify(BLEService* svc, const char* uuid) {
    BLECharacteristic* c = svc->createCharacteristic(
        uuid, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
    c->addDescriptor(new BLE2902());  // CCCD; allocated once for the device lifetime
    return c;
}

bool notifyChar(BLECharacteristic* c, const char* json, size_t len) {
    if (c == nullptr || len == 0) return false;
    c->setValue((uint8_t*)json, len);
    if (!Ble.isConnected()) return false;  // value stays readable for the next connection
    // The MTU is only known after the phone's MTU exchange, so check at send time.
    static uint32_t lastWarnMs = 0;
    if (len + 3 > Ble.peerMtu() && (lastWarnMs == 0 || millis() - lastWarnMs > 30000)) {
        lastWarnMs = millis();
        LOG_BLE("WARNING: %u-byte packet exceeds MTU %u; phone must request MTU >= %u", (unsigned)len,
                Ble.peerMtu(), BLE_MAX_PACKET_LEN + 3);
    }
    c->notify();
    return true;
}

}  // namespace

bool BLEManager::begin() {
    if (s_rxQueue == nullptr) s_rxQueue = xQueueCreate(BLE_RX_QUEUE_LEN, sizeof(RxItem));
    if (s_rxQueue == nullptr) {
        LOG_BLE("ERROR: RX queue allocation failed");
        return false;
    }

    BLEDevice::init(BLE_DEVICE_NAME);
    if (!BLEDevice::getInitialized()) {
        LOG_BLE("ERROR: Bluetooth controller init failed");
        return false;
    }
    BLEDevice::setMTU(BLE_PREFERRED_MTU);

    s_server = BLEDevice::createServer();
    s_server->setCallbacks(&s_serverCb);

    BLEService* svc = s_server->createService(BLEUUID(BLEProtocol::SERVICE_UUID), 40, 0);
    s_chStatus = makeNotify(svc, BLEProtocol::CHAR_STATUS);
    s_chBattery = makeNotify(svc, BLEProtocol::CHAR_BATTERY);
    s_chFall = makeNotify(svc, BLEProtocol::CHAR_FALL);
    s_chSos = makeNotify(svc, BLEProtocol::CHAR_SOS);

    s_chAck = svc->createCharacteristic(BLEProtocol::CHAR_ACK, BLECharacteristic::PROPERTY_WRITE |
                                                                   BLECharacteristic::PROPERTY_WRITE_NR);
    s_chAck->setCallbacks(&s_writeCb);
    s_chConfig = svc->createCharacteristic(BLEProtocol::CHAR_CONFIG, BLECharacteristic::PROPERTY_READ |
                                                                         BLECharacteristic::PROPERTY_WRITE);
    s_chConfig->setCallbacks(&s_writeCb);
    s_chTime = svc->createCharacteristic(BLEProtocol::CHAR_TIME, BLECharacteristic::PROPERTY_READ |
                                                                     BLECharacteristic::PROPERTY_WRITE);
    s_chTime->setCallbacks(&s_writeCb);
    svc->start();

    // Standard Battery Service so phones/OS tools show the level natively.
    BLEService* bas = s_server->createService(BLEUUID((uint16_t)0x180F));
    s_chBas = bas->createCharacteristic(BLEUUID((uint16_t)0x2A19),
                                        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
    s_chBas->addDescriptor(new BLE2902());
    uint8_t lvl = 0;
    s_chBas->setValue(&lvl, 1);
    bas->start();

    // Advertising: flags + 128-bit service UUID (21 bytes) in the advert so
    // phones can filter by service; the name goes in the scan response.
    BLEAdvertising* adv = BLEDevice::getAdvertising();
    BLEAdvertisementData advData;
    advData.setFlags(ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT);
    advData.setCompleteServices(BLEUUID(BLEProtocol::SERVICE_UUID));
    BLEAdvertisementData scanData;
    scanData.setName(BLE_DEVICE_NAME);
    adv->setAdvertisementData(advData);
    adv->setScanResponseData(scanData);
    adv->setMinPreferred(0x06);  // connection-interval hints that help iOS
    adv->setMaxPreferred(0x12);

    m_ready = true;
    startAdvertising(millis());
    return true;
}

void BLEManager::startAdvertising(uint32_t nowMs) {
    BLEDevice::startAdvertising();
    m_lastAdvStartMs = nowMs;
    m_restartAdvPending = false;
    LOG_BLE("Advertising %s", BLE_DEVICE_NAME);
}

void BLEManager::onConnect(uint16_t connId, uint16_t mtu) {
    m_connected = true;
    m_connectedEdge = true;
    m_mtu = mtu;
    (void)connId;
}

void BLEManager::onDisconnect() {
    m_connected = false;
    m_disconnectMs = millis();
    m_restartAdvPending = true;
}

void BLEManager::onWrite(const uint8_t* data, size_t len) {
    if (s_rxQueue == nullptr || len == 0) return;
    RxItem item;
    if (len > BLE_MAX_PACKET_LEN) len = BLE_MAX_PACKET_LEN;  // parse will reject a truncated object
    memcpy(item.data, data, len);
    item.data[len] = '\0';
    item.len = (uint16_t)len;
    xQueueSend(s_rxQueue, &item, 0);  // drop if the loop is hopelessly behind
}

bool BLEManager::consumeConnectedEdge() {
    if (!m_connectedEdge) return false;
    m_connectedEdge = false;
    LOG_BLE("Phone connected");
    return true;
}

void BLEManager::update(uint32_t nowMs) {
    if (!m_ready) return;

    // Restart advertising shortly after a disconnect.
    if (m_restartAdvPending && !m_connected && (nowMs - m_disconnectMs) >= BLE_ADV_RESTART_DELAY_MS) {
        LOG_BLE("Phone disconnected");
        startAdvertising(nowMs);
    }

    // Recovery: catch a missed disconnect callback, and periodically re-arm
    // advertising while nobody is connected.
    if (nowMs - m_lastConnCheckMs >= 5000) {
        m_lastConnCheckMs = nowMs;
        if (m_connected && s_server->getConnectedCount() == 0) {
            LOG_BLE("Connection lost without callback; recovering");
            onDisconnect();
        } else if (!m_connected && !m_restartAdvPending && (nowMs - m_lastAdvStartMs) >= BLE_ADV_WATCHDOG_MS) {
            BLEDevice::getAdvertising()->stop();
            startAdvertising(nowMs);
        }
    }
}

bool BLEManager::pollCommand(BLEProtocol::Command& out, bool& malformed) {
    malformed = false;
    if (s_rxQueue == nullptr) return false;
    RxItem item;
    if (xQueueReceive(s_rxQueue, &item, 0) != pdTRUE) return false;
    if (!BLEProtocol::parseCommand(item.data, item.len, out)) {
        malformed = true;
        LOG_BLE("Rejected malformed command: %.60s", item.data);
    }
    return true;
}

bool BLEManager::notifyStatus(const char* json, size_t len) { return notifyChar(s_chStatus, json, len); }
bool BLEManager::notifyFall(const char* json, size_t len) { return notifyChar(s_chFall, json, len); }
bool BLEManager::notifySos(const char* json, size_t len) { return notifyChar(s_chSos, json, len); }

bool BLEManager::notifyBattery(const char* json, size_t len, uint8_t pct) {
    if (s_chBas) {
        s_chBas->setValue(&pct, 1);
        if (m_connected) s_chBas->notify();
    }
    return notifyChar(s_chBattery, json, len);
}

void BLEManager::setConfigValue(const char* json, size_t len) {
    if (s_chConfig && len) s_chConfig->setValue((uint8_t*)json, len);
}

void BLEManager::setTimeValue(const char* json, size_t len) {
    if (s_chTime && len) s_chTime->setValue((uint8_t*)json, len);
}
