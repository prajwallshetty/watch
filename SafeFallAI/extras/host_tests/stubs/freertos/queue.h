#pragma once
// Minimal FIFO queue with FreeRTOS semantics (copy-in / copy-out).
#include "FreeRTOS.h"
#include <cstring>
#include <deque>
#include <vector>
struct HostQueue {
    size_t itemSize, cap;
    std::deque<std::vector<uint8_t>> q;
};
typedef HostQueue* QueueHandle_t;
inline QueueHandle_t xQueueCreate(size_t len, size_t itemSize) { return new HostQueue{itemSize, len, {}}; }
inline BaseType_t xQueueSend(QueueHandle_t h, const void* item, TickType_t) {
    if (h->q.size() >= h->cap) return pdFALSE;
    const uint8_t* p = static_cast<const uint8_t*>(item);
    h->q.emplace_back(p, p + h->itemSize);
    return pdTRUE;
}
inline BaseType_t xQueueReceive(QueueHandle_t h, void* out, TickType_t) {
    if (h->q.empty()) return pdFALSE;
    memcpy(out, h->q.front().data(), h->itemSize);
    h->q.pop_front();
    return pdTRUE;
}
