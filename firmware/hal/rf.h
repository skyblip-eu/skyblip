#ifndef SKYBLIP_HAL_RF_H
#define SKYBLIP_HAL_RF_H

#include <cstdint>

#include "core/util/result.h"

namespace skyblip::hal {

enum class RfMode : uint8_t { Idle, RxMband, RxOband, TxMband };

// One armed dwell, in absolute microseconds on the clock the executor reads.
// A dwell that cannot complete before end_us is refused, never truncated on air:
// ADS-L 4 SRD-860 issue 2 §C.5 requires a burst to finish inside its slot.
struct RfPlan {
    uint64_t start_us{0};
    uint64_t end_us{0};
    RfMode mode{RfMode::Idle};
    uint32_t freq_hz{0};
    const uint8_t* tx{nullptr};
    uint8_t tx_len{0};
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
