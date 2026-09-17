// Host tests for lib/aht10: the six bytes an AHT10 returns.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic).

#include <unity.h>

#include "aht10.h"

static constexpr float kEps = 0.01f;

void setUp(void) {}
void tearDown(void) {}

// Humidity and temperature both mid-scale: raw 0x80000 each.
// 0x80000 * 100 / 2^20 = 50 %RH; 0x80000 * 200 / 2^20 - 50 = 50 C.
static void test_convert_mid_scale(void) {
    const uint8_t f[6] = {0x1C, 0x80, 0x00, 0x08, 0x00, 0x00};
    aht10::Sample s{};
    TEST_ASSERT_TRUE(aht10::convert(f, sizeof f, s));
    TEST_ASSERT_FLOAT_WITHIN(kEps, 50.0f, s.humidity_pct);
    TEST_ASSERT_FLOAT_WITHIN(kEps, 50.0f, s.temperature_c);
}

// Raw temperature 0x20000 is an eighth of scale: 200/8 - 50 = -25 C.
// Raw humidity 0x10000 is a sixteenth: 6.25 %RH.
static void test_convert_negative_temperature(void) {
    const uint8_t f[6] = {0x1C, 0x10, 0x00, 0x02, 0x00, 0x00};
    aht10::Sample s{};
    TEST_ASSERT_TRUE(aht10::convert(f, sizeof f, s));
    TEST_ASSERT_FLOAT_WITHIN(kEps, 6.25f, s.humidity_pct);
    TEST_ASSERT_FLOAT_WITHIN(kEps, -25.0f, s.temperature_c);
}

// Full-scale humidity with a valid temperature: 0xFFFFF is 100 %RH to within
// a thousandth, and must not be rejected as an all-ones frame.
static void test_convert_full_scale_humidity(void) {
    const uint8_t f[6] = {0x1C, 0xFF, 0xFF, 0xF8, 0x00, 0x00};
    aht10::Sample s{};
    TEST_ASSERT_TRUE(aht10::convert(f, sizeof f, s));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.0f, s.humidity_pct);
    TEST_ASSERT_FLOAT_WITHIN(kEps, 50.0f, s.temperature_c);
}

// A sensor that never answered: the driver reads zeros off an idle bus.
static void test_convert_rejects_all_zero(void) {
    const uint8_t f[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    aht10::Sample s{};
    TEST_ASSERT_FALSE(aht10::convert(f, sizeof f, s));
}

// A disconnected SDA line reads as all ones.
static void test_convert_rejects_all_ones(void) {
    const uint8_t f[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    aht10::Sample s{};
    TEST_ASSERT_FALSE(aht10::convert(f, sizeof f, s));
}

// Status bit 7 set means the conversion was still running.
static void test_convert_rejects_busy(void) {
    const uint8_t f[6] = {0x80, 0x80, 0x00, 0x08, 0x00, 0x00};
    aht10::Sample s{};
    TEST_ASSERT_FALSE(aht10::convert(f, sizeof f, s));
}

// Status bit 3 clear means the sensor lost its calibration.
static void test_convert_rejects_uncalibrated(void) {
    const uint8_t f[6] = {0x00 | 0x10, 0x80, 0x00, 0x08, 0x00, 0x00};
    aht10::Sample s{};
    TEST_ASSERT_FALSE(aht10::convert(f, sizeof f, s));
}

static void test_convert_rejects_short_frame(void) {
    const uint8_t f[5] = {0x1C, 0x80, 0x00, 0x08, 0x00};
    aht10::Sample s{};
    TEST_ASSERT_FALSE(aht10::convert(f, sizeof f, s));
}

static void test_convert_rejects_null(void) {
    aht10::Sample s{};
    TEST_ASSERT_FALSE(aht10::convert(nullptr, 6, s));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_convert_mid_scale);
    RUN_TEST(test_convert_negative_temperature);
    RUN_TEST(test_convert_full_scale_humidity);
    RUN_TEST(test_convert_rejects_all_zero);
    RUN_TEST(test_convert_rejects_all_ones);
    RUN_TEST(test_convert_rejects_busy);
    RUN_TEST(test_convert_rejects_uncalibrated);
    RUN_TEST(test_convert_rejects_short_frame);
    RUN_TEST(test_convert_rejects_null);
    return UNITY_END();
}
