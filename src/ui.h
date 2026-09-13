// The screen: two stacked full-width rows over a status bar.
//
// Each row shows one channel's name, its current temperature large enough to
// read across a room, its humidity, and twelve hours of history underneath.
//
// Kept separate from the sources and the channels so the layout can change
// without touching either. Everything here runs on the LVGL timer thread, so
// call these from the same context as lv_timer_handler().
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic).

#pragma once

#include <cstddef>
#include <cstdint>

#include "Channel.h"

namespace ui {

// The WIDEST a chart may be. Each row's actual column count is chosen to match
// its own channel's sample cadence and never exceeds this - see init().
constexpr size_t CHART_POINTS = 380;

constexpr uint32_t CHART_WINDOW_MS = 12UL * 60UL * 60UL * 1000UL;

// Two rows today. The layout divides the available height evenly, so a third
// channel is a row height change rather than a rewrite.
constexpr size_t MAX_ROWS = 4;

// Builds the widget tree. `channels` must outlive the UI. Call once, after
// LVGL and the display are up.
//
// `chart_points` gives each row its own column count, and getting it right is
// what makes a slow channel visible at all. A channel is drawn as a line
// between adjacent columns, and LVGL breaks that line wherever a column has no
// data. Give a channel that reports every fifteen minutes the full 380 columns
// and its 48 readings land isolated with empty columns between them, so no line
// is ever drawn. Sized to the cadence - window divided by interval - the
// columns are contiguous and the line appears. Values are clamped to
// [2, CHART_POINTS].
void init(channel::Channel** channels, const uint16_t* chart_points,
          size_t count);

// Updates every row's readout and staleness from its channel. Safe to call
// every loop; it touches the display only when a displayed value actually
// changed, so LVGL does not redraw for nothing.
void refresh(uint32_t now_ms);

// Redraws one row's chart from its channel's history. Call after a new sample
// lands, not every loop - a full downsample of twelve hours is not free.
void updateChart(size_t row);

// Connection state for the status bar.
void setStatus(bool wifi_up, bool mqtt_up);

// Celsius or Fahrenheit. Conversion happens at display time; history always
// stores Celsius.
void setFahrenheit(bool on);
bool fahrenheit();

}  // namespace ui
