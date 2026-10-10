#include "FallDetector.h"
#include "../config/AppConfig.h"
#include "../config/DebugConfig.h"

#include <math.h>

FallDetector Detector;

static const uint32_t BUCKET_MS = 100;
static const float GRAVITY_LP_ALPHA = 0.10f;   // ~100 ms time constant at 100 Hz
static const float MOTION_EMA_ALPHA = 0.05f;   // ~2 s window at 100 Hz

static inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

bool FallDetector::begin() {
    if (m_queue == nullptr) m_queue = xQueueCreate(8, sizeof(Event));
    if (m_queue == nullptr) return false;
    for (uint8_t i = 0; i < HISTORY_LEN; i++) {
        m_bucketGravity[i] = {0, 0, 1};
        m_bucketGyroMax[i] = 0;
    }
    for (uint8_t i = 0; i < IMPACT_HISTORY; i++) m_impactTimes[i] = 0;
    m_phase = m_enabledReq ? Phase::MONITORING : Phase::PAUSED;
    m_enabled = m_enabledReq;
    LOG_FALL("Detector ready: impact %.2fg, confidence >= %d, inactivity %d ms", impactThreshold(),
             confidenceThreshold(), FALL_INACTIVITY_WINDOW_MS);
    return true;
}

float FallDetector::impactThreshold() const {
    switch (m_sensitivity) {
        case Sensitivity::LOW_SENS:  return FALL_IMPACT_G * FALL_SENSITIVITY_LOW_IMPACT_MUL;
        case Sensitivity::HIGH_SENS: return FALL_IMPACT_G * FALL_SENSITIVITY_HIGH_IMPACT_MUL;
        default:                     return FALL_IMPACT_G;
    }
}

int FallDetector::confidenceThreshold() const {
    switch (m_sensitivity) {
        case Sensitivity::LOW_SENS:  return FALL_CONFIDENCE_THRESHOLD + FALL_SENSITIVITY_CONF_STEP;
        case Sensitivity::HIGH_SENS: return FALL_CONFIDENCE_THRESHOLD - FALL_SENSITIVITY_CONF_STEP;
        default:                     return FALL_CONFIDENCE_THRESHOLD;
    }
}

FallDetector::Vec3 FallDetector::normalize(const Vec3& v) {
    const float n = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
    if (n < 1e-6f) return {0, 0, 1};
    return {v.x / n, v.y / n, v.z / n};
}

float FallDetector::angleDeg(const Vec3& a, const Vec3& b) {
    const Vec3 na = normalize(a);
    const Vec3 nb = normalize(b);
    float d = na.x * nb.x + na.y * nb.y + na.z * nb.z;
    if (d > 1.0f) d = 1.0f;
    if (d < -1.0f) d = -1.0f;
    return acosf(d) * 57.29578f;
}

uint8_t FallDetector::countRecentImpacts(uint32_t now) const {
    uint8_t n = 0;
    for (uint8_t i = 0; i < IMPACT_HISTORY; i++) {
        if (m_impactTimes[i] != 0 && (now - m_impactTimes[i]) <= FALL_REPETITIVE_WINDOW_MS) n++;
    }
    return n;
}

void FallDetector::updateMotionLevel(float mag) {
    const float diff = mag - m_magMean;
    m_magMean += MOTION_EMA_ALPHA * diff;
    m_magVar = (1.0f - MOTION_EMA_ALPHA) * (m_magVar + MOTION_EMA_ALPHA * diff * diff);
    const float sd = sqrtf(m_magVar);
    if (sd < MOTION_STILL_STDDEV_G) m_motion = MotionLevel::STILL;
    else if (sd > MOTION_ACTIVE_STDDEV_G) m_motion = MotionLevel::ACTIVE;
    else m_motion = MotionLevel::NORMAL;
    m_lastMag = mag;
}

void FallDetector::resetToMonitoring(uint32_t now, uint32_t cooldownMs) {
    m_phaseStartMs = now;
    m_cooldownMs = cooldownMs;
    m_phase = cooldownMs > 0 ? Phase::COOLDOWN : Phase::MONITORING;
}

void FallDetector::emit(EventType t, RejectReason r, uint8_t conf, uint32_t now) {
    Event e;
    e.type = t;
    e.reason = r;
    e.confidence = conf;
    e.peakG = m_peakG;
    e.peakGyro = m_peakGyro;
    e.orientationDeg = m_lastOrientationDeg;
    e.stillRatio = m_lastStillRatio;
    e.freeFall = m_freeFallBefore;
    e.ms = now;
    if (m_queue != nullptr && xQueueSend(m_queue, &e, 0) != pdTRUE) {
        // Queue full (application loop stalled): drop the oldest, keep the newest.
        Event dropped;
        xQueueReceive(m_queue, &dropped, 0);
        xQueueSend(m_queue, &e, 0);
    }
}

void FallDetector::process(const ImuSample& s) {
    const uint32_t now = s.ms;
    uint32_t dt = (m_lastSampleMs == 0) ? IMU_SAMPLE_PERIOD_MS : (now - m_lastSampleMs);
    if (dt == 0) dt = 1;
    if (dt > 50) dt = 50;  // a long gap (task starved) must not count as stillness
    m_lastSampleMs = now;

    const float mag = sqrtf(s.ax * s.ax + s.ay * s.ay + s.az * s.az);
    const float gyro = sqrtf(s.gx * s.gx + s.gy * s.gy + s.gz * s.gz);
    updateMotionLevel(mag);

    // --- enable / disable requests from the application ---
    if (!m_enabledReq) {
        if (m_phase != Phase::PAUSED) {
            m_phase = Phase::PAUSED;
            m_enabled = false;
        }
    } else if (!m_enabled) {
        m_enabled = true;
        resetToMonitoring(now, 0);
    }

    // --- continuous tracking (needed even before an impact) ---
    const Vec3 acc = {s.ax, s.ay, s.az};
    if (!m_gravityInit) {
        m_gravityLp = acc;
        m_gravityInit = true;
        m_bucketStartMs = now;
    } else {
        m_gravityLp.x += GRAVITY_LP_ALPHA * (acc.x - m_gravityLp.x);
        m_gravityLp.y += GRAVITY_LP_ALPHA * (acc.y - m_gravityLp.y);
        m_gravityLp.z += GRAVITY_LP_ALPHA * (acc.z - m_gravityLp.z);
    }
    if (gyro > m_curBucketGyroMax) m_curBucketGyroMax = gyro;
    if (now - m_bucketStartMs >= BUCKET_MS) {
        m_bucketGravity[m_bucketIdx] = m_gravityLp;
        m_bucketGyroMax[m_bucketIdx] = m_curBucketGyroMax;
        m_bucketIdx = (uint8_t)((m_bucketIdx + 1) % HISTORY_LEN);
        if (m_bucketFill < HISTORY_LEN) m_bucketFill++;
        m_curBucketGyroMax = 0;
        m_bucketStartMs = now;
    }

    // Stage 1: free-fall episodes
    if (mag < FALL_FREEFALL_G) {
        if (!m_inFreeFall) {
            m_inFreeFall = true;
            m_freeFallStartMs = now;
        }
    } else if (m_inFreeFall) {
        m_inFreeFall = false;
        const uint32_t dur = now - m_freeFallStartMs;
        if (dur >= FALL_FREEFALL_MIN_MS) {
            m_lastFreeFallEndMs = now;
            m_lastFreeFallDurMs = dur;
        }
    }

    // Impact edge detection (also feeds the repetitive-impact filter)
    const float impactG = impactThreshold();
    const bool above = mag >= impactG;
    bool impactEdge = false;
    if (above && !m_aboveImpact && (m_lastImpactMs == 0 || now - m_lastImpactMs >= FALL_IMPACT_DEBOUNCE_MS)) {
        impactEdge = true;
        m_lastImpactMs = now;
        m_impactTimes[m_impactIdx] = now;
        m_impactIdx = (uint8_t)((m_impactIdx + 1) % IMPACT_HISTORY);
    }
    m_aboveImpact = above;

    switch (m_phase) {
        case Phase::PAUSED:
            return;

        case Phase::COOLDOWN:
            if (now - m_phaseStartMs >= m_cooldownMs) m_phase = Phase::MONITORING;
            return;

        case Phase::MONITORING: {
            if (!impactEdge) return;
            // False-positive filter: several impacts in a short window is an
            // activity (clapping, hammering, jogging), not a fall.
            if (countRecentImpacts(now) >= FALL_REPETITIVE_COUNT) return;

            // Stage 2: impact -> start evaluating a candidate
            m_impactMs = now;
            m_peakG = mag;
            float g = gyro > m_curBucketGyroMax ? gyro : m_curBucketGyroMax;
            const uint8_t lookBuckets = 10;  // rotation during the last ~1 s (the fall itself)
            for (uint8_t i = 1; i <= lookBuckets && i <= m_bucketFill; i++) {
                const uint8_t idx = (uint8_t)((m_bucketIdx + HISTORY_LEN - i) % HISTORY_LEN);
                if (m_bucketGyroMax[idx] > g) g = m_bucketGyroMax[idx];
            }
            m_peakGyro = g;
            m_freeFallBefore = (m_lastFreeFallEndMs != 0) && (now - m_lastFreeFallEndMs <= FALL_FREEFALL_TO_IMPACT_MAX_MS);
            m_freeFallDurMs = m_freeFallBefore ? m_lastFreeFallDurMs : 0;

            // Pre-impact attitude: gravity direction ~1.5 s before impact,
            // before the person lost balance.
            const uint8_t back = (uint8_t)(FALL_PRE_IMPACT_LOOKBACK_MS / BUCKET_MS);
            if (m_bucketFill == 0) {
                m_preGravity = m_gravityLp;
            } else {
                const uint8_t avail = back <= m_bucketFill ? back : m_bucketFill;
                const uint8_t idx = (uint8_t)((m_bucketIdx + HISTORY_LEN - avail) % HISTORY_LEN);
                m_preGravity = m_bucketGravity[idx];
            }

            m_highGMs = 0;
            m_lastOrientationDeg = 0;
            m_lastStillRatio = 0;
            m_phase = Phase::POST_IMPACT;
            m_phaseStartMs = now;
            m_candidates++;
            emit(EventType::IMPACT_CANDIDATE, RejectReason::NONE, 0, now);
            return;
        }

        case Phase::POST_IMPACT: {
            // Stage 3: post-impact analysis
            if (mag > m_peakG) m_peakG = mag;
            if (gyro > m_peakGyro) m_peakGyro = gyro;
            if (above) m_highGMs += dt;
            if (m_highGMs > FALL_IMPACT_MAX_HIGH_G_MS) {
                m_rejected++;
                emit(EventType::CANDIDATE_REJECTED, RejectReason::LONG_HIGH_G, 0, now);
                resetToMonitoring(now, FALL_REJECT_COOLDOWN_MS);
                return;
            }
            if (now - m_phaseStartMs >= FALL_POST_IMPACT_SETTLE_MS) {
                m_phase = Phase::INACTIVITY;
                m_phaseStartMs = now;
                m_stillSamples = 0;
                m_totalSamples = 0;
                memset(m_activeHist, 0, sizeof(m_activeHist));
                m_activeIdx = 0;
                m_activeCount = 0;
                m_postGravitySum = {0, 0, 0};
            }
            return;
        }

        case Phase::INACTIVITY: {
            // Stage 5: motionless period (stage 4 orientation is accumulated here)
            const float dev = fabsf(mag - 1.0f);
            const bool still = (dev < FALL_STILL_ACC_DEV_G) && (gyro < FALL_STILL_GYRO_DPS);
            m_totalSamples++;
            if (still) m_stillSamples++;
            m_postGravitySum.x += acc.x;
            m_postGravitySum.y += acc.y;
            m_postGravitySum.z += acc.z;

            // Early abort: the wearer is clearly up and moving (sliding 1 s
            // window). Skipped when the fall evidence is strong, so arm
            // movement on the floor cannot suppress the alert.
            const uint8_t active = ((dev > FALL_RECOVERY_ACC_DEV_G) || (gyro > FALL_RECOVERY_GYRO_DPS)) ? 1 : 0;
            m_activeCount = (uint8_t)(m_activeCount - m_activeHist[m_activeIdx] + active);
            m_activeHist[m_activeIdx] = active;
            m_activeIdx = (uint8_t)((m_activeIdx + 1) % ACTIVE_WINDOW);
            const uint32_t windowSamples = FALL_RECOVERY_WINDOW_MS / IMU_SAMPLE_PERIOD_MS;
            if (m_totalSamples >= windowSamples &&
                m_activeCount >= (uint8_t)(FALL_RECOVERY_ACTIVE_FRACTION * ACTIVE_WINDOW) &&
                evidenceScore(angleDeg(m_preGravity, m_gravityLp)) < FALL_STRONG_EVIDENCE_SCORE) {
                m_lastStillRatio = (float)m_stillSamples / m_totalSamples;
                m_rejected++;
                emit(EventType::CANDIDATE_REJECTED, RejectReason::USER_MOVING, 0, now);
                resetToMonitoring(now, FALL_REJECT_COOLDOWN_MS);
                return;
            }
            if (now - m_phaseStartMs >= FALL_INACTIVITY_WINDOW_MS) finishDecision(now);
            return;
        }
    }
}

// Fall evidence gathered up to the end of the settle phase, 0..80 points:
// impact magnitude 30, preceding free-fall 15, rotation 15, orientation 20.
float FallDetector::evidenceScore(float orientationDeg) const {
    const float impactG = impactThreshold();
    const float impactScore = 5.0f + 25.0f * clamp01((m_peakG - impactG) / (FALL_IMPACT_FULL_SCORE_G - impactG));
    const float freeFallScore = m_freeFallBefore ? 15.0f * clamp01((float)m_freeFallDurMs / 150.0f) : 0.0f;
    const float gyroScore = 15.0f * clamp01((m_peakGyro - FALL_GYRO_MIN_SCORE_DPS) /
                                            (FALL_GYRO_FULL_SCORE_DPS - FALL_GYRO_MIN_SCORE_DPS));
    const float orientScore = 20.0f * clamp01((orientationDeg - FALL_ORIENT_MIN_SCORE_DEG) /
                                              (FALL_ORIENT_FULL_SCORE_DEG - FALL_ORIENT_MIN_SCORE_DEG));
    return impactScore + freeFallScore + gyroScore + orientScore;
}

void FallDetector::finishDecision(uint32_t now) {
    const float stillRatio = m_totalSamples ? (float)m_stillSamples / m_totalSamples : 0.0f;
    const float orient = angleDeg(m_preGravity, m_postGravitySum);
    m_lastStillRatio = stillRatio;
    m_lastOrientationDeg = orient;

    // Confidence = evidence (0..80) + stillness (0..20)
    const float evidence = evidenceScore(orient);
    const float stillScore = 20.0f * clamp01((stillRatio - 0.3f) / (0.95f - 0.3f));
    float total = evidence + stillScore;
    if (total > 100.0f) total = 100.0f;
    const uint8_t conf = (uint8_t)(total + 0.5f);
    m_lastConfidence = conf;

    // Sensitivity shifts the strong-evidence bar together with the threshold.
    const int strongBar = FALL_STRONG_EVIDENCE_SCORE + (confidenceThreshold() - FALL_CONFIDENCE_THRESHOLD);
    const bool strong = evidence >= (float)strongBar;
    const float minStill = strong ? FALL_INACTIVITY_MIN_RATIO_STRONG : FALL_INACTIVITY_MIN_RATIO;

    if (stillRatio < minStill) {
        m_rejected++;
        emit(EventType::CANDIDATE_REJECTED, RejectReason::NOT_MOTIONLESS, conf, now);
        resetToMonitoring(now, FALL_REJECT_COOLDOWN_MS);
        return;
    }

    if (strong || (int)conf >= confidenceThreshold()) {
        m_confirmed++;
        emit(EventType::FALL_CONFIRMED, RejectReason::NONE, conf, now);
        resetToMonitoring(now, FALL_COOLDOWN_MS);
    } else {
        m_rejected++;
        emit(EventType::CANDIDATE_REJECTED, RejectReason::LOW_CONFIDENCE, conf, now);
        resetToMonitoring(now, FALL_REJECT_COOLDOWN_MS);
    }
}

bool FallDetector::pollEvent(Event& out) {
    if (m_queue == nullptr) return false;
    return xQueueReceive(m_queue, &out, 0) == pdTRUE;
}

FallDetector::Snapshot FallDetector::snapshot() {
    Snapshot s;
    s.phase = m_phase;
    s.motion = m_motion;
    s.accelMag = m_lastMag;
    s.lastConfidence = m_lastConfidence;
    s.candidates = m_candidates;
    s.confirmed = m_confirmed;
    s.rejected = m_rejected;
    return s;
}

const char* FallDetector::phaseName(Phase p) {
    switch (p) {
        case Phase::MONITORING:  return "MONITORING";
        case Phase::POST_IMPACT: return "POST_IMPACT";
        case Phase::INACTIVITY:  return "INACTIVITY";
        case Phase::COOLDOWN:    return "COOLDOWN";
        case Phase::PAUSED:    return "PAUSED";
    }
    return "?";
}

const char* FallDetector::reasonName(RejectReason r) {
    switch (r) {
        case RejectReason::NONE:           return "none";
        case RejectReason::LONG_HIGH_G:    return "sustained high-g (activity/vibration)";
        case RejectReason::USER_MOVING:    return "user moving after impact";
        case RejectReason::NOT_MOTIONLESS: return "not motionless after impact";
        case RejectReason::LOW_CONFIDENCE: return "low confidence";
    }
    return "?";
}

const char* FallDetector::motionName(MotionLevel m) {
    switch (m) {
        case MotionLevel::STILL:  return "Still";
        case MotionLevel::NORMAL: return "Normal";
        case MotionLevel::ACTIVE: return "Active";
    }
    return "?";
}
