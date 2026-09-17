// The AHT10's six-byte measurement frame, converted and sanity-checked.
//
// No Arduino, Wire or LVGL includes: this compiles for the host so the
// arithmetic is tested without hardware. src/source_aht10 owns the I2C.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic).

#pragma once

#include <cstddef>
#include <cstdint>

namespace aht10 {

// The part has one fixed address. Nothing else on this board's bus uses it:
// the companion MCU is 0x30, the real-time clock 0x51, touch 0x5D.
constexpr uint8_t ADDRESS = 0x38;

// Status byte flags, as the datasheet numbers them.
constexpr uint8_t STATUS_BUSY       = 0x80;
constexpr uint8_t STATUS_CALIBRATED = 0x08;

struct Sample {
    float temperature_c;
    float humidity_pct;
};

// Converts one frame: status, then a 20-bit humidity and a 20-bit temperature
// sharing the middle byte's nibbles.
//
// Returns false, leaving `out` untouched, when the frame cannot be a real
// reading: fewer than six bytes, the busy or uncalibrated status, the all-zero
// frame an absent sensor leaves on the bus, the all-ones frame a disconnected
// SDA line reads, or a value outside the part's own range (-40 to 85 C,
// 0 to 100 %RH). The AHT10 sends no CRC - that is an AHT20 feature - so these
// checks are the only defence against a garbled read.
bool convert(const uint8_t* frame, size_t len, Sample& out);

}  // namespace aht10
