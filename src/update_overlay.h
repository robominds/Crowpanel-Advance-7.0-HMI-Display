// A full-screen panel shown while a firmware update is received.
//
// It lives on LVGL's top layer, so it covers whichever view is showing, and it
// is hidden the rest of the time. esp32-ota-kit calls the observer methods from
// ota::poll(), outside lv_timer_handler(), and a push blocks loop() until it
// ends, so each method repaints the screen itself.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic).

#pragma once

#include <cstdint>

#include <ota.h>

class UpdateOverlay : public ota::Observer {
public:
    // Builds the hidden panel. Call once, after ui::init().
    void init();

    // Hides the panel once an error has been shown for ERROR_HOLD_MS. Call
    // every loop.
    void poll(uint32_t now_ms);

    void onTransferStarted(ota::Source source) override;
    void onProgress(ota::Source source, uint8_t percent) override;
    void onRebooting(ota::Source source) override;
    void onError(ota::Source source, const char* message) override;

    static constexpr uint32_t ERROR_HOLD_MS = 5000;
};
