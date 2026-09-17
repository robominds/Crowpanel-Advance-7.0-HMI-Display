# Local AHT10 Sensor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A panel with an AHT10 on `I2C-OUT` shows and publishes its own reading; a panel without one subscribes to the broker as it does today.

**Architecture:** The conversion arithmetic is a pure library (`lib/aht10`) with host tests. `src/source_aht10` owns the I2C: a probe at boot and a non-blocking trigger/collect cycle. `source_mqtt` gains a `Mode` — `Subscribe` (today) or `PublishOnly` — and a `publish()`. `main.cpp` probes once and picks the mode.

**Tech Stack:** PlatformIO, arduino-esp32 3.3.9, Wire (I2C at 400 kHz on GPIO15/16), PubSubClient, LVGL 9.1, Unity host tests.

**Spec:** `docs/superpowers/specs/2026-09-16-local-aht10-sensor-design.md`

## Global Constraints

- Branch `local-sensor` in `/Users/markcastelluccio/Crowpanel-7.0-HMI-IPS-Display`.
- File headers end with `// Author: Mark Castelluccio <markacastelluccio@gmail.com>` then `// Written with assistance from Claude Code (Anthropic).`
- Commits end with `Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>`.
- Never read, print or edit `secrets.ini`. Never upload, open a serial port or run espota; the controller does hardware.
- `lib/aht10` must not include Arduino, Wire or LVGL headers — it has to compile for the host.
- The AHT10 shares the touch controller's bus: no busy-wait between trigger and collect, and no transaction longer than a few bytes.
- `custom_fw_version = 2.3.0`.
- zsh: no word-splitting loops.

---

### Task 1: `lib/aht10` and its host tests

**Files:**
- Create: `lib/aht10/library.json`, `lib/aht10/aht10.h`, `lib/aht10/aht10.cpp`, `test/test_aht10/test_aht10.cpp`

**Interfaces:**
- Produces: `aht10::ADDRESS`, `aht10::Sample{temperature_c, humidity_pct}`, `bool aht10::convert(const uint8_t*, size_t, Sample&)` — used by Task 2.

- [ ] **Step 1: Write the failing tests** — `test/test_aht10/test_aht10.cpp`

```cpp
// Host tests for lib/aht10: the six bytes an AHT10 returns.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic).

#include <unity.h>

#include "aht10.h"

static constexpr float kEps = 0.01f;

void setUp(void) {}
void tearDown(void) {}

// Humidity and temperature both mid-scale: raw 0x80000 each.
// 0x80000 * 100 / 2^20 = 50 %RH; 0x80000 * 200 / 2^20 - 50 = 50 C.
static void test_convert_mid_scale(void) {
    const uint8_t f[6] = {0x1C, 0x80, 0x00, 0x08, 0x00, 0x00};
    aht10::Sample s{};
    TEST_ASSERT_TRUE(aht10::convert(f, sizeof f, s));
    TEST_ASSERT_FLOAT_WITHIN(kEps, 50.0f, s.humidity_pct);
    TEST_ASSERT_FLOAT_WITHIN(kEps, 50.0f, s.temperature_c);
}

// Raw temperature 0x20000 is an eighth of scale: 200/8 - 50 = -25 C.
// Raw humidity 0x10000 is a sixteenth: 6.25 %RH.
static void test_convert_negative_temperature(void) {
    const uint8_t f[6] = {0x1C, 0x10, 0x00, 0x02, 0x00, 0x00};
    aht10::Sample s{};
    TEST_ASSERT_TRUE(aht10::convert(f, sizeof f, s));
    TEST_ASSERT_FLOAT_WITHIN(kEps, 6.25f, s.humidity_pct);
    TEST_ASSERT_FLOAT_WITHIN(kEps, -25.0f, s.temperature_c);
}

// Full-scale humidity with a valid temperature: 0xFFFFF is 100 %RH to within
// a thousandth, and must not be rejected as an all-ones frame.
static void test_convert_full_scale_humidity(void) {
    const uint8_t f[6] = {0x1C, 0xFF, 0xFF, 0xF8, 0x00, 0x00};
    aht10::Sample s{};
    TEST_ASSERT_TRUE(aht10::convert(f, sizeof f, s));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.0f, s.humidity_pct);
    TEST_ASSERT_FLOAT_WITHIN(kEps, 50.0f, s.temperature_c);
}

// A sensor that never answered: the driver reads zeros off an idle bus.
static void test_convert_rejects_all_zero(void) {
    const uint8_t f[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    aht10::Sample s{};
    TEST_ASSERT_FALSE(aht10::convert(f, sizeof f, s));
}

// A disconnected SDA line reads as all ones.
static void test_convert_rejects_all_ones(void) {
    const uint8_t f[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    aht10::Sample s{};
    TEST_ASSERT_FALSE(aht10::convert(f, sizeof f, s));
}

// Status bit 7 set means the conversion was still running.
static void test_convert_rejects_busy(void) {
    const uint8_t f[6] = {0x80, 0x80, 0x00, 0x08, 0x00, 0x00};
    aht10::Sample s{};
    TEST_ASSERT_FALSE(aht10::convert(f, sizeof f, s));
}

// Status bit 3 clear means the sensor lost its calibration.
static void test_convert_rejects_uncalibrated(void) {
    const uint8_t f[6] = {0x00 | 0x10, 0x80, 0x00, 0x08, 0x00, 0x00};
    aht10::Sample s{};
    TEST_ASSERT_FALSE(aht10::convert(f, sizeof f, s));
}

static void test_convert_rejects_short_frame(void) {
    const uint8_t f[5] = {0x1C, 0x80, 0x00, 0x08, 0x00};
    aht10::Sample s{};
    TEST_ASSERT_FALSE(aht10::convert(f, sizeof f, s));
}

static void test_convert_rejects_null(void) {
    aht10::Sample s{};
    TEST_ASSERT_FALSE(aht10::convert(nullptr, 6, s));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_convert_mid_scale);
    RUN_TEST(test_convert_negative_temperature);
    RUN_TEST(test_convert_full_scale_humidity);
    RUN_TEST(test_convert_rejects_all_zero);
    RUN_TEST(test_convert_rejects_all_ones);
    RUN_TEST(test_convert_rejects_busy);
    RUN_TEST(test_convert_rejects_uncalibrated);
    RUN_TEST(test_convert_rejects_short_frame);
    RUN_TEST(test_convert_rejects_null);
    return UNITY_END();
}
```

- [ ] **Step 2: Run them and watch them fail**

Run: `pio test -e native 2>&1 | tail -3`
Expected: the build fails because `aht10.h` does not exist.

- [ ] **Step 3: `lib/aht10/library.json`**

```json
{
  "name": "aht10",
  "version": "1.0.0",
  "description": "Conversion and validation for the AHT10 temperature and humidity sensor's six-byte frame. Pure C++17, host-testable."
}
```

- [ ] **Step 4: `lib/aht10/aht10.h`**

```cpp
// The AHT10's six-byte measurement frame, converted and sanity-checked.
//
// No Arduino, Wire or LVGL includes: this compiles for the host so the
// arithmetic is tested without hardware. src/source_aht10 owns the I2C.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic).

#pragma once

#include <cstddef>
#include <cstdint>

namespace aht10 {

// The part has one fixed address. Nothing else on this board's bus uses it:
// the companion MCU is 0x30, the real-time clock 0x51, touch 0x5D.
constexpr uint8_t ADDRESS = 0x38;

// Status byte flags, as the datasheet numbers them.
constexpr uint8_t STATUS_BUSY       = 0x80;
constexpr uint8_t STATUS_CALIBRATED = 0x08;

struct Sample {
    float temperature_c;
    float humidity_pct;
};

// Converts one frame: status, then a 20-bit humidity and a 20-bit temperature
// sharing the middle byte's nibbles.
//
// Returns false, leaving `out` untouched, when the frame cannot be a real
// reading: fewer than six bytes, the busy or uncalibrated status, the all-zero
// frame an absent sensor leaves on the bus, the all-ones frame a disconnected
// SDA line reads, or a value outside the part's own range (-40 to 85 C,
// 0 to 100 %RH). The AHT10 sends no CRC - that is an AHT20 feature - so these
// checks are the only defence against a garbled read.
bool convert(const uint8_t* frame, size_t len, Sample& out);

}  // namespace aht10
```

- [ ] **Step 5: `lib/aht10/aht10.cpp`**

```cpp
// Implementation of the AHT10 frame conversion. See aht10.h.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic).

#include "aht10.h"

namespace aht10 {
namespace {

constexpr float FULL_SCALE = 1048576.0f;   // 2^20

constexpr float TEMP_MIN_C = -40.0f;
constexpr float TEMP_MAX_C = 85.0f;

constexpr uint32_t FIELD_MAX = 0xFFFFF;

}  // namespace

bool convert(const uint8_t* frame, size_t len, Sample& out) {
    if (frame == nullptr || len < 6) return false;

    const uint8_t status = frame[0];
    if ((status & STATUS_BUSY) != 0) return false;
    if ((status & STATUS_CALIBRATED) == 0) return false;

    const uint32_t humidity_raw = (static_cast<uint32_t>(frame[1]) << 12) |
                                  (static_cast<uint32_t>(frame[2]) << 4) |
                                  (static_cast<uint32_t>(frame[3]) >> 4);
    const uint32_t temperature_raw =
        (static_cast<uint32_t>(frame[3] & 0x0F) << 16) |
        (static_cast<uint32_t>(frame[4]) << 8) | static_cast<uint32_t>(frame[5]);

    // An absent sensor leaves the bus at zero; a disconnected data line reads
    // as ones. Either value is theoretically in range, so the frame as a whole
    // is what rules them out.
    if (humidity_raw == 0 && temperature_raw == 0) return false;
    if (humidity_raw == FIELD_MAX && temperature_raw == FIELD_MAX) return false;

    const float humidity = static_cast<float>(humidity_raw) * 100.0f / FULL_SCALE;
    const float temperature =
        static_cast<float>(temperature_raw) * 200.0f / FULL_SCALE - 50.0f;

    if (humidity < 0.0f || humidity > 100.0f) return false;
    if (temperature < TEMP_MIN_C || temperature > TEMP_MAX_C) return false;

    out.humidity_pct  = humidity;
    out.temperature_c = temperature;
    return true;
}

}  // namespace aht10
```

- [ ] **Step 6: Run the tests**

Run: `pio test -e native 2>&1 | tail -3`
Expected: every test passes; the total is nine higher than before (was 70).

- [ ] **Step 7: Commit**

```bash
git add lib/aht10 test/test_aht10
git commit -q -m "Add lib/aht10: convert and sanity-check the sensor's frame

Nine host tests cover the datasheet's scaling, a negative temperature, full
scale humidity, and the frames an absent or disconnected sensor produces.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 2: The I2C driver, publishing, and the wiring

**Files:**
- Create: `src/source_aht10.h`, `src/source_aht10.cpp`
- Modify: `src/source_mqtt.h`, `src/source_mqtt.cpp`, `src/main.cpp`, `platformio.ini`

**Interfaces:**
- Consumes: `aht10::convert`, `aht10::ADDRESS`, `aht10::Sample` from Task 1; `board::I2C_*` already initialised by `Wire.begin()` in `setup()`.
- Produces: `source_aht10::begin/poll/present/failedReads`; `source_mqtt::Mode`, `source_mqtt::begin(channel, mode)`, `source_mqtt::publish`.

- [ ] **Step 1: `src/source_aht10.h`**

```cpp
// The local AHT10 on the I2C-OUT connector, when one is fitted.
//
// begin() probes the bus and decides this panel's role: a panel with a sensor
// shows and publishes its own reading, a panel without one reads the broker.
//
// The sensor shares the bus with the touch controller, and blocking that bus
// starves the RGB panel's DMA. A measurement takes 80 ms, so poll() triggers
// one and collects it on a later call rather than waiting.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic).

#pragma once

#include <cstdint>

namespace source_aht10 {

// Soft-resets and initialises the sensor, then reads its status. Call once,
// after Wire.begin(). Returns whether a calibrated sensor answered.
bool begin();

// Call every loop. Returns true on the single call where a fresh, valid sample
// became available, writing it to the two references; false otherwise.
bool poll(float& temperature_c, float& humidity_pct);

bool present();

// Frames rejected since boot: a bus error, or a frame aht10::convert refused.
uint32_t failedReads();

}  // namespace source_aht10
```

- [ ] **Step 2: `src/source_aht10.cpp`**

```cpp
// Implementation of the local AHT10 source. See source_aht10.h.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic).

#include "source_aht10.h"

#include <Arduino.h>
#include <Wire.h>

#include "aht10.h"

namespace source_aht10 {
namespace {

// The house sensors publish every ten seconds, and the staleness rule for the
// indoor channel is a minute, so this matches what a subscriber would see.
constexpr uint32_t SAMPLE_INTERVAL_MS = 10000;

// The datasheet asks for 80 ms; 100 ms costs nothing and covers a slow part.
constexpr uint32_t CONVERSION_MS = 100;

// One complaint a minute. A sensor pulled out of its socket would otherwise
// fill the log with identical lines.
constexpr uint32_t COMPLAIN_INTERVAL_MS = 60000;

enum class State { Idle, Converting };

State    g_state           = State::Idle;
bool     g_present         = false;
uint32_t g_next_due_ms     = 0;
uint32_t g_trigger_ms      = 0;
uint32_t g_failed          = 0;
uint32_t g_last_complaint  = 0;
bool     g_complained      = false;

bool writeCommand(const uint8_t* bytes, size_t len) {
    Wire.beginTransmission(aht10::ADDRESS);
    Wire.write(bytes, len);
    return Wire.endTransmission() == 0;
}

void readFailed(uint32_t now) {
    ++g_failed;
    g_state       = State::Idle;
    g_next_due_ms = now + SAMPLE_INTERVAL_MS;
    if (!g_complained || now - g_last_complaint >= COMPLAIN_INTERVAL_MS) {
        Serial.printf("aht10: read failed (%lu total)\n",
                      static_cast<unsigned long>(g_failed));
        g_last_complaint = now;
        g_complained     = true;
    }
}

}  // namespace

bool begin() {
    const uint8_t reset[]       = {0xBA};
    const uint8_t initialise[]  = {0xE1, 0x08, 0x00};

    if (!writeCommand(reset, sizeof reset)) {
        Serial.println("aht10: no sensor at 0x38, reading the broker instead");
        return false;
    }
    delay(20);   // datasheet: 20 ms after a soft reset

    if (!writeCommand(initialise, sizeof initialise)) {
        Serial.println("aht10: no sensor at 0x38, reading the broker instead");
        return false;
    }
    delay(10);

    if (Wire.requestFrom(static_cast<int>(aht10::ADDRESS), 1) != 1) {
        Serial.println("aht10: no sensor at 0x38, reading the broker instead");
        return false;
    }
    const uint8_t status = static_cast<uint8_t>(Wire.read());
    if ((status & aht10::STATUS_CALIBRATED) == 0) {
        Serial.printf("aht10: 0x38 answered but is not calibrated (status 0x%02X);"
                      " reading the broker instead\n",
                      static_cast<unsigned>(status));
        return false;
    }

    g_present     = true;
    g_next_due_ms = millis();
    Serial.printf("aht10: found at 0x38, publishing %s\n", MQTT_TOPIC_TEMP);
    return true;
}

bool poll(float& temperature_c, float& humidity_pct) {
    if (!g_present) return false;

    const uint32_t now = millis();

    if (g_state == State::Idle) {
        // Signed difference, so a due time that has just wrapped still fires.
        if (static_cast<int32_t>(now - g_next_due_ms) < 0) return false;
        const uint8_t trigger[] = {0xAC, 0x33, 0x00};
        if (!writeCommand(trigger, sizeof trigger)) {
            readFailed(now);
            return false;
        }
        g_trigger_ms = now;
        g_state      = State::Converting;
        return false;
    }

    if (now - g_trigger_ms < CONVERSION_MS) return false;

    uint8_t frame[6] = {0};
    if (Wire.requestFrom(static_cast<int>(aht10::ADDRESS),
                         static_cast<int>(sizeof frame)) !=
        static_cast<int>(sizeof frame)) {
        readFailed(now);
        return false;
    }
    for (uint8_t& b : frame) b = static_cast<uint8_t>(Wire.read());

    aht10::Sample sample{};
    if (!aht10::convert(frame, sizeof frame, sample)) {
        readFailed(now);
        return false;
    }

    g_state       = State::Idle;
    g_next_due_ms = g_trigger_ms + SAMPLE_INTERVAL_MS;
    temperature_c = sample.temperature_c;
    humidity_pct  = sample.humidity_pct;
    return true;
}

bool present() { return g_present; }

uint32_t failedReads() { return g_failed; }

}  // namespace source_aht10
```

- [ ] **Step 3: `src/source_mqtt.h`** — add the mode and `publish()`

Replace

```cpp
// Binds the channel that incoming readings are written into. The channel must
// outlive this module; in practice it is allocated once in setup() and never
// freed.
void begin(channel::Channel& indoor);
```

with

```cpp
// Subscribe: the broker owns the indoor reading, as on a panel with no sensor.
// PublishOnly: this panel owns it, reads it locally and writes it to the same
// two topics. A publishing panel never subscribes, so it cannot feed itself.
enum class Mode { Subscribe, PublishOnly };

// Binds the channel that incoming readings are written into, and the role this
// panel takes. The channel must outlive this module; in practice it is
// allocated once in setup() and never freed. In PublishOnly the channel is
// still bound, but only publish() and the connection logic are used.
void begin(channel::Channel& indoor, Mode mode);

// PublishOnly only: writes one reading to the two configured topics, as bare
// decimals with one decimal place, QoS 0 and not retained. A no-op while
// disconnected - nothing is queued, the next sample is ten seconds away.
void publish(float temperature_c, float humidity_pct);
```

- [ ] **Step 4: `src/source_mqtt.cpp`** — honour the mode

a) After the line `channel::Channel* g_channel = nullptr;` insert:

```cpp
Mode g_mode = Mode::Subscribe;
```

b) Replace

```cpp
    g_mqtt.subscribe(TOPIC_TEMP);
    g_mqtt.subscribe(TOPIC_HUM);
    g_retry_ms = RETRY_MIN_MS;
    Serial.println("mqtt: connected and subscribed");
```

with

```cpp
    g_retry_ms = RETRY_MIN_MS;
    if (g_mode == Mode::Subscribe) {
        g_mqtt.subscribe(TOPIC_TEMP);
        g_mqtt.subscribe(TOPIC_HUM);
        Serial.println("mqtt: connected and subscribed");
    } else {
        // No subscription: this panel reads its own sensor, and subscribing to
        // the topics it writes would only feed its own data back to it.
        Serial.println("mqtt: connected, publishing only");
    }
```

c) Replace

```cpp
void begin(channel::Channel& indoor) {
    g_channel = &indoor;
```

with

```cpp
void begin(channel::Channel& indoor, Mode mode) {
    g_channel = &indoor;
    g_mode    = mode;
```

d) Directly after the closing brace of `begin()` and its blank line, insert:

```cpp
void publish(float temperature_c, float humidity_pct) {
    if (g_mode != Mode::PublishOnly || !g_mqtt.connected()) return;

    char value[16];
    snprintf(value, sizeof value, "%.1f", temperature_c);
    const bool temp_ok = g_mqtt.publish(TOPIC_TEMP, value);
    snprintf(value, sizeof value, "%.1f", humidity_pct);
    const bool hum_ok = g_mqtt.publish(TOPIC_HUM, value);

    if (temp_ok && hum_ok) {
        Serial.printf("mqtt: published %.1f C, %.1f %%\n", temperature_c,
                      humidity_pct);
    } else {
        Serial.println("mqtt: publish failed");
    }
}

```

- [ ] **Step 5: `src/main.cpp`** — probe, choose the mode, feed both

a) Add `#include "source_aht10.h"` directly before `#include "source_mqtt.h"`.

b) Replace

```cpp
    net::begin();
    source_mqtt::begin(*g_channels[1]);
    source_weather::begin(*g_channels[0]);
```

with

```cpp
    // Which role this panel takes is a question for the bus, not a build flag:
    // a panel with an AHT10 on I2C-OUT shows and publishes its own reading, a
    // panel without one reads the same topics from the broker.
    const bool local_sensor = source_aht10::begin();

    net::begin();
    source_mqtt::begin(*g_channels[1], local_sensor
                                           ? source_mqtt::Mode::PublishOnly
                                           : source_mqtt::Mode::Subscribe);
    source_weather::begin(*g_channels[0]);
```

c) Replace

```cpp
    source_mqtt::poll();
    source_weather::poll();
```

with

```cpp
    source_mqtt::poll();
    source_weather::poll();

    // One sample feeds the screen and the broker. A failed publish is dropped
    // rather than queued: the next sample is ten seconds away.
    float sensor_c = 0.0f, sensor_rh = 0.0f;
    if (source_aht10::poll(sensor_c, sensor_rh)) {
        g_channels[1]->update(sensor_c, sensor_rh, millis());
        source_mqtt::publish(sensor_c, sensor_rh);
    }
```

- [ ] **Step 6: `platformio.ini`** — version

Replace `custom_fw_version = 2.2.0` with `custom_fw_version = 2.3.0`.

- [ ] **Step 7: Build and verify**

```bash
pio run -e advance_70 2>&1 | grep -E "Flash:|\[SUCCESS\]|\[FAILED\]|error:"
pio run -e advance_70 2>&1 | grep -i warning | grep -E "src/(main|source_mqtt|source_aht10)\.cpp" || echo "no warnings from the changed sources"
~/.platformio/packages/toolchain-xtensa-esp-elf/bin/xtensa-esp32s3-elf-nm .pio/build/advance_70/firmware.elf | grep -i verifyRollbackLater
pio run -e advance_70_ota 2>&1 | grep -E "\[SUCCESS\]|\[FAILED\]"
pio run -e diag 2>&1 | grep -E "\[SUCCESS\]|\[FAILED\]"
pio test -e native 2>&1 | tail -1
grep -rn 'delay(' src/source_aht10.cpp
```

Expected: `[SUCCESS]` under 6,553,600 bytes; no warnings; exactly one ` T verifyRollbackLater`; the OTA and diag envs build; 79 host tests pass; the only `delay()` calls in the driver are the two in `begin()` (20 ms and 10 ms, at boot, before the panel is scanning).

- [ ] **Step 8: Commit**

```bash
git add src/source_aht10.h src/source_aht10.cpp src/source_mqtt.h src/source_mqtt.cpp src/main.cpp platformio.ini
git status --short
git commit -q -m "Read a local AHT10 and publish it, or fall back to the broker

A probe at 0x38 decides this panel's role. With a sensor the indoor row is
local and MQTT is publish-only; without one nothing changes. The trigger and
the collect are separate poll() calls, so the touch controller's bus is never
held. Version 2.3.0.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 3: README

**Files:**
- Modify: `README.md`

- [ ] **Step 1:** In the intro list, replace

```markdown
- **An indoor room** — a reading published by an MQTT broker on your own
  network. The room's name and its two topics come from `secrets.ini`, so the
  same firmware serves a panel in any room. The broker accepts anonymous
  connections; there is nothing to authenticate.
```

with

```markdown
- **An indoor room** — either a local AHT10 on the `I2C-OUT` connector, or a
  reading published by an MQTT broker on your own network. The room's name and
  its two topics come from `secrets.ini`, so the same firmware serves a panel in
  any room. The broker accepts anonymous connections; there is nothing to
  authenticate.
```

- [ ] **Step 2:** Add this section directly before `## Build and flash`:

```markdown
## The indoor reading: local sensor or broker

At boot the firmware probes `0x38` on the `I2C-OUT` header (J13: GND, 3V3,
GPIO15 SDA, GPIO16 SCL).

- **An AHT10 answers.** The indoor row shows that sensor, sampled every ten
  seconds, and the panel publishes each reading to `mqtt_topic_temp` and
  `mqtt_topic_hum` as bare decimals. It does not subscribe. The row keeps
  updating even when the broker is unreachable.
- **Nothing answers.** The panel subscribes to those two topics instead, which
  is how a panel with no sensor has always worked.

The serial log says which: `aht10: found at 0x38, publishing <topic>` or
`aht10: no sensor at 0x38, reading the broker instead`. A sensor that stops
answering stops the publishes, and the row dims after a minute like any other
stale reading.

The sensor shares its bus with the touch controller, so a measurement is
triggered and collected on separate loop passes — the bus is never held while
the sensor converts, which would starve the display's DMA.
```

- [ ] **Step 3: Verify and commit**

```bash
grep -c '^```' README.md
git add README.md
git commit -q -m "README: the indoor reading comes from a local AHT10 or the broker

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

Expected: an even fence count.

---

### Task 4 (controller, hardware — bedroom panel only)

The panel is on `/dev/cu.wchusbserial2120` running Elecrow's factory firmware. The office and kitchen panels stay on 2.2.0.

1. Set `secrets.ini` to the bedroom values: `indoor_label = "MasterBedroom"`, `device_host = "MasterBedroom-Display"`, `mqtt_topic_temp = "MasterBedroom/AHT10/tempc"`, `mqtt_topic_hum = "MasterBedroom/AHT10/hum"`. Keep the office values backed up in the scratchpad.
2. `nm` check, then USB flash: `pio run -e advance_70 -t upload --upload-port /dev/cu.wchusbserial2120`. This also writes the two-slot partition table, so it cannot be done over the air.
3. Capture serial. Expect the 2.3.0 banner, `aht10: found at 0x38, publishing MasterBedroom/AHT10/tempc`, `mqtt: connected, publishing only`, then `mqtt: published <c> C, <rh> %` about every ten seconds. Confirm the first sample precedes `wifi: up`, which proves the row does not wait for the network.
4. Watch the broker from the Mac: `mosquitto_sub -h <broker> -t 'MasterBedroom/AHT10/#' -v -W 30` — two topics, bare decimals, about every ten seconds, matching the serial log.
5. Ask the user to confirm the screen: the indoor row labelled `MasterBedroom` with the room's real temperature and humidity, the clock view in red, and no `stale` note.
6. Sensor-lost path, with the user's agreement: unplug the AHT10 while running. Expect the publishes to stop, `aht10: read failed (N total)` at most once a minute, the row dimming after 60 s, and no reboot. Re-plug and confirm it recovers within ten seconds or, if the part needs its init sequence again, after a reset.
7. Restore `secrets.ini` to the office values.
8. Merge `local-sensor` into `main` and push, once the user approves.
