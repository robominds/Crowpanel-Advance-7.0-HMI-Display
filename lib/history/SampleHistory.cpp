// Implementation of the SampleHistory ring buffer and chart downsampler.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic).

#include "SampleHistory.h"

#include <cassert>

namespace history {

SampleHistory::SampleHistory(size_t capacity) : buf_(capacity), capacity_(capacity) {}

void SampleHistory::add(const Sample& s) {
    if (capacity_ == 0) return;  // sink; see the constructor comment

    if (size_ < capacity_) {
        buf_[(head_ + size_) % capacity_] = s;
        ++size_;
    } else {
        // Full: the write lands on the oldest slot, which then becomes the
        // newest, so the head has to step forward with it.
        buf_[head_] = s;
        head_       = (head_ + 1) % capacity_;
    }
}

void SampleHistory::clear() {
    head_ = 0;
    size_ = 0;
}

const Sample& SampleHistory::at(size_t index) const {
    assert(index < size_);
    return buf_[(head_ + index) % capacity_];
}

const Sample& SampleHistory::newest() const { return at(size_ - 1); }

const Sample& SampleHistory::oldest() const { return at(0); }

bool SampleHistory::temperatureRange(float& min_c, float& max_c) const {
    if (size_ == 0) return false;

    min_c = max_c = at(0).temperature_c;
    for (size_t i = 1; i < size_; ++i) {
        const float t = at(i).temperature_c;
        if (t < min_c) min_c = t;
        if (t > max_c) max_c = t;
    }
    return true;
}

size_t SampleHistory::downsample(uint32_t window_ms, Bucket* out, size_t bucket_count) const {
    if (bucket_count == 0) return 0;

    for (size_t i = 0; i < bucket_count; ++i) out[i] = Bucket{0.0f, 0.0f, false};

    // Every bucket stays invalid rather than filling the first MAX_BUCKETS of
    // them: a silently truncated chart is indistinguishable from a real one.
    if (bucket_count > MAX_BUCKETS) return 0;
    if (size_ == 0) return 0;

    // Counts live in their own array rather than being folded into the output so
    // that bucketing depends only on t_ms. A streaming pass could keep the count
    // in a local, but only by assuming samples arrive in time order - and a
    // sensor read that straggles would then silently corrupt a mean.
    //
    // Static, not local vectors: three vectors totalling ~7.6 KB, allocated
    // and freed on every call, churn the internal SRAM heap that the network
    // stack shares - none of them is large enough to be pushed out to
    // PSRAM by the allocator's size threshold. Only
    // the first bucket_count entries of each are ever touched, and they are
    // cleared here rather than relying on what the previous call left behind.
    // This is what makes downsample() non-reentrant; see the header.
    static uint32_t counts[MAX_BUCKETS];
    static double   sum_t[MAX_BUCKETS];
    static double   sum_h[MAX_BUCKETS];
    for (size_t i = 0; i < bucket_count; ++i) {
        counts[i] = 0;
        sum_t[i]  = 0.0;
        sum_h[i]  = 0.0;
    }

    const uint32_t anchor = newest().t_ms;

    for (size_t i = 0; i < size_; ++i) {
        const Sample& s = at(i);

        // Unsigned subtraction, so the gap stays correct straight through the
        // millis() rollover at 2^32 ms (~49.7 days) as long as the real elapsed
        // time is under that. The flip side: a sample timestamped AFTER the
        // anchor reads as nearly 2^32 ms old and drops out of the window, which
        // is the behaviour we want for a stray out-of-order or future sample.
        const uint32_t age = anchor - s.t_ms;
        if (age > window_ms) continue;

        // Buckets run oldest-left to newest-right, anchored on the newest
        // sample so the live trace always touches the right edge of the chart.
        size_t from_newest = 0;
        if (window_ms != 0) {
            from_newest = static_cast<size_t>((static_cast<uint64_t>(age) * bucket_count) /
                                              window_ms);
            if (from_newest >= bucket_count) from_newest = bucket_count - 1;
        }
        const size_t idx = bucket_count - 1 - from_newest;

        sum_t[idx] += s.temperature_c;
        sum_h[idx] += s.humidity_pct;
        ++counts[idx];
    }

    size_t valid = 0;
    for (size_t i = 0; i < bucket_count; ++i) {
        if (counts[i] == 0) continue;
        out[i].temperature_c = static_cast<float>(sum_t[i] / counts[i]);
        out[i].humidity_pct  = static_cast<float>(sum_h[i] / counts[i]);
        out[i].valid         = true;
        ++valid;
    }
    return valid;
}

}  // namespace history
