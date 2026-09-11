# MQTT Temperature Display — Design

**Date:** 2026-09-11
**Target:** Elecrow CrowPanel Advance 7.0-HMI, SKU `DIS02170A`, 800x480 IPS
**Status:** Approved for planning. No hardware in hand; the board has not arrived.

---

## 1. What this is

A wall display that shows two temperatures large enough to read across a room,
each with twelve hours of history underneath.

| Channel | Source | Cadence |
| --- | --- | --- |
| **Maple Valley** | Open-Meteo public API over plain HTTP | 15 minutes |
| **Mark's Office** | MQTT broker at `192.0.2.10:1883` | ~10 seconds |

Both readings carry humidity alongside the temperature. Neither source requires
credentials.

This is a sibling of the existing `Crowpanel-7.0-HMI-Display` project, which
reads a local DHT20 and charts one hour. The architecture is deliberately the
same shape; the data sources and the board are different.

### Verified source details

The broker was polled on 2026-09-11 and accepts anonymous connections. Four
devices publish, of which one is wanted:

```
office/DHT/tempc      19.6
office/DHT/tempf      67.3
office/DHT/hum        55.1
office/Status/uptime  20044.0
```

Payloads are **bare decimal ASCII**, not JSON. Both Celsius and Fahrenheit are
published; this firmware subscribes to Celsius and converts at display time.

The weather request, confirmed returning HTTP 200 with no redirect:

```
http://api.open-meteo.com/v1/forecast?latitude=47.3673&longitude=-122.0437
  &current=temperature_2m,relative_humidity_2m,weather_code
  &timezone=America%2FLos_Angeles
```

Plain HTTP is deliberate. It removes the TLS stack from the firmware, saving
roughly 40 KB of heap that the frame buffers want, and removes the certificate
expiry that silently kills embedded HTTPS clients. Those coordinates were
confirmed against the National Weather Service, which resolves them to
"Maple Valley WA".

---

## 2. Layout

Two full-width stacked rows over a status bar. Charts get the full panel width,
which matters because twelve hours across 380 pixels is already coarse.

```
+-----------------------------------------------+
|  MAPLE VALLEY        61.0 F      62% RH       |
|    .-.       .--.                             |
|  -    -------    ------.        .---          |
|                         --------              |
+-----------------------------------------------+
|  MARK'S OFFICE       67.3 F      55% RH       |
|     .--.      .-.        .----                |
|  ---    ------   --------                     |
+-----------------------------------------------+
|  mqtt ok   wifi ok              10:34 PDT     |
+-----------------------------------------------+
```

> **Deferred: the clock in the status bar.** The mock above shows `10:34 PDT`
> at the right of the status bar. The firmware does not draw it, and this is
> deliberate rather than an oversight. The time would come from the onboard
> PCF8563 RTC, whose I2C address (`0x51`) is taken from the part's datasheet and
> not from any Elecrow sketch or schematic annotation — no code in their
> repository touches that chip and the schematic shows no address pins, so the
> address is unconfirmed, and the `#INT` line is not wired to the ESP32 either.
> Probing a wrong address on a bus shared with the panel MCU and the touch
> controller is not something to do blind. The clock stays out until the board
> is in hand and the RTC has been verified on it; the mock is kept as the
> intended end state.

Channels are a **registry**, not two hardcoded panels. Adding the garage or AC2
later is a table entry and a row, not a refactor. The registry is the reason the
indirection exists; with two channels it would otherwise be over-engineering.

---

## 3. Module structure

| Path | Contents | Host-testable |
| --- | --- | --- |
| `lib/history/` | Ring buffer, retention, chart downsampling | **Yes** |
| `lib/parse/` | MQTT payload and Open-Meteo JSON extraction | **Yes** |
| `src/board_pins.h` | Every GPIO, named, with its source | — |
| `src/panel_mcu.*` | The STC8 at I2C 0x30: backlight, touch activation | — |
| `src/display_driver.*` | RGB panel via LovyanGFX, LVGL 9 binding | — |
| `src/touch.*` | GT911, polled on a fixed interval | — |
| `src/net.*` | Wi-Fi connect and reconnect | — |
| `src/source_mqtt.*` | Subscribes to `office/DHT/#` | — |
| `src/source_weather.*` | Polls Open-Meteo, parses, feeds a channel | — |
| `src/channels.*` | Registry: name, latest reading, staleness, history | — |
| `src/ui.*` | Two stacked rows and the status bar | — |
| `src/main.cpp` | Startup order and the main loop | — |

`lib/history` and `lib/parse` include no Arduino, ESP-IDF, LVGL or FreeRTOS
headers, so the logic that is easy to get wrong and painful to debug on a panel
is tested on the host instead.

### Data flow

```
  MQTT broker  --> source_mqtt   --\
                                    >--> channels --> ui --> LVGL --> panel
  Open-Meteo   --> source_weather --/       |
                                            +--> history (per channel)
```

Sources never touch the UI and the UI never touches the network. Both write into
the channel registry; the UI reads from it. That is what lets both sources be
replaced or a third added without opening `ui.cpp`.

---

## 4. Design decisions and their reasons

### 4.1 Per-channel staleness thresholds

A single timeout cannot serve both sources. The office publishes every 10
seconds, so a minute of silence means something broke. Maple Valley updates
every 15 minutes, so a minute of silence is normal.

| Channel | Expected interval | Stale after |
| --- | --- | --- |
| Mark's Office | 10 s | 60 s |
| Maple Valley | 15 min | 45 min |

A stale reading **dims and says why** rather than showing a frozen number that
looks live. This is carried over from the reference project, where it earned its
place: a stale number indistinguishable from a live one is worse than an obvious
gap.

### 4.2 Keep every sample; downsample only when drawing

Twelve hours of office samples is about 52 KB against 8 MB of PSRAM. Memory is
not a constraint, so there is no reason to pre-average on the way in.

| Channel | Samples in 12 h | Bytes |
| --- | --- | --- |
| Mark's Office | ~4,320 | ~52 KB |
| Maple Valley | ~48 | ~0.6 KB |

Retention stays a plain ring buffer and downsampling stays a pure function of
the buffer, which is what makes both exhaustively testable on the host. Chart
buckets are computed at draw time into roughly 380 columns.

The Maple Valley trace will legitimately look like a step function at 48 points
across 12 hours. That is the data, not a bug, and the chart should not
interpolate it into a smoothness it does not have.

### 4.3 Design around the display DMA bug from the start

Elecrow's own issue tracker documents that blocking GT911 reads on the shared
I2C bus starve the RGB panel's DMA and produce visible display shake. That bus
also carries the real-time clock and the panel's companion microcontroller, so
this is an architectural constraint rather than a touch-driver detail:

- poll touch on a fixed 20 ms interval, never per-loop
- keep every I2C transaction short
- yield in the main loop
- allocate LVGL's buffers in DMA-capable PSRAM

### 4.4 Celsius internally, Fahrenheit on screen

History always stores Celsius; conversion happens at display time. Both sources
can supply either, so the choice is about having one representation internally
rather than about either source. Fahrenheit is the default on screen.

### 4.5 Backlight probing, because the revision is unknown

**This board's backlight is not on a GPIO.** It is an I2C command to a companion
microcontroller at 0x30, and the encoding inverted between board revisions:

| Revision | Backlight | Touch activation |
| --- | --- | --- |
| V1.2 | `0x10` on, `0x05` off | `0x19` |
| V1.3+ | `0` brightest, `245` off | `250` |

Getting it wrong gives a black screen with no error and a correctly scanning
panel behind it.

**Revised during planning.** The original intent was for `panel_mcu` to try the
V1.3+ encoding, fall back to V1.2, and report which worked. That cannot be made
to work: the companion microcontroller **acknowledges any byte on the I2C bus**,
so a write cannot report whether it was understood. A probe would simply send a
V1.3+ brightness command to a V1.2 board, which would read it as something else
entirely.

Instead the revision is a compile-time constant, `CROWPANEL_ADVANCE_REV`,
defaulting to V1.3+ because that is current stock. At startup `panel_mcu` logs
which revision it is driving and what to change if the screen stays dark. Once
the silkscreen has been read, the constant is set once and never thought about
again.

This is less clever than probing and considerably more honest about what the
hardware can tell us.

---

## 5. Error handling

| Failure | Behaviour |
| --- | --- |
| Wi-Fi down at boot | Retry with backoff. Panel comes up and shows "no wifi"; it never blocks on the network. |
| Wi-Fi drops later | Reconnect in the background. Channels go stale on their own timers and say so. |
| Broker unreachable | Status bar shows the MQTT state. Reconnect with backoff. The weather channel is unaffected. |
| Open-Meteo unreachable or malformed | Keep the last good reading until its staleness threshold, then dim it. Never display a partially parsed value. |
| Malformed MQTT payload | Reject the sample, count it, leave history untouched. A bad parse must not enter the chart. |
| Display init fails | Fatal and logged. Almost always PSRAM configuration. |
| Touch unavailable | Not fatal. The display is useful without it. |

The governing rule, inherited from the reference project: **never show a number
that looks live when it is not.**

---

## 6. Testing

### On the host, now, with `pio test -e native`

- Ring buffer retention, wraparound, and a capacity of zero
- Downsampling into buckets, including empty and sparse windows
- The sparse case specifically: 48 points across 380 columns
- Staleness transitions at each channel's threshold
- MQTT payload parsing, including malformed and out-of-range input
- Open-Meteo JSON extraction, including missing fields and truncated responses

### Only when the board arrives

Panel bring-up, touch, and the backlight command cannot be tested at all before
then. `docs/HARDWARE.md` section 8 is the ordered checklist, beginning with
reading the board revision off the silkscreen, since the backlight encoding, the
microphone type, the pixel clock and the free pins all depend on it.

---

## 7. Build configuration

Per `docs/HARDWARE.md` section 5. The values that differ from the reference
project and would silently break things if carried over:

| Setting | Value | Note |
| --- | --- | --- |
| Flash | **16 MB** | The reference board is 4 MB. This inverts. |
| PSRAM | 8 MB octal, `qio_opi` | Start at 80 MHz |
| `ARDUINO_USB_CDC_ON_BOOT` | **0** | Same as the reference, same reason: no native USB |
| Upload speed | 921600 | The reference board fails at this |
| Partitions | Huge APP | The default is too small |
| Pixel clock | 16 MHz on V1.3+, 21 MHz on V1.0 and V1.2 | Revision-dependent |

Libraries: LovyanGFX 1.2.x, LVGL 9.1.0, PubSubClient, ArduinoJson 7.

Secrets live in `include/secrets.h`, which is gitignored, with a committed
template. Wi-Fi credentials, the broker address and the coordinates go there.

---

## 8. Known risks

1. **The board revision is unknown** until it arrives, and it determines the
   backlight encoding, the touch activation command and the pixel clock.
2. **The module may not be an N16R8.** Elecrow's schematic labels it so but their
   own notes decline to guarantee it. A wrong flash size boot loops.
3. **PSRAM speed guidance is contradictory** between the vendor and the
   community, and appears to be revision-dependent.
4. **Nothing in the hardware reference has been verified against hardware.**
   Every value is sourced and cited, and none is confirmed.

---

## Authorship

Mark Castelluccio <markacastelluccio@gmail.com>

Designed with Claude Code (Anthropic Claude Opus 5). The MQTT topic scheme and
payload format were read from the live broker; the weather endpoint was
confirmed against a real response; hardware values come from
[HARDWARE.md](../../HARDWARE.md) and are cited there.
