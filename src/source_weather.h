// The Maple Valley outdoor temperature, from Open-Meteo.
//
// Deliberately plain HTTP, not HTTPS. Open-Meteo serves this endpoint over HTTP
// with a 200 and no redirect, which removes the TLS stack from the firmware.
// That saves roughly 40 KB of heap the frame buffers want, and removes the
// certificate expiry that silently kills embedded HTTPS clients about a year
// after they are flashed. There is nothing secret in a public weather reading.
//
// No API key. Open-Meteo's current-conditions data updates every fifteen
// minutes, so polling faster only wastes requests.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic).

#pragma once

#include <cstdint>

#include "Channel.h"

namespace source_weather {

// Binds the channel that readings are written into. The channel must outlive
// this module.
void begin(channel::Channel& outdoor);

// Call every loop. Fetches at most once per interval; cheap otherwise.
void poll();

// Consecutive failed fetches. Non-zero means the last attempt did not produce a
// usable reading, whether from the network or from the response body.
uint32_t failures();

}  // namespace source_weather
