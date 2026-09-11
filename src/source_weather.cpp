// Implementation of the Open-Meteo source.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic).

#include "source_weather.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>

#include "parse.h"
#include "secrets.h"

namespace source_weather {
namespace {

// Open-Meteo's current block carries interval 900, so this is its real update
// rate. Polling faster returns the same numbers.
constexpr uint32_t FETCH_INTERVAL_MS = 15UL * 60UL * 1000UL;

// After a failure, try again sooner than the full interval, but not so soon
// that a sustained outage hammers a free public API.
constexpr uint32_t RETRY_AFTER_FAIL_MS = 60UL * 1000UL;

constexpr uint16_t HTTP_TIMEOUT_MS = 8000;

channel::Channel* g_channel     = nullptr;
uint32_t          g_next_due_ms = 0;
uint32_t          g_failures    = 0;
bool              g_first       = true;

bool fetch() {
    char url[256];
    snprintf(url, sizeof(url),
             "http://api.open-meteo.com/v1/forecast"
             "?latitude=%s&longitude=%s"
             "&current=temperature_2m,relative_humidity_2m,weather_code"
             "&timezone=America%%2FLos_Angeles",
             WEATHER_LATITUDE, WEATHER_LONGITUDE);

    HTTPClient http;
    http.setTimeout(HTTP_TIMEOUT_MS);
    http.setConnectTimeout(HTTP_TIMEOUT_MS);
    if (!http.begin(url)) {
        Serial.println("weather: http.begin failed");
        return false;
    }

    const int code = http.GET();
    if (code != HTTP_CODE_OK) {
        Serial.printf("weather: HTTP %d\n", code);
        http.end();
        return false;
    }

    const String body = http.getString();
    http.end();

    parse::Weather w{};
    if (!parse::openMeteo(body.c_str(), body.length(), w)) {
        Serial.println("weather: response did not parse");
        return false;
    }

    if (g_channel != nullptr) {
        g_channel->update(w.temperature_c, w.humidity_pct, millis());
    }
    Serial.printf("weather: %.1f C  %.0f%% RH\n", w.temperature_c, w.humidity_pct);
    return true;
}

}  // namespace

void begin(channel::Channel& outdoor) {
    g_channel     = &outdoor;
    g_next_due_ms = 0;
    g_first       = true;
}

void poll() {
    if (WiFi.status() != WL_CONNECTED) return;

    const uint32_t now = millis();
    // Unsigned comparison, correct across the millis() rollover.
    if (!g_first && static_cast<int32_t>(now - g_next_due_ms) < 0) return;
    g_first = false;

    if (fetch()) {
        g_failures    = 0;
        g_next_due_ms = now + FETCH_INTERVAL_MS;
    } else {
        ++g_failures;
        g_next_due_ms = now + RETRY_AFTER_FAIL_MS;
    }
}

uint32_t failures() { return g_failures; }

}  // namespace source_weather
