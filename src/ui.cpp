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

float toDisplay(float celsius) {
    return g_fahrenheit ? (celsius * 9.0f / 5.0f + 32.0f) : celsius;
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

    row.chart = lv_chart_create(row.panel);
    lv_obj_set_size(row.chart, board::LCD_WIDTH - 32, height - 76);
    lv_obj_align(row.chart, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_chart_set_type(row.chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(row.chart, CHART_POINTS);
    lv_chart_set_update_mode(row.chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_div_line_count(row.chart, 3, 0);
    lv_obj_set_style_size(row.chart, 0, 0, LV_PART_INDICATOR);  // no point dots
    row.series = lv_chart_add_series(row.chart, lv_palette_main(LV_PALETTE_BLUE),
                                     LV_CHART_AXIS_PRIMARY_Y);
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
}

}  // namespace

void init(channel::Channel** channels, size_t count) {
    if (count > MAX_ROWS) count = MAX_ROWS;
    g_row_count = count;

    const int usable = board::LCD_HEIGHT - STATUS_H;
    const int height = usable / static_cast<int>(count);

    for (size_t i = 0; i < count; ++i) {
        g_rows[i].ch = channels[i];
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

    const size_t valid =
        row.ch->history().downsample(CHART_WINDOW_MS, g_buckets, CHART_POINTS);
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
    lv_chart_set_range(row.chart, LV_CHART_AXIS_PRIMARY_Y,
                       static_cast<int32_t>(lo_d - 1.0f),
                       static_cast<int32_t>(hi_d + 1.0f));

    for (size_t i = 0; i < CHART_POINTS; ++i) {
        if (g_buckets[i].valid) {
            lv_chart_set_value_by_id(
                row.chart, row.series, static_cast<uint32_t>(i),
                static_cast<int32_t>(toDisplay(g_buckets[i].temperature_c)));
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
