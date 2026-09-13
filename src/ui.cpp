// Implementation of the screen.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic).

#include "ui.h"

#include <lvgl.h>

#include <cmath>
#include <cstdio>

#include "board_pins.h"

namespace ui {
namespace {

constexpr int STATUS_H = 40;

struct Row {
    channel::Channel* ch        = nullptr;
    lv_obj_t*         panel     = nullptr;
    lv_obj_t*         name      = nullptr;
    lv_obj_t*         value     = nullptr;
    lv_obj_t*         humidity  = nullptr;
    lv_obj_t*         note      = nullptr;   // shown only when stale
    lv_obj_t*         chart     = nullptr;
    lv_chart_series_t* series   = nullptr;
    lv_obj_t*         y_scale   = nullptr;   // degrees, left of the chart
    lv_obj_t*         x_scale   = nullptr;   // time, below the chart
    size_t            points    = CHART_POINTS;

    // Last values actually written to the widgets, so refresh() can skip LVGL
    // calls when nothing changed.
    float last_temp_c = NAN;
    float last_hum    = NAN;
    bool  last_stale  = true;
    bool  last_valid  = false;
    bool  primed      = false;
};

Row    g_rows[MAX_ROWS];
size_t g_row_count = 0;

lv_obj_t* g_status_wifi = nullptr;
lv_obj_t* g_status_mqtt = nullptr;

// Last connection state actually written to the status bar. `primed` false
// means nothing has been written yet, so the first setStatus() call always
// draws whatever the initial "wifi ..." / "mqtt ..." placeholders should
// become.
bool g_last_wifi_up  = false;
bool g_last_mqtt_up  = false;
bool g_status_primed = false;

bool g_fahrenheit = true;

history::SampleHistory::Bucket g_buckets[CHART_POINTS];

// The time axis is fixed: every chart shows the same twelve-hour window, with
// "now" at the right-hand edge where the live trace arrives. LVGL keeps this
// pointer rather than copying, so it has to outlive every call.
const char* kTimeLabels[] = {"-12h", "-9h", "-6h", "-3h", "now", nullptr};

constexpr int Y_AXIS_W = 68;   // room for the degree labels and their ticks
constexpr int X_AXIS_H = 36;   // room for the time labels and their ticks

// The last time label, "now", is centred on the final tick at the chart's right
// edge, so without this margin half of it renders off the panel.
constexpr int RIGHT_MARGIN = 26;

// A scale places each label a fixed 15 px beyond the END of its tick, and that
// gap is not stylable. Tick length is what actually controls how far the labels
// sit from the axis line, so these do double duty: they make the ticks read as
// a proper axis, and they hold the numbers clear of it.
constexpr int MAJOR_TICK_LEN = 12;
constexpr int MINOR_TICK_LEN = 6;

void styleScale(lv_obj_t* scale) {
    lv_obj_set_style_length(scale, MAJOR_TICK_LEN, LV_PART_INDICATOR);
    lv_obj_set_style_length(scale, MINOR_TICK_LEN, LV_PART_ITEMS);
    lv_obj_set_style_line_width(scale, 2, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(scale, 1, LV_PART_ITEMS);
    lv_obj_set_style_text_font(scale, &lv_font_montserrat_16, LV_PART_INDICATOR);
}

float toDisplay(float celsius) {
    return g_fahrenheit ? (celsius * 9.0f / 5.0f + 32.0f) : celsius;
}

// lv_chart works in integers. Everything handed to a chart - both the series
// values and the axis range - is therefore multiplied by this, giving tenths of
// a degree instead of whole degrees. Without it an office that drifts two
// degrees over twelve hours draws as a two-step staircase. Remove the factor
// from one of the two and the trace leaves the visible range entirely, so keep
// them in step.
constexpr float CHART_SCALE = 10.0f;

int32_t toChart(float display_value) {
    // Round, not truncate: truncation biases every point toward zero, which on
    // a Fahrenheit trace is a consistent tenth-of-a-degree downward shift.
    return static_cast<int32_t>(std::lroundf(display_value * CHART_SCALE));
}

void buildRow(Row& row, int y, int height) {
    row.panel = lv_obj_create(lv_screen_active());
    lv_obj_set_pos(row.panel, 0, y);
    lv_obj_set_size(row.panel, board::LCD_WIDTH, height);
    lv_obj_set_style_radius(row.panel, 0, 0);
    lv_obj_set_style_border_width(row.panel, 0, 0);
    lv_obj_set_style_pad_all(row.panel, 8, 0);
    lv_obj_remove_flag(row.panel, LV_OBJ_FLAG_SCROLLABLE);

    row.name = lv_label_create(row.panel);
    lv_obj_set_style_text_font(row.name, &lv_font_montserrat_20, 0);
    lv_label_set_text(row.name, row.ch->name());
    lv_obj_align(row.name, LV_ALIGN_TOP_LEFT, 0, 0);

    row.value = lv_label_create(row.panel);
    lv_obj_set_style_text_font(row.value, &lv_font_montserrat_48, 0);
    lv_label_set_text(row.value, "--");
    lv_obj_align(row.value, LV_ALIGN_TOP_LEFT, 260, -6);

    row.humidity = lv_label_create(row.panel);
    lv_obj_set_style_text_font(row.humidity, &lv_font_montserrat_28, 0);
    lv_label_set_text(row.humidity, "--");
    lv_obj_align(row.humidity, LV_ALIGN_TOP_LEFT, 470, 8);

    row.note = lv_label_create(row.panel);
    lv_obj_set_style_text_font(row.note, &lv_font_montserrat_16, 0);
    lv_label_set_text(row.note, "");
    lv_obj_align(row.note, LV_ALIGN_TOP_RIGHT, 0, 4);

    const int chart_w = board::LCD_WIDTH - 32 - Y_AXIS_W - RIGHT_MARGIN;
    const int chart_h = height - 76 - X_AXIS_H;

    row.chart = lv_chart_create(row.panel);
    lv_obj_set_size(row.chart, chart_w, chart_h);
    lv_obj_align(row.chart, LV_ALIGN_BOTTOM_RIGHT, -RIGHT_MARGIN, -X_AXIS_H);
    lv_chart_set_type(row.chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(row.chart, static_cast<uint32_t>(row.points));

    // Five horizontal and five vertical divisions, matching the five major
    // ticks on each scale, so the gridlines actually line up with the labels.
    lv_chart_set_div_line_count(row.chart, 5, 5);

    // A sparse series needs visible markers. Maple Valley contributes one
    // reading every fifteen minutes, so for the first hours of uptime its trace
    // is a few isolated points with nothing to draw a line between - without
    // dots there is simply nothing on screen. A dense series would be a solid
    // smear of dots, so it gets none.
    const int dot = (row.points <= 64) ? 5 : 0;
    lv_obj_set_style_size(row.chart, dot, dot, LV_PART_INDICATOR);

    row.series = lv_chart_add_series(row.chart, lv_palette_main(LV_PALETTE_BLUE),
                                     LV_CHART_AXIS_PRIMARY_Y);

    // LVGL 9 removed the chart's built-in axis ticks, so the scales are
    // separate widgets placed against the chart's edges. The vertical one's
    // range is re-set on every redraw to follow the data; the horizontal one is
    // fixed, because the window always covers the same twelve hours.
    row.y_scale = lv_scale_create(row.panel);
    lv_scale_set_mode(row.y_scale, LV_SCALE_MODE_VERTICAL_LEFT);
    lv_obj_set_size(row.y_scale, Y_AXIS_W, chart_h);
    lv_obj_align_to(row.y_scale, row.chart, LV_ALIGN_OUT_LEFT_MID, 0, 0);
    lv_scale_set_total_tick_count(row.y_scale, 9);
    lv_scale_set_major_tick_every(row.y_scale, 2);
    lv_scale_set_label_show(row.y_scale, true);
    styleScale(row.y_scale);

    row.x_scale = lv_scale_create(row.panel);
    lv_scale_set_mode(row.x_scale, LV_SCALE_MODE_HORIZONTAL_BOTTOM);
    lv_obj_set_size(row.x_scale, chart_w, X_AXIS_H);
    lv_obj_align_to(row.x_scale, row.chart, LV_ALIGN_OUT_BOTTOM_MID, 0, 2);
    lv_scale_set_total_tick_count(row.x_scale, 13);
    lv_scale_set_major_tick_every(row.x_scale, 3);
    lv_scale_set_label_show(row.x_scale, true);
    lv_scale_set_text_src(row.x_scale, kTimeLabels);
    styleScale(row.x_scale);
}

void buildStatusBar() {
    lv_obj_t* bar = lv_obj_create(lv_screen_active());
    lv_obj_set_pos(bar, 0, board::LCD_HEIGHT - STATUS_H);
    lv_obj_set_size(bar, board::LCD_WIDTH, STATUS_H);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 6, 0);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    g_status_wifi = lv_label_create(bar);
    lv_obj_set_style_text_font(g_status_wifi, &lv_font_montserrat_16, 0);
    lv_label_set_text(g_status_wifi, "wifi ...");
    lv_obj_align(g_status_wifi, LV_ALIGN_LEFT_MID, 0, 0);

    g_status_mqtt = lv_label_create(bar);
    lv_obj_set_style_text_font(g_status_mqtt, &lv_font_montserrat_16, 0);
    lv_label_set_text(g_status_mqtt, "mqtt ...");
    lv_obj_align(g_status_mqtt, LV_ALIGN_LEFT_MID, 140, 0);

    lv_obj_t* window = lv_label_create(bar);
    lv_obj_set_style_text_font(window, &lv_font_montserrat_16, 0);
    lv_label_set_text(window, "charts: last 12 h");
    lv_obj_align(window, LV_ALIGN_RIGHT_MID, 0, 0);
}

}  // namespace

void init(channel::Channel** channels, const uint16_t* chart_points,
          size_t count) {
    if (count > MAX_ROWS) count = MAX_ROWS;
    g_row_count = count;

    const int usable = board::LCD_HEIGHT - STATUS_H;
    const int height = usable / static_cast<int>(count);

    for (size_t i = 0; i < count; ++i) {
        g_rows[i].ch = channels[i];

        size_t p = chart_points[i];
        if (p < 2) p = 2;
        if (p > CHART_POINTS) p = CHART_POINTS;
        g_rows[i].points = p;

        buildRow(g_rows[i], static_cast<int>(i) * height, height);
    }
    buildStatusBar();
}

void refresh(uint32_t now_ms) {
    char buf[32];

    for (size_t i = 0; i < g_row_count; ++i) {
        Row& row = g_rows[i];
        const channel::Reading& r = row.ch->latest();
        const bool stale = row.ch->isStale(now_ms);

        const bool changed = !row.primed ||
                             stale != row.last_stale ||
                             r.valid != row.last_valid ||
                             r.temperature_c != row.last_temp_c ||
                             r.humidity_pct != row.last_hum;
        if (!changed) continue;

        row.primed      = true;
        row.last_stale  = stale;
        row.last_valid  = r.valid;
        row.last_temp_c = r.temperature_c;
        row.last_hum    = r.humidity_pct;

        if (!r.valid) {
            lv_label_set_text(row.value, "--");
            lv_label_set_text(row.humidity, "--");
            lv_label_set_text(row.note, "waiting");
        } else {
            snprintf(buf, sizeof(buf), "%.1f%s", toDisplay(r.temperature_c),
                     g_fahrenheit ? " F" : " C");
            lv_label_set_text(row.value, buf);

            snprintf(buf, sizeof(buf), "%.0f%% RH", r.humidity_pct);
            lv_label_set_text(row.humidity, buf);

            lv_label_set_text(row.note, stale ? "stale" : "");
        }

        // A stale number that looks live is worse than an obvious gap, so dim
        // the whole row rather than relying on the word alone being noticed.
        const lv_opa_t opa = (stale || !r.valid) ? LV_OPA_40 : LV_OPA_COVER;
        lv_obj_set_style_text_opa(row.value, opa, 0);
        lv_obj_set_style_text_opa(row.humidity, opa, 0);
    }
}

void updateChart(size_t row_index) {
    if (row_index >= g_row_count) return;
    Row& row = g_rows[row_index];

    const size_t pts = row.points;
    const size_t valid =
        row.ch->history().downsample(CHART_WINDOW_MS, g_buckets, pts);
    if (valid == 0) return;

    float lo = 0.0f, hi = 0.0f;
    if (!row.ch->history().temperatureRange(lo, hi)) return;

    // A flat trace against an auto-scaled axis turns sensor noise into drama.
    // Two degrees of headroom keeps a steady reading looking steady.
    float lo_d = toDisplay(lo);
    float hi_d = toDisplay(hi);
    if (hi_d - lo_d < 2.0f) {
        const float mid = (hi_d + lo_d) / 2.0f;
        lo_d = mid - 1.0f;
        hi_d = mid + 1.0f;
    }
    // Snap the axis to whole degrees, with a span divisible by four so the five
    // major ticks land on round numbers. An axis labelled 63, 64, 65 reads at a
    // glance; one labelled 62.7, 64.1, 65.4 does not, and this panel is meant
    // to be read across a room.
    int lo_i = static_cast<int>(std::floor(lo_d - 1.0f));
    int hi_i = static_cast<int>(std::ceil(hi_d + 1.0f));
    int span = hi_i - lo_i;
    if (span < 4) {
        hi_i = lo_i + 4;
        span = 4;
    }
    if (const int rem = span % 4) hi_i += (4 - rem);

    lv_chart_set_range(row.chart, LV_CHART_AXIS_PRIMARY_Y,
                       toChart(static_cast<float>(lo_i)),
                       toChart(static_cast<float>(hi_i)));
    lv_scale_set_range(row.y_scale, lo_i, hi_i);

    for (size_t i = 0; i < pts; ++i) {
        if (g_buckets[i].valid) {
            lv_chart_set_value_by_id(row.chart, row.series,
                                     static_cast<uint32_t>(i),
                                     toChart(toDisplay(g_buckets[i].temperature_c)));
        } else {
            // A gap is drawn as a gap. The Maple Valley trace legitimately has
            // only about 48 real points across twelve hours; interpolating them
            // into smoothness would invent data the API never sent.
            lv_chart_set_value_by_id(row.chart, row.series,
                                     static_cast<uint32_t>(i),
                                     LV_CHART_POINT_NONE);
        }
    }
    lv_chart_refresh(row.chart);
}

void setStatus(bool wifi_up, bool mqtt_up) {
    // main.cpp calls this every loop iteration, and lv_label_set_text()
    // invalidates the label before it compares anything - so without this guard
    // both labels would be dirty on every refresh for the life of the device
    // and the panel would never get an idle frame. Same shape as refresh().
    if (g_status_primed && wifi_up == g_last_wifi_up && mqtt_up == g_last_mqtt_up) {
        return;
    }
    g_status_primed = true;
    g_last_wifi_up  = wifi_up;
    g_last_mqtt_up  = mqtt_up;

    if (g_status_wifi != nullptr) {
        lv_label_set_text(g_status_wifi, wifi_up ? "wifi ok" : "wifi down");
    }
    if (g_status_mqtt != nullptr) {
        lv_label_set_text(g_status_mqtt, mqtt_up ? "mqtt ok" : "mqtt down");
    }
}

void setFahrenheit(bool on) {
    if (on == g_fahrenheit) return;
    g_fahrenheit = on;
    for (size_t i = 0; i < g_row_count; ++i) {
        g_rows[i].primed = false;  // force the readouts to rewrite
        updateChart(i);
    }
}

bool fahrenheit() { return g_fahrenheit; }

}  // namespace ui
