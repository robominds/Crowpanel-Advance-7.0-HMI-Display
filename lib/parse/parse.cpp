// Implementation of the payload parsers.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic).

#include "parse.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include <ArduinoJson.h>

namespace parse {
namespace {

bool isSpace(char c) {
    return std::isspace(static_cast<unsigned char>(c)) != 0;
}

}  // namespace

bool decimal(const char* payload, size_t len, float& out) {
    if (payload == nullptr || len == 0) return false;

    // Copy into a bounded, null-terminated buffer. strtof needs a terminator
    // and the caller's buffer has none. 31 characters is far more than any
    // reading needs and keeps this off the heap.
    char buf[32];
    if (len >= sizeof(buf)) return false;
    std::memcpy(buf, payload, len);
    buf[len] = '\0';

    // Trim both ends. Leading whitespace strtof would skip anyway; trailing
    // whitespace it would leave in `end` and we would wrongly reject.
    size_t begin = 0;
    size_t stop  = len;
    while (begin < stop && isSpace(buf[begin])) ++begin;
    while (stop > begin && isSpace(buf[stop - 1])) --stop;
    if (begin == stop) return false;
    buf[stop] = '\0';

    // Every byte of the trimmed payload must be one a bare decimal can
    // contain. This rejects three things at once that strtof would otherwise
    // accept: an embedded null byte, C99 hex notation such as "0x1p0", and
    // the words "nan" and "inf".
    for (size_t i = begin; i < stop; ++i) {
        const char c = buf[i];
        const bool permitted = std::isdigit(static_cast<unsigned char>(c)) ||
                               c == '.' || c == '-' || c == '+' ||
                               c == 'e' || c == 'E';
        if (!permitted) return false;
    }

    char*       end = nullptr;
    const float v   = std::strtof(buf + begin, &end);

    if (end == buf + begin) return false;  // nothing consumed
    if (end != buf + stop) return false;   // trailing garbage
    if (!std::isfinite(v)) return false;

    out = v;
    return true;
}

int isoClockMinutes(const char* iso) {
    if (iso == nullptr) return -1;

    // Find the date/time separator rather than assuming a fixed offset: the
    // date part is always ten characters today, but anchoring on 'T' costs
    // nothing and does not care.
    const char* t = std::strchr(iso, 'T');
    if (t == nullptr) return -1;

    const char* h = t + 1;
    if (!std::isdigit(static_cast<unsigned char>(h[0])) ||
        !std::isdigit(static_cast<unsigned char>(h[1])) || h[2] != ':' ||
        !std::isdigit(static_cast<unsigned char>(h[3])) ||
        !std::isdigit(static_cast<unsigned char>(h[4]))) {
        return -1;
    }

    const int hh = (h[0] - '0') * 10 + (h[1] - '0');
    const int mm = (h[3] - '0') * 10 + (h[4] - '0');
    if (hh > 23 || mm > 59) return -1;

    return hh * 60 + mm;
}

bool openMeteo(const char* json, size_t len, Weather& out) {
    if (json == nullptr || len == 0) return false;

    // The response is around 400 bytes. 2 KB leaves generous headroom for
    // Open-Meteo adding fields without this needing a change.
    JsonDocument doc;
    if (deserializeJson(doc, json, len) != DeserializationError::Ok) return false;

    JsonVariantConst current = doc["current"];
    if (current.isNull()) return false;

    JsonVariantConst t = current["temperature_2m"];
    JsonVariantConst h = current["relative_humidity_2m"];

    // is<float>() is false for null, for a string and for a missing key, which
    // is exactly the set we want to reject.
    if (!t.is<float>() || !h.is<float>()) return false;

    out.temperature_c = t.as<float>();
    out.humidity_pct  = h.as<float>();

    // Sun times are optional. A response without them still yields a usable
    // reading: the display's job is the temperature, and the backlight simply
    // stays bright when it cannot know whether it is night.
    out.sunrise_min = -1;
    out.sunset_min  = -1;

    JsonVariantConst daily = doc["daily"];
    if (!daily.isNull()) {
        JsonVariantConst rise = daily["sunrise"][0];
        JsonVariantConst set  = daily["sunset"][0];
        if (rise.is<const char*>()) out.sunrise_min = isoClockMinutes(rise.as<const char*>());
        if (set.is<const char*>())  out.sunset_min  = isoClockMinutes(set.as<const char*>());
    }
    return true;
}

}  // namespace parse
