# Temperature Monitor — Elecrow CrowPanel Advance 7.0-HMI Display

Firmware for the Elecrow CrowPanel Advance 7.0-HMI ESP32-S3 display (SKU
`DIS02170A`, ESP32-S3-WROOM-1-N16R8, IPS 800x480). It shows two temperature
readings, each with humidity and a twelve-hour scrolling chart:

- **Maple Valley** — outdoor conditions, fetched from the [Open-Meteo](https://open-meteo.com/)
  public API over plain HTTP. No API key.
- **Mark's Office** — an indoor reading published by an MQTT broker at
  `192.0.2.10:1883`. The broker accepts anonymous connections; there is
  nothing to authenticate.

No photograph exists yet — see the warning below about the state of this
project.

## Build and flash

```sh
pio run -e advance_70 -t upload   # build and flash the board
pio test -e native                # run 64 host tests, no hardware needed
```

Before flashing, copy `include/secrets.h.template` to `include/secrets.h` and
fill in your Wi-Fi credentials. `include/secrets.h` is gitignored; the template
is not.

## Layout

| Path | Contents |
| --- | --- |
| `lib/history/` | Fixed-capacity sample ring buffer and chart downsampling. No Arduino, ESP-IDF or LVGL includes, so it tests on the host. |
| `test/test_history/` | 26 host tests for the ring buffer and bucketing. |
| `lib/channel/` | One data channel: latest reading, per-channel staleness rule, and its history. Depends on `lib/history`. |
| `test/test_channel/` | 12 host tests, including the `millis()` rollover. |
| `lib/parse/` | Payload parsing for MQTT bare-decimal values and Open-Meteo JSON. |
| `test/test_parse/` | 26 host tests for both payload formats, malformed input included. |
| `include/` | `secrets.h.template` for Wi-Fi credentials; `secrets.h` itself is gitignored. |
| `src/board_pins.h` | Every GPIO for this board, named, with the source for each value. |
| `src/display_driver.*` | The RGB panel over LovyanGFX, plus the LVGL 9 binding. Does not own the backlight. |
| `src/panel_mcu.*` | The STC8H1K28 companion microcontroller: backlight, touch reset, buzzer. |
| `src/touch.*` | GT911 over raw I2C, polled and rate-limited. |
| `src/net.*` | Wi-Fi with non-blocking reconnection. |
| `src/source_mqtt.*` | The office reading, subscribed from the MQTT broker. |
| `src/source_weather.*` | The Maple Valley reading, polled from Open-Meteo. |
| `src/ui.*` | The screen: two stacked channel rows over a status bar. |
| `src/main.cpp` | Startup order and the main loop. |
| `docs/HARDWARE.md` | The hardware reference this README summarizes. Read it before touching a GPIO. |

## This board is NOT the CrowPanel 7.0 HMI

Elecrow sell two 7-inch 800x480 ESP32-S3 panels whose names differ by one
word. This project targets the **Advance**. Its sibling project, targeting the
plain **7.0 HMI**, is a different board in nearly every way that matters to
firmware, and copying anything from it — wiring, pin numbers, backlight code —
produces a board that flashes cleanly and shows nothing. This is the single
most expensive mistake available on this project; the full comparison is
`docs/HARDWARE.md` section 0, reproduced here:

| | CrowPanel **7.0 HMI** (`DIS08070H`) | CrowPanel **Advance 7.0-HMI** (`DIS02170A`) |
| --- | --- | --- |
| Panel | TN, EK9716BD3 + EK73002ACGB | **IPS, SC7277** |
| Module | ESP32-S3-WROOM-1-**N4R8** | ESP32-S3-WROOM-1-**N16R8** |
| Flash | **4 MB** | **16 MB** |
| PSRAM | 8 MB octal @ 80 MHz | 8 MB octal, 80 or **120 MHz** |
| I2C bus | GPIO19 / GPIO20 | **GPIO15 / GPIO16** |
| GPIO19 / GPIO20 | the I2C bus | **microphone**, or UART1, or the radio socket |
| Expander | PCA9557 @ 0x18 | **STC8H1K28 MCU @ 0x30** (V1.2+) |
| Backlight | GPIO2, direct PWM | **no GPIO at all — an I2C command** |
| Pixel clock | 15 MHz | **16 MHz** on V1.3+, **21 MHz** on V1.0 and V1.2 |
| Porches | 40 / 48 / 40, 1 / 31 / 13 | **8 / 4 / 8 on both axes** |
| Upload speed | 460800; 921600 fails | **921600** |
| Free GPIO | one, `GPIO_D` = IO38 | **two**, GPIO2 and GPIO8, on V1.3+ only |
| Onboard extras | microSD, I2S | microSD, speaker, **mic, RTC, battery charger, buzzer, radio socket** |
| Revisions | V1.0 – V3.0 | V1.0 – V1.5 |

## Three things that will cost you an evening

**The backlight is not a GPIO.** On this board there is no pin to PWM. Turning
the panel on or dimming it means sending an I2C command to the STC8H1K28
companion microcontroller, and that command's encoding **inverted between
board revisions**: V1.2 writes `0x05`–`0x10` with `0x10` brightest, while
V1.3 and later write `0`–`245` with `0` brightest and `245` off. Sending the
wrong revision's encoding does not error — the byte is silently accepted — so
a mismatch just looks like a dead backlight. `src/panel_mcu.h` documents both
encodings and which one this firmware assumes.

**The flash is 16 MB, not 4 MB — and that inverts the sibling project's
warning.** The sibling board's README warns that setting 16 MB flash causes a
boot loop, because that board really is 4 MB. Here the correct figure is the
opposite one: this module is an N16R8, and setting 4 MB on this board is the
mistake. Confirm the module's laser marking before assuming either number.

**`ARDUINO_USB_CDC_ON_BOOT` must stay off.** Neither this board nor its
sibling has native USB — the S3's USB D-/D+ pins are consumed by other
peripherals (here, the microphone analog mux) — so flashing and serial both go
through a CH340K bridge on UART0. Enabling CDC-on-boot retargets `Serial` to a
USB-CDC peripheral that does not physically exist here, which produces a
silently dead serial monitor with no error to explain it.

## Nothing here has touched hardware yet

The board has not arrived. Every pin assignment, timing value, and I2C address
in this repository comes from Elecrow's committed source, schematics, and
wiki — not from a scan of a physical unit. **No value in this project has been
verified against hardware.** `docs/HARDWARE.md` section 8 is the checklist for
what to check, roughly in order of what a wrong answer costs, the moment the
board is in hand: board revision from the silkscreen, the module's laser
marking, whether the backlight responds to the assumed encoding, an I2C bus
scan, and so on through touch axis orientation and power supply headroom.

## Verified so far

- `pio test -e native` passes 64 host tests for parsing, sample history, and
  channel staleness — all pure logic, none of it touching the panel, touch, or
  network.
- `pio run -e advance_70` builds and links successfully: flash 1,526,863 of
  3,145,728 bytes (48.5%), internal RAM 185,544 of 327,680 bytes (56.6%).

Nothing beyond that. The panel, touch, backlight, and both network sources are
unverified until the board arrives.

## License

MIT. See [LICENSE](LICENSE).

## Authorship

Mark Castelluccio <markacastelluccio@gmail.com>

Developed with Claude Code (Anthropic Claude Opus 5).
