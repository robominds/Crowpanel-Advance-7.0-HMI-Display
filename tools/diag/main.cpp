// Throwaway diagnostic for the two subsystems the application never touches:
// the PCF8563 real-time clock and the microSD card.
//
// Deliberately does NOT initialise the RGB panel. Nothing here needs it, and
// leaving the panel alone keeps this build simple and keeps its I2C and SPI
// traffic from competing with the scanout DMA. The screen will hold whatever
// the application last drew; flashing the application back restores it.
//
//   pio run -e diag -t upload            # run the diagnostic
//   pio run -e advance_70 -t upload      # put the application back
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic).

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <Wire.h>

#include "board_pins.h"

namespace {

// microSD, from docs/HARDWARE.md section 2.8. Note these are the SAME pins the
// I2S amplifier uses; a CH486F analog switch pair decides which subsystem is
// connected, and the DIP switches drive that selection. On a V1.3 or later board the
// only position that reaches the card is S1=1, S0=1.
constexpr int SD_MISO = 4;
constexpr int SD_SCK  = 5;
constexpr int SD_MOSI = 6;

// The card's chip select is hardwired to ground, so the card is permanently
// selected and no GPIO drives it. The SD library still demands a pin number,
// so give it one of the two genuinely free pins rather than GPIO0 - GPIO0 is
// the boot strapping pin and the auto-download circuit, and holding it low
// across a reset would drop the board into the bootloader.
constexpr int SD_CS_DUMMY = board::FREE_GPIO_B;  // GPIO8, V1.3+ only

SPIClass g_sd_spi(FSPI);

uint8_t bcd2dec(uint8_t v) { return static_cast<uint8_t>((v >> 4) * 10 + (v & 0x0F)); }

const char* deviceName(uint8_t addr) {
    switch (addr) {
        case 0x18: return "I/O expander (a V1.0 board - not supported)";
        case 0x30: return "STC8H1K28 companion MCU (backlight, touch reset)";
        case 0x51: return "PCF8563 real-time clock";
        case 0x5D: return "GT911 touch, primary address";
        case 0x14: return "GT911 touch, BACKUP address";
        default:   return "unexpected - not documented for this board";
    }
}

void scanI2C() {
    Serial.println("\n--- I2C bus scan (GPIO15 SDA, GPIO16 SCL) ---");
    int found = 0;
    for (uint8_t a = 0x08; a < 0x78; ++a) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() == 0) {
            Serial.printf("  0x%02X  %s\n", a, deviceName(a));
            ++found;
        }
    }
    if (found == 0) Serial.println("  nothing answered - check the bus");
    Serial.printf("  %d device(s)\n", found);
}

void testRTC() {
    Serial.println("\n--- PCF8563 real-time clock ---");
    Serial.printf("  probing 0x%02X, an address taken from the part's datasheet:\n",
                  board::RTC_ADDR);
    Serial.println("  no Elecrow example ever touches this chip, so this is the");
    Serial.println("  first confirmation it is really there.");

    Wire.beginTransmission(board::RTC_ADDR);
    if (Wire.endTransmission() != 0) {
        Serial.println("  NOT PRESENT at 0x51.");
        return;
    }
    Serial.println("  present.");

    // Registers 0x00..0x08: two control bytes then seconds, minutes, hours,
    // day, weekday, month, year. All BCD.
    Wire.beginTransmission(board::RTC_ADDR);
    Wire.write(0x00);
    if (Wire.endTransmission(false) != 0) {
        Serial.println("  register read failed (address phase)");
        return;
    }
    uint8_t r[9] = {0};
    if (Wire.requestFrom(static_cast<int>(board::RTC_ADDR), 9) != 9) {
        Serial.println("  register read failed (data phase)");
        return;
    }
    for (uint8_t& b : r) b = Wire.read();

    Serial.printf("  control1=0x%02X  control2=0x%02X\n", r[0], r[1]);

    // Bit 7 of the seconds register is the voltage-low flag. Set means the
    // oscillator stopped at some point, so the time cannot be trusted - which
    // is exactly what a board with a fresh or absent backup cell reports.
    const bool vl = (r[2] & 0x80) != 0;
    Serial.printf("  clock integrity: %s\n",
                  vl ? "VL SET - time is NOT trustworthy (oscillator stopped)"
                     : "ok (oscillator has run continuously)");

    Serial.printf("  reads back: 20%02u-%02u-%02u %02u:%02u:%02u (weekday %u)\n",
                  bcd2dec(r[8]), bcd2dec(r[7] & 0x1F), bcd2dec(r[5] & 0x3F),
                  bcd2dec(r[4] & 0x3F), bcd2dec(r[3] & 0x7F), bcd2dec(r[2] & 0x7F),
                  bcd2dec(r[6] & 0x07));
    if (vl) {
        Serial.println("  (an unset clock reports a meaningless date - expected");
        Serial.println("   until something writes the time and the CR1220 holds it)");
    }
}

// Measure before driving. GPIO4/5/6 reach the card only through a CH486F
// analog switch, and if the DIP switches have routed them elsewhere there is
// nothing on the other end. A seated, powered SD card holds its DAT0 line -
// which is MISO here - high through an internal pull-up, so a floating or
// pulled-down reading means the mux is not connecting the card, no matter what
// the switch is labelled.
void probeSDPins() {
    Serial.println("\n--- microSD pin probe (passive, before driving) ---");
    const int pins[] = {SD_MISO, SD_SCK, SD_MOSI};
    const char* names[] = {"MISO/DAT0", "SCK", "MOSI/CMD"};

    for (int i = 0; i < 3; ++i) {
        pinMode(pins[i], INPUT);
        delay(2);
        const int floating = digitalRead(pins[i]);

        pinMode(pins[i], INPUT_PULLDOWN);
        delay(5);
        const int pulled_down = digitalRead(pins[i]);

        pinMode(pins[i], INPUT);
        delay(2);

        const char* verdict =
            (floating == HIGH && pulled_down == HIGH)
                ? "driven HIGH - something is attached and holding it"
            : (floating == HIGH && pulled_down == LOW)
                ? "floating high, no external pull-up - nothing attached"
                : "low - nothing holding it";

        Serial.printf("  GPIO%-2d %-10s float=%d  with pulldown=%d   %s\n",
                      pins[i], names[i], floating, pulled_down, verdict);
    }
    Serial.println("  A routed, powered card holds MISO/DAT0 HIGH against a");
    Serial.println("  pulldown. If none of these are held, the analog switch is");
    Serial.println("  not connecting the card and no SPI setting will help.");
}

void testSD() {
    Serial.println("\n--- microSD ---");
    Serial.printf("  SPI on FSPI: SCK %d, MISO %d, MOSI %d, CS hardwired to GND\n",
                  SD_SCK, SD_MISO, SD_MOSI);

    g_sd_spi.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS_DUMMY);

    // 40 MHz is what Elecrow's own example uses. Fall back once, because a
    // marginal card or a long trace often works at a slower clock.
    bool ok = SD.begin(SD_CS_DUMMY, g_sd_spi, 40000000);
    if (!ok) {
        Serial.println("  40 MHz failed, retrying at 10 MHz...");
        ok = SD.begin(SD_CS_DUMMY, g_sd_spi, 10000000);
    }

    if (!ok) {
        Serial.println("  CARD NOT FOUND.");
        Serial.println("  Check the DIP switches FIRST. These pins are shared with");
        Serial.println("  the audio path through a CH486F analog switch, and on a");
        Serial.println("  V1.3 or later board the ONLY position that reaches the");
        Serial.println("  card is S1=1, S0=1 (microphone and TF card together).");
        Serial.println("  No firmware can see the card in any other position.");
        return;
    }

    const uint8_t type = SD.cardType();
    const char*   name = type == CARD_MMC   ? "MMC"
                         : type == CARD_SD  ? "SDSC"
                         : type == CARD_SDHC ? "SDHC/SDXC"
                                             : "unknown";
    Serial.printf("  card type: %s\n", name);
    Serial.printf("  size: %llu MB\n", SD.cardSize() / (1024ULL * 1024ULL));
    Serial.printf("  used: %llu MB of %llu MB\n",
                  SD.usedBytes() / (1024ULL * 1024ULL),
                  SD.totalBytes() / (1024ULL * 1024ULL));

    Serial.println("  root directory:");
    File root = SD.open("/");
    if (!root || !root.isDirectory()) {
        Serial.println("    could not open root");
        return;
    }
    int n = 0;
    for (File f = root.openNextFile(); f; f = root.openNextFile()) {
        Serial.printf("    %-28s %s%u\n", f.name(),
                      f.isDirectory() ? "<dir>  " : "", 
                      f.isDirectory() ? 0u : static_cast<unsigned>(f.size()));
        if (++n >= 20) { Serial.println("    ... (truncated at 20)"); break; }
    }
    if (n == 0) Serial.println("    (empty)");

    // Prove it is writable, then clean up after ourselves.
    const char* probe = "/crowpanel_diag.txt";
    File w = SD.open(probe, FILE_WRITE);
    if (w) {
        w.println("CrowPanel Advance 7.0 diagnostic write test");
        w.close();
        File rd = SD.open(probe);
        const bool readable = rd && rd.available() > 0;
        if (rd) rd.close();
        SD.remove(probe);
        Serial.printf("  write test: %s\n",
                      readable ? "PASSED (wrote, read back, removed)" : "wrote but read back empty");
    } else {
        Serial.println("  write test: FAILED to open a file for writing (card locked?)");
    }
}

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("\n\n=== CrowPanel Advance 7.0 diagnostic ===");
    Serial.println("RTC and microSD only. The panel is deliberately not started,");
    Serial.println("so the screen holds whatever the application last drew.");

    Wire.begin(board::I2C_SDA, board::I2C_SCL, board::I2C_HZ);
    delay(50);

    scanI2C();
    testRTC();
    probeSDPins();
    testSD();

    Serial.println("\n=== done ===");
    Serial.println("Restore the application with: pio run -e advance_70 -t upload");
}

// Live probe. The DIP switch positions are not readable in software - they
// drive the analog switch's select lines directly - and Elecrow never document
// which physical slider position is "0". So watch the pins instead: flip a
// switch and see whether the card starts holding MISO high. That answers the
// question empirically in seconds, where reflashing per position takes minutes.
void loop() {
    static uint32_t n = 0;

    const int pins[] = {SD_MISO, SD_SCK, SD_MOSI};
    int held[3];
    for (int i = 0; i < 3; ++i) {
        pinMode(pins[i], INPUT_PULLDOWN);
        delay(3);
        held[i] = digitalRead(pins[i]);
        pinMode(pins[i], INPUT);
    }

    Serial.printf("[%4u] against a pulldown:  MISO=%d  SCK=%d  MOSI=%d   %s\n",
                  n++, held[0], held[1], held[2],
                  held[0] ? "<<< CARD IS ROUTED - this switch position works"
                          : "nothing routed - try the other switch positions");
    delay(1000);
}
