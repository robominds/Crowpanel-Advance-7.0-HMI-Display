// Implementation of Channel.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic).

#include "Channel.h"

namespace channel {

Channel::Channel(const char* name, uint32_t stale_after_ms, size_t capacity)
    : name_(name), stale_after_ms_(stale_after_ms), history_(capacity) {}

void Channel::update(float temperature_c, float humidity_pct, uint32_t now_ms) {
    latest_.temperature_c = temperature_c;
    latest_.humidity_pct  = humidity_pct;
    latest_.t_ms          = now_ms;
    latest_.valid         = true;

    history_.add(history::Sample{now_ms, temperature_c, humidity_pct});
}

bool Channel::isStale(uint32_t now_ms) const {
    if (!latest_.valid) return true;

    // Unsigned subtraction, so the age stays correct straight through the
    // millis() rollover at 2^32 ms (about 49.7 days) as long as the real
    // elapsed time is under that. Same reasoning as SampleHistory::downsample.
    const uint32_t age = now_ms - latest_.t_ms;
    return age > stale_after_ms_;
}

}  // namespace channel
