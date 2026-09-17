// Implementation of the AHT10 frame conversion. See aht10.h.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic).

#include "aht10.h"

namespace aht10 {
namespace {

constexpr float FULL_SCALE = 1048576.0f;   // 2^20

constexpr float TEMP_MIN_C = -40.0f;
constexpr float TEMP_MAX_C = 85.0f;

constexpr uint32_t FIELD_MAX = 0xFFFFF;

}  // namespace

bool convert(const uint8_t* frame, size_t len, Sample& out) {
    if (frame == nullptr || len < 6) return false;

    const uint8_t status = frame[0];
    if ((status & STATUS_BUSY) != 0) return false;
    if ((status & STATUS_CALIBRATED) == 0) return false;

    const uint32_t humidity_raw = (static_cast<uint32_t>(frame[1]) << 12) |
                                  (static_cast<uint32_t>(frame[2]) << 4) |
                                  (static_cast<uint32_t>(frame[3]) >> 4);
    const uint32_t temperature_raw =
        (static_cast<uint32_t>(frame[3] & 0x0F) << 16) |
        (static_cast<uint32_t>(frame[4]) << 8) | static_cast<uint32_t>(frame[5]);

    // An absent sensor leaves the bus at zero; a disconnected data line reads
    // as ones. Either value is theoretically in range, so the frame as a whole
    // is what rules them out.
    if (humidity_raw == 0 && temperature_raw == 0) return false;
    if (humidity_raw == FIELD_MAX && temperature_raw == FIELD_MAX) return false;

    const float humidity = static_cast<float>(humidity_raw) * 100.0f / FULL_SCALE;
    const float temperature =
        static_cast<float>(temperature_raw) * 200.0f / FULL_SCALE - 50.0f;

    if (humidity < 0.0f || humidity > 100.0f) return false;
    if (temperature < TEMP_MIN_C || temperature > TEMP_MAX_C) return false;

    out.humidity_pct  = humidity;
    out.temperature_c = temperature;
    return true;
}

}  // namespace aht10
