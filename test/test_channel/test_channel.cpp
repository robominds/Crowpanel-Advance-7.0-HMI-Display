// Host tests for lib/channel: reading updates, per-channel staleness, and the
// millis() rollover.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic).

#include <unity.h>

#include <cstring>

#include "Channel.h"

using channel::Channel;

static constexpr float kEps = 1e-4f;

// The two real channels' thresholds, from the spec.
static constexpr uint32_t kOfficeStaleMs  = 60UL * 1000UL;
static constexpr uint32_t kWeatherStaleMs = 45UL * 60UL * 1000UL;

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// Before any reading arrives
// ---------------------------------------------------------------------------

static void test_new_channel_has_no_valid_reading(void) {
    Channel c("Mark's Office", kOfficeStaleMs, 16);
    TEST_ASSERT_FALSE(c.latest().valid);
    TEST_ASSERT_EQUAL_STRING("Mark's Office", c.name());
    TEST_ASSERT_TRUE(c.history().empty());
}

static void test_new_channel_is_stale_immediately(void) {
    // Nothing has ever arrived, so the reading cannot be trusted. Reporting
    // "fresh" here would show a zero as though it were a measurement.
    Channel c("Mark's Office", kOfficeStaleMs, 16);
    TEST_ASSERT_TRUE(c.isStale(0));
    TEST_ASSERT_TRUE(c.isStale(5000));
}

// ---------------------------------------------------------------------------
// Updating
// ---------------------------------------------------------------------------

static void test_update_sets_latest_and_appends_history(void) {
    Channel c("Mark's Office", kOfficeStaleMs, 16);
    c.update(19.6f, 55.1f, 1000);

    TEST_ASSERT_TRUE(c.latest().valid);
    TEST_ASSERT_FLOAT_WITHIN(kEps, 19.6f, c.latest().temperature_c);
    TEST_ASSERT_FLOAT_WITHIN(kEps, 55.1f, c.latest().humidity_pct);
    TEST_ASSERT_EQUAL_UINT32(1000, c.latest().t_ms);

    TEST_ASSERT_EQUAL_size_t(1, c.history().size());
    TEST_ASSERT_FLOAT_WITHIN(kEps, 19.6f, c.history().newest().temperature_c);
    TEST_ASSERT_EQUAL_UINT32(1000, c.history().newest().t_ms);
}

static void test_repeated_updates_all_land_in_history(void) {
    Channel c("Mark's Office", kOfficeStaleMs, 16);
    for (uint32_t i = 0; i < 5; ++i) {
        c.update(20.0f + i, 50.0f + i, 1000 * i);
    }
    TEST_ASSERT_EQUAL_size_t(5, c.history().size());
    TEST_ASSERT_FLOAT_WITHIN(kEps, 24.0f, c.latest().temperature_c);
    TEST_ASSERT_FLOAT_WITHIN(kEps, 20.0f, c.history().oldest().temperature_c);
}

// ---------------------------------------------------------------------------
// Staleness, per channel
// ---------------------------------------------------------------------------

static void test_office_fresh_within_threshold(void) {
    Channel c("Mark's Office", kOfficeStaleMs, 16);
    c.update(19.6f, 55.1f, 10000);
    TEST_ASSERT_FALSE(c.isStale(10000));
    TEST_ASSERT_FALSE(c.isStale(10000 + 59000));
}

static void test_office_stale_past_threshold(void) {
    Channel c("Mark's Office", kOfficeStaleMs, 16);
    c.update(19.6f, 55.1f, 10000);
    TEST_ASSERT_TRUE(c.isStale(10000 + 61000));
}

static void test_office_boundary_is_not_yet_stale(void) {
    Channel c("Mark's Office", kOfficeStaleMs, 16);
    c.update(19.6f, 55.1f, 10000);
    TEST_ASSERT_FALSE(c.isStale(10000 + kOfficeStaleMs));
    TEST_ASSERT_TRUE(c.isStale(10000 + kOfficeStaleMs + 1));
}

static void test_weather_tolerates_a_gap_that_would_fail_the_office(void) {
    // The whole point of per-channel thresholds. Ten minutes of silence is a
    // dead broker for the office and completely normal for the weather.
    const uint32_t ten_minutes = 10UL * 60UL * 1000UL;

    Channel office("Mark's Office", kOfficeStaleMs, 16);
    Channel weather("Maple Valley", kWeatherStaleMs, 16);
    office.update(19.6f, 55.1f, 0);
    weather.update(16.1f, 62.0f, 0);

    TEST_ASSERT_TRUE(office.isStale(ten_minutes));
    TEST_ASSERT_FALSE(weather.isStale(ten_minutes));
}

static void test_weather_stale_past_its_own_threshold(void) {
    Channel c("Maple Valley", kWeatherStaleMs, 16);
    c.update(16.1f, 62.0f, 0);
    TEST_ASSERT_TRUE(c.isStale(kWeatherStaleMs + 1));
}

static void test_update_refreshes_staleness(void) {
    Channel c("Mark's Office", kOfficeStaleMs, 16);
    c.update(19.6f, 55.1f, 0);
    TEST_ASSERT_TRUE(c.isStale(120000));
    c.update(19.7f, 55.0f, 120000);
    TEST_ASSERT_FALSE(c.isStale(120000));
}

// ---------------------------------------------------------------------------
// millis() rollover at 2^32 ms, about 49.7 days
// ---------------------------------------------------------------------------

static void test_staleness_survives_millis_rollover(void) {
    Channel c("Mark's Office", kOfficeStaleMs, 16);

    const uint32_t before = 0xFFFFFF00UL;  // 256 ms before the wrap
    c.update(19.6f, 55.1f, before);

    // 10 s later, having wrapped through zero. Unsigned subtraction keeps the
    // elapsed time correct, so this must read as fresh rather than as 49 days.
    // The addition is deliberately made to wrap: cast explicitly so the
    // compiler knows the truncation to uint32_t is intentional.
    const uint32_t after = static_cast<uint32_t>(before + 10000UL);
    TEST_ASSERT_FALSE(c.isStale(after));

    TEST_ASSERT_TRUE(c.isStale(before + kOfficeStaleMs + 1000UL));
}

// ---------------------------------------------------------------------------
// History capacity
// ---------------------------------------------------------------------------

static void test_history_evicts_oldest_at_capacity(void) {
    Channel c("Mark's Office", kOfficeStaleMs, 4);
    for (uint32_t i = 0; i < 6; ++i) c.update(20.0f + i, 50.0f, 1000 * i);

    TEST_ASSERT_EQUAL_size_t(4, c.history().size());
    TEST_ASSERT_FLOAT_WITHIN(kEps, 22.0f, c.history().oldest().temperature_c);
    TEST_ASSERT_FLOAT_WITHIN(kEps, 25.0f, c.history().newest().temperature_c);
    // The latest reading is unaffected by eviction.
    TEST_ASSERT_FLOAT_WITHIN(kEps, 25.0f, c.latest().temperature_c);
}

int main(int, char**) {
    UNITY_BEGIN();

    RUN_TEST(test_new_channel_has_no_valid_reading);
    RUN_TEST(test_new_channel_is_stale_immediately);

    RUN_TEST(test_update_sets_latest_and_appends_history);
    RUN_TEST(test_repeated_updates_all_land_in_history);

    RUN_TEST(test_office_fresh_within_threshold);
    RUN_TEST(test_office_stale_past_threshold);
    RUN_TEST(test_office_boundary_is_not_yet_stale);
    RUN_TEST(test_weather_tolerates_a_gap_that_would_fail_the_office);
    RUN_TEST(test_weather_stale_past_its_own_threshold);
    RUN_TEST(test_update_refreshes_staleness);

    RUN_TEST(test_staleness_survives_millis_rollover);

    RUN_TEST(test_history_evicts_oldest_at_capacity);

    return UNITY_END();
}
