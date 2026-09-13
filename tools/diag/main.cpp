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

uint8_t dec2bcd(uint8_t v) { return static_cast<uint8_t>(((v / 10) << 4) | (v % 10)); }

// Writes the time and, by writing bit 7 of the seconds register as zero, clears
// the voltage-low flag. That flag latches whenever the oscillator stops, so
// clearing it here is what makes the next power-up meaningful: if it comes back
// set, the clock lost power, which means the CR1220 is not holding it.
bool setRTC(int y, int mo, int d, int wd, int h, int mi, int sec) {
    Wire.beginTransmission(board::RTC_ADDR);
    Wire.write(0x02);
    Wire.write(dec2bcd(static_cast<uint8_t>(sec)) & 0x7F);  // bit 7 = 0 clears VL
    Wire.write(dec2bcd(static_cast<uint8_t>(mi)));
    Wire.write(dec2bcd(static_cast<uint8_t>(h)));
    Wire.write(dec2bcd(static_cast<uint8_t>(d)));
    Wire.write(dec2bcd(static_cast<uint8_t>(wd)));
    Wire.write(dec2bcd(static_cast<uint8_t>(mo)));          // century bit 7 = 0
    Wire.write(dec2bcd(static_cast<uint8_t>(y % 100)));
    return Wire.endTransmission() == 0;
}

void printRTC(const char* prefix) {
    Wire.beginTransmission(board::RTC_ADDR);
    Wire.write(0x02);
    if (Wire.endTransmission(false) != 0) { Serial.printf("%s read failed\n", prefix); return; }
    if (Wire.requestFrom(static_cast<int>(board::RTC_ADDR), 7) != 7) {
        Serial.printf("%s read failed\n", prefix); return;
    }
    uint8_t r[7]; for (uint8_t& b : r) b = Wire.read();
    const bool vl = (r[0] & 0x80) != 0;
    Serial.printf("%s 20%02u-%02u-%02u %02u:%02u:%02u   VL=%d  %s\n", prefix,
                  bcd2dec(r[6]), bcd2dec(r[5] & 0x1F), bcd2dec(r[3] & 0x3F),
                  bcd2dec(r[2] & 0x3F), bcd2dec(r[1] & 0x7F), bcd2dec(r[0] & 0x7F),
                  vl ? 1 : 0,
                  vl ? "<<< oscillator STOPPED since the flag was cleared"
                     : "clock has run continuously since it was set");
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

// Command driven, so the clock can be set and read back WITHOUT reflashing in
// between. That matters for the battery test: the board has to be fully
// unpowered between the two reads, and a reflash would reset the clock again.
//
//   set Y M D WD h m s   write the clock and clear the voltage-low flag
//   get                  read it back
//   probe                sample the microSD pins against a pulldown
void loop() {
    static bool prompted = false;
    if (!prompted) {
        Serial.println("\ncommands: set <Y> <M> <D> <WD> <h> <m> <s> | get | probe");
        prompted = true;
    }

    if (!Serial.available()) { delay(50); return; }

    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) return;

    if (line.startsWith("set")) {
        int y, mo, d, wd, h, mi, sec;
        if (sscanf(line.c_str(), "set %d %d %d %d %d %d %d", &y, &mo, &d, &wd, &h,
                   &mi, &sec) != 7) {
            Serial.println("  usage: set <Y> <M> <D> <WD> <h> <m> <s>");
            return;
        }
        Serial.printf("  writing 20%02d-%02d-%02d %02d:%02d:%02d ...\n", y % 100, mo,
                      d, h, mi, sec);
        if (!setRTC(y, mo, d, wd, h, mi, sec)) {
            Serial.println("  WRITE FAILED");
            return;
        }
        delay(20);
        printRTC("  now reads:");
        Serial.println("  voltage-low flag cleared. Unplug the board completely,");
        Serial.println("  wait, plug it back in, and run 'get'. If VL comes back");
        Serial.println("  set, the CR1220 is not holding the clock.");
    } else if (line.startsWith("get")) {
        printRTC("  reads:");
    } else if (line.startsWith("probe")) {
        probeSDPins();
    } else {
        Serial.printf("  unknown command: %s\n", line.c_str());
    }
}