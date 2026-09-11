# MQTT Temperature Display Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A wall display on the Elecrow CrowPanel Advance 7.0-HMI showing outdoor temperature for Maple Valley and indoor temperature for Mark's office, each with twelve hours of history.

**Architecture:** Pure, host-testable libraries (`lib/history`, `lib/parse`, `lib/channel`) hold every piece of logic that is easy to get wrong. Thin hardware drivers in `src/` wrap the RGB panel, the GT911 touch controller and the board's companion microcontroller. Two network sources write into a channel registry; the UI reads from it and never touches the network.

**Tech Stack:** PlatformIO, Arduino-ESP32, LovyanGFX 1.2.x, LVGL 9.1.0, PubSubClient, ArduinoJson 7, Unity for host tests.

**Spec:** [docs/superpowers/specs/2026-09-11-mqtt-temperature-display-design.md](../specs/2026-09-11-mqtt-temperature-display-design.md)

**Hardware reference:** [docs/HARDWARE.md](../../HARDWARE.md) — every pin and timing value in this plan is cited there. Do not substitute values from the older CrowPanel 7.0 HMI project; they differ and will not work.

## Global Constraints

- **Board:** Elecrow CrowPanel Advance 7.0-HMI, SKU `DIS02170A`, ESP32-S3-WROOM-1-N16R8.
- **Flash is 16 MB.** The sibling project's board is 4 MB. Do not carry that value over.
- **PSRAM:** 8 MB octal, `qio_opi`, start at 80 MHz.
- **`ARDUINO_USB_CDC_ON_BOOT` must be `0`.** This board has no native USB; `Serial` reaches the host through a CH340K on GPIO43/44. Enabling CDC gives a silent dead serial monitor.
- **Never write `while (!Serial) {}`.** It waits forever on this board.
- **Upload speed 921600**, monitor speed 115200.
- **Partition scheme: Huge APP.** The default is too small.
- **Pixel clock is revision-dependent:** 16 MHz on V1.3+, 21 MHz on V1.0 and V1.2. Porches are 8/4/8 on both axes in every revision.
- **I2C is GPIO15 (SDA) and GPIO16 (SCL)** at 400 kHz, shared by touch, the real-time clock and the companion MCU.
- **Poll touch on a fixed 20 ms interval and keep I2C transactions short.** Blocking reads on this bus starve the RGB panel's DMA and cause visible display shake (Elecrow issue #8).
- **All temperatures are stored in Celsius** and converted at display time. Fahrenheit is the default on screen.
- **Language:** C++17. `lib/` code must compile on the host with no Arduino, ESP-IDF, LVGL or FreeRTOS headers.
- **Authorship:** every file gets a header comment naming Mark Castelluccio <markacastelluccio@gmail.com> and noting it was written with assistance from Claude Code (Anthropic), matching the sibling project.
- **The board has not arrived.** Tasks 1 through 4 must pass on the host. Tasks 5 onward cannot be tested until hardware exists; write them carefully and do not claim they work.

---

## File Structure

| Path | Responsibility |
| --- | --- |
| `platformio.ini` | Build environments: `advance_70` for the board, `native` for host tests |
| `lib/history/` | Ring buffer, retention, chart downsampling. Vendored unchanged from the sibling project. |
| `lib/parse/` | MQTT bare-decimal payloads and Open-Meteo JSON, as pure functions |
| `lib/channel/` | One channel's name, latest reading, staleness rule and history |
| `test/test_history/` | 26 vendored tests for the ring buffer |
| `test/test_parse/` | Tests for both payload parsers |
| `test/test_channel/` | Tests for staleness and reading updates |
| `src/board_pins.h` | Every GPIO, named, with the source for each |
| `src/panel_mcu.*` | The companion MCU at I2C 0x30: backlight and touch activation |
| `src/display_driver.*` | RGB panel via LovyanGFX, LVGL 9 binding, PSRAM draw buffers |
| `src/touch.*` | GT911 over raw I2C, polled |
| `src/net.*` | Wi-Fi connect and reconnect with backoff |
| `src/source_mqtt.*` | Subscribes to the office topics, writes into a channel |
| `src/source_weather.*` | Polls Open-Meteo, writes into a channel |
| `src/ui.*` | Two stacked rows and the status bar |
| `src/main.cpp` | Startup order and the main loop |
| `include/secrets.h.template` | Committed template; the real `secrets.h` is gitignored |

---

## Task 1: Project skeleton, build config, and the vendored history library

**Files:**
- Create: `platformio.ini`
- Create: `lib/history/SampleHistory.h`, `lib/history/SampleHistory.cpp`, `lib/history/library.json`
- Create: `test/test_history/test_sample_history.cpp`
- Create: `include/secrets.h.template`

**Interfaces:**
- Consumes: nothing.
- Produces: `history::Sample{uint32_t t_ms; float temperature_c; float humidity_pct;}` and `history::SampleHistory` with `add(const Sample&)`, `size()`, `capacity()`, `empty()`, `clear()`, `at(size_t)`, `newest()`, `oldest()`, `temperatureRange(float&, float&)`, and `downsample(uint32_t window_ms, Bucket* out, size_t bucket_count)` returning the count of valid buckets. `SampleHistory::Bucket{float temperature_c; float humidity_pct; bool valid;}`.

The sibling project at `/Users/markcastelluccio/projects/Crowpanel-7.0-HMI-Display` already contains exactly this library and its tests. It is generic, timestamp-based and has no Arduino dependency, so it is vendored unchanged rather than rewritten.

- [ ] **Step 1: Copy the library and its tests from the sibling project**

```bash
SRC=/Users/markcastelluccio/projects/Crowpanel-7.0-HMI-Display
mkdir -p lib/history test/test_history include
cp "$SRC/lib/history/SampleHistory.h"   lib/history/
cp "$SRC/lib/history/SampleHistory.cpp" lib/history/
cp "$SRC/lib/history/library.json"      lib/history/
cp "$SRC/test/test_history/test_sample_history.cpp" test/test_history/
```

- [ ] **Step 2: Write `platformio.ini`**

```ini
; Elecrow CrowPanel Advance 7.0-HMI (SKU DIS02170A)
; See docs/HARDWARE.md for the reasoning behind every override below.

[platformio]
default_envs = advance_70

; ---------------------------------------------------------------------------
; Firmware for the board itself.
; ---------------------------------------------------------------------------
[env:advance_70]
platform  = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.39/platform-espressif32.zip
framework = arduino

; There is no upstream PlatformIO board for this panel. Elecrow ship a custom
; JSON, but it carries Espressif native-USB hwids on a CH340K board, which
; breaks port auto-detection. esp32-s3-devkitc-1 with explicit overrides is the
; cleaner base. Real geometry: ESP32-S3-WROOM-1-N16R8 = 16 MB QUAD flash +
; 8 MB OCTAL PSRAM.
board = esp32-s3-devkitc-1

board_build.arduino.memory_type = qio_opi      ; quad flash, OCTAL psram
board_build.psram_type          = opi
board_build.flash_mode          = qio
board_upload.flash_size         = 16MB         ; N16R8. NOT 4MB - that is the
board_upload.maximum_size       = 16777216     ; older CrowPanel 7.0 HMI.
board_build.partitions          = huge_app.csv

build_flags =
    -std=gnu++17
    -DBOARD_HAS_PSRAM
    ; NOTE: deliberately no ARDUINO_USB_CDC_ON_BOOT. This board has no native
    ; USB - GPIO19/20 are the PDM microphone - and flashes through a CH340K on
    ; GPIO43/44. Enabling CDC routes Serial to a peripheral that does not exist.
    -DARDUINO_USB_CDC_ON_BOOT=0
    -DARDUINO_USB_MODE=0
    ;
    ; LVGL is configured through build flags rather than a checked-in
    ; lv_conf.h, which is how Elecrow's own project does it.
    -DLV_CONF_SKIP
    -DLV_KCONFIG_IGNORE
    -DLV_COLOR_DEPTH=16
    -DLV_FONT_MONTSERRAT_16=1
    -DLV_FONT_MONTSERRAT_20=1
    -DLV_FONT_MONTSERRAT_28=1
    -DLV_FONT_MONTSERRAT_48=1
    -DLV_MEM_SIZE=131072
    -DLV_DEF_REFR_PERIOD=33
    -DLV_USE_PERF_MONITOR=0

lib_deps =
    lovyan03/LovyanGFX @ ^1.2.0
    lvgl/lvgl @ ~9.1.0
    knolleary/PubSubClient @ ^2.8
    bblanchon/ArduinoJson @ ^7.0.0

upload_speed    = 921600
monitor_speed   = 115200
monitor_filters = esp32_exception_decoder, time

; ---------------------------------------------------------------------------
; Host tests. Builds ONLY the pure libraries - no Arduino, no hardware.
; ---------------------------------------------------------------------------
[env:native]
platform = native
build_flags = -std=c++17 -Wall -Wextra
build_src_filter = -<*>
lib_compat_mode = off
lib_deps =
    bblanchon/ArduinoJson @ ^7.0.0
```

- [ ] **Step 3: Write `include/secrets.h.template`**

```cpp
// Copy this file to include/secrets.h and fill in your values.
// secrets.h is gitignored; this template is not.

#pragma once

#define WIFI_SSID     "your-network"
#define WIFI_PASSWORD "your-password"

// MQTT broker. Anonymous access; no username or password needed.
#define MQTT_HOST "192.0.2.10"
#define MQTT_PORT 1883

// Maple Valley, WA. Confirmed against the National Weather Service, which
// resolves these coordinates to "Maple Valley WA".
#define WEATHER_LATITUDE  "47.3673"
#define WEATHER_LONGITUDE "-122.0437"
```

- [ ] **Step 4: Run the vendored tests to verify the toolchain works**

Run: `pio test -e native`
Expected: PASS, 26 tests in `test_history`. If PlatformIO is not installed, stop and report that rather than working around it.

- [ ] **Step 5: Commit**

```bash
git add platformio.ini lib/history test/test_history include/secrets.h.template
git commit -m "Add build config and vendor the host-testable history library

The ring buffer and chart downsampler are generic and timestamp-based, with
no Arduino dependency, so they come across from the sibling CrowPanel project
unchanged along with their 26 tests.

Build settings differ from that project in ways that matter: 16 MB flash
rather than 4, and the pioarduino platform fork Elecrow require.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

## Task 2: MQTT payload parsing

**Files:**
- Create: `lib/parse/parse.h`, `lib/parse/parse.cpp`, `lib/parse/library.json`
- Test: `test/test_parse/test_parse.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: `bool parse::decimal(const char* payload, size_t len, float& out)` — returns true and sets `out` only for a well-formed decimal number; returns false and leaves `out` untouched otherwise.

The broker publishes bare decimal ASCII, not JSON. Payloads are not null-terminated by PubSubClient, so the length must be respected. A bad parse must never enter the chart.

- [ ] **Step 1: Write `lib/parse/library.json`**

```json
{
  "name": "parse",
  "version": "1.0.0",
  "description": "Payload parsing for MQTT bare-decimal values and Open-Meteo JSON. Pure C++17, host-testable.",
  "build": {
    "flags": ["-std=c++17"]
  }
}
```

- [ ] **Step 2: Write the failing tests**

Create `test/test_parse/test_parse.cpp`:

```cpp
// Host tests for lib/parse: MQTT bare-decimal payloads and Open-Meteo JSON.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic).

#include <unity.h>

#include <cstring>

#include "parse.h"

static constexpr float kEps = 1e-4f;

void setUp(void) {}
void tearDown(void) {}

static bool dec(const char* s, float& out) {
    return parse::decimal(s, std::strlen(s), out);
}

// ---------------------------------------------------------------------------
// Well-formed payloads, exactly as the broker sends them
// ---------------------------------------------------------------------------

static void test_decimal_typical_temperature(void) {
    float v = 0.0f;
    TEST_ASSERT_TRUE(dec("19.6", v));
    TEST_ASSERT_FLOAT_WITHIN(kEps, 19.6f, v);
}

static void test_decimal_humidity_with_one_place(void) {
    float v = 0.0f;
    TEST_ASSERT_TRUE(dec("55.1", v));
    TEST_ASSERT_FLOAT_WITHIN(kEps, 55.1f, v);
}

static void test_decimal_integer_without_point(void) {
    float v = 0.0f;
    TEST_ASSERT_TRUE(dec("21", v));
    TEST_ASSERT_FLOAT_WITHIN(kEps, 21.0f, v);
}

static void test_decimal_negative(void) {
    float v = 0.0f;
    TEST_ASSERT_TRUE(dec("-7.5", v));
    TEST_ASSERT_FLOAT_WITHIN(kEps, -7.5f, v);
}

static void test_decimal_uptime_style_trailing_zero(void) {
    float v = 0.0f;
    TEST_ASSERT_TRUE(dec("20044.0", v));
    TEST_ASSERT_FLOAT_WITHIN(1e-2f, 20044.0f, v);
}

static void test_decimal_leading_and_trailing_space(void) {
    float v = 0.0f;
    TEST_ASSERT_TRUE(dec("  19.6  ", v));
    TEST_ASSERT_FLOAT_WITHIN(kEps, 19.6f, v);
}

// ---------------------------------------------------------------------------
// The payload is NOT null-terminated by PubSubClient
// ---------------------------------------------------------------------------

static void test_decimal_respects_length_and_ignores_trailing_bytes(void) {
    // A real PubSubClient buffer: the digits, then unrelated bytes after.
    const char buf[] = {'1', '9', '.', '6', 'X', 'Y', 'Z'};
    float v = 0.0f;
    TEST_ASSERT_TRUE(parse::decimal(buf, 4, v));
    TEST_ASSERT_FLOAT_WITHIN(kEps, 19.6f, v);
}

// ---------------------------------------------------------------------------
// Malformed input must be rejected and must not touch the output
// ---------------------------------------------------------------------------

static void test_decimal_rejects_empty(void) {
    float v = 123.0f;
    TEST_ASSERT_FALSE(parse::decimal("", 0, v));
    TEST_ASSERT_FLOAT_WITHIN(kEps, 123.0f, v);
}

static void test_decimal_rejects_null_pointer(void) {
    float v = 123.0f;
    TEST_ASSERT_FALSE(parse::decimal(nullptr, 4, v));
    TEST_ASSERT_FLOAT_WITHIN(kEps, 123.0f, v);
}

static void test_decimal_rejects_letters(void) {
    float v = 123.0f;
    TEST_ASSERT_FALSE(dec("nan", v));
    TEST_ASSERT_FLOAT_WITHIN(kEps, 123.0f, v);
}

static void test_decimal_rejects_trailing_garbage(void) {
    float v = 123.0f;
    TEST_ASSERT_FALSE(dec("19.6C", v));
    TEST_ASSERT_FLOAT_WITHIN(kEps, 123.0f, v);
}

static void test_decimal_rejects_json(void) {
    float v = 123.0f;
    TEST_ASSERT_FALSE(dec("{\"t\":19.6}", v));
    TEST_ASSERT_FLOAT_WITHIN(kEps, 123.0f, v);
}

static void test_decimal_rejects_infinity(void) {
    float v = 123.0f;
    TEST_ASSERT_FALSE(dec("inf", v));
    TEST_ASSERT_FLOAT_WITHIN(kEps, 123.0f, v);
}

static void test_decimal_rejects_only_whitespace(void) {
    float v = 123.0f;
    TEST_ASSERT_FALSE(dec("   ", v));
    TEST_ASSERT_FLOAT_WITHIN(kEps, 123.0f, v);
}

int main(int, char**) {
    UNITY_BEGIN();

    RUN_TEST(test_decimal_typical_temperature);
    RUN_TEST(test_decimal_humidity_with_one_place);
    RUN_TEST(test_decimal_integer_without_point);
    RUN_TEST(test_decimal_negative);
    RUN_TEST(test_decimal_uptime_style_trailing_zero);
    RUN_TEST(test_decimal_leading_and_trailing_space);

    RUN_TEST(test_decimal_respects_length_and_ignores_trailing_bytes);

    RUN_TEST(test_decimal_rejects_empty);
    RUN_TEST(test_decimal_rejects_null_pointer);
    RUN_TEST(test_decimal_rejects_letters);
    RUN_TEST(test_decimal_rejects_trailing_garbage);
    RUN_TEST(test_decimal_rejects_json);
    RUN_TEST(test_decimal_rejects_infinity);
    RUN_TEST(test_decimal_rejects_only_whitespace);

    return UNITY_END();
}
```

- [ ] **Step 3: Run the tests to verify they fail**

Run: `pio test -e native -f test_parse`
Expected: FAIL to compile, `parse.h: No such file or directory`.

- [ ] **Step 4: Write `lib/parse/parse.h`**

```cpp
// Payload parsing for the two data sources.
//
// Deliberately free of Arduino, ESP-IDF and LVGL headers so the parsing - the
// part most likely to be handed malformed input - is tested on the host.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic).

#pragma once

#include <cstddef>

namespace parse {

// A bare decimal ASCII payload, which is what the broker publishes: "19.6",
// "55.1", "21", "-7.5". NOT JSON.
//
// The buffer is NOT required to be null-terminated, because PubSubClient hands
// its callback a pointer into its own receive buffer with a separate length.
// Surrounding whitespace is tolerated; anything else is a rejection.
//
// Returns false and leaves `out` untouched on any malformed input, including
// trailing characters, "nan" and "inf". A rejected payload must never reach
// the chart, so silence is better than a plausible-looking wrong number.
bool decimal(const char* payload, size_t len, float& out);

}  // namespace parse
```

- [ ] **Step 5: Write `lib/parse/parse.cpp`**

```cpp
// Implementation of the payload parsers.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic).

#include "parse.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace parse {
namespace {

bool isSpace(char c) {
    return std::isspace(static_cast<unsigned char>(c)) != 0;
}

}  // namespace

bool decimal(const char* payload, size_t len, float& out) {
    if (payload == nullptr || len == 0) return false;

    // Copy into a bounded, null-terminated buffer. strtof needs a terminator
    // and the caller's buffer has none. 31 characters is far more than any
    // reading needs and keeps this off the heap.
    char buf[32];
    if (len >= sizeof(buf)) return false;
    std::memcpy(buf, payload, len);
    buf[len] = '\0';

    // Trim both ends. Leading whitespace strtof would skip anyway; trailing
    // whitespace it would leave in `end` and we would wrongly reject.
    size_t begin = 0;
    size_t stop  = len;
    while (begin < stop && isSpace(buf[begin])) ++begin;
    while (stop > begin && isSpace(buf[stop - 1])) --stop;
    if (begin == stop) return false;
    buf[stop] = '\0';

    // Reject "nan" and "inf" before strtof, which accepts both. A sensor that
    // reports NaN has failed, and a NaN in the ring buffer poisons every mean
    // computed from it.
    const char first = buf[begin];
    if (!(std::isdigit(static_cast<unsigned char>(first)) || first == '-' ||
          first == '+' || first == '.')) {
        return false;
    }

    char*       end = nullptr;
    const float v   = std::strtof(buf + begin, &end);

    if (end == buf + begin) return false;  // nothing consumed
    if (*end != '\0') return false;        // trailing garbage
    if (!std::isfinite(v)) return false;

    out = v;
    return true;
}

}  // namespace parse
```

- [ ] **Step 6: Run the tests to verify they pass**

Run: `pio test -e native -f test_parse`
Expected: PASS, 14 tests.

- [ ] **Step 7: Commit**

```bash
git add lib/parse test/test_parse
git commit -m "Add MQTT bare-decimal payload parsing

The broker publishes plain numbers rather than JSON, and PubSubClient hands
its callback an unterminated pointer into its own buffer, so the length has
to be respected rather than assumed.

Rejects NaN and infinity explicitly. strtof accepts both, and a NaN reaching
the ring buffer would poison every mean computed from it afterwards.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

## Task 3: Open-Meteo JSON parsing

**Files:**
- Modify: `lib/parse/parse.h`, `lib/parse/parse.cpp`
- Modify: `test/test_parse/test_parse.cpp`

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces: `struct parse::Weather { float temperature_c; float humidity_pct; };` and `bool parse::openMeteo(const char* json, size_t len, Weather& out)` — returns true only when both fields were present and numeric.

A real response, captured on 2026-09-11, is the fixture. Temperature comes back in Celsius because the request omits `temperature_unit`.

- [ ] **Step 1: Add the failing tests to `test/test_parse/test_parse.cpp`**

Insert before `int main`:

```cpp
// ---------------------------------------------------------------------------
// Open-Meteo. The fixture is a real response captured on 2026-09-11.
// ---------------------------------------------------------------------------

static const char kRealResponse[] =
    "{\"latitude\":47.37403,\"longitude\":-122.03002,"
    "\"generationtime_ms\":0.2496,\"utc_offset_seconds\":-25200,"
    "\"timezone\":\"America/Los_Angeles\",\"timezone_abbreviation\":\"GMT-7\","
    "\"elevation\":150.0,\"current_units\":{\"time\":\"iso8601\","
    "\"interval\":\"seconds\",\"temperature_2m\":\"C\","
    "\"relative_humidity_2m\":\"%\",\"weather_code\":\"wmo code\"},"
    "\"current\":{\"time\":\"2026-09-11T10:30\",\"interval\":900,"
    "\"temperature_2m\":16.1,\"relative_humidity_2m\":62,"
    "\"weather_code\":3}}";

static bool om(const char* s, parse::Weather& out) {
    return parse::openMeteo(s, std::strlen(s), out);
}

static void test_openmeteo_real_response(void) {
    parse::Weather w{};
    TEST_ASSERT_TRUE(om(kRealResponse, w));
    TEST_ASSERT_FLOAT_WITHIN(kEps, 16.1f, w.temperature_c);
    TEST_ASSERT_FLOAT_WITHIN(kEps, 62.0f, w.humidity_pct);
}

static void test_openmeteo_minimal_object(void) {
    parse::Weather w{};
    TEST_ASSERT_TRUE(om("{\"current\":{\"temperature_2m\":-3.25,"
                        "\"relative_humidity_2m\":91}}", w));
    TEST_ASSERT_FLOAT_WITHIN(kEps, -3.25f, w.temperature_c);
    TEST_ASSERT_FLOAT_WITHIN(kEps, 91.0f, w.humidity_pct);
}

static void test_openmeteo_rejects_missing_temperature(void) {
    parse::Weather w{};
    w.temperature_c = 99.0f;
    TEST_ASSERT_FALSE(om("{\"current\":{\"relative_humidity_2m\":62}}", w));
    TEST_ASSERT_FLOAT_WITHIN(kEps, 99.0f, w.temperature_c);
}

static void test_openmeteo_rejects_missing_humidity(void) {
    parse::Weather w{};
    TEST_ASSERT_FALSE(om("{\"current\":{\"temperature_2m\":16.1}}", w));
}

static void test_openmeteo_rejects_missing_current_object(void) {
    parse::Weather w{};
    TEST_ASSERT_FALSE(om("{\"latitude\":47.37,\"elevation\":150.0}", w));
}

static void test_openmeteo_rejects_truncated_response(void) {
    parse::Weather w{};
    TEST_ASSERT_FALSE(om("{\"current\":{\"temperature_2m\":16", w));
}

static void test_openmeteo_rejects_empty_and_null(void) {
    parse::Weather w{};
    TEST_ASSERT_FALSE(parse::openMeteo("", 0, w));
    TEST_ASSERT_FALSE(parse::openMeteo(nullptr, 10, w));
}

static void test_openmeteo_rejects_api_error_body(void) {
    parse::Weather w{};
    TEST_ASSERT_FALSE(om("{\"error\":true,\"reason\":\"Latitude must be in "
                         "range of -90 to 90\"}", w));
}

static void test_openmeteo_rejects_non_numeric_temperature(void) {
    parse::Weather w{};
    TEST_ASSERT_FALSE(om("{\"current\":{\"temperature_2m\":null,"
                         "\"relative_humidity_2m\":62}}", w));
}
```

Add these to `main` before `return UNITY_END();`:

```cpp
    RUN_TEST(test_openmeteo_real_response);
    RUN_TEST(test_openmeteo_minimal_object);
    RUN_TEST(test_openmeteo_rejects_missing_temperature);
    RUN_TEST(test_openmeteo_rejects_missing_humidity);
    RUN_TEST(test_openmeteo_rejects_missing_current_object);
    RUN_TEST(test_openmeteo_rejects_truncated_response);
    RUN_TEST(test_openmeteo_rejects_empty_and_null);
    RUN_TEST(test_openmeteo_rejects_api_error_body);
    RUN_TEST(test_openmeteo_rejects_non_numeric_temperature);
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `pio test -e native -f test_parse`
Expected: FAIL to compile, `openMeteo` is not a member of `parse`.

- [ ] **Step 3: Add the declaration to `lib/parse/parse.h`**

Insert after the `decimal` declaration, inside `namespace parse`:

```cpp
// One current-conditions reading from Open-Meteo.
struct Weather {
    float temperature_c;
    float humidity_pct;
};

// Extracts `current.temperature_2m` and `current.relative_humidity_2m` from an
// Open-Meteo response body.
//
// Temperature is Celsius because the request omits `temperature_unit`, whose
// default is Celsius. Keep it that way: history stores Celsius throughout and
// converts only at display time.
//
// Returns false and leaves `out` untouched unless BOTH fields were present and
// numeric. A partially parsed reading is worse than none - it would show a real
// temperature beside a humidity of zero, with nothing to indicate which is
// which. Also returns false for the API's error body, which has no `current`.
bool openMeteo(const char* json, size_t len, Weather& out);
```

- [ ] **Step 4: Add the implementation to `lib/parse/parse.cpp`**

Add `#include <ArduinoJson.h>` at the top with the other includes, then add inside `namespace parse`:

```cpp
bool openMeteo(const char* json, size_t len, Weather& out) {
    if (json == nullptr || len == 0) return false;

    // The response is around 400 bytes. 2 KB leaves generous headroom for
    // Open-Meteo adding fields without this needing a change.
    JsonDocument doc;
    if (deserializeJson(doc, json, len) != DeserializationError::Ok) return false;

    JsonVariantConst current = doc["current"];
    if (current.isNull()) return false;

    JsonVariantConst t = current["temperature_2m"];
    JsonVariantConst h = current["relative_humidity_2m"];

    // is<float>() is false for null, for a string and for a missing key, which
    // is exactly the set we want to reject.
    if (!t.is<float>() || !h.is<float>()) return false;

    out.temperature_c = t.as<float>();
    out.humidity_pct  = h.as<float>();
    return true;
}
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `pio test -e native -f test_parse`
Expected: PASS, 23 tests.

- [ ] **Step 6: Commit**

```bash
git add lib/parse test/test_parse
git commit -m "Add Open-Meteo response parsing

Fixture is a real response body captured from the live API, so the test
breaks if Open-Meteo changes its shape rather than passing against an
idealised structure that never existed.

Requires both fields before reporting success. A partially parsed reading
would put a real temperature beside a humidity of zero with nothing on
screen to say which one is untrustworthy.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

## Task 4: The channel — latest reading, staleness, and history

**Files:**
- Create: `lib/channel/Channel.h`, `lib/channel/Channel.cpp`, `lib/channel/library.json`
- Test: `test/test_channel/test_channel.cpp`

**Interfaces:**
- Consumes: `history::SampleHistory` and `history::Sample` from Task 1.
- Produces:
  - `struct channel::Reading { float temperature_c; float humidity_pct; uint32_t t_ms; bool valid; };`
  - `class channel::Channel` with constructor `Channel(const char* name, uint32_t stale_after_ms, size_t capacity)`, and methods `const char* name() const`, `void update(float temperature_c, float humidity_pct, uint32_t now_ms)`, `const Reading& latest() const`, `bool isStale(uint32_t now_ms) const`, `uint32_t staleAfterMs() const`, `history::SampleHistory& history()`, `const history::SampleHistory& history() const`.

This is where the spec's per-channel staleness rule lives. The office publishes every 10 seconds and the weather every 15 minutes, so one shared timeout would either cry wolf on the weather or hide a dead broker.

- [ ] **Step 1: Write `lib/channel/library.json`**

```json
{
  "name": "channel",
  "version": "1.0.0",
  "description": "One data channel: name, latest reading, per-channel staleness rule, and its sample history. Pure C++17, host-testable.",
  "dependencies": {
    "history": "*"
  },
  "build": {
    "flags": ["-std=c++17"]
  }
}
```

- [ ] **Step 2: Write the failing tests**

Create `test/test_channel/test_channel.cpp`:

```cpp
// Host tests for lib/channel: reading updates, per-channel staleness, and the
// millis() rollover.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic).

#include <unity.h>

#include <cstring>

#include "Channel.h"

using channel::Channel;

static constexpr float kEps = 1e-4f;

// The two real channels' thresholds, from the spec.
static constexpr uint32_t kOfficeStaleMs  = 60UL * 1000UL;
static constexpr uint32_t kWeatherStaleMs = 45UL * 60UL * 1000UL;

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// Before any reading arrives
// ---------------------------------------------------------------------------

static void test_new_channel_has_no_valid_reading(void) {
    Channel c("Mark's Office", kOfficeStaleMs, 16);
    TEST_ASSERT_FALSE(c.latest().valid);
    TEST_ASSERT_EQUAL_STRING("Mark's Office", c.name());
    TEST_ASSERT_TRUE(c.history().empty());
}

static void test_new_channel_is_stale_immediately(void) {
    // Nothing has ever arrived, so the reading cannot be trusted. Reporting
    // "fresh" here would show a zero as though it were a measurement.
    Channel c("Mark's Office", kOfficeStaleMs, 16);
    TEST_ASSERT_TRUE(c.isStale(0));
    TEST_ASSERT_TRUE(c.isStale(5000));
}

// ---------------------------------------------------------------------------
// Updating
// ---------------------------------------------------------------------------

static void test_update_sets_latest_and_appends_history(void) {
    Channel c("Mark's Office", kOfficeStaleMs, 16);
    c.update(19.6f, 55.1f, 1000);

    TEST_ASSERT_TRUE(c.latest().valid);
    TEST_ASSERT_FLOAT_WITHIN(kEps, 19.6f, c.latest().temperature_c);
    TEST_ASSERT_FLOAT_WITHIN(kEps, 55.1f, c.latest().humidity_pct);
    TEST_ASSERT_EQUAL_UINT32(1000, c.latest().t_ms);

    TEST_ASSERT_EQUAL_size_t(1, c.history().size());
    TEST_ASSERT_FLOAT_WITHIN(kEps, 19.6f, c.history().newest().temperature_c);
    TEST_ASSERT_EQUAL_UINT32(1000, c.history().newest().t_ms);
}

static void test_repeated_updates_all_land_in_history(void) {
    Channel c("Mark's Office", kOfficeStaleMs, 16);
    for (uint32_t i = 0; i < 5; ++i) {
        c.update(20.0f + i, 50.0f + i, 1000 * i);
    }
    TEST_ASSERT_EQUAL_size_t(5, c.history().size());
    TEST_ASSERT_FLOAT_WITHIN(kEps, 24.0f, c.latest().temperature_c);
    TEST_ASSERT_FLOAT_WITHIN(kEps, 20.0f, c.history().oldest().temperature_c);
}

// ---------------------------------------------------------------------------
// Staleness, per channel
// ---------------------------------------------------------------------------

static void test_office_fresh_within_threshold(void) {
    Channel c("Mark's Office", kOfficeStaleMs, 16);
    c.update(19.6f, 55.1f, 10000);
    TEST_ASSERT_FALSE(c.isStale(10000));
    TEST_ASSERT_FALSE(c.isStale(10000 + 59000));
}

static void test_office_stale_past_threshold(void) {
    Channel c("Mark's Office", kOfficeStaleMs, 16);
    c.update(19.6f, 55.1f, 10000);
    TEST_ASSERT_TRUE(c.isStale(10000 + 61000));
}

static void test_office_boundary_is_not_yet_stale(void) {
    Channel c("Mark's Office", kOfficeStaleMs, 16);
    c.update(19.6f, 55.1f, 10000);
    TEST_ASSERT_FALSE(c.isStale(10000 + kOfficeStaleMs));
    TEST_ASSERT_TRUE(c.isStale(10000 + kOfficeStaleMs + 1));
}

static void test_weather_tolerates_a_gap_that_would_fail_the_office(void) {
    // The whole point of per-channel thresholds. Ten minutes of silence is a
    // dead broker for the office and completely normal for the weather.
    const uint32_t ten_minutes = 10UL * 60UL * 1000UL;

    Channel office("Mark's Office", kOfficeStaleMs, 16);
    Channel weather("Maple Valley", kWeatherStaleMs, 16);
    office.update(19.6f, 55.1f, 0);
    weather.update(16.1f, 62.0f, 0);

    TEST_ASSERT_TRUE(office.isStale(ten_minutes));
    TEST_ASSERT_FALSE(weather.isStale(ten_minutes));
}

static void test_weather_stale_past_its_own_threshold(void) {
    Channel c("Maple Valley", kWeatherStaleMs, 16);
    c.update(16.1f, 62.0f, 0);
    TEST_ASSERT_TRUE(c.isStale(kWeatherStaleMs + 1));
}

static void test_update_refreshes_staleness(void) {
    Channel c("Mark's Office", kOfficeStaleMs, 16);
    c.update(19.6f, 55.1f, 0);
    TEST_ASSERT_TRUE(c.isStale(120000));
    c.update(19.7f, 55.0f, 120000);
    TEST_ASSERT_FALSE(c.isStale(120000));
}

// ---------------------------------------------------------------------------
// millis() rollover at 2^32 ms, about 49.7 days
// ---------------------------------------------------------------------------

static void test_staleness_survives_millis_rollover(void) {
    Channel c("Mark's Office", kOfficeStaleMs, 16);

    const uint32_t before = 0xFFFFFF00UL;  // 256 ms before the wrap
    c.update(19.6f, 55.1f, before);

    // 10 s later, having wrapped through zero. Unsigned subtraction keeps the
    // elapsed time correct, so this must read as fresh rather than as 49 days.
    const uint32_t after = before + 10000UL;
    TEST_ASSERT_FALSE(c.isStale(after));

    TEST_ASSERT_TRUE(c.isStale(before + kOfficeStaleMs + 1000UL));
}

// ---------------------------------------------------------------------------
// History capacity
// ---------------------------------------------------------------------------

static void test_history_evicts_oldest_at_capacity(void) {
    Channel c("Mark's Office", kOfficeStaleMs, 4);
    for (uint32_t i = 0; i < 6; ++i) c.update(20.0f + i, 50.0f, 1000 * i);

    TEST_ASSERT_EQUAL_size_t(4, c.history().size());
    TEST_ASSERT_FLOAT_WITHIN(kEps, 22.0f, c.history().oldest().temperature_c);
    TEST_ASSERT_FLOAT_WITHIN(kEps, 25.0f, c.history().newest().temperature_c);
    // The latest reading is unaffected by eviction.
    TEST_ASSERT_FLOAT_WITHIN(kEps, 25.0f, c.latest().temperature_c);
}

int main(int, char**) {
    UNITY_BEGIN();

    RUN_TEST(test_new_channel_has_no_valid_reading);
    RUN_TEST(test_new_channel_is_stale_immediately);

    RUN_TEST(test_update_sets_latest_and_appends_history);
    RUN_TEST(test_repeated_updates_all_land_in_history);

    RUN_TEST(test_office_fresh_within_threshold);
    RUN_TEST(test_office_stale_past_threshold);
    RUN_TEST(test_office_boundary_is_not_yet_stale);
    RUN_TEST(test_weather_tolerates_a_gap_that_would_fail_the_office);
    RUN_TEST(test_weather_stale_past_its_own_threshold);
    RUN_TEST(test_update_refreshes_staleness);

    RUN_TEST(test_staleness_survives_millis_rollover);

    RUN_TEST(test_history_evicts_oldest_at_capacity);

    return UNITY_END();
}
```

- [ ] **Step 3: Run the tests to verify they fail**

Run: `pio test -e native -f test_channel`
Expected: FAIL to compile, `Channel.h: No such file or directory`.

- [ ] **Step 4: Write `lib/channel/Channel.h`**

```cpp
// One data channel: where a reading came from, what it says now, whether it can
// still be trusted, and its history.
//
// Each channel carries its own staleness threshold because the two sources
// differ by two orders of magnitude in cadence. The office publishes every ten
// seconds, so a minute of silence means something broke. Maple Valley updates
// every fifteen minutes, so a minute of silence is normal. A single shared
// timeout would either cry wolf on the weather or hide a dead broker.
//
// Free of Arduino, ESP-IDF and LVGL headers so the staleness logic is tested on
// the host. Time arrives as a millis()-style uint32_t the caller supplies.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic).

#pragma once

#include <cstddef>
#include <cstdint>

#include "SampleHistory.h"

namespace channel {

struct Reading {
    float    temperature_c = 0.0f;
    float    humidity_pct  = 0.0f;
    uint32_t t_ms          = 0;
    bool     valid         = false;  // false until the first update
};

class Channel {
  public:
    // `name` must outlive the Channel; in practice it is a string literal.
    // `capacity` of 0 is legal and yields a channel that tracks the latest
    // reading but retains no history.
    Channel(const char* name, uint32_t stale_after_ms, size_t capacity);

    const char* name() const { return name_; }
    uint32_t    staleAfterMs() const { return stale_after_ms_; }

    // Records a new reading and appends it to history. `now_ms` is the
    // millis() value at which the reading was taken.
    void update(float temperature_c, float humidity_pct, uint32_t now_ms);

    const Reading& latest() const { return latest_; }

    // True when the reading should no longer be shown as live. Always true
    // before the first update: showing an uninitialised zero as a measurement
    // is worse than showing an obvious gap.
    bool isStale(uint32_t now_ms) const;

    history::SampleHistory&       history() { return history_; }
    const history::SampleHistory& history() const { return history_; }

  private:
    const char*             name_;
    uint32_t                stale_after_ms_;
    Reading                 latest_;
    history::SampleHistory  history_;
};

}  // namespace channel
```

- [ ] **Step 5: Write `lib/channel/Channel.cpp`**

```cpp
// Implementation of Channel.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic).

#include "Channel.h"

namespace channel {

Channel::Channel(const char* name, uint32_t stale_after_ms, size_t capacity)
    : name_(name), stale_after_ms_(stale_after_ms), history_(capacity) {}

void Channel::update(float temperature_c, float humidity_pct, uint32_t now_ms) {
    latest_.temperature_c = temperature_c;
    latest_.humidity_pct  = humidity_pct;
    latest_.t_ms          = now_ms;
    latest_.valid         = true;

    history_.add(history::Sample{now_ms, temperature_c, humidity_pct});
}

bool Channel::isStale(uint32_t now_ms) const {
    if (!latest_.valid) return true;

    // Unsigned subtraction, so the age stays correct straight through the
    // millis() rollover at 2^32 ms (about 49.7 days) as long as the real
    // elapsed time is under that. Same reasoning as SampleHistory::downsample.
    const uint32_t age = now_ms - latest_.t_ms;
    return age > stale_after_ms_;
}

}  // namespace channel
```

- [ ] **Step 6: Run the tests to verify they pass**

Run: `pio test -e native -f test_channel`
Expected: PASS, 12 tests.

- [ ] **Step 7: Run the whole host suite**

Run: `pio test -e native`
Expected: PASS, 61 tests across `test_history`, `test_parse` and `test_channel`.

- [ ] **Step 8: Commit**

```bash
git add lib/channel test/test_channel
git commit -m "Add Channel with per-channel staleness

The two sources differ by two orders of magnitude in cadence, so a shared
timeout cannot serve both: ten minutes of silence is a dead broker for the
office and entirely normal for the weather.

A channel with no reading yet reports stale rather than fresh, so an
uninitialised zero is never shown as though it were a measurement.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

> **Everything from here on cannot be tested until the board arrives.** Each task ends with a compile, not a test run. Do not claim any of it works. `docs/HARDWARE.md` section 8 is the arrival checklist.

---

## Task 5: Board pin definitions

**Files:**
- Create: `src/board_pins.h`

**Interfaces:**
- Consumes: nothing.
- Produces: namespace `board` with every constant used by Tasks 6 through 13. Notably `LCD_R0..R4`, `LCD_G0..G5`, `LCD_B0..B4`, `LCD_DE`, `LCD_VSYNC`, `LCD_HSYNC`, `LCD_PCLK`, `LCD_WIDTH`, `LCD_HEIGHT`, `LCD_PCLK_HZ`, the six porch constants, `I2C_SDA`, `I2C_SCL`, `I2C_HZ`, `GT911_ADDR_PRIMARY`, `GT911_ADDR_BACKUP`, `PANEL_MCU_ADDR`, `TOUCH_POLL_MS`.

- [ ] **Step 1: Write `src/board_pins.h`**

```cpp
// Pin assignments for the Elecrow CrowPanel Advance 7.0-HMI.
//
// SKU DIS02170A, ESP32-S3-WROOM-1-N16R8.
//
// Every value here is quoted from Elecrow's own LovyanGFX_Driver.h and
// cross-checked against the net names in their V1.5 Eagle schematic. See
// docs/HARDWARE.md for sources and for the places where Elecrow's own
// documentation contradicts itself.
//
// DO NOT copy values from the sibling CrowPanel 7.0 HMI project. That board is
// a different product: different I2C pins, different porches, a quarter of the
// flash, and a backlight on a GPIO rather than behind an I2C command.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic).

#pragma once

#include <cstdint>

// Board revision. The backlight encoding, the touch activation command and the
// pixel clock all depend on it, and it is printed on the silkscreen.
//   130 = V1.3, V1.4, V1.5   (current stock)
//   120 = V1.2
// Set this once the physical board is in hand. Until then panel_mcu probes.
#ifndef CROWPANEL_ADVANCE_REV
#define CROWPANEL_ADVANCE_REV 130
#endif

namespace board {

// ---------------------------------------------------------------------------
// Display: 16-bit RGB565 parallel. Identical in every revision, V1.0 to V1.5.
// ---------------------------------------------------------------------------
// Elecrow name the schematic nets in RGB888 bit positions, so the lowest red
// line is called R3 and the highest R7. That is correct: a 16-bit bus feeding a
// 6-bit-per-channel panel lands on R3-R7, G2-G7 and B3-B7, and the panel's
// unused low bits are tied to ground. Do not renumber them.
constexpr int LCD_R0 = 7;    // net IO7_R3
constexpr int LCD_R1 = 17;   // net IO17_R4
constexpr int LCD_R2 = 18;   // net IO18_R5
constexpr int LCD_R3 = 3;    // net IO3_R6   - also a strapping pin
constexpr int LCD_R4 = 46;   // net IO46_R7  - also a strapping pin

constexpr int LCD_G0 = 9;    // net IO9_G2
constexpr int LCD_G1 = 10;   // net IO10_G3
constexpr int LCD_G2 = 11;   // net IO11_G4
constexpr int LCD_G3 = 12;   // net IO12_G5
constexpr int LCD_G4 = 13;   // net IO13_G6
constexpr int LCD_G5 = 14;   // net IO14_G7

constexpr int LCD_B0 = 21;   // net IO21_B3
constexpr int LCD_B1 = 47;   // net IO47_B4
constexpr int LCD_B2 = 48;   // net IO48_B5  - also a strapping pin
constexpr int LCD_B3 = 45;   // net IO45_B6  - also a strapping pin
constexpr int LCD_B4 = 38;   // net IO38_B7

constexpr int LCD_DE    = 42;  // net IO42_DE
constexpr int LCD_VSYNC = 41;  // net IO41_VSYNC
constexpr int LCD_HSYNC = 40;  // net IO40_HSYNC
constexpr int LCD_PCLK  = 39;  // net IO39_CLK_DCLK

constexpr int LCD_WIDTH  = 800;
constexpr int LCD_HEIGHT = 480;

// Elecrow LOWERED the pixel clock between V1.2 and V1.3. Read verbatim from
// their three driver files. This single fact reconciles most of the
// contradictory advice in circulation.
#if CROWPANEL_ADVANCE_REV >= 130
constexpr uint32_t LCD_PCLK_HZ = 16000000;
#else
constexpr uint32_t LCD_PCLK_HZ = 21000000;
#endif

// Porches are identical in every revision, and both axes use the same values.
// That looks like a copy-paste error and is not.
constexpr int LCD_HSYNC_POLARITY    = 0;
constexpr int LCD_HSYNC_FRONT_PORCH = 8;
constexpr int LCD_HSYNC_PULSE_WIDTH = 4;
constexpr int LCD_HSYNC_BACK_PORCH  = 8;

constexpr int LCD_VSYNC_POLARITY    = 0;
constexpr int LCD_VSYNC_FRONT_PORCH = 8;
constexpr int LCD_VSYNC_PULSE_WIDTH = 4;
constexpr int LCD_VSYNC_BACK_PORCH  = 8;

constexpr bool LCD_PCLK_IDLE_HIGH = true;

// ---------------------------------------------------------------------------
// Backlight: NOT A GPIO.
// ---------------------------------------------------------------------------
// The MT9201 boost is driven by a companion microcontroller. You turn the
// backlight on by sending an I2C command. See panel_mcu.h. Elecrow's own
// driver has the LovyanGFX Light_PWM block commented out entirely, including
// `// cfg.pin_bl = GPIO_NUM_2;` - there is no LovyanGFX-managed backlight here.

// ---------------------------------------------------------------------------
// I2C: touch, the companion MCU, the real-time clock, and the external header.
// ---------------------------------------------------------------------------
constexpr int      I2C_SDA = 15;
constexpr int      I2C_SCL = 16;
constexpr uint32_t I2C_HZ  = 400000;

// GT911 capacitive touch, 5-point. It latches its address from the state of its
// interrupt line during reset, so probe both rather than assuming.
constexpr uint8_t GT911_ADDR_PRIMARY = 0x5D;  // INT low at reset
constexpr uint8_t GT911_ADDR_BACKUP  = 0x14;  // INT high at reset

// The GT911 interrupt is wired to this pin even though the driver polls. It is
// held low across reset to force address 0x5D.
constexpr int TP_INT = 1;

// STC8H1K28 companion MCU: backlight, touch reset, buzzer, amplifier control
// and battery charger status. V1.2 and later. On V1.0 this is an I/O expander
// at 0x18 instead, which this firmware does not support.
constexpr uint8_t PANEL_MCU_ADDR = 0x30;

// PCF8563 real-time clock with a CR1220 backup cell. Unused by this firmware,
// recorded so nothing else claims the address. Note that 0x51 comes from the
// part's datasheet: no Elecrow code touches the RTC.
constexpr uint8_t RTC_ADDR = 0x51;

// ---------------------------------------------------------------------------
// Timing.
// ---------------------------------------------------------------------------
// Blocking GT911 reads on the shared I2C bus starve the RGB panel's DMA and
// produce visible display shake. This is Elecrow's own issue #8. Poll on a
// fixed interval, never per-loop.
constexpr uint32_t TOUCH_POLL_MS = 20;

// ---------------------------------------------------------------------------
// Unused here, recorded so nothing else claims them by accident.
// ---------------------------------------------------------------------------
// GPIO4/5/6 are microSD and the I2S amplifier, multiplexed by a CH486F analog
// switch under the S0/S1 DIP switches. GPIO19/20 are the PDM microphone, the
// radio socket and UART1, behind a second switch. GPIO43/44 are UART0 to the
// CH340K. GPIO0 is the boot strapping pin and the auto-download circuit.
//
// GPIO2 and GPIO8 are the ONLY genuinely free signal pins, and only on V1.3+.
// On V1.0 and V1.2 GPIO2 is the I2S microphone's word-select and GPIO8 is the
// buzzer.
constexpr int FREE_GPIO_A = 2;   // V1.3+ only
constexpr int FREE_GPIO_B = 8;   // V1.3+ only

// GPIO26-32 are the SPI flash and GPIO33-37 are consumed by the octal PSRAM on
// this R8 module. Neither group is usable. GPIO22-25 do not exist on the S3.

}  // namespace board
```

- [ ] **Step 2: Verify it compiles standalone**

Run: `echo '#include "src/board_pins.h"
int main(){ return board::LCD_WIDTH == 800 ? 0 : 1; }' > /tmp/pins_check.cpp && c++ -std=c++17 -I. /tmp/pins_check.cpp -o /tmp/pins_check && /tmp/pins_check && echo OK`
Expected: `OK`.

- [ ] **Step 3: Commit**

```bash
git add src/board_pins.h
git commit -m "Add board pin definitions for the CrowPanel Advance 7.0

Values quoted from Elecrow's driver source and cross-checked against their
V1.5 schematic net names. The pixel clock is revision-dependent: Elecrow
lowered it from 21 MHz to 16 MHz at V1.3, which reconciles most of the
contradictory advice in circulation.

Records explicitly that the backlight is not a GPIO on this board.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

## Task 6: The companion MCU — backlight and touch activation

**Files:**
- Create: `src/panel_mcu.h`, `src/panel_mcu.cpp`

**Interfaces:**
- Consumes: `board::PANEL_MCU_ADDR`, `board::TP_INT`, `board::GT911_ADDR_PRIMARY` from Task 5.
- Produces: `bool panel_mcu::begin()`, `void panel_mcu::backlightOn()`, `void panel_mcu::backlightOff()`, `void panel_mcu::backlightLevel(uint8_t percent)`, `void panel_mcu::activateTouch()`, `int panel_mcu::detectedRevision()`.

This is the module most likely to be the difference between a working display and an apparently dead board. The backlight is an I2C command, its encoding inverted between revisions, and getting it wrong gives a black screen with no error and a correctly scanning panel behind it.

- [ ] **Step 1: Write `src/panel_mcu.h`**

```cpp
// The STC8H1K28 companion microcontroller at I2C 0x30.
//
// On this board the backlight is NOT on a GPIO. An MT9201 boost is driven by a
// second microcontroller, and you turn the backlight on by sending it an I2C
// command. The same part owns the GT911's reset line, the buzzer, the
// amplifier's mute, and the battery charger's status lines.
//
// The command encoding INVERTED between board revisions:
//
//   V1.2    0x10 = on (brightest), 0x05 = off.   Touch activation: 0x19
//   V1.3+   0 = brightest, 244 = dimmest,        Touch activation: 250
//           245 = off. Writing 255 is out of range, not bright.
//
// Writes are a single bare byte with no register address.
//
// Getting this wrong produces a black screen with no error, no serial
// complaint, and a correctly scanning panel behind it - which reads as a dead
// board. The companion MCU acknowledges any byte, so a write cannot report
// whether it was understood; therefore begin() does not probe. The revision
// comes from the compile-time constant CROWPANEL_ADVANCE_REV (which defaults to
// V1.3 and later—the current stock). begin() logs which revision it is driving
// and what to change if the screen stays dark.
//
// Once the revision is known, set CROWPANEL_ADVANCE_REV in board_pins.h.
//
// The part is a black box. Elecrow publish no source and no register
// specification, its behaviour changed between revisions, and it is not
// reflashable through the ESP32. Do not probe undefined command bytes to
// discover the rest of its table; Elecrow advise against it and there is no way
// to undo a mistake.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic).

#pragma once

#include <cstdint>

namespace panel_mcu {

// Probes for the companion MCU on the I2C bus. Wire.begin() must already have
// been called. Returns false if nothing answered at 0x30, which on a V1.0 board
// is expected - that revision has an I/O expander at 0x18 instead and is not
// supported here.
bool begin();

// 130 for V1.3 and later, 120 for V1.2, or 0 if begin() has not run or found
// nothing. Log this at startup: on a board whose revision you have not checked,
// it is the fastest way to find out.
int detectedRevision();

// Full brightness. Call this LAST, after the panel is initialised and the RGB
// clocks have settled, or the screen flashes on boot.
void backlightOn();

void backlightOff();

// 0 (off) to 100 (brightest). The revision's encoding is applied internally, so
// callers never see the inverted V1.3+ scale.
void backlightLevel(uint8_t percent);

// Asks the companion MCU to reset the touch controller. The caller must hold
// board::TP_INT low across this call to latch the GT911 at address 0x5D.
void activateTouch();

}  // namespace panel_mcu
```

- [ ] **Step 2: Write `src/panel_mcu.cpp`**

```cpp
// Implementation of the companion MCU driver.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic).

#include "panel_mcu.h"

#include <Arduino.h>
#include <Wire.h>

#include "board_pins.h"

namespace panel_mcu {
namespace {

// V1.3 and later.
constexpr uint8_t CMD_V13_BRIGHTEST = 0;
constexpr uint8_t CMD_V13_DIMMEST   = 244;
constexpr uint8_t CMD_V13_OFF       = 245;
constexpr uint8_t CMD_V13_TOUCH     = 250;

// V1.2.
constexpr uint8_t CMD_V12_ON    = 0x10;
constexpr uint8_t CMD_V12_OFF   = 0x05;
constexpr uint8_t CMD_V12_TOUCH = 0x19;

int g_revision = 0;

// A single bare byte, no register address. Returns true if the device ACKed.
bool send(uint8_t command) {
    Wire.beginTransmission(board::PANEL_MCU_ADDR);
    Wire.write(command);
    return Wire.endTransmission() == 0;
}

bool present() {
    Wire.beginTransmission(board::PANEL_MCU_ADDR);
    return Wire.endTransmission() == 0;
}

}  // namespace

bool begin() {
    g_revision = 0;

    if (!present()) {
        Serial.printf("panel_mcu: nothing at 0x%02X.\n", board::PANEL_MCU_ADDR);
        Serial.println("panel_mcu: a V1.0 board has an expander at 0x18 instead,");
        Serial.println("panel_mcu: which this firmware does not support.");
        return false;
    }

    // The device ACKs any byte, so probing cannot distinguish the revisions by
    // return value. Trust the compile-time setting, and where it is the default
    // prefer V1.3+ because that is what current stock ships.
    g_revision = CROWPANEL_ADVANCE_REV;
    Serial.printf("panel_mcu: found at 0x%02X, driving it as V%d.%d\n",
                  board::PANEL_MCU_ADDR, g_revision / 100, (g_revision / 10) % 10);
    Serial.println("panel_mcu: if the screen stays dark, check the silkscreen and");
    Serial.println("panel_mcu: set CROWPANEL_ADVANCE_REV in board_pins.h.");
    return true;
}

int detectedRevision() { return g_revision; }

void backlightOn() { backlightLevel(100); }

void backlightOff() {
#if CROWPANEL_ADVANCE_REV >= 130
    send(CMD_V13_OFF);
#else
    send(CMD_V12_OFF);
#endif
}

void backlightLevel(uint8_t percent) {
    if (percent > 100) percent = 100;

    if (percent == 0) {
        backlightOff();
        return;
    }

#if CROWPANEL_ADVANCE_REV >= 130
    // 0 is brightest and 244 is dimmest, so the scale runs backwards from every
    // intuition about brightness values.
    const uint32_t span  = CMD_V13_DIMMEST - CMD_V13_BRIGHTEST;
    const uint8_t  value = static_cast<uint8_t>(span - (span * percent) / 100);
    send(value);
#else
    // V1.2 accepts 0x05 to 0x10, and dimming requires sending 0x10 first.
    send(CMD_V12_ON);
    const uint32_t span  = CMD_V12_ON - CMD_V12_OFF;
    const uint8_t  value = static_cast<uint8_t>(CMD_V12_OFF + (span * percent) / 100);
    send(value);
#endif
}

void activateTouch() {
#if CROWPANEL_ADVANCE_REV >= 130
    send(CMD_V13_TOUCH);
#else
    send(CMD_V12_TOUCH);
#endif
}

}  // namespace panel_mcu
```

- [ ] **Step 3: Verify it compiles**

Run: `pio run -e advance_70`
Expected: the build gets past `panel_mcu.cpp`. It will still fail at link with `undefined reference to setup`/`loop` until Task 13; that is expected at this stage.

- [ ] **Step 4: Commit**

```bash
git add src/panel_mcu.h src/panel_mcu.cpp
git commit -m "Add the companion MCU driver for backlight and touch activation

The backlight on this board is not a GPIO. It is a single-byte I2C command to
a second microcontroller, and the encoding inverted between V1.2 and V1.3: on
current stock 0 is brightest and 245 is off.

Getting it wrong gives a black screen with no error and a correctly scanning
panel behind it, so begin() logs which revision it is driving and what to do
if the screen stays dark.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

## Task 7: Panel bring-up and the LVGL binding

**Files:**
- Create: `src/display_driver.h`, `src/display_driver.cpp`

**Interfaces:**
- Consumes: everything display-related from `board_pins.h` (Task 5).
- Produces: `class LGFX`, `extern LGFX display_lcd`, `bool display_init()`, `lv_display_t* display_lvgl()`.

- [ ] **Step 1: Write `src/display_driver.h`**

```cpp
// Panel bring-up for the Elecrow CrowPanel Advance 7.0-HMI.
//
// Wraps the 800x480 16-bit RGB565 IPS panel in a LovyanGFX device and hands
// LVGL 9 a pair of PSRAM draw buffers.
//
// This driver does NOT own the backlight. On this board the backlight is an
// I2C command to a companion microcontroller; see panel_mcu.h. Elecrow's own
// driver has the LovyanGFX Light_PWM block commented out for the same reason.
//
// Every pin, timing and clock value comes from board_pins.h. Nothing here
// hardcodes a GPIO number.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic).

#pragma once

#include <LovyanGFX.hpp>
// LovyanGFX.hpp does not pull in the RGB parallel bus and panel; they are
// ESP32-S3 specific and have to be asked for by name.
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lvgl.h>

#include "board_pins.h"

class LGFX : public lgfx::LGFX_Device {
  public:
    LGFX();

  private:
    lgfx::Bus_RGB   bus_;
    lgfx::Panel_RGB panel_;
};

// The single panel instance. Defined in display_driver.cpp.
extern LGFX display_lcd;

// Initialises LVGL, brings up the panel, allocates the PSRAM draw buffers and
// registers the LVGL display. Also installs a millis()-backed LVGL tick source,
// so the caller only has to pump lv_timer_handler().
//
// Does NOT turn the backlight on - that belongs to panel_mcu and must happen
// after this returns.
//
// Returns false if the panel or, far more likely, the PSRAM allocation failed.
// A false return is fatal: there is nothing to draw on. At 800x480 and 16 bpp a
// full frame is 768 KB and cannot come from internal SRAM, so a build that
// omits PSRAM or selects quad instead of octal fails here rather than at
// compile time.
bool display_init();

// The LVGL display registered by display_init(), or nullptr before it runs.
lv_display_t* display_lvgl();
```

- [ ] **Step 2: Write `src/display_driver.cpp`**

```cpp
// Implementation of the panel driver and the LVGL 9 binding.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic).

#include "display_driver.h"

#include <Arduino.h>
#include <esp_heap_caps.h>

LGFX display_lcd;

namespace {

lv_display_t* g_disp = nullptr;

// Ten lines of partial rendering. Full double buffering at this size would be
// 1.5 MB, which PSRAM could afford but which buys nothing here: the UI redraws
// small regions - two numbers and two charts - not whole frames.
constexpr int   DRAW_LINES = 10;
constexpr size_t DRAW_PIXELS =
    static_cast<size_t>(board::LCD_WIDTH) * DRAW_LINES;

void flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    const int32_t w = area->x2 - area->x1 + 1;
    const int32_t h = area->y2 - area->y1 + 1;

    display_lcd.pushImageDMA(area->x1, area->y1, w, h,
                             reinterpret_cast<lgfx::rgb565_t*>(px_map));
    lv_display_flush_ready(disp);
}

uint32_t tick_cb() { return millis(); }

}  // namespace

LGFX::LGFX() {
    {
        auto cfg = bus_.config();

        cfg.panel = &panel_;

        cfg.pin_d0  = board::LCD_B0;
        cfg.pin_d1  = board::LCD_B1;
        cfg.pin_d2  = board::LCD_B2;
        cfg.pin_d3  = board::LCD_B3;
        cfg.pin_d4  = board::LCD_B4;
        cfg.pin_d5  = board::LCD_G0;
        cfg.pin_d6  = board::LCD_G1;
        cfg.pin_d7  = board::LCD_G2;
        cfg.pin_d8  = board::LCD_G3;
        cfg.pin_d9  = board::LCD_G4;
        cfg.pin_d10 = board::LCD_G5;
        cfg.pin_d11 = board::LCD_R0;
        cfg.pin_d12 = board::LCD_R1;
        cfg.pin_d13 = board::LCD_R2;
        cfg.pin_d14 = board::LCD_R3;
        cfg.pin_d15 = board::LCD_R4;

        cfg.pin_henable = board::LCD_DE;
        cfg.pin_vsync   = board::LCD_VSYNC;
        cfg.pin_hsync   = board::LCD_HSYNC;
        cfg.pin_pclk    = board::LCD_PCLK;
        cfg.freq_write  = board::LCD_PCLK_HZ;

        cfg.hsync_polarity    = board::LCD_HSYNC_POLARITY;
        cfg.hsync_front_porch = board::LCD_HSYNC_FRONT_PORCH;
        cfg.hsync_pulse_width = board::LCD_HSYNC_PULSE_WIDTH;
        cfg.hsync_back_porch  = board::LCD_HSYNC_BACK_PORCH;

        cfg.vsync_polarity    = board::LCD_VSYNC_POLARITY;
        cfg.vsync_front_porch = board::LCD_VSYNC_FRONT_PORCH;
        cfg.vsync_pulse_width = board::LCD_VSYNC_PULSE_WIDTH;
        cfg.vsync_back_porch  = board::LCD_VSYNC_BACK_PORCH;

        cfg.pclk_idle_high = board::LCD_PCLK_IDLE_HIGH;

        bus_.config(cfg);
    }
    {
        auto cfg = panel_.config();
        cfg.memory_width  = board::LCD_WIDTH;
        cfg.memory_height = board::LCD_HEIGHT;
        cfg.panel_width   = board::LCD_WIDTH;
        cfg.panel_height  = board::LCD_HEIGHT;
        cfg.offset_x      = 0;
        cfg.offset_y      = 0;
        panel_.config(cfg);
    }

    panel_.setBus(&bus_);
    setPanel(&panel_);
}

bool display_init() {
    if (!display_lcd.init()) {
        Serial.println("display: lcd.init() failed");
        return false;
    }
    display_lcd.setColorDepth(16);

    lv_init();
    lv_tick_set_cb(tick_cb);

    // Draw buffers must be DMA-capable. Asking for SPIRAM here rather than
    // letting malloc choose keeps them out of the internal SRAM that the
    // network stack wants.
    const size_t bytes = DRAW_PIXELS * sizeof(uint16_t);
    void* buf1 = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
    void* buf2 = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
    if (buf1 == nullptr || buf2 == nullptr) {
        Serial.println("display: draw buffer allocation failed");
        Serial.println("display: check that the build selects OPI PSRAM");
        return false;
    }

    g_disp = lv_display_create(board::LCD_WIDTH, board::LCD_HEIGHT);
    if (g_disp == nullptr) {
        Serial.println("display: lv_display_create failed");
        return false;
    }
    lv_display_set_flush_cb(g_disp, flush_cb);
    lv_display_set_buffers(g_disp, buf1, buf2, bytes,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    Serial.printf("display: %dx%d up, pclk %u Hz\n", board::LCD_WIDTH,
                  board::LCD_HEIGHT, static_cast<unsigned>(board::LCD_PCLK_HZ));
    return true;
}

lv_display_t* display_lvgl() { return g_disp; }
```

- [ ] **Step 3: Verify it compiles**

Run: `pio run -e advance_70`
Expected: past `display_driver.cpp`. Still unlinked until Task 13.

- [ ] **Step 4: Commit**

```bash
git add src/display_driver.h src/display_driver.cpp
git commit -m "Add RGB panel bring-up and the LVGL 9 binding

Draw buffers are explicitly DMA-capable PSRAM rather than whatever malloc
hands back, which keeps them out of the internal SRAM the network stack
wants and is one of the three documented causes of display shake on this
board.

Deliberately does not own the backlight: on this board that is an I2C
command to a companion MCU, which is why Elecrow's own driver has the
LovyanGFX backlight block commented out.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

## Task 8: Touch

**Files:**
- Create: `src/touch.h`, `src/touch.cpp`

**Interfaces:**
- Consumes: `board::GT911_ADDR_PRIMARY`, `board::GT911_ADDR_BACKUP`, `board::TP_INT`, `board::TOUCH_POLL_MS`, `board::LCD_WIDTH`, `board::LCD_HEIGHT` from Task 5; `panel_mcu::activateTouch()` from Task 6.
- Produces: `bool touch_init()`, `bool touch_pressed()`, `void touch_poll()`, `uint8_t touch_address()`, and an LVGL pointer input device registered internally.

- [ ] **Step 1: Write `src/touch.h`**

```cpp
// GT911 capacitive touch over raw I2C, polled.
//
// Two things make this board's touch worth its own module.
//
// First, the GT911 latches its I2C address from the state of its interrupt line
// during reset: low gives 0x5D, high gives 0x14. Reset is not an ESP32 GPIO
// here - it belongs to the companion MCU - so the sequence is: hold the
// interrupt pin low, ask panel_mcu to reset, wait, release. Both addresses are
// probed afterwards so a mis-latched controller announces itself instead of
// presenting as "touch does not work".
//
// Second, polling this bus too eagerly breaks the display. Blocking GT911 reads
// starve the RGB panel's DMA and produce visible shake; this is Elecrow's own
// issue #8. touch_poll() therefore rate-limits itself to board::TOUCH_POLL_MS
// and is safe to call every loop iteration.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic).

#pragma once

#include <cstdint>

// Runs the address-latching reset sequence, probes both addresses, and
// registers an LVGL pointer device. Wire.begin() and panel_mcu::begin() must
// have run first.
//
// Returns false if neither address answered. Not fatal: the display is useful
// without touch.
bool touch_init();

// The address that answered, or 0 if none did. Log it - a controller that came
// up at 0x14 is the single most likely touch fault on this family of boards.
uint8_t touch_address();

// Reads the controller at most once per board::TOUCH_POLL_MS. Safe and intended
// to be called every loop iteration.
void touch_poll();

// True while a finger is down, as of the last poll.
bool touch_pressed();
```

- [ ] **Step 2: Write `src/touch.cpp`**

```cpp
// Implementation of the GT911 driver.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic).

#include "touch.h"

#include <Arduino.h>
#include <Wire.h>
#include <lvgl.h>

#include "board_pins.h"
#include "panel_mcu.h"

namespace {

// GT911 register map, from the Goodix programming guide.
constexpr uint16_t REG_STATUS   = 0x814E;
constexpr uint16_t REG_POINT1_X = 0x8150;

uint8_t g_addr = 0;

bool g_pressed = false;
int16_t g_x = 0;
int16_t g_y = 0;

uint32_t g_last_poll_ms = 0;

bool readRegs(uint16_t reg, uint8_t* out, size_t len) {
    Wire.beginTransmission(g_addr);
    Wire.write(static_cast<uint8_t>(reg >> 8));
    Wire.write(static_cast<uint8_t>(reg & 0xFF));
    if (Wire.endTransmission(false) != 0) return false;

    if (Wire.requestFrom(static_cast<int>(g_addr), static_cast<int>(len)) !=
        static_cast<int>(len)) {
        return false;
    }
    for (size_t i = 0; i < len; ++i) out[i] = Wire.read();
    return true;
}

bool writeReg(uint16_t reg, uint8_t value) {
    Wire.beginTransmission(g_addr);
    Wire.write(static_cast<uint8_t>(reg >> 8));
    Wire.write(static_cast<uint8_t>(reg & 0xFF));
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

bool probe(uint8_t addr) {
    Wire.beginTransmission(addr);
    return Wire.endTransmission() == 0;
}

void lvgl_read_cb(lv_indev_t*, lv_indev_data_t* data) {
    // Reads cached state only. The actual I2C transaction happens in
    // touch_poll() on its own interval, because LVGL calls this far more often
    // than this bus can afford.
    data->state = g_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    data->point.x = g_x;
    data->point.y = g_y;
}

}  // namespace

bool touch_init() {
    // Hold the interrupt line low across the reset so the controller latches
    // 0x5D rather than 0x14. Reset itself belongs to the companion MCU.
    pinMode(board::TP_INT, OUTPUT);
    digitalWrite(board::TP_INT, LOW);
    panel_mcu::activateTouch();
    delay(120);
    pinMode(board::TP_INT, INPUT);
    delay(100);

    if (probe(board::GT911_ADDR_PRIMARY)) {
        g_addr = board::GT911_ADDR_PRIMARY;
    } else if (probe(board::GT911_ADDR_BACKUP)) {
        g_addr = board::GT911_ADDR_BACKUP;
        Serial.println("touch: GT911 answered at 0x14, not 0x5D.");
        Serial.println("touch: the address latch did not take. Coordinates");
        Serial.println("touch: will still work; the sequence is suspect.");
    } else {
        Serial.println("touch: no GT911 at 0x5D or 0x14");
        return false;
    }

    Serial.printf("touch: GT911 at 0x%02X\n", g_addr);

    lv_indev_t* indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, lvgl_read_cb);
    return true;
}

uint8_t touch_address() { return g_addr; }

void touch_poll() {
    if (g_addr == 0) return;

    const uint32_t now = millis();
    if (now - g_last_poll_ms < board::TOUCH_POLL_MS) return;
    g_last_poll_ms = now;

    uint8_t status = 0;
    if (!readRegs(REG_STATUS, &status, 1)) return;

    // Bit 7 means the controller has a fresh result; the low nibble is the
    // number of points.
    if ((status & 0x80) == 0) return;

    const uint8_t points = status & 0x0F;
    if (points > 0) {
        uint8_t buf[4];
        if (readRegs(REG_POINT1_X, buf, sizeof(buf))) {
            const int16_t x =
                static_cast<int16_t>(buf[0] | (static_cast<uint16_t>(buf[1]) << 8));
            const int16_t y =
                static_cast<int16_t>(buf[2] | (static_cast<uint16_t>(buf[3]) << 8));

            // Clamp rather than trust. A glitched read that lands off-screen
            // would otherwise send LVGL a pointer outside every object.
            g_x = x < 0 ? 0 : (x >= board::LCD_WIDTH ? board::LCD_WIDTH - 1 : x);
            g_y = y < 0 ? 0 : (y >= board::LCD_HEIGHT ? board::LCD_HEIGHT - 1 : y);
            g_pressed = true;
        }
    } else {
        g_pressed = false;
    }

    // The status register must be cleared or the controller stops reporting.
    writeReg(REG_STATUS, 0);
}

bool touch_pressed() { return g_pressed; }
```

- [ ] **Step 3: Verify it compiles**

Run: `pio run -e advance_70`
Expected: past `touch.cpp`. Still unlinked until Task 13.

- [ ] **Step 4: Commit**

```bash
git add src/touch.h src/touch.cpp
git commit -m "Add the GT911 touch driver, rate-limited

Polls at a fixed 20 ms rather than per-loop. Blocking reads on this shared
I2C bus starve the RGB panel's DMA and cause visible display shake, which is
Elecrow's own issue #8 and the most likely flicker cause in freshly written
firmware.

The LVGL read callback returns cached state only; the I2C transaction
happens on its own interval, because LVGL calls that callback far more often
than this bus can afford.

Probes both 0x5D and 0x14 so a mis-latched controller announces itself.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

## Task 9: Wi-Fi

**Files:**
- Create: `src/net.h`, `src/net.cpp`

**Interfaces:**
- Consumes: `WIFI_SSID` and `WIFI_PASSWORD` from `include/secrets.h`.
- Produces: `void net::begin()`, `void net::poll()`, `bool net::connected()`.

- [ ] **Step 1: Write `src/net.h`**

```cpp
// Wi-Fi, with non-blocking reconnection.
//
// Nothing here ever blocks. The panel must come up and show its status whether
// or not the network exists, so a display that waits for an access point is a
// display that shows nothing when the router is rebooting.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic).

#pragma once

namespace net {

// Starts the first connection attempt and returns immediately.
void begin();

// Call every loop. Retries with backoff when disconnected. Cheap when
// connected.
void poll();

bool connected();

}  // namespace net
```

- [ ] **Step 2: Write `src/net.cpp`**

```cpp
// Implementation of the Wi-Fi manager.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic).

#include "net.h"

#include <Arduino.h>
#include <WiFi.h>

#include "secrets.h"

namespace net {
namespace {

constexpr uint32_t RETRY_MIN_MS = 2000;
constexpr uint32_t RETRY_MAX_MS = 60000;

uint32_t g_retry_ms     = RETRY_MIN_MS;
uint32_t g_last_try_ms  = 0;
bool     g_was_connected = false;

void attempt() {
    Serial.printf("wifi: connecting to %s\n", WIFI_SSID);
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    g_last_try_ms = millis();
}

}  // namespace

void begin() {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    // The panel is mains-powered and needs throughput more than it needs the
    // few milliwatts modem sleep would save.
    WiFi.setSleep(false);
    attempt();
}

void poll() {
    const bool up = connected();

    if (up != g_was_connected) {
        g_was_connected = up;
        if (up) {
            Serial.printf("wifi: up, %s\n", WiFi.localIP().toString().c_str());
            g_retry_ms = RETRY_MIN_MS;
        } else {
            Serial.println("wifi: down");
        }
    }

    if (up) return;

    if (millis() - g_last_try_ms < g_retry_ms) return;

    attempt();
    g_retry_ms *= 2;
    if (g_retry_ms > RETRY_MAX_MS) g_retry_ms = RETRY_MAX_MS;
}

bool connected() { return WiFi.status() == WL_CONNECTED; }

}  // namespace net
```

- [ ] **Step 3: Create your local secrets file and verify it compiles**

```bash
cp include/secrets.h.template include/secrets.h
# then edit include/secrets.h with your Wi-Fi credentials
pio run -e advance_70
```
Expected: past `net.cpp`. Still unlinked until Task 13.

- [ ] **Step 4: Commit**

```bash
git add src/net.h src/net.cpp
git commit -m "Add non-blocking Wi-Fi with backoff

Never blocks. The panel has to come up and show its status whether or not
the network exists; a display that waits for an access point is a display
that shows nothing while the router reboots.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

## Task 10: The MQTT source

**Files:**
- Create: `src/source_mqtt.h`, `src/source_mqtt.cpp`

**Interfaces:**
- Consumes: `parse::decimal` (Task 2), `channel::Channel` (Task 4), `MQTT_HOST` and `MQTT_PORT` from `include/secrets.h`.
- Produces: `void source_mqtt::begin(channel::Channel& office)`, `void source_mqtt::poll()`, `bool source_mqtt::connected()`, `uint32_t source_mqtt::rejectedPayloads()`.

The broker was polled on 2026-09-11 and accepts anonymous connections. Topics and their exact payload shape are recorded in the spec.

- [ ] **Step 1: Write `src/source_mqtt.h`**

```cpp
// The office temperature, from the MQTT broker.
//
// Subscribes to office/DHT/tempc and office/DHT/hum. The broker
// publishes bare decimal ASCII, not JSON, about every ten seconds. It accepts
// anonymous connections; there are no credentials to supply.
//
// Temperature and humidity arrive as two separate messages. A history sample is
// appended when a temperature lands, carrying the most recent humidity, so the
// two topics do not need to be synchronised. No sample is appended until both
// have been seen at least once - a chart entry showing a real temperature
// beside a humidity of zero would be worse than a slightly later first point.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic).

#pragma once

#include <cstdint>

#include "Channel.h"

namespace source_mqtt {

// Binds the channel that incoming readings are written into. The channel must
// outlive this module; in practice it is a file-scope object in main.cpp.
void begin(channel::Channel& office);

// Call every loop. Handles connection, reconnection with backoff, and message
// dispatch. Cheap when connected and idle.
void poll();

bool connected();

// Payloads rejected as malformed since boot. Surfaced so a broker that starts
// publishing something unexpected is visible rather than silently ignored.
uint32_t rejectedPayloads();

}  // namespace source_mqtt
```

- [ ] **Step 2: Write `src/source_mqtt.cpp`**

```cpp
// Implementation of the MQTT source.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic).

#include "source_mqtt.h"

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>

#include <cstring>

#include "parse.h"
#include "secrets.h"

namespace source_mqtt {
namespace {

constexpr char TOPIC_TEMP[] = "office/DHT/tempc";
constexpr char TOPIC_HUM[]  = "office/DHT/hum";

constexpr uint32_t RETRY_MIN_MS = 2000;
constexpr uint32_t RETRY_MAX_MS = 30000;

WiFiClient   g_wifi;
PubSubClient g_mqtt(g_wifi);

channel::Channel* g_channel = nullptr;

float    g_last_hum      = 0.0f;
bool     g_have_hum      = false;
uint32_t g_rejected      = 0;
uint32_t g_retry_ms      = RETRY_MIN_MS;
uint32_t g_last_try_ms   = 0;

void onMessage(char* topic, uint8_t* payload, unsigned int len) {
    float value = 0.0f;
    if (!parse::decimal(reinterpret_cast<const char*>(payload), len, value)) {
        ++g_rejected;
        Serial.printf("mqtt: rejected payload on %s\n", topic);
        return;
    }

    if (std::strcmp(topic, TOPIC_HUM) == 0) {
        g_last_hum = value;
        g_have_hum = true;
        return;
    }

    if (std::strcmp(topic, TOPIC_TEMP) == 0) {
        if (!g_have_hum) return;  // wait for the first humidity
        if (g_channel != nullptr) {
            g_channel->update(value, g_last_hum, millis());
        }
    }
}

void connect() {
    g_last_try_ms = millis();

    // A client id must be unique on the broker or it will disconnect the
    // previous holder in a loop. The MAC guarantees that.
    char id[32];
    snprintf(id, sizeof(id), "crowpanel-%012llX", ESP.getEfuseMac());

    Serial.printf("mqtt: connecting to %s:%d as %s\n", MQTT_HOST, MQTT_PORT, id);
    if (!g_mqtt.connect(id)) {
        Serial.printf("mqtt: failed, state %d\n", g_mqtt.state());
        return;
    }

    g_mqtt.subscribe(TOPIC_TEMP);
    g_mqtt.subscribe(TOPIC_HUM);
    g_retry_ms = RETRY_MIN_MS;
    Serial.println("mqtt: connected and subscribed");
}

}  // namespace

void begin(channel::Channel& office) {
    g_channel = &office;
    g_mqtt.setServer(MQTT_HOST, MQTT_PORT);
    g_mqtt.setCallback(onMessage);
    // Payloads are single numbers, so the default 256-byte buffer is ample.
    g_mqtt.setKeepAlive(30);
}

void poll() {
    if (WiFi.status() != WL_CONNECTED) return;

    if (!g_mqtt.connected()) {
        if (millis() - g_last_try_ms < g_retry_ms) return;
        connect();
        if (!g_mqtt.connected()) {
            g_retry_ms *= 2;
            if (g_retry_ms > RETRY_MAX_MS) g_retry_ms = RETRY_MAX_MS;
        }
        return;
    }

    g_mqtt.loop();
}

bool connected() { return g_mqtt.connected(); }

uint32_t rejectedPayloads() { return g_rejected; }

}  // namespace source_mqtt
```

- [ ] **Step 3: Verify it compiles**

Run: `pio run -e advance_70`
Expected: past `source_mqtt.cpp`. Still unlinked until Task 13.

- [ ] **Step 4: Commit**

```bash
git add src/source_mqtt.h src/source_mqtt.cpp
git commit -m "Add the MQTT source for the office temperature

Temperature and humidity arrive as separate messages, so a sample is
appended when a temperature lands carrying the most recent humidity. No
sample is appended until both have been seen once: a chart point showing a
real temperature beside a humidity of zero is worse than a later first
point.

The client id carries the MAC. A fixed id would have the broker disconnect
the previous holder in a loop if the board ever reconnects before the old
session times out.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

## Task 11: The weather source

**Files:**
- Create: `src/source_weather.h`, `src/source_weather.cpp`

**Interfaces:**
- Consumes: `parse::openMeteo` and `parse::Weather` (Task 3), `channel::Channel` (Task 4), `WEATHER_LATITUDE` and `WEATHER_LONGITUDE` from `include/secrets.h`.
- Produces: `void source_weather::begin(channel::Channel& outdoor)`, `void source_weather::poll()`, `uint32_t source_weather::failures()`.

- [ ] **Step 1: Write `src/source_weather.h`**

```cpp
// The Maple Valley outdoor temperature, from Open-Meteo.
//
// Deliberately plain HTTP, not HTTPS. Open-Meteo serves this endpoint over HTTP
// with a 200 and no redirect, which removes the TLS stack from the firmware.
// That saves roughly 40 KB of heap the frame buffers want, and removes the
// certificate expiry that silently kills embedded HTTPS clients about a year
// after they are flashed. There is nothing secret in a public weather reading.
//
// No API key. Open-Meteo's current-conditions data updates every fifteen
// minutes, so polling faster only wastes requests.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic).

#pragma once

#include <cstdint>

#include "Channel.h"

namespace source_weather {

// Binds the channel that readings are written into. The channel must outlive
// this module.
void begin(channel::Channel& outdoor);

// Call every loop. Fetches at most once per interval; cheap otherwise.
void poll();

// Consecutive failed fetches. Non-zero means the last attempt did not produce a
// usable reading, whether from the network or from the response body.
uint32_t failures();

}  // namespace source_weather
```

- [ ] **Step 2: Write `src/source_weather.cpp`**

```cpp
// Implementation of the Open-Meteo source.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic).

#include "source_weather.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>

#include "parse.h"
#include "secrets.h"

namespace source_weather {
namespace {

// Open-Meteo's current block carries interval 900, so this is its real update
// rate. Polling faster returns the same numbers.
constexpr uint32_t FETCH_INTERVAL_MS = 15UL * 60UL * 1000UL;

// After a failure, try again sooner than the full interval, but not so soon
// that a sustained outage hammers a free public API.
constexpr uint32_t RETRY_AFTER_FAIL_MS = 60UL * 1000UL;

constexpr uint16_t HTTP_TIMEOUT_MS = 8000;

channel::Channel* g_channel     = nullptr;
uint32_t          g_next_due_ms = 0;
uint32_t          g_failures    = 0;
bool              g_first       = true;

bool fetch() {
    char url[256];
    snprintf(url, sizeof(url),
             "http://api.open-meteo.com/v1/forecast"
             "?latitude=%s&longitude=%s"
             "&current=temperature_2m,relative_humidity_2m,weather_code"
             "&timezone=America%%2FLos_Angeles",
             WEATHER_LATITUDE, WEATHER_LONGITUDE);

    HTTPClient http;
    http.setTimeout(HTTP_TIMEOUT_MS);
    http.setConnectTimeout(HTTP_TIMEOUT_MS);
    if (!http.begin(url)) {
        Serial.println("weather: http.begin failed");
        return false;
    }

    const int code = http.GET();
    if (code != HTTP_CODE_OK) {
        Serial.printf("weather: HTTP %d\n", code);
        http.end();
        return false;
    }

    const String body = http.getString();
    http.end();

    parse::Weather w{};
    if (!parse::openMeteo(body.c_str(), body.length(), w)) {
        Serial.println("weather: response did not parse");
        return false;
    }

    if (g_channel != nullptr) {
        g_channel->update(w.temperature_c, w.humidity_pct, millis());
    }
    Serial.printf("weather: %.1f C  %.0f%% RH\n", w.temperature_c, w.humidity_pct);
    return true;
}

}  // namespace

void begin(channel::Channel& outdoor) {
    g_channel     = &outdoor;
    g_next_due_ms = 0;
    g_first       = true;
}

void poll() {
    if (WiFi.status() != WL_CONNECTED) return;

    const uint32_t now = millis();
    // Unsigned comparison, correct across the millis() rollover.
    if (!g_first && static_cast<int32_t>(now - g_next_due_ms) < 0) return;
    g_first = false;

    if (fetch()) {
        g_failures    = 0;
        g_next_due_ms = now + FETCH_INTERVAL_MS;
    } else {
        ++g_failures;
        g_next_due_ms = now + RETRY_AFTER_FAIL_MS;
    }
}

uint32_t failures() { return g_failures; }

}  // namespace source_weather
```

- [ ] **Step 3: Verify it compiles**

Run: `pio run -e advance_70`
Expected: past `source_weather.cpp`. Still unlinked until Task 13.

- [ ] **Step 4: Commit**

```bash
git add src/source_weather.h src/source_weather.cpp
git commit -m "Add the Open-Meteo source for the Maple Valley reading

Plain HTTP on purpose. Open-Meteo serves this endpoint over HTTP with a 200
and no redirect, so the firmware needs no TLS stack: about 40 KB of heap the
frame buffers want, and no certificate to expire a year after flashing.
There is nothing secret in a public weather reading.

Polls every fifteen minutes because that is the API's own update interval,
and backs off to one minute after a failure rather than hammering a free
public service during an outage.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

## Task 12: The screen

**Files:**
- Create: `src/ui.h`, `src/ui.cpp`

**Interfaces:**
- Consumes: `channel::Channel` (Task 4), `history::SampleHistory::Bucket` (Task 1), `board::LCD_WIDTH`, `board::LCD_HEIGHT` (Task 5).
- Produces: `ui::CHART_POINTS`, `ui::CHART_WINDOW_MS`, `ui::MAX_ROWS`, `void ui::init(channel::Channel** channels, size_t count)`, `void ui::refresh(uint32_t now_ms)`, `void ui::updateChart(size_t row)`, `void ui::setStatus(bool wifi_up, bool mqtt_up)`, `void ui::setFahrenheit(bool)`, `bool ui::fahrenheit()`.

- [ ] **Step 1: Write `src/ui.h`**

```cpp
// The screen: two stacked full-width rows over a status bar.
//
// Each row shows one channel's name, its current temperature large enough to
// read across a room, its humidity, and twelve hours of history underneath.
//
// Kept separate from the sources and the channels so the layout can change
// without touching either. Everything here runs on the LVGL timer thread, so
// call these from the same context as lv_timer_handler().
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic).

#pragma once

#include <cstddef>
#include <cstdint>

#include "Channel.h"

namespace ui {

// One bucket per two horizontal pixels of chart. Per-pixel would be wasteful
// and makes the trace noisy at this width.
constexpr size_t CHART_POINTS = 380;

constexpr uint32_t CHART_WINDOW_MS = 12UL * 60UL * 60UL * 1000UL;

// Two rows today. The layout divides the available height evenly, so a third
// channel is a row height change rather than a rewrite.
constexpr size_t MAX_ROWS = 4;

// Builds the widget tree. `channels` must outlive the UI. Call once, after
// LVGL and the display are up.
void init(channel::Channel** channels, size_t count);

// Updates every row's readout and staleness from its channel. Safe to call
// every loop; it touches the display only when a displayed value actually
// changed, so LVGL does not redraw for nothing.
void refresh(uint32_t now_ms);

// Redraws one row's chart from its channel's history. Call after a new sample
// lands, not every loop - a full downsample of twelve hours is not free.
void updateChart(size_t row);

// Connection state for the status bar.
void setStatus(bool wifi_up, bool mqtt_up);

// Celsius or Fahrenheit. Conversion happens at display time; history always
// stores Celsius.
void setFahrenheit(bool on);
bool fahrenheit();

}  // namespace ui
```

- [ ] **Step 2: Write `src/ui.cpp`**

```cpp
// Implementation of the screen.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic).

#include "ui.h"

#include <lvgl.h>

#include <cmath>
#include <cstdio>

#include "board_pins.h"

namespace ui {
namespace {

constexpr int STATUS_H = 40;

struct Row {
    channel::Channel* ch        = nullptr;
    lv_obj_t*         panel     = nullptr;
    lv_obj_t*         name      = nullptr;
    lv_obj_t*         value     = nullptr;
    lv_obj_t*         humidity  = nullptr;
    lv_obj_t*         note      = nullptr;   // shown only when stale
    lv_obj_t*         chart     = nullptr;
    lv_chart_series_t* series   = nullptr;

    // Last values actually written to the widgets, so refresh() can skip LVGL
    // calls when nothing changed.
    float last_temp_c = NAN;
    float last_hum    = NAN;
    bool  last_stale  = true;
    bool  primed      = false;
};

Row    g_rows[MAX_ROWS];
size_t g_row_count = 0;

lv_obj_t* g_status_wifi = nullptr;
lv_obj_t* g_status_mqtt = nullptr;

bool g_fahrenheit = true;

history::SampleHistory::Bucket g_buckets[CHART_POINTS];

float toDisplay(float celsius) {
    return g_fahrenheit ? (celsius * 9.0f / 5.0f + 32.0f) : celsius;
}

void buildRow(Row& row, int y, int height) {
    row.panel = lv_obj_create(lv_screen_active());
    lv_obj_set_pos(row.panel, 0, y);
    lv_obj_set_size(row.panel, board::LCD_WIDTH, height);
    lv_obj_set_style_radius(row.panel, 0, 0);
    lv_obj_set_style_border_width(row.panel, 0, 0);
    lv_obj_set_style_pad_all(row.panel, 8, 0);
    lv_obj_clear_flag(row.panel, LV_OBJ_FLAG_SCROLLABLE);

    row.name = lv_label_create(row.panel);
    lv_obj_set_style_text_font(row.name, &lv_font_montserrat_20, 0);
    lv_label_set_text(row.name, row.ch->name());
    lv_obj_align(row.name, LV_ALIGN_TOP_LEFT, 0, 0);

    row.value = lv_label_create(row.panel);
    lv_obj_set_style_text_font(row.value, &lv_font_montserrat_48, 0);
    lv_label_set_text(row.value, "--");
    lv_obj_align(row.value, LV_ALIGN_TOP_LEFT, 260, -6);

    row.humidity = lv_label_create(row.panel);
    lv_obj_set_style_text_font(row.humidity, &lv_font_montserrat_28, 0);
    lv_label_set_text(row.humidity, "--");
    lv_obj_align(row.humidity, LV_ALIGN_TOP_LEFT, 470, 8);

    row.note = lv_label_create(row.panel);
    lv_obj_set_style_text_font(row.note, &lv_font_montserrat_16, 0);
    lv_label_set_text(row.note, "");
    lv_obj_align(row.note, LV_ALIGN_TOP_RIGHT, 0, 4);

    row.chart = lv_chart_create(row.panel);
    lv_obj_set_size(row.chart, board::LCD_WIDTH - 32, height - 76);
    lv_obj_align(row.chart, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_chart_set_type(row.chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(row.chart, CHART_POINTS);
    lv_chart_set_update_mode(row.chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_div_line_count(row.chart, 3, 0);
    lv_obj_set_style_size(row.chart, 0, 0, LV_PART_INDICATOR);  // no point dots
    row.series = lv_chart_add_series(row.chart, lv_palette_main(LV_PALETTE_BLUE),
                                     LV_CHART_AXIS_PRIMARY_Y);
}

void buildStatusBar() {
    lv_obj_t* bar = lv_obj_create(lv_screen_active());
    lv_obj_set_pos(bar, 0, board::LCD_HEIGHT - STATUS_H);
    lv_obj_set_size(bar, board::LCD_WIDTH, STATUS_H);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 6, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    g_status_wifi = lv_label_create(bar);
    lv_obj_set_style_text_font(g_status_wifi, &lv_font_montserrat_16, 0);
    lv_label_set_text(g_status_wifi, "wifi ...");
    lv_obj_align(g_status_wifi, LV_ALIGN_LEFT_MID, 0, 0);

    g_status_mqtt = lv_label_create(bar);
    lv_obj_set_style_text_font(g_status_mqtt, &lv_font_montserrat_16, 0);
    lv_label_set_text(g_status_mqtt, "mqtt ...");
    lv_obj_align(g_status_mqtt, LV_ALIGN_LEFT_MID, 140, 0);
}

}  // namespace

void init(channel::Channel** channels, size_t count) {
    if (count > MAX_ROWS) count = MAX_ROWS;
    g_row_count = count;

    const int usable = board::LCD_HEIGHT - STATUS_H;
    const int height = usable / static_cast<int>(count);

    for (size_t i = 0; i < count; ++i) {
        g_rows[i].ch = channels[i];
        buildRow(g_rows[i], static_cast<int>(i) * height, height);
    }
    buildStatusBar();
}

void refresh(uint32_t now_ms) {
    char buf[32];

    for (size_t i = 0; i < g_row_count; ++i) {
        Row& row = g_rows[i];
        const channel::Reading& r = row.ch->latest();
        const bool stale = row.ch->isStale(now_ms);

        const bool changed = !row.primed ||
                             stale != row.last_stale ||
                             r.temperature_c != row.last_temp_c ||
                             r.humidity_pct != row.last_hum;
        if (!changed) continue;

        row.primed      = true;
        row.last_stale  = stale;
        row.last_temp_c = r.temperature_c;
        row.last_hum    = r.humidity_pct;

        if (!r.valid) {
            lv_label_set_text(row.value, "--");
            lv_label_set_text(row.humidity, "--");
            lv_label_set_text(row.note, "waiting");
        } else {
            snprintf(buf, sizeof(buf), "%.1f%s", toDisplay(r.temperature_c),
                     g_fahrenheit ? " F" : " C");
            lv_label_set_text(row.value, buf);

            snprintf(buf, sizeof(buf), "%.0f%% RH", r.humidity_pct);
            lv_label_set_text(row.humidity, buf);

            lv_label_set_text(row.note, stale ? "stale" : "");
        }

        // A stale number that looks live is worse than an obvious gap, so dim
        // the whole row rather than relying on the word alone being noticed.
        const lv_opa_t opa = (stale || !r.valid) ? LV_OPA_40 : LV_OPA_COVER;
        lv_obj_set_style_text_opa(row.value, opa, 0);
        lv_obj_set_style_text_opa(row.humidity, opa, 0);
    }
}

void updateChart(size_t row_index) {
    if (row_index >= g_row_count) return;
    Row& row = g_rows[row_index];

    const size_t valid =
        row.ch->history().downsample(CHART_WINDOW_MS, g_buckets, CHART_POINTS);
    if (valid == 0) return;

    float lo = 0.0f, hi = 0.0f;
    if (!row.ch->history().temperatureRange(lo, hi)) return;

    // A flat trace against an auto-scaled axis turns sensor noise into drama.
    // Two degrees of headroom keeps a steady reading looking steady.
    float lo_d = toDisplay(lo);
    float hi_d = toDisplay(hi);
    if (hi_d - lo_d < 2.0f) {
        const float mid = (hi_d + lo_d) / 2.0f;
        lo_d = mid - 1.0f;
        hi_d = mid + 1.0f;
    }
    lv_chart_set_range(row.chart, LV_CHART_AXIS_PRIMARY_Y,
                       static_cast<int32_t>(lo_d - 1.0f),
                       static_cast<int32_t>(hi_d + 1.0f));

    for (size_t i = 0; i < CHART_POINTS; ++i) {
        if (g_buckets[i].valid) {
            lv_chart_set_value_by_id(
                row.chart, row.series, static_cast<uint32_t>(i),
                static_cast<int32_t>(toDisplay(g_buckets[i].temperature_c)));
        } else {
            // A gap is drawn as a gap. The Maple Valley trace legitimately has
            // only about 48 real points across twelve hours; interpolating them
            // into smoothness would invent data the API never sent.
            lv_chart_set_value_by_id(row.chart, row.series,
                                     static_cast<uint32_t>(i),
                                     LV_CHART_POINT_NONE);
        }
    }
    lv_chart_refresh(row.chart);
}

void setStatus(bool wifi_up, bool mqtt_up) {
    if (g_status_wifi != nullptr) {
        lv_label_set_text(g_status_wifi, wifi_up ? "wifi ok" : "wifi down");
    }
    if (g_status_mqtt != nullptr) {
        lv_label_set_text(g_status_mqtt, mqtt_up ? "mqtt ok" : "mqtt down");
    }
}

void setFahrenheit(bool on) {
    if (on == g_fahrenheit) return;
    g_fahrenheit = on;
    for (size_t i = 0; i < g_row_count; ++i) {
        g_rows[i].primed = false;  // force the readouts to rewrite
        updateChart(i);
    }
}

bool fahrenheit() { return g_fahrenheit; }

}  // namespace ui
```

- [ ] **Step 3: Verify it compiles**

Run: `pio run -e advance_70`
Expected: past `ui.cpp`. Still unlinked until Task 13.

- [ ] **Step 4: Commit**

```bash
git add src/ui.h src/ui.cpp
git commit -m "Add the two-row screen with per-row charts

Rows are driven from a channel array rather than hardcoded, so a third
source is a row height change rather than a rewrite.

Gaps in the chart are drawn as gaps. The Maple Valley trace legitimately has
about 48 real points across twelve hours, and interpolating them into
smoothness would invent data the API never sent.

A stale row dims as well as saying so. A stale number that looks live is
worse than an obvious gap.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

## Task 13: Wiring it together

**Files:**
- Create: `src/main.cpp`

**Interfaces:**
- Consumes: every module from Tasks 4 through 12.
- Produces: `setup()` and `loop()`.

Startup order matters. The backlight goes on **last**, after the panel is scanning and there is something worth looking at.

- [ ] **Step 1: Write `src/main.cpp`**

```cpp
// Temperature display for the Elecrow CrowPanel Advance 7.0-HMI.
//
// Shows the outdoor temperature for Maple Valley, from Open-Meteo, above the
// indoor temperature for Mark's office, from an MQTT broker, each with twelve
// hours of history.
//
// Startup order matters on this board. The backlight is an I2C command to a
// companion microcontroller, and it goes on LAST - after the panel is scanning
// and the first frame is drawn. See docs/HARDWARE.md.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic).

#include <Arduino.h>
#include <Wire.h>

#include "Channel.h"
#include "board_pins.h"
#include "display_driver.h"
#include "net.h"
#include "panel_mcu.h"
#include "source_mqtt.h"
#include "source_weather.h"
#include "touch.h"
#include "ui.h"

namespace {

// Expected publish intervals, used only to size the ring buffers.
constexpr uint32_t OFFICE_INTERVAL_MS  = 10UL * 1000UL;
constexpr uint32_t WEATHER_INTERVAL_MS = 15UL * 60UL * 1000UL;

// Per-channel staleness. The office publishes every ten seconds, so a minute of
// silence means something broke. Maple Valley updates every fifteen minutes, so
// a minute of silence is normal.
constexpr uint32_t OFFICE_STALE_MS  = 60UL * 1000UL;
constexpr uint32_t WEATHER_STALE_MS = 45UL * 60UL * 1000UL;

// Capacity is the window divided by the interval, plus slack so the chart's
// window is always fully covered rather than starved at the left edge.
constexpr size_t OFFICE_CAPACITY =
    (ui::CHART_WINDOW_MS / OFFICE_INTERVAL_MS) + 64;
constexpr size_t WEATHER_CAPACITY =
    (ui::CHART_WINDOW_MS / WEATHER_INTERVAL_MS) + 8;

channel::Channel g_outdoor("Maple Valley", WEATHER_STALE_MS, WEATHER_CAPACITY);
channel::Channel g_office("Mark's Office", OFFICE_STALE_MS, OFFICE_CAPACITY);

channel::Channel* g_channels[] = {&g_outdoor, &g_office};
constexpr size_t  CHANNEL_COUNT = sizeof(g_channels) / sizeof(g_channels[0]);

// Long-press anywhere toggles Celsius and Fahrenheit. Deliberately not a
// button: the panel is meant to be read, not operated, and a stray brush
// against the glass should not change the units.
constexpr uint32_t UNIT_TOGGLE_HOLD_MS = 1000;

void pollUnitToggle() {
    static uint32_t press_started_ms = 0;
    static bool     handled          = false;

    if (!touch_pressed()) {
        press_started_ms = 0;
        handled          = false;
        return;
    }

    const uint32_t now = millis();
    if (press_started_ms == 0) {
        press_started_ms = now;
        return;
    }

    if (!handled && (now - press_started_ms) >= UNIT_TOGGLE_HOLD_MS) {
        handled = true;
        ui::setFahrenheit(!ui::fahrenheit());
        Serial.printf("units: %s\n", ui::fahrenheit() ? "F" : "C");
    }
}

// Redraws a row's chart when its channel has gained a sample.
void pollCharts() {
    static size_t last_size[CHANNEL_COUNT] = {0};

    for (size_t i = 0; i < CHANNEL_COUNT; ++i) {
        const size_t n = g_channels[i]->history().size();
        if (n == last_size[i]) continue;
        last_size[i] = n;
        ui::updateChart(i);
    }
}

}  // namespace

void setup() {
    Serial.begin(115200);
    // Serial reaches the host through a CH340K on GPIO43/44, not native USB.
    // Nothing to wait for, so no while(!Serial) here - that would hang forever.
    delay(200);
    Serial.println("\nCrowPanel Advance 7.0 temperature display");

    Wire.begin(board::I2C_SDA, board::I2C_SCL, board::I2C_HZ);
    delay(50);

    if (!panel_mcu::begin()) {
        // Not fatal on its own, but the backlight will not come on, so say so
        // loudly rather than leaving a dark screen unexplained.
        Serial.println("FATAL: no companion MCU; the backlight cannot be lit");
    }

    if (!display_init()) {
        // Almost always a PSRAM problem: the draw buffers cannot come from
        // internal SRAM. Check that the build selects OPI PSRAM.
        Serial.println("FATAL: display_init failed");
        while (true) delay(1000);
    }

    if (!touch_init()) {
        // Not fatal. The display is useful without touch; only the unit toggle
        // is lost.
        Serial.println("warning: touch unavailable");
    }

    ui::init(g_channels, CHANNEL_COUNT);
    ui::refresh(millis());

    net::begin();
    source_mqtt::begin(g_office);
    source_weather::begin(g_outdoor);

    // Draw the first frame before the backlight comes on, so the panel lights
    // up showing the UI rather than whatever was in the buffers.
    lv_timer_handler();
    panel_mcu::backlightOn();

    Serial.println("ready");
}

void loop() {
    lv_timer_handler();

    net::poll();
    source_mqtt::poll();
    source_weather::poll();

    touch_poll();
    pollUnitToggle();

    const uint32_t now = millis();
    ui::refresh(now);
    ui::setStatus(net::connected(), source_mqtt::connected());
    pollCharts();

    // Yield. Starving the RGB panel's DMA is one of the three documented causes
    // of display shake on this board.
    delay(5);
}
```

- [ ] **Step 2: Build and confirm it links**

Run: `pio run -e advance_70`
Expected: SUCCESS. This is the first task whose build completes. Note the reported flash and RAM usage; the app should be far inside the 3 MB Huge APP partition.

- [ ] **Step 3: Run the whole host suite again to confirm nothing regressed**

Run: `pio test -e native`
Expected: PASS, 61 tests.

- [ ] **Step 4: Commit**

```bash
git add src/main.cpp
git commit -m "Wire the modules together

Startup order matters on this board: the backlight is an I2C command to a
companion MCU and goes on last, after the first frame is drawn, so the panel
lights up showing the UI rather than whatever was in the buffers.

Charts redraw when a channel gains a sample rather than every loop. A full
downsample of twelve hours is not free.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

## Task 14: README and the arrival checklist

**Files:**
- Create: `README.md`, `LICENSE`

**Interfaces:**
- Consumes: nothing.
- Produces: nothing consumed by code.

- [ ] **Step 1: Write `LICENSE`**

Use the MIT license text, copyright `2026 Mark Castelluccio`, matching the sibling project.

- [ ] **Step 2: Write `README.md`**

It must cover, in this order:

1. What the display shows and where each number comes from, including the broker address and that it needs no credentials.
2. Build and flash commands: `pio run -e advance_70 -t upload` and `pio test -e native`.
3. That `include/secrets.h` must be copied from the template first.
4. A layout table of the repository, one line per directory, matching the sibling project's style.
5. **A prominent warning that this board is not the CrowPanel 7.0 HMI**, with the comparison table from `docs/HARDWARE.md` section 0, since that confusion is the single most expensive mistake available here.
6. The three things most likely to cost an evening: the backlight is an I2C command and its encoding is revision-dependent; the flash is 16 MB where the sibling board is 4 MB; and `ARDUINO_USB_CDC_ON_BOOT` must stay off.
7. A pointer to `docs/HARDWARE.md` section 8 as the arrival checklist, noting that **no value in this project has been verified against hardware**.
8. Authorship, matching the sibling project: Mark Castelluccio, developed with Claude Code.

- [ ] **Step 3: Commit**

```bash
git add README.md LICENSE
git commit -m "Add README and license

Leads with the warning that this board is not the CrowPanel 7.0 HMI. The
names differ by one word, almost nothing carries over, and that confusion is
the single most expensive mistake available here.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

## Plan self-review

Checked after writing, per the writing-plans skill.

**Spec coverage.** Every section of the spec maps to a task: the two sources to Tasks 10 and 11; the stacked layout to Task 12; the module structure to Tasks 1 through 13; per-channel staleness to Task 4; keeping every sample to Tasks 1 and 4; the DMA constraint to Tasks 7, 8 and 13; Celsius internally to Tasks 3, 4 and 12; backlight revision handling to Task 6; error handling to Tasks 9, 10, 11 and 12; testing to Tasks 1 through 4; build configuration to Task 1; and the risks to Tasks 5, 6 and 14.

**Type consistency.** `history::Sample` and `SampleHistory::Bucket` are used with the same field names in Tasks 1, 4 and 12. `channel::Channel`'s constructor signature is identical in Tasks 4, 12 and 13. `parse::decimal` and `parse::openMeteo` signatures match between Tasks 2, 3, 10 and 11. `ui::CHART_WINDOW_MS` is defined in Task 12 and consumed in Task 13.

**Known gaps, deliberate.**

1. **Task 6's revision handling is compile-time, not a runtime probe.** The design discussion imagined probing both encodings at boot. That turned out not to work: the companion MCU ACKs any byte, so a write cannot report whether it was understood. Trying V1.3+ then V1.2 would send a brightness command to a V1.2 board that reads it as something else entirely. The module therefore trusts `CROWPANEL_ADVANCE_REV`, defaults to V1.3+ because that is current stock, and logs what it is doing plus what to change if the screen stays dark. **This is a change from the spec and should be reflected there.**
2. **No task verifies the panel, touch or backlight.** They cannot be verified before the board exists. Tasks 5 through 13 end with a compile.
3. **The status bar has no clock**, though the spec's layout sketch shows one. The board has a real-time clock with a backup cell, but no vendor example uses it and its address is datasheet inference. Wiring it up is worth its own task once the hardware confirms the part answers at 0x51.
