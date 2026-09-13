// Implementation of the Wi-Fi manager.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic).

#include "net.h"

#include <Arduino.h>
#include <WiFi.h>

#include "secrets.h"

namespace net {
namespace {

// Six seconds, not two. A WPA2 association plus DHCP commonly takes three to
// five seconds, and the first bring-up on real hardware showed a two-second
// retry firing underneath an attempt still in flight: WiFi.begin() then fails
// outright with "sta is connecting, cannot set config" and the attempt it
// interrupted is wasted. The floor has to clear a normal association.
constexpr uint32_t RETRY_MIN_MS = 6000;
constexpr uint32_t RETRY_MAX_MS = 60000;

uint32_t g_retry_ms     = RETRY_MIN_MS;
uint32_t g_last_try_ms  = 0;
bool     g_was_connected = false;

// True while an association attempt is genuinely in flight. Starting a second
// one underneath it makes WiFi.begin() fail outright with "sta is connecting,
// cannot set config" and wastes the attempt it interrupted.
bool     g_attempting         = false;
uint32_t g_attempt_started_ms = 0;

// Hardware showed associations finishing anywhere from two to well past six
// seconds, so no fixed interval is safe. Wait for the radio to say it is done.
// This cap only exists so a lost event cannot wedge the state machine.
constexpr uint32_t ATTEMPT_TIMEOUT_MS = 20000;

void onWiFiEvent(arduino_event_id_t event) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_CONNECTED:
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
        case ARDUINO_EVENT_WIFI_STA_STOP:
            g_attempting = false;
            break;
        default:
            break;
    }
}

void attempt() {
    Serial.printf("wifi: connecting to %s\n", WIFI_SSID);
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    g_last_try_ms        = millis();
    g_attempt_started_ms = g_last_try_ms;
    g_attempting         = true;
}

}  // namespace

void begin() {
    WiFi.onEvent(onWiFiEvent);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    // The panel is mains-powered and needs throughput more than it needs the
    // few milliwatts modem sleep would save.
    WiFi.setSleep(false);
    attempt();
}

void poll() {
    const bool up = connected();

    if (up != g_was_connected) {
        g_was_connected = up;
        if (up) {
            Serial.printf("wifi: up, %s\n", WiFi.localIP().toString().c_str());
            g_retry_ms = RETRY_MIN_MS;
        } else {
            Serial.println("wifi: down");
        }
    }

    if (up) return;

    // Never start an attempt on top of one still in flight. The radio tells us
    // when the previous one concluded, one way or the other; the timeout is
    // only a backstop against a lost event.
    if (g_attempting) {
        if (millis() - g_attempt_started_ms < ATTEMPT_TIMEOUT_MS) return;
        g_attempting = false;
    }

    if (millis() - g_last_try_ms < g_retry_ms) return;

    attempt();
    g_retry_ms *= 2;
    if (g_retry_ms > RETRY_MAX_MS) g_retry_ms = RETRY_MAX_MS;
}

bool connected() { return WiFi.status() == WL_CONNECTED; }

}  // namespace net
