#ifndef SKYBLIP_HAL_RF_H
#define SKYBLIP_HAL_RF_H

#include <cstdint>

#include "core/util/result.h"

namespace skyblip::hal {

enum class RfMode : uint8_t { Idle, RxMband, RxOband };

// One armed dwell, in absolute microseconds on the clock the executor reads.
// A dwell that cannot complete before end_us is refused, never truncated on air:
// ADS-L 4 SRD-860 issue 2 §C.5 requires a burst to finish inside its slot.
//
// A dwell may carry one transmission: the executor receives on freq_hz until
// tx_at_us, puts the burst on air, and returns to receiving for the remainder.
// Splitting that into two plans would leave the slot deaf between them.
struct RfPlan {
    uint64_t start_us{0};
    uint64_t end_us{0};
    RfMode mode{RfMode::Idle};
    uint32_t freq_hz{0};
    // What the receiver's sync detector matches during this dwell, and how many
    // bytes it reads once it has. A window can be shared by two systems
    // (core/protocol/air.h), so this is what the dwell listens FOR, never which
    // protocol it expects to get.
    const uint8_t* sync{nullptr};
    uint8_t sync_bits{0};
    uint8_t rx_len{0};
    const uint8_t* tx{nullptr};
    uint8_t tx_len{0};
    uint64_t tx_at_us{0};
    // §D.3: the MAC is CSMA with listen-before-talk. Sampling the carrier and
    // backing off are hardware timing, so they belong to the executor; whether
    // the rule applies to this burst is policy (§D.3 allows a forced
    // transmission after 3000 ms of failed attempts).
    bool lbt{false};
    int8_t lbt_threshold_dbm{-90};
    uint32_t backoff_min_ms{15};
    uint32_t backoff_max_ms{250};
};

class Rf {
   public:
    virtual ~Rf() = default;

    virtual Status begin() = 0;
    virtual Status arm(const RfPlan& plan) = 0;
    virtual void abort() = 0;
};

}  // namespace skyblip::hal

#endif
