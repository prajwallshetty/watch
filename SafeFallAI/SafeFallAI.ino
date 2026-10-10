/**
 * SafeFall AI - wearable fall-detection firmware
 * Board : Waveshare ESP32-S3-Touch-LCD-1.28 (ESP32-S3R2, GC9A01, CST816S, QMI8658)
 * Core  : ESP32 Arduino Core 2.0.12 | LVGL 8.3.10 | TFT_eSPI 2.5.34 | CST816S (fbiego)
 *
 * ENGINEERING PROTOTYPE - NOT A MEDICAL DEVICE.
 * Fall detection is heuristic, has not been clinically validated, and will not
 * detect every fall. Do not rely on it as the only safety measure.
 *
 * Architecture
 *   imu task (100 Hz, core 1) : QMI8658 -> FallDetector -> event queue
 *   loop()   (core 1)         : state machine, BLE, power, UI (LVGL), settings
 *   BT task  (core 0)         : GATT callbacks -> command queue
 * The IMU task never touches LVGL and keeps running while the screen is off.
 */

#include "src/config/BoardConfig.h"
#include "src/config/AppConfig.h"
#include "src/config/DebugConfig.h"

#include "src/hardware/I2CBus.h"
#include "src/hardware/DisplayManager.h"
#include "src/hardware/TouchManager.h"
#include "src/hardware/IMUManager.h"
#include "src/hardware/BatteryManager.h"

#include "src/fall/FallDetector.h"
#include "src/fall/FallStateMachine.h"

#include "src/ble/BLEManager.h"
#include "src/ble/BLEProtocol.h"

#include "src/services/EventManager.h"
#include "src/services/TimeManager.h"
#include "src/services/PowerManager.h"
#include "src/services/SettingsStore.h"

#include "src/ui/UIManager.h"

#include <esp_attr.h>

// ---------------------------------------------------------------------------
// Application state (glue only; safety state lives in the state machine)
// ---------------------------------------------------------------------------
static char s_activeEventId[12] = "";
static bool s_uiReady = false;
static bool s_statusDirty = true;
static bool s_batteryDirty = true;
static uint32_t s_lastStatusMs = 0;
static uint32_t s_lastBatteryTxMs = 0;
static uint32_t s_lastTouchSeen = 0;
static bool s_wasScreenOff = false;

// Survives a software reboot but not a power cycle: prevents reboot loops
// when the IMU is physically dead.
RTC_NOINIT_ATTR static uint32_t s_imuRebootMarker;
static const uint32_t IMU_REBOOT_MAGIC = 0x51AFE0A1;

static const char* const TRIG_DETECTOR = "DETECTOR";
static const char* const TRIG_TIMEOUT = "TIMEOUT";
static const char* const TRIG_USER_SOS = "USER_SOS";
static const char* const TRIG_USER_OK = "USER_OK";
static const char* const TRIG_USER_CANCEL = "USER_CANCEL";
static const char* const TRIG_PHONE = "PHONE";
static const char* const TRIG_SCREEN = "SCREEN";
static const char* const TRIG_BOOT_KEY = "BOOT_KEY";

// ---------------------------------------------------------------------------
// Callbacks
// ---------------------------------------------------------------------------
static void onImuSample(const ImuSample& s, void* /*ctx*/) {
    Detector.process(s);  // runs in the IMU task
}

static bool onTouchWake(void* /*ctx*/) {
    return Power.wakeFromTouch();
}

static void onStateTransition(DeviceState from, DeviceState to, Trigger why, void* /*ctx*/) {
    (void)from;
    const uint32_t epoch = Clock.epochUtc();
    const uint32_t up = Clock.uptimeS();

    switch (to) {
        case DeviceState::FALL_DETECTED: {
            SafetyEvent* ev = Events.create(EventKind::FALL, EventStage::DETECTED, StateMachine.lastConfidence(),
                                            TRIG_DETECTOR, epoch, up);
            strncpy(s_activeEventId, ev->id, sizeof(s_activeEventId) - 1);
            LOG_FALL("FALL DETECTED %s (confidence %u) - waiting %us for user", ev->id,
                     StateMachine.lastConfidence(), StateMachine.countdownSeconds());
            break;
        }
        case DeviceState::EMERGENCY: {
            const char* trig = (why == Trigger::COUNTDOWN_EXPIRED) ? TRIG_TIMEOUT : TRIG_USER_SOS;
            if (s_activeEventId[0] == '\0' || !Events.updateStage(s_activeEventId, EventStage::EMERGENCY, trig)) {
                SafetyEvent* ev = Events.create(EventKind::FALL, EventStage::EMERGENCY, StateMachine.lastConfidence(),
                                                trig, epoch, up);
                strncpy(s_activeEventId, ev->id, sizeof(s_activeEventId) - 1);
            }
            LOG_FALL("EMERGENCY %s (%s)", s_activeEventId, trig);
            break;
        }
        case DeviceState::SOS_ACTIVE: {
            const char* trig = (why == Trigger::BUTTON_SOS) ? TRIG_BOOT_KEY : TRIG_SCREEN;
            SafetyEvent* ev = Events.create(EventKind::SOS, EventStage::ACTIVE, 0, trig, epoch, up);
            strncpy(s_activeEventId, ev->id, sizeof(s_activeEventId) - 1);
            break;
        }
        case DeviceState::USER_CANCELLED: {
            if (s_activeEventId[0] != '\0') {
                const bool remote = (why == Trigger::REMOTE_CANCEL);
                const char* trig = remote ? TRIG_PHONE : (why == Trigger::USER_OK ? TRIG_USER_OK : TRIG_USER_CANCEL);
                Events.updateStage(s_activeEventId, remote ? EventStage::RESOLVED : EventStage::CANCELLED, trig);
            }
            s_activeEventId[0] = '\0';
            break;
        }
        default:
            break;
    }

    if (StateMachine.needsAttention()) Power.noteActivity(millis());
    s_statusDirty = true;
}

// ---------------------------------------------------------------------------
// BLE helpers
// ---------------------------------------------------------------------------
static void publishConfig() {
    char buf[BLE_MAX_PACKET_LEN + 1];
    const UserSettings& st = Settings.get();
    BLEProtocol::ConfigInfo c = {st.countdownS, st.sensitivity, st.monitoring, st.screenTimeoutS, st.brightness};
    size_t n = BLEProtocol::buildConfig(buf, sizeof(buf), c);
    Ble.setConfigValue(buf, n);
}

static void publishTime() {
    char buf[BLE_MAX_PACKET_LEN + 1];
    size_t n = BLEProtocol::buildTime(buf, sizeof(buf), Clock.epochUtc(), Clock.tzOffset(), Clock.isSynced());
    Ble.setTimeValue(buf, n);
}

static void sendResult(const char* cmd, bool ok, const char* detail, const char* eventId) {
    char buf[BLE_MAX_PACKET_LEN + 1];
    size_t n = BLEProtocol::buildResult(buf, sizeof(buf), cmd, ok, detail, eventId);
    Ble.notifyStatus(buf, n);
}

static void sendStatus(uint32_t now) {
    char buf[BLE_MAX_PACKET_LEN + 1];
    BLEProtocol::StatusInfo s;
    s.state = FallStateMachine::stateName(StateMachine.state());
    s.monitoring = Settings.get().monitoring;
    s.imuOk = IMU.isHealthy();
    s.battery = Battery.percent();
    s.timeSynced = Clock.isSynced();
    s.uptimeS = Clock.uptimeS();
    s.pending = Events.pendingCount();
    s.activeEventId = s_activeEventId;
    size_t n = BLEProtocol::buildStatus(buf, sizeof(buf), s);
    Ble.notifyStatus(buf, n);
    s_lastStatusMs = now;
    s_statusDirty = false;
}

static void sendBattery(uint32_t now) {
    char buf[BLE_MAX_PACKET_LEN + 1];
    size_t n = BLEProtocol::buildBattery(buf, sizeof(buf), Battery.percent(), Battery.millivolts(), Battery.isLow(),
                                         Battery.isCritical());
    Ble.notifyBattery(buf, n, Battery.percent());
    s_lastBatteryTxMs = now;
    s_batteryDirty = false;
}

static void serviceOutbox(uint32_t now) {
    if (!Ble.isConnected()) return;
    SafetyEvent* ev = Events.nextToSend(now);
    if (ev == nullptr) return;
    char buf[BLE_MAX_PACKET_LEN + 1];
    size_t n = BLEProtocol::buildEvent(buf, sizeof(buf), *ev, Battery.percent());
    if (n == 0) return;
    const bool sent = (ev->kind == EventKind::FALL) ? Ble.notifyFall(buf, n) : Ble.notifySos(buf, n);
    if (sent) {
        Events.markSent(ev, now);
        if (ev->txCount == 1) LOG_BLE("Sent %s", buf);
    }
}

static void applySensitivity(uint8_t s) {
    Settings.setSensitivity(s);
    Detector.setSensitivity((Sensitivity)Settings.get().sensitivity);
}

static void applyMonitoring(bool on) {
    Settings.setMonitoring(on);
    Detector.setEnabled(on);
    LOG_FALL("Fall detection %s", on ? "enabled" : "PAUSED by user");
}

static void handleCommand(const BLEProtocol::Command& c) {
    const char* name = BLEProtocol::cmdName(c.type);
    switch (c.type) {
        case BLEProtocol::CmdType::ACK: {
            EventManager::AckResult r = Events.onAck(c.eventId, c.seq);
            if (r == EventManager::AckResult::UNKNOWN) LOG_BLE("ACK for unknown event %s ignored", c.eventId);
            else if (r == EventManager::AckResult::DUPLICATE) LOG_BLE("Duplicate ACK for %s ignored", c.eventId);
            else if (r == EventManager::AckResult::STALE) LOG_BLE("Stale ACK for %s (seq %lu) ignored", c.eventId,
                                                                  (unsigned long)c.seq);
            s_statusDirty = true;
            break;
        }
        case BLEProtocol::CmdType::CANCEL: {
            if (Events.wasRemoteCancelled(c.eventId)) {
                sendResult(name, false, "duplicate", c.eventId);
            } else if (strcmp(c.eventId, s_activeEventId) == 0 && StateMachine.remoteCancel()) {
                Events.rememberRemoteCancel(c.eventId);
                sendResult(name, true, "cancelled", c.eventId);
            } else {
                sendResult(name, false, "not active", c.eventId);
            }
            break;
        }
        case BLEProtocol::CmdType::CONFIG: {
            if (c.hasCountdown) {
                Settings.setCountdown((uint8_t)constrain(c.countdown, FALL_COUNTDOWN_MIN_S, FALL_COUNTDOWN_MAX_S));
                StateMachine.setCountdownSeconds(Settings.get().countdownS);
            }
            if (c.hasSensitivity) applySensitivity((uint8_t)constrain(c.sensitivity, 0, 2));
            if (c.hasFallDetection) applyMonitoring(c.fallDetection);
            if (c.hasScreenTimeout) {
                Settings.setScreenTimeout((uint8_t)constrain(c.screenTimeout, SCREEN_TIMEOUT_MIN_S, SCREEN_TIMEOUT_MAX_S));
            }
            if (c.hasBrightness) {
                Settings.setBrightness((uint8_t)constrain(c.brightness, BRIGHTNESS_MIN_PERCENT, 100));
                Display.setBrightness(Settings.get().brightness);
            }
            publishConfig();
            sendResult(name, true, "applied", "");
            s_statusDirty = true;
            LOG_BLE("CONFIG applied");
            break;
        }
        case BLEProtocol::CmdType::TIME_SYNC: {
            const int32_t tz = c.hasTzOffset ? c.tzOffset : Clock.tzOffset();
            const bool ok = Clock.sync(c.epoch, tz);
            if (ok) Settings.setTzOffset(tz);
            publishTime();
            sendResult(name, ok, ok ? "synced" : "invalid time", "");
            break;
        }
        default:
            break;
    }
}

static void handleUiAction(UiAction a) {
    Power.noteActivity(millis());
    switch (a) {
        case UiAction::IM_OK:            StateMachine.userOk(); break;
        case UiAction::FALL_SOS:         StateMachine.userRequestSOS(); break;
        case UiAction::CANCEL_EMERGENCY: StateMachine.userCancelEmergency(); break;
        case UiAction::MANUAL_SOS:       StateMachine.manualSOS(Trigger::SCREEN_SOS); break;
        case UiAction::BRIGHTNESS_UP:
            Settings.setBrightness((uint8_t)min(100, Settings.get().brightness + BRIGHTNESS_STEP_PERCENT));
            Display.setBrightness(Settings.get().brightness);
            publishConfig();
            break;
        case UiAction::BRIGHTNESS_DOWN:
            Settings.setBrightness((uint8_t)max(BRIGHTNESS_MIN_PERCENT, Settings.get().brightness - BRIGHTNESS_STEP_PERCENT));
            Display.setBrightness(Settings.get().brightness);
            publishConfig();
            break;
        case UiAction::MONITORING_ON:
            applyMonitoring(true);
            publishConfig();
            s_statusDirty = true;
            break;
        case UiAction::MONITORING_OFF:
            applyMonitoring(false);
            publishConfig();
            s_statusDirty = true;
            break;
        case UiAction::CYCLE_SENSITIVITY:
            applySensitivity((uint8_t)((Settings.get().sensitivity + 1) % 3));
            publishConfig();
            break;
        default:
            break;
    }
}

static void handleDetectorEvent(const FallDetector::Event& e) {
    switch (e.type) {
        case FallDetector::EventType::IMPACT_CANDIDATE:
            LOG_FALL("Impact %.2f g, rotation %.0f dps%s -> analysing", e.peakG, e.peakGyro,
                     e.freeFall ? ", free-fall before impact" : "");
            StateMachine.onImpactCandidate();
            break;
        case FallDetector::EventType::FALL_CONFIRMED:
            LOG_FALL("Confirmed: confidence %u (peak %.2f g, %.0f dps, orientation %.0f deg, still %.0f%%)",
                     e.confidence, e.peakG, e.peakGyro, e.orientationDeg, e.stillRatio * 100.0f);
            StateMachine.onFallConfirmed(e.confidence);
            break;
        case FallDetector::EventType::CANDIDATE_REJECTED:
            LOG_FALL("Rejected: %s (confidence %u, orientation %.0f deg, still %.0f%%)",
                     FallDetector::reasonName(e.reason), e.confidence, e.orientationDeg, e.stillRatio * 100.0f);
            StateMachine.onFallRejected();
            break;
    }
}

static void buildModel(UiModel& m, uint32_t now) {
    const DeviceState st = StateMachine.state();
    m.state = st;
    m.cancelledFrom = StateMachine.stateBeforeCancel();
    m.lastTrigger = StateMachine.lastTrigger();
    m.countdownTotalS = StateMachine.countdownSeconds();
    if (st == DeviceState::AWAITING_CONFIRMATION) {
        const uint32_t total = (uint32_t)m.countdownTotalS * 1000u;
        const uint32_t el = now - StateMachine.stateSinceMs();
        m.countdownMsLeft = el >= total ? 0 : total - el;
    } else {
        m.countdownMsLeft = (uint32_t)m.countdownTotalS * 1000u;
    }

    Clock.formatClock(m.clock, sizeof(m.clock));
    Clock.formatDate(m.date, sizeof(m.date));
    m.timeSynced = Clock.isSynced();

    m.battery = Battery.percent();
    m.batteryMv = Battery.millivolts();
    m.batteryPresent = Battery.present();
    m.batteryLow = Battery.isLow();
    m.batteryCritical = Battery.isCritical();
    m.bleReady = Ble.isReady();
    m.bleConnected = Ble.isConnected();

    m.monitoring = Settings.get().monitoring;
    m.imuOk = IMU.isHealthy();
    m.touchOk = Touch.isDetected();
    m.displayOk = Display.isOk();
    m.det = Detector.snapshot();
    m.motion = m.det.motion;

    m.activeEventId = s_activeEventId;
    m.lastEventId = Events.lastEventId();
    m.eventDelivered = s_activeEventId[0] != '\0' && Events.isDelivered(s_activeEventId);
    m.pendingEvents = Events.pendingCount();

    m.brightness = Settings.get().brightness;
    m.sensitivity = Settings.get().sensitivity;

    m.imu = IMU.latest();
    m.imuBigEndian = IMU.bigEndian();
    m.imuRecoveries = IMU.recoveries();
    m.touchChipId = Touch.chipId();
    m.bleMtu = Ble.peerMtu();
    m.flashSize = ESP.getFlashChipSize();
    m.psramFound = psramFound();
    m.psramSize = m.psramFound ? ESP.getPsramSize() : 0;
    m.freeHeap = ESP.getFreeHeap();
    m.minFreeHeap = ESP.getMinFreeHeap();
    m.uptimeS = Clock.uptimeS();
    m.resetReason = Power.resetReason();
}

// ---------------------------------------------------------------------------
// setup / loop
// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(DEBUG_BAUD);
    LOG_BOOT("SafeFall AI starting (firmware %s)", SAFE_FALL_VERSION);
    LOG_BOOT("%s | %s rev %d | %lu MHz | flash %lu MB | PSRAM %s", BOARD_NAME, ESP.getChipModel(),
             ESP.getChipRevision(), (unsigned long)ESP.getCpuFreqMHz(),
             (unsigned long)(ESP.getFlashChipSize() / (1024UL * 1024UL)), psramFound() ? "found" : "not enabled");
    LOG_BOOT("ENGINEERING PROTOTYPE - not a medical device");

    Settings.begin();
    Power.begin();  // reset reason + task watchdog

    if (!I2CBus::begin()) LOG_BOOT("ERROR: I2C bus init failed");

    // Display first so boot problems are visible.
    if (!Display.begin()) LOG_DISPLAY("ERROR: display init failed - running headless (alerts still work)");
    Display.setBrightness(Settings.get().brightness);
    s_uiReady = Display.disp() != nullptr;

    Touch.begin();
    if (s_uiReady) {
        Touch.registerWithLvgl();
        Touch.setWakeHandler(onTouchWake, nullptr);
    }

    Battery.begin();
    Clock.begin(Settings.get().tzOffsetS);
    Events.begin();

    Detector.setEnabled(Settings.get().monitoring);
    Detector.setSensitivity((Sensitivity)Settings.get().sensitivity);
    Detector.begin();

    const bool imuOk = IMU.begin();
    if (imuOk) s_imuRebootMarker = 0;
    // The task starts even if the IMU failed: it keeps trying to recover it.
    IMU.startTask(onImuSample, nullptr);

    if (s_uiReady) {
        UI.begin(Display.disp());
        Display.logBacklightState("UI created");
    }

    StateMachine.setListener(onStateTransition, nullptr);
    StateMachine.begin(Settings.get().countdownS);
    StateMachine.setHealth(!IMU.isHealthy(), Battery.isCritical());

    if (!Ble.begin()) LOG_BLE("ERROR: BLE unavailable - alerts will only show on the watch");
    publishConfig();
    publishTime();

    Power.feedWatchdog();
    LOG_BOOT("Ready (free heap %lu bytes)", (unsigned long)ESP.getFreeHeap());
}

void loop() {
    const uint32_t now = millis();
    Power.feedWatchdog();

    // 1) Fall detector output -> state machine
    FallDetector::Event de;
    while (Detector.pollEvent(de)) handleDetectorEvent(de);

    // 2) Health inputs
    if (Battery.update(now)) s_batteryDirty = true;
    StateMachine.setHealth(!IMU.isHealthy(), Battery.isCritical());
    StateMachine.update(now);

    // 3) Phone commands
    BLEProtocol::Command cmd;
    bool malformed = false;
    while (Ble.pollCommand(cmd, malformed)) {
        if (malformed) sendResult("UNKNOWN", false, "malformed", "");
        else handleCommand(cmd);
    }

    // 4) User input
    UiAction action;
    while (UI.poll(action)) handleUiAction(action);
    if (Power.consumeBootButtonSOS()) StateMachine.manualSOS(Trigger::BUTTON_SOS);
    if (Touch.lastTouchMs() != s_lastTouchSeen) {
        s_lastTouchSeen = Touch.lastTouchMs();
        Power.noteActivity(now);
    }

    // 5) BLE link + outbound packets
    Ble.update(now);
    if (Ble.consumeConnectedEdge()) {
        Events.onConnected();
        publishConfig();
        publishTime();
        s_statusDirty = true;
        s_batteryDirty = true;
    }
    serviceOutbox(now);
    if (s_statusDirty || now - s_lastStatusMs >= BLE_STATUS_INTERVAL_MS) sendStatus(now);
    if (s_batteryDirty || now - s_lastBatteryTxMs >= BLE_BATTERY_INTERVAL_MS) sendBattery(now);

    // 6) Power policy (screen only - sensing never stops)
    Power.update(now, StateMachine.needsAttention(), Settings.get().screenTimeoutS);
    const bool screenOff = Power.isScreenOff();
    if (screenOff && !s_wasScreenOff && s_uiReady) UI.goHome();
    s_wasScreenOff = screenOff;
    Display.update(now);

    // 7) UI
    if (s_uiReady) {
        static UiModel model;
        buildModel(model, now);
        UI.update(model, now, screenOff);
        lv_timer_handler();
    }

    Settings.update(now);

    // 8) Last-resort recovery: an IMU dead for minutes gets one safe reboot,
    //    but never during an alert and never twice in a row.
    if (!IMU.isHealthy() && IMU.faultSinceMs() != 0 && (now - IMU.faultSinceMs()) >= IMU_FAULT_REBOOT_MS &&
        StateMachine.isBaseline() && s_imuRebootMarker != IMU_REBOOT_MAGIC) {
        s_imuRebootMarker = IMU_REBOOT_MAGIC;
        Power.safeReboot("IMU unrecoverable");
    }

    delay(5);  // same pacing as LVGL_Arduino; yields to lower-priority tasks
}
