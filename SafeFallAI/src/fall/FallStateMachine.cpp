#include "FallStateMachine.h"
#include "../config/AppConfig.h"
#include "../config/DebugConfig.h"

FallStateMachine StateMachine;

void FallStateMachine::begin(uint8_t countdownSeconds) {
    setCountdownSeconds(countdownSeconds);
    m_state = baseline();
    m_stateSinceMs = millis();
    m_lastTrigger = Trigger::BOOT;
    LOG_STATE("%s", stateName(m_state));
}

void FallStateMachine::setCountdownSeconds(uint8_t s) {
    if (s < FALL_COUNTDOWN_MIN_S) s = FALL_COUNTDOWN_MIN_S;
    if (s > FALL_COUNTDOWN_MAX_S) s = FALL_COUNTDOWN_MAX_S;
    m_countdownS = s;
}

DeviceState FallStateMachine::baseline() const {
    if (m_sensorFault) return DeviceState::DEVICE_ERROR;
    if (m_batteryCritical) return DeviceState::LOW_BATTERY;
    return DeviceState::NORMAL;
}

bool FallStateMachine::needsAttention() const {
    switch (m_state) {
        case DeviceState::FALL_DETECTED:
        case DeviceState::AWAITING_CONFIRMATION:
        case DeviceState::USER_CANCELLED:
        case DeviceState::EMERGENCY:
        case DeviceState::SOS_ACTIVE:
            return true;
        default:
            return false;
    }
}

void FallStateMachine::transition(DeviceState to, Trigger why) {
    if (to == m_state) return;
    const DeviceState from = m_state;
    if (to == DeviceState::USER_CANCELLED) m_beforeCancel = from;
    m_state = to;
    m_stateSinceMs = millis();
    m_lastTrigger = why;
    LOG_STATE("%s -> %s (%s)", stateName(from), stateName(to), triggerName(why));
    if (m_listener) m_listener(from, to, why, m_ctx);
}

uint8_t FallStateMachine::countdownRemaining(uint32_t nowMs) const {
    if (m_state != DeviceState::AWAITING_CONFIRMATION) return 0;
    const uint32_t elapsed = nowMs - m_stateSinceMs;
    const uint32_t total = (uint32_t)m_countdownS * 1000u;
    if (elapsed >= total) return 0;
    return (uint8_t)((total - elapsed + 999u) / 1000u);  // ceil -> shows 10 .. 1
}

void FallStateMachine::update(uint32_t nowMs) {
    switch (m_state) {
        case DeviceState::FALL_DETECTED:
            transition(DeviceState::AWAITING_CONFIRMATION, Trigger::AUTO_ADVANCE);
            break;
        case DeviceState::AWAITING_CONFIRMATION:
            if (nowMs - m_stateSinceMs >= (uint32_t)m_countdownS * 1000u) {
                transition(DeviceState::EMERGENCY, Trigger::COUNTDOWN_EXPIRED);
            }
            break;
        case DeviceState::POSSIBLE_FALL:
            // Safety net: the detector always resolves a candidate, but never
            // let the device sit here if an event was lost.
            if (nowMs - m_stateSinceMs >= FALL_POSSIBLE_TIMEOUT_MS) {
                transition(baseline(), Trigger::POSSIBLE_TIMEOUT);
            }
            break;
        case DeviceState::USER_CANCELLED:
            if (nowMs - m_stateSinceMs >= USER_CANCELLED_DISPLAY_MS) {
                transition(baseline(), Trigger::CANCEL_DISPLAY_DONE);
            }
            break;
        default:
            break;
    }
}

void FallStateMachine::setHealth(bool sensorFault, bool batteryCritical) {
    m_sensorFault = sensorFault;
    m_batteryCritical = batteryCritical;
    if (isBaseline()) transition(baseline(), Trigger::HEALTH_CHANGE);
}

void FallStateMachine::onImpactCandidate() {
    if (isBaseline()) transition(DeviceState::POSSIBLE_FALL, Trigger::IMPACT_CANDIDATE);
}

void FallStateMachine::onFallConfirmed(uint8_t confidence) {
    if (isBaseline() || m_state == DeviceState::POSSIBLE_FALL) {
        m_lastConfidence = confidence;
        transition(DeviceState::FALL_DETECTED, Trigger::FALL_CONFIRMED);
    }
}

void FallStateMachine::onFallRejected() {
    if (m_state == DeviceState::POSSIBLE_FALL) transition(baseline(), Trigger::FALL_REJECTED);
}

bool FallStateMachine::userOk() {
    if (m_state != DeviceState::AWAITING_CONFIRMATION) return false;
    transition(DeviceState::USER_CANCELLED, Trigger::USER_OK);
    return true;
}

bool FallStateMachine::userRequestSOS() {
    if (m_state != DeviceState::AWAITING_CONFIRMATION && m_state != DeviceState::FALL_DETECTED) return false;
    transition(DeviceState::EMERGENCY, Trigger::USER_SOS);
    return true;
}

bool FallStateMachine::userCancelEmergency() {
    if (!isEmergency()) return false;
    transition(DeviceState::USER_CANCELLED, Trigger::USER_CANCEL);
    return true;
}

bool FallStateMachine::manualSOS(Trigger source) {
    if (isEmergency()) return false;
    if (m_state == DeviceState::AWAITING_CONFIRMATION || m_state == DeviceState::FALL_DETECTED) {
        // SOS during a fall alert escalates that fall instead of opening a second event.
        transition(DeviceState::EMERGENCY, Trigger::USER_SOS);
        return true;
    }
    transition(DeviceState::SOS_ACTIVE, source);
    return true;
}

bool FallStateMachine::remoteCancel() {
    if (m_state == DeviceState::AWAITING_CONFIRMATION || isEmergency()) {
        transition(DeviceState::USER_CANCELLED, Trigger::REMOTE_CANCEL);
        return true;
    }
    return false;
}

const char* FallStateMachine::stateName(DeviceState s) {
    switch (s) {
        case DeviceState::NORMAL:                return "NORMAL";
        case DeviceState::POSSIBLE_FALL:         return "POSSIBLE_FALL";
        case DeviceState::FALL_DETECTED:         return "FALL_DETECTED";
        case DeviceState::AWAITING_CONFIRMATION: return "AWAITING_CONFIRMATION";
        case DeviceState::USER_CANCELLED:        return "USER_CANCELLED";
        case DeviceState::EMERGENCY:             return "EMERGENCY";
        case DeviceState::SOS_ACTIVE:            return "SOS_ACTIVE";
        case DeviceState::LOW_BATTERY:           return "LOW_BATTERY";
        case DeviceState::DEVICE_ERROR:          return "DEVICE_ERROR";
    }
    return "?";
}

const char* FallStateMachine::triggerName(Trigger t) {
    switch (t) {
        case Trigger::NONE:                return "none";
        case Trigger::BOOT:                return "boot";
        case Trigger::HEALTH_CHANGE:       return "health change";
        case Trigger::IMPACT_CANDIDATE:    return "impact candidate";
        case Trigger::FALL_CONFIRMED:      return "fall confirmed";
        case Trigger::FALL_REJECTED:       return "candidate rejected";
        case Trigger::POSSIBLE_TIMEOUT:    return "possible-fall timeout";
        case Trigger::AUTO_ADVANCE:        return "auto";
        case Trigger::USER_OK:             return "user pressed I'M OK";
        case Trigger::USER_SOS:            return "user pressed SOS";
        case Trigger::COUNTDOWN_EXPIRED:   return "countdown expired";
        case Trigger::USER_CANCEL:         return "user cancelled";
        case Trigger::REMOTE_CANCEL:       return "phone cancelled";
        case Trigger::SCREEN_SOS:          return "SOS from screen";
        case Trigger::BUTTON_SOS:          return "SOS from BOOT key";
        case Trigger::CANCEL_DISPLAY_DONE: return "cancel shown";
    }
    return "?";
}
