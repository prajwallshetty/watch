/**
 * SafeFall AI - BLEProtocol.h
 *
 * GATT layout and compact-JSON packet format. Full documentation with
 * examples: docs/BLE_PROTOCOL.md.
 *
 * Service  5AFE1000-7C3A-4F8E-9B2D-1E6A4C8F0D21
 *   STATUS   ...1001  read/notify   DEVICE_STATUS, RESULT          watch -> phone
 *   BATTERY  ...1002  read/notify   BATTERY                        watch -> phone
 *   FALL     ...1003  read/notify   FALL events                    watch -> phone
 *   SOS      ...1004  read/notify   SOS events                     watch -> phone
 *   ACK      ...1005  write         ACK, CANCEL                    phone -> watch
 *   CONFIG   ...1006  read/write    CONFIG                         both
 *   TIME     ...1007  read/write    TIME_SYNC                      both
 *
 * Packets are single JSON objects of at most BLE_MAX_PACKET_LEN (180) bytes,
 * so the phone needs an ATT MTU of at least 183. iOS negotiates 185 by itself;
 * Android apps must call requestMtu(247) after connecting (default is 23).
 */
#pragma once

#include <Arduino.h>
#include "../services/EventManager.h"

namespace BLEProtocol {

static const char* const SERVICE_UUID   = "5afe1000-7c3a-4f8e-9b2d-1e6a4c8f0d21";
static const char* const CHAR_STATUS    = "5afe1001-7c3a-4f8e-9b2d-1e6a4c8f0d21";
static const char* const CHAR_BATTERY   = "5afe1002-7c3a-4f8e-9b2d-1e6a4c8f0d21";
static const char* const CHAR_FALL      = "5afe1003-7c3a-4f8e-9b2d-1e6a4c8f0d21";
static const char* const CHAR_SOS       = "5afe1004-7c3a-4f8e-9b2d-1e6a4c8f0d21";
static const char* const CHAR_ACK       = "5afe1005-7c3a-4f8e-9b2d-1e6a4c8f0d21";
static const char* const CHAR_CONFIG    = "5afe1006-7c3a-4f8e-9b2d-1e6a4c8f0d21";
static const char* const CHAR_TIME      = "5afe1007-7c3a-4f8e-9b2d-1e6a4c8f0d21";

static const uint8_t PROTOCOL_VERSION = 1;

enum class CmdType : uint8_t { UNKNOWN, ACK, CANCEL, CONFIG, TIME_SYNC };

struct Command {
    CmdType type;
    char eventId[12];
    uint32_t seq;

    bool hasCountdown;      int32_t countdown;
    bool hasSensitivity;    int32_t sensitivity;
    bool hasFallDetection;  bool fallDetection;
    bool hasScreenTimeout;  int32_t screenTimeout;
    bool hasBrightness;     int32_t brightness;

    bool hasEpoch;          uint32_t epoch;
    bool hasTzOffset;       int32_t tzOffset;
};

struct StatusInfo {
    const char* state;
    bool monitoring;
    bool imuOk;
    uint8_t battery;
    bool timeSynced;
    uint32_t uptimeS;
    uint8_t pending;
    const char* activeEventId;  // "" when none
};

struct ConfigInfo {
    uint8_t countdown;
    uint8_t sensitivity;
    bool fallDetection;
    uint8_t screenTimeout;
    uint8_t brightness;
};

// Parsing (phone -> watch). Returns false for malformed JSON or unknown type.
bool parseCommand(const char* json, size_t len, Command& out);
const char* cmdName(CmdType t);

// Building (watch -> phone). Each returns the length written (0 on overflow).
size_t buildEvent(char* buf, size_t len, const SafetyEvent& e, uint8_t battery);
size_t buildStatus(char* buf, size_t len, const StatusInfo& s);
size_t buildBattery(char* buf, size_t len, uint8_t pct, uint16_t mv, bool low, bool critical);
size_t buildConfig(char* buf, size_t len, const ConfigInfo& c);
size_t buildTime(char* buf, size_t len, uint32_t epoch, int32_t tz, bool synced);
size_t buildResult(char* buf, size_t len, const char* cmd, bool ok, const char* detail, const char* eventId);

}  // namespace BLEProtocol
