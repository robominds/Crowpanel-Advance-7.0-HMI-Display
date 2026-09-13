// Implementation of the wall-clock source.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic).

#include "rtc.h"

#include <Arduino.h>
#include <Wire.h>

#include <sys/time.h>

#include "board_pins.h"

namespace rtc {
namespace {

// Register 0x02 is the seconds, and its top bit is the voltage-low flag: the
// chip sets it whenever its oscillator stops and only a write clears it. That
// is what lets a clock distinguish "I have kept time" from "I am reporting a
// plausible number I made up on power-up", and it is why reading the time alone
// proves nothing.
constexpr uint8_t REG_SECONDS = 0x02;

// Anything before this is not a real wall-clock reading. Used to tell a synced
// clock from the 1970 the system starts at.
constexpr time_t PLAUSIBLE_EPOCH = 1767225600;  // 2026-01-01

constexpr uint32_t SYNC_RETRY_MS = 30000;

bool     g_present = false;
bool     g_synced  = false;
uint32_t g_sntp_started_ms = 0;
bool     g_sntp_started    = false;

uint8_t bcd2dec(uint8_t v) { return static_cast<uint8_t>((v >> 4) * 10 + (v & 0x0F)); }
uint8_t dec2bcd(uint8_t v) { return static_cast<uint8_t>(((v / 10) << 4) | (v % 10)); }

// Reads the chip. `trustworthy` reports the voltage-low flag: false means the
// oscillator stopped at some point and the numbers mean nothing.
bool readChip(struct tm& out, bool& trustworthy) {
    Wire.beginTransmission(board::RTC_ADDR);
    Wire.write(REG_SECONDS);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(static_cast<int>(board::RTC_ADDR), 7) != 7) return false;

    uint8_t r[7];
    for (uint8_t& b : r) b = Wire.read();

    trustworthy = (r[0] & 0x80) == 0;

    out = {};
    out.tm_sec  = bcd2dec(r[0] & 0x7F);
    out.tm_min  = bcd2dec(r[1] & 0x7F);
    out.tm_hour = bcd2dec(r[2] & 0x3F);
    out.tm_mday = bcd2dec(r[3] & 0x3F);
    out.tm_wday = bcd2dec(r[4] & 0x07);
    out.tm_mon  = bcd2dec(r[5] & 0x1F) - 1;      // tm months are 0-based
    out.tm_year = bcd2dec(r[6]) + 100;           // tm years are since 1900
    out.tm_isdst = -1;                           // let mktime work out DST
    return true;
}

// Writes the chip and clears the voltage-low flag, which is what makes the
// next power-up's flag meaningful.
bool writeChip(const struct tm& t) {
    Wire.beginTransmission(board::RTC_ADDR);
    Wire.write(REG_SECONDS);
    Wire.write(dec2bcd(static_cast<uint8_t>(t.tm_sec)) & 0x7F);
    Wire.write(dec2bcd(static_cast<uint8_t>(t.tm_min)));
    Wire.write(dec2bcd(static_cast<uint8_t>(t.tm_hour)));
    Wire.write(dec2bcd(static_cast<uint8_t>(t.tm_mday)));
    Wire.write(dec2bcd(static_cast<uint8_t>(t.tm_wday)));
    Wire.write(dec2bcd(static_cast<uint8_t>(t.tm_mon + 1)));
    Wire.write(dec2bcd(static_cast<uint8_t>(t.tm_year % 100)));
    return Wire.endTransmission() == 0;
}

}  // namespace

bool begin(const char* tz) {
    setenv("TZ", tz, 1);
    tzset();

    Wire.beginTransmission(board::RTC_ADDR);
    g_present = (Wire.endTransmission() == 0);
    if (!g_present) {
        Serial.printf("rtc: nothing at 0x%02X; the chart axis will stay relative\n",
                      board::RTC_ADDR);
        return false;
    }

    struct tm t;
    bool trustworthy = false;
    if (!readChip(t, trustworthy)) {
        Serial.println("rtc: present but unreadable");
        return false;
    }

    if (!trustworthy) {
        // Expected on a board whose clock has never been set, or whose backup
        // cell has been out. Not an error, and the network will fix it.
        Serial.println("rtc: present, but its oscillator stopped at some point");
        Serial.println("rtc: so the time it holds is meaningless. Waiting for the");
        Serial.println("rtc: network to set it.");
        return true;
    }

    const time_t     epoch = mktime(&t);
    struct timeval   tv    = {epoch, 0};
    settimeofday(&tv, nullptr);
    Serial.printf("rtc: adopted %04d-%02d-%02d %02d:%02d:%02d from the chip\n",
                  t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min,
                  t.tm_sec);
    return true;
}

bool present() { return g_present; }

bool hasTime() { return time(nullptr) >= PLAUSIBLE_EPOCH; }

bool syncedFromNetwork() { return g_synced; }

void poll(bool network_up) {
    if (g_synced || !network_up) return;

    if (!g_sntp_started) {
        // The timezone is already set; pass nullptr so SNTP does not reset it.
        configTzTime(getenv("TZ"), "pool.ntp.org", "time.nist.gov");
        g_sntp_started    = true;
        g_sntp_started_ms = millis();
        Serial.println("rtc: asking the network for the time");
        return;
    }

    if (!hasTime()) {
        // Retry periodically rather than giving up: the board may have
        // associated before the network was actually usable.
        if (millis() - g_sntp_started_ms > SYNC_RETRY_MS) g_sntp_started = false;
        return;
    }

    g_synced = true;

    const time_t now = time(nullptr);
    struct tm    lt;
    localtime_r(&now, &lt);
    Serial.printf("rtc: network says %04d-%02d-%02d %02d:%02d:%02d\n",
                  lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, lt.tm_hour,
                  lt.tm_min, lt.tm_sec);

    if (!g_present) return;
    Serial.println(writeChip(lt) ? "rtc: written back to the chip"
                                 : "rtc: WRITE BACK FAILED");
}

}  // namespace rtc
