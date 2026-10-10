/**
 * SafeFall AI - host-side tests for the pure-logic modules.
 *
 * Runs the real FallDetector, FallStateMachine, EventManager and BLEProtocol
 * sources on a PC with stubbed Arduino/FreeRTOS headers, feeding synthetic
 * 100 Hz IMU traces. Synthetic traces check the algorithm's logic and
 * thresholds; they are NOT a substitute for testing with recorded real falls.
 *
 *   cd extras/host_tests && make
 */
#include "Arduino.h"
#include "../../src/fall/FallDetector.h"
#include "../../src/fall/FallStateMachine.h"
#include "../../src/services/EventManager.h"
#include "../../src/services/SettingsStore.h"
#include "../../src/ble/BLEProtocol.h"

#include <string>
#include <vector>

uint32_t g_millis = 1000;
HostSerial Serial;

// --- SettingsStore stub: only the event counter is used by EventManager ---
SettingsStore Settings;
static uint32_t s_nvsCounter = 0;
static int s_nvsWrites = 0;
uint32_t SettingsStore::loadEventCounter() { return s_nvsCounter; }
void SettingsStore::saveEventCounter(uint32_t v) { s_nvsCounter = v; s_nvsWrites++; }

// ---------------------------------------------------------------------------
static int s_failures = 0;
static int s_checks = 0;
#define CHECK(cond, ...)                                                          \
    do {                                                                          \
        s_checks++;                                                               \
        if (!(cond)) {                                                            \
            s_failures++;                                                         \
            std::printf("  FAIL %s:%d: %s  -- ", __FILE__, __LINE__, #cond);      \
            std::printf(__VA_ARGS__);                                             \
            std::printf("\n");                                                    \
        }                                                                         \
    } while (0)

// --- synthetic IMU trace helpers -------------------------------------------
static uint32_t s_rng = 12345;
static float noise(float amp) {
    s_rng = s_rng * 1103515245u + 12345u;
    return amp * (((s_rng >> 8) & 0xFFFF) / 32768.0f - 1.0f);
}

struct Collected {
    std::vector<FallDetector::Event> events;
    int count(FallDetector::EventType t) const {
        int n = 0;
        for (auto& e : events) n += (e.type == t);
        return n;
    }
    const FallDetector::Event* last(FallDetector::EventType t) const {
        for (auto it = events.rbegin(); it != events.rend(); ++it)
            if (it->type == t) return &*it;
        return nullptr;
    }
};

static void drain(FallDetector& d, Collected& c) {
    FallDetector::Event e;
    while (d.pollEvent(e)) c.events.push_back(e);
}

static void feed(FallDetector& d, Collected& c, float ax, float ay, float az, float gyro, uint32_t ms,
                 float accNoise = 0.01f, float gyroNoise = 2.0f) {
    for (uint32_t t = 0; t < ms; t += 10) {
        g_millis += 10;
        ImuSample s;
        s.ms = g_millis;
        s.ax = ax + noise(accNoise);
        s.ay = ay + noise(accNoise);
        s.az = az + noise(accNoise);
        s.gx = gyro + noise(gyroNoise);
        s.gy = noise(gyroNoise);
        s.gz = noise(gyroNoise);
        d.process(s);
        drain(d, c);
    }
}

// Walking-like motion: |a| oscillates around 1 g, moderate rotation
static void walk(FallDetector& d, Collected& c, uint32_t ms, float amp = 0.35f) {
    for (uint32_t t = 0; t < ms; t += 10) {
        g_millis += 10;
        const float ph = (float)t / 1000.0f * 2.0f * 3.14159f * 1.8f;
        ImuSample s;
        s.ms = g_millis;
        s.ax = 0.2f * sinf(ph * 0.5f) + noise(0.03f);
        s.ay = 0.1f + noise(0.03f);
        s.az = 1.0f + amp * sinf(ph) + noise(0.03f);
        s.gx = 80.0f * sinf(ph) + noise(5);
        s.gy = 40.0f * cosf(ph) + noise(5);
        s.gz = noise(5);
        d.process(s);
        drain(d, c);
    }
}

static void freshDetector(FallDetector& d, Sensitivity sens = Sensitivity::NORMAL) {
    d = FallDetector();
    d.setSensitivity(sens);
    d.setEnabled(true);
    d.begin();
}

// Canonical forward fall: upright -> free-fall with rotation -> impact -> lying still on the side
static void simulateFall(FallDetector& d, Collected& c, float impactG, bool freeFall, float lieAngleDeg,
                         uint32_t stillMs = 7000) {
    feed(d, c, 0, 0, 1.0f, 0, 2500);                         // standing still
    if (freeFall) feed(d, c, 0.1f, 0, 0.3f, 280, 300);       // ~300 ms free fall, rotating
    else feed(d, c, 0.3f, 0, 0.8f, 200, 300);                // stumble, no free fall
    feed(d, c, 0.5f * impactG, 0, 0.87f * impactG, 300, 40);  // impact spike
    feed(d, c, 0.6f, 0, 0.9f, 120, 300);                     // bounce
    const float r = lieAngleDeg * 3.14159f / 180.0f;
    feed(d, c, sinf(r), 0, cosf(r), 0, stillMs, 0.02f, 3.0f);  // lying still
}

// ---------------------------------------------------------------------------
static void testDetector() {
    std::printf("FallDetector\n");
    using ET = FallDetector::EventType;
    FallDetector d;
    Collected c;

    freshDetector(d);
    feed(d, c, 0, 0, 1.0f, 0, 5000);
    CHECK(c.events.empty(), "rest produced %zu events", c.events.size());

    freshDetector(d); c = Collected();
    walk(d, c, 20000);
    CHECK(c.count(ET::IMPACT_CANDIDATE) == 0, "walking produced %d candidates", c.count(ET::IMPACT_CANDIDATE));

    freshDetector(d); c = Collected();
    simulateFall(d, c, 4.5f, true, 90);
    CHECK(c.count(ET::IMPACT_CANDIDATE) == 1, "candidates=%d", c.count(ET::IMPACT_CANDIDATE));
    CHECK(c.count(ET::FALL_CONFIRMED) == 1, "confirmed=%d", c.count(ET::FALL_CONFIRMED));
    if (auto* e = c.last(ET::FALL_CONFIRMED)) {
        std::printf("  classic fall: confidence %u, peak %.1fg, orientation %.0f deg, still %.0f%%\n",
                    e->confidence, e->peakG, e->orientationDeg, e->stillRatio * 100);
        CHECK(e->confidence >= 75, "confidence %u", e->confidence);
        CHECK(e->freeFall, "free fall not detected");
        CHECK(e->orientationDeg > 70, "orientation %.0f", e->orientationDeg);
    }

    freshDetector(d); c = Collected();
    simulateFall(d, c, 3.5f, false, 70);
    CHECK(c.count(ET::FALL_CONFIRMED) == 1, "fall without free-fall not confirmed");
    if (auto* e = c.last(ET::FALL_CONFIRMED)) std::printf("  stumble fall: confidence %u\n", e->confidence);

    // Table slap: impact but no orientation change, no rotation -> rejected
    freshDetector(d); c = Collected();
    feed(d, c, 0, 0, 1.0f, 0, 2500);
    feed(d, c, 0, 0, 3.0f, 20, 30);
    feed(d, c, 0, 0, 1.0f, 0, 7000);
    CHECK(c.count(ET::IMPACT_CANDIDATE) == 1, "slap candidates=%d", c.count(ET::IMPACT_CANDIDATE));
    CHECK(c.count(ET::FALL_CONFIRMED) == 0, "slap was confirmed as a fall");
    if (auto* e = c.last(ET::CANDIDATE_REJECTED)) {
        CHECK(e->reason == FallDetector::RejectReason::LOW_CONFIDENCE, "reason %s",
              FallDetector::reasonName(e->reason));
        std::printf("  table slap: rejected, confidence %u\n", e->confidence);
    }

    // Moderate impact, then the user walks on -> early abort (user moving)
    freshDetector(d); c = Collected();
    feed(d, c, 0, 0, 1.0f, 0, 2500);
    feed(d, c, 0.3f, 0, 0.8f, 150, 300);
    feed(d, c, 1.0f, 0, 2.8f, 150, 40);
    feed(d, c, 0.3f, 0, 0.95f, 100, 1500);
    walk(d, c, 4000, 0.8f);
    CHECK(c.count(ET::FALL_CONFIRMED) == 0, "walking user confirmed");
    if (auto* e = c.last(ET::CANDIDATE_REJECTED)) {
        CHECK(e->reason == FallDetector::RejectReason::USER_MOVING, "reason %s", FallDetector::reasonName(e->reason));
    }

    // Hard fall, but the person gets straight up and walks away -> rejected
    freshDetector(d); c = Collected();
    feed(d, c, 0, 0, 1.0f, 0, 2500);
    feed(d, c, 0.1f, 0, 0.3f, 280, 300);
    feed(d, c, 2.0f, 0, 4.0f, 300, 40);
    feed(d, c, 0.6f, 0, 0.9f, 120, 1500);
    walk(d, c, 4500, 0.8f);
    CHECK(c.count(ET::FALL_CONFIRMED) == 0, "recovered user confirmed after a hard fall");

    // Hard fall, person conscious on the floor moving an arm -> must still alert
    freshDetector(d); c = Collected();
    feed(d, c, 0, 0, 1.0f, 0, 2500);
    feed(d, c, 0.1f, 0, 0.3f, 280, 300);
    feed(d, c, 2.3f, 0, 4.0f, 300, 40);
    feed(d, c, 0.9f, 0, 0.4f, 120, 300);
    for (int i = 0; i < 7; i++) {
        feed(d, c, 1.0f, 0, 0.05f, 0, 600, 0.02f, 3.0f);   // lying still
        feed(d, c, 1.0f, 0.3f, 0.2f, 220, 400, 0.3f, 40);  // waving / trying to move
    }
    CHECK(c.count(ET::FALL_CONFIRMED) == 1, "conscious-on-floor fall not confirmed");
    if (auto* e = c.last(ET::FALL_CONFIRMED)) {
        std::printf("  fall + arm movement on floor: confidence %u, still %.0f%%\n", e->confidence, e->stillRatio * 100);
    }

    // Repetitive impacts (clapping / hammering): third impact within window is ignored
    freshDetector(d); c = Collected();
    feed(d, c, 0, 0, 1.0f, 0, 2500);
    for (int i = 0; i < 5; i++) {
        feed(d, c, 0, 0, 3.2f, 150, 30);
        feed(d, c, 0.3f, 0, 1.2f, 150, 400, 0.3f, 60);
    }
    feed(d, c, 0, 0, 1.0f, 0, 7000);
    CHECK(c.count(ET::FALL_CONFIRMED) == 0, "clapping confirmed as a fall");

    // Vibration / sustained high g -> LONG_HIGH_G
    freshDetector(d); c = Collected();
    feed(d, c, 0, 0, 1.0f, 0, 2500);
    feed(d, c, 0, 0, 3.0f, 50, 700, 0.2f, 20);
    feed(d, c, 0, 0, 1.0f, 0, 6000);
    CHECK(c.count(ET::FALL_CONFIRMED) == 0, "vibration confirmed");
    if (auto* e = c.last(ET::CANDIDATE_REJECTED)) {
        CHECK(e->reason == FallDetector::RejectReason::LONG_HIGH_G, "reason %s", FallDetector::reasonName(e->reason));
    }

    // Disabled detector emits nothing
    freshDetector(d); c = Collected();
    d.setEnabled(false);
    simulateFall(d, c, 5.0f, true, 90);
    CHECK(c.events.empty(), "paused detector produced %zu events", c.events.size());

    // Cooldown: a second impact right after a confirmed fall is not a new fall
    freshDetector(d); c = Collected();
    simulateFall(d, c, 4.5f, true, 90);
    feed(d, c, 1.0f, 0, 4.0f, 300, 40);
    feed(d, c, 1.0f, 0, 0.0f, 0, 7000);
    CHECK(c.count(ET::FALL_CONFIRMED) == 1, "duplicate confirmation inside cooldown: %d", c.count(ET::FALL_CONFIRMED));

    // Sensitivity: a soft fall (2.2 g) only triggers on HIGH sensitivity
    freshDetector(d, Sensitivity::NORMAL); c = Collected();
    simulateFall(d, c, 2.2f, true, 90);
    CHECK(c.count(ET::IMPACT_CANDIDATE) == 0, "2.2g triggered on NORMAL");
    freshDetector(d, Sensitivity::HIGH_SENS); c = Collected();
    simulateFall(d, c, 2.2f, true, 90);
    CHECK(c.count(ET::FALL_CONFIRMED) == 1, "2.2g fall not confirmed on HIGH");
}

// ---------------------------------------------------------------------------
struct TransitionLog {
    std::vector<std::pair<DeviceState, Trigger>> t;
};
static void onT(DeviceState, DeviceState to, Trigger why, void* ctx) {
    static_cast<TransitionLog*>(ctx)->t.push_back({to, why});
}

static void testStateMachine() {
    std::printf("FallStateMachine\n");
    FallStateMachine sm;
    TransitionLog log;
    sm.setListener(onT, &log);
    sm.begin(10);
    CHECK(sm.state() == DeviceState::NORMAL, "start %s", FallStateMachine::stateName(sm.state()));

    sm.onImpactCandidate();
    CHECK(sm.state() == DeviceState::POSSIBLE_FALL, "%s", FallStateMachine::stateName(sm.state()));
    sm.onFallRejected();
    CHECK(sm.state() == DeviceState::NORMAL, "%s", FallStateMachine::stateName(sm.state()));

    // Fall -> countdown -> I'M OK
    sm.onImpactCandidate();
    sm.onFallConfirmed(80);
    CHECK(sm.state() == DeviceState::FALL_DETECTED, "%s", FallStateMachine::stateName(sm.state()));
    sm.update(g_millis);
    CHECK(sm.state() == DeviceState::AWAITING_CONFIRMATION, "%s", FallStateMachine::stateName(sm.state()));
    g_millis += 3000;
    CHECK(sm.countdownRemaining(g_millis) == 7, "remaining %u", sm.countdownRemaining(g_millis));
    CHECK(sm.userOk(), "userOk rejected");
    CHECK(sm.state() == DeviceState::USER_CANCELLED, "%s", FallStateMachine::stateName(sm.state()));
    g_millis += 3000;
    sm.update(g_millis);
    CHECK(sm.state() == DeviceState::NORMAL, "%s", FallStateMachine::stateName(sm.state()));

    // Fall -> countdown expires -> EMERGENCY -> hold cancel
    sm.onFallConfirmed(70);
    sm.update(g_millis);
    g_millis += 9999;
    sm.update(g_millis);
    CHECK(sm.state() == DeviceState::AWAITING_CONFIRMATION, "expired early");
    g_millis += 2;
    sm.update(g_millis);
    CHECK(sm.state() == DeviceState::EMERGENCY, "%s", FallStateMachine::stateName(sm.state()));
    CHECK(sm.lastTrigger() == Trigger::COUNTDOWN_EXPIRED, "trigger %s", FallStateMachine::triggerName(sm.lastTrigger()));
    g_millis += 600000;
    sm.update(g_millis);
    CHECK(sm.state() == DeviceState::EMERGENCY, "emergency must not time out on its own");
    sm.onFallConfirmed(90);  // new detections ignored during an emergency
    CHECK(sm.state() == DeviceState::EMERGENCY, "%s", FallStateMachine::stateName(sm.state()));
    CHECK(sm.userCancelEmergency(), "cancel rejected");
    g_millis += 3000;
    sm.update(g_millis);
    CHECK(sm.state() == DeviceState::NORMAL, "%s", FallStateMachine::stateName(sm.state()));

    // SOS from the alert escalates the same fall
    sm.onFallConfirmed(65);
    sm.update(g_millis);
    CHECK(sm.manualSOS(Trigger::SCREEN_SOS), "manual sos during alert");
    CHECK(sm.state() == DeviceState::EMERGENCY, "%s", FallStateMachine::stateName(sm.state()));
    CHECK(sm.remoteCancel(), "remote cancel");
    CHECK(sm.lastTrigger() == Trigger::REMOTE_CANCEL, "trigger");
    g_millis += 3000;
    sm.update(g_millis);

    // Manual SOS from idle
    CHECK(sm.manualSOS(Trigger::BUTTON_SOS), "manual sos");
    CHECK(sm.state() == DeviceState::SOS_ACTIVE, "%s", FallStateMachine::stateName(sm.state()));
    CHECK(!sm.manualSOS(Trigger::SCREEN_SOS), "double SOS accepted");
    CHECK(!sm.userOk(), "I'M OK must not cancel an SOS");
    sm.userCancelEmergency();
    g_millis += 3000;
    sm.update(g_millis);

    // Health: sensor fault and low battery select the baseline
    sm.setHealth(false, true);
    CHECK(sm.state() == DeviceState::LOW_BATTERY, "%s", FallStateMachine::stateName(sm.state()));
    sm.setHealth(true, true);
    CHECK(sm.state() == DeviceState::DEVICE_ERROR, "%s", FallStateMachine::stateName(sm.state()));
    CHECK(sm.manualSOS(Trigger::SCREEN_SOS), "SOS must work with a sensor fault");
    sm.setHealth(true, true);
    CHECK(sm.state() == DeviceState::SOS_ACTIVE, "health change must not override an SOS");
    sm.userCancelEmergency();
    g_millis += 3000;
    sm.update(g_millis);
    CHECK(sm.state() == DeviceState::DEVICE_ERROR, "%s", FallStateMachine::stateName(sm.state()));
    sm.setHealth(false, false);
    CHECK(sm.state() == DeviceState::NORMAL, "%s", FallStateMachine::stateName(sm.state()));

    // POSSIBLE_FALL safety timeout
    sm.onImpactCandidate();
    g_millis += FALL_POSSIBLE_TIMEOUT_MS + 1;
    sm.update(g_millis);
    CHECK(sm.state() == DeviceState::NORMAL, "possible-fall timeout: %s", FallStateMachine::stateName(sm.state()));

    CHECK(!log.t.empty(), "listener never called");
}

// ---------------------------------------------------------------------------
static bool jsonLooksValid(const char* s) {
    // Structural sanity: balanced braces/quotes, starts with { ends with }
    size_t n = strlen(s);
    if (n < 2 || s[0] != '{' || s[n - 1] != '}') return false;
    int quotes = 0;
    for (size_t i = 0; i < n; i++) quotes += (s[i] == '"');
    return quotes % 2 == 0;
}

static void testEventsAndProtocol() {
    std::printf("EventManager + BLEProtocol\n");
    using namespace BLEProtocol;
    s_nvsCounter = 41;
    EventManager ev;
    ev.begin();
    SafetyEvent* e = ev.create(EventKind::FALL, EventStage::DETECTED, 87, "DETECTOR", 1728550000, 1234);
    CHECK(strcmp(e->id, "SF-000042") == 0, "id %s", e->id);
    CHECK(s_nvsCounter == 42 && s_nvsWrites >= 1, "counter not persisted");
    CHECK(ev.pendingCount() == 1, "pending %u", ev.pendingCount());

    SafetyEvent* tx = ev.nextToSend(g_millis);
    CHECK(tx == e, "nextToSend");
    ev.markSent(tx, g_millis);
    CHECK(ev.nextToSend(g_millis + 100) == nullptr, "retry too early");
    CHECK(ev.nextToSend(g_millis + BLE_EVENT_RETRY_MS) == e, "retry not scheduled");

    const uint32_t seq1 = e->seq;
    ev.updateStage(e->id, EventStage::EMERGENCY, "TIMEOUT");
    CHECK(ev.onAck(e->id, seq1) == EventManager::AckResult::STALE, "stale ack accepted");
    CHECK(ev.onAck(e->id, e->seq) == EventManager::AckResult::OK, "ack");
    CHECK(ev.onAck(e->id, e->seq) == EventManager::AckResult::DUPLICATE, "duplicate ack");
    CHECK(ev.onAck("SF-999999", 0) == EventManager::AckResult::UNKNOWN, "unknown ack");
    CHECK(ev.pendingCount() == 0, "pending after ack");

    char buf[BLE_MAX_PACKET_LEN + 1];
    size_t n = buildEvent(buf, sizeof(buf), *e, 82);
    std::printf("  %s\n", buf);
    CHECK(n > 0 && jsonLooksValid(buf), "event json");

    // Worst-case lengths must fit in one notification (MTU >= BLE_MAX_PACKET_LEN + 3)
    SafetyEvent w = *e;
    strcpy(w.id, "SF-999999");
    w.seq = 4294967295u; w.createdEpoch = 4294967295u; w.createdUptimeS = 4294967295u; w.confidence = 100;
    w.stage = EventStage::EMERGENCY; w.trigger = "USER_CANCEL";
    n = buildEvent(buf, sizeof(buf), w, 100);
    std::printf("  worst FALL packet: %zu bytes\n", n);
    CHECK(n > 0 && n <= BLE_MAX_PACKET_LEN, "FALL packet %zu", n);
    w.kind = EventKind::SOS;
    n = buildEvent(buf, sizeof(buf), w, 100);
    CHECK(n > 0 && n <= BLE_MAX_PACKET_LEN, "SOS packet %zu", n);
    StatusInfo si = {"AWAITING_CONFIRMATION", true, true, 100, true, 4294967295u, 8, "SF-999999"};
    n = buildStatus(buf, sizeof(buf), si);
    std::printf("  worst STATUS packet: %zu bytes\n", n);
    CHECK(n > 0 && n <= BLE_MAX_PACKET_LEN && jsonLooksValid(buf), "STATUS packet %zu", n);
    n = buildBattery(buf, sizeof(buf), 100, 4200, false, false);
    CHECK(n > 0 && jsonLooksValid(buf), "battery");
    ConfigInfo ci = {60, 2, true, 120, 100};
    n = buildConfig(buf, sizeof(buf), ci);
    CHECK(n > 0 && jsonLooksValid(buf), "config");
    n = buildResult(buf, sizeof(buf), "TIME_SYNC", false, "invalid time", "SF-999999");
    CHECK(n > 0 && n <= BLE_MAX_PACKET_LEN, "result %zu", n);

    // Parsing
    Command c;
    const char* ack = "{\"type\":\"ACK\",\"eventId\":\"SF-000042\",\"seq\":7}";
    CHECK(parseCommand(ack, strlen(ack), c) && c.type == CmdType::ACK && c.seq == 7 &&
              strcmp(c.eventId, "SF-000042") == 0, "ack parse");
    const char* cancel = " { \"eventId\" : \"SF-000042\" , \"type\" : \"CANCEL\" } ";
    CHECK(parseCommand(cancel, strlen(cancel), c) && c.type == CmdType::CANCEL, "cancel parse (order/whitespace)");
    const char* cfg = "{\"type\":\"CONFIG\",\"countdown\":15,\"sensitivity\":2,\"fallDetection\":false,"
                      "\"screenTimeout\":30,\"brightness\":60,\"extra\":{\"a\":[1,2,{\"b\":\"}\"}]}}";
    CHECK(parseCommand(cfg, strlen(cfg), c) && c.type == CmdType::CONFIG && c.countdown == 15 &&
              c.sensitivity == 2 && c.hasFallDetection && !c.fallDetection && c.brightness == 60 &&
              c.screenTimeout == 30, "config parse");
    const char* ts = "{\"type\":\"TIME_SYNC\",\"epoch\":1728550000,\"tzOffset\":-18000}";
    CHECK(parseCommand(ts, strlen(ts), c) && c.type == CmdType::TIME_SYNC && c.epoch == 1728550000u &&
              c.tzOffset == -18000, "time parse");

    const char* bad[] = {
        "", "{", "{}", "[]", "{\"type\":\"ACK\"}", "{\"type\":\"NOPE\",\"eventId\":\"x\"}",
        "{\"type\":\"ACK\",\"eventId\":\"SF-1\"", "{\"type\":\"TIME_SYNC\"}", "{\"type\":ACK}",
        "{\"type\":\"ACK\" \"eventId\":\"SF-1\"}", "{\"type\":\"TIME_SYNC\",\"epoch\":-5}",
    };
    for (const char* b : bad) {
        CHECK(!parseCommand(b, strlen(b), c), "malformed accepted: %s", b);
    }

    // Remote-cancel de-duplication
    ev.rememberRemoteCancel("SF-000042");
    CHECK(ev.wasRemoteCancelled("SF-000042") && !ev.wasRemoteCancelled("SF-000043"), "cancel dedupe");

    // Outbox overflow keeps the newest undelivered events
    for (int i = 0; i < EVENT_OUTBOX_SIZE + 3; i++) ev.create(EventKind::SOS, EventStage::ACTIVE, 0, "SCREEN", 0, 0);
    CHECK(ev.pendingCount() == EVENT_OUTBOX_SIZE, "outbox pending %u", ev.pendingCount());
    CHECK(ev.find(ev.lastEventId()) != nullptr, "newest event evicted");
}

int main() {
    testDetector();
    testStateMachine();
    testEventsAndProtocol();
    std::printf("\n%d checks, %d failures\n", s_checks, s_failures);
    return s_failures == 0 ? 0 : 1;
}
