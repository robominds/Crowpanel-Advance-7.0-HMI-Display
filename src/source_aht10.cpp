// Implementation of the local AHT10 source. See source_aht10.h.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic).

#include "source_aht10.h"

#include <Arduino.h>
#include <Wire.h>

#include "aht10.h"

namespace source_aht10 {
namespace {

// The house sensors publish every ten seconds, and the staleness rule for the
// indoor channel is a minute, so this matches what a subscriber would see.
constexpr uint32_t SAMPLE_INTERVAL_MS = 10000;

// The datasheet asks for 80 ms; 100 ms costs nothing and covers a slow part.
constexpr uint32_t CONVERSION_MS = 100;

// One complaint a minute. A sensor pulled out of its socket would otherwise
// fill the log with identical lines.
constexpr uint32_t COMPLAIN_INTERVAL_MS = 60000;

enum class State { Idle, Converting };

State    g_state           = State::Idle;
bool     g_present         = false;
uint32_t g_next_due_ms     = 0;
uint32_t g_trigger_ms      = 0;
uint32_t g_failed          = 0;
uint32_t g_last_complaint  = 0;
bool     g_complained      = false;

bool writeCommand(const uint8_t* bytes, size_t len) {
    Wire.beginTransmission(aht10::ADDRESS);
    Wire.write(bytes, len);
    return Wire.endTransmission() == 0;
}

void readFailed(uint32_t now) {
    ++g_failed;
    g_state       = State::Idle;
    g_next_due_ms = now + SAMPLE_INTERVAL_MS;
    if (!g_complained || now - g_last_complaint >= COMPLAIN_INTERVAL_MS) {
        Serial.printf("aht10: read failed (%lu total)\n",
                      static_cast<unsigned long>(g_failed));
        g_last_complaint = now;
        g_complained     = true;
    }
}

}  // namespace

bool begin() {
    const uint8_t reset[]       = {0xBA};
    const uint8_t initialise[]  = {0xE1, 0x08, 0x00};

    if (!writeCommand(reset, sizeof reset)) {
        Serial.println("aht10: no sensor at 0x38, reading the broker instead");
        return false;
    }
    delay(20);   // datasheet: 20 ms after a soft reset

    if (!writeCommand(initialise, sizeof initialise)) {
        Serial.println("aht10: no sensor at 0x38, reading the broker instead");
        return false;
    }
    delay(10);

    if (Wire.requestFrom(static_cast<int>(aht10::ADDRESS), 1) != 1) {
        Serial.println("aht10: no sensor at 0x38, reading the broker instead");
        return false;
    }
    const uint8_t status = static_cast<uint8_t>(Wire.read());
    if ((status & aht10::STATUS_CALIBRATED) == 0) {
        Serial.printf("aht10: 0x38 answered but is not calibrated (status 0x%02X);"
                      " reading the broker instead\n",
                      static_cast<unsigned>(status));
        return false;
    }

    g_present     = true;
    g_next_due_ms = millis();
    Serial.printf("aht10: found at 0x38, publishing %s\n", MQTT_TOPIC_TEMP);
    return true;
}

bool poll(float& temperature_c, float& humidity_pct) {
    if (!g_present) return false;

    const uint32_t now = millis();

    if (g_state == State::Idle) {
        // Signed difference, so a due time that has just wrapped still fires.
        if (static_cast<int32_t>(now - g_next_due_ms) < 0) return false;
        const uint8_t trigger[] = {0xAC, 0x33, 0x00};
        if (!writeCommand(trigger, sizeof trigger)) {
            readFailed(now);
            return false;
        }
        g_trigger_ms = now;
        g_state      = State::Converting;
        return false;
    }

    if (now - g_trigger_ms < CONVERSION_MS) return false;

    uint8_t frame[6] = {0};
    if (Wire.requestFrom(static_cast<int>(aht10::ADDRESS),
                         static_cast<int>(sizeof frame)) !=
        static_cast<int>(sizeof frame)) {
        readFailed(now);
        return false;
    }
    for (uint8_t& b : frame) b = static_cast<uint8_t>(Wire.read());

    aht10::Sample sample{};
    if (!aht10::convert(frame, sizeof frame, sample)) {
        readFailed(now);
        return false;
    }

    g_state       = State::Idle;
    g_next_due_ms = g_trigger_ms + SAMPLE_INTERVAL_MS;
    temperature_c = sample.temperature_c;
    humidity_pct  = sample.humidity_pct;
    return true;
}

bool present() { return g_present; }

uint32_t failedReads() { return g_failed; }

}  // namespace source_aht10
