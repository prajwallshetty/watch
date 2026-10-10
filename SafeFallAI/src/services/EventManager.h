/**
 * SafeFall AI - EventManager.h
 *
 * Safety events (falls and SOS) with unique, persistent IDs (SF-000001...).
 * The counter lives in NVS, so IDs never repeat across reboots.
 *
 * Every event sits in a small outbox until the phone acknowledges its latest
 * stage. Undelivered events are retransmitted (every BLE_EVENT_RETRY_MS while
 * connected, immediately on reconnect), which gives at-least-once delivery;
 * the phone de-duplicates by (eventId, seq). Incoming ACK/CANCEL commands are
 * de-duplicated here so a repeated write is never processed twice.
 */
#pragma once

#include <Arduino.h>
#include "../config/AppConfig.h"

enum class EventKind : uint8_t { FALL, SOS };
enum class EventStage : uint8_t { DETECTED, EMERGENCY, ACTIVE, CANCELLED, RESOLVED };

struct SafetyEvent {
    bool used;
    char id[12];               // "SF-000001"
    EventKind kind;
    EventStage stage;
    const char* trigger;       // static string, see protocol docs
    uint8_t confidence;        // 0 for SOS
    uint32_t createdEpoch;     // UTC seconds, 0 if clock not synced
    uint32_t createdUptimeS;
    uint32_t seq;              // increments on every stage change (global)
    bool delivered;            // phone ACKed this seq
    uint32_t lastTxMs;         // 0 = not yet sent for this seq
    uint16_t txCount;
};

class EventManager {
public:
    enum class AckResult : uint8_t { OK, DUPLICATE, STALE, UNKNOWN };

    void begin();

    SafetyEvent* create(EventKind kind, EventStage stage, uint8_t confidence, const char* trigger,
                        uint32_t epochUtc, uint32_t uptimeS);
    bool updateStage(const char* id, EventStage stage, const char* trigger);

    SafetyEvent* find(const char* id);
    SafetyEvent* active();                 // the open (non-terminal) event, if any
    const char* lastEventId() const { return m_lastId; }

    AckResult onAck(const char* id, uint32_t seq);
    bool isDelivered(const char* id);

    // Remote-cancel de-duplication
    bool wasRemoteCancelled(const char* id) const;
    void rememberRemoteCancel(const char* id);

    SafetyEvent* nextToSend(uint32_t nowMs);
    void markSent(SafetyEvent* ev, uint32_t nowMs);
    void onConnected();
    uint8_t pendingCount() const;
    uint32_t counter() const { return m_counter; }

    static bool isTerminal(EventStage s) { return s == EventStage::CANCELLED || s == EventStage::RESOLVED; }
    static const char* kindName(EventKind k) { return k == EventKind::FALL ? "FALL" : "SOS"; }
    static const char* stageName(EventStage s);

private:
    SafetyEvent* allocSlot();

    SafetyEvent m_outbox[EVENT_OUTBOX_SIZE];
    char m_recentCancels[EVENT_RECENT_CANCEL_SIZE][12];
    uint8_t m_cancelIdx = 0;
    uint32_t m_counter = 0;
    uint32_t m_seq = 0;
    char m_lastId[12] = "";
};

extern EventManager Events;
