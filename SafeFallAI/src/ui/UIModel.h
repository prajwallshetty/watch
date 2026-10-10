/**
 * SafeFall AI - UIModel.h
 *
 * The UI is a pure view: each frame the application fills a UiModel snapshot
 * and the screens render it. The UI never changes device/fall state itself;
 * user intent leaves the UI as a UiAction that the application routes to the
 * state machine. This keeps UI state and fall-detection state separate.
 */
#pragma once

#include <Arduino.h>
#include "../fall/FallStateMachine.h"
#include "../fall/FallDetector.h"
#include "../hardware/IMUManager.h"

enum class UiAction : uint8_t {
    NONE = 0,
    IM_OK,               // fall alert: "I'M OK"
    FALL_SOS,            // fall alert: "SOS"
    CANCEL_EMERGENCY,    // SOS screen: hold to cancel
    MANUAL_SOS,          // SOS page: hold for help
    BRIGHTNESS_UP,
    BRIGHTNESS_DOWN,
    MONITORING_ON,
    MONITORING_OFF,
    CYCLE_SENSITIVITY,
};

struct UiModel {
    DeviceState state;
    DeviceState cancelledFrom;
    Trigger lastTrigger;
    uint32_t countdownMsLeft;
    uint8_t countdownTotalS;

    // Header data
    char clock[8];
    char date[20];
    bool timeSynced;
    uint8_t battery;
    uint16_t batteryMv;
    bool batteryPresent;
    bool batteryLow;
    bool batteryCritical;
    bool bleReady;
    bool bleConnected;

    // Monitoring
    bool monitoring;
    bool imuOk;
    bool touchOk;
    bool displayOk;
    MotionLevel motion;

    // Events
    const char* activeEventId;
    const char* lastEventId;
    bool eventDelivered;
    uint8_t pendingEvents;

    // Settings
    uint8_t brightness;
    uint8_t sensitivity;

    // Diagnostics
    ImuSample imu;
    FallDetector::Snapshot det;
    bool imuBigEndian;
    uint32_t imuRecoveries;
    uint8_t touchChipId;
    uint16_t bleMtu;
    uint32_t flashSize;
    bool psramFound;
    uint32_t psramSize;
    uint32_t freeHeap;
    uint32_t minFreeHeap;
    uint32_t uptimeS;
    const char* resetReason;
};
