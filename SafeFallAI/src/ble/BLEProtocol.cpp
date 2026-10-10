#include "BLEProtocol.h"
#include "../config/AppConfig.h"

#include <ctype.h>
#include <stdlib.h>

namespace BLEProtocol {

// ---------------------------------------------------------------------------
// Minimal, allocation-free JSON reader for flat objects:
//   { "key": "string" | number | true | false | null , ... }
// Nested objects/arrays are skipped. Anything malformed fails the parse.
// ---------------------------------------------------------------------------
namespace {

struct Reader {
    const char* p;
    const char* end;

    void ws() { while (p < end && isspace((unsigned char)*p)) p++; }
    bool eat(char c) {
        ws();
        if (p < end && *p == c) { p++; return true; }
        return false;
    }
    // Reads a JSON string into out (truncated to cap-1). Handles \" and \\ escapes.
    bool str(char* out, size_t cap) {
        ws();
        if (p >= end || *p != '"') return false;
        p++;
        size_t n = 0;
        while (p < end && *p != '"') {
            char c = *p++;
            if (c == '\\') {
                if (p >= end) return false;
                char e = *p++;
                c = (e == 'n') ? '\n' : (e == 't') ? '\t' : e;  // \uXXXX not needed by this protocol
            }
            if (out && n + 1 < cap) out[n++] = c;
        }
        if (p >= end) return false;
        p++;  // closing quote
        if (out && cap) out[n] = '\0';
        return true;
    }
    bool skipValue() {
        ws();
        if (p >= end) return false;
        if (*p == '"') return str(nullptr, 0);
        if (*p == '{' || *p == '[') {
            int depth = 0;
            while (p < end) {
                if (*p == '"') { if (!str(nullptr, 0)) return false; continue; }
                if (*p == '{' || *p == '[') depth++;
                if (*p == '}' || *p == ']') { depth--; if (depth == 0) { p++; return true; } }
                p++;
            }
            return false;
        }
        while (p < end && *p != ',' && *p != '}' && !isspace((unsigned char)*p)) p++;
        return true;
    }
    // Scalar token as text (numbers / true / false / null)
    bool token(char* out, size_t cap) {
        ws();
        size_t n = 0;
        while (p < end && *p != ',' && *p != '}' && !isspace((unsigned char)*p)) {
            if (n + 1 < cap) out[n++] = *p;
            p++;
        }
        out[n] = '\0';
        return n > 0;
    }
};

bool toInt(const char* t, int32_t& v) {
    char* e = nullptr;
    long r = strtol(t, &e, 10);
    if (e == t || *e != '\0') return false;
    v = (int32_t)r;
    return true;
}

bool toU32(const char* t, uint32_t& v) {
    if (t[0] == '-') return false;
    char* e = nullptr;
    unsigned long r = strtoul(t, &e, 10);
    if (e == t || *e != '\0') return false;
    v = (uint32_t)r;
    return true;
}

bool toBool(const char* t, bool& v) {
    if (strcmp(t, "true") == 0) { v = true; return true; }
    if (strcmp(t, "false") == 0) { v = false; return true; }
    return false;
}

}  // namespace

const char* cmdName(CmdType t) {
    switch (t) {
        case CmdType::ACK:       return "ACK";
        case CmdType::CANCEL:    return "CANCEL";
        case CmdType::CONFIG:    return "CONFIG";
        case CmdType::TIME_SYNC: return "TIME_SYNC";
        default:                 return "UNKNOWN";
    }
}

bool parseCommand(const char* json, size_t len, Command& out) {
    memset(&out, 0, sizeof(out));
    out.type = CmdType::UNKNOWN;
    Reader r{json, json + len};
    if (!r.eat('{')) return false;

    char type[16] = "";
    if (r.eat('}')) return false;
    for (;;) {
        char key[20];
        if (!r.str(key, sizeof(key))) return false;
        if (!r.eat(':')) return false;
        r.ws();
        if (r.p < r.end && *r.p == '"') {
            char sval[24];
            if (!r.str(sval, sizeof(sval))) return false;
            if (strcmp(key, "type") == 0) strncpy(type, sval, sizeof(type) - 1);
            else if (strcmp(key, "eventId") == 0) strncpy(out.eventId, sval, sizeof(out.eventId) - 1);
        } else if (r.p < r.end && (*r.p == '{' || *r.p == '[')) {
            if (!r.skipValue()) return false;
        } else {
            char tok[16];
            if (!r.token(tok, sizeof(tok))) return false;
            int32_t iv;
            uint32_t uv;
            bool bv;
            if (strcmp(key, "seq") == 0 && toU32(tok, uv)) out.seq = uv;
            else if (strcmp(key, "countdown") == 0 && toInt(tok, iv)) { out.hasCountdown = true; out.countdown = iv; }
            else if (strcmp(key, "sensitivity") == 0 && toInt(tok, iv)) { out.hasSensitivity = true; out.sensitivity = iv; }
            else if (strcmp(key, "fallDetection") == 0 && toBool(tok, bv)) { out.hasFallDetection = true; out.fallDetection = bv; }
            else if (strcmp(key, "screenTimeout") == 0 && toInt(tok, iv)) { out.hasScreenTimeout = true; out.screenTimeout = iv; }
            else if (strcmp(key, "brightness") == 0 && toInt(tok, iv)) { out.hasBrightness = true; out.brightness = iv; }
            else if (strcmp(key, "epoch") == 0 && toU32(tok, uv)) { out.hasEpoch = true; out.epoch = uv; }
            else if (strcmp(key, "tzOffset") == 0 && toInt(tok, iv)) { out.hasTzOffset = true; out.tzOffset = iv; }
        }
        if (r.eat(',')) continue;
        if (r.eat('}')) break;
        return false;
    }

    if (strcmp(type, "ACK") == 0) out.type = CmdType::ACK;
    else if (strcmp(type, "CANCEL") == 0) out.type = CmdType::CANCEL;
    else if (strcmp(type, "CONFIG") == 0) out.type = CmdType::CONFIG;
    else if (strcmp(type, "TIME_SYNC") == 0) out.type = CmdType::TIME_SYNC;
    else return false;

    if ((out.type == CmdType::ACK || out.type == CmdType::CANCEL) && out.eventId[0] == '\0') return false;
    if (out.type == CmdType::TIME_SYNC && !out.hasEpoch) return false;
    return true;
}

// ---------------------------------------------------------------------------
// Builders
// ---------------------------------------------------------------------------
static size_t finish(int n, size_t len) {
    return (n > 0 && (size_t)n < len) ? (size_t)n : 0;
}

size_t buildEvent(char* buf, size_t len, const SafetyEvent& e, uint8_t battery) {
    int n;
    if (e.kind == EventKind::FALL) {
        n = snprintf(buf, len,
                     "{\"type\":\"FALL\",\"eventId\":\"%s\",\"stage\":\"%s\",\"seq\":%lu,\"timestamp\":%lu,"
                     "\"uptime\":%lu,\"confidence\":%u,\"trigger\":\"%s\",\"battery\":%u}",
                     e.id, EventManager::stageName(e.stage), (unsigned long)e.seq, (unsigned long)e.createdEpoch,
                     (unsigned long)e.createdUptimeS, e.confidence, e.trigger, battery);
    } else {
        n = snprintf(buf, len,
                     "{\"type\":\"SOS\",\"eventId\":\"%s\",\"stage\":\"%s\",\"seq\":%lu,\"timestamp\":%lu,"
                     "\"uptime\":%lu,\"trigger\":\"%s\",\"battery\":%u}",
                     e.id, EventManager::stageName(e.stage), (unsigned long)e.seq, (unsigned long)e.createdEpoch,
                     (unsigned long)e.createdUptimeS, e.trigger, battery);
    }
    return finish(n, len);
}

size_t buildStatus(char* buf, size_t len, const StatusInfo& s) {
    int n = snprintf(buf, len,
                     "{\"type\":\"DEVICE_STATUS\",\"state\":\"%s\",\"monitoring\":%s,\"imu\":%s,\"battery\":%u,"
                     "\"timeSynced\":%s,\"uptime\":%lu,\"pending\":%u,\"activeEvent\":\"%s\"}",
                     s.state, s.monitoring ? "true" : "false", s.imuOk ? "true" : "false", s.battery,
                     s.timeSynced ? "true" : "false", (unsigned long)s.uptimeS, s.pending,
                     s.activeEventId ? s.activeEventId : "");
    return finish(n, len);
}

size_t buildBattery(char* buf, size_t len, uint8_t pct, uint16_t mv, bool low, bool critical) {
    int n = snprintf(buf, len, "{\"type\":\"BATTERY\",\"battery\":%u,\"mv\":%u,\"low\":%s,\"critical\":%s}", pct, mv,
                     low ? "true" : "false", critical ? "true" : "false");
    return finish(n, len);
}

size_t buildConfig(char* buf, size_t len, const ConfigInfo& c) {
    int n = snprintf(buf, len,
                     "{\"type\":\"CONFIG\",\"countdown\":%u,\"sensitivity\":%u,\"fallDetection\":%s,"
                     "\"screenTimeout\":%u,\"brightness\":%u,\"protocol\":%u,\"fw\":\"%s\"}",
                     c.countdown, c.sensitivity, c.fallDetection ? "true" : "false", c.screenTimeout, c.brightness,
                     PROTOCOL_VERSION, SAFE_FALL_VERSION);
    return finish(n, len);
}

size_t buildTime(char* buf, size_t len, uint32_t epoch, int32_t tz, bool synced) {
    int n = snprintf(buf, len, "{\"type\":\"TIME\",\"epoch\":%lu,\"tzOffset\":%ld,\"synced\":%s}",
                     (unsigned long)epoch, (long)tz, synced ? "true" : "false");
    return finish(n, len);
}

size_t buildResult(char* buf, size_t len, const char* cmd, bool ok, const char* detail, const char* eventId) {
    int n = snprintf(buf, len, "{\"type\":\"RESULT\",\"cmd\":\"%s\",\"ok\":%s,\"detail\":\"%s\",\"eventId\":\"%s\"}",
                     cmd, ok ? "true" : "false", detail ? detail : "", eventId ? eventId : "");
    return finish(n, len);
}

}  // namespace BLEProtocol
