// Implementation of the day/night backlight policy.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic).

#include "brightness.h"

#include <Arduino.h>

#include <ctime>

#include "panel_mcu.h"

namespace brightness {
namespace {

constexpr uint32_t EVALUATE_EVERY_MS = 10000;

int      g_sunrise_min = -1;
int      g_sunset_min  = -1;
uint8_t  g_level       = DAY_PERCENT;
bool     g_written     = false;
uint32_t g_last_eval_ms = 0;

// Daylight is the span between sunrise and sunset. The wrapped case - sunset
// earlier in the day than sunrise - cannot happen at these latitudes, but
// handling it costs one branch and turns a silent inversion into correct
// behaviour if this is ever pointed somewhere polar.
bool isDaytime(int now_min, int sunrise_min, int sunset_min) {
    if (sunrise_min <= sunset_min) {
        return now_min >= sunrise_min && now_min < sunset_min;
    }
    return now_min >= sunrise_min || now_min < sunset_min;
}

}  // namespace

void setSunTimes(int sunrise_min, int sunset_min) {
    if (sunrise_min == g_sunrise_min && sunset_min == g_sunset_min) return;
    g_sunrise_min = sunrise_min;
    g_sunset_min  = sunset_min;

    if (sunrise_min < 0 || sunset_min < 0) {
        Serial.println("brightness: no sun times; staying at full brightness");
        return;
    }
    Serial.printf("brightness: sunrise %02d:%02d, sunset %02d:%02d\n",
                  sunrise_min / 60, sunrise_min % 60, sunset_min / 60,
                  sunset_min % 60);
    g_last_eval_ms = 0;  // re-evaluate now rather than on the next tick
}

void poll(bool have_clock) {
    const uint32_t now_ms = millis();
    if (g_written && (now_ms - g_last_eval_ms) < EVALUATE_EVERY_MS) return;
    g_last_eval_ms = now_ms;

    uint8_t want = DAY_PERCENT;
    if (have_clock && g_sunrise_min >= 0 && g_sunset_min >= 0) {
        const time_t now = time(nullptr);
        struct tm    lt;
        localtime_r(&now, &lt);
        const int now_min = lt.tm_hour * 60 + lt.tm_min;
        want = isDaytime(now_min, g_sunrise_min, g_sunset_min) ? DAY_PERCENT
                                                              : NIGHT_PERCENT;
    }

    if (g_written && want == g_level) return;

    g_level   = want;
    g_written = true;
    panel_mcu::backlightLevel(g_level);
    Serial.printf("brightness: %u%%\n", static_cast<unsigned>(g_level));
}

uint8_t current() { return g_level; }

// Derived from the level actually written rather than re-deriving the hour:
// one source of truth, and it inherits the "unknown means day" caution above.
bool isNight() { return g_written && g_level == NIGHT_PERCENT; }

}  // namespace brightness
