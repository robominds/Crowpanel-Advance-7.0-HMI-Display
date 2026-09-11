// Host tests for lib/history (SampleHistory): retention, oldest-first ordering
// across ring wraparound, temperature range, and chart downsampling - including
// the millis() 49.7-day rollover.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic).

#include <unity.h>

#include <vector>

#include "SampleHistory.h"

using history::Sample;
using history::SampleHistory;
using Bucket = history::SampleHistory::Bucket;

static constexpr float kEps = 1e-4f;

static Sample mk(uint32_t t_ms, float t_c, float rh) { return Sample{t_ms, t_c, rh}; }

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// Empty
// ---------------------------------------------------------------------------

static void test_empty_buffer(void) {
    SampleHistory h(8);
    TEST_ASSERT_TRUE(h.empty());
    TEST_ASSERT_EQUAL_size_t(0, h.size());
    TEST_ASSERT_EQUAL_size_t(8, h.capacity());

    float lo = 99.0f, hi = -99.0f;
    TEST_ASSERT_FALSE(h.temperatureRange(lo, hi));
    TEST_ASSERT_FLOAT_WITHIN(kEps, 99.0f, lo);   // untouched on failure
    TEST_ASSERT_FLOAT_WITHIN(kEps, -99.0f, hi);
}

static void test_empty_downsample_is_all_invalid(void) {
    SampleHistory h(8);
    Bucket b[4];
    TEST_ASSERT_EQUAL_size_t(0, h.downsample(1000, b, 4));
    for (size_t i = 0; i < 4; ++i) TEST_ASSERT_FALSE(b[i].valid);
}

// ---------------------------------------------------------------------------
// Fill and retention
// ---------------------------------------------------------------------------

static void test_partial_fill_is_oldest_first(void) {
    SampleHistory h(8);
    for (uint32_t i = 0; i < 3; ++i) h.add(mk(1000 * i, 20.0f + i, 50.0f + i));

    TEST_ASSERT_EQUAL_size_t(3, h.size());
    TEST_ASSERT_FALSE(h.empty());
    for (uint32_t i = 0; i < 3; ++i) {
        TEST_ASSERT_EQUAL_UINT32(1000 * i, h.at(i).t_ms);
        TEST_ASSERT_FLOAT_WITHIN(kEps, 20.0f + i, h.at(i).temperature_c);
        TEST_ASSERT_FLOAT_WITHIN(kEps, 50.0f + i, h.at(i).humidity_pct);
    }
    TEST_ASSERT_EQUAL_UINT32(0, h.oldest().t_ms);
    TEST_ASSERT_EQUAL_UINT32(2000, h.newest().t_ms);
}

static void test_exact_fill_to_capacity(void) {
    SampleHistory h(4);
    for (uint32_t i = 0; i < 4; ++i) h.add(mk(i, static_cast<float>(i), 0.0f));

    TEST_ASSERT_EQUAL_size_t(4, h.size());
    TEST_ASSERT_EQUAL_size_t(4, h.capacity());
    TEST_ASSERT_EQUAL_UINT32(0, h.oldest().t_ms);
    TEST_ASSERT_EQUAL_UINT32(3, h.newest().t_ms);
}

static void test_overflow_evicts_oldest(void) {
    SampleHistory h(4);
    const size_t n = 4 + 3;  // capacity + N
    for (uint32_t i = 0; i < n; ++i) h.add(mk(i, static_cast<float>(i), 0.0f));

    TEST_ASSERT_EQUAL_size_t(4, h.size());
    // After capacity + N adds the oldest retained is the (N+1)th added, i.e. t=3.
    TEST_ASSERT_EQUAL_UINT32(3, h.oldest().t_ms);
    TEST_ASSERT_EQUAL_UINT32(6, h.newest().t_ms);
}

static void test_ordering_holds_across_many_wraps(void) {
    // The classic ring bug only shows after the head has lapped the buffer
    // several times and the newest sample sits mid-array.
    const size_t cap = 5;
    SampleHistory h(cap);
    const uint32_t n = 5 * 7 + 3;  // 38 adds, head lands mid-buffer
    for (uint32_t i = 0; i < n; ++i) h.add(mk(i * 10, static_cast<float>(i), 100.0f - i));

    TEST_ASSERT_EQUAL_size_t(cap, h.size());
    for (size_t i = 0; i < cap; ++i) {
        const uint32_t expect = static_cast<uint32_t>(n - cap + i);
        TEST_ASSERT_EQUAL_UINT32(expect * 10, h.at(i).t_ms);
        TEST_ASSERT_FLOAT_WITHIN(kEps, static_cast<float>(expect), h.at(i).temperature_c);
        TEST_ASSERT_FLOAT_WITHIN(kEps, 100.0f - expect, h.at(i).humidity_pct);
    }
    // at() must agree with the dedicated accessors at both ends.
    TEST_ASSERT_EQUAL_UINT32(h.at(0).t_ms, h.oldest().t_ms);
    TEST_ASSERT_EQUAL_UINT32(h.at(cap - 1).t_ms, h.newest().t_ms);
    // Timestamps must be strictly increasing with index, never rotated.
    for (size_t i = 1; i < cap; ++i) TEST_ASSERT_TRUE(h.at(i).t_ms > h.at(i - 1).t_ms);
}

static void test_capacity_of_zero_is_a_sink(void) {
    SampleHistory h(0);
    h.add(mk(0, 21.0f, 50.0f));
    h.add(mk(1, 22.0f, 51.0f));

    TEST_ASSERT_TRUE(h.empty());
    TEST_ASSERT_EQUAL_size_t(0, h.size());
    TEST_ASSERT_EQUAL_size_t(0, h.capacity());

    float lo = 0.0f, hi = 0.0f;
    TEST_ASSERT_FALSE(h.temperatureRange(lo, hi));

    Bucket b[3];
    TEST_ASSERT_EQUAL_size_t(0, h.downsample(1000, b, 3));
    for (size_t i = 0; i < 3; ++i) TEST_ASSERT_FALSE(b[i].valid);
}

static void test_clear_resets_and_buffer_is_reusable(void) {
    SampleHistory h(4);
    for (uint32_t i = 0; i < 6; ++i) h.add(mk(i, static_cast<float>(i), 0.0f));

    h.clear();
    TEST_ASSERT_TRUE(h.empty());
    TEST_ASSERT_EQUAL_size_t(0, h.size());
    TEST_ASSERT_EQUAL_size_t(4, h.capacity());
    float lo = 0.0f, hi = 0.0f;
    TEST_ASSERT_FALSE(h.temperatureRange(lo, hi));

    // Reuse must start from a clean head, not from wherever the ring was left.
    h.add(mk(100, 30.0f, 60.0f));
    h.add(mk(200, 31.0f, 61.0f));
    TEST_ASSERT_EQUAL_size_t(2, h.size());
    TEST_ASSERT_EQUAL_UINT32(100, h.at(0).t_ms);
    TEST_ASSERT_EQUAL_UINT32(200, h.at(1).t_ms);
}

// ---------------------------------------------------------------------------
// temperatureRange
// ---------------------------------------------------------------------------

static void test_range_single_sample(void) {
    SampleHistory h(4);
    h.add(mk(0, 21.5f, 50.0f));

    float lo = 0.0f, hi = 0.0f;
    TEST_ASSERT_TRUE(h.temperatureRange(lo, hi));
    TEST_ASSERT_FLOAT_WITHIN(kEps, 21.5f, lo);
    TEST_ASSERT_FLOAT_WITHIN(kEps, 21.5f, hi);
}

static void test_range_many_samples(void) {
    SampleHistory h(8);
    const float temps[] = {21.0f, 25.5f, 19.25f, 23.0f, 30.75f, 22.0f};
    for (uint32_t i = 0; i < 6; ++i) h.add(mk(i, temps[i], 50.0f));

    float lo = 0.0f, hi = 0.0f;
    TEST_ASSERT_TRUE(h.temperatureRange(lo, hi));
    TEST_ASSERT_FLOAT_WITHIN(kEps, 19.25f, lo);
    TEST_ASSERT_FLOAT_WITHIN(kEps, 30.75f, hi);
}

static void test_range_with_negative_temperatures(void) {
    SampleHistory h(8);
    const float temps[] = {-5.5f, -20.0f, -0.25f, -12.0f};
    for (uint32_t i = 0; i < 4; ++i) h.add(mk(i, temps[i], 50.0f));

    float lo = 0.0f, hi = 0.0f;
    TEST_ASSERT_TRUE(h.temperatureRange(lo, hi));
    TEST_ASSERT_FLOAT_WITHIN(kEps, -20.0f, lo);
    TEST_ASSERT_FLOAT_WITHIN(kEps, -0.25f, hi);
}

static void test_range_all_equal_values(void) {
    SampleHistory h(8);
    for (uint32_t i = 0; i < 5; ++i) h.add(mk(i, 22.0f, 50.0f));

    float lo = 0.0f, hi = 0.0f;
    TEST_ASSERT_TRUE(h.temperatureRange(lo, hi));
    TEST_ASSERT_FLOAT_WITHIN(kEps, 22.0f, lo);
    TEST_ASSERT_FLOAT_WITHIN(kEps, 22.0f, hi);
}

static void test_range_ignores_evicted_samples(void) {
    SampleHistory h(3);
    h.add(mk(0, 100.0f, 50.0f));  // evicted
    h.add(mk(1, 20.0f, 50.0f));
    h.add(mk(2, 21.0f, 50.0f));
    h.add(mk(3, 22.0f, 50.0f));

    float lo = 0.0f, hi = 0.0f;
    TEST_ASSERT_TRUE(h.temperatureRange(lo, hi));
    TEST_ASSERT_FLOAT_WITHIN(kEps, 20.0f, lo);
    TEST_ASSERT_FLOAT_WITHIN(kEps, 22.0f, hi);
}

// ---------------------------------------------------------------------------
// downsample
// ---------------------------------------------------------------------------

static void test_downsample_even_spread_lands_one_per_bucket(void) {
    SampleHistory h(16);
    // 10 samples, 100 ms apart, newest at t=1000; window 1000 ms, 10 buckets,
    // so each bucket is one 100 ms slice and holds exactly one sample.
    for (uint32_t i = 1; i <= 10; ++i) h.add(mk(i * 100, static_cast<float>(i), 40.0f + i));

    Bucket b[10];
    TEST_ASSERT_EQUAL_size_t(10, h.downsample(1000, b, 10));
    for (size_t i = 0; i < 10; ++i) {
        TEST_ASSERT_TRUE(b[i].valid);
        // Bucket 0 is oldest (t=100, 1.0 C); bucket 9 is newest (t=1000, 10.0 C).
        TEST_ASSERT_FLOAT_WITHIN(kEps, static_cast<float>(i + 1), b[i].temperature_c);
        TEST_ASSERT_FLOAT_WITHIN(kEps, 40.0f + (i + 1), b[i].humidity_pct);
    }
}

static void test_downsample_bucket_holding_several_samples_is_the_mean(void) {
    SampleHistory h(16);
    // Window 1000 ms over 5 buckets = 200 ms slices, anchored on the newest.
    h.add(mk(700, 5.0f, 30.0f));    // age 300 -> bucket 3
    h.add(mk(950, 10.0f, 40.0f));   // age  50 -> bucket 4
    h.add(mk(980, 20.0f, 60.0f));   // age  20 -> bucket 4
    h.add(mk(1000, 30.0f, 80.0f));  // age   0 -> bucket 4

    Bucket b[5];
    TEST_ASSERT_EQUAL_size_t(2, h.downsample(1000, b, 5));
    TEST_ASSERT_TRUE(b[3].valid);
    TEST_ASSERT_FLOAT_WITHIN(kEps, 5.0f, b[3].temperature_c);
    TEST_ASSERT_TRUE(b[4].valid);
    TEST_ASSERT_FLOAT_WITHIN(kEps, 20.0f, b[4].temperature_c);  // (10+20+30)/3
    TEST_ASSERT_FLOAT_WITHIN(kEps, 60.0f, b[4].humidity_pct);   // (40+60+80)/3
    TEST_ASSERT_FALSE(b[0].valid);
    TEST_ASSERT_FALSE(b[1].valid);
    TEST_ASSERT_FALSE(b[2].valid);
}

static void test_downsample_marks_gaps_invalid(void) {
    SampleHistory h(16);
    h.add(mk(500, 15.0f, 45.0f));   // age 500 -> bucket 4
    h.add(mk(1000, 25.0f, 55.0f));  // age   0 -> bucket 9

    Bucket b[10];
    TEST_ASSERT_EQUAL_size_t(2, h.downsample(1000, b, 10));
    for (size_t i = 0; i < 10; ++i) {
        const bool expect_valid = (i == 4 || i == 9);
        TEST_ASSERT_EQUAL_INT(expect_valid ? 1 : 0, b[i].valid ? 1 : 0);
    }
    TEST_ASSERT_FLOAT_WITHIN(kEps, 15.0f, b[4].temperature_c);
    TEST_ASSERT_FLOAT_WITHIN(kEps, 25.0f, b[9].temperature_c);
}

static void test_downsample_excludes_samples_older_than_window(void) {
    SampleHistory h(16);
    h.add(mk(3000, 99.0f, 99.0f));  // age 2000 > window -> dropped
    h.add(mk(4000, 50.0f, 50.0f));  // age 1000 == window -> kept, oldest bucket
    h.add(mk(4500, 10.0f, 20.0f));  // age  500 -> bucket 1
    h.add(mk(5000, 30.0f, 40.0f));  // age    0 -> bucket 3

    Bucket b[4];
    TEST_ASSERT_EQUAL_size_t(3, h.downsample(1000, b, 4));
    TEST_ASSERT_TRUE(b[0].valid);
    TEST_ASSERT_FLOAT_WITHIN(kEps, 50.0f, b[0].temperature_c);  // no 99.0 contamination
    TEST_ASSERT_TRUE(b[1].valid);
    TEST_ASSERT_FLOAT_WITHIN(kEps, 10.0f, b[1].temperature_c);
    TEST_ASSERT_FALSE(b[2].valid);
    TEST_ASSERT_TRUE(b[3].valid);
    TEST_ASSERT_FLOAT_WITHIN(kEps, 30.0f, b[3].temperature_c);
}

static void test_downsample_single_bucket_averages_the_window(void) {
    SampleHistory h(16);
    h.add(mk(0, 100.0f, 100.0f));  // outside the 1000 ms window
    h.add(mk(1500, 10.0f, 20.0f));
    h.add(mk(1800, 20.0f, 40.0f));
    h.add(mk(2000, 30.0f, 60.0f));

    Bucket b[1];
    TEST_ASSERT_EQUAL_size_t(1, h.downsample(1000, b, 1));
    TEST_ASSERT_TRUE(b[0].valid);
    TEST_ASSERT_FLOAT_WITHIN(kEps, 20.0f, b[0].temperature_c);
    TEST_ASSERT_FLOAT_WITHIN(kEps, 40.0f, b[0].humidity_pct);
}

static void test_downsample_with_more_buckets_than_samples(void) {
    SampleHistory h(16);
    h.add(mk(800, 1.0f, 10.0f));   // age 200 -> bucket 7
    h.add(mk(900, 2.0f, 20.0f));   // age 100 -> bucket 8
    h.add(mk(1000, 3.0f, 30.0f));  // age   0 -> bucket 9

    Bucket b[10];
    TEST_ASSERT_EQUAL_size_t(3, h.downsample(1000, b, 10));
    for (size_t i = 0; i < 7; ++i) TEST_ASSERT_FALSE(b[i].valid);
    TEST_ASSERT_FLOAT_WITHIN(kEps, 1.0f, b[7].temperature_c);
    TEST_ASSERT_FLOAT_WITHIN(kEps, 2.0f, b[8].temperature_c);
    TEST_ASSERT_FLOAT_WITHIN(kEps, 3.0f, b[9].temperature_c);
}

static void test_downsample_zero_buckets_writes_nothing(void) {
    SampleHistory h(4);
    h.add(mk(1000, 20.0f, 50.0f));

    Bucket canary{-1.0f, -1.0f, true};
    TEST_ASSERT_EQUAL_size_t(0, h.downsample(1000, &canary, 0));
    TEST_ASSERT_TRUE(canary.valid);  // untouched
    TEST_ASSERT_FLOAT_WITHIN(kEps, -1.0f, canary.temperature_c);
}

static void test_downsample_zero_window_keeps_only_the_newest_instant(void) {
    SampleHistory h(4);
    h.add(mk(999, 10.0f, 10.0f));
    h.add(mk(1000, 20.0f, 50.0f));

    Bucket b[3];
    TEST_ASSERT_EQUAL_size_t(1, h.downsample(0, b, 3));
    TEST_ASSERT_FALSE(b[0].valid);
    TEST_ASSERT_FALSE(b[1].valid);
    TEST_ASSERT_TRUE(b[2].valid);
    TEST_ASSERT_FLOAT_WITHIN(kEps, 20.0f, b[2].temperature_c);
}

static void test_downsample_duplicate_timestamps_are_averaged_together(void) {
    SampleHistory h(8);
    h.add(mk(1000, 10.0f, 20.0f));
    h.add(mk(1000, 20.0f, 40.0f));
    h.add(mk(1000, 30.0f, 60.0f));

    Bucket b[4];
    TEST_ASSERT_EQUAL_size_t(1, h.downsample(1000, b, 4));
    TEST_ASSERT_TRUE(b[3].valid);
    TEST_ASSERT_FLOAT_WITHIN(kEps, 20.0f, b[3].temperature_c);
    TEST_ASSERT_FLOAT_WITHIN(kEps, 40.0f, b[3].humidity_pct);
}

static void test_downsample_buckets_by_timestamp_not_arrival_order(void) {
    // Samples are assumed to arrive in non-decreasing time order; if they do
    // not, at() stays in arrival order but bucketing still keys off t_ms. The
    // window anchor is the LAST ADDED sample, which is what shifts here.
    SampleHistory h(8);
    h.add(mk(1000, 30.0f, 60.0f));
    h.add(mk(800, 10.0f, 20.0f));  // out of order, still the window anchor

    TEST_ASSERT_EQUAL_UINT32(1000, h.at(0).t_ms);  // arrival order preserved
    TEST_ASSERT_EQUAL_UINT32(800, h.newest().t_ms);

    Bucket b[4];
    // Anchor t=800, window 1000: t=1000 is "in the future", age wraps huge and
    // it drops out. Only t=800 survives, in the newest bucket.
    TEST_ASSERT_EQUAL_size_t(1, h.downsample(1000, b, 4));
    TEST_ASSERT_TRUE(b[3].valid);
    TEST_ASSERT_FLOAT_WITHIN(kEps, 10.0f, b[3].temperature_c);
}

// ---------------------------------------------------------------------------
// millis() rollover
// ---------------------------------------------------------------------------

static void test_timestamp_wraparound_keeps_elapsed_time_correct(void) {
    SampleHistory h(8);
    // Straddle the 2^32 ms rollover: samples before the wrap, newest after it.
    h.add(mk(0xFFFFF000u, 99.0f, 99.0f));  // 4146 ms old -> outside the window
    h.add(mk(0xFFFFFF00u, 10.0f, 20.0f));  //  306 ms old -> bucket 6
    h.add(mk(0x00000032u, 30.0f, 60.0f));  //    0 ms old -> bucket 9

    TEST_ASSERT_EQUAL_UINT32(0x00000032u, h.newest().t_ms);

    Bucket b[10];
    TEST_ASSERT_EQUAL_size_t(2, h.downsample(1000, b, 10));
    TEST_ASSERT_TRUE(b[6].valid);
    TEST_ASSERT_FLOAT_WITHIN(kEps, 10.0f, b[6].temperature_c);
    TEST_ASSERT_TRUE(b[9].valid);
    TEST_ASSERT_FLOAT_WITHIN(kEps, 30.0f, b[9].temperature_c);
    for (size_t i = 0; i < 10; ++i) {
        if (i != 6 && i != 9) TEST_ASSERT_FALSE(b[i].valid);
    }
}

static void test_wraparound_does_not_disturb_ring_ordering(void) {
    SampleHistory h(4);
    h.add(mk(0xFFFFFFFEu, 1.0f, 1.0f));
    h.add(mk(0xFFFFFFFFu, 2.0f, 2.0f));
    h.add(mk(0x00000000u, 3.0f, 3.0f));
    h.add(mk(0x00000001u, 4.0f, 4.0f));

    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFEu, h.at(0).t_ms);
    TEST_ASSERT_EQUAL_UINT32(0x00000001u, h.at(3).t_ms);
    TEST_ASSERT_FLOAT_WITHIN(kEps, 1.0f, h.oldest().temperature_c);
    TEST_ASSERT_FLOAT_WITHIN(kEps, 4.0f, h.newest().temperature_c);
}

// ---------------------------------------------------------------------------
// Realistic shape: one hour of samples onto an 800 px chart
// ---------------------------------------------------------------------------

static void test_hour_of_samples_onto_chart_width(void) {
    const uint32_t kWindowMs = 60u * 60u * 1000u;
    const size_t   kPixels   = 800;
    SampleHistory  h(720);  // one hour at 5 s intervals

    for (uint32_t i = 0; i < 720; ++i) h.add(mk(i * 5000u, 20.0f + (i % 10) * 0.1f, 50.0f));

    std::vector<Bucket> b(kPixels);
    const size_t valid = h.downsample(kWindowMs, b.data(), kPixels);
    // 720 samples cannot fill 800 buckets; every valid bucket must be in range.
    TEST_ASSERT_TRUE(valid > 0);
    TEST_ASSERT_TRUE(valid <= 720);
    for (size_t i = 0; i < kPixels; ++i) {
        if (!b[i].valid) continue;
        TEST_ASSERT_TRUE(b[i].temperature_c >= 20.0f - kEps);
        TEST_ASSERT_TRUE(b[i].temperature_c <= 20.9f + kEps);
        TEST_ASSERT_FLOAT_WITHIN(kEps, 50.0f, b[i].humidity_pct);
    }
    TEST_ASSERT_TRUE(b[kPixels - 1].valid);  // newest sample anchors the right edge
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_empty_buffer);
    RUN_TEST(test_empty_downsample_is_all_invalid);

    RUN_TEST(test_partial_fill_is_oldest_first);
    RUN_TEST(test_exact_fill_to_capacity);
    RUN_TEST(test_overflow_evicts_oldest);
    RUN_TEST(test_ordering_holds_across_many_wraps);
    RUN_TEST(test_capacity_of_zero_is_a_sink);
    RUN_TEST(test_clear_resets_and_buffer_is_reusable);

    RUN_TEST(test_range_single_sample);
    RUN_TEST(test_range_many_samples);
    RUN_TEST(test_range_with_negative_temperatures);
    RUN_TEST(test_range_all_equal_values);
    RUN_TEST(test_range_ignores_evicted_samples);

    RUN_TEST(test_downsample_even_spread_lands_one_per_bucket);
    RUN_TEST(test_downsample_bucket_holding_several_samples_is_the_mean);
    RUN_TEST(test_downsample_marks_gaps_invalid);
    RUN_TEST(test_downsample_excludes_samples_older_than_window);
    RUN_TEST(test_downsample_single_bucket_averages_the_window);
    RUN_TEST(test_downsample_with_more_buckets_than_samples);
    RUN_TEST(test_downsample_zero_buckets_writes_nothing);
    RUN_TEST(test_downsample_zero_window_keeps_only_the_newest_instant);
    RUN_TEST(test_downsample_duplicate_timestamps_are_averaged_together);
    RUN_TEST(test_downsample_buckets_by_timestamp_not_arrival_order);

    RUN_TEST(test_timestamp_wraparound_keeps_elapsed_time_correct);
    RUN_TEST(test_wraparound_does_not_disturb_ring_ordering);

    RUN_TEST(test_hour_of_samples_onto_chart_width);

    return UNITY_END();
}
