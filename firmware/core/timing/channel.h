// What the channel is worth before we speak on it, and what we have already
// spent on it: ADS-L 4 SRD-860 issue 2 §D.3 (CSMA, listen before talk) and
// EN 300 220-2 band h1.4 (868.0-868.6 MHz, 25 mW e.r.p., 1% duty cycle).
#ifndef SKYBLIP_CORE_TIMING_CHANNEL_H
#define SKYBLIP_CORE_TIMING_CHANNEL_H

#include <cstdint>

namespace skyblip::timing {

// The level an empty channel sits at, here and now. A fixed carrier-sense
// threshold is a device that goes silent at a noisy site and never says why, so
// the threshold is a measurement plus a margin instead of a constant.
class NoiseFloor {
   public:
    // OGN's figures on the same silicon (oss/nrf52-ogn-tracker
    // src/ogn-radio.cpp:77-78, 778, 845-851): the average starts at -105 dBm,
    // every live sample moves it by 0.05 of the residual, the channel is clear
    // when the live level is under the average plus 10 dB, and each failed
    // attempt buys 3 dB more tolerance.
    static constexpr int8_t kSeedDbm = -105;
    static constexpr int32_t kWeightPercent = 5;
    static constexpr int8_t kClearMarginDb = 10;
    static constexpr int8_t kRetryStepDb = 3;
    // Nothing this band is licensed for reaches the receiver at 0 dBm, so an
    // escalation that got there has stopped being carrier sense.
    static constexpr int8_t kThresholdCeilingDbm = 0;

    void sample(int8_t rssi_dbm);

    int8_t dbm() const;
    int8_t threshold_dbm(uint8_t retry = 0) const;
    uint32_t samples() const { return samples_; }

   private:
    int32_t centi_dbm_{static_cast<int32_t>(kSeedDbm) * 100};
    uint32_t samples_{0};
};

// Air time per rolling hour, which is the window EN 300 220-2 measures a duty
// cycle over. Listen-before-talk plus backoff is our route out of the 1% limit,
// but a route out is an argument and this is the evidence: sixty one-minute
// buckets, each one stamped with the minute it holds, so a bucket that fell out
// of the window contributes nothing without anything having to sweep it.
class AirTime {
   public:
    static constexpr uint32_t kWindowMs = 3600000;
    static constexpr uint8_t kBuckets = 60;
    static constexpr uint32_t kBucketMs = kWindowMs / kBuckets;
    static constexpr uint32_t kLimitPermille = 10;
    static constexpr uint32_t kBudgetMs = kWindowMs / 1000 * kLimitPermille;

    void spend(uint32_t now_ms, uint32_t air_ms);

    uint32_t window_ms(uint32_t now_ms) const;
    uint32_t permille(uint32_t now_ms) const { return window_ms(now_ms) * 1000u / kWindowMs; }
    bool may_spend(uint32_t now_ms, uint32_t air_ms) const {
        return window_ms(now_ms) + air_ms <= kBudgetMs;
    }

    uint32_t bursts() const { return bursts_; }
    uint32_t total_ms() const { return total_ms_; }

   private:
    static uint32_t minute_of(uint32_t now_ms) { return now_ms / kBucketMs; }

    uint32_t ms_[kBuckets]{};
    // The minute a bucket holds, plus one: zero is a bucket nothing was ever
    // spent in, which is not the same as the minute numbered zero.
    uint32_t stamp_[kBuckets]{};
    uint32_t bursts_{0};
    uint32_t total_ms_{0};
};

}  // namespace skyblip::timing

#endif
