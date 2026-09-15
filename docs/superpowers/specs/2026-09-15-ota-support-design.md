# Over-the-Air Updates for the Temperature Display — Design

Author: Mark Castelluccio <markacastelluccio@gmail.com>
Drafted with Claude Code (Anthropic Claude Opus 5). **Draft for review:** written
while the user was away. The four decisions in "Decided with the user" were made
in conversation on 2026-09-15; the three open questions were answered on review.

## Goal

Make this application uploadable over WiFi with
`pio run -e advance_70_ota -t upload`, protected by bootloader rollback, using
the shared library `robominds/esp32-ota-kit` (private). While an upload runs the
panel shows a full-screen overlay instead of the readings.

This is sub-project 3 of 3. It starts after the library is tagged `v1.0.0`
(sub-project 2 verifies it on hardware). Library spec:
`~/projects/esp32-ota-kit/docs/superpowers/specs/2026-09-15-esp32-ota-kit-design.md`.

## Decided with the user

| Decision | Choice |
| --- | --- |
| OTA kinds | Push + rollback only; pull disabled (`manifest_url = nullptr`) |
| Secrets | All secrets move from `include/secrets.h` to a gitignored `secrets.ini` |
| Update UI | Full-screen overlay over whichever view is showing |
| Code source | The shared `esp32-ota-kit` library |

## Changes

| File | Change |
| --- | --- |
| `platformio.ini` | `[platformio] extra_configs = secrets.ini`; `board_build.partitions = default_16MB.csv`; `board_upload.maximum_size = 6553600`; `custom_fw_version`; build flags for every secret and `FW_VERSION`; `lib_deps` gains the library; new `[env:advance_70_ota]` (espota) |
| `secrets.ini.example` (new), `secrets.ini` (local, gitignored) | replaces `include/secrets.h.template` / `include/secrets.h` |
| `include/secrets.h.template` | deleted; `include/secrets.h` removed from `.gitignore`, `secrets.ini` added |
| `src/net.cpp`, `src/source_mqtt.cpp`, `src/source_weather.cpp` | drop `#include "secrets.h"`; the macros now come from build flags with the same names |
| `src/net.cpp` | `WiFi.setHostname(OTA_HOSTNAME)` before `WiFi.mode(WIFI_STA)` so the DHCP name matches the OTA name |
| `src/update_overlay.{h,cpp}` (new) | `UpdateOverlay : public ota::Observer` drawing on `lv_layer_top()` |
| `src/main.cpp` | boot banner prints `FW_VERSION`; `ota::begin()` in `setup()` right after `ui::init()` and the overlay, before `net::begin()` and before the first `lv_timer_handler()` / `brightness::poll()` lights the backlight; `ota::setNetworkUp(net::connected())` and `ota::poll()` in `loop()` after `net::poll()`, plus the overlay's error-hide check |
| `README.md` | secrets.ini setup, first USB flash, updating over WiFi, rollback, security limits, troubleshooting |

## Secrets

`secrets.ini` (values double-quoted, as in the OTA demo, except the numeric port):

```ini
[secrets]
wifi_ssid         = "your-network"
wifi_pass         = "your-password"
mqtt_host         = "192.0.2.10"
mqtt_port         = 1883              ; bare number: it becomes an integer
mqtt_topic_temp   = "office/DHT/tempc"
mqtt_topic_hum    = "office/DHT/hum"
weather_latitude  = "47.3673"
weather_longitude = "-122.0437"
device_host       = "temperature-display"   ; mDNS/OTA name, no ".local"
ota_password      = "a-long-password"
```

Build flags keep the existing macro names:
`-DWIFI_SSID='${secrets.wifi_ssid}'`, `-DWIFI_PASSWORD='${secrets.wifi_pass}'`,
`-DMQTT_HOST='${secrets.mqtt_host}'`, `-DMQTT_PORT=${secrets.mqtt_port}`,
`-DMQTT_TOPIC_TEMP=...`, `-DMQTT_TOPIC_HUM=...`, `-DWEATHER_LATITUDE=...`,
`-DWEATHER_LONGITUDE=...`, `-DOTA_HOSTNAME='${secrets.device_host}'`,
`-DOTA_PASSWORD='${secrets.ota_password}'`, `-DFW_VERSION='"${this.custom_fw_version}"'`.

The existing values were checked (without printing them) and contain none of
the characters that break shell-quoted flags (`"`, `'`, `\`, `$`, `` ` ``, `;`).
Migration copies them from `include/secrets.h` into `secrets.ini` with a script
that never prints them. The `diag` env overrides `build_flags` and needs none of
the secrets; `native` builds only `lib/`.

## Partition table and first flash

`huge_app.csv` (one 3 MB app) becomes `default_16MB.csv` (two 6.4 MB slots). The
current image is 1,662,256 bytes, leaving about 4.8 MB of headroom; the library
adds roughly 60-100 KB. The first flash after the change must be over USB. NVS
starts at the same offset in both tables; this app stores nothing in NVS
except, from now on, the library's boot counter.

## Update overlay

- Built once in `setup()` on `lv_layer_top()`: a full-screen dark panel with
  "Updating firmware" (large), a progress bar with percentage, and a status line;
  hidden by default. It covers both the charts view and the clock view.
- `onTransferStarted(Push)`: show the overlay, bar at 0, status
  "Receiving update"; `lv_timer_handler()`.
- `onProgress(Push, p)`: bar and percentage; `lv_timer_handler()`.
- `onRebooting(Push)`: bar 100, status "Rebooting"; `lv_timer_handler()`.
- `onError(Push, msg)`: status "Update failed: <msg>" in red; hide the overlay
  5 s later (checked from `loop()`).
- `onSlot`, `onPushListening`: serial log only (this UI has no slot row).
- Touch is not polled during a transfer (the library blocks `poll()`), so
  long-press actions cannot fire mid-update.
- The backlight is left as `brightness` has set it; no forced full brightness
  during an update.

## Loop and connectivity during an upload

A push blocks `loop()` for the transfer (about 15-30 s for 1.7 MB). During that
time MQTT (keepalive 30 s) may time out and the weather poll pauses; after a
successful push the device reboots, and after a failed one `source_mqtt`
reconnects through its existing logic. Chart history lives in RAM, so every
successful update starts the charts empty *(accepted consequence; persisting
history is out of scope)*.

The app already owns WiFi retries (`net.cpp`), which suits the library contract.
The app does not use mDNS itself; ArduinoOTA starts it with `OTA_HOSTNAME`.

## Rollback

Library defaults: confirm 30 s after WiFi first connects, 90 s from boot as a
fallback, and confirm before an update writes. A build that crashes before then
(for example in MQTT or weather code shortly after WiFi) is reverted by the
bootloader. Power-cycling within about 40 s of an update also reverts a good
image — documented in the README.

## Version

`custom_fw_version = 2.0.0` (first OTA-capable release; the app had no version).
Printed in the boot banner (`CrowPanel Advance 7.0 temperature display 2.0.0`);
not shown on screen.

## Verification

- Host: the existing `pio test -e native` suites still pass.
- Build: `advance_70` and `advance_70_ota` build; image below 6,553,600 bytes;
  `nm` shows exactly one ` T verifyRollbackLater`; no compiler warnings from
  `src/`.
- Hardware (each with the user's go):
  1. USB flash 2.0.0: charts, clock, touch, MQTT, weather and brightness behave as
     before; serial shows `ota: running app0, state serial-flashed` and
     `ota: push listening on <host>.local:3232`.
  2. Push 2.0.1 from either view: overlay with climbing progress, "Rebooting",
     then the readings return; `ota: image confirmed (ESP_OK)` about 30 s after
     WiFi.
  3. Wrong password: espota fails; overlay shows the error and hides after 5 s;
     readings continue; MQTT reconnects if it dropped.
  4. Rollback: push a build with `abort()` as the first line of `loop()`; the
     bootloader restores the previous version.
  5. Crash after WiFi: push a build whose `source_mqtt` `connect()` calls
     `abort()` once the broker accepts; it crashes seconds after WiFi and the
     bootloader restores the previous version.
  6. Update inside the window: push twice within 30 s; the log shows
     `ota: image confirmed before update`.

## Security limits

As in the library: `firmware.bin` carries the WiFi password, MQTT host and OTA
password as plain strings; this app never serves it, but anyone who obtains a
build can read them. espota traffic is authenticated but not encrypted.

## Decided with the user on review

1. Hostname `temperature-display` (`device_host` in `secrets.ini`). Switching
   the panel between this app and the OTA demo means a USB flash anyway, since
   each firmware answers only to its own name.
2. Version starts at 2.0.0 (`MAJOR.MINOR.PATCH`, as the library requires).
3. The backlight stays as `brightness` has set it during an update.

## Out of scope

Pull updates, persisting chart history, WiFi provisioning, UI changes beyond the
overlay.
