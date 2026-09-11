// One data channel: where a reading came from, what it says now, whether it can
// still be trusted, and its history.
//
// Each channel carries its own staleness threshold because the two sources
// differ by two orders of magnitude in cadence. The office publishes every ten
// seconds, so a minute of silence means something broke. Maple Valley updates
// every fifteen minutes, so a minute of silence is normal. A single shared
// timeout would either cry wolf on the weather or hide a dead broker.
//
// Free of Arduino, ESP-IDF and LVGL headers so the staleness logic is tested on
// the host. Time arrives as a millis()-style uint32_t the caller supplies.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic).

#pragma once

#include <cstddef>
#include <cstdint>

#include "SampleHistory.h"

namespace channel {

struct Reading {
    float    temperature_c = 0.0f;
    float    humidity_pct  = 0.0f;
    uint32_t t_ms          = 0;
    bool     valid         = false;  // false until the first update
};

class Channel {
  public:
    // `name` must outlive the Channel; in practice it is a string literal.
    // `capacity` of 0 is legal and yields a channel that tracks the latest
    // reading but retains no history.
    Channel(const char* name, uint32_t stale_after_ms, size_t capacity);

    const char* name() const { return name_; }
    uint32_t    staleAfterMs() const { return stale_after_ms_; }

    // Records a new reading and appends it to history. `now_ms` is the
    // millis() value at which the reading was taken.
    void update(float temperature_c, float humidity_pct, uint32_t now_ms);

    const Reading& latest() const { return latest_; }

    // True when the reading should no longer be shown as live. Always true
    // before the first update: showing an uninitialised zero as a measurement
    // is worse than showing an obvious gap.
    bool isStale(uint32_t now_ms) const;

    history::SampleHistory&       history() { return history_; }
    const history::SampleHistory& history() const { return history_; }

  private:
    const char*             name_;
    uint32_t                stale_after_ms_;
    Reading                 latest_;
    history::SampleHistory  history_;
};

}  // namespace channel
