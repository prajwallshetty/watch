# SafeFall AI – BLE Protocol (v1)

The watch is a BLE GATT **peripheral** advertising as **`SafeFall-AI`**. A phone
companion app connects as the central, subscribes to notifications, and
acknowledges safety events.

> SafeFall AI is an engineering prototype, not a medical device. The phone app
> must not present alerts as guaranteed or clinically validated.

## 1. Advertising

| Field | Value |
|---|---|
| Advertising data | Flags `0x06` + complete 128-bit service UUID |
| Scan response | Complete local name `SafeFall-AI` |
| Connections | 1 phone at a time; advertising restarts 0.5 s after a disconnect |
| Security | None (prototype). Add bonding before any real-world use. |

Scan with a service-UUID filter on `5AFE1000-7C3A-4F8E-9B2D-1E6A4C8F0D21`.

## 2. GATT layout

Service **`5AFE1000-7C3A-4F8E-9B2D-1E6A4C8F0D21`**

| Characteristic | UUID | Properties | Direction | Carries |
|---|---|---|---|---|
| STATUS  | `5AFE1001-7C3A-4F8E-9B2D-1E6A4C8F0D21` | read, notify | watch → phone | `DEVICE_STATUS`, `RESULT` |
| BATTERY | `5AFE1002-7C3A-4F8E-9B2D-1E6A4C8F0D21` | read, notify | watch → phone | `BATTERY` |
| FALL    | `5AFE1003-7C3A-4F8E-9B2D-1E6A4C8F0D21` | read, notify | watch → phone | `FALL` events |
| SOS     | `5AFE1004-7C3A-4F8E-9B2D-1E6A4C8F0D21` | read, notify | watch → phone | `SOS` events |
| ACK     | `5AFE1005-7C3A-4F8E-9B2D-1E6A4C8F0D21` | write, write-without-response | phone → watch | `ACK`, `CANCEL` |
| CONFIG  | `5AFE1006-7C3A-4F8E-9B2D-1E6A4C8F0D21` | read, write | both | `CONFIG` |
| TIME    | `5AFE1007-7C3A-4F8E-9B2D-1E6A4C8F0D21` | read, write | both | `TIME_SYNC` / `TIME` |

The standard **Battery Service** (`0x180F`, Battery Level `0x2A19`, read/notify,
1 byte 0–100) is also exposed.

Notify characteristics carry a CCCD (`0x2902`); the phone must enable
notifications on STATUS, BATTERY, FALL and SOS after connecting.

The watch routes incoming writes by their `"type"` field, so writing any
command to any writable characteristic works; the table shows the intended one.

## 3. Packet format

* UTF-8 compact JSON, **one object per ATT write/notification**.
* Maximum length **180 bytes** (`BLE_MAX_PACKET_LEN`). The ATT MTU must be at
  least **183**. iOS negotiates 185 automatically; **Android apps must call
  `requestMtu(247)`** right after connecting (the default MTU of 23 truncates
  packets).
* Integers are decimal; booleans are `true`/`false`. Unknown keys are ignored.
* `timestamp` is UTC epoch seconds, or `0` if the watch clock has not been set
  (see `TIME_SYNC`). `uptime` is seconds since the watch booted.

## 4. Watch → phone

### 4.1 `FALL` (FALL characteristic)

```json
{"type":"FALL","eventId":"SF-000001","stage":"EMERGENCY","seq":12,"timestamp":1728550000,"uptime":5321,"confidence":87,"trigger":"TIMEOUT","battery":82}
```

| Field | Meaning |
|---|---|
| `eventId` | Unique ID `SF-NNNNNN`. Persistent counter in NVS: never reused across reboots. |
| `stage` | `DETECTED` → fall confirmed by the algorithm, watch is asking "Are you OK?"<br>`EMERGENCY` → no response before the countdown, or the user pressed SOS<br>`CANCELLED` → the user pressed "I'M OK" or held "cancel" on the watch<br>`RESOLVED` → cancelled by the phone (`CANCEL` command) |
| `seq` | Increments on every stage change (global counter, resets on reboot). Echo it in the `ACK`. |
| `timestamp` | Event creation time (UTC epoch s) or 0 |
| `uptime` | Event creation time (s since boot) |
| `confidence` | Detector confidence 0–100 |
| `trigger` | `DETECTOR`, `TIMEOUT` (countdown expired), `USER_SOS`, `USER_OK`, `USER_CANCEL`, `PHONE` |
| `battery` | Battery % at transmission |

Typical sequences for one `eventId`:

```
DETECTED ──► CANCELLED (USER_OK)                      user is fine
DETECTED ──► EMERGENCY (TIMEOUT|USER_SOS) ──► CANCELLED (USER_CANCEL) | RESOLVED (PHONE)
```

Recommended phone behaviour: show a pending state on `DETECTED`; start the
caregiver workflow (location, notifications, calls) on `EMERGENCY`.

### 4.2 `SOS` (SOS characteristic)

```json
{"type":"SOS","eventId":"SF-000002","stage":"ACTIVE","seq":13,"timestamp":1728550100,"uptime":5421,"trigger":"SCREEN","battery":81}
```

`stage`: `ACTIVE` → `CANCELLED` (`USER_CANCEL`) or `RESOLVED` (`PHONE`).
`trigger`: `SCREEN` (hold the SOS button) or `BOOT_KEY` (hold the BOOT key 3 s).

### 4.3 `DEVICE_STATUS` (STATUS characteristic)

Sent on every state change, after each ACK, on connect, and every 30 s.

```json
{"type":"DEVICE_STATUS","state":"NORMAL","monitoring":true,"imu":true,"battery":82,"timeSynced":true,"uptime":5400,"pending":0,"activeEvent":""}
```

`state` is one of `NORMAL`, `POSSIBLE_FALL`, `FALL_DETECTED`,
`AWAITING_CONFIRMATION`, `USER_CANCELLED`, `EMERGENCY`, `SOS_ACTIVE`,
`LOW_BATTERY`, `DEVICE_ERROR`. `monitoring` is false when the user paused fall
detection; `imu` false means the motion sensor failed (fall detection
unavailable, `state` is `DEVICE_ERROR`). `pending` = events not yet ACKed.

### 4.4 `BATTERY` (BATTERY characteristic)

Sent on change, on connect, and every 60 s.

```json
{"type":"BATTERY","battery":82,"mv":3950,"low":false,"critical":false}
```

`low` ≤ 20 %, `critical` ≤ 10 % (3 % hysteresis).

### 4.5 `RESULT` (STATUS characteristic)

Reply to `CANCEL`, `CONFIG`, `TIME_SYNC` and to malformed writes:

```json
{"type":"RESULT","cmd":"CANCEL","ok":true,"detail":"cancelled","eventId":"SF-000001"}
```

`detail` values: `cancelled`, `duplicate`, `not active`, `applied`, `synced`,
`invalid time`, `malformed` (with `cmd` = `UNKNOWN`).

## 5. Phone → watch

### 5.1 `ACK` (ACK characteristic)

```json
{"type":"ACK","eventId":"SF-000001","seq":12}
```

Acknowledges the stage identified by `seq`. `seq` may be omitted (or 0) to
acknowledge the latest stage. An ACK carrying an older `seq` is ignored, so a
late ACK can never mark a newer stage as delivered.

### 5.2 `CANCEL` (ACK characteristic)

```json
{"type":"CANCEL","eventId":"SF-000001"}
```

Ends the active alert from the phone (caregiver resolved it). Works while the
watch is counting down, in a fall `EMERGENCY` or in `SOS_ACTIVE`. The watch
moves to `USER_CANCELLED`, sends the event with stage `RESOLVED`, and replies
`RESULT`. A repeated `CANCEL` for the same ID returns `duplicate`.

### 5.3 `CONFIG` (CONFIG characteristic)

All fields optional; values are clamped to the allowed range.

```json
{"type":"CONFIG","countdown":10,"sensitivity":1,"fallDetection":true,"screenTimeout":20,"brightness":80}
```

| Field | Range | Meaning |
|---|---|---|
| `countdown` | 5–60 s | "Are you OK?" countdown |
| `sensitivity` | 0 low, 1 normal, 2 high | impact threshold ×1.25 / ×1 / ×0.8 and confidence threshold +10 / 0 / −10 |
| `fallDetection` | bool | pause/resume fall detection |
| `screenTimeout` | 10–120 s | display off after inactivity |
| `brightness` | 10–100 % | backlight |

Reading CONFIG returns the current values plus `"protocol":1` and `"fw"`.

### 5.4 `TIME_SYNC` (TIME characteristic)

```json
{"type":"TIME_SYNC","epoch":1728550000,"tzOffset":19800}
```

`epoch` = UTC seconds (must be ≥ 1700000000). `tzOffset` = local offset from
UTC in seconds (−50400 … 50400), optional. The board has no battery-backed
RTC: send `TIME_SYNC` on every connection. Reading TIME returns
`{"type":"TIME","epoch":…,"tzOffset":…,"synced":true}`.

## 6. Delivery and duplicate handling

* **At-least-once delivery.** Each event stage stays in a watch outbox (8
  entries) until ACKed. While connected the watch retransmits every 4 s; on
  reconnect all pending stages are sent immediately. Events raised while the
  phone was away are delivered when it returns.
* **The phone must de-duplicate** by `(eventId, seq)`: process a pair once,
  but ACK every copy (the earlier ACK may have been lost).
* **The watch de-duplicates** incoming `ACK` (already delivered → ignored) and
  `CANCEL` (remembered per event ID).
* **One fall → one event.** After a confirmed fall the detector has a 20 s
  cooldown, and further detections are ignored while an alert is active.
* Event IDs are unique per watch. Combine with the BLE address if the phone
  manages several watches.

## 7. Recommended phone connection sequence

1. Scan for the service UUID, connect.
2. Android: `requestMtu(247)`.
3. Enable notifications on STATUS, BATTERY, FALL, SOS (and 0x2A19 if wanted).
4. Write `TIME_SYNC`.
5. Read CONFIG (optional) and STATUS.
6. Handle FALL/SOS notifications: process if `(eventId, seq)` is new, always
   reply `ACK`.
