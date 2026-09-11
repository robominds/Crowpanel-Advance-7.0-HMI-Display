// Fixed-capacity ring buffer of timestamped temperature/humidity samples, plus
// downsampling of that history onto a chart's pixel columns.
//
// This unit backs the live readout and the one-hour scrolling chart. It is
// deliberately free of Arduino, ESP-IDF, LVGL and FreeRTOS headers so the
// retention and bucketing logic - the part that is easy to get wrong and
// painful to debug on the panel - can be tested on the host.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic).

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace history {

struct Sample {
    uint32_t t_ms;         // millis() timestamp when captured
    float    temperature_c;
    float    humidity_pct;
};

class SampleHistory {
  public:
    // A capacity of 0 is legal and yields a sink that discards every sample, so
    // a capacity computed at runtime (window / interval) can never trap.
    explicit SampleHistory(size_t capacity);

    void   add(const Sample& s);
    size_t size() const { return size_; }
    size_t capacity() const { return capacity_; }
    bool   empty() const { return size_ == 0; }
    void   clear();

    // Oldest-first indexing. index 0 == oldest retained sample.
    // Caller must ensure index < size(); out-of-range access is a programming
    // error, not a runtime condition, so it asserts rather than throwing.
    const Sample& at(size_t index) const;
    const Sample& newest() const;
    const Sample& oldest() const;

    bool temperatureRange(float& min_c, float& max_c) const;

    struct Bucket {
        float temperature_c;
        float humidity_pct;
        bool  valid;
    };

    // Largest bucket_count downsample() will accept. Its scratch arrays are
    // static rather than allocated per call - on the panel downsample() runs
    // every ten seconds and the blocks are small enough to land in the internal
    // SRAM the Wi-Fi stack needs - and a static needs a compile-time bound. One
    // bucket per pixel of the widest panel this firmware drives is the most any
    // caller can sensibly ask for; the chart itself uses 380 of them.
    static constexpr size_t MAX_BUCKETS = 800;

    // Averages the samples within `window_ms` of the newest one into
    // `bucket_count` buckets, oldest-left, and returns how many buckets got at
    // least one sample. Buckets with no samples are marked invalid; they are
    // gaps, not interpolated.
    //
    // Asking for more than MAX_BUCKETS buckets fills none of them and returns
    // 0. It does not quietly fill the first MAX_BUCKETS, because a truncated
    // chart looks like real data.
    //
    // Not reentrant: the scratch arrays are shared. Call it from one context
    // (on the panel, the LVGL thread).
    size_t downsample(uint32_t window_ms, Bucket* out, size_t bucket_count) const;

  private:
    std::vector<Sample> buf_;
    size_t              capacity_ = 0;
    size_t              head_     = 0;  // slot holding the oldest sample
    size_t              size_     = 0;
};

}  // namespace history
