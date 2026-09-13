// Backlight brightness, dimmed between sunset and sunrise.
//
// The sun times come from the same Open-Meteo request that supplies the outdoor
// reading, and the comparison is made against the board's own clock. That is
// deliberately not the API's `is_day` flag, which would also have worked: the
// times let the change land on the exact minute rather than whenever the next
// fifteen-minute poll happens to arrive, and they keep working with the network
// down, because this board has a real-time clock that survives a power cut.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic).

#pragma once

#include <cstdint>

namespace brightness {

// Percentages, not raw backlight bytes - panel_mcu owns the revision's encoding.
//
// These are LED current, not perceived brightness: eyes are roughly
// logarithmic, so halving the current looks closer to seventy percent as
// bright. Tried in order on the real panel: 50 was barely distinguishable from
// full, exactly as that curve predicts; 25 was clearly dimmer; 10 dimmer again.
// 5 is the fourth attempt.
constexpr uint8_t DAY_PERCENT   = 100;
constexpr uint8_t NIGHT_PERCENT = 5;

// Today's sun times, as minutes since local midnight, or -1 for unknown.
// Called whenever a weather response lands.
void setSunTimes(int sunrise_min, int sunset_min);

// Call every loop. Re-evaluates at most once a minute and writes the backlight
// ONLY when the level actually changes - that I2C bus is shared with the touch
// controller and the panel's companion MCU, and needless traffic on it is what
// makes the display jitter.
//
// `have_clock` false, or sun times unknown, means full brightness. A panel that
// is mysteriously dim is worse than one that is too bright, and "I do not know
// what time it is" should not express itself as a dark screen.
void poll(bool have_clock);

// The level last written, for logging and tests.
uint8_t current();

}  // namespace brightness
