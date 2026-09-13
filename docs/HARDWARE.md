# Elecrow CrowPanel Advance 7.0-HMI — Hardware Reference

**SKU:** `DIS02170A` (bare board), `DIS02170A-1` (acrylic-case bundle)
**Marketing name:** CrowPanel Advance 7inch HMI — ESP32-S3 AI-Powered IPS Touch
Screen, 800x480, Support LVGL
**Amazon listing:** ASIN `B0FX5447WD`, titled "ELECROW 7\" ESP32 Display 800x480,
HMI IPS AI Touch Screen with Acrylic Case ... (Advanced)"
**Board revisions:** V1.0, V1.2, V1.3, V1.4, V1.5 — this matters more than on most
boards, see section 4

> **A unit was brought up on 2026-09-12** — connected, flashed, and run, over
> the board's own USB-C with esptool v5.3.0. That confirmed the module is an
> N16R8, the board is revision V1.3 or later, touch and the panel come up as
> documented, and serial and upload work as predicted. It also turned up one
> new finding not in any vendor source: macOS needs a driver Apple does not
> ship before it will even see the board. Section 8 is now a record of what
> that bring-up checked and what it showed; section 9 lists what is still
> open. Everything not confirmed there still comes from Elecrow's committed
> source, their Eagle schematics, or their wiki, cited as before, with values
> read verbatim out of a source file quoted as code.

This document is a consolidated, source-cited hardware reference intended to be
reusable across any firmware project targeting this board. It records not only
what the hardware is, but where the vendor documentation is wrong and where the
vendor contradicts itself, which is where most of the time on this board gets
lost.

---

## 0. This is not the CrowPanel 7.0 HMI, and almost nothing carries over

Elecrow sell two 7-inch 800x480 ESP32-S3 panels whose names differ by one word.
They share a resolution and very little else that matters to firmware.

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

**Nearly every hard-won lesson from the older board inverts or relocates.** The
flash size, the I2C pins, the backlight mechanism, the porches and the free pin
are all different. Copying a working `board_pins.h` forward produces a board that
flashes cleanly and shows nothing.

Two lessons do carry over intact, both for the same underlying reason — neither
board has native USB:

- `ARDUINO_USB_CDC_ON_BOOT` must be **off**. See section 2.5.
- The vendor's PlatformIO board file carries Espressif native-USB hardware IDs it
  should not, breaking port auto-detection. Elecrow made the identical mistake
  twice.

---

## 1. Processor and memory

| Item | Value |
| --- | --- |
| MCU module | **ESP32-S3-WROOM-1-N16R8** |
| Core | Xtensa **LX7** dual-core @ 240 MHz |
| SRAM | 512 KB internal |
| PSRAM | **8 MB, Octal SPI (OPI)** — 80 MHz stock, 120 MHz with a patched toolchain |
| Flash | **16 MB, quad SPI (QIO) @ 80 MHz** |
| ROM | 384 KB |
| Panel | 7.0" **IPS** TFT-LCD, 800x480, driver **SC7277**, glass `LI070DE42Y` |
| Panel interface | 16-bit RGB565 parallel over the S3's LCD_CAM peripheral |
| Touch | GT911 capacitive, 5-point |
| Backlight boost | MT9201, driving `LCD_LEDA` / `LCD_LEDK` |
| Panel spec | 178° viewing angle, 400 cd/m², active area 156 x 87 mm |
| Board | 181.26 x 108.36 x 16 mm, −20 to 70 °C |

The `N16R8` suffix is the load-bearing detail: **16 MB flash, 8 MB octal PSRAM.**
The memory type is `qio_opi` — **quad** flash, **octal** PSRAM. Same memory
*type* as the older board, four times the flash.

> **Read this if you have used the older CrowPanel 7.0.** That board's single
> most expensive gotcha was that 16 MB boot loops and you must set 4 MB. **Here
> the opposite is true.** Elecrow's committed PlatformIO board definition sets
> `"flash_size": "16MB"` with `"maximum_size": 16777216`, their Arduino menu
> screenshot shows `Flash Size: "16MB (128Mb)"`, and all six of their
> per-memory-mode sdkconfigs declare `CONFIG_ESPTOOLPY_FLASHSIZE_16MB`.

### How firmly is N16R8 established

Well attested, but with a caveat Elecrow publish themselves. `N16R8` appears in
the repository readme spec table, as a text label on the V1.4 and V1.5 schematic
sheets (`ESP32-S3-N16R8`, part `U5`), and in the schematic PDF. No `N8R8`,
`N4R8` or `N16R8V` string appears anywhere in the repository.

However, **every one of those is a text annotation, not a part number.** The
Eagle *deviceset* is plain `ESP32-S3-WROOM-1` with no memory suffix, because the
Eagle library does not distinguish memory capacities. There is no BOM file
anywhere in the repository, and no datasheet filename names the variant.

A file in the repository states the caveat plainly, advising that N16R8 "is
likely" the configuration but that "the laser marking on the physical module and
the Arduino build menu must take precedence."

> **Do not cite that file as a vendor specification.** Its own header reads
> "Author: OpenAI Codex (cross-compiled from project materials)" and it is dated
> after the hardware shipped. It is a derived document that happens to be
> correct where it has been checked against the schematic nets and the example
> code. The caveat it raises stands on its own merits — the deviceset really
> does lack a memory suffix — but the warning is not Elecrow engineering
> speaking.

**Settled now, from the silicon.** `esptool.py` v5.3.0, talking to the chip
over its own USB-C on 2026-09-12, reports:

```
Chip type:          ESP32-S3 (QFN56) (revision v0.2)
Features:           Wi-Fi, BT 5 (LE), Dual Core + LP Core, 240MHz, Embedded PSRAM 8MB (AP_3v3)
Crystal frequency:  40MHz
MAC:                a4:cb:8f:c0:73:c8
Manufacturer: 46
Device: 4018
Detected flash size: 16MB
Flash type set in eFuse: quad (4 data lines)
Flash voltage set by eFuse: 3.3V
```

16 MB flash, 8 MB embedded PSRAM — N16R8, read from the part itself rather
than a schematic annotation. This is exactly the kind of evidence the Eagle
deviceset could never provide, and it settles the question the caveat above
raises. The laser marking on the module shield was not read; it was not
needed.

### PSRAM speed: the flicker question, and it is genuinely unsettled

Elecrow ship a folder named `ESP32S3_120M/` which is **not** a board package. It
is a drop-in replacement for the Arduino core's precompiled ESP-IDF libraries,
declaring itself `framework-arduinoespressif32-libs` version
`5.1.4+sha.bd2b9390ef`. You copy it over
`Arduino15/packages/esp32/tools/esp32-arduino-libs/idf-release_v5.1-bd2b9390ef`.

> "ESP32S3_120M is a recompiled official file that has enabled PSRAM high-speed
> communication mode ... If not replaced, the maximum can only reach 80M, and
> after replacement, the maximum is 120M."

**This has been properly root-caused, in the vendor's own issue tracker.**
Repository issue #7, reproduced on a V1.4 board with a public reproducer:
the stock `esp32-arduino-libs` for core 3.0.2 builds PSRAM at 80 MHz, which
starves the RGB scanout DMA and produces roughly 10 ms display shake. The factory
firmware BIN was linked against libraries rebuilt at **120 MHz with
`CONFIG_SPIRAM_FETCH_INSTRUCTIONS=1`** — which is exactly what `ESP32S3_120M`
contains. So on V1.3+ hardware, 120 MHz is the fix rather than the risk.

**But the opposite is reported on V1.2.** The Home Assistant community thread
finds 120 MHz unreliable there and recommends 80 MHz, and Elecrow's own ESPHome
YAML uses `psram: {mode: octal, speed: 80MHz}`. This is most likely
revision-dependent, which would reconcile both reports.

**And there are two other causes of flicker on this board that are not PSRAM at
all**, so do not assume the clock is your problem:

- **Blocking GT911 I2C polling starves the RGB DMA** (issue #8). The fix is a
  20 ms touch poll interval, a `vTaskDelay(1)` in the loop, and LVGL buffers
  allocated in PSRAM DMA-capable segments. **This one is a firmware bug you can
  write yourself**, and it is the most likely flicker cause in a
  freshly-written application.
- **RGB timing** (forum discussion 28135). A V1.2 user fixed micro-jitter purely
  by raising PCLK to about 21 MHz with a strict initialisation order, explicitly
  ruling out PSRAM speed. Note that 21 MHz is Elecrow's own V1.2 default, so this
  reads as someone who had been running the V1.3 value on V1.2 hardware.

Practical advice: get the touch polling right first, use the revision's own
pixel clock, and treat PSRAM speed as the last variable to change.

---

## 2. Complete pin map

Source for this section unless noted: Elecrow's `LovyanGFX_Driver.h` under
`example/<revision>/.../lesson-03/BigInch_LVGL/`, read directly, and
cross-checked against net names in the V1.5 Eagle schematic.

### 2.1 Display — 16-bit RGB565 parallel

**Identical across every revision, V1.0 through V1.5.**

| Signal | GPIO | Schematic net |
| --- | --- | --- |
| R0 | 7 | `IO7_R3` |
| R1 | 17 | `IO17_R4` |
| R2 | 18 | `IO18_R5` |
| R3 | 3 | `IO3_R6` |
| R4 | 46 | `IO46_R7` |
| G0 | 9 | `IO9_G2` |
| G1 | 10 | `IO10_G3` |
| G2 | 11 | `IO11_G4` |
| G3 | 12 | `IO12_G5` |
| G4 | 13 | `IO13_G6` |
| G5 | 14 | `IO14_G7` |
| B0 | 21 | `IO21_B3` |
| B1 | 47 | `IO47_B4` |
| B2 | 48 | `IO48_B5` |
| B3 | 45 | `IO45_B6` |
| B4 | 38 | `IO38_B7` |
| DE | 42 | `IO42_DE` |
| VSYNC | 41 | `IO41_VSYNC` |
| HSYNC | 40 | `IO40_HSYNC` |
| PCLK | 39 | `IO39_CLK_DCLK` |

The driver source and the schematic agree pin for pin, which is the strongest
corroboration available short of a continuity check.

**On the net naming.** Elecrow name the nets in RGB888 bit positions, so the
lowest red line is `R3` and the highest `R7`. That is correct and worth
understanding rather than "fixing": a 16-bit bus feeding a 6-bit-per-channel
panel lands on R3–R7, G2–G7 and B3–B7. The panel's unused low bits are tied to
ground. The older CrowPanel's source made the opposite mistake, labelling the
same arrangement B0/G0/R0; here the labels are right, so do not renumber them.

`TFT_RESET` is derived from an RC and diode network, **not** a GPIO. There is no
panel reset line to drive.

> **Porting note: GPIO38 is a display data line here.** On the CrowPanel 7.0 it
> was `GPIO_D`, the one free pin, brought out for a sensor. Attaching anything to
> IO38 on this board corrupts the blue channel.

### 2.2 Display timing — the pixel clock is revision-dependent

Porches, polarities and PSRAM use are **identical in every revision**:

```
hsync_polarity = 0,  front_porch = 8,  pulse_width = 4,  back_porch = 8
vsync_polarity = 0,  front_porch = 8,  pulse_width = 4,  back_porch = 8
pclk_idle_high = 1                 // latch on the falling edge
use_psram      = 1
```

Both axes use the same 8 / 4 / 8 values, which looks like a copy-paste error and
is not. `pclk_active_neg` does not appear in any version of the file.

**The pixel clock differs, and Elecrow lowered it:**

| Source | `freq_write` |
| --- | --- |
| V1.0 `LovyanGFX_Driver.h` | **21 MHz** |
| V1.2 `LovyanGFX_Driver.h` | **21 MHz** |
| **V1.3 / V1.4 / V1.5 `LovyanGFX_Driver.h`** | **16 MHz** |
| V1.3+ ESP-IDF port | 16 MHz |
| V1.3+ `lesson-04`, the microSD example | **18 MHz**, and `use_psram = 2` |
| Elecrow's ESPHome YAML | 20 MHz |

Read verbatim from the three driver files. **Use the value for your revision.**
This single fact reconciles most of the conflicting advice in circulation: people
quoting 21 MHz are on V1.0 or V1.2 boards, people quoting 16 MHz are on V1.3+,
and the 18 MHz figure in circulation traces to a single Elecrow example,
`lesson-04`, which is the only file in the tree that also sets `use_psram = 2`.
Every other V1.3+ file uses 16 MHz and `use_psram = 1`.

The ESP-IDF port expresses the same clock edge differently: LovyanGFX has no
`pclk_active_neg` field and uses `pclk_idle_high = 1`, while the IDF port sets
`.flags.pclk_active_neg = 1`. Same intent, two APIs. The IDF port also sets
`fb_in_psram = 1`, `data_width = 16`, `bits_per_pixel = 16`,
`sram_trans_align = 4` and `psram_trans_align = 64`.

The ESPHome config additionally sets `color_order: RGB` and `invert_colors: true`.
If the panel comes up with inverted or swapped colours under LovyanGFX, those are
the first two settings to look at.

### 2.3 Backlight — there is no backlight GPIO

This is the biggest structural difference from the older board and the most
likely cause of a "my new board is dead" panic.

The backlight is an MT9201 constant-current boost that **no ESP32 pin touches.**
On V1.2+ it is driven by the STC8H1K28 companion MCU on nets `P3_5/LCD_BK_PWR`
and `P1_1/LED_BK_EN`. You turn it on by sending an **I2C command to address
0x30**. On V1.0 it is an I/O expander at 0x18 instead.

Elecrow's `LovyanGFX_Driver.h` has the `Light_PWM` block commented out entirely,
including `// cfg.pin_bl = GPIO_NUM_2;`. There is no LovyanGFX-managed backlight
on this board in any revision.

**The backlight is off after reset.** Initialise the panel perfectly, never send
that I2C command, and you get a black screen with no error, no serial complaint,
and a correctly scanning panel behind it.

**The command encoding inverted between V1.2 and V1.3**, which is documented on a
single Elecrow wiki page that contains both versions:

| Revision | How to control it |
| --- | --- |
| V1.0 | Expander at 0x18, IO1 high. On or off only, no dimming. |
| V1.2 | Write `0x10` for on (max), `0x05` for off. Range `0x05`–`0x10`. To dim, send `0x10` **first**, then a level. |
| V1.3, V1.4, V1.5 | Write one byte, **0 to 245**, where **0 is brightest**, 244 is dimmest and **245 is off**. |

The V1.3+ scale inverts every intuition about brightness values. Writing 255 is
out of range, not bright. Writing 0, which looks like "off", is full brightness.

The same STC8 handles other peripherals at the same address. **Writes are a
single bare byte with no register address.** These are the only commands that
appear anywhere in Elecrow's example code:

| Command | Effect | Revision | Evidence |
| --- | --- | --- | --- |
| `0` … `245` | Backlight, 0 brightest, 245 off | V1.3+ | code |
| `250` | Activate / recover the touch controller | **V1.3+** | code |
| `248` / `249` | Amplifier unmute / mute | V1.3+ | code |
| `2`, `3` | Audio path select | V1.3+ | code |
| `0x05` … `0x10` | Backlight, `0x10` brightest, `0x05` off | **V1.2** | code |
| `0x19` | Activate the touch controller | **V1.2** | code |
| `0x17` | Turn on the speaker | **V1.2** | code |
| `246` / `247` | Buzzer on / off | V1.3+ | **wiki only** |

> **The touch-activation command changed with the backlight encoding.** V1.2
> boards use `0x19`; V1.3 and later use `250`. Sending the wrong one leaves touch
> dead with no other symptom.
>
> **The buzzer commands are documented but never exercised.** No example in the
> repository sounds the buzzer, so 246 and 247 come from the wiki alone. Elecrow
> warn against probing undefined command bytes to discover the rest of the
> table, and since the STC8's firmware is undocumented and not reflashable, that
> is worth heeding.

### 2.4 Touch — GT911 over I2C

**Identical in every revision**, read verbatim from all three driver files:

| Item | Value |
| --- | --- |
| Controller | GT911 capacitive, 5-point |
| I2C address | **0x5D** — the source comments `// 0x5D , 0x14` |
| SDA | **GPIO15** |
| SCL | **GPIO16** |
| Bus speed | 400 kHz, `I2C_NUM_0` |
| `pin_int` | **−1** — Elecrow poll rather than use the interrupt |
| `pin_rst` | **−1** — reset is not an ESP32 GPIO |
| Connector | 6-pin 0.5 mm FPC |

The INT line is physically wired to **GPIO1** (net `IO1_TP_INT`) even though the
driver ignores it, and the application code does use it during startup. RST goes
to the STC8 (`P1_7/TP_RST`) on V1.2+, or the 0x18 expander on V1.0.

**The startup sequence exists to pin the I2C address.** The GT911 latches its
address from the state of its interrupt line during reset: low gives 0x5D, high
gives 0x14. Elecrow hold GPIO1 low across the reset pulse to force 0x5D, then
release it to an input:

- **V1.0** — drive GPIO1 low, pulse expander IO2 low then high, wait 100 ms,
  return GPIO1 to input. The low is held about 20 ms.
- **V1.2** — drive GPIO1 low, send command `0x19` so the STC8 performs the
  reset, hold **120 ms**, return GPIO1 to input.
- **V1.3+** — the same sequence, but the command is **`250`**, not `0x19`.

Elecrow wrap this in a retry loop that scans for both 0x30 and 0x5D and repeats
the reset until both answer, which is a sound pattern worth copying.

Probe both 0x5D and 0x14 anyway. It costs nothing and makes the failure announce
itself rather than presenting as "touch does not work". At least one community
project ships a `-DCROWPANEL_TOUCH_I2C_ADDR=0x14` build variant, so 0x14 boards
evidently exist.

**Poll touch no faster than every 20 ms.** Blocking I2C reads on this bus starve
the RGB panel's DMA and cause visible display shake — vendor issue #8.

### 2.5 I2C bus occupancy

One bus, `GPIO15` / `GPIO16`, shared by everything including the external
`I2C-OUT` header. Know what is on it before adding a device:

| Address | Device | Revision |
| --- | --- | --- |
| 0x18 | PCA9557 / TCA9534 I/O expander | **V1.0 only** |
| 0x30 | STC8H1K28 companion MCU | **V1.2+** |
| 0x51 | PCF8563 real-time clock | all |
| 0x5D | GT911 touch (alternate 0x14) | all |

> Elecrow's own V1.0 code addresses the 0x18 expander through **two different
> libraries** in different lessons — `TCA9534` in lessons 2 to 5, `PCA9557` in
> lesson 6. It is one chip at one address; the two parts are register-compatible
> and either driver works. Do not go looking for a second expander.
>
> One V1.2 ESP-IDF sketch still carries a stale `#define TCA9534_ADDR 0x18`
> while actually writing to 0x30. Dead define, not a second device.

### 2.6 USB and serial — CH340K, no native USB

**There is no native USB on this board.** The USB-C connector (`J1`) has its data
lines routed through R6/R7 to a **CH340K** bridge (`U1`) and nowhere else. There
is no path from the connector to the ESP32-S3's native USB pins. The bridge
drives UART0 on **GPIO43 / GPIO44**, and a `UMH3NTN` dual transistor (`U8`)
forms the auto-download circuit onto `ESP32_EN` and `IO0_BOOT`, so you never
touch the boot button.

GPIO19 and GPIO20 are the S3's native USB D− and D+ pins. Here they are consumed
by the CH486F analog mux feeding the microphone, the radio socket and UART1.
Native USB is therefore not merely unused, it is physically unavailable.

The consequence is the same as on the older board, for the same reason:

> **`USB CDC On Boot` must be Disabled** — `-D ARDUINO_USB_CDC_ON_BOOT=0`.
> Enable it and `Serial` retargets to a USB-CDC peripheral that does not exist
> here. You get a silent, permanently dead serial monitor and nothing anywhere to
> explain it.

With CDC disabled, `Serial` **is** UART0, so `Serial.print` reaches the USB-C
port through the CH340K. Elecrow set this correctly in both places: their Arduino
menu screenshot shows `USB CDC On Boot: "Disabled"`, and their PlatformIO board
JSON carries `-D ARDUINO_USB_CDC_ON_BOOT=0` with `-D ARDUINO_USB_MODE=0`.

> Two community `platformio.ini` files in circulation set
> `-DARDUINO_USB_CDC_ON_BOOT=1`. That contradicts the hardware. Ignore them.

Do not write `while (!Serial) {}` in `setup()`. UART0 is always ready, so the
condition is meaningless, and on a board with no CDC it waits forever.

**Upload at 921600 is fine here**, per both Elecrow's board JSON (`"speed": 921600`)
and their Arduino menu screenshot. The CH340K sustains it. This inverts the older
board, whose CH340C fails at 921600 and needs 460800. If uploads prove flaky,
step down to 460800 then 115200.

Monitor at 115200. Elecrow's `platformio.ini` sets `monitor_speed = 115200` and
their examples use `Serial.begin(115200)`, though lesson-01 inconsistently uses
9600.

**macOS does not recognise this board out of the box, and the failure looks
like a dead board.** Confirmed on macOS 26.6.2 during bring-up. The CH340K
enumerates as USB vendor `0x1a86`, product **`0x7522`**. Apple's built-in
serial driver, `/System/Library/DriverExtensions/com.apple.DriverKit-AppleUSBCHCOM.dext`,
matches only `1a86:7523` and `1a86:55d4` — one digit off — so macOS sees the
device on the bus but binds no serial driver and creates no `/dev/cu.*` node.
`pio device list` shows nothing, and there is no error to explain why.

The fix is WCH's own DriverKit driver, `CH34xVCPDriver`
(<https://github.com/WCHSoftGroup/ch34xser_macos>, also on the Mac App Store),
then approving it under System Settings → General → Login Items & Extensions →
Driver Extensions. Until approved, `systemextensionsctl list` shows it as
`[activated waiting for user]` — flashing will not work until that changes.

> **Do not install the older kernel-extension version of this driver.** It is
> documented elsewhere as causing kernel panics. And do not install two CH34x
> drivers at once — that produces two serial ports, one of which is dead,
> which is its own hour of confusion on top of the first one.

### 2.7 The STC8H1K28 companion MCU — read this before blaming your code

From V1.2 onward there is a **second microcontroller on the board**, an
STC8H1K28-36I in LQFP32, acting as an I2C slave at **0x30**. It owns things you
would expect to be GPIOs:

- backlight boost enable and PWM level (`P1.1`, `P3.5`)
- the GT911 reset line (`P1.7`)
- the buzzer (`P2.7`)
- the NS4168 amplifier's mute and shutdown (`P3.7`, `P3.6` / `NS_CTRL`)
- the battery charger status lines (`P1.4/CHRG`, `P1.3/DONE`)

**Several peripherals are therefore inert until you send an I2C command**, with
no symptom beyond silence or a dark screen. Charge status is *only* readable over
I2C; there is no GPIO for it.

> **It is a black box.** Elecrow publish no source and no register specification
> for the STC8, its behaviour changed between V1.2 and V1.3, and the firmware
> inside it is not reflashable through the ESP32. Repository issue #4 asks for
> documentation and is open and unanswered. A 4-pin header (`J14`, `HC-PZ200`)
> exposes the STC8's own `P3.1` TXD and `P3.0` RXD for programming it directly.

On V1.0 this role is played by the 0x18 expander with far less capability: on/off
backlight, no PWM, no buzzer, no charger status.

### 2.8 Audio, microSD, and the hardware multiplexer

Three subsystems share one set of pins through **two CH486F analog switches**
(`U11` for GPIO4/5/6, `U9` for GPIO19/20), selected by the **S0 / S1 DIP
switches** (`K1`, a `DSHP02TS-S`, with 10 K pulldowns; closed reads as 1). This
is a hardware mux. No firmware makes all three work at once.

| S1 | S0 | Selected |
| --- | --- | --- |
| 0 | 0 | Microphone and speaker |
| 0 | 1 | Wireless module socket, **and `UART1-OUT`** |
| 1 | 0 | microSD — V1.0 and V1.2 only |
| 1 | 1 | Microphone and microSD — V1.2+, and the **only** microSD option on V1.3+ |

On V1.3+ the mux's channel-2 inputs are physically unconnected, which is why the
`1 0` row vanishes from Elecrow's V1.3 documentation. Elecrow support confirm
plainly that "the audio output and TF card usage cannot be used at the same
time."

**Speaker — NS4168 class-D amplifier (`U10`), all revisions.**

| Signal | GPIO |
| --- | --- |
| I2S SDIN | 4 |
| I2S BCLK | 5 |
| I2S LRCLK | 6 |

Mono, wired to the schematic's "Right channel" block, out to a 2-pin PH2.0
connector (`J15`). **Mute and shutdown are not on an ESP32 GPIO** — they are
expander IO3/IO4 on V1.0, STC8 `P3.7`/`P3.6` on V1.2+. The amplifier is muted
until you say otherwise.

**Microphone — and the part changed at V1.3.**

| Revision | Part | Interface | Pins |
| --- | --- | --- | --- |
| V1.0, V1.2 | **INMP441** | I2S, 3-wire | SCK **19**, SD **20**, **WS GPIO2** |
| V1.3, V1.4, V1.5 | **LMD3526B261-OFA01** | **PDM**, 2-wire | CLK **19**, DATA **20** |

This matters twice over. An I2S microphone driver will not read a PDM part, and
**the move to PDM is what freed GPIO2** on V1.3+ boards. Elecrow's own V1.3+ code
uses `audio.setPinsPdmRx(19, 20)` with `I2S_MODE_PDM_RX`, mono.

> Elecrow's wiki still labels the V1.3 row "I2S MIC" while listing only two pins
> and shipping PDM sample code. It is PDM. The widely-copied `MIC_WS(IO2)` line
> is V1.0/V1.2 only.

**microSD — SPI, not SDMMC.**

| Signal | GPIO |
| --- | --- |
| MOSI / DI | 6 |
| MISO / DO | 4 |
| SCK | 5 |
| **CS** | **tied to ground** (R33 10 K to GND; the 3V3 pull-up R29 is not fitted) |

Elecrow's code says it outright: `#define SD_CS 0 //The chip selector pin is not
connected to IO`. The schematic agrees — net `SDCS` reaches only R29 and R33 and
never an ESP32 pin. Card selection is handled by the switch network, not by
firmware. Runs at 40 MHz, SPI only, no SDMMC.

> **That `0` is an API placeholder, not a pin.** The SD library requires a
> number. Do not configure or drive GPIO0, which is the boot strapping pin and
> part of the auto-download circuit.

> The wiki's claim that CS is `3.3V` is wrong, and the readme's claim that CS is
> GPIO7 is worse — GPIO7 is a red data line. See section 7.

### 2.9 Real-time clock, battery, buzzer, radio

- **RTC: `U4`.** The Eagle deviceset is named `BM8563EMA`, but the fitted value
  on the V1.3 and V1.5 schematics is **`PCF8563MDTR`**. Both names circulate
  because both are true, for different revisions; the parts are
  register-compatible, so either driver works. Address **0x51** — but note that
this comes from the PCF8563 datasheet, not from Elecrow, since **no sketch in the
repository ever touches the RTC** and the schematic shows no address pins. A
  32.768 kHz crystal (`Y1`) and a **CR1220** backup cell (`BT1`) through a
  BAT54C. **The RTC's `#INT` is not wired to the ESP32.**
  No Elecrow sketch uses the RTC at all, though a `I2C_BM8563_RTC` library is
  bundled unused in the examples folder.
- **Battery: `U2` = TP4059** charger, connector `J3` = PH2.0 2-pin, 3.7–4.2 V,
  with a charge LED. Status only via the STC8 over I2C.
- **Buzzer:** passive. **GPIO8 on V1.0.** From V1.2 it moved to the STC8's `P2.7`
  and is no longer on any ESP32 pin.
- **Radio socket:** two 1x7 headers (`J9`, `J11`) accepting an ESP32-H2,
  ESP32-C6, nRF24L01+ or SX1262. Requires S1=0, S0=1, which costs you the
  microphone, the speaker and the microSD card.

### 2.10 Strapping pins carrying application functions

**GPIO3, GPIO45, GPIO46 and GPIO48 are ESP32-S3 strapping pins, and all four
carry RGB data here** (R3, B3, R4, B2). Normal for an RGB panel design, with one
consequence:

> Any external pull-up or pull-down on those lines changes the strapping state at
> reset and the board will not boot. Do not attach anything to them.

GPIO0 is the boot strapping pin and, unlike the older CrowPanel, is **not** the
pixel clock here — PCLK is GPIO39. GPIO0 drives the auto-download circuit.

### 2.11 Reserved and unusable

GPIO26–32 are the module's SPI flash. **GPIO33–37 are consumed by the octal
PSRAM** on this R8 module. GPIO22–25 do not exist on the ESP32-S3. None are
available under any configuration.

---

## 3. Expansion connectors and free GPIO

Verified from the V1.3 and V1.5 schematics. Net membership is certain; **absolute
pin-1 orientation on the 4-pin headers should be checked against the silkscreen
before wiring.**

| Silkscreen | Ref | Connector | Nets |
| --- | --- | --- | --- |
| `UART0-OUT` | J2 | HY2.0-4P | GND, 3V3, GPIO43 TX, GPIO44 RX |
| `UART0-IN` | J10 | **XH2.54-4P** | GND, **+5 V input**, level-shifted TXD0/RXD0 |
| `UART1-OUT` | J12 | HY2.0-4P | GND, 3V3, GPIO20 TX, GPIO19 RX — **only when S1=0, S0=1** |
| `I2C-OUT` | J13 | HY2.0-4P | GND, 3V3, **GPIO15 SDA, GPIO16 SCL** |
| Speaker | J15 | PH2.0-2P | ROUT+, ROUT− |
| Battery | J3 | PH2.0-2P | VBAT, GND |
| STC8 programming | J14 | 1x4 | STC8 P3.1 TXD, P3.0 RXD, VIN, GND |

3V3 outputs are rated 1 A total; the 5 V input on `UART0-IN` accepts 2 A.

### The wireless-module socket, J9 and J11

| J9 | Net | | J11 | Net |
| --- | --- | --- | --- | --- |
| 1 | GPIO20, via 0R | | 1 | GPIO19, via 0R |
| 2 | GPIO5, muxed | | 2 | GPIO16 SCL |
| 3 | GPIO4, muxed | | 3 | GPIO15 SDA |
| 4 | GPIO6, muxed | | 4 | *unconnected* |
| 5 | 3V3 | | 5 | **GPIO2** (V1.3+) |
| 6 | GND | | 6 | **GPIO8** (V1.3+) |
| 7 | 5 V, via L3, not fitted | | 7 | *unconnected* |

### What is actually free

- **GPIO2 and GPIO8 are genuinely free on V1.3 and later.** They go only to J11
  pins 5 and 6 and touch no onboard peripheral. On V1.0 and V1.2 they are not
  free: GPIO2 is the I2S microphone's word-select and GPIO8 is the buzzer.
  **These are the only two uncommitted signal pins on the board.**
- **GPIO4, GPIO5, GPIO6, GPIO19, GPIO20** are conditionally usable depending on
  S0/S1, but always sit behind the analog switches. Elecrow's own demos
  repurpose GPIO19 as a plain output for an LED.
- **GPIO1** is the touch interrupt. It is wired, and the startup sequence uses
  it, even though the driver polls.
- Everything else is the RGB bus (16), DE/VSYNC/HSYNC/PCLK (4), I2C (2),
  UART0 (2), BOOT, flash and PSRAM.

**`I2C-OUT` is the practical expansion port.** It is a real, already-pulled-up
I2C bus with three known occupants at 0x30, 0x51 and 0x5D. Anything with a
non-colliding address drops straight on. Extend that bus; do not repurpose it.

---

## 4. Board revisions

Five exist, and the differences are not cosmetic. **Running V1.0 code on a V1.2+
board gives you a black screen**, which is the most commonly reported problem
with this panel.

| Revision | What changed |
| --- | --- |
| V1.0 | I/O expander at 0x18 (addressed as either PCA9557 or TCA9534). Backlight on/off only. Buzzer on GPIO8. INMP441 I2S microphone using GPIO2 as word-select. |
| V1.2 | Expander **replaced by the STC8H1K28** at 0x30. Backlight gains PWM, encoded `0x05`–`0x10`. Buzzer, amplifier control and charger status move to the STC8. microSD gains the S1=1, S0=1 mode. |
| V1.3 | **Backlight encoding inverts** to 0–245 with 0 brightest. **Microphone changes to PDM**, freeing GPIO2. **Pixel clock lowered from 21 MHz to 16 MHz.** microSD only in the S1=1, S0=1 mode. Touch activation changes from `0x19` to `250`. Amplifier mute gains commands 248 and 249. |
| V1.4 | Button change only. Firmware-identical to V1.3. |
| V1.5 | Touch FPC connector package only. Current revision. |

V1.2 to V1.3 is a **firmware change inside the STC8** and is not reflashable, so
a V1.2 board cannot be made to behave like a V1.3 one.

A board bought new in 2026 is almost certainly V1.4 or V1.5. Confirm from the
silkscreen before writing a line of code.

> **Most third-party pinouts describe V1.0 and are wrong for current stock.**
> The espboards.dev pinout, for instance, shows `IO8 → BUZZER`, which has not
> been true since V1.2.

---

## 5. Build configuration

### 5.1 Arduino IDE

Elecrow document this only as a screenshot of the Tools menu. Read off that
image, every value literal:

| Setting | Value |
| --- | --- |
| Board | ESP32S3 Dev Module |
| Flash Size | **16MB (128Mb)** |
| Flash Mode | QIO 80MHz |
| PSRAM | **OPI PSRAM** |
| Partition Scheme | **Huge APP (3MB No OTA/1MB SPIFFS)** |
| **USB CDC On Boot** | **Disabled** |
| Upload Speed | 921600 |
| Upload Mode | UART0 / Hardware CDC |
| USB Mode | Hardware CDC and JTAG |
| CPU Frequency | 240MHz (WiFi) |
| Core Debug Level | None |
| Events Run On / Arduino Runs On | Core 1 |

Elecrow pin the **ESP32 Arduino core to 3.0.2** and repeat "All subsequent
examples run on ESP32 version 3.0.2" in every lesson readme. They also require
the `ESP32S3_120M` library replacement described in section 1.

> **Use Huge APP, not the default partition scheme.** Elecrow's own factory demo
> is 4.0 MB against a 3.1 MB default limit and will not fit — repository issue #2.
> Third-party guides saying "leave the partition scheme at default" are wrong.

### 5.2 PlatformIO — Elecrow's committed configuration

```ini
[env:advance-hmi]
platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.39/platform-espressif32.zip
board = ESP32-S3-WROOM-1-N16R8
framework = arduino
monitor_speed = 115200
board_build.arduino.partitions = partitions.csv
board_upload.partitions = partitions.csv
build_flags =
    -DCONFIG_SPIRAM_SPEED_120M=1
    -DLV_CONF_INCLUDE_SIMPLE
    -I include
lib_deps =
    lvgl/lvgl@9.1.0
    adafruit/Adafruit BusIO@1.17.0
    adafruit/Adafruit SSD1306@2.5.13
    tamctec/TAMC_GT911@1.0.2
    robtillaart/TCA9554@0.1.1
    lovyan03/LovyanGFX@1.2.26
```

Their earlier V1.2 project used `Jason2866/platform-espressif32.git#Arduino/IDF53`
instead. Elecrow support insist on one of these forks rather than stock
`espressif32`, saying otherwise "screen flickering will occur".

Note the PlatformIO path uses **LVGL 9.1.0** while the Arduino path uses LVGL
8.3.x. These are not interchangeable; the LVGL 9 API differs substantially.

`board = ESP32-S3-WROOM-1-N16R8` is **not an upstream board id.** It resolves to
a JSON file Elecrow commit at `PlatformIO/boards/ESP32-S3-WROOM-1-N16R8.json`:

```json
{
  "build": {
    "arduino": { "memory_type": "qio_opi" },
    "extra_flags": [
      "-D ARDUINO_USB_CDC_ON_BOOT=0",
      "-D ARDUINO_USB_MODE=0",
      "-mfix-esp32-psram-cache-issue",
      "-DBOARD_HAS_PSRAM"
    ],
    "f_cpu": "240000000L",
    "f_flash": "80000000L",
    "flash_mode": "qio",
    "hwids": [["0x303A", "0x1001"]],
    "mcu": "esp32s3"
  },
  "upload": {
    "flash_size": "16MB",
    "maximum_ram_size": 8388608,
    "maximum_size": 16777216,
    "speed": 921600
  }
}
```

Either vendor that JSON into your project, or use the upstream board with
explicit overrides — which is what every community project does, and they all
converge on the same values:

```ini
board = esp32-s3-devkitc-1
board_build.arduino.memory_type = qio_opi
board_build.flash_mode          = qio
board_upload.flash_size         = 16MB
board_upload.maximum_size       = 16777216
board_build.partitions          = huge_app.csv
build_flags = -DBOARD_HAS_PSRAM
upload_speed = 921600
```

> **Their `hwids` are wrong.** `0x303A:0x1001` is Espressif's native-USB
> identifier. This board enumerates as a **CH340K**, so PlatformIO's port
> auto-detection will not find it and you must name the port explicitly. Elecrow
> made the identical mistake in the older CrowPanel's board file.

Their `partitions.csv` has **no OTA and no filesystem**:

```
nvs,      data, nvs,     0x9000,  0x6000,
phy_init, data, phy,     0xf000,  0x1000,
factory,  app,  factory, 0x10000, 4M,
```

That uses 4 MB of the 16 MB available. A project wanting OTA, a filesystem, or
simply more room should write its own table — 12 MB sits unused.

**No community source shows a clean way to apply the 120 MHz PSRAM patch under
PlatformIO.** It is an Arduino-IDE-shaped fix.

### 5.3 Three different partition layouts, not three settings

Elecrow's example trees each use a different table. They are separate projects,
not alternatives:

- **Arduino:** the stock `Huge APP` menu entry, 3 MB app / 1 MB SPIFFS.
- **PlatformIO:** custom, a single 4 MB factory app, nothing else.
- **ESP-IDF:** custom, 2 MB factory plus two 2 MB OTA slots plus 4 MB SPIFFS.
- **Speech recognition build:** a fourth again — two 3 MB OTA apps, 7 MB SPIFFS
  and a dedicated 2.9 MB `model` partition for ESP-SR wake-word data.

### 5.4 Graphics stack

LovyanGFX for the panel, LVGL above it — the same arrangement as the older board.
The panel goes through `lgfx::Bus_RGB` and `lgfx::Panel_RGB` with the pins and
timings from section 2. Under ESP-IDF or ESPHome the equivalent is
`esp_lcd_rgb_panel` / `rpi_dpi_rgb`.

At 800x480 and 16 bpp a full frame buffer is **768 KB** and must come from PSRAM.
Any configuration omitting PSRAM or selecting quad instead of octal fails during
panel initialisation rather than at build time.

Elecrow enable tear avoidance in their PlatformIO defaults
(`CONFIG_EXAMPLE_LVGL_PORT_AVOID_TEAR_ENABLE=y`) along with
`CONFIG_SPIRAM_FETCH_INSTRUCTIONS`, `CONFIG_SPIRAM_RODATA`, `FREERTOS_HZ=1000`
and `COMPILER_OPTIMIZATION_PERF`.

---

## 6. Getting sensor data onto this board

The older CrowPanel had exactly one free pin, and the interesting question was
how to read a one-wire sensor on it without disturbing the RGB panel's DMA. Here
the question is different: there are two free pins on V1.3+, but the DMA
sensitivity is, if anything, worse.

**Over the network.** Wi-Fi and Bluetooth LE are on the module, and with no local
sensor there is no timing-sensitive bit-banging to contend with the panel. Pulling
readings from an MQTT broker or an HTTP endpoint is the natural fit, and it is
what the voice features assume anyway.

**Over I2C, on the `I2C-OUT` connector.** A DHT20 at 0x38, SHT4x at 0x44 or
BME280 at 0x76 collides with nothing already on the bus, and the pull-ups are
fitted. This is the clean way to attach a local sensor. **Keep transactions
short** — this is the same bus the touch controller is on, and blocking it
starves the display.

**On GPIO2 or GPIO8, V1.3+ only.** The only genuinely free signal pins. Note that
bit-banging a one-wire sensor here has exactly the problem documented on the older
board: disabling interrupts starves the RGB DMA and Espressif warn this produces a
permanently shifted image. Use the RMT peripheral if you must do this at all.

**Over `UART1-OUT`.** Possible, at the cost of the microphone, and only with the
DIP switches at S1=0, S0=1.

One convenience: the **PCF8563 with its CR1220 backup cell** keeps wall-clock time
across power cycles, so timestamps on a chart survive a reboot without re-syncing
from the network first. No Elecrow example uses it, so expect to write that
yourself.

---

## 7. Known-wrong vendor documentation, collected

1. **The repository readme's "7, Pin definition" section is wrong throughout. Do
   not use it.** It lists `SD_CS 7`, `I2S_DOUT 12`, `I2S_BCLK 13`, `I2S_LRC 11`,
   `pin_sclk 42`, `pin_mosi 39`, `pin_dc 41`. On this board GPIO7 is R0, GPIO11
   to GPIO13 are green data, GPIO39 is the pixel clock, GPIO41 is VSYNC and
   GPIO42 is DE. It appears pasted from a different product. **Use
   `LovyanGFX_Driver.h` as the source of truth.**
2. **The wiki says the microSD chip select is `CS(3.3V)`.** It is tied to ground.
3. **The wiki labels the V1.3 microphone "I2S MIC"** while listing two pins and
   shipping PDM sample code. It is PDM.
4. **Elecrow's own PlatformIO wiki page for V1.2 specifies `8MB` flash,
   `default_8MB.csv` and `-DPSRAM_SIZE=2`.** That is wrong for an N16R8 board.
   Use the V1.3 page or the repository version.
5. **The V1.3 Port Introduction page says the UART1-OUT demo uses "pin 18".** It
   is GPIO19/20 here; the text was copied from the 3.5-inch page.
6. **Pixel clock: 16 MHz in the V1.3+ Arduino driver, 20 MHz in their own ESPHome
   YAML**, 21 MHz in the V1.0 and V1.2 drivers. Porches agree everywhere.
7. **Library versions contradict across pages:** LVGL 8.3.11 on the wiki, 8.3.3 in
   the readme, 9.1.0 in the PlatformIO project. ESP32-audioI2S 2.0.0 on the course
   page while the repository ships 3.0.12.
8. **Arduino core version: 3.0.2 in every lesson readme**, but 3.3.8 in one
   machine-generated hardware note citing no source. Treat 3.3.8 as unfounded.
9. **The DIP switch table lists `1 0 = TF Card` in the V1.2 section** and drops it
   in the V1.3 section. The schematic confirms channel 2 is genuinely
   unconnected on V1.3+.
10. **The V1.5 schematic PDF's title block still says V1.4.**
11. **The PlatformIO board JSON carries Espressif native-USB hwids** on a CH340K
    board, breaking port auto-detection.
12. **`factory_sourcecode` references `ui.h` and `ui.c` that were never
    committed.** Use `example/V1.3_and_V1.4_and_V1.5/Arduino/lesson-03/BigInch_LVGL`.
13. **N16R8 is a schematic text label, not the Eagle device name** — Elecrow
    themselves flag this and say to verify against the module marking. Now
    settled independently: the bring-up unit's silicon reports N16R8 directly.
    See section 1.
14. **The product page advertises MicroPython**, for which Elecrow ship no
    firmware. openHASP also does not support the Advance series.
15. **macOS ships no driver for this board's CH340K.** It enumerates as
    `1a86:7522`; Apple's built-in driver only matches `1a86:7523` and
    `1a86:55d4`. The board enumerates but gets no `/dev/cu.*` node, so it
    presents as a dead board rather than a driver problem. Not a vendor
    documentation error, but it costs exactly the same evening. See section
    2.6.

---

## 8. What the first bring-up checked

A unit was brought up on 2026-09-12 — connected, flashed, and run over its own
USB-C, esptool v5.3.0. Same order as the old pre-arrival checklist, so it is
easy to see what moved from "to check" to "checked."

1. **Board revision.** Not read from the silkscreen — inferred from behaviour,
   which works just as well. The companion MCU answered at I2C 0x30, which
   rules out V1.0 (that has an expander at 0x18 instead). The backlight lit
   correctly using the V1.3+ encoding, 0 brightest, which rules out V1.2. So:
   **V1.3 or later.** Nothing on this unit distinguishes V1.3 from V1.4 from
   V1.5 — consistent with section 9's note that those three appear
   electrically identical.
2. **The module.** Confirmed N16R8 directly from the silicon via esptool,
   without needing the laser marking. See section 1.
3. **The backlight responds** to the V1.3+ encoding. Confirmed.
4. **I2C bus scan.** 0x30 (companion MCU) and 0x5D (touch) both answered.
   0x51 (the RTC) was not probed — still outstanding.
5. **GT911 address.** Answered at 0x5D, the primary address, on the first
   try. The address-latch sequence — hold GPIO1 low, command the companion
   MCU to reset, 120 ms hold, release — worked as documented. The 0x14
   fallback was not needed.
6. **DIP switch positions as shipped.** Not checked. Still outstanding, and so
   are the audio, microphone and microSD paths that depend on them.
7. **Serial at 115200 with CDC off.** Confirmed, through the CH340K — once
   macOS had a driver for it at all. See section 2.6; that is not a firmware
   or wiring problem, but it cost real time regardless.
8. **Upload at 921600.** Confirmed reliable: roughly a dozen flashes, hash
   verified every time. This differs from the older CrowPanel 7.0, which
   needs 460800.
9. **Pixel clock.** Confirmed at the documented 16 MHz for V1.3+, with 8/4/8
   porches on both axes, 800x480, image stable. Whether the panel tolerates a
   higher clock was not attempted — still outstanding.
10. **Touch polling and display shake.** Checked, and root-caused rather than
    just observed. Two firmware causes, both fixed:
    - The LVGL draw buffers were allocated in PSRAM, and the RGB panel scans
      its framebuffer out of PSRAM continuously, so every flush contended the
      same bus the scanout needed. Moving the draw buffers — two 16 KB
      buffers — to internal DMA-capable RAM eliminated the idle jitter.
    - The touch driver issued two separate I2C reads per poll, one for the
      status byte and one for the coordinates. Those registers are
      contiguous, so one six-byte read fetches both and holds the shared bus
      for half as long. This eliminated the jitter that appeared while a
      finger was on the glass.
    Both confirmed by direct before-and-after observation.
11. **PSRAM speed.** 120 MHz is confirmed unreachable under PlatformIO: the
    installed
    `framework-arduinoespressif32-libs/esp32s3/qio_opi/include/sdkconfig.h`
    is precompiled with `CONFIG_SPIRAM_SPEED_80M`, and an application-level
    `-DCONFIG_SPIRAM_SPEED_120M=1` cannot change what the precompiled
    libraries already did — confirming section 5.2's note. More importantly,
    **80 MHz proved fine once the buffer placement above was corrected.** On
    this unit the jitter was never a PSRAM-speed problem, which is useful
    context for the contradictory vendor and community advice in section 1 —
    it does not resolve which revision, if any, genuinely needs 120 MHz, only
    that this one did not.
12. **Touch axis orientation.** Not checked. A long press anywhere works,
    which proves touch functions, but says nothing about axis orientation or
    precision across the screen. Still outstanding.
13. **Power supply headroom.** Not checked. Still outstanding.

---

## 9. Open questions

### Answered by the first bring-up

1. **Whether this specific unit is N16R8.** Yes — read directly from the
   silicon with esptool on 2026-09-12. See section 1. Elecrow's own caveat
   about the Eagle deviceset lacking a memory suffix still explains why the
   schematic alone couldn't settle this; it just no longer needs to.

### Still not resolvable from documentation, or from this bring-up

1. **Whether 120 MHz PSRAM helps or hurts on this revision.** Vendor issue #7
   root-causes flicker to 80 MHz on a V1.4 and fixes it at 120 MHz; the Home
   Assistant community finds 120 MHz unreliable on V1.2. Probably
   revision-dependent, but still unconfirmed — 120 MHz was never reached on
   the bring-up unit, because it is not reachable under PlatformIO (section
   8). What the bring-up did establish is that 80 MHz was fine once this
   unit's real, firmware-side jitter cause was fixed — evidence that this
   unit didn't need 120 MHz, not evidence about other revisions.
2. **The absolute pin-1 orientation of the 4-pin PH2.0 and XH2.54 headers.** Net
   membership is certain; physical order should be checked against the silkscreen.
3. **The full STC8H1K28 command table.** Only the bytes listed in section 2.3
    appear in any example. No source, no register spec, behaviour varies by
    revision, and the vendor issue asking for documentation is open and
    unanswered. Probing undefined bytes to find the rest is explicitly
    discouraged, and the part is not reflashable through the ESP32.
4. **The buzzer command byte.** No example in the repository sounds the buzzer.
    246 and 247 come from the wiki and are untested.
5. **The RTC's I2C address, from a vendor source.** 0x51 is the PCF8563
    datasheet address and is almost certainly right, but no Elecrow code, and
    no bring-up so far, touches the RTC, and the schematic shows no address
    pins — so it remains datasheet inference rather than vendor or hardware
    confirmation.
6. **Which physical DIP switch position is "0" and which is "1".** The schematic
    note says set S1 and S0 to 0 and 1 for `UART1-OUT`, but never defines the
    mapping to the physical slider. The 10 K pulldowns imply closed reads as 1;
    that is inference.
7. **Whether V1.5 differs electrically from V1.4** beyond the touch FPC package.
    Searching the V1.3, V1.4 and V1.5 schematics for every part discussed here
    returns identical results, which is presumably why Elecrow ship one example
    folder for all three.
8. **The LMD3526's L/R channel select on V1.3+.** Its pull-up R34 is marked
    `10K/NC` and appears unfitted, so the pin floats. Elecrow's code is mono and
    sidesteps the question.
9. **J11 pins 4 and 7 have no net** — mechanical only, or reserved, unknown.
10. **Whether anyone has run on-device ESP-SR wake-word detection on this
    board.** It is technically feasible and the patched core libraries even ship
    `esp_sr/srmodels.bin`, but Elecrow provide no example.
11. **Reported PCF8563 read flakiness under ESPHome** — driver, bus contention,
    or unit-specific, unknown.
12. **The V1.4 wireless socket's S3-to-ESP32-C6 TX pin**, which vendor issue #6
    asks about and which remains unanswered.

### On the "AI" in the product name

It is marketing for **voice interaction**: a microphone, a speaker and Wi-Fi.
An exhaustive listing of the repository finds **no ESP-SR, ESP-Skainet or
micro-wake-word example for this board.** Elecrow's audio lessons are HTTP MP3
streaming and record-then-play-back. The only "AI speech" asset in the tree is an
`OpenAI_Speech` example bundled inside the third-party `ESP32-audioI2S` library,
which is a cloud API call. **Treat the AI features as cloud services reached over
Wi-Fi**, with local wake-word detection possible in principle but unsupported by
the vendor.

---

## 10. Sources

- Vendor repository:
  <https://github.com/Elecrow-RD/CrowPanel-Advance-7-HMI-ESP32-S3-AI-Powered-IPS-Touch-Screen-800x480>
  — Arduino, ESP-IDF, PlatformIO and ESPHome examples, factory source, Eagle
  schematics and PCB for V1.0 through V1.5, datasheets, and the `ESP32S3_120M`
  patched toolchain.
- **Pin and timing source of truth**, within that repository:
  `example/<revision>/.../lesson-03/BigInch_LVGL/LovyanGFX_Driver.h`
- V1.5 schematic PDF:
  <https://github.com/Elecrow-RD/CrowPanel-Advance-7-HMI-ESP32-S3-AI-Powered-IPS-Touch-Screen-800x480/blob/master/Eagle_SCH%26PCB/version1.5/ESP32-Display-7.0-inch-V1.5.pdf>
- Vendor issue #2, the default partition scheme being too small:
  <https://github.com/Elecrow-RD/CrowPanel-Advance-7-HMI-ESP32-S3-AI-Powered-IPS-Touch-Screen-800x480/issues/2>
- Vendor issue #4, the undocumented STC8H1K28:
  <https://github.com/Elecrow-RD/CrowPanel-Advance-7-HMI-ESP32-S3-AI-Powered-IPS-Touch-Screen-800x480/issues/4>
- Vendor issue #7, flicker root-caused to PSRAM clock, with a reproducer:
  <https://github.com/Elecrow-RD/CrowPanel-Advance-7-HMI-ESP32-S3-AI-Powered-IPS-Touch-Screen-800x480/issues/7>
- Vendor issue #8, flicker caused by blocking GT911 I2C polling:
  <https://github.com/Elecrow-RD/CrowPanel-Advance-7-HMI-ESP32-S3-AI-Powered-IPS-Touch-Screen-800x480/issues/8>
- Elecrow wiki, V1.3 introduction and port reference:
  <https://www.elecrow.com/wiki/7.0_1_Introduction_to_CrowPanel-Advance-HMI_Screen_V13.html>
- Elecrow HMI display course:
  <https://www.elecrow.com/wiki/HMI_Display_course.html>
- Elecrow PlatformIO page for V1.2, the one with the wrong flash size:
  <https://www.elecrow.com/wiki/CrowPanel_advance_ESP32_AI_display_with_PlatformIO_V1.2.html>
- Product page:
  <https://www.elecrow.com/crowpanel-advance-7-hmi-esp32-ai-display-800x480-ai-ips-touch-screen.html>
- Elecrow forum, V1.0 versus V1.2 differences and the black-screen symptom:
  <https://forum.elecrow.com/discussion/2929/>
- Elecrow forum, audio and TF card being mutually exclusive:
  <https://forum.elecrow.com/discussion/26139/>
- Elecrow forum, micro-jitter fixed by RGB timing on a V1.2:
  <https://forum.elecrow.com/discussion/28135/>
- Elecrow forum, the required PlatformIO platform fork:
  <https://forum.elecrow.com/discussion/28413/>
- Elecrow forum, SKU and revision confirmation:
  <https://forum.elecrow.com/discussion/28231/>
- espboards.dev pinout — **describes V1.0**, so read it with that in mind:
  <https://www.espboards.dev/esp32/elecrow-crowpanel-advance-7-esp32-s3/>
- espboards.dev comparison of the two CrowPanel 7-inch generations:
  <https://www.espboards.dev/blog/crowpanel-esp32-hmi-evolution-comparison/>
- Harald Kreuzer, LVGL and ESP-IDF on this board:
  <https://www.haraldkreuzer.net/en/news/crowpanel-advance-7-display-basics-and-gui-development-lvgl-esp-idf>
- Home Assistant community thread — 120 MHz PSRAM instability on V1.2, and the
  PCF8563 read problems:
  <https://community.home-assistant.io/t/elecrow-crowpanel-advance-7-0-hmi-voice-assistant-and-control-panel-ha/1000607>
- Espressif ESP32-S3 series datasheet:
  <https://documentation.espressif.com/esp32-s3_datasheet_en.pdf>
- Espressif ESP32-S3 RGB LCD driver notes, on DMA underrun and permanently
  shifted images:
  <https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/lcd/rgb_lcd.html>
- Goodix GT911 Programming Guide, rev 10 — the I2C address strapping rule:
  <https://www.lcd-module.de/fileadmin/eng/pdf/zubehoer/GT911_Programming_Guide_Rev.10.pdf>

---

## Authorship

Compiled by Mark Castelluccio <markacastelluccio@gmail.com>.

Researched and drafted with Claude Code (Anthropic Claude Opus 5). Hardware
claims were cross-checked against Elecrow's Eagle schematics and committed
source; values read verbatim from those files are quoted as code. Conflicting
figures between revisions were resolved by reading all three versions of the
driver directly rather than trusting any single summary. **A physical unit was
brought up on 2026-09-12** — connected, flashed, and run over its own USB-C
with esptool v5.3.0 — and section 8 records what that confirmed. Section 9
lists what documentation and that bring-up together still cannot settle.
