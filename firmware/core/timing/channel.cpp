#include "core/timing/channel.h"

namespace skyblip::timing {

// Integer arithmetic on hundredths of a dBm. Truncation toward zero would stall
// the average one step short of every target, so a residual that is too small
// to weigh still moves the average by one hundredth.
void NoiseFloor::sample(int8_t rssi_dbm) {
    samples_++;
    const int32_t residual = static_cast<int32_t>(rssi_dbm) * 100 - centi_dbm_;
    int32_t step = residual * kWeightPercent / 100;
    if (step == 0) step = residual > 0 ? 1 : (residual < 0 ? -1 : 0);
    centi_dbm_ += step;
}

int8_t NoiseFloor::dbm() const {
    const int32_t rounded = centi_dbm_ >= 0 ? (centi_dbm_ + 50) / 100 : (centi_dbm_ - 50) / 100;
    if (rounded < -128) return -128;
    if (rounded > 127) return 127;
    return static_cast<int8_t>(rounded);
}

int8_t NoiseFloor::threshold_dbm(uint8_t retry) const {
    int32_t level =
        static_cast<int32_t>(dbm()) + kClearMarginDb + static_cast<int32_t>(retry) * kRetryStepDb;
    if (level > kThresholdCeilingDbm) level = kThresholdCeilingDbm;
    if (level < -128) level = -128;
    return static_cast<int8_t>(level);
}

void AirTime::spend(uint32_t now_ms, uint32_t air_ms) {
    const uint32_t minute = minute_of(now_ms);
    const uint8_t slot = static_cast<uint8_t>(minute % kBuckets);
    if (stamp_[slot] != minute + 1) {
        stamp_[slot] = minute + 1;
        ms_[slot] = 0;
    }
    ms_[slot] += air_ms;
    total_ms_ += air_ms;
    bursts_++;
}

uint32_t AirTime::window_ms(uint32_t now_ms) const {
    const uint32_t minute = minute_of(now_ms);
    uint32_t sum = 0;
    for (uint8_t i = 0; i < kBuckets; i++) {
        if (stamp_[i] == 0) continue;
        if (minute - (stamp_[i] - 1) < kBuckets) sum += ms_[i];
    }
    return sum;
}

}  // namespace skyblip::timing
