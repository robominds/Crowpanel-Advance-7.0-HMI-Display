// Implementation of the MQTT source.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic).

#include "source_mqtt.h"

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>

#include <cstring>

#include "parse.h"
#include "secrets.h"

namespace source_mqtt {
namespace {

// From include/secrets.h, because which device publishes the indoor reading is
// a property of one person's network rather than of this firmware.
constexpr char TOPIC_TEMP[] = MQTT_TOPIC_TEMP;
constexpr char TOPIC_HUM[]  = MQTT_TOPIC_HUM;

constexpr uint32_t RETRY_MIN_MS = 2000;
constexpr uint32_t RETRY_MAX_MS = 30000;

WiFiClient   g_wifi;
PubSubClient g_mqtt(g_wifi);

channel::Channel* g_channel = nullptr;

float    g_last_hum      = 0.0f;
bool     g_have_hum      = false;
uint32_t g_rejected      = 0;
uint32_t g_retry_ms      = RETRY_MIN_MS;
uint32_t g_last_try_ms   = 0;

void onMessage(char* topic, uint8_t* payload, unsigned int len) {
    float value = 0.0f;
    if (!parse::decimal(reinterpret_cast<const char*>(payload), len, value)) {
        ++g_rejected;
        Serial.printf("mqtt: rejected payload on %s\n", topic);
        return;
    }

    if (std::strcmp(topic, TOPIC_HUM) == 0) {
        g_last_hum = value;
        g_have_hum = true;
        return;
    }

    if (std::strcmp(topic, TOPIC_TEMP) == 0) {
        if (!g_have_hum) return;  // wait for the first humidity
        if (g_channel != nullptr) {
            g_channel->update(value, g_last_hum, millis());
        }
    }
}

void connect() {
    g_last_try_ms = millis();

    // A client id must be unique on the broker or it will disconnect the
    // previous holder in a loop. The MAC guarantees that.
    char id[32];
    snprintf(id, sizeof(id), "crowpanel-%012llX", ESP.getEfuseMac());

    Serial.printf("mqtt: connecting to %s:%d as %s\n", MQTT_HOST, MQTT_PORT, id);
    if (!g_mqtt.connect(id)) {
        Serial.printf("mqtt: failed, state %d\n", g_mqtt.state());
        return;
    }

    g_mqtt.subscribe(TOPIC_TEMP);
    g_mqtt.subscribe(TOPIC_HUM);
    g_retry_ms = RETRY_MIN_MS;
    Serial.println("mqtt: connected and subscribed");
}

}  // namespace

void begin(channel::Channel& office) {
    g_channel = &office;
    g_mqtt.setServer(MQTT_HOST, MQTT_PORT);
    g_mqtt.setCallback(onMessage);
    // Payloads are single numbers, so the default 256-byte buffer is ample.
    g_mqtt.setKeepAlive(30);
}

void poll() {
    if (WiFi.status() != WL_CONNECTED) return;

    if (!g_mqtt.connected()) {
        if (millis() - g_last_try_ms < g_retry_ms) return;
        connect();
        if (!g_mqtt.connected()) {
            g_retry_ms *= 2;
            if (g_retry_ms > RETRY_MAX_MS) g_retry_ms = RETRY_MAX_MS;
        }
        return;
    }

    g_mqtt.loop();
}

bool connected() { return g_mqtt.connected(); }

uint32_t rejectedPayloads() { return g_rejected; }

}  // namespace source_mqtt
