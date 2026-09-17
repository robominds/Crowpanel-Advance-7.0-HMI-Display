// The local AHT10 on the I2C-OUT connector, when one is fitted.
//
// begin() probes the bus and decides this panel's role: a panel with a sensor
// shows and publishes its own reading, a panel without one reads the broker.
//
// The sensor shares the bus with the touch controller, and blocking that bus
// starves the RGB panel's DMA. A measurement takes 80 ms, so poll() triggers
// one and collects it on a later call rather than waiting.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic).

#pragma once

#include <cstdint>

namespace source_aht10 {

// Soft-resets and initialises the sensor, then reads its status. Call once,
// after Wire.begin(). Returns whether a calibrated sensor answered.
bool begin();

// Call every loop. Returns true on the single call where a fresh, valid sample
// became available, writing it to the two references; false otherwise.
bool poll(float& temperature_c, float& humidity_pct);

bool present();

// Frames rejected since boot: a bus error, or a frame aht10::convert refused.
uint32_t failedReads();

}  // namespace source_aht10
