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

namespace {
// 10^(-d/10) in units of 1/2^20, for d decibels below the loudest reading in
// the window. A mean of at most kMaxSamples terms cannot be pulled a whole
// decibel by anything further down than the last entry, so it contributes zero.
constexpr uint32_t kPowerRatioQ20[] = {1048576, 832914, 661607, 525533, 417446, 331589,
                                       263390,  209218, 166188, 132008, 104858, 83291,
                                       66161,   52553,  41745,  33159,  26339};
constexpr int kPowerRatioSpanDb = static_cast<int>(sizeof(kPowerRatioQ20) / sizeof(uint32_t));
}  // namespace

int8_t CarrierSense::mean_dbm(const int8_t* samples, uint8_t n) {
    if (samples == nullptr || n == 0) return 0;
    if (n > kMaxSamples) n = kMaxSamples;
    int8_t peak = samples[0];
    for (uint8_t i = 1; i < n; i++)
        if (samples[i] > peak) peak = samples[i];
    uint32_t sum = 0;
    for (uint8_t i = 0; i < n; i++) {
        const int down = static_cast<int>(peak) - static_cast<int>(samples[i]);
        if (down < kPowerRatioSpanDb) sum += kPowerRatioQ20[down];
    }
    const uint32_t mean = sum / n;
    int down = 0;
    while (down + 1 < kPowerRatioSpanDb && kPowerRatioQ20[down + 1] >= mean) down++;
    return static_cast<int8_t>(static_cast<int>(peak) - down);
}

int8_t NoiseFloor::threshold_dbm(uint8_t retry) const {
    int32_t level =
        static_cast<int32_t>(dbm()) + kClearMarginDb + static_cast<int32_t>(retry) * kRetryStepDb;
    if (level > kThresholdCeilingDbm) level = kThresholdCeilingDbm;
    if (level < -128) level = -128;
    return static_cast<int8_t>(level);
}

void AirTime::spend(uint32_t now_ms, uint32_t air_ms) {
    if (!started_) {
        started_ = true;
        head_start_ms_ = now_ms;
    } else if (now_ms - head_start_ms_ >= kWindowMs && head_start_ms_ - now_ms >= kWindowMs) {
        // A gap wider than the window in either direction: nothing the ring holds
        // is inside the hour any more. Only one of the two differences can be
        // under the window, so an instant a little BEHIND the head is a caller
        // repeating a pass, not a gap, and it lands in the head bucket below.
        for (uint8_t i = 0; i < kBuckets; i++) ms_[i] = 0;
        head_ = 0;
        head_start_ms_ = now_ms;
    } else {
        while (now_ms - head_start_ms_ >= kBucketMs && now_ms - head_start_ms_ < kWindowMs) {
            head_ = static_cast<uint8_t>(head_ + 1 == kBuckets ? 0 : head_ + 1);
            ms_[head_] = 0;
            head_start_ms_ += kBucketMs;
        }
    }
    ms_[head_] += air_ms;
    total_ms_ += air_ms;
    bursts_++;
}

uint32_t AirTime::window_ms(uint32_t now_ms) const {
    if (!started_) return 0;
    uint32_t sum = 0;
    uint32_t start_ms = head_start_ms_;
    for (uint8_t back = 0; back < kBuckets; back++) {
        const uint8_t at = static_cast<uint8_t>((head_ + kBuckets - back) % kBuckets);
        if (now_ms - start_ms < kWindowMs) sum += ms_[at];
        start_ms -= kBucketMs;
    }
    return sum;
}

}  // namespace skyblip::timing
