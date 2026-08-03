// G6: the accumulator behind the bench PPS-to-lock histogram. Its two feeds -
// the latched PPS edge and an armed dwell's outcome - are exercised directly
// here on virtual time, exactly as core/timing/timing_stats.h promises: no
// heap, integer only, and no phase math to get wrong across the second wrap.
#include <limits>

#include "core/timing/timing_stats.h"
#include "doctest/doctest.h"

using namespace skyblip::timing;

TEST_CASE("timing_stats: a clean 1 Hz PPS train lands in the centre bucket") {
    SlotTimingStats stats;
    uint64_t edge_us = 1000000;
    stats.record_edge(edge_us, true);  // the first edge only seeds the clock
    for (int i = 0; i < 10; i++) {
        edge_us += 1000000;
        stats.record_edge(edge_us, true);
    }
    CHECK(stats.pps_samples() == 10);
    CHECK(stats.pps_bucket(3) == 10);
    for (int b = 0; b < SlotTimingStats::kBuckets; b++)
        if (b != 3) CHECK(stats.pps_bucket(b) == 0);
    CHECK(stats.pps_worst_us() == 0);
    CHECK(stats.holdover_events() == 0);
}

TEST_CASE("timing_stats: polling between edges never inflates the sample count") {
    SlotTimingStats stats;
    stats.record_edge(1000000, true);
    for (int i = 0; i < 50; i++) stats.record_edge(1000000, true);  // same edge, many passes
    stats.record_edge(2000000, true);
    CHECK(stats.pps_samples() == 1);
    CHECK(stats.pps_bucket(3) == 1);
}

// The three named edges, in order: inside the SX1262's own retune time (still
// the centre bucket - that error is the chip doing what its datasheet
// promises), past it but inside the hop guard, past that but inside the
// jitter guard, and past the whole budget.
TEST_CASE("timing_stats: a drifting edge walks the buckets outward, one guard at a time") {
    SlotTimingStats stats;
    uint64_t edge_us = 0;
    stats.record_edge(edge_us, true);

    const int64_t errors[] = {10, 200, 2000, 8000};
    const int expect_late[] = {3, 4, 5, 6};
    for (int64_t error : errors) {
        edge_us += static_cast<uint64_t>(1000000 + error);
        stats.record_edge(edge_us, true);
    }
    for (int b : expect_late) CHECK(stats.pps_bucket(b) == 1);
    CHECK(stats.pps_samples() == 4);
    CHECK(stats.pps_worst_us() == 8000);

    SlotTimingStats early;
    edge_us = 0;
    early.record_edge(edge_us, true);
    const int expect_early[] = {3, 2, 1, 0};
    for (int64_t error : errors) {
        edge_us += static_cast<uint64_t>(1000000 - error);
        early.record_edge(edge_us, true);
    }
    for (int b : expect_early) CHECK(early.pps_bucket(b) == 1);
    CHECK(early.pps_worst_us() == -8000);
}

TEST_CASE("timing_stats: a lost PPS becomes holdover, not a phase outlier") {
    SlotTimingStats stats;
    uint64_t edge_us = 0;
    stats.record_edge(edge_us, true);
    edge_us += 1000000;
    stats.record_edge(edge_us, true);
    REQUIRE(stats.pps_samples() == 1);

    // The receiver drops lock and stays down for a while: nothing to report,
    // every pass the same as the last.
    stats.record_edge(0, false);
    stats.record_edge(0, false);
    stats.record_edge(0, false);
    CHECK(stats.holdover_events() == 1);  // one transition, not one per pass

    // Ten seconds pass before it comes back. That whole gap must not become a
    // sample: the histogram has nothing honest to say about it.
    edge_us += 10 * 1000000;
    stats.record_edge(edge_us, true);
    CHECK(stats.pps_samples() == 1);
    for (int b = 0; b < SlotTimingStats::kBuckets; b++)
        if (b != 3) CHECK(stats.pps_bucket(b) == 0);

    // The second after reacquisition is measured normally again.
    edge_us += 1000000;
    stats.record_edge(edge_us, true);
    CHECK(stats.pps_samples() == 2);
    CHECK(stats.pps_bucket(3) == 2);
}

TEST_CASE("timing_stats: an edge gap while still reporting locked is holdover too") {
    SlotTimingStats stats;
    stats.record_edge(0, true);
    stats.record_edge(2200000, true);  // more than one edge went missing
    CHECK(stats.holdover_events() == 1);
    CHECK(stats.pps_samples() == 0);
}

// Slot 1's dwell opens at 800 ms and its tail runs to 200 ms past the second
// it started in (core/timing/slot.h kSlot1Wrap). An armed deadline just before
// the wrap and a landed instant just after it are still only a few thousand
// microseconds apart - not a second, and not undefined, because nothing here
// is derived from a phase.
TEST_CASE("timing_stats: the dwell-phase error is exact across the UTC second wrap") {
    SlotTimingStats stats;
    const uint64_t deadline_us = 999995000;
    const uint64_t landed_us = 1000001200;
    stats.record_dwell_phase(static_cast<int64_t>(landed_us) - static_cast<int64_t>(deadline_us));
    CHECK(stats.dwell_samples() == 1);
    CHECK(stats.dwell_worst_us() == 6200);
    CHECK(stats.dwell_bucket(SlotTimingStats::bucket_of(6200)) == 1);
}

TEST_CASE("timing_stats: an extreme error saturates the outer bucket, never indexes past it") {
    CHECK(SlotTimingStats::bucket_of(1000000000) == 6);
    CHECK(SlotTimingStats::bucket_of(-1000000000) == 0);

    SlotTimingStats stats;
    stats.record_dwell_phase(9000000000LL);  // far past anything this budget bounds
    CHECK(stats.dwell_bucket(6) == 1);
    CHECK(stats.dwell_worst_us() == std::numeric_limits<int32_t>::max());
}

TEST_CASE("timing_stats: missed and refused are counted apart from the phase histograms") {
    SlotTimingStats stats;
    stats.record_missed();
    stats.record_missed();
    stats.record_refused();
    CHECK(stats.missed() == 2);
    CHECK(stats.refused() == 1);
    CHECK(stats.dwell_samples() == 0);
    CHECK(stats.pps_samples() == 0);
    for (int b = 0; b < SlotTimingStats::kBuckets; b++) {
        CHECK(stats.dwell_bucket(b) == 0);
        CHECK(stats.pps_bucket(b) == 0);
    }
}

TEST_CASE("timing_stats: an out-of-range bucket index reads zero, not memory past the array") {
    SlotTimingStats stats;
    stats.record_dwell_phase(0);
    CHECK(stats.dwell_bucket(-1) == 0);
    CHECK(stats.dwell_bucket(SlotTimingStats::kBuckets) == 0);
}
