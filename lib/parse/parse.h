// Payload parsing for the two data sources.
//
// Deliberately free of Arduino, ESP-IDF and LVGL headers so the parsing - the
// part most likely to be handed malformed input - is tested on the host.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic).

#pragma once

#include <cstddef>

namespace parse {

// A bare decimal ASCII payload, which is what the broker publishes: "19.6",
// "55.1", "21", "-7.5". NOT JSON.
//
// The buffer is NOT required to be null-terminated, because PubSubClient hands
// its callback a pointer into its own receive buffer with a separate length.
// Surrounding whitespace is tolerated; anything else is a rejection.
//
// Returns false and leaves `out` untouched on any malformed input, including
// trailing characters, "nan" and "inf". A rejected payload must never reach
// the chart, so silence is better than a plausible-looking wrong number.
bool decimal(const char* payload, size_t len, float& out);

}  // namespace parse
