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

#include "Channel.h"
#include "board_pins.h"
#include "display_driver.h"
#include "net.h"
#include "panel_mcu.h"
#include "source_mqtt.h"
#include "source_weather.h"
#include "touch.h"
#include "ui.h"

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

channel::Channel g_outdoor("Maple Valley", WEATHER_STALE_MS, WEATHER_CAPACITY);
channel::Channel g_office("Mark's Office", OFFICE_STALE_MS, OFFICE_CAPACITY);

channel::Channel* g_channels[] = {&g_outdoor, &g_office};
constexpr size_t  CHANNEL_COUNT = sizeof(g_channels) / sizeof(g_channels[0]);

// Long-press anywhere toggles Celsius and Fahrenheit. Deliberately not a
// button: the panel is meant to be read, not operated, and a stray brush
// against the glass should not change the units.
constexpr uint32_t UNIT_TOGGLE_HOLD_MS = 1000;

void pollUnitToggle() {
    static uint32_t press_started_ms = 0;
    static bool     handled          = false;

    if (!touch_pressed()) {
        press_started_ms = 0;
        handled          = false;
        return;
    }

    const uint32_t now = millis();
    if (press_started_ms == 0) {
        press_started_ms = now;
        return;
    }

    if (!handled && (now - press_started_ms) >= UNIT_TOGGLE_HOLD_MS) {
        handled = true;
        ui::setFahrenheit(!ui::fahrenheit());
        Serial.printf("units: %s\n", ui::fahrenheit() ? "F" : "C");
    }
}

// Redraws a row's chart when its channel has gained a sample.
void pollCharts() {
    static size_t last_size[CHANNEL_COUNT] = {0};

    for (size_t i = 0; i < CHANNEL_COUNT; ++i) {
        const size_t n = g_channels[i]->history().size();
        if (n == last_size[i]) continue;
        last_size[i] = n;
        ui::updateChart(i);
    }
}

}  // namespace

void setup() {
    Serial.begin(115200);
    // Serial reaches the host through a CH340K on GPIO43/44, not native USB.
    // Nothing to wait for, so no while(!Serial) here - that would hang forever.
    delay(200);
    Serial.println("\nCrowPanel Advance 7.0 temperature display");

    Wire.begin(board::I2C_SDA, board::I2C_SCL, board::I2C_HZ);
    delay(50);

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

    ui::init(g_channels, CHANNEL_COUNT);
    ui::refresh(millis());

    net::begin();
    source_mqtt::begin(g_office);
    source_weather::begin(g_outdoor);

    // Draw the first frame before the backlight comes on, so the panel lights
    // up showing the UI rather than whatever was in the buffers.
    lv_timer_handler();
    panel_mcu::backlightOn();

    Serial.println("ready");
}

void loop() {
    lv_timer_handler();

    net::poll();
    source_mqtt::poll();
    source_weather::poll();

    touch_poll();
    pollUnitToggle();

    const uint32_t now = millis();
    ui::refresh(now);
    ui::setStatus(net::connected(), source_mqtt::connected());
    pollCharts();

    // Yield. Starving the RGB panel's DMA is one of the three documented causes
    // of display shake on this board.
    delay(5);
}
