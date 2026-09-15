// Implementation of the update overlay.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic).

#include "update_overlay.h"

#include <Arduino.h>
#include <lvgl.h>

#include <cstdio>

namespace {

lv_obj_t* g_panel   = nullptr;
lv_obj_t* g_bar     = nullptr;
lv_obj_t* g_percent = nullptr;
lv_obj_t* g_status  = nullptr;

bool     g_error_showing  = false;
uint32_t g_error_since_ms = 0;

void show() {
    g_error_showing = false;
    lv_obj_remove_flag(g_panel, LV_OBJ_FLAG_HIDDEN);
}

void setStatus(const char* text, lv_color_t color) {
    lv_label_set_text(g_status, text);
    lv_obj_set_style_text_color(g_status, color, 0);
}

void setPercent(uint8_t percent) {
    lv_bar_set_value(g_bar, percent, LV_ANIM_OFF);
    lv_label_set_text_fmt(g_percent, "%u %%", static_cast<unsigned>(percent));
}

}  // namespace

void UpdateOverlay::init() {
    g_panel = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(g_panel);
    lv_obj_set_size(g_panel, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(g_panel, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_panel, LV_OPA_COVER, 0);
    lv_obj_add_flag(g_panel, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* title = lv_label_create(g_panel);
    lv_label_set_text(title, "Updating firmware");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -110);

    g_bar = lv_bar_create(g_panel);
    lv_obj_set_size(g_bar, 600, 36);
    lv_bar_set_range(g_bar, 0, 100);
    lv_obj_set_style_bg_color(g_bar, lv_palette_darken(LV_PALETTE_GREY, 3), LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_bar, lv_palette_main(LV_PALETTE_BLUE), LV_PART_INDICATOR);
    lv_obj_align(g_bar, LV_ALIGN_CENTER, 0, 0);

    g_percent = lv_label_create(g_panel);
    lv_obj_set_style_text_font(g_percent, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(g_percent, lv_color_white(), 0);
    lv_obj_align(g_percent, LV_ALIGN_CENTER, 0, 60);

    g_status = lv_label_create(g_panel);
    lv_obj_set_style_text_font(g_status, &lv_font_montserrat_28, 0);
    lv_obj_align(g_status, LV_ALIGN_CENTER, 0, 120);

    setPercent(0);
    setStatus("", lv_color_white());
}

void UpdateOverlay::poll(uint32_t now_ms) {
    if (!g_error_showing || now_ms - g_error_since_ms < ERROR_HOLD_MS) return;
    g_error_showing = false;
    lv_obj_add_flag(g_panel, LV_OBJ_FLAG_HIDDEN);
}

void UpdateOverlay::onTransferStarted(ota::Source) {
    show();
    setPercent(0);
    setStatus("Receiving update", lv_color_white());
    lv_timer_handler();
}

void UpdateOverlay::onProgress(ota::Source, uint8_t percent) {
    setPercent(percent);
    lv_timer_handler();
}

void UpdateOverlay::onRebooting(ota::Source) {
    setPercent(100);
    setStatus("Rebooting", lv_color_white());
    lv_timer_handler();
}

void UpdateOverlay::onError(ota::Source, const char* message) {
    // One failure can report two errors (a failed connect-back also fails
    // Update.end()); the first names the cause, so a second only restarts the
    // hold time. onTransferStarted clears this for the next attempt.
    if (g_error_showing) {
        g_error_since_ms = millis();
        return;
    }
    // A wrong password fails before onTransferStarted, so show the panel here.
    show();
    char text[96];
    snprintf(text, sizeof text, "Update failed: %s", message);
    setStatus(text, lv_palette_main(LV_PALETTE_RED));
    g_error_showing  = true;
    g_error_since_ms = millis();
    lv_timer_handler();
}
