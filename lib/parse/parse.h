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

// One current-conditions reading from Open-Meteo, plus the day's sun times
// when the response carried them.
struct Weather {
    float temperature_c = 0.0f;
    float humidity_pct  = 0.0f;

    // Minutes since local midnight, or -1 when the response did not include
    // them or they did not parse. Open-Meteo returns these already converted to
    // the requested timezone, so they compare directly against a local clock.
    int sunrise_min = -1;
    int sunset_min  = -1;
};

// Minutes since midnight from an ISO 8601 local timestamp such as
// "2026-09-12T06:41". Returns -1 for anything that is not one, including an
// out-of-range hour or minute. Exposed separately because it is the fiddly
// part and deserves its own tests.
int isoClockMinutes(const char* iso);

// Extracts `current.temperature_2m` and `current.relative_humidity_2m` from an
// Open-Meteo response body.
//
// Temperature is Celsius because the request omits `temperature_unit`, whose
// default is Celsius. Keep it that way: history stores Celsius throughout and
// converts only at display time.
//
// Returns false and leaves `out` untouched unless BOTH fields were present and
// numeric. A partially parsed reading is worse than none - it would show a real
// temperature beside a humidity of zero, with nothing to indicate which is
// which. Also returns false for the API's error body, which has no `current`.
bool openMeteo(const char* json, size_t len, Weather& out);

}  // namespace parse
