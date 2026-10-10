/**
 * SafeFall AI - FallDetector.h
 *
 * Multi-stage, wrist-worn fall detector. ENGINEERING PROTOTYPE: it is not a
 * certified medical device and will not detect every fall.
 *
 * Pipeline (all thresholds in AppConfig.h):
 *
 *   MONITORING      track gravity direction, free-fall episodes, recent
 *                   impacts and rotation rate
 *        | |a| >= impact threshold (and not repetitive activity)
 *        v                                         -> Event IMPACT_CANDIDATE
 *   POST_IMPACT     collect peak g / peak rotation, reject long high-g bursts
 *        | settle time elapsed
 *        v
 *   INACTIVITY      measure stillness; mostly-active motion = the user got
 *                   up (reject early, unless the fall evidence is strong).
 *                   Accumulate post-fall orientation.
 *        | window elapsed
 *        v
 *   DECISION        orientation change vs. pre-impact attitude. Evidence
 *                   score (impact 30, free-fall 15, rotation 15, orientation
 *                   20) + stillness 20 = confidence 0..100.
 *                     strong evidence + stillness >= 40 %   -> confirmed
 *                     otherwise stillness >= 80 % and
 *                     confidence >= threshold               -> confirmed
 *                   -> Event FALL_CONFIRMED / CANDIDATE_REJECTED
 *        v
 *   COOLDOWN        suppress duplicate detections of the same fall
 *
 * process() runs in the IMU task. Events cross to the application loop through
 * a FreeRTOS queue; configuration setters are safe to call from any task.
 */
#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "../hardware/IMUManager.h"

enum class Sensitivity : uint8_t { LOW_SENS = 0, NORMAL = 1, HIGH_SENS = 2 };
enum class MotionLevel : uint8_t { STILL = 0, NORMAL = 1, ACTIVE = 2 };

class FallDetector {
public:
    enum class Phase : uint8_t { MONITORING, POST_IMPACT, INACTIVITY, COOLDOWN, PAUSED };

    enum class EventType : uint8_t { IMPACT_CANDIDATE, FALL_CONFIRMED, CANDIDATE_REJECTED };

    enum class RejectReason : uint8_t {
        NONE,
        LONG_HIGH_G,       // vibration / sustained activity
        USER_MOVING,       // sustained motion after impact: user recovered
        NOT_MOTIONLESS,    // stillness ratio below the mandatory minimum
        LOW_CONFIDENCE,    // evidence too weak
    };

    struct Event {
        EventType type;
        RejectReason reason;
        uint8_t confidence;
        float peakG;
        float peakGyro;
        float orientationDeg;
        float stillRatio;
        bool freeFall;
        uint32_t ms;
    };

    struct Snapshot {
        Phase phase;
        MotionLevel motion;
        float accelMag;
        uint8_t lastConfidence;
        uint32_t candidates;
        uint32_t confirmed;
        uint32_t rejected;
    };

    bool begin();
    void process(const ImuSample& s);           // IMU task only

    bool pollEvent(Event& out);                 // application loop
    Snapshot snapshot();

    void setEnabled(bool en) { m_enabledReq = en; }
    bool enabled() const { return m_enabledReq; }
    void setSensitivity(Sensitivity s) { m_sensitivity = s; }
    Sensitivity sensitivity() const { return m_sensitivity; }

    static const char* phaseName(Phase p);
    static const char* reasonName(RejectReason r);
    static const char* motionName(MotionLevel m);

private:
    struct Vec3 { float x, y, z; };
    static const uint8_t HISTORY_LEN = 20;      // 20 x 100 ms buckets = 2 s
    static const uint8_t IMPACT_HISTORY = 8;

    void resetToMonitoring(uint32_t now, uint32_t cooldownMs);
    void emit(EventType t, RejectReason r, uint8_t conf, uint32_t now);
    void finishDecision(uint32_t now);
    float evidenceScore(float orientationDeg) const;
    float impactThreshold() const;
    int confidenceThreshold() const;
    uint8_t countRecentImpacts(uint32_t now) const;
    static Vec3 normalize(const Vec3& v);
    static float angleDeg(const Vec3& a, const Vec3& b);
    void updateMotionLevel(float mag);

    QueueHandle_t m_queue = nullptr;
    portMUX_TYPE m_mux = portMUX_INITIALIZER_UNLOCKED;

    volatile bool m_enabledReq = true;
    volatile Sensitivity m_sensitivity = Sensitivity::NORMAL;
    bool m_enabled = true;

    Phase m_phase = Phase::MONITORING;
    uint32_t m_phaseStartMs = 0;
    uint32_t m_cooldownMs = 0;
    uint32_t m_lastSampleMs = 0;

    // Gravity / orientation tracking
    Vec3 m_gravityLp = {0, 0, 1};
    bool m_gravityInit = false;
    Vec3 m_bucketGravity[HISTORY_LEN];
    float m_bucketGyroMax[HISTORY_LEN];
    uint8_t m_bucketIdx = 0;
    uint8_t m_bucketFill = 0;
    uint32_t m_bucketStartMs = 0;
    float m_curBucketGyroMax = 0;

    // Free-fall tracking
    uint32_t m_freeFallStartMs = 0;
    bool m_inFreeFall = false;
    uint32_t m_lastFreeFallEndMs = 0;
    uint32_t m_lastFreeFallDurMs = 0;

    // Impact history (repetitive-impact filter)
    uint32_t m_impactTimes[IMPACT_HISTORY];
    uint8_t m_impactIdx = 0;
    uint32_t m_lastImpactMs = 0;
    bool m_aboveImpact = false;

    // Candidate being evaluated
    uint32_t m_impactMs = 0;
    float m_peakG = 0;
    float m_peakGyro = 0;
    bool m_freeFallBefore = false;
    uint32_t m_freeFallDurMs = 0;
    Vec3 m_preGravity = {0, 0, 1};
    uint32_t m_highGMs = 0;
    uint32_t m_stillSamples = 0;
    uint32_t m_totalSamples = 0;
    static const uint8_t ACTIVE_WINDOW = 100;   // samples (1 s at 100 Hz)
    uint8_t m_activeHist[ACTIVE_WINDOW];
    uint8_t m_activeIdx = 0;
    uint8_t m_activeCount = 0;
    Vec3 m_postGravitySum = {0, 0, 0};
    float m_lastOrientationDeg = 0;
    float m_lastStillRatio = 0;

    // Motion level (EMA of |a| mean and variance)
    float m_magMean = 1.0f;
    float m_magVar = 0.0f;
    volatile MotionLevel m_motion = MotionLevel::NORMAL;
    volatile float m_lastMag = 1.0f;

    volatile uint8_t m_lastConfidence = 0;
    volatile uint32_t m_candidates = 0;
    volatile uint32_t m_confirmed = 0;
    volatile uint32_t m_rejected = 0;
};

extern FallDetector Detector;
