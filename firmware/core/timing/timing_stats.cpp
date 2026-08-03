#include "core/timing/timing_stats.h"

#include <cstdint>
#include <limits>

namespace skyblip::timing {

namespace {
int32_t clamp_i32(int64_t v) {
    if (v > std::numeric_limits<int32_t>::max()) return std::numeric_limits<int32_t>::max();
    if (v < std::numeric_limits<int32_t>::min()) return std::numeric_limits<int32_t>::min();
    return static_cast<int32_t>(v);
}
}  // namespace

int SlotTimingStats::bucket_of(int64_t error_us) {
    const int64_t magnitude = error_us < 0 ? -error_us : error_us;
    if (magnitude < kRetuneUs) return 3;
    if (magnitude < kHopGuardUs) return error_us < 0 ? 2 : 4;
    if (magnitude < kJitterGuardUs) return error_us < 0 ? 1 : 5;
    return error_us < 0 ? 0 : 6;
}

void SlotTimingStats::bump(uint32_t* buckets, uint32_t& samples, int32_t& worst_us,
                           int64_t error_us) {
    buckets[bucket_of(error_us)]++;
    samples++;
    const int64_t magnitude = error_us < 0 ? -error_us : error_us;
    const int64_t worst_magnitude = worst_us < 0 ? -static_cast<int64_t>(worst_us) : worst_us;
    if (magnitude > worst_magnitude) worst_us = clamp_i32(error_us);
}

void SlotTimingStats::record_edge(uint64_t edge_us, bool locked) {
    if (!locked) {
        if (have_prev_edge_) holdover_++;
        have_prev_edge_ = false;
        return;
    }
    if (!have_prev_edge_) {
        prev_edge_us_ = edge_us;
        have_prev_edge_ = true;
        return;
    }
    if (edge_us == prev_edge_us_) return;

    const int64_t delta_us = static_cast<int64_t>(edge_us) - static_cast<int64_t>(prev_edge_us_);
    prev_edge_us_ = edge_us;
    // A clock that ran backwards or skipped an edge is not describable against
    // the nominal second at all: holdover, so it cannot masquerade as an
    // enormous phase error in the histogram below.
    if (delta_us <= 0 || delta_us > kHoldoverGapUs) {
        holdover_++;
        return;
    }
    bump(pps_buckets_, pps_samples_, pps_worst_us_, delta_us - kNominalSecondUs);
}

void SlotTimingStats::record_dwell_phase(int64_t error_us) {
    bump(dwell_buckets_, dwell_samples_, dwell_worst_us_, error_us);
}

void SlotTimingStats::record_missed() { missed_++; }
void SlotTimingStats::record_refused() { refused_++; }

}  // namespace skyblip::timing
