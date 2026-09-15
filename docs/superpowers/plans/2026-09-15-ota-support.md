# Over-the-Air Updates for the Temperature Display — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the temperature display uploadable with `pio run -e advance_70_ota -t upload`, protected by bootloader rollback, with a full-screen overlay during an upload.

**Architecture:** Secrets move from `include/secrets.h` to a gitignored `secrets.ini` and reach the code as build flags with the same macro names. The partition table becomes `default_16MB.csv`. The private library `esp32-ota-kit` v1.0.0 provides push and rollback; an `UpdateOverlay` observer on LVGL's top layer shows progress and errors. Pull stays disabled.

**Tech Stack:** PlatformIO (pioarduino platform-espressif32 55.03.39, arduino-esp32 3.3.9), LVGL ~9.1.0, LovyanGFX, PubSubClient, ArduinoJson 7, esp32-ota-kit v1.0.0 (`git+ssh://git@github.com/robominds/esp32-ota-kit.git#v1.0.0`).

**Spec:** `docs/superpowers/specs/2026-09-15-ota-support-design.md`

## Global Constraints

- Work on branch `ota-support` in `/Users/markcastelluccio/Crowpanel-7.0-HMI-IPS-Display`.
- New file headers follow this repo's style: `// Author: Mark Castelluccio <markacastelluccio@gmail.com>` then `// Written with assistance from Claude Code (Anthropic).`
- Commits end with `Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>`.
- Implementers build and commit. They never run `-t upload`, never open the serial port, never run espota, and never read, print or edit `secrets.ini` or `include/secrets.h`.
- Macro names stay: `WIFI_SSID`, `WIFI_PASSWORD`, `MQTT_HOST`, `MQTT_PORT` (bare integer), `MQTT_TOPIC_TEMP`, `MQTT_TOPIC_HUM`, `WEATHER_LATITUDE`, `WEATHER_LONGITUDE`; new `OTA_HOSTNAME`, `OTA_PASSWORD`, `FW_VERSION`.
- `custom_fw_version = 2.0.0`. Hostname `temperature-display`.
- `board_build.partitions = default_16MB.csv`, `board_upload.maximum_size = 6553600`.
- Pull disabled: `ota::Config::manifest_url` is left `nullptr`.
- The backlight is not touched by the update overlay.
- The firmware must link exactly one ` T verifyRollbackLater`.
- The shell is zsh: no word-splitting loops.
- Do not edit `docs/HARDWARE.md` (its `huge_app.csv` block quotes Elecrow's board file) or the copied drivers.

---

### Task 0 (controller): create `secrets.ini` from `include/secrets.h`

Not dispatched; values are never printed.

- [ ] **Step 1: Write `secrets.ini`** with a Python script that parses every `#define NAME value` in `include/secrets.h`, maps `WIFI_SSID→wifi_ssid`, `WIFI_PASSWORD→wifi_pass`, `MQTT_HOST→mqtt_host`, `MQTT_PORT→mqtt_port` (bare), `MQTT_TOPIC_TEMP→mqtt_topic_temp`, `MQTT_TOPIC_HUM→mqtt_topic_hum`, `WEATHER_LATITUDE→weather_latitude`, `WEATHER_LONGITUDE→weather_longitude`, adds `device_host = "temperature-display"` and `ota_password` copied from the OTA demo's `secrets.ini`, and writes `[secrets]` with the quoted values.
- [ ] **Step 2: Verify without printing**: re-parse both files and assert every mapped value is identical, every quoted value is free of `"`, `'`, `\`, `$`, `` ` ``, `;`, and `mqtt_port` is all digits. Print only `OK <n> keys`.
- [ ] **Step 3:** Delete `include/secrets.h` (it is untracked and fully copied; once Task 1 removes it from `.gitignore`, a leftover copy could be committed by accident). Confirm `git status --short` shows no `include/secrets.h`.

---

### Task 1: Secrets as build flags, two-slot partition table, version

**Files:**
- Modify: `platformio.ini`, `.gitignore`, `src/net.cpp`, `src/source_mqtt.cpp`, `src/source_mqtt.h`, `src/source_weather.cpp`
- Create: `secrets.ini.example`
- Delete: `include/secrets.h.template`

**Interfaces:**
- Consumes: `secrets.ini` (created by the controller; do not open it).
- Produces: build flags `WIFI_SSID`, `WIFI_PASSWORD`, `MQTT_HOST`, `MQTT_PORT`, `MQTT_TOPIC_TEMP`, `MQTT_TOPIC_HUM`, `WEATHER_LATITUDE`, `WEATHER_LONGITUDE`, `OTA_HOSTNAME`, `OTA_PASSWORD`, `FW_VERSION` for Task 2; `custom_fw_version`.

- [ ] **Step 1: Confirm the controller's setup**

Run: `ls secrets.ini && ls include/secrets.h 2>&1 | tail -1`
Expected: `secrets.ini`, then `No such file or directory` for `include/secrets.h`. Otherwise stop and report BLOCKED.

- [ ] **Step 2: `platformio.ini`, `[platformio]` section**

Replace

```ini
[platformio]
default_envs = advance_70
```

with

```ini
[platformio]
default_envs  = advance_70
extra_configs = secrets.ini       ; gitignored; copy secrets.ini.example
```

- [ ] **Step 3: `platformio.ini`, partition table and version**

Replace

```ini
board_upload.flash_size         = 16MB         ; N16R8. NOT 4MB - that is the
board_upload.maximum_size       = 16777216     ; older CrowPanel 7.0 HMI.
board_build.partitions          = huge_app.csv
```

with

```ini
board_upload.flash_size         = 16MB         ; N16R8. NOT 4MB - that is the older CrowPanel 7.0 HMI.
; Two OTA app slots of 0x640000 each, bundled with the core. Moving from
; huge_app.csv rewrites the partition table, so that first flash goes over USB.
board_build.partitions          = default_16MB.csv
board_upload.maximum_size       = 6553600      ; one slot, so the size check is honest

; Bump this to release a new version. It is the only place the version lives.
custom_fw_version = 2.0.0
```

- [ ] **Step 4: `platformio.ini`, build flags**

Replace

```ini
    -DARDUINO_USB_CDC_ON_BOOT=0
    -DARDUINO_USB_MODE=0
    ;
    ; LVGL is configured through build flags rather than a checked-in
```

with

```ini
    -DARDUINO_USB_CDC_ON_BOOT=0
    -DARDUINO_USB_MODE=0
    ;
    ; Secrets come from the gitignored secrets.ini. Its values carry their own
    ; double quotes (PlatformIO keeps them), so they get only the shell's single
    ; quotes; mqtt_port is a bare number. custom_fw_version is bare, so it gets
    ; both quote levels.
    -DFW_VERSION='"${this.custom_fw_version}"'
    -DWIFI_SSID='${secrets.wifi_ssid}'
    -DWIFI_PASSWORD='${secrets.wifi_pass}'
    -DMQTT_HOST='${secrets.mqtt_host}'
    -DMQTT_PORT=${secrets.mqtt_port}
    -DMQTT_TOPIC_TEMP='${secrets.mqtt_topic_temp}'
    -DMQTT_TOPIC_HUM='${secrets.mqtt_topic_hum}'
    -DWEATHER_LATITUDE='${secrets.weather_latitude}'
    -DWEATHER_LONGITUDE='${secrets.weather_longitude}'
    -DOTA_HOSTNAME='${secrets.device_host}'
    -DOTA_PASSWORD='${secrets.ota_password}'
    ;
    ; LVGL is configured through build flags rather than a checked-in
```

- [ ] **Step 5: Create `secrets.ini.example`**

```ini
; Copy to secrets.ini (gitignored) and fill in. Read by platformio.ini.
;
; Put every value except mqtt_port in DOUBLE QUOTES. PlatformIO keeps the
; quotes, so the value is already a C string literal when platformio.ini passes
; it to the compiler. Values pass through the shell, so they must not contain
;  "  '  \  $  `  or  ;
[secrets]
wifi_ssid         = "your-network"
wifi_pass         = "your-password"

; MQTT broker. Anonymous access; no username or password needed.
mqtt_host         = "192.0.2.10"
mqtt_port         = 1883                      ; bare number, no quotes

; The two topics carrying the indoor reading. Temperature must be the CELSIUS
; topic: this firmware stores Celsius throughout and converts only for display.
mqtt_topic_temp   = "office/DHT/tempc"
mqtt_topic_hum    = "office/DHT/hum"

; Maple Valley, WA. Confirmed against the National Weather Service, which
; resolves these coordinates to "Maple Valley WA".
weather_latitude  = "47.3673"
weather_longitude = "-122.0437"

; Over-the-air updates: the mDNS name (without .local) and the espota password.
device_host       = "temperature-display"
ota_password      = "a-long-password"
```

- [ ] **Step 6: `.gitignore` and the template**

In `.gitignore`, replace the line `include/secrets.h` with `secrets.ini`. Then:

```bash
git rm -q include/secrets.h.template
```

- [ ] **Step 7: Remove the header includes and stale comments**

- `src/net.cpp`: delete the line `#include "secrets.h"` and the blank line after it, so `#include <WiFi.h>` is followed by a blank line and `namespace net {`. Add, directly above `namespace net {`:

```cpp
// WIFI_SSID and WIFI_PASSWORD come from secrets.ini as build flags.
```

- `src/source_mqtt.cpp`: delete the line `#include "secrets.h"`. Replace `// From include/secrets.h, because which device publishes the indoor reading is` with `// From secrets.ini, because which device publishes the indoor reading is`.
- `src/source_mqtt.h`: replace `// include/secrets.h, temperature in Celsius and relative humidity. The broker` with `// secrets.ini, temperature in Celsius and relative humidity. The broker`.
- `src/source_weather.cpp`: delete the line `#include "secrets.h"`.

- [ ] **Step 8: Build and test**

```bash
grep -rn 'secrets\.h' src include lib test .gitignore platformio.ini || echo "no secrets.h references"
pio run -e advance_70 2>&1 | grep -E "Flash:|\[SUCCESS\]|\[FAILED\]|error:"
pio test -e native 2>&1 | tail -1
pio run -e diag 2>&1 | grep -E "\[SUCCESS\]|\[FAILED\]"
```

Expected: `no secrets.h references`; `Flash: ... (used ~1662000 bytes from 6553600 bytes)` and `[SUCCESS]`; the native summary line with 0 failures; `[SUCCESS]` for diag.

If a secret macro is reported undefined, stop and report NEEDS_CONTEXT naming the macro (do not open `secrets.ini`).

- [ ] **Step 9: Commit**

```bash
git add platformio.ini .gitignore secrets.ini.example src/net.cpp src/source_mqtt.cpp src/source_mqtt.h src/source_weather.cpp
git status --short
git commit -q -m "Move secrets to secrets.ini and switch to the two-slot partition table

Every secret reaches the code as a build flag with its old macro name, from
a gitignored secrets.ini. default_16MB.csv gives two 6.4 MB app slots for
over-the-air updates. Version 2.0.0.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

`git status --short` before the commit must show only the files added above and the deletion of `include/secrets.h.template`; `secrets.ini` must not appear.

---

### Task 2: Push updates, rollback and the update overlay

**Files:**
- Create: `src/update_overlay.h`, `src/update_overlay.cpp`
- Modify: `platformio.ini`, `src/main.cpp`, `src/net.cpp`

**Interfaces:**
- Consumes: Task 1's `OTA_HOSTNAME`, `OTA_PASSWORD`, `FW_VERSION` flags. Library `ota.h`: `ota::Config` (`hostname`, `push_password`, `running_version`, `manifest_url` default `nullptr`), `ota::Observer`, `ota::Source`, `ota::begin(const Config&, Observer&)`, `ota::setNetworkUp(bool)`, `ota::poll()`. The library itself logs the slot (`ota: running ...`) and `ota: push listening on ...`.
- Produces: `class UpdateOverlay : public ota::Observer` with `void init()` and `void poll(uint32_t now_ms)`.

- [ ] **Step 1: `platformio.ini`, library and OTA environment**

a) In `[env:advance_70]` `lib_deps`, after `    bblanchon/ArduinoJson @ ^7.0.0` add:

```ini
    ; Private library: fetched over SSH, so the build needs read access to it.
    esp32-ota-kit=git+ssh://git@github.com/robominds/esp32-ota-kit.git#v1.0.0
```

b) Directly before the `; Host tests.` comment block (the rule line above it), insert:

```ini
; ---------------------------------------------------------------------------
; Same firmware, uploaded over WiFi to a panel already running it.
;   pio run -e advance_70_ota -t upload
; ---------------------------------------------------------------------------
[env:advance_70_ota]
extends         = env:advance_70
upload_protocol = espota
upload_port     = ${secrets.device_host}.local
upload_flags    = --auth=${secrets.ota_password}

```

- [ ] **Step 2: `src/net.cpp`, hostname before the mode**

Replace

```cpp
void begin() {
    WiFi.onEvent(onWiFiEvent);
    WiFi.mode(WIFI_STA);
```

with

```cpp
void begin() {
    WiFi.onEvent(onWiFiEvent);
    // Before mode(): the station interface takes its DHCP hostname when it
    // starts. The same name answers mDNS for over-the-air updates.
    WiFi.setHostname(OTA_HOSTNAME);
    WiFi.mode(WIFI_STA);
```

and update the comment added in Task 1 to `// WIFI_SSID, WIFI_PASSWORD and OTA_HOSTNAME come from secrets.ini as build flags.`

- [ ] **Step 3: Create `src/update_overlay.h`**

```cpp
// A full-screen panel shown while a firmware update is received.
//
// It lives on LVGL's top layer, so it covers whichever view is showing, and it
// is hidden the rest of the time. esp32-ota-kit calls the observer methods from
// ota::poll(), outside lv_timer_handler(), and a push blocks loop() until it
// ends, so each method repaints the screen itself.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic).

#pragma once

#include <cstdint>

#include <ota.h>

class UpdateOverlay : public ota::Observer {
public:
    // Builds the hidden panel. Call once, after ui::init().
    void init();

    // Hides the panel once an error has been shown for ERROR_HOLD_MS. Call
    // every loop.
    void poll(uint32_t now_ms);

    void onTransferStarted(ota::Source source) override;
    void onProgress(ota::Source source, uint8_t percent) override;
    void onRebooting(ota::Source source) override;
    void onError(ota::Source source, const char* message) override;

    static constexpr uint32_t ERROR_HOLD_MS = 5000;
};
```

- [ ] **Step 4: Create `src/update_overlay.cpp`**

```cpp
// Implementation of the update overlay.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic).

#include "update_overlay.h"

#include <Arduino.h>
#include <lvgl.h>

#include <cstdio>

namespace {

lv_obj_t* g_panel   = nullptr;
lv_obj_t* g_bar     = nullptr;
lv_obj_t* g_percent = nullptr;
lv_obj_t* g_status  = nullptr;

bool     g_error_showing  = false;
uint32_t g_error_since_ms = 0;

void show() {
    g_error_showing = false;
    lv_obj_remove_flag(g_panel, LV_OBJ_FLAG_HIDDEN);
}

void setStatus(const char* text, lv_color_t color) {
    lv_label_set_text(g_status, text);
    lv_obj_set_style_text_color(g_status, color, 0);
}

void setPercent(uint8_t percent) {
    lv_bar_set_value(g_bar, percent, LV_ANIM_OFF);
    lv_label_set_text_fmt(g_percent, "%u %%", static_cast<unsigned>(percent));
}

}  // namespace

void UpdateOverlay::init() {
    g_panel = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(g_panel);
    lv_obj_set_size(g_panel, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(g_panel, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_panel, LV_OPA_COVER, 0);
    lv_obj_add_flag(g_panel, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* title = lv_label_create(g_panel);
    lv_label_set_text(title, "Updating firmware");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -110);

    g_bar = lv_bar_create(g_panel);
    lv_obj_set_size(g_bar, 600, 36);
    lv_bar_set_range(g_bar, 0, 100);
    lv_obj_set_style_bg_color(g_bar, lv_palette_darken(LV_PALETTE_GREY, 3), LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_bar, lv_palette_main(LV_PALETTE_BLUE), LV_PART_INDICATOR);
    lv_obj_align(g_bar, LV_ALIGN_CENTER, 0, 0);

    g_percent = lv_label_create(g_panel);
    lv_obj_set_style_text_font(g_percent, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(g_percent, lv_color_white(), 0);
    lv_obj_align(g_percent, LV_ALIGN_CENTER, 0, 60);

    g_status = lv_label_create(g_panel);
    lv_obj_set_style_text_font(g_status, &lv_font_montserrat_28, 0);
    lv_obj_align(g_status, LV_ALIGN_CENTER, 0, 120);

    setPercent(0);
    setStatus("", lv_color_white());
}

void UpdateOverlay::poll(uint32_t now_ms) {
    if (!g_error_showing || now_ms - g_error_since_ms < ERROR_HOLD_MS) return;
    g_error_showing = false;
    lv_obj_add_flag(g_panel, LV_OBJ_FLAG_HIDDEN);
}

void UpdateOverlay::onTransferStarted(ota::Source) {
    show();
    setPercent(0);
    setStatus("Receiving update", lv_color_white());
    lv_timer_handler();
}

void UpdateOverlay::onProgress(ota::Source, uint8_t percent) {
    setPercent(percent);
    lv_timer_handler();
}

void UpdateOverlay::onRebooting(ota::Source) {
    setPercent(100);
    setStatus("Rebooting", lv_color_white());
    lv_timer_handler();
}

void UpdateOverlay::onError(ota::Source, const char* message) {
    // A wrong password fails before onTransferStarted, so show the panel here.
    show();
    char text[96];
    snprintf(text, sizeof text, "Update failed: %s", message);
    setStatus(text, lv_palette_main(LV_PALETTE_RED));
    g_error_showing  = true;
    g_error_since_ms = millis();
    lv_timer_handler();
}
```

- [ ] **Step 5: `src/main.cpp`**

a) After the line `#include <new>` and its blank line, the include block gains `#include <ota.h>` directly after `#include <Wire.h>`, and `#include "update_overlay.h"` after `#include "ui.h"`:

```cpp
#include <Arduino.h>
#include <Wire.h>
#include <ota.h>

#include <new>

#include "Channel.h"
#include "board_pins.h"
#include "brightness.h"
#include "display_driver.h"
#include "net.h"
#include "rtc.h"
#include "panel_mcu.h"
#include "source_mqtt.h"
#include "source_weather.h"
#include "touch.h"
#include "ui.h"
#include "update_overlay.h"
```

b) Directly after

```cpp
const uint16_t g_chart_points[CHANNEL_COUNT] = {WEATHER_CHART_POINTS,
                                                OFFICE_CHART_POINTS};
```

insert a blank line and:

```cpp
// Shows push updates on top of both views.
UpdateOverlay g_update_overlay;
```

c) Replace `    Serial.println("\nCrowPanel Advance 7.0 temperature display");` with:

```cpp
    Serial.printf("\nCrowPanel Advance 7.0 temperature display %s\n", FW_VERSION);
```

d) Replace

```cpp
    ui::init(g_channels, g_chart_points, CHANNEL_COUNT);
    ui::refresh(millis());

    net::begin();
```

with

```cpp
    ui::init(g_channels, g_chart_points, CHANNEL_COUNT);
    ui::refresh(millis());
    g_update_overlay.init();

    // Push updates and rollback. Before the backlight comes on, because begin()
    // writes the boot counter to flash, and before Wi-Fi, which starts push.
    // Pull updates stay off: no manifest_url.
    ota::Config ota_config;
    ota_config.hostname        = OTA_HOSTNAME;
    ota_config.push_password   = OTA_PASSWORD;
    ota_config.running_version = FW_VERSION;
    ota::begin(ota_config, g_update_overlay);

    net::begin();
```

e) Replace

```cpp
    net::poll();
    rtc::poll(net::connected());
```

with

```cpp
    net::poll();
    // A push blocks here until it finishes; MQTT may drop meanwhile and
    // reconnects on its own if the update fails.
    ota::setNetworkUp(net::connected());
    ota::poll();
    g_update_overlay.poll(millis());
    rtc::poll(net::connected());
```

- [ ] **Step 6: Build and verify**

```bash
pio run -e advance_70 2>&1 | grep -E "esp32-ota-kit|Flash:|\[SUCCESS\]|\[FAILED\]|error:"
pio run -e advance_70 2>&1 | grep -i "warning" | grep -E "src/(main|net|update_overlay)\.cpp" || echo "no warnings from main, net or update_overlay"
~/.platformio/packages/toolchain-xtensa-esp-elf/bin/xtensa-esp32s3-elf-nm .pio/build/advance_70/firmware.elf | grep -i verifyRollbackLater
pio run -e advance_70_ota 2>&1 | grep -E "\[SUCCESS\]|\[FAILED\]"
pio test -e native 2>&1 | tail -1
```

Expected: `esp32-ota-kit @ 1.0.0+sha.38ea79a` in the dependency graph, `Flash:` under 6553600 bytes, `[SUCCESS]`; `no warnings from main, net or update_overlay`; exactly one line ending ` T verifyRollbackLater`; `[SUCCESS]` for the OTA env (build only); native tests still pass.

If the library cannot be fetched (SSH auth), stop and report BLOCKED with the Library Manager output.

- [ ] **Step 7: Commit**

```bash
git add platformio.ini src/main.cpp src/net.cpp src/update_overlay.h src/update_overlay.cpp
git status --short
git commit -q -m "Add push updates with rollback through esp32-ota-kit

pio run -e advance_70_ota -t upload now updates a panel over WiFi. A new
image is confirmed 30 s after WiFi comes up; one that crashes first is rolled
back by the bootloader. A full-screen overlay shows progress and errors.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 3: README

**Files:**
- Modify: `README.md`

**Interfaces:**
- Consumes: Tasks 1-2 (envs `advance_70`, `advance_70_ota`, `native`, `diag`; `secrets.ini.example`; `src/update_overlay.*`).

- [ ] **Step 1: "Build and flash"**

Replace

````markdown
```sh
pio run -e advance_70 -t upload   # build and flash the board
pio test -e native                # run 70 host tests, no hardware needed
```

Before flashing, copy `include/secrets.h.template` to `include/secrets.h` and
fill in your Wi-Fi credentials. `include/secrets.h` is gitignored; the template
is not.
````

with

````markdown
```sh
cp secrets.ini.example secrets.ini     # then fill it in
pio run -e advance_70 -t upload        # build and flash over USB
pio run -e advance_70_ota -t upload    # afterwards: update over WiFi
pio test -e native                     # run 70 host tests, no hardware needed
```

`secrets.ini` holds the Wi-Fi credentials, the MQTT broker and topics, the
weather coordinates, and the over-the-air hostname and password. It is
gitignored. Put every value except `mqtt_port` in double quotes, and keep
`"`, `'`, `\`, `$`, `` ` `` and `;` out of the values.

The build fetches the private library `robominds/esp32-ota-kit` (tag `v1.0.0`)
over SSH, so it needs read access to that repository.

### Updating over WiFi

The first flash after pulling this change must go over USB: it replaces the
single-app partition table with `default_16MB.csv`, which has two 6.4 MB app
slots. After that, `pio run -e advance_70_ota -t upload` sends the build to
`<device_host>.local`, authenticated with `ota_password`. A full-screen
"Updating firmware" panel shows progress on either view, and the panel reboots
into the new version. The charts start empty after every update, because their
history lives in RAM.

A new image is confirmed 30 s after Wi-Fi comes up, or 90 s after boot,
whichever comes first. One that crashes or hangs before then is rolled back by
the bootloader to the previous version. The same window has a cost:
power-cycling the panel within about 40 s of an update also rolls back a good
image; run the update again. MQTT may disconnect during an upload and
reconnects on its own if the update fails.

espota has the panel connect back to the computer. If the upload ends with
`No response from device`, allow PlatformIO's Python
(`~/.platformio/penv/bin/python`) to accept incoming connections in the macOS
firewall. `Authentication Failed` means `ota_password` differs from the one the
running firmware was built with.

`firmware.bin` contains the Wi-Fi and OTA passwords as plain strings, and espota
traffic is authenticated but not encrypted. Use it on a network you trust.
````

- [ ] **Step 2: Layout table**

- Replace the row ``| `include/` | `secrets.h.template` for Wi-Fi credentials; `secrets.h` itself is gitignored. |`` with ``| `secrets.ini.example` | Template for the gitignored `secrets.ini`: Wi-Fi, MQTT, weather location, over-the-air hostname and password. |``
- After the ``| `src/rtc.*` |`` row add ``| `src/update_overlay.*` | Full-screen progress and error panel for over-the-air updates, from `esp32-ota-kit` events. |``

- [ ] **Step 3: Verify and commit**

```bash
grep -n 'secrets\.h' README.md || echo "no secrets.h references"
grep -c '^```' README.md
git add README.md
git commit -q -m "README: secrets.ini, updating over WiFi, rollback and its limits

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

Expected: `no secrets.h references`; an even fence count.

---

### Task 4 (controller, hardware — each step waits for the user's go)

Panel at `/dev/cu.wchusbserial2120`, currently running the OTA demo 1.2.5 on the same partition table. Capture with `.superpowers/sdd/2026-09-15-ota-support/capture.py` (DTR/RTS low, RTS pulse; `--no-reset` to listen only), run with `~/.platformio/penv/bin/python`. Never echo `ota_password`: scripts read it from `secrets.ini` into a variable. After any push or install, capture with `--no-reset` and do not reset the panel until `ota: image confirmed` appears. Before every flash or push of a new build, `nm` shows exactly one ` T verifyRollbackLater`. Temporary `abort();` lines are never committed.

1. **USB flash 2.0.0.** `pio run -e advance_70 -t upload --upload-port /dev/cu.wchusbserial2120`. Serial: `CrowPanel Advance 7.0 temperature display 2.0.0`, `ota: running app0, state serial-flashed, boot #N`, `wifi: up, <ip>`, `ota: push listening on temperature-display.local:3232`, MQTT connects, weather fetched. User: charts, clock view, long-press unit toggle, brightness as before; no overlay visible.
2. **Push 2.0.1** from the chart view. Serial: `ota: push start`, `ota: push done, rebooting`, then `2.0.1`, `app1 pending verify`, `ota: network up, confirming in 30 s`, `ota: image confirmed (ESP_OK)`. User: overlay with climbing bar and percentage, `Rebooting`, then readings return (charts empty).
3. **Wrong password.** `~/.platformio/penv/bin/python ~/.platformio/packages/framework-arduinoespressif32/tools/espota.py -i temperature-display.local -a wrong-password -f .pio/build/advance_70_ota/firmware.bin`. Run this step from the clock view. Serial: `ota: push error 0 (auth failed)` twice about a second apart (espota retries with an MD5 hash). User: overlay with red `Update failed: auth failed`, hidden about 5 s after the second error; readings continue; no reboot.
4. **Rollback.** Set 2.0.2, `abort();` as the first line of `loop()`, push. Serial: panic, then `2.0.1`, `app1 confirmed`. Remove the line.
5. **Crash after WiFi.** Precondition: the previous boot logged `mqtt: connected and subscribed` (if the broker is down, the crash never happens and the image confirms; recover over USB with step 1's command). Set 2.0.2, `abort();` directly before `Serial.println("mqtt: connected and subscribed");` in `source_mqtt.cpp`'s `connect()`, push. Serial: `mqtt: connecting to ...`, panic, then `2.0.1 confirmed`. Remove the line.
6. **Update inside the window.** Set 2.0.4, `pio run -e advance_70_ota`, copy `.pio/build/advance_70_ota/firmware.bin` to `scratchpad/fw-204.bin`. Set 2.0.3 and push it with `-t upload`. A script watching the `--no-reset` capture, the moment it shows `ota: network up, confirming in 30 s`, reads `ota_password` from `secrets.ini` without echoing it and runs `~/.platformio/penv/bin/python ~/.platformio/packages/framework-arduinoespressif32/tools/espota.py -i temperature-display.local -a "$PW" -f scratchpad/fw-204.bin` (as the demo's `m5-window.sh`). Serial on 2.0.3: `ota: image confirmed before update (ESP_OK)`; then `CrowPanel Advance 7.0 temperature display 2.0.4`, which confirms normally. Leave `custom_fw_version = 2.0.4`.
7. `git diff --stat` shows only `platformio.ini` (both `abort();` lines gone) with `custom_fw_version = 2.0.4` (the confirmed image); commit `Release 2.0.4: over-the-air updates verified on the CrowPanel Advance`.

### Task 5 (controller, release — only after the user approves)

1. Clean clone of `ota-support` into the scratchpad, copy `secrets.ini`, `pio run -e advance_70`: `[SUCCESS]`, one ` T verifyRollbackLater`.
2. Merge `ota-support` into `main` and push to `robominds/Crowpanel-Advance-7.0-HMI-Display` (public; building needs access to the private library).
