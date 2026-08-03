// core/timing/timing_stats.h: the bench-facing accumulator behind G6, the
// launch gate's "slot timing proven on silicon". It has exactly two inputs -
// the PPS edge hardware/boards already latches every pass, and the outcome an
// armed dwell reports back against the deadline core/timing::Transmitter chose
// - so nothing here forms a second opinion about either. ADS-L 4 SRD-860
// issue 2 §C.5 is the budget both halves are read against.
#ifndef SKYBLIP_CORE_TIMING_TIMING_STATS_H
#define SKYBLIP_CORE_TIMING_TIMING_STATS_H

#include <cstdint>

#include "core/timing/slot.h"

namespace skyblip::timing {

// Seven buckets, signed and symmetric about zero. The edges are the three
// figures that already bound this budget, not a round number:
//  - kRetuneUs, the SX1262's own share of it. Semtech DS.SX1261-2.W.APP rev
//    2.1 §3.5.2 table 3-7, TS_HOP: 30 us typical for a 10 MHz synthesizer
//    step; the M-band hop here is 200 kHz (kMband1Hz - kMband0Hz), so the chip
//    settles inside this bucket with room left over. An error this small is
//    the radio doing exactly what its own datasheet promises, not a PPS or a
//    scheduling fault.
//  - kHopGuardMs (core/timing/slot.h), the smaller of the two slot-map
//    guards, paid at every M-band channel change.
//  - kJitterGuardMs (core/timing/slot.h), the guard both band edges keep, and
//    the figure core/timing/transmit.h's kCompletionSlackMs is drawn from.
//    Past this the error is not something the slot map budgeted for.
// A count outside the widest pair still lands somewhere: saturated into the
// outermost bucket, never wrapped, because a fault we cannot bound is one this
// class must not misreport as a smaller one.
class SlotTimingStats {
   public:
    static constexpr int32_t kRetuneUs = 30;
    static constexpr int32_t kHopGuardUs = kHopGuardMs * 1000;
    static constexpr int32_t kJitterGuardUs = kJitterGuardMs * 1000;
    static constexpr int kBuckets = 7;

    // Less than two nominal seconds, more than any jitter this budget could
    // ever call ordinary: a gap this wide means at least one PPS edge went
    // missing, which is holdover, not a sample for the interval histogram.
    static constexpr int64_t kHoldoverGapUs = 1500000;
    static constexpr int64_t kNominalSecondUs = 1000000;

    // The PPS edge exactly as hardware/boards latched it, and the lock flag
    // read alongside it. Safe to call every service pass: only a genuinely
    // new edge value, or a transition of the lock flag, moves anything.
    void record_edge(uint64_t edge_us, bool locked);

    // The signed microseconds between an armed deadline and the instant the
    // outcome it was armed for actually landed. Both arguments are absolute
    // instants on the clock hal::Rf deadlines are armed against, so this needs
    // no phase and no knowledge of which second, or which half of slot 1's
    // wrap, either one fell in.
    void record_dwell_phase(int64_t error_us);

    // A dwell that carried a plan and closed without the outcome it was armed
    // for. Counted apart from the phase buckets above: a plan that never
    // landed has no landing instant to measure against.
    void record_missed();
    // A plan the policy never armed at all: the hour's air-time budget was
    // already spent (core/timing/transmit.h Transmitter::Attempt::over_budget).
    void record_refused();

    uint32_t pps_bucket(int index) const { return bucket_at(pps_buckets_, index); }
    uint32_t dwell_bucket(int index) const { return bucket_at(dwell_buckets_, index); }
    uint32_t pps_samples() const { return pps_samples_; }
    uint32_t dwell_samples() const { return dwell_samples_; }
    int32_t pps_worst_us() const { return pps_worst_us_; }
    int32_t dwell_worst_us() const { return dwell_worst_us_; }
    uint32_t holdover_events() const { return holdover_; }
    uint32_t missed() const { return missed_; }
    uint32_t refused() const { return refused_; }

    // Exposed for the bench arithmetic to be checked directly, not only
    // through record_edge()/record_dwell_phase()'s side effects.
    static int bucket_of(int64_t error_us);

   private:
    static uint32_t bucket_at(const uint32_t* buckets, int index) {
        return (index >= 0 && index < kBuckets) ? buckets[static_cast<unsigned>(index)] : 0;
    }
    static void bump(uint32_t* buckets, uint32_t& samples, int32_t& worst_us, int64_t error_us);

    uint32_t pps_buckets_[kBuckets]{};
    uint32_t dwell_buckets_[kBuckets]{};
    uint32_t pps_samples_{0};
    uint32_t dwell_samples_{0};
    int32_t pps_worst_us_{0};
    int32_t dwell_worst_us_{0};
    uint32_t holdover_{0};
    uint32_t missed_{0};
    uint32_t refused_{0};
    uint64_t prev_edge_us_{0};
    bool have_prev_edge_{false};
};

}  // namespace skyblip::timing

#endif
