// Host tests for lib/parse: MQTT bare-decimal payloads and Open-Meteo JSON.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic).

#include <unity.h>

#include <cstring>

#include "parse.h"

static constexpr float kEps = 1e-4f;

void setUp(void) {}
void tearDown(void) {}

static bool dec(const char* s, float& out) {
    return parse::decimal(s, std::strlen(s), out);
}

// ---------------------------------------------------------------------------
// Well-formed payloads, exactly as the broker sends them
// ---------------------------------------------------------------------------

static void test_decimal_typical_temperature(void) {
    float v = 0.0f;
    TEST_ASSERT_TRUE(dec("19.6", v));
    TEST_ASSERT_FLOAT_WITHIN(kEps, 19.6f, v);
}

static void test_decimal_humidity_with_one_place(void) {
    float v = 0.0f;
    TEST_ASSERT_TRUE(dec("55.1", v));
    TEST_ASSERT_FLOAT_WITHIN(kEps, 55.1f, v);
}

static void test_decimal_integer_without_point(void) {
    float v = 0.0f;
    TEST_ASSERT_TRUE(dec("21", v));
    TEST_ASSERT_FLOAT_WITHIN(kEps, 21.0f, v);
}

static void test_decimal_negative(void) {
    float v = 0.0f;
    TEST_ASSERT_TRUE(dec("-7.5", v));
    TEST_ASSERT_FLOAT_WITHIN(kEps, -7.5f, v);
}

static void test_decimal_uptime_style_trailing_zero(void) {
    float v = 0.0f;
    TEST_ASSERT_TRUE(dec("20044.0", v));
    TEST_ASSERT_FLOAT_WITHIN(1e-2f, 20044.0f, v);
}

static void test_decimal_leading_and_trailing_space(void) {
    float v = 0.0f;
    TEST_ASSERT_TRUE(dec("  19.6  ", v));
    TEST_ASSERT_FLOAT_WITHIN(kEps, 19.6f, v);
}

// ---------------------------------------------------------------------------
// The payload is NOT null-terminated by PubSubClient
// ---------------------------------------------------------------------------

static void test_decimal_respects_length_and_ignores_trailing_bytes(void) {
    // A real PubSubClient buffer: the digits, then unrelated bytes after.
    const char buf[] = {'1', '9', '.', '6', 'X', 'Y', 'Z'};
    float v = 0.0f;
    TEST_ASSERT_TRUE(parse::decimal(buf, 4, v));
    TEST_ASSERT_FLOAT_WITHIN(kEps, 19.6f, v);
}

// ---------------------------------------------------------------------------
// Malformed input must be rejected and must not touch the output
// ---------------------------------------------------------------------------

static void test_decimal_rejects_empty(void) {
    float v = 123.0f;
    TEST_ASSERT_FALSE(parse::decimal("", 0, v));
    TEST_ASSERT_FLOAT_WITHIN(kEps, 123.0f, v);
}

static void test_decimal_rejects_null_pointer(void) {
    float v = 123.0f;
    TEST_ASSERT_FALSE(parse::decimal(nullptr, 4, v));
    TEST_ASSERT_FLOAT_WITHIN(kEps, 123.0f, v);
}

static void test_decimal_rejects_letters(void) {
    float v = 123.0f;
    TEST_ASSERT_FALSE(dec("nan", v));
    TEST_ASSERT_FLOAT_WITHIN(kEps, 123.0f, v);
}

static void test_decimal_rejects_trailing_garbage(void) {
    float v = 123.0f;
    TEST_ASSERT_FALSE(dec("19.6C", v));
    TEST_ASSERT_FLOAT_WITHIN(kEps, 123.0f, v);
}

static void test_decimal_rejects_json(void) {
    float v = 123.0f;
    TEST_ASSERT_FALSE(dec("{\"t\":19.6}", v));
    TEST_ASSERT_FLOAT_WITHIN(kEps, 123.0f, v);
}

static void test_decimal_rejects_infinity(void) {
    float v = 123.0f;
    TEST_ASSERT_FALSE(dec("inf", v));
    TEST_ASSERT_FLOAT_WITHIN(kEps, 123.0f, v);
}

static void test_decimal_rejects_only_whitespace(void) {
    float v = 123.0f;
    TEST_ASSERT_FALSE(dec("   ", v));
    TEST_ASSERT_FLOAT_WITHIN(kEps, 123.0f, v);
}

static void test_decimal_rejects_embedded_null(void) {
    // PubSubClient hands over raw bytes. A truncated or corrupted packet can
    // put a null inside the length, and strtof would stop there and report a
    // plausible wrong number.
    const char buf[] = {'1', '9', '\0', '.', '6'};
    float v = 123.0f;
    TEST_ASSERT_FALSE(parse::decimal(buf, sizeof(buf), v));
    TEST_ASSERT_FLOAT_WITHIN(kEps, 123.0f, v);
}

static void test_decimal_rejects_hex_float_notation(void) {
    float v = 123.0f;
    TEST_ASSERT_FALSE(dec("0x1p0", v));
    TEST_ASSERT_FLOAT_WITHIN(kEps, 123.0f, v);
    TEST_ASSERT_FALSE(dec("0x1.8p3", v));
    TEST_ASSERT_FLOAT_WITHIN(kEps, 123.0f, v);
}

static void test_decimal_still_accepts_exponent_notation(void) {
    float v = 0.0f;
    TEST_ASSERT_TRUE(dec("1.96e1", v));
    TEST_ASSERT_FLOAT_WITHIN(kEps, 19.6f, v);
}

// ---------------------------------------------------------------------------
// Open-Meteo. The fixture is a real response captured on 2026-09-11.
// ---------------------------------------------------------------------------

static const char kRealResponse[] =
    "{\"latitude\":47.37403,\"longitude\":-122.03002,"
    "\"generationtime_ms\":0.2496,\"utc_offset_seconds\":-25200,"
    "\"timezone\":\"America/Los_Angeles\",\"timezone_abbreviation\":\"GMT-7\","
    "\"elevation\":150.0,\"current_units\":{\"time\":\"iso8601\","
    "\"interval\":\"seconds\",\"temperature_2m\":\"C\","
    "\"relative_humidity_2m\":\"%\",\"weather_code\":\"wmo code\"},"
    "\"current\":{\"time\":\"2026-09-11T10:30\",\"interval\":900,"
    "\"temperature_2m\":16.1,\"relative_humidity_2m\":62,"
    "\"weather_code\":3}}";

static bool om(const char* s, parse::Weather& out) {
    return parse::openMeteo(s, std::strlen(s), out);
}

static void test_openmeteo_real_response(void) {
    parse::Weather w{};
    TEST_ASSERT_TRUE(om(kRealResponse, w));
    TEST_ASSERT_FLOAT_WITHIN(kEps, 16.1f, w.temperature_c);
    TEST_ASSERT_FLOAT_WITHIN(kEps, 62.0f, w.humidity_pct);
}

static void test_openmeteo_minimal_object(void) {
    parse::Weather w{};
    TEST_ASSERT_TRUE(om("{\"current\":{\"temperature_2m\":-3.25,"
                        "\"relative_humidity_2m\":91}}", w));
    TEST_ASSERT_FLOAT_WITHIN(kEps, -3.25f, w.temperature_c);
    TEST_ASSERT_FLOAT_WITHIN(kEps, 91.0f, w.humidity_pct);
}

static void test_openmeteo_rejects_missing_temperature(void) {
    parse::Weather w{};
    w.temperature_c = 99.0f;
    TEST_ASSERT_FALSE(om("{\"current\":{\"relative_humidity_2m\":62}}", w));
    TEST_ASSERT_FLOAT_WITHIN(kEps, 99.0f, w.temperature_c);
}

static void test_openmeteo_rejects_missing_humidity(void) {
    parse::Weather w{};
    TEST_ASSERT_FALSE(om("{\"current\":{\"temperature_2m\":16.1}}", w));
}

static void test_openmeteo_rejects_missing_current_object(void) {
    parse::Weather w{};
    TEST_ASSERT_FALSE(om("{\"latitude\":47.37,\"elevation\":150.0}", w));
}

static void test_openmeteo_rejects_truncated_response(void) {
    parse::Weather w{};
    TEST_ASSERT_FALSE(om("{\"current\":{\"temperature_2m\":16", w));
}

static void test_openmeteo_rejects_empty_and_null(void) {
    parse::Weather w{};
    TEST_ASSERT_FALSE(parse::openMeteo("", 0, w));
    TEST_ASSERT_FALSE(parse::openMeteo(nullptr, 10, w));
}

static void test_openmeteo_rejects_api_error_body(void) {
    parse::Weather w{};
    TEST_ASSERT_FALSE(om("{\"error\":true,\"reason\":\"Latitude must be in "
                         "range of -90 to 90\"}", w));
}

static void test_openmeteo_rejects_non_numeric_temperature(void) {
    parse::Weather w{};
    TEST_ASSERT_FALSE(om("{\"current\":{\"temperature_2m\":null,"
                         "\"relative_humidity_2m\":62}}", w));
}

int main(int, char**) {
    UNITY_BEGIN();

    RUN_TEST(test_decimal_typical_temperature);
    RUN_TEST(test_decimal_humidity_with_one_place);
    RUN_TEST(test_decimal_integer_without_point);
    RUN_TEST(test_decimal_negative);
    RUN_TEST(test_decimal_uptime_style_trailing_zero);
    RUN_TEST(test_decimal_leading_and_trailing_space);

    RUN_TEST(test_decimal_respects_length_and_ignores_trailing_bytes);

    RUN_TEST(test_decimal_rejects_empty);
    RUN_TEST(test_decimal_rejects_null_pointer);
    RUN_TEST(test_decimal_rejects_letters);
    RUN_TEST(test_decimal_rejects_trailing_garbage);
    RUN_TEST(test_decimal_rejects_json);
    RUN_TEST(test_decimal_rejects_infinity);
    RUN_TEST(test_decimal_rejects_only_whitespace);

    RUN_TEST(test_decimal_rejects_embedded_null);
    RUN_TEST(test_decimal_rejects_hex_float_notation);
    RUN_TEST(test_decimal_still_accepts_exponent_notation);

    RUN_TEST(test_openmeteo_real_response);
    RUN_TEST(test_openmeteo_minimal_object);
    RUN_TEST(test_openmeteo_rejects_missing_temperature);
    RUN_TEST(test_openmeteo_rejects_missing_humidity);
    RUN_TEST(test_openmeteo_rejects_missing_current_object);
    RUN_TEST(test_openmeteo_rejects_truncated_response);
    RUN_TEST(test_openmeteo_rejects_empty_and_null);
    RUN_TEST(test_openmeteo_rejects_api_error_body);
    RUN_TEST(test_openmeteo_rejects_non_numeric_temperature);

    return UNITY_END();
}
