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
#include <ctime>

#include "Channel.h"

namespace ui {

// The WIDEST a chart may be. Each row's actual column count is chosen to match
// its own channel's sample cadence and never exceeds this - see init().
constexpr size_t CHART_POINTS = 380;

constexpr uint32_t CHART_WINDOW_MS = 12UL * 60UL * 60UL * 1000UL;

// Two rows today. The layout divides the available height evenly, so a third
// channel is a row height change rather than a rewrite.
constexpr size_t MAX_ROWS = 4;

// Which of the two layouts is on screen.
//
//   Charts  two stacked channel rows with twelve-hour charts, over a status bar
//   Clock   the time in large digits, with each channel's reading beneath it
//
// Both trees are built once and live for the life of the program; switching
// hides one and shows the other. That keeps the charts current while they are
// out of sight, so coming back is instant rather than a rebuild.
enum class View { Charts, Clock };

void setView(View v);
void toggleView();
View view();

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

// Relabels the time axis from the wall clock, so it reads 07:25, 10:25, 13:25
// and so on rather than -12h, -9h, -6h. `now` is a local-time epoch and marks
// the right-hand edge of the window.
//
// Pass have_time false when the clock is not known, and the axis reverts to the
// relative labels. An axis that invents a plausible-looking time is worse than
// one that admits it does not know.
//
// Cheap to call repeatedly: it rewrites the labels only when the displayed
// minute actually changes.
void setClock(bool have_time, time_t now);

// Clock-view palette: white through the day, red at night. Driven by the same
// sunrise and sunset times as the backlight policy, so colour and brightness
// change together on the minute. Safe to call every loop - it repaints only
// when the state actually flips.
void setNightMode(bool on);

// Celsius or Fahrenheit. Conversion happens at display time; history always
// stores Celsius.
void setFahrenheit(bool on);
bool fahrenheit();

}  // namespace ui
