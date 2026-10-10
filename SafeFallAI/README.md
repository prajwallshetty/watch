# SafeFall AI

Wearable fall-detection firmware for the **Waveshare ESP32-S3-Touch-LCD-1.28**
(ESP32-S3R2, 240×240 round GC9A01 LCD, CST816S touch, QMI8658 IMU).

> **Engineering prototype – not a medical device.** The fall-detection
> algorithm is heuristic, has not been clinically validated, and **will not
> detect every fall**. It can also raise false alarms. Do not use it as the
> only safety measure for anyone.

---

## Features

- Multi-stage fall detection (free-fall → impact → post-impact analysis →
  orientation change → motionless period → confidence score) running in its
  own 100 Hz task, independent of the UI and of the screen state.
- Full-screen "FALL DETECTED – Are you OK?" alert with a 10 s countdown and
  large **I'M OK** / **SOS** buttons; escalates to an emergency on timeout.
  Works with no phone or internet.
- Manual SOS: hold the on-screen SOS button (1.5 s) or the **BOOT** key (3 s).
- BLE GATT service (`SafeFall-AI`) with unique, persistent event IDs
  (`SF-000001`), acknowledgements, retransmission and duplicate protection.
- Round, high-contrast LVGL 8.3 UI: Home, SOS, Status, Settings, hidden
  Diagnostics.
- Battery monitoring with low/critical warnings.
- Screen dim/off with touch/button wake; panel sleep while dark.
- Task watchdog, sensor fault detection and automatic IMU recovery, BLE
  advertising recovery, settings in NVS, safe-reboot path.
- Tagged serial debug log (`[BOOT] [DISPLAY] [TOUCH] [IMU] [BLE] [BATTERY] [FALL] [STATE]`).

---

## Where the hardware configuration comes from

The firmware does not invent pins or init sequences. The Waveshare demo
package was **not present** in this repository, and the Waveshare download
site was not reachable from the build environment, so each fact was taken from
Waveshare's own published code or documentation instead:

| Item | Value | Source |
|---|---|---|
| LCD | GC9A01, MOSI 11, SCLK 10, CS 9, DC 8, RST 14, MISO 12, BL 2 (active HIGH), 240×240, 40 MHz, rotation 0 | Waveshare's TFT_eSPI setup `Setup302_Waveshare_ESP32S3_GC9A01.h` (Bodmer/TFT_eSPI PR #2844, by a Waveshare contributor) |
| LCD init | TFT_eSPI's GC9A01 init (`tft.begin()`), LVGL flush = `startWrite / setAddrWindow / pushColors(…, true) / endWrite`, `lv_tick_inc` from an `esp_timer` | LVGL_Arduino architecture |
| Touch | `CST816S touch(6, 7, 13, 5)` → SDA 6, SCL 7, RST 13, IRQ 5, addr 0x15, fbiego CST816S library | LVGL_Arduino example (confirmed by two independent board projects) |
| IMU | QMI8658 on the same I2C bus, addr 0x6B, WHO_AM_I 0x05, `CTRL1=0x60`, CTRL2/CTRL3 range+ODR, `CTRL7=0x03` | Waveshare `qmi8658` driver (waveshareteam/Waveshare-ESP32-components) |
| Battery | GPIO1, 200 K/100 K divider, `V = 3.3 / (1<<12) * 3 * AD_value` | Waveshare wiki for this board |
| Serial | UART0 via the on-board CH343P USB-UART → **USB CDC On Boot: Disabled** | board schematic notes |

`src/config/BoardConfig.h` checks at **compile time** that the TFT_eSPI setup
you selected has exactly these LCD pins, and stops the build with a clear
message if it does not.

Two deliberate, documented deviations:

1. **IMU output data rate** is 125 Hz instead of Waveshare's 1 kHz. Ranges
   (±8 g, ±512 dps) and the register sequence are unchanged; the lower ODR
   makes the sensor's decimation filter band-limit the signal sampled at 100 Hz.
2. **IMU byte order is measured at boot.** Waveshare's driver writes
   `CTRL1 = 0x60` and decodes little-endian, while some QMI8658 datasheet
   revisions describe bit 5 of CTRL1 as "big-endian". The firmware reads a few
   samples at rest and keeps whichever byte order gives ≈1 g (logged as
   `[IMU] Data byte order: …`). Keep the watch still for one second at power-up.

---

## Project structure

```
SafeFallAI/
├── SafeFallAI.ino              setup()/loop(): wiring, event routing, BLE glue
├── src/                        (Arduino IDE only compiles sub-folders under src/)
│   ├── config/   BoardConfig.h  AppConfig.h  DebugConfig.h
│   ├── hardware/ DisplayManager  TouchManager  IMUManager  BatteryManager  I2CBus
│   ├── fall/     FallDetector  FallStateMachine
│   ├── ble/      BLEManager  BLEProtocol
│   ├── ui/       UIManager  HomeScreen  FallAlertScreen  SOSScreen  StatusScreen
│   │             SettingsScreen  DiagnosticsScreen  HoldButton  UITheme  UIModel.h
│   └── services/ EventManager  TimeManager  PowerManager  SettingsStore
├── extras/
│   ├── lv_conf.h                                    → copy to Arduino/libraries/
│   ├── TFT_eSPI/Setup302_Waveshare_ESP32S3_GC9A01.h → copy to TFT_eSPI/User_Setups/
│   └── host_tests/                                  PC unit tests (make)
└── docs/BLE_PROTOCOL.md
```

The requested layout (`config/`, `hardware/`, … next to the `.ino`) is kept,
but nested under `src/`: the Arduino IDE silently ignores `.cpp` files in any
other sub-folder, which would leave every module unlinked.

---

## Installation (Arduino IDE 2.x)

### 1. ESP32 board package – exactly 2.0.12

1. *File → Preferences → Additional boards manager URLs*:
   `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
2. *Tools → Board → Boards Manager*, search **esp32** (by Espressif Systems),
   select version **2.0.12**, *Install*.
   Core 3.x is **not** supported (LEDC/BLE/WDT APIs changed); the build stops
   with an error if a 3.x core is selected.

Note: core 2.0.12 still reports itself internally as 2.0.11
(`ESP_ARDUINO_VERSION_PATCH`); that is expected.

### 2. Libraries – exact versions

| Library | Version | How |
|---|---|---|
| **lvgl** (LVGL) | **8.3.10** | Library Manager: "lvgl" → 8.3.10 (not 9.x) |
| **TFT_eSPI** (Bodmer) | **2.5.34** | Library Manager: "TFT_eSPI" → 2.5.34 |
| **CST816S** (fbiego) | 1.1.1 (tested; 1.3.0 also compiles) | Library Manager: "CST816S", or the copy in the Waveshare demo package |
| BLE, Preferences, Wire, SPI | bundled | part of the ESP32 core – do **not** install "ArduinoBLE" or "NimBLE" |

If you already copied the libraries from the Waveshare demo package
(`ESP32-S3-Touch-LCD-1.28-Demo/Arduino/libraries`), you can use those instead
– as long as the versions match the table.

### 3. `lv_conf.h`

Copy `extras/lv_conf.h` to your Arduino libraries folder, **next to** the
`lvgl` folder:

```
Documents/Arduino/libraries/lv_conf.h      ← here
Documents/Arduino/libraries/lvgl/
```

It enables the fonts the UI uses (Montserrat 14/16/20/28/48), 16-bit colour
without byte swap, and a 64 KB LVGL heap. If you had the Waveshare `lv_conf.h`
there, replace it (keep a backup); the build stops with a clear `#error` if a
required option is missing.

### 4. TFT_eSPI setup

Either keep a working Waveshare `TFT_eSPI_Setups` configuration (if the
`LVGL_Arduino` demo shows correctly, it is right), or:

1. Copy `extras/TFT_eSPI/Setup302_Waveshare_ESP32S3_GC9A01.h` to
   `Arduino/libraries/TFT_eSPI/User_Setups/`.
2. Edit `Arduino/libraries/TFT_eSPI/User_Setup_Select.h`:
   ```c
   //#include <User_Setup.h>
   #include <User_Setups/Setup302_Waveshare_ESP32S3_GC9A01.h>
   ```

The compile-time check in `BoardConfig.h` verifies the result.
(TFT_eSPI prints a harmless `TOUCH_CS pin not defined` warning; touch is the
CST816S over I2C, not the TFT_eSPI resistive touch.)

### 5. Board settings (Tools menu)

| Setting | Value |
|---|---|
| Board | **ESP32S3 Dev Module** |
| USB CDC On Boot | **Disabled** (Serial goes through the CH343P UART bridge) |
| CPU Frequency | 240MHz (WiFi) |
| Core Debug Level | None |
| USB DFU On Boot | Disabled |
| Erase All Flash Before Sketch Upload | Disabled (Enabled once if you want to reset settings/event counter) |
| Events Run On | Core 1 |
| Flash Mode | QIO 80MHz (use DIO 80MHz if the board does not boot after upload) |
| Flash Size | **16MB (128Mb)** |
| JTAG Adapter | Disabled |
| Arduino Runs On | Core 1 |
| USB Firmware MSC On Boot | Disabled |
| Partition Scheme | **16M Flash (3MB APP/9.9MB FATFS)** |
| PSRAM | **QSPI PSRAM** (the S3R2's 2 MB PSRAM is quad-SPI; the firmware also runs with PSRAM disabled) |
| Upload Mode | UART0 / Hardware CDC |
| Upload Speed | 921600 |
| USB Mode | Hardware CDC and JTAG |

The firmware is ~1.30 MB, so the default 1.2 MB-app partition scheme is too
tight; use the 3 MB app scheme above. The equivalent `arduino-cli` FQBN is:

```
esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=enabled,FlashMode=qio,CDCOnBoot=default,CPUFreq=240
```

### 6. Flash / upload

1. Connect the watch with a USB-C **data** cable. It appears as a CH343 serial
   port (`/dev/ttyACM*` / `/dev/cu.wchusbserial*` / `COMx`). On Windows/macOS
   install the WCH CH343 driver if no port appears.
2. Open `SafeFallAI/SafeFallAI.ino`, select the port, click **Upload**.
3. If the upload cannot connect: hold **BOOT**, press and release **RESET**,
   release **BOOT**, then upload again. Press **RESET** after flashing.

---

## Serial debugging

Open the Serial Monitor at **115200 baud** (USB CDC On Boot must be
*Disabled*). A normal boot looks like this (example; values differ per unit):

```
[BOOT] SafeFall AI starting (firmware 0.1.0)
[BOOT] Waveshare ESP32-S3-Touch-LCD-1.28 | ESP32-S3 rev 0 | 240 MHz | flash 16 MB | PSRAM found
[BOOT] ENGINEERING PROTOTYPE - not a medical device
[BOOT] Settings: brightness 80%, monitoring on, sensitivity 1, countdown 10s, timeout 20s
[BOOT] Reset reason: POWER_ON, boot #1
[BOOT] Task watchdog armed (8 s, panic reboot)
[DISPLAY] GC9A01 initialized (240x240, rotation 0, SPI 40000000 Hz)
[DISPLAY] LVGL 8.3.10 ready, draw buffer 24 lines
[TOUCH] CST816S detected (chip id 0xB5, fw 1)
[BATTERY] 3.95 V, 80%
[TIME] Clock not set; waiting for TIME_SYNC from phone
[EVENT] Event counter at 0 (next ID SF-000001)
[FALL] Detector ready: impact 2.50g, confidence >= 60, inactivity 4000 ms
[IMU] QMI8658 detected at 0x6B
[IMU] Data byte order: little-endian (|a|-1g error LE 0.01 / BE 7.80)
[IMU] Accel +-8g, gyro +-512dps, 125 Hz ODR, |a| = 1.00 g
[UI] UI ready
[STATE] NORMAL
[BLE] Advertising SafeFall-AI
[BOOT] Ready (free heap 150000 bytes)
[DISPLAY] First frame flushed after 380 ms
```

(Chip IDs, voltages and heap numbers will differ.) After boot the log only
prints on events, for example a fall:

```
[FALL] Impact 4.31 g, rotation 290 dps, free-fall before impact -> analysing
[STATE] NORMAL -> POSSIBLE_FALL (impact candidate)
[FALL] Confirmed: confidence 83 (peak 4.31 g, 290 dps, orientation 84 deg, still 97%)
[STATE] POSSIBLE_FALL -> FALL_DETECTED (fall confirmed)
[EVENT] FALL SF-000001 created (DETECTED, trigger DETECTOR, confidence 83)
[FALL] FALL DETECTED SF-000001 (confidence 83) - waiting 10s for user
[STATE] FALL_DETECTED -> AWAITING_CONFIRMATION (auto)
[STATE] AWAITING_CONFIRMATION -> EMERGENCY (countdown expired)
[EVENT] FALL SF-000001 -> EMERGENCY (TIMEOUT)
[FALL] EMERGENCY SF-000001 (TIMEOUT)
[BLE] Sent {"type":"FALL","eventId":"SF-000001","stage":"EMERGENCY",...}
[EVENT] FALL SF-000001 acknowledged by phone (seq 2, 1 transmissions)
```

Options in `src/config/DebugConfig.h`:

- `DEBUG_MODE 0` – no serial output at all (production).
- `DEBUG_IMU_STREAM 1` – one accelerometer/gyro line every 2 s (for tuning).

---

## Using the watch

| Where | Action |
|---|---|
| Home | Time, battery, Bluetooth, status pill (SAFE / CHECKING / LOW BATTERY / PAUSED / SENSOR ERROR), motion level |
| Swipe left | **SOS** page → press and hold the red button 1.5 s |
| Swipe left again | **Status**: battery, phone link, monitoring, last alert |
| Swipe left again | **Settings**: brightness −/+, fall detection (hold to pause, tap to resume), sensitivity |
| Settings → long-press `v0.1.0` | hidden **Diagnostics** (subsystems, accel/gyro XYZ, detector stats); swipe right or *Close* |
| BOOT key | short press wakes the screen; hold 3 s = SOS |
| Fall alert | **I'M OK** cancels; **SOS** or no answer within 10 s → emergency |
| Emergency / SOS screen | shows delivery status; **hold to cancel** |

A touch on a dark screen only wakes it (it never presses a button). The screen
is forced on during any alert. Time shows `--:--` until the phone sends
`TIME_SYNC` (the board has no battery-backed RTC).

Alerts are visual only: the firmware does not drive a vibration motor or buzzer.

---

## Fall detection

Implemented in `src/fall/FallDetector.cpp`, thresholds in
`src/config/AppConfig.h`:

1. **Monitoring** – tracks gravity direction (2 s history), free-fall episodes
   (|a| < 0.55 g for ≥ 60 ms) and recent impacts.
2. **Impact** – |a| ≥ 2.5 g starts a candidate (`POSSIBLE_FALL`, home shows
   *CHECKING*). Ignored if it is the 3rd impact within 4 s (clapping,
   hammering, jogging).
3. **Post-impact analysis** (1.5 s) – peak g and rotation; > 400 ms of
   sustained high g is rejected as vibration/sport.
4. **Orientation change** – angle between gravity 1.5 s before the impact and
   the mean gravity after it.
5. **Motionless period** (4 s) – fraction of still samples. If the wearer is
   clearly up and moving (≥ 50 % active samples over 1 s), the candidate is
   dropped early, unless the fall evidence is strong.
6. **Decision** – evidence score (impact 30 + free-fall 15 + rotation 15 +
   orientation 20) plus stillness 20 = confidence 0–100.
   *Strong evidence (≥ 55) + ≥ 40 % stillness* → fall, so a conscious person
   moving an arm on the floor still gets the prompt; otherwise ≥ 80 %
   stillness **and** confidence ≥ 60 → fall.
7. **Cooldown** 20 s – one fall produces one event.

Sensitivity (Settings or BLE CONFIG): Low = impact ×1.25 and threshold +10,
High = impact ×0.8 and threshold −10.

The confirmation countdown exists because false positives are expected:
dropping the watch, or flopping onto a bed and lying still, can look like a fall.
Known limitations: slow "slumping" falls without an impact are not detected;
a fall followed by immediately getting up is intentionally ignored; the
thresholds have been checked only against synthetic traces (see host tests),
**not** against recorded real falls. Tune with `DEBUG_IMU_STREAM` and real data.

---

## Device state machine

`NORMAL`, `POSSIBLE_FALL`, `FALL_DETECTED`, `AWAITING_CONFIRMATION`,
`USER_CANCELLED`, `EMERGENCY`, `SOS_ACTIVE`, `LOW_BATTERY`, `DEVICE_ERROR`
(`src/fall/FallStateMachine.*`). It contains no UI or BLE code: the UI renders
a `UiModel` snapshot and sends user intent back as `UiAction`s, and the sketch
turns transitions into events and BLE packets. Baseline state is chosen from
health: IMU fault → `DEVICE_ERROR` (SOS still works), battery ≤ 10 % →
`LOW_BATTERY`, otherwise `NORMAL`. Emergencies never time out by themselves.

## BLE

See **[docs/BLE_PROTOCOL.md](docs/BLE_PROTOCOL.md)** for UUIDs, every packet,
the ACK/retry rules and the recommended phone sequence. Short version: the
watch notifies `FALL` / `SOS` / `DEVICE_STATUS` / `BATTERY` JSON packets; the
phone writes `ACK`, `CANCEL`, `CONFIG`, `TIME_SYNC`. Android apps must request
MTU ≥ 183.

## Power management

- Screen: full brightness → dimmed 6 s before the timeout (default 20 s) →
  backlight off + GC9A01 sleep. Touch or BOOT wakes it.
- The IMU task, fall detection, state machine and BLE keep running with the
  screen off.
- CPU frequency is left at 240 MHz; Wi-Fi is never started.

## Robustness

- Task watchdog (8 s, panic → reboot) on the loop and IMU tasks.
- Display: compile-time pin check plus runtime check that LVGL flushes a frame
  within 3 s; if the display fails the watch runs headless (fall → countdown →
  emergency still works, BOOT key SOS works).
- IMU: WHO_AM_I and control-register read-back at init; read errors, frozen
  data and periodic WHO_AM_I failures mark it faulty (`DEVICE_ERROR`); the
  task retries every 2 s (soft reset, then I2C bus re-init). An IMU dead for
  5 minutes triggers one safe reboot (never during an alert, never twice in a
  row).
- I2C: touch and IMU share the bus through a mutex.
- BLE: advertising restarts after disconnects, missed disconnects are
  detected, advertising is re-armed every 2 min while idle.
- No Arduino `String`; fixed-size buffers and FreeRTOS queues; LVGL uses its
  own fixed 64 KB pool; no blocking waits in `loop()` (BLE notifications are
  non-blocking). Reset reason and boot count are logged.

---

## Verification done for this version

- **Compiled** with the exact toolchain: ESP32 Arduino core 2.0.12, LVGL
  8.3.10, TFT_eSPI 2.5.34 (Setup302), CST816S 1.1.1 and 1.3.0, using the
  board settings above, `--warnings all`: no warnings from SafeFall sources.
  Also compiled with `DEBUG_MODE 0`. Size: 1.30 MB flash, 115 KB static RAM.
- **Host tests** (`extras/host_tests`, `make`): 90 checks of the fall detector
  (rest, walking, classic fall, stumble fall, table slap, getting up, arm
  movement on the floor, clapping, vibration, cooldown, pause, sensitivity),
  the state machine, event IDs/ACK/retry/outbox, and JSON packet parsing and
  worst-case sizes.
- Issues found and fixed during the review: `DISABLED` collides with an
  Arduino core macro; BLE status packet exceeded the iOS MTU; the MTU warning
  fired before the MTU exchange.
- **Not yet done:** running on the physical board. First bring-up should
  confirm the boot log above, touch coordinates (`BOARD_TOUCH_*` flags in
  `BoardConfig.h` if taps land mirrored), and the IMU byte-order line.
