// The indoor temperature, from the MQTT broker.
//
// Subscribes to the two topics named by MQTT_TOPIC_TEMP and MQTT_TOPIC_HUM in
// secrets.ini, temperature in Celsius and relative humidity. The broker
// publishes bare decimal ASCII, not JSON, about every ten seconds. It accepts
// anonymous connections; there are no credentials to supply.
//
// Temperature and humidity arrive as two separate messages. A history sample is
// appended when a temperature lands, carrying the most recent humidity, so the
// two topics do not need to be synchronised. No sample is appended until both
// have been seen at least once - a chart entry showing a real temperature
// beside a humidity of zero would be worse than a slightly later first point.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic).

#pragma once

#include <cstdint>

#include "Channel.h"

namespace source_mqtt {

// Binds the channel that incoming readings are written into. The channel must
// outlive this module; in practice it is allocated once in setup() and never
// freed.
void begin(channel::Channel& indoor);

// Call every loop. Handles connection, reconnection with backoff, and message
// dispatch. Cheap when connected and idle.
void poll();

bool connected();

// Payloads rejected as malformed since boot. Surfaced so a broker that starts
// publishing something unexpected is visible rather than silently ignored.
uint32_t rejectedPayloads();

}  // namespace source_mqtt
