/**
 * SafeFall AI - FallStateMachine.h
 *
 * The device safety state. Pure logic: no display, no BLE, no LVGL. Inputs
 * are detector events, user/remote requests and health flags; outputs are the
 * current state and a transition callback the application uses for side
 * effects (event creation, BLE packets, screen changes).
 *
 *   NORMAL / LOW_BATTERY / DEVICE_ERROR   "baseline" states, chosen from health
 *        | impact candidate
 *   POSSIBLE_FALL ---- rejected / timeout ----> baseline
 *        | fall confirmed
 *   FALL_DETECTED  (entry: create event)
 *        | immediately
 *   AWAITING_CONFIRMATION --- I'M OK / remote cancel ---> USER_CANCELLED
 *        | SOS pressed or countdown expired
 *   EMERGENCY ------------ hold-to-cancel / remote cancel ---> USER_CANCELLED
 *
 *   any non-emergency state -- SOS (screen/BOOT key) --> SOS_ACTIVE --cancel--> USER_CANCELLED
 *   USER_CANCELLED -- after a short confirmation display --> baseline
 */
#pragma once

#include <Arduino.h>

enum class DeviceState : uint8_t {
    NORMAL = 0,
    POSSIBLE_FALL,
    FALL_DETECTED,
    AWAITING_CONFIRMATION,
    USER_CANCELLED,
    EMERGENCY,
    SOS_ACTIVE,
    LOW_BATTERY,
    DEVICE_ERROR,
};

enum class Trigger : uint8_t {
    NONE = 0,
    BOOT,
    HEALTH_CHANGE,
    IMPACT_CANDIDATE,
    FALL_CONFIRMED,
    FALL_REJECTED,
    POSSIBLE_TIMEOUT,
    AUTO_ADVANCE,
    USER_OK,
    USER_SOS,
    COUNTDOWN_EXPIRED,
    USER_CANCEL,
    REMOTE_CANCEL,
    SCREEN_SOS,
    BUTTON_SOS,
    CANCEL_DISPLAY_DONE,
};

class FallStateMachine {
public:
    typedef void (*Listener)(DeviceState from, DeviceState to, Trigger why, void* ctx);

    void begin(uint8_t countdownSeconds);
    void setListener(Listener fn, void* ctx) { m_listener = fn; m_ctx = ctx; }
    void setCountdownSeconds(uint8_t s);
    uint8_t countdownSeconds() const { return m_countdownS; }

    void update(uint32_t nowMs);

    // Health inputs (re-evaluated whenever the device is in a baseline state)
    void setHealth(bool sensorFault, bool batteryCritical);

    // Detector inputs
    void onImpactCandidate();
    void onFallConfirmed(uint8_t confidence);
    void onFallRejected();

    // User / remote inputs. Each returns true if it caused a transition.
    bool userOk();
    bool userRequestSOS();          // SOS button on the fall alert
    bool userCancelEmergency();     // hold-to-cancel on the SOS screen
    bool manualSOS(Trigger source); // SCREEN_SOS / BUTTON_SOS
    bool remoteCancel();

    DeviceState state() const { return m_state; }
    uint32_t stateSinceMs() const { return m_stateSinceMs; }
    uint8_t lastConfidence() const { return m_lastConfidence; }
    Trigger lastTrigger() const { return m_lastTrigger; }
    DeviceState stateBeforeCancel() const { return m_beforeCancel; }
    uint8_t countdownRemaining(uint32_t nowMs) const;

    bool isBaseline() const { return isBaseline(m_state); }
    bool isEmergency() const { return m_state == DeviceState::EMERGENCY || m_state == DeviceState::SOS_ACTIVE; }
    bool needsAttention() const;  // states that must keep the screen on

    static bool isBaseline(DeviceState s) {
        return s == DeviceState::NORMAL || s == DeviceState::LOW_BATTERY || s == DeviceState::DEVICE_ERROR;
    }
    static const char* stateName(DeviceState s);
    static const char* triggerName(Trigger t);

private:
    void transition(DeviceState to, Trigger why);
    DeviceState baseline() const;

    DeviceState m_state = DeviceState::NORMAL;
    DeviceState m_beforeCancel = DeviceState::NORMAL;
    uint32_t m_stateSinceMs = 0;
    uint8_t m_countdownS = 10;
    uint8_t m_lastConfidence = 0;
    Trigger m_lastTrigger = Trigger::NONE;
    bool m_sensorFault = false;
    bool m_batteryCritical = false;

    Listener m_listener = nullptr;
    void* m_ctx = nullptr;
};

extern FallStateMachine StateMachine;
