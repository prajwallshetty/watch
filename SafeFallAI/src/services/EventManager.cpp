#include "EventManager.h"
#include "SettingsStore.h"
#include "../config/DebugConfig.h"

EventManager Events;

void EventManager::begin() {
    memset(m_outbox, 0, sizeof(m_outbox));
    memset(m_recentCancels, 0, sizeof(m_recentCancels));
    m_counter = Settings.loadEventCounter();
    LOG_EVENT("Event counter at %lu (next ID " EVENT_ID_PREFIX "%06lu)", (unsigned long)m_counter,
              (unsigned long)(m_counter + 1));
}

const char* EventManager::stageName(EventStage s) {
    switch (s) {
        case EventStage::DETECTED:  return "DETECTED";
        case EventStage::EMERGENCY: return "EMERGENCY";
        case EventStage::ACTIVE:    return "ACTIVE";
        case EventStage::CANCELLED: return "CANCELLED";
        case EventStage::RESOLVED:  return "RESOLVED";
    }
    return "?";
}

SafetyEvent* EventManager::allocSlot() {
    // 1) free slot  2) oldest delivered + terminal  3) oldest delivered  4) oldest overall
    SafetyEvent* best = nullptr;
    for (auto& e : m_outbox) if (!e.used) return &e;
    for (auto& e : m_outbox) {
        if (e.delivered && isTerminal(e.stage) && (best == nullptr || e.seq < best->seq)) best = &e;
    }
    if (best) return best;
    for (auto& e : m_outbox) {
        if (e.delivered && (best == nullptr || e.seq < best->seq)) best = &e;
    }
    if (best) return best;
    for (auto& e : m_outbox) {
        if (best == nullptr || e.seq < best->seq) best = &e;
    }
    LOG_EVENT("WARNING: outbox full, dropping undelivered %s", best->id);
    return best;
}

SafetyEvent* EventManager::create(EventKind kind, EventStage stage, uint8_t confidence, const char* trigger,
                                  uint32_t epochUtc, uint32_t uptimeS) {
    SafetyEvent* e = allocSlot();
    m_counter++;
    Settings.saveEventCounter(m_counter);  // persist before the ID is ever transmitted

    memset(e, 0, sizeof(*e));
    e->used = true;
    snprintf(e->id, sizeof(e->id), EVENT_ID_PREFIX "%06lu", (unsigned long)(m_counter % 1000000UL));
    e->kind = kind;
    e->stage = stage;
    e->trigger = trigger;
    e->confidence = confidence;
    e->createdEpoch = epochUtc;
    e->createdUptimeS = uptimeS;
    e->seq = ++m_seq;
    e->delivered = false;
    e->lastTxMs = 0;
    strncpy(m_lastId, e->id, sizeof(m_lastId) - 1);
    LOG_EVENT("%s %s created (%s, trigger %s, confidence %u)", kindName(kind), e->id, stageName(stage), trigger,
              confidence);
    return e;
}

SafetyEvent* EventManager::find(const char* id) {
    if (id == nullptr || id[0] == '\0') return nullptr;
    for (auto& e : m_outbox) {
        if (e.used && strncmp(e.id, id, sizeof(e.id)) == 0) return &e;
    }
    return nullptr;
}

SafetyEvent* EventManager::active() {
    SafetyEvent* best = nullptr;
    for (auto& e : m_outbox) {
        if (e.used && !isTerminal(e.stage) && (best == nullptr || e.seq > best->seq)) best = &e;
    }
    return best;
}

bool EventManager::updateStage(const char* id, EventStage stage, const char* trigger) {
    SafetyEvent* e = find(id);
    if (e == nullptr || e->stage == stage) return false;
    e->stage = stage;
    e->trigger = trigger;
    e->seq = ++m_seq;
    e->delivered = false;
    e->lastTxMs = 0;
    e->txCount = 0;
    LOG_EVENT("%s %s -> %s (%s)", kindName(e->kind), e->id, stageName(stage), trigger);
    return true;
}

EventManager::AckResult EventManager::onAck(const char* id, uint32_t seq) {
    SafetyEvent* e = find(id);
    if (e == nullptr) return AckResult::UNKNOWN;
    if (seq != 0 && seq != e->seq) return AckResult::STALE;  // ACK for an older stage
    if (e->delivered) return AckResult::DUPLICATE;
    e->delivered = true;
    LOG_EVENT("%s %s acknowledged by phone (seq %lu, %u transmissions)", kindName(e->kind), e->id,
              (unsigned long)e->seq, e->txCount);
    return AckResult::OK;
}

bool EventManager::isDelivered(const char* id) {
    SafetyEvent* e = find(id);
    return e != nullptr && e->delivered;
}

bool EventManager::wasRemoteCancelled(const char* id) const {
    for (uint8_t i = 0; i < EVENT_RECENT_CANCEL_SIZE; i++) {
        if (m_recentCancels[i][0] != '\0' && strncmp(m_recentCancels[i], id, 12) == 0) return true;
    }
    return false;
}

void EventManager::rememberRemoteCancel(const char* id) {
    strncpy(m_recentCancels[m_cancelIdx], id, 11);
    m_recentCancels[m_cancelIdx][11] = '\0';
    m_cancelIdx = (uint8_t)((m_cancelIdx + 1) % EVENT_RECENT_CANCEL_SIZE);
}

SafetyEvent* EventManager::nextToSend(uint32_t nowMs) {
    SafetyEvent* best = nullptr;
    for (auto& e : m_outbox) {
        if (!e.used || e.delivered) continue;
        if (e.lastTxMs != 0 && (nowMs - e.lastTxMs) < BLE_EVENT_RETRY_MS) continue;
        if (best == nullptr || e.seq < best->seq) best = &e;  // oldest first
    }
    return best;
}

void EventManager::markSent(SafetyEvent* ev, uint32_t nowMs) {
    if (ev == nullptr) return;
    ev->lastTxMs = nowMs ? nowMs : 1;
    if (ev->txCount < 0xFFFF) ev->txCount++;
}

void EventManager::onConnected() {
    for (auto& e : m_outbox) {
        if (e.used && !e.delivered) e.lastTxMs = 0;
    }
}

uint8_t EventManager::pendingCount() const {
    uint8_t n = 0;
    for (const auto& e : m_outbox) {
        if (e.used && !e.delivered) n++;
    }
    return n;
}
