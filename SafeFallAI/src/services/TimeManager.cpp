#include "TimeManager.h"
#include "../config/AppConfig.h"
#include "../config/DebugConfig.h"

#include <sys/time.h>
#include <time.h>

TimeManager Clock;

void TimeManager::begin(int32_t tzOffsetS) {
    m_tz = tzOffsetS;
    if (isSynced()) {
        LOG_TIME("Clock kept across reset (epoch %lu)", (unsigned long)epochUtc());
    } else {
        LOG_TIME("Clock not set; waiting for TIME_SYNC from phone");
    }
}

bool TimeManager::isSynced() const {
    return (uint32_t)time(nullptr) >= TIME_VALID_EPOCH_MIN;
}

uint32_t TimeManager::epochUtc() const {
    const time_t t = time(nullptr);
    return ((uint32_t)t >= TIME_VALID_EPOCH_MIN) ? (uint32_t)t : 0;
}

bool TimeManager::sync(uint32_t epochUtc, int32_t tzOffsetS) {
    if (epochUtc < TIME_VALID_EPOCH_MIN) return false;
    if (tzOffsetS < -14 * 3600 || tzOffsetS > 14 * 3600) return false;
    struct timeval tv;
    tv.tv_sec = (time_t)epochUtc;
    tv.tv_usec = 0;
    if (settimeofday(&tv, nullptr) != 0) return false;
    m_tz = tzOffsetS;
    m_lastMinute = -1;
    LOG_TIME("Synced: epoch %lu, UTC offset %ld s", (unsigned long)epochUtc, (long)tzOffsetS);
    return true;
}

bool TimeManager::formatClock(char* buf, size_t len) {
    if (!isSynced()) {
        snprintf(buf, len, "--:--");
        const bool changed = (m_lastMinute != -2);
        m_lastMinute = -2;
        return changed;
    }
    const time_t local = time(nullptr) + m_tz;
    struct tm tmv;
    gmtime_r(&local, &tmv);
#if TIME_FORMAT_24H
    snprintf(buf, len, "%02d:%02d", tmv.tm_hour, tmv.tm_min);
#else
    int h = tmv.tm_hour % 12;
    if (h == 0) h = 12;
    snprintf(buf, len, "%d:%02d", h, tmv.tm_min);
#endif
    const int minuteKey = tmv.tm_hour * 60 + tmv.tm_min;
    const bool changed = (minuteKey != m_lastMinute);
    m_lastMinute = minuteKey;
    return changed;
}

void TimeManager::formatDate(char* buf, size_t len) const {
    if (!isSynced()) {
        if (len) buf[0] = '\0';
        return;
    }
    static const char* kDays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char* kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    const time_t local = time(nullptr) + m_tz;
    struct tm tmv;
    gmtime_r(&local, &tmv);
    snprintf(buf, len, "%s %d %s", kDays[tmv.tm_wday % 7], tmv.tm_mday, kMonths[tmv.tm_mon % 12]);
}
