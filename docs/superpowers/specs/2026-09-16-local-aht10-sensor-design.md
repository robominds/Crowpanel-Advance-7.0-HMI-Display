# A Local AHT10 Sensor, Published to MQTT — Design

Author: Mark Castelluccio <markacastelluccio@gmail.com>
Written with assistance from Claude Code (Anthropic Claude Opus 5).

## Goal

A third panel sits in the master bedroom with an AHT10 temperature and humidity
sensor on the `I2C-OUT` connector. That panel must read the sensor directly,
show it as the indoor row, and publish it to the house MQTT broker. A panel with
no sensor keeps today's behaviour: it subscribes to the topics `secrets.ini`
names.

The same firmware serves every panel. Which role a panel takes is decided at
boot by whether a sensor answers, not by a build flag.

## Decided with the user (2026-09-16)

| Decision | Choice |
| --- | --- |
| Indoor row on a sensor panel | The local sensor, directly. MQTT is publish-only |
| Publish cadence | Every 10 s, one publish per sample, QoS 0, not retained |
| Retained messages | No. The panel is a client of the house broker and has no direct subscribers |
| Sensor lost after boot | Keep retrying, publish nothing, let the row go stale and dim |
| Subscribe on a sensor panel | No. Publish only, so there is no feedback loop |
| Rollout | The bedroom panel only, at 2.3.0, while it is being tested. The office and kitchen panels stay on 2.2.0 until the user asks |

## The sensor

AHT10 on the `I2C-OUT` header (J13: GND, 3V3, GPIO15 SDA, GPIO16 SCL), address
**0x38**, which collides with nothing already on the bus — companion MCU 0x30,
PCF8563 0x51, GT911 touch 0x5D.

Protocol, as the datasheet gives it:

| Step | Bytes | Wait |
| --- | --- | --- |
| Soft reset | `0xBA` | 20 ms |
| Initialise / calibrate | `0xE1 0x08 0x00` | 10 ms |
| Status read | read 1 byte | — |
| Trigger measurement | `0xAC 0x33 0x00` | ≥ 80 ms |
| Collect | read 6 bytes | — |

Status bit 3 (`0x08`) is the calibrated flag; bit 7 (`0x80`) means busy. The six
bytes are status, then a 20-bit humidity and a 20-bit temperature sharing a
nibble:

```
hum_raw  = (b1 << 12) | (b2 << 4) | (b3 >> 4)
temp_raw = ((b3 & 0x0F) << 16) | (b4 << 8) | b5
RH  = hum_raw  * 100 / 2^20
T_C = temp_raw * 200 / 2^20 - 50
```

The AHT10 sends no CRC byte; that is an AHT20 feature. Reads are validated by
range instead: a sample is rejected when the raw fields are all zeros or all
ones, when the temperature falls outside −40 to 85 °C, or when the humidity
falls outside 0 to 100 %RH.

**This is the touch controller's bus.** Blocking it starves the RGB panel's DMA
and shifts the picture, which is a documented fault on this board. The driver
therefore never waits on the bus: the trigger and the collect are two short
transactions separated by a `poll()` return, not a delay.

## Architecture

| File | Responsibility |
| --- | --- |
| `lib/aht10/` (new) | Pure conversion and validation: 6 raw bytes → °C and %RH, no Arduino or Wire includes, host-tested like `lib/parse` |
| `test/test_aht10/` (new) | Host tests: datasheet vectors, a negative temperature, 0 % and 100 % humidity, the all-zero and all-ones disconnected patterns |
| `src/source_aht10.{h,cpp}` (new) | The I2C state machine and the probe |
| `src/source_mqtt.{h,cpp}` | Gains a mode and a `publish()` |
| `src/main.cpp` | Probes once, picks the mode, feeds channel and publish from one sample |

### `source_aht10`

```cpp
namespace source_aht10 {
// Probes 0x38: soft reset, initialise, read status, require the calibrated bit.
// Call after Wire.begin(). Returns whether a sensor answered.
bool begin();

// Call every loop. Triggers a measurement every SAMPLE_INTERVAL_MS and collects
// it on a later call, at least 80 ms after the trigger. Returns true on the one
// call where a fresh, valid sample became available.
bool poll(float& temperature_c, float& humidity_pct);

bool present();
uint32_t failedReads();
}
```

Three states: idle until the next sample is due, triggered and waiting for the
conversion, then collect. A failed read returns to idle and counts; the cycle
retries on the next interval, so a sensor unplugged at runtime simply stops
producing samples.

### `source_mqtt`

```cpp
enum class Mode { Subscribe, PublishOnly };
void begin(channel::Channel& indoor, Mode mode);
// PublishOnly: writes the two topics as bare decimals with one decimal place.
// A no-op while disconnected; nothing is queued.
void publish(float temperature_c, float humidity_pct);
```

`Subscribe` is exactly today's path, including the rule that a temperature is
accepted only once a humidity has been seen. `PublishOnly` connects with the
same client id, 30 s keepalive and backoff, and never subscribes.

### `main.cpp`

```cpp
const bool local_sensor = source_aht10::begin();
source_mqtt::begin(*g_channels[1], local_sensor ? source_mqtt::Mode::PublishOnly
                                                : source_mqtt::Mode::Subscribe);
```

and in `loop()`, after `source_mqtt::poll()`:

```cpp
float c = 0.0f, rh = 0.0f;
if (source_aht10::poll(c, rh)) {
    g_channels[1]->update(c, rh, millis());
    source_mqtt::publish(c, rh);
}
```

One sample, one channel update, one pair of publishes. The chart, the clock
view, the staleness dimming and the OTA overlay are untouched.

## Configuration

No new `secrets.ini` keys. On a sensor panel `mqtt_topic_temp` and
`mqtt_topic_hum` are the topics it *writes*; elsewhere they stay the topics it
reads. The bedroom panel is therefore only a `secrets.ini`:

```ini
indoor_label      = "MasterBedroom"
mqtt_topic_temp   = "MasterBedroom/AHT10/tempc"
mqtt_topic_hum    = "MasterBedroom/AHT10/hum"
device_host       = "MasterBedroom-Display"
```

## Logging

- `aht10: found at 0x38, publishing <temp topic>` or `aht10: no sensor at 0x38, reading the broker instead`
- `aht10: read failed (%lu total)`, rate-limited to one line a minute
- `mqtt: published %.1f C, %.1f %%` at most once per sample

## Behaviour summary

| Situation | Indoor row | MQTT |
| --- | --- | --- |
| Sensor present, broker up | Local reading, updated every 10 s | Publishes both topics every 10 s |
| Sensor present, broker down | Local reading, unaffected | Publishes resume on reconnect; nothing queued |
| Sensor absent | Whatever the broker sends, as today | Subscribes to both topics |
| Sensor stops answering | Dims after 60 s and shows `stale` on the chart view | Publishes stop |

## Verification

- Host: `pio test -e native`, including the new `test_aht10` suite.
- Build: `advance_70`, `advance_70_ota`, `diag`, `native`; image under 6,553,600 bytes; exactly one ` T verifyRollbackLater`; no new warnings.
- Hardware, on the bedroom panel, each step with the user's go:
  1. USB flash 2.3.0 — it also writes the two-slot partition table, so the first flash cannot be over the air. Expect `aht10: found at 0x38` and an indoor row showing the room's real temperature within about 10 s.
  2. Watch the broker from the Mac: both topics arriving about every 10 s, bare decimals, values tracking the panel's display.
  3. Broker independence, free of charge at boot: the row must show a real reading before `wifi: up` appears in the log, because the sensor path does not wait for the network. Confirm from the serial capture that the first sample precedes the first successful MQTT connection.
  4. Sensor-absent path: with the user's agreement, unplug the AHT10 from J13 and reboot. Expect `aht10: no sensor at 0x38`, the subscribe path, and — because nothing else publishes those topics — a dimmed `--` row. Re-plug and reboot to return.
  5. Sensor-lost path: unplug the sensor while running. Expect publishes to stop and the row to dim after 60 s, with no crash and no reboot.
- The office and kitchen panels are not touched; they stay on 2.2.0.

## Out of scope

Reading more than one local sensor, AHT20's CRC, retained publishes, Home
Assistant discovery, and any change to the chart or clock layout.
