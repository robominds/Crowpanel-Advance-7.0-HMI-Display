// Temperature display for the Elecrow CrowPanel Advance 7.0-HMI.
//
// Shows the outdoor temperature for Maple Valley, from Open-Meteo, above the
// indoor temperature for Mark's office, from an MQTT broker, each with twelve
// hours of history.
//
// Startup order matters on this board. The backlight is an I2C command to a
// companion microcontroller, and it goes on LAST - after the panel is scanning
// and the first frame is drawn. See docs/HARDWARE.md.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic).

#include <Arduino.h>
#include <Wire.h>
#include <ota.h>

#include <new>

#include "Channel.h"
#include "board_pins.h"
#include "brightness.h"
#include "display_driver.h"
#include "net.h"
#include "rtc.h"
#include "panel_mcu.h"
#include "source_mqtt.h"
#include "source_weather.h"
#include "touch.h"
#include "ui.h"
#include "update_overlay.h"

namespace {

// Expected publish intervals, used only to size the ring buffers.
constexpr uint32_t OFFICE_INTERVAL_MS  = 10UL * 1000UL;
constexpr uint32_t WEATHER_INTERVAL_MS = 15UL * 60UL * 1000UL;

// Per-channel staleness. The office publishes every ten seconds, so a minute of
// silence means something broke. Maple Valley updates every fifteen minutes, so
// a minute of silence is normal.
constexpr uint32_t OFFICE_STALE_MS  = 60UL * 1000UL;
constexpr uint32_t WEATHER_STALE_MS = 45UL * 60UL * 1000UL;

// Capacity is the window divided by the interval, plus slack so the chart's
// window is always fully covered rather than starved at the left edge.
constexpr size_t OFFICE_CAPACITY =
    (ui::CHART_WINDOW_MS / OFFICE_INTERVAL_MS) + 64;
constexpr size_t WEATHER_CAPACITY =
    (ui::CHART_WINDOW_MS / WEATHER_INTERVAL_MS) + 8;

// Two channels: [0] outdoor, [1] office. Deliberately pointers filled in by
// setup() rather than file-scope objects. Each Channel constructor allocates
// its whole history up front - about 52 KB for the office - and a file-scope
// object would do that during static initialisation, before Serial.begin().
// On a module that does not carry the memory the schematic claims, that failure
// aborts and reboots the board with nothing printed at all. Constructed inside
// setup() the same failure is at least explicable.
constexpr size_t  CHANNEL_COUNT           = 2;
channel::Channel* g_channels[CHANNEL_COUNT] = {nullptr, nullptr};

// Each chart gets a column count matched to its channel's cadence. This is not
// cosmetic: a chart draws a line between adjacent columns, and LVGL breaks that
// line wherever a column holds no data. Maple Valley contributes 48 readings in
// twelve hours, so spread across 380 columns every one of them sits isolated
// with empty columns either side and no line is ever drawn - which is exactly
// what the first hardware bring-up showed. At 48 columns they are contiguous.
//
// Same order as g_channels: outdoor first, office second.
constexpr uint16_t WEATHER_CHART_POINTS =
    static_cast<uint16_t>(ui::CHART_WINDOW_MS / WEATHER_INTERVAL_MS);
constexpr uint16_t OFFICE_CHART_POINTS = static_cast<uint16_t>(ui::CHART_POINTS);

const uint16_t g_chart_points[CHANNEL_COUNT] = {WEATHER_CHART_POINTS,
                                                OFFICE_CHART_POINTS};

// Shows push updates on top of both views.
UpdateOverlay g_update_overlay;

// Long press, deliberately not a button: the panel is meant to be read, not
// operated, and a stray brush against the glass should change nothing.
//
// Where the press STARTS decides what it does. The top-right corner switches
// between the chart view and the clock view; anywhere else toggles Celsius and
// Fahrenheit, in either view.
constexpr uint32_t LONG_PRESS_HOLD_MS = 1000;

// A fifth of the width and a quarter of the height. Big enough to hit on
// purpose without looking, small enough that reaching across the glass for the
// unit toggle does not land in it by accident.
constexpr int16_t CORNER_W = 160;
constexpr int16_t CORNER_H = 120;

// Local time for the chart axis. Maple Valley is America/Los_Angeles, matching
// the coordinates the weather source asks about. A POSIX rule rather than a
// zone name so no timezone database has to be carried.
#ifndef DISPLAY_TZ
#define DISPLAY_TZ "PST8PDT,M3.2.0,M11.1.0"
#endif

void pollLongPress() {
    static uint32_t press_started_ms = 0;
    static bool     handled          = false;
    static int16_t  start_x          = 0;
    static int16_t  start_y          = 0;

    if (!touch_pressed()) {
        press_started_ms = 0;
        handled          = false;
        return;
    }

    const uint32_t now = millis();
    if (press_started_ms == 0) {
        press_started_ms = now;
        // Latch where the press BEGAN. Reading the live position instead would
        // let a finger that drifts during the hold change which action fires.
        start_x = touch_x();
        start_y = touch_y();
        return;
    }

    if (handled || (now - press_started_ms) < LONG_PRESS_HOLD_MS) return;
    handled = true;

    const bool in_corner = (start_x >= board::LCD_WIDTH - CORNER_W) &&
                           (start_y < CORNER_H);

    if (in_corner) {
        ui::toggleView();
        Serial.printf("view: %s\n",
                      ui::view() == ui::View::Clock ? "clock" : "charts");
    } else {
        ui::setFahrenheit(!ui::fahrenheit());
        Serial.printf("units: %s\n", ui::fahrenheit() ? "F" : "C");
    }
}


// Redraws a row's chart when its channel has gained a sample. Keyed on the
// newest reading's TIMESTAMP, not on history size: size stops changing once the
// ring buffer fills, which would silently freeze every chart about twelve hours
// after boot. Inequality rather than ordering, so it survives the millis()
// rollover.
void pollCharts() {
    static uint32_t last_t_ms[CHANNEL_COUNT] = {0};
    static bool     seen[CHANNEL_COUNT]      = {false};

    for (size_t i = 0; i < CHANNEL_COUNT; ++i) {
        const channel::Reading& r = g_channels[i]->latest();
        if (!r.valid) continue;
        if (seen[i] && r.t_ms == last_t_ms[i]) continue;
        seen[i]      = true;
        last_t_ms[i] = r.t_ms;
        ui::updateChart(i);
    }
}

}  // namespace

void setup() {
    Serial.begin(115200);
    // Serial reaches the host through a CH340K on GPIO43/44, not native USB.
    // Nothing to wait for, so no while(!Serial) here - that would hang forever.
    delay(200);
    Serial.printf("\nCrowPanel Advance 7.0 temperature display %s\n", FW_VERSION);

    // Now that there is somewhere to complain to, build the channels. The
    // nothrow new covers the Channel objects themselves; if the far larger
    // history buffer inside one cannot be allocated the C++ runtime aborts, but
    // with Serial already up that abort is visible on the console instead of
    // being a silent reboot loop.
    g_channels[0] = new (std::nothrow)
        channel::Channel("Maple Valley", WEATHER_STALE_MS, WEATHER_CAPACITY);
    g_channels[1] = new (std::nothrow)
        channel::Channel("Mark's Office", OFFICE_STALE_MS, OFFICE_CAPACITY);
    if (g_channels[0] == nullptr || g_channels[1] == nullptr) {
        Serial.println("FATAL: out of memory allocating the channel histories");
        Serial.printf("FATAL: needed room for %u + %u samples (~%u KB)\n",
                      static_cast<unsigned>(WEATHER_CAPACITY),
                      static_cast<unsigned>(OFFICE_CAPACITY),
                      static_cast<unsigned>((WEATHER_CAPACITY + OFFICE_CAPACITY) *
                                            sizeof(history::Sample) / 1024));
        Serial.println("FATAL: check that the module really has 8 MB of PSRAM");
        while (true) delay(1000);
    }

    Wire.begin(board::I2C_SDA, board::I2C_SCL, board::I2C_HZ);
    delay(50);

    // Before anything wants the time. The backup cell holds this across a cold
    // boot, so the axis can be labelled correctly long before Wi-Fi associates.
    rtc::begin(DISPLAY_TZ);

    if (!panel_mcu::begin()) {
        // Not fatal on its own, but the backlight will not come on, so say so
        // loudly rather than leaving a dark screen unexplained.
        Serial.println("FATAL: no companion MCU; the backlight cannot be lit");
    }

    if (!display_init()) {
        // Almost always a PSRAM problem: the draw buffers cannot come from
        // internal SRAM. Check that the build selects OPI PSRAM.
        Serial.println("FATAL: display_init failed");
        while (true) delay(1000);
    }

    if (!touch_init()) {
        // Not fatal. The display is useful without touch; only the unit toggle
        // is lost.
        Serial.println("warning: touch unavailable");
    }

    ui::init(g_channels, g_chart_points, CHANNEL_COUNT);
    ui::refresh(millis());
    g_update_overlay.init();

    // Push updates and rollback. Before the backlight comes on, because begin()
    // writes the boot counter to flash, and before Wi-Fi, which starts push.
    // Pull updates stay off: no manifest_url.
    ota::Config ota_config;
    ota_config.hostname        = OTA_HOSTNAME;
    ota_config.push_password   = OTA_PASSWORD;
    ota_config.running_version = FW_VERSION;
    ota::begin(ota_config, g_update_overlay);

    net::begin();
    source_mqtt::begin(*g_channels[1]);
    source_weather::begin(*g_channels[0]);

    // Draw the first frame before the backlight comes on, so the panel lights
    // up showing the UI rather than whatever was in the buffers.
    lv_timer_handler();

    // Brightness owns the backlight from here. It comes up at full and dims
    // once a weather response has supplied the day's sun times.
    brightness::poll(rtc::hasTime());

    Serial.println("ready");
}

void loop() {
    lv_timer_handler();

    net::poll();
    // A push blocks here until it finishes; MQTT may drop meanwhile and
    // reconnects on its own if the update fails.
    ota::setNetworkUp(net::connected());
    ota::poll();
    g_update_overlay.poll(millis());
    rtc::poll(net::connected());
    brightness::poll(rtc::hasTime());
    source_mqtt::poll();
    source_weather::poll();

    touch_poll();
    pollLongPress();

    const uint32_t now = millis();
    ui::refresh(now);
    ui::setStatus(net::connected(), source_mqtt::connected());
    ui::setClock(rtc::hasTime(), time(nullptr));
    pollCharts();

    // Yield. Starving the RGB panel's DMA is one of the three documented causes
    // of display shake on this board.
    delay(5);
}
